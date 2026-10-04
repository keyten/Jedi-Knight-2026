/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Volumetric clouds (r_clouds) and their shadows (r_cloudShadows), see
// docs/rend2-volumetric-clouds.md and glsl/clouds_*.glsl.
//
// A cloud renderer of its own next to the froxel fog (tr_volumetric.cpp)
// and the atmosphere (tr_atmosphere.cpp): the froxel grid ends a few
// thousand units from the camera, clouds are kilometres away and above. The
// clouds are a spherical shell between two altitudes above the ground
// (r_cloudBase / r_cloudTop, metres; r_atmosphereUnitScale and
// r_atmosphereGroundZ give the world scale and the ground, with or without
// the atmosphere), ray marched only inside the shell, on sky pixels, at a
// reduced resolution (r_cloudScale) with a temporal history of its own.
//
// Frame (main view, first scene):
//   RB_CloudsBeginView        cloud shadow map (r_cloudShadows), before the
//                             froxel injection and the opaque surfaces
//   RB_SubmitRenderPass       ... atmosphere composite -> early sun
//                             (RB_CloudsDrawSunEarly) -> RB_CloudsComposite
//                             (march, resolve, composite) -> froxel composite
//   RB_RenderMainPass         sun rays, masked by the clouds
//
// Density: weather map (coverage, cloud type) x height profile x 3D shape
// noise, eroded by a 3D detail noise. The noise is generated on the GPU on
// the first frame (deterministic integer hash), histogram equalized on the
// CPU so r_cloudCoverage reads as a cloud fraction. No assets.
//
// Lighting: the sun (through the atmosphere's transmittance when it is
// composited), a dual lobe phase, a short light march for the self shadow,
// multiple scattering octaves, and an isotropic ambient from the sky. No
// dynamic lights.
//
// Legacy SP fx_cloudlayer (ET_CLOUD -> CG_Clouds -> RT_CLOUDS): with r_clouds
// and r_cloudLegacy the renderer takes the RT_CLOUDS entity of the main view
// (R_CloudsCaptureLegacy) instead of drawing its haze disc; see the doc for
// what its keys can mean. r_clouds 0 leaves that path untouched.

#include "tr_local.h"

#define CLOUD_SHAPE_SIZE		128
#define CLOUD_DETAIL_SIZE		32
#define CLOUD_WEATHER_SIZE		512
#define CLOUD_SHADOW_SIZE		512		// as clouds_shadow.glsl

#define CLOUD_PLANET_RADIUS		6360.0f	// km, as the atmosphere
// extinction (1/km) at r_cloudDensity 1 and full cloud: cumulus are 0.05 - 0.15 per metre
#define CLOUD_EXTINCTION		60.0f
#define CLOUD_BACK_LOBE_G		-0.3f
#define CLOUD_FORWARD_WEIGHT	0.8f

// history cuts (the reprojection is exact for rotations, these are teleports)
#define CLOUD_CUT_DISTANCE		4096.0f
#define CLOUD_CUT_TIME			250		// msec

typedef struct
{
	qboolean	valid;
	vec3_t		origin;
	float		radius;		// outer radius (radius key)
	float		inner;		// inner radius of a TUBE layer (random key)
	qboolean	tube;		// spawnflags 1
	qboolean	alt;		// spawnflags 2: gfx/world/haze2
} cloudLegacyLayer_t;

typedef struct
{
	float	base, top;					// km above the ground
	float	coverage, extinction;		// -, 1/km
	float	g;
	float	shapeTile, detailTile, weatherTile, detail;
	vec3_t	shapeOffset, detailOffset;	// tiles
	vec2_t	weatherOffset;
	float	maxDistance, lightLength, stepLength;
	int		steps, minSteps, shadowSteps, octaves;
	float	kmPerUnit, groundZ, groundAltitude;
	vec3_t	sunDir, sunColor;
	vec3_t	ambientSky, ambientGround;
	float	ambientScale;
	vec3_t	camera;						// cloud space, km
	int		legacyMode;					// 0 none, 1 hint, 2 literal
	cloudLegacyLayer_t legacy;
	float	typeScale;
	vec2_t	windVelocity;				// world units / s
	int		debug;
	qboolean atmosphere;
} cloudParams_t;

static struct
{
	qboolean	resources;
	int			scale;				// r_cloudScale at image creation
	int			marchWidth, marchHeight;

	qboolean	noiseReady;
	int			noiseBuilds;
	int			noiseMsec;

	// temporal history
	qboolean	historyValid;
	unsigned	historyFrame;
	const world_t *historyWorld;
	uint32_t	historyKey;
	int			historyViewport[4];
	vec3_t		historyOrigin;
	int			historyTime;
	matrix_t	historyViewProjection;
	int			current;			// history image written last
	unsigned	lastFrame;			// realFrameNumber + 1 of the last composite
	float		lastWeight;

	// "off" reason, printed once per map
	const char	*reason;
	const world_t *reasonWorld;

	// legacy cloud layer
	cloudLegacyLayer_t captured;
	int			capturedFrame;
	cloudLegacyLayer_t test;

	// shadows
	qboolean	shadowValid;
	const world_t *shadowWorld;
	unsigned	shadowFrame;
	vec4_t		shadowLookup[2];
	int			shadowBuilds;
	int			unitsChecked;		// 0 unknown, 1 ok, -1 too few texture units

	// last composite (sun ray mask, r_cloudInfo)
	vec4_t		lastUniforms[CLOUD_UNIFORM_VEC4S];
	matrix_t	lastInvViewProjection;
	qboolean	lastViewValid;		// this view's resolved clouds are in history[current]
	cloudParams_t last;
	qboolean	hasLast;
	int			marches;
} s_clouds;

/*
============================================================

Resources

============================================================
*/

qboolean R_CloudsEnabled( void )
{
	return (qboolean)(r_clouds && r_clouds->integer != 0);
}

qboolean R_CloudShadowsAvailable( void )
{
	if ( !R_CloudsEnabled() )
		return qfalse;
	if ( s_clouds.unitsChecked == 0 )
	{
		GLint fragmentUnits = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragmentUnits);
		GLint computeUnits = fragmentUnits;
		if ( R_VolumetricComputeAvailable() )
			qglGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &computeUnits);
		s_clouds.unitsChecked = (fragmentUnits > TB_CLOUDSHADOW && computeUnits > TB_CLOUDSHADOW) ? 1 : -1;
		if ( s_clouds.unitsChecked < 0 )
			ri.Printf(PRINT_WARNING, "r_cloudShadows: needs %d texture units (fragment %d, compute %d), off\n",
				TB_CLOUDSHADOW + 1, fragmentUnits, computeUnits);
	}
	return (qboolean)(s_clouds.unitsChecked > 0);
}

void R_CreateCloudImages( void )
{
	const cloudLegacyLayer_t test = s_clouds.test;
	Com_Memset(&s_clouds, 0, sizeof(s_clouds));
	s_clouds.test = test;

	tr.cloudShapeImage = NULL;
	tr.cloudDetailImage = NULL;
	tr.cloudWeatherImage = NULL;
	tr.cloudMarchImage = NULL;
	tr.cloudMarchDepthImage = NULL;
	tr.cloudShadowImage = NULL;
	for ( int i = 0; i < 2; i++ )
	{
		tr.cloudHistoryImage[i] = NULL;
		tr.cloudHistoryDepthImage[i] = NULL;
	}
	if ( !R_CloudsEnabled() )
		return;

	// tiling noise (repeat, mips): filled by RB_CloudsGenerateNoise
	tr.cloudShapeImage = R_CreateImage3D("*cloudShape", NULL,
		CLOUD_SHAPE_SIZE, CLOUD_SHAPE_SIZE, CLOUD_SHAPE_SIZE, GL_R8, IMGFLAG_MIPMAP, NULL);
	tr.cloudDetailImage = R_CreateImage3D("*cloudDetail", NULL,
		CLOUD_DETAIL_SIZE, CLOUD_DETAIL_SIZE, CLOUD_DETAIL_SIZE, GL_R8, IMGFLAG_MIPMAP, NULL);
	// a single layer 3D texture: mips and repeat with the 3D image path
	tr.cloudWeatherImage = R_CreateImage3D("*cloudWeather", NULL,
		CLOUD_WEATHER_SIZE, CLOUD_WEATHER_SIZE, 1, GL_RG8, IMGFLAG_MIPMAP, NULL);

	s_clouds.scale = Com_Clampi(1, 4, r_cloudScale->integer);
	if ( s_clouds.scale == 3 )
		s_clouds.scale = 4;
	s_clouds.marchWidth = (glConfig.vidWidth + s_clouds.scale - 1) / s_clouds.scale;
	s_clouds.marchHeight = (glConfig.vidHeight + s_clouds.scale - 1) / s_clouds.scale;

	// texelFetch only: no filtering needed
	const int flags = IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE;
	tr.cloudMarchImage = R_CreateImage("*cloudMarch", NULL, s_clouds.marchWidth, s_clouds.marchHeight,
		IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
	tr.cloudMarchDepthImage = R_CreateImage("*cloudMarchDepth", NULL, s_clouds.marchWidth, s_clouds.marchHeight,
		IMGTYPE_COLORALPHA, flags, GL_R16F);
	for ( int i = 0; i < 2; i++ )
	{
		tr.cloudHistoryImage[i] = R_CreateImage(va("*cloudHistory%d", i), NULL,
			s_clouds.marchWidth, s_clouds.marchHeight, IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
		tr.cloudHistoryDepthImage[i] = R_CreateImage(va("*cloudHistoryDepth%d", i), NULL,
			s_clouds.marchWidth, s_clouds.marchHeight, IMGTYPE_COLORALPHA, flags, GL_R16F);
	}

	// sun transmittance on the ground plane: bilinear, clamped
	tr.cloudShadowImage = R_CreateImage("*cloudShadow", NULL, CLOUD_SHADOW_SIZE, CLOUD_SHADOW_SIZE,
		IMGTYPE_COLORALPHA, flags, GL_R16F);
}

static FBO_t *R_CloudsCreateFbo( const char *name, image_t *color, image_t *distance )
{
	FBO_t *fbo = FBO_Create(name, color->width, color->height);
	FBO_Bind(fbo);
	FBO_AttachTextureImage(color, 0);
	if ( distance )
	{
		FBO_AttachTextureImage(distance, 1);
		const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		qglDrawBuffers(2, bufs);
	}
	else
		qglDrawBuffer(GL_COLOR_ATTACHMENT0);
	R_CheckFBO(fbo);
	return fbo;
}

void R_CreateCloudFBOs( void )
{
	tr.cloudNoiseFbo = NULL;
	tr.cloudMarchFbo = NULL;
	tr.cloudHistoryFbo[0] = tr.cloudHistoryFbo[1] = NULL;
	tr.cloudShadowFbo = NULL;
	if ( !tr.cloudMarchImage || !tr.renderFbo )
		return;

	// slices are attached per draw (RB_CloudsGenerateNoise)
	tr.cloudNoiseFbo = FBO_Create("_cloudNoise", CLOUD_WEATHER_SIZE, CLOUD_WEATHER_SIZE);
	tr.cloudMarchFbo = R_CloudsCreateFbo("_cloudMarch", tr.cloudMarchImage, tr.cloudMarchDepthImage);
	for ( int i = 0; i < 2; i++ )
		tr.cloudHistoryFbo[i] = R_CloudsCreateFbo(va("_cloudHistory%d", i),
			tr.cloudHistoryImage[i], tr.cloudHistoryDepthImage[i]);
	tr.cloudShadowFbo = R_CloudsCreateFbo("_cloudShadow", tr.cloudShadowImage, NULL);
	s_clouds.resources = qtrue;
}

/*
============================================================

Parameters

============================================================
*/

static float R_CloudsLuminance( const vec3_t c )
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

// fraction of a tile, wrapped in double precision (long sessions)
static float R_CloudsWrap( double x )
{
	return (float)(x - floor(x));
}

static const cloudLegacyLayer_t *R_CloudsLegacyLayer( void )
{
	// the entity of this or the last frame (captured by the front end)
	if ( s_clouds.captured.valid && tr.frameCount - s_clouds.capturedFrame <= 2 )
		return &s_clouds.captured;
	if ( s_clouds.test.valid )
		return &s_clouds.test;
	return NULL;
}

static void R_CloudsBuildParams( const trRefdef_t *refdef, const viewParms_t *view, cloudParams_t *p )
{
	Com_Memset(p, 0, sizeof(*p));

	const float unitScale = MAX(1e-4f, r_atmosphereUnitScale->value);	// metres per unit
	p->kmPerUnit = unitScale * 0.001f;
	p->groundZ = R_AtmosphereGroundZ();
	p->groundAltitude = r_atmosphereAltitude->value * 0.001f;

	p->base = MAX(0.0f, r_cloudBase->value) * 0.001f;
	p->top = MAX(p->base + 0.01f, r_cloudTop->value * 0.001f);
	p->coverage = Com_Clamp(0.0f, 1.0f, r_cloudCoverage->value);
	p->extinction = CLOUD_EXTINCTION * MAX(0.0f, r_cloudDensity->value);
	p->g = Com_Clamp(-0.9f, 0.95f, r_cloudAnisotropy->value);
	p->shapeTile = MAX(0.05f, r_cloudShapeScale->value);
	p->detailTile = MAX(0.01f, r_cloudDetailScale->value);
	p->weatherTile = MAX(0.5f, r_cloudWeatherScale->value);
	p->detail = Com_Clamp(0.0f, 2.0f, r_cloudDetail->value);
	p->maxDistance = Com_Clamp(1.0f, 500.0f, r_cloudMaxDistance->value);
	p->steps = Com_Clampi(8, 256, r_cloudSteps->integer);
	p->minSteps = MIN(16, p->steps);
	p->stepLength = MAX(0.005f, r_cloudStepLength->value * 0.001f);
	p->shadowSteps = Com_Clampi(1, 16, r_cloudShadowSteps->integer);
	p->octaves = Com_Clampi(1, 4, r_cloudMSOctaves->integer);
	p->typeScale = 1.0f;

	// legacy fx_cloudlayer
	p->legacyMode = Com_Clampi(0, 2, r_cloudLegacy->integer);
	const cloudLegacyLayer_t *layer = R_CloudsLegacyLayer();
	if ( !layer )
		p->legacyMode = 0;
	if ( p->legacyMode )
	{
		p->legacy = *layer;
		if ( p->legacyMode == 2 )
		{
			// literal: the entity's height is the base, the cvar thickness is kept
			const float thickness = p->top - p->base;
			p->base = MAX(0.0f, (layer->origin[2] - p->groundZ) * p->kmPerUnit);
			p->top = p->base + thickness;
		}
		if ( layer->alt )
		{
			// gfx/world/haze2: a thin hazy layer
			p->extinction *= 0.45f;
			p->detail = MIN(2.0f, p->detail * 1.5f);
			p->typeScale = 0.25f;
		}
	}
	p->lightLength = Com_Clamp(0.3f, 3.0f, p->top - p->base);

	VectorCopy(refdef->sunDir, p->sunDir);
	if ( VectorNormalize(p->sunDir) < 1e-4f )
		VectorSet(p->sunDir, 0.0f, 0.0f, 1.0f);
	R_AtmosphereSunGround(refdef, p->sunColor);

	// without the atmosphere: a clear sky is ~0.15 of a sunlit white surface, bluish;
	// the ground (albedo 0.3) lit by the sun
	const float sunLum = R_CloudsLuminance(p->sunColor);
	VectorSet(p->ambientSky, sunLum * 0.15f * 0.75f, sunLum * 0.15f * 0.95f, sunLum * 0.15f * 1.35f);
	VectorScale(p->sunColor, 0.3f * MAX(p->sunDir[2], 0.0f), p->ambientGround);
	p->ambientScale = MAX(0.0f, r_cloudAmbient->value);

	p->camera[0] = view->ori.origin[0] * p->kmPerUnit;
	p->camera[1] = view->ori.origin[1] * p->kmPerUnit;
	p->camera[2] = (view->ori.origin[2] - p->groundZ) * p->kmPerUnit;

	// wind: the pattern moves with it, a sample at x reads the noise at x - v t
	const double t = refdef->time * 0.001;
	const double angle = DEG2RAD(r_cloudWindDir->value);
	const double speed = r_cloudWindSpeed->value * 0.001;	// km / s
	const double vx = cos(angle) * speed, vy = sin(angle) * speed;
	p->shapeOffset[0] = R_CloudsWrap(-vx * t / p->shapeTile);
	p->shapeOffset[1] = R_CloudsWrap(-vy * t / p->shapeTile);
	p->shapeOffset[2] = 0.0f;
	// the detail drifts a little faster and rises slowly (0.3 m/s): the edges boil
	p->detailOffset[0] = R_CloudsWrap(-vx * 1.3 * t / p->detailTile);
	p->detailOffset[1] = R_CloudsWrap(-vy * 1.3 * t / p->detailTile);
	p->detailOffset[2] = R_CloudsWrap(-0.0003 * t / p->detailTile);
	p->weatherOffset[0] = R_CloudsWrap(-vx * t / p->weatherTile);
	p->weatherOffset[1] = R_CloudsWrap(-vy * t / p->weatherTile);
	p->windVelocity[0] = (float)(vx / p->kmPerUnit);
	p->windVelocity[1] = (float)(vy / p->kmPerUnit);

	p->debug = r_cloudDebug->integer;
}

static uint32_t R_CloudsHash( uint32_t h, float v )
{
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	h ^= bits + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

// what the history depends on (not the wind, time, sun or camera)
static uint32_t R_CloudsHistoryKey( const cloudParams_t *p )
{
	const float values[] =
	{
		p->base, p->top, p->coverage, p->extinction, p->g, p->shapeTile, p->detailTile,
		p->weatherTile, p->detail, p->maxDistance, (float)p->octaves, p->kmPerUnit, p->groundZ,
		(float)p->legacyMode, p->legacy.origin[0], p->legacy.origin[1], p->legacy.radius, p->legacy.inner,
		(float)p->legacy.tube, (float)p->legacy.alt, p->ambientScale, (float)p->debug,
		(float)p->atmosphere,
	};
	uint32_t h = 0x434c4f44u;
	for ( size_t i = 0; i < ARRAY_LEN(values); i++ )
		h = R_CloudsHash(h, values[i]);
	return h;
}

/*
=================
R_CloudsOffReason

NULL when the clouds can be drawn on this map (front end and back end)
=================
*/
const char *R_CloudsOffReason( void )
{
	if ( !R_CloudsEnabled() )
		return "r_clouds 0";
	if ( !tr.world )
		return "no world";
	if ( !s_clouds.resources || !tr.cloudMarchShader.program || !tr.cloudCompositeShader.program ||
		!tr.atmosphereCompositeFbo )
		return "resources unavailable (r_clouds is latched: vid_restart after changing it)";
	if ( !tr.sunParsed && !r_forceSun->integer )
		return "the map has no sun (q3map_sun / sun in a shader); r_forceSun enables it";
	if ( tr.world->skyboxportal && !r_cloudSkyPortal->integer )
		return "sky portal map: its sky is a scene (r_cloudSkyPortal 1 composites over it)";
	// the legacy global fog is baked into the surfaces and the fog cap
	if ( tr.world->globalFog && !R_VolumetricFroxelEnabled() )
		return "the map has a global fog and the froxel fog (r_volumetricFog 2) is off";
	return NULL;
}

/*
=================
R_CloudsCaptureLegacy

Front end, R_AddEntitySurface: the RT_CLOUDS entity of SP fx_cloudlayer
(CG_Clouds). True when the new clouds take it over in this view (its haze
disc is not drawn); mirrors and portals keep the legacy surface.
=================
*/
qboolean R_CloudsCaptureLegacy( const trRefEntity_t *ent )
{
	if ( !R_CloudsEnabled() || !r_cloudLegacy->integer )
		return qfalse;
	if ( tr.viewParms.viewParmType != VPT_MAIN || tr.viewParms.isPortal || tr.viewParms.isSkyPortal )
		return qfalse;
	if ( R_CloudsOffReason() )
		return qfalse;

	cloudLegacyLayer_t *layer = &s_clouds.captured;
	layer->valid = qtrue;
	VectorCopy(ent->e.origin, layer->origin);
	layer->radius = ent->e.radius;
	layer->tube = (qboolean)((ent->e.renderfx & RF_GROW) != 0);
	layer->inner = layer->tube ? ent->e.rotation : 0.0f;
	const shader_t *shader = R_GetShaderByHandle(ent->e.customShader);
	layer->alt = (qboolean)(shader && !Q_stricmp(shader->name, "gfx/world/haze2"));
	s_clouds.capturedFrame = tr.frameCount;
	return qtrue;
}

/*
============================================================

Back end

============================================================
*/

static qboolean RB_CloudsMainView( void )
{
	if ( backEnd.projection2D || backEnd.depthFill || backEnd.refractionFill || backEnd.framePostProcessed )
		return qfalse;
	const viewParms_t& view = backEnd.viewParms;
	if ( view.viewParmType != VPT_MAIN || view.isPortal || view.isSkyPortal ||
		(view.flags & (VPF_DEPTHSHADOW | VPF_NOCUBEMAPS | VPF_NODIFFUSEIBL)) )
		return qfalse;
	if ( view.targetFbo != NULL && view.targetFbo != tr.renderFbo )
		return qfalse;
	if ( backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE) )
		return qfalse;
	// one cloud history: the first world scene of the frame only
	if ( backEndData->currentFrame && backEndData->currentFrame->currentScene != 0 )
		return qfalse;
	return qtrue;
}

static const char *RB_CloudsOffReason( void )
{
	const char *reason = R_CloudsOffReason();
	if ( !reason && tr.world->globalFog && !RB_VolumetricCompositeActive() )
		reason = "the map has a global fog and the froxel fog is not composited in this view";
	if ( reason )
	{
		if ( s_clouds.reasonWorld != tr.world || s_clouds.reason != reason )
		{
			ri.Printf(PRINT_ALL, "r_clouds: off, %s\n", reason);
			s_clouds.reasonWorld = tr.world;
		}
		s_clouds.reason = reason;
		return reason;
	}
	if ( s_clouds.reason )
		s_clouds.reasonWorld = NULL;
	s_clouds.reason = NULL;
	return NULL;
}

/*
=================
RB_CloudsActive

Does the current view get the cloud composite? The main world view of the
first scene, once per frame, as the atmosphere: portals, mirrors, the sky
portal and cubemap captures keep their look.
=================
*/
qboolean RB_CloudsActive( void )
{
	if ( !R_CloudsEnabled() || backEnd.cloudsComposited )
		return qfalse;
	if ( !RB_CloudsMainView() || glState.currentFBO != tr.renderFbo )
		return qfalse;
	if ( s_clouds.lastFrame == backEndData->realFrameNumber + 1 )
		return qfalse;
	return (qboolean)(RB_CloudsOffReason() == NULL);
}

// the atmosphere builds its sky-view LUT for the clouds' ambient light
qboolean RB_CloudsWantSkyView( void )
{
	return (qboolean)(R_CloudsEnabled() && s_clouds.resources && s_clouds.reason == NULL);
}

// Histogram equalization of a generated channel into bytes: the rank of each
// value (4096 bins between min and max, linear inside a bin), so 0..255 are
// used evenly. The generated fbm values bunch around their mean; equalized,
// a coverage c leaves about a fraction c of the field above 1 - c.
// tools/rend2/test_clouds_gl.py mirrors this.
#define CLOUD_EQUALIZE_BINS 4096

static void RB_CloudsEqualize( const float *src, int srcStride, size_t count, byte *dst, int dstStride )
{
	float lo = src[0], hi = src[0];
	for ( size_t i = 0; i < count; i++ )
	{
		lo = MIN(lo, src[i * srcStride]);
		hi = MAX(hi, src[i * srcStride]);
	}
	const float scale = hi > lo ? CLOUD_EQUALIZE_BINS / (hi - lo) : 0.0f;
	std::vector<uint32_t> histogram(CLOUD_EQUALIZE_BINS, 0);
	for ( size_t i = 0; i < count; i++ )
	{
		const int bin = MIN(CLOUD_EQUALIZE_BINS - 1, (int)((src[i * srcStride] - lo) * scale));
		histogram[bin]++;
	}
	std::vector<double> below(CLOUD_EQUALIZE_BINS, 0.0);
	for ( int b = 1; b < CLOUD_EQUALIZE_BINS; b++ )
		below[b] = below[b - 1] + histogram[b - 1];
	for ( size_t i = 0; i < count; i++ )
	{
		const float x = (src[i * srcStride] - lo) * scale;
		const int bin = MIN(CLOUD_EQUALIZE_BINS - 1, (int)x);
		const double rank = (below[bin] + histogram[bin] * Com_Clamp(0.0f, 1.0f, x - bin)) / (double)count;
		dst[i * dstStride] = (byte)Com_Clampi(0, 255, (int)(rank * 256.0));
	}
}

/*
=================
RB_CloudsGenerateNoise

Once per renderer start: every slice of the shape, detail and weather
fields by clouds_noise.glsl into a float scratch texture, read back,
histogram equalized into bytes (the cloud type of the weather map is only
quantized), uploaded to the R8 / RG8 textures with mips.
=================
*/
static void RB_CloudsGenerateNoise( void )
{
	if ( s_clouds.noiseReady || !tr.cloudNoiseShader.program || !tr.cloudNoiseFbo )
		return;

	const int startTime = ri.Milliseconds();
	const int timer = RB_VolumetricBeginTimer("Clouds noise gen");
	R_PushDebugGroup(AL_STAGE, "Cloud noise");

	FBO_Bind(tr.cloudNoiseFbo);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	shaderProgram_t *sp = &tr.cloudNoiseShader;
	GLSL_BindProgram(sp);
	const GLenum buf = GL_COLOR_ATTACHMENT0;
	qglDrawBuffers(1, &buf);

	struct
	{
		image_t *image;
		int size, depth, target, channels;
	} targets[3] =
	{
		{ tr.cloudShapeImage, CLOUD_SHAPE_SIZE, CLOUD_SHAPE_SIZE, 0, 1 },
		{ tr.cloudDetailImage, CLOUD_DETAIL_SIZE, CLOUD_DETAIL_SIZE, 1, 1 },
		{ tr.cloudWeatherImage, CLOUD_WEATHER_SIZE, 1, 2, 2 },
	};

	vec4_t u[CLOUD_UNIFORM_VEC4S];
	Com_Memset(u, 0, sizeof(u));
	std::vector<float> values;
	std::vector<byte> bytes;
	for ( int t = 0; t < 3; t++ )
	{
		const int size = targets[t].size, depth = targets[t].depth, channels = targets[t].channels;
		const GLenum format = channels == 1 ? GL_RED : GL_RG;

		// float scratch target (the GL texture cache of the unit is kept in sync)
		GLuint scratch = 0;
		qglGenTextures(1, &scratch);
		GL_SelectTexture(TB_COLORMAP);
		qglBindTexture(GL_TEXTURE_3D, scratch);
		glState.currenttextures[TB_COLORMAP] = scratch;
		qglTexImage3D(GL_TEXTURE_3D, 0, channels == 1 ? GL_R32F : GL_RG32F, size, size, depth, 0,
			format, GL_FLOAT, NULL);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		qglTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		GL_SetViewportAndScissor(0, 0, size, size);
		for ( int z = 0; z < depth; z++ )
		{
			qglFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_3D, scratch, 0, z);
			if ( z == 0 )
				R_CheckFBO(tr.cloudNoiseFbo);
			VectorSet4(u[0], (float)targets[t].target, (float)z, (float)size, 0.0f);
			GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
			RB_InstantTriangle();
		}
		qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

		const size_t count = (size_t)size * size * depth;
		values.resize(count * channels);
		bytes.resize(count * channels);
		qglPixelStorei(GL_PACK_ALIGNMENT, 1);
		qglGetTexImage(GL_TEXTURE_3D, 0, format, GL_FLOAT, values.data());
		qglPixelStorei(GL_PACK_ALIGNMENT, 4);
		qglBindTexture(GL_TEXTURE_3D, 0);
		glState.currenttextures[TB_COLORMAP] = 0;
		qglDeleteTextures(1, &scratch);

		RB_CloudsEqualize(values.data(), channels, count, bytes.data(), channels);
		for ( size_t i = 0; channels > 1 && i < count; i++ )
			bytes[i * channels + 1] = (byte)Com_Clampi(0, 255, (int)(values[i * channels + 1] * 255.0f + 0.5f));

		GL_BindToTMU(targets[t].image, TB_COLORMAP);
		GL_SelectTexture(TB_COLORMAP);
		qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		qglTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, size, size, depth, format, GL_UNSIGNED_BYTE, bytes.data());
		qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		qglGenerateMipmap(GL_TEXTURE_3D);
	}

	RB_VolumetricEndTimer(timer);

	s_clouds.noiseReady = qtrue;
	s_clouds.noiseBuilds++;
	s_clouds.noiseMsec = ri.Milliseconds() - startTime;
}

static void RB_CloudsUniforms( const cloudParams_t *p, const viewParms_t *view, float weight,
	vec4_t u[CLOUD_UNIFORM_VEC4S] )
{
	Com_Memset(u, 0, sizeof(vec4_t) * CLOUD_UNIFORM_VEC4S);
	const int scale = MAX(1, s_clouds.scale);
	VectorSet4(u[0], p->base, p->top, CLOUD_PLANET_RADIUS, p->coverage);
	VectorSet4(u[1], p->extinction, p->g, CLOUD_BACK_LOBE_G, CLOUD_FORWARD_WEIGHT);
	VectorSet4(u[2], p->shapeTile, p->detailTile, p->weatherTile, p->detail);
	VectorSet4(u[3], p->shapeOffset[0], p->shapeOffset[1], p->shapeOffset[2], 0.0f);
	VectorSet4(u[4], p->detailOffset[0], p->detailOffset[1], p->detailOffset[2], p->maxDistance);
	VectorSet4(u[5], p->weatherOffset[0], p->weatherOffset[1], (float)p->octaves, p->lightLength);
	VectorSet4(u[6], p->sunDir[0], p->sunDir[1], p->sunDir[2], (float)p->shadowSteps);
	VectorSet4(u[7], p->sunColor[0], p->sunColor[1], p->sunColor[2], p->atmosphere ? 1.0f : 0.0f);
	VectorSet4(u[8], p->ambientSky[0], p->ambientSky[1], p->ambientSky[2], p->ambientScale);
	VectorSet4(u[9], p->ambientGround[0], p->ambientGround[1], p->ambientGround[2], p->groundAltitude);
	VectorSet4(u[10], p->camera[0], p->camera[1], p->camera[2], (float)p->steps);
	VectorSet4(u[11], p->stepLength, (float)p->minSteps, p->kmPerUnit, p->groundZ);
	VectorSet4(u[12], p->legacy.origin[0] * p->kmPerUnit, p->legacy.origin[1] * p->kmPerUnit,
		p->legacy.radius * p->kmPerUnit, p->legacy.inner * p->kmPerUnit);
	VectorSet4(u[13], (float)p->legacyMode, p->legacy.tube ? 1.0f : 0.0f, p->legacy.alt ? 1.0f : 0.0f, p->typeScale);
	if ( view )
	{
		VectorSet4(u[14],
			view->viewportX / (float)tr.renderFbo->width,
			view->viewportY / (float)tr.renderFbo->height,
			view->viewportWidth / (float)tr.renderFbo->width,
			view->viewportHeight / (float)tr.renderFbo->height);
		VectorSet4(u[15], (float)(view->viewportX / scale), (float)(view->viewportY / scale),
			(float)MIN(s_clouds.marchWidth - view->viewportX / scale, (view->viewportWidth + scale - 1) / scale),
			(float)MIN(s_clouds.marchHeight - view->viewportY / scale, (view->viewportHeight + scale - 1) / scale));
		VectorSet4(u[19], view->ori.origin[0], view->ori.origin[1], view->ori.origin[2], 0.0f);
	}
	VectorSet4(u[16], (float)p->debug, weight, (float)(backEndData->realFrameNumber & 63), (float)scale);
	VectorSet4(u[18], 0.0f, p->atmosphere ? 1.0f : 0.0f, p->windVelocity[0], p->windVelocity[1]);
}

static void RB_CloudsBindNoise( void )
{
	GL_BindToTMU(tr.cloudShapeImage, TB_SPECULARMAP);
	GL_BindToTMU(tr.cloudDetailImage, TB_SHADOWMAP);
	GL_BindToTMU(tr.cloudWeatherImage, TB_CUBEMAP);
}

static qboolean RB_CloudsHistoryValid( uint32_t key, const viewParms_t *view, const int lowViewport[4] )
{
	if ( !s_clouds.historyValid )
		return qfalse;
	return (qboolean)!(
		s_clouds.historyFrame + 1 != backEndData->realFrameNumber ||
		s_clouds.historyWorld != tr.world ||
		s_clouds.historyKey != key ||
		!tr.temporalHistoryValid ||
		memcmp(s_clouds.historyViewport, lowViewport, sizeof(s_clouds.historyViewport)) != 0 ||
		Distance(s_clouds.historyOrigin, view->ori.origin) > CLOUD_CUT_DISTANCE ||
		backEnd.refdef.time < s_clouds.historyTime ||
		backEnd.refdef.time - s_clouds.historyTime > CLOUD_CUT_TIME);
}

/*
=================
RB_CloudsDrawSunEarly

The sun quad of RB_RenderMainPass is drawn after every composite. Under
clouds it is drawn here instead, after the atmosphere composite (whose
analytic sky replaces sky pixels) and before the cloud and froxel
composites, so both cover it. The pass is being submitted: draw immediately
(no pass), as RB_RenderMainPass does.
=================
*/
void RB_CloudsDrawSunEarly( void )
{
	if ( !r_drawSun->integer || !backEnd.skyRenderedThisView )
		return;
	Pass *pass = backEndData->currentPass;
	trRefEntity_t *entity = backEnd.currentEntity;
	backEndData->currentPass = nullptr;
	RB_DrawSun(0.1f, tr.sunShader);
	backEndData->currentPass = pass;
	backEnd.currentEntity = entity;
	backEnd.cloudSunDrawn = qtrue;
}

/*
=================
RB_CloudsComposite

March, temporal resolve and composite of the clouds over the sky pixels.
Called by RB_SubmitRenderPass with renderFbo bound, after the atmosphere
composite and before the froxel composite.
=================
*/
void RB_CloudsComposite( void )
{
	if ( !RB_CloudsActive() )
		return;

	backEnd.cloudsComposited = qtrue;
	s_clouds.lastFrame = backEndData->realFrameNumber + 1;
	s_clouds.lastViewValid = qfalse;

	const viewParms_t *view = &backEnd.viewParms;
	cloudParams_t params;
	R_CloudsBuildParams(&backEnd.refdef, view, &params);

	FBO_t *oldFbo = glState.currentFBO;
	R_PushDebugGroup(AL_STAGE, "Clouds");

	RB_CloudsGenerateNoise();
	if ( !s_clouds.noiseReady || params.coverage <= 0.0f || params.extinction <= 0.0f )
	{
		// a clear sky: nothing to draw, nothing to keep
		s_clouds.historyValid = qfalse;
		s_clouds.last = params;
		s_clouds.hasLast = qtrue;
		FBO_Bind(oldFbo);
		GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
		return;
	}

	// the view without the SMAA T2x jitter (written into projectionMatrix[2] / [6], tr_main.cpp):
	// two alternating sub pixel offsets would only shake the history
	matrix_t projection, viewProjection, invViewProjection;
	Com_Memcpy(projection, view->projectionMatrix, sizeof(projection));
	if ( r_smaa->integer == 2 )
		projection[2] = projection[6] = 0.0f;
	Matrix16Multiply(projection, view->world.modelViewMatrix, viewProjection);
	// The rays come from a camera relative inverse (no translation): with the camera tens of
	// thousands of units from the origin the far plane points of the full inverse cost ~1e-4 rad
	// of float precision, a few tens of metres at a cloud 20 km away. World positions are the
	// camera (u_Cloud[19]) plus these offsets.
	{
		matrix_t viewRotation, relativeViewProjection;
		Com_Memcpy(viewRotation, view->world.modelViewMatrix, sizeof(viewRotation));
		viewRotation[12] = viewRotation[13] = viewRotation[14] = 0.0f;
		Matrix16Multiply(projection, viewRotation, relativeViewProjection);
		if ( !R_VolumetricInvertMatrix(relativeViewProjection, invViewProjection) )
			Matrix16Identity(invViewProjection);
	}

	// MSAA: the depth texture is the resolve target (the atmosphere may have resolved it already)
	if ( tr.msaaResolveFbo && !backEnd.atmosphereComposited )
	{
		GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	vec4_t u[CLOUD_UNIFORM_VEC4S];
	RB_CloudsUniforms(&params, view, 0.0f, u);
	const int lowViewport[4] = { (int)u[15][0], (int)u[15][1], (int)u[15][2], (int)u[15][3] };

	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);

	// march
	int timer = RB_VolumetricBeginTimer("Clouds march");
	{
		shaderProgram_t *sp = &tr.cloudMarchShader;
		FBO_Bind(tr.cloudMarchFbo);
		GL_SetViewportAndScissor(lowViewport[0], lowViewport[1], lowViewport[2], lowViewport[3]);
		GLSL_BindProgram(sp);
		params.atmosphere = RB_AtmosphereCloudUniforms(sp);
		u[7][3] = params.atmosphere ? 1.0f : 0.0f;
		u[18][1] = u[7][3];
		GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_CLOUDINVVIEWPROJECTION, invViewProjection, 1);
		GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
		RB_CloudsBindNoise();
		RB_InstantTriangle();
	}
	RB_VolumetricEndTimer(timer);
	s_clouds.marches++;

	// resolve: own history (the key includes whether the atmosphere lit it)
	const uint32_t key = R_CloudsHistoryKey(&params);
	const qboolean temporal = (qboolean)(r_cloudTemporal->value > 0.0f &&
		(params.debug == 0 || params.debug == 7) && RB_CloudsHistoryValid(key, view, lowViewport));
	const float weight = temporal ? Com_Clamp(0.0f, 0.98f, r_cloudTemporal->value) : 0.0f;
	const int previous = s_clouds.current;
	const int next = previous ^ 1;
	u[16][1] = weight;
	u[19][3] = temporal ? (backEnd.refdef.time - s_clouds.historyTime) * 0.001f : 0.0f;

	timer = RB_VolumetricBeginTimer("Clouds resolve");
	{
		shaderProgram_t *sp = &tr.cloudResolveShader;
		FBO_Bind(tr.cloudHistoryFbo[next]);
		GL_SetViewportAndScissor(lowViewport[0], lowViewport[1], lowViewport[2], lowViewport[3]);
		GLSL_BindProgram(sp);
		GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_CLOUDINVVIEWPROJECTION, invViewProjection, 1);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_CLOUDPREVVIEWPROJECTION, s_clouds.historyViewProjection, 1);
		GL_BindToTMU(tr.cloudMarchImage, TB_ENVBRDFMAP);
		GL_BindToTMU(tr.cloudMarchDepthImage, TB_SHADOWMAPARRAY);
		GL_BindToTMU(tr.cloudHistoryImage[previous], TB_SSAOMAP);
		GL_BindToTMU(tr.cloudHistoryDepthImage[previous], TB_EMISSIVEMAP);
		RB_InstantTriangle();
	}
	RB_VolumetricEndTimer(timer);

	s_clouds.current = next;
	s_clouds.historyValid = qtrue;
	s_clouds.historyFrame = backEndData->realFrameNumber;
	s_clouds.historyWorld = tr.world;
	s_clouds.historyKey = key;
	Com_Memcpy(s_clouds.historyViewport, lowViewport, sizeof(s_clouds.historyViewport));
	VectorCopy(view->ori.origin, s_clouds.historyOrigin);
	s_clouds.historyTime = backEnd.refdef.time;
	Com_Memcpy(s_clouds.historyViewProjection, viewProjection, sizeof(matrix_t));
	s_clouds.lastWeight = weight;

	// composite over the sky pixels: color = (T_ap S + (1 - T) S_ap, T), blend ONE, SRC_ALPHA
	timer = RB_VolumetricBeginTimer("Clouds composite");
	{
		shaderProgram_t *sp = &tr.cloudCompositeShader;
		FBO_Bind(tr.atmosphereCompositeFbo);
		GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
		GLSL_BindProgram(sp);
		if ( params.atmosphere && !RB_AtmosphereCloudUniforms(sp) )
		{
			params.atmosphere = qfalse;
			u[18][1] = 0.0f;
		}
		u[18][0] = 0.0f;
		GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
		GLSL_SetUniformMatrix4x4(sp, UNIFORM_CLOUDINVVIEWPROJECTION, invViewProjection, 1);
		GLSL_SetUniformVec4N(sp, UNIFORM_CLOUDSHADOW, &s_clouds.shadowLookup[0][0], 2);
		GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
		GL_BindToTMU(tr.cloudHistoryImage[next], TB_ENVBRDFMAP);
		GL_BindToTMU(tr.cloudHistoryDepthImage[next], TB_SHADOWMAPARRAY);
		GL_BindToTMU(tr.cloudShadowImage, TB_SSAOMAP);
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_SRC_ALPHA);
		// keep the destination alpha (GL_State only masks all channels)
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		RB_InstantTriangle();
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		GL_ResetScreenAuxWrite();
	}
	RB_VolumetricEndTimer(timer);

	// a debug view replaces the frame: no froxel fog over it
	if ( params.debug > 0 )
		backEnd.volumetricComposited = qtrue;

	Com_Memcpy(s_clouds.lastUniforms, u, sizeof(u));
	Com_Memcpy(s_clouds.lastInvViewProjection, invViewProjection, sizeof(matrix_t));
	s_clouds.lastViewValid = qtrue;
	s_clouds.last = params;
	s_clouds.hasLast = qtrue;

	FBO_Bind(oldFbo);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
}

/*
=================
RB_CloudsSunRaysMask

RB_RenderMainPass, after the sun flare is drawn into tr.sunRaysFbo: the sun
rays start from what the clouds let through (flare *= T_clouds).
=================
*/
void RB_CloudsSunRaysMask( void )
{
	if ( !backEnd.cloudsComposited || !s_clouds.lastViewValid || !tr.sunRaysFbo )
		return;

	const viewParms_t *view = &backEnd.viewParms;
	shaderProgram_t *sp = &tr.cloudCompositeShader;
	vec4_t u[CLOUD_UNIFORM_VEC4S];
	Com_Memcpy(u, s_clouds.lastUniforms, sizeof(u));
	u[18][0] = 1.0f;
	u[16][0] = 0.0f;

	GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
	GLSL_BindProgram(sp);
	GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
	GL_BindToTMU(tr.cloudHistoryImage[s_clouds.current], TB_ENVBRDFMAP);
	GL_BindToTMU(tr.cloudHistoryDepthImage[s_clouds.current], TB_SHADOWMAPARRAY);
	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR);
	RB_InstantTriangle();
}

/*
============================================================

Cloud shadows (r_cloudShadows)

============================================================
*/

static qboolean RB_CloudShadowsActive( void )
{
	return (qboolean)(R_CloudsEnabled() && r_cloudShadows->integer && s_clouds.shadowValid &&
		s_clouds.shadowWorld == tr.world && tr.cloudShadowImage && R_CloudShadowsAvailable());
}

// history key of the froxel fog: its sun changes with the cloud shadows
int R_CloudShadowKey( void )
{
	return (R_CloudsEnabled() && r_cloudShadows->integer && R_CloudShadowsAvailable()) ? 1 : 0;
}

/*
=================
RB_CloudsBeginView

RB_DrawSurfs, before the depth passes of a view: the cloud shadow map of
the main view (every r_cloudShadowInterval frames), so the froxel injection
and the opaque surfaces of this frame see it. Other views of the frame use
the last map.
=================
*/
void RB_CloudsBeginView( void )
{
	if ( !R_CloudsEnabled() )
		return;
	if ( !r_cloudShadows->integer || !R_CloudShadowsAvailable() || !tr.cloudShadowFbo ||
		!tr.cloudShadowShader.program )
	{
		s_clouds.shadowValid = qfalse;
		return;
	}
	if ( !RB_CloudsMainView() || s_clouds.shadowFrame == backEndData->realFrameNumber + 1 )
		return;
	if ( R_CloudsOffReason() )
	{
		s_clouds.shadowValid = qfalse;
		return;
	}
	const int interval = Com_Clampi(1, 60, r_cloudShadowInterval->integer);
	if ( s_clouds.shadowValid && s_clouds.shadowWorld == tr.world &&
		(backEndData->realFrameNumber % interval) != 0 )
		return;

	const viewParms_t *view = &backEnd.viewParms;
	cloudParams_t params;
	R_CloudsBuildParams(&backEnd.refdef, view, &params);
	s_clouds.shadowFrame = backEndData->realFrameNumber + 1;

	FBO_t *oldFbo = glState.currentFBO;
	RB_CloudsGenerateNoise();
	if ( !s_clouds.noiseReady )
	{
		FBO_Bind(oldFbo);
		return;
	}

	// a square around the camera, its centre snapped to whole texels (no shimmer when walking)
	const float extent = Com_Clamp(0.5f, 100.0f, r_cloudShadowExtent->value);	// km
	const float texel = extent / CLOUD_SHADOW_SIZE;
	const float centre[2] = {
		floorf(params.camera[0] / texel + 0.5f) * texel,
		floorf(params.camera[1] / texel + 0.5f) * texel };

	vec4_t u[CLOUD_UNIFORM_VEC4S];
	RB_CloudsUniforms(&params, NULL, 0.0f, u);
	VectorSet4(u[17], centre[0], centre[1], extent, (float)Com_Clampi(2, 32, params.shadowSteps * 2));

	const int timer = RB_VolumetricBeginTimer("Clouds shadow map");
	R_PushDebugGroup(AL_STAGE, "Cloud shadow map");
	shaderProgram_t *sp = &tr.cloudShadowShader;
	FBO_Bind(tr.cloudShadowFbo);
	GL_SetViewportAndScissor(0, 0, CLOUD_SHADOW_SIZE, CLOUD_SHADOW_SIZE);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(sp);
	GLSL_SetUniformVec4N(sp, UNIFORM_CLOUD, &u[0][0], CLOUD_UNIFORM_VEC4S);
	RB_CloudsBindNoise();
	RB_InstantTriangle();
	RB_VolumetricEndTimer(timer);
	FBO_Bind(oldFbo);

	// lookup (world units): slide the point along the sun to the ground plane z0,
	// uv = (q - centre) / extent + 0.5; faded out for a low sun (the plane
	// mapping stretches) and with the elevation of a sun below the horizon
	const float sunZ = MAX(params.sunDir[2], 0.05f);
	const float strength = (params.coverage > 0.0f && params.extinction > 0.0f) ?
		Com_Clamp(0.0f, 1.0f, (params.sunDir[2] - 0.03f) / 0.07f) : 0.0f;
	VectorSet4(s_clouds.shadowLookup[0], centre[0] / params.kmPerUnit, centre[1] / params.kmPerUnit,
		params.kmPerUnit / extent, params.groundZ);
	VectorSet4(s_clouds.shadowLookup[1], params.sunDir[0] / sunZ, params.sunDir[1] / sunZ, strength, 1.0f);
	s_clouds.shadowValid = qtrue;
	s_clouds.shadowWorld = tr.world;
	s_clouds.shadowBuilds++;
}

/*
=================
RB_CloudShadowBind

lightall stages with the sun shadow map (tr_shade.cpp): the cloud shadow
lookup, disabled (w = 0) when there is no map. Nothing at all with r_clouds 0.
=================
*/
void RB_CloudShadowBind( UniformDataWriter &uniformDataWriter, SamplerBindingsWriter &samplerBindingsWriter )
{
	if ( !R_CloudsEnabled() )
		return;
	if ( RB_CloudShadowsActive() )
	{
		uniformDataWriter.SetUniformVec4(UNIFORM_CLOUDSHADOW, &s_clouds.shadowLookup[0][0], 2);
		samplerBindingsWriter.AddStaticImage(tr.cloudShadowImage, TB_CLOUDSHADOW);
	}
	else
	{
		const vec4_t off[2] = {};
		uniformDataWriter.SetUniformVec4(UNIFORM_CLOUDSHADOW, &off[0][0], 2);
	}
}

// the froxel injection (tr_volumetric.cpp): same lookup, direct uniforms
qboolean RB_CloudShadowBindDirect( shaderProgram_t *sp )
{
	if ( !R_CloudsEnabled() )
		return qfalse;
	if ( RB_CloudShadowsActive() )
	{
		GLSL_SetUniformVec4N(sp, UNIFORM_CLOUDSHADOW, &s_clouds.shadowLookup[0][0], 2);
		GL_BindToTMU(tr.cloudShadowImage, TB_CLOUDSHADOW);
		return qtrue;
	}
	const vec4_t off[2] = {};
	GLSL_SetUniformVec4N(sp, UNIFORM_CLOUDSHADOW, &off[0][0], 2);
	return qfalse;
}

/*
============================================================

Console

============================================================
*/

/*
=================
R_CloudLayerTest_f

r_cloudLayerTest <x> <y> <z> <radius> [inner] [flags]: a fake fx_cloudlayer
(no stock map has one) for r_cloudLegacy; flags 1 TUBE, 2 ALT.
r_cloudLayerTest clear removes it.
=================
*/
void R_CloudLayerTest_f( void )
{
	if ( ri.Cmd_Argc() == 2 && !Q_stricmp(ri.Cmd_Argv(1), "clear") )
	{
		Com_Memset(&s_clouds.test, 0, sizeof(s_clouds.test));
		ri.Printf(PRINT_ALL, "r_cloudLayerTest: cleared\n");
		return;
	}
	if ( ri.Cmd_Argc() < 5 )
	{
		ri.Printf(PRINT_ALL, "usage: r_cloudLayerTest <x> <y> <z> <radius> [inner radius] [flags: 1 tube, 2 alt] | clear\n");
		return;
	}
	cloudLegacyLayer_t *layer = &s_clouds.test;
	layer->origin[0] = atof(ri.Cmd_Argv(1));
	layer->origin[1] = atof(ri.Cmd_Argv(2));
	layer->origin[2] = atof(ri.Cmd_Argv(3));
	layer->radius = MAX(1.0f, (float)atof(ri.Cmd_Argv(4)));
	const int flags = ri.Cmd_Argc() > 6 ? atoi(ri.Cmd_Argv(6)) : 0;
	layer->tube = (qboolean)((flags & 1) != 0);
	layer->alt = (qboolean)((flags & 2) != 0);
	layer->inner = (layer->tube && ri.Cmd_Argc() > 5) ? (float)atof(ri.Cmd_Argv(5)) : 0.0f;
	layer->valid = qtrue;
	ri.Printf(PRINT_ALL, "r_cloudLayerTest: layer at %.0f %.0f %.0f, radius %.0f, inner %.0f, %s%s (r_cloudLegacy %d)\n",
		layer->origin[0], layer->origin[1], layer->origin[2], layer->radius, layer->inner,
		layer->tube ? "tube" : "disc", layer->alt ? ", alt" : "", r_cloudLegacy->integer);
}

/*
=================
R_CloudInfo_f

r_cloudInfo: state, parameters, legacy layer, memory
=================
*/
void R_CloudInfo_f( void )
{
	const char *reason = R_CloudsEnabled() ? (tr.world ? R_CloudsOffReason() : "no map loaded") : "r_clouds 0";
	ri.Printf(PRINT_ALL, "r_clouds %d: %s\n", r_clouds->integer, reason ? reason : "on");
	if ( !R_CloudsEnabled() )
		return;

	const size_t march = (size_t)s_clouds.marchWidth * s_clouds.marchHeight;
	const size_t bytes = march * (8 + 2) * 3 +		// march + 2 history (RGBA16F + R16F)
		(size_t)CLOUD_SHAPE_SIZE * CLOUD_SHAPE_SIZE * CLOUD_SHAPE_SIZE * 8 / 7 +
		(size_t)CLOUD_DETAIL_SIZE * CLOUD_DETAIL_SIZE * CLOUD_DETAIL_SIZE * 8 / 7 +
		(size_t)CLOUD_WEATHER_SIZE * CLOUD_WEATHER_SIZE * 2 * 4 / 3 +
		(size_t)CLOUD_SHADOW_SIZE * CLOUD_SHADOW_SIZE * 2;
	ri.Printf(PRINT_ALL, "march %dx%d (r_cloudScale %d), memory %.1f MB\n", s_clouds.marchWidth,
		s_clouds.marchHeight, s_clouds.scale, bytes / (1024.0 * 1024.0));
	ri.Printf(PRINT_ALL, "noise: %s (%d builds, %d msec), marches %d\n", s_clouds.noiseReady ? "ready" : "not built",
		s_clouds.noiseBuilds, s_clouds.noiseMsec, s_clouds.marches);
	ri.Printf(PRINT_ALL, "shadows: %s, %d builds\n", r_cloudShadows->integer ?
		(R_CloudShadowsAvailable() ? (s_clouds.shadowValid ? "on" : "no map yet") : "too few texture units") : "off",
		s_clouds.shadowBuilds);

	const cloudLegacyLayer_t *layer = R_CloudsLegacyLayer();
	if ( layer )
		ri.Printf(PRINT_ALL, "legacy cloud layer (%s): origin %.0f %.0f %.0f, radius %.0f, inner %.0f, %s%s, r_cloudLegacy %d\n",
			layer == &s_clouds.test ? "r_cloudLayerTest" : "fx_cloudlayer", layer->origin[0], layer->origin[1],
			layer->origin[2], layer->radius, layer->inner, layer->tube ? "tube" : "disc", layer->alt ? ", alt (haze2)" : "",
			r_cloudLegacy->integer);
	else
		ri.Printf(PRINT_ALL, "legacy cloud layer: none\n");

	if ( !s_clouds.hasLast )
		return;
	const cloudParams_t *p = &s_clouds.last;
	ri.Printf(PRINT_ALL, "last composite:\n");
	ri.Printf(PRINT_ALL, "  layer %.0f - %.0f m above ground z %.1f (%g m per unit), camera at %.0f m\n",
		p->base * 1000.0f, p->top * 1000.0f, p->groundZ, p->kmPerUnit * 1000.0f, p->camera[2] * 1000.0f);
	ri.Printf(PRINT_ALL, "  coverage %.2f, extinction %.1f /km, g %.2f, detail %.2f, legacy mode %d\n",
		p->coverage, p->extinction, p->g, p->detail, p->legacyMode);
	ri.Printf(PRINT_ALL, "  steps %d (min %d, %.0f m), light %d over %.0f m, octaves %d, max distance %.0f km\n",
		p->steps, p->minSteps, p->stepLength * 1000.0f, p->shadowSteps, p->lightLength * 1000.0f, p->octaves,
		p->maxDistance);
	ri.Printf(PRINT_ALL, "  sun %.3f %.3f %.3f (%s), elevation %.1f deg, history weight %.2f\n",
		p->sunColor[0], p->sunColor[1], p->sunColor[2], p->atmosphere ? "through the atmosphere" : "map sun",
		RAD2DEG(asinf(Com_Clamp(-1.0f, 1.0f, p->sunDir[2]))), s_clouds.lastWeight);
}
