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

// Long range atmosphere and aerial perspective (r_atmosphere), see
// docs/rend2-atmosphere.md and glsl/atmosphere_*.glsl.
//
// A separate component next to the froxel fog (tr_volumetric.cpp): the
// froxels keep the local media (fog volumes, height fog, particles) up to
// r_volumetricFogFar and their analytic tail; the atmosphere is the air
// between the camera and distant surfaces and the sky. Its composite runs
// before the froxel composite in RB_SubmitRenderPass, so the order along a
// ray is camera -> local media -> atmosphere -> surface:
//
//   L = S_froxel + T_froxel * (S_atm + T_atm * surface)
//
// Model: Hillaire 2020 (transmittance, multiple scattering and sky-view LUTs,
// fragment passes, GL 3.2) with the Earth parameters of Bruneton 2017. The
// aerial perspective of the scene is analytic per pixel (exponential
// profiles along a short flat path, 3 segments), not a camera volume.
//
// Units: r_atmosphereUnitScale metres per world unit (0.03: the 64 unit tall
// player bounding box is ~1.8 m), r_atmosphereAerialScale multiplies the
// aerial path only (JA views are a few hundred metres, physically almost
// clear air).
//
// LUTs: transmittance + multiple scattering when the medium changes, the
// sky-view LUT when the sun direction, the camera altitude bucket or the
// medium changes. Nothing is rebuilt per frame for a static sun.

#include "tr_local.h"

#define ATMO_TRANSMITTANCE_W	256
#define ATMO_TRANSMITTANCE_H	64
#define ATMO_MULTISCATTER_W		32
#define ATMO_MULTISCATTER_H		32
#define ATMO_SKYVIEW_W			192
#define ATMO_SKYVIEW_H			108

#define ATMO_UNIFORM_VEC4S		8

// model constants (same as atmosphere_common.glsl), km
#define ATMO_R_GROUND			6360.0
#define ATMO_R_TOP				6460.0
#define ATMO_RAYLEIGH_H			8.0
#define ATMO_MIE_H				1.2
#define ATMO_MIE_EXTINCTION		4.40e-3

static const double atmoRayleighScattering[3] = { 5.802e-3, 13.558e-3, 33.1e-3 };
static const double atmoOzoneAbsorption[3] = { 0.650e-3, 1.881e-3, 0.085e-3 };

// at most this much brighter than the map's sun (luminance) when the map sun
// is taken as the ground level sun (r_atmosphereSunColor 1, a sun at the horizon)
#define ATMO_SUN_TOA_MAX_GAIN	8.0f

enum
{
	ATMO_LUT_TRANSMITTANCE,
	ATMO_LUT_MULTISCATTER,
	ATMO_LUT_SKYVIEW,
	ATMO_LUT_COUNT
};

typedef struct atmosphereParams_s
{
	float	rayleigh, mie, ozone, mieG;
	float	unitScale, aerialScale, groundZ, groundAltitude;	// m / unit, -, units, km
	vec3_t	sunDir;
	float	cosSunRadius;
	vec3_t	sunGround;		// map sun (buffer units)
	vec3_t	sunToa;			// illuminance at the top of the atmosphere (buffer units)
	float	cameraAltitude;	// km
	int		skyMode;
	float	skyBlend, sunGlow, start, skyDistance;
	int		debug;
	qboolean drawDisc;
} atmosphereParams_t;

static struct
{
	qboolean	lutValid;
	uint32_t	mediumKey;
	qboolean	skyViewValid;
	uint32_t	skyKey;
	int			mediumBuilds;
	int			skyViewBuilds;
	const world_t *reasonWorld;		// the "off" reason is printed once per map
	const char	*reason;			// why the atmosphere is off in the last main view, NULL = on
	atmosphereParams_t last;		// last composited parameters (r_atmosphereInfo)
	qboolean	hasLast;
} s_atmo;

static FBO_t *s_atmoLutFbo[ATMO_LUT_COUNT];

/*
============================================================

Resources

============================================================
*/

void R_CreateAtmosphereImages( void )
{
	Com_Memset(&s_atmo, 0, sizeof(s_atmo));

	// small (~300 KB): always created, r_atmosphere is not latched
	const int flags = IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE;
	tr.atmosphereTransmittanceImage = R_CreateImage("*atmosphereTransmittance", NULL,
		ATMO_TRANSMITTANCE_W, ATMO_TRANSMITTANCE_H, IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
	tr.atmosphereMultiScatterImage = R_CreateImage("*atmosphereMultiScatter", NULL,
		ATMO_MULTISCATTER_W, ATMO_MULTISCATTER_H, IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
	tr.atmosphereSkyViewImage = R_CreateImage("*atmosphereSkyView", NULL,
		ATMO_SKYVIEW_W, ATMO_SKYVIEW_H, IMGTYPE_COLORALPHA, flags, GL_RGBA16F);
}

void R_CreateAtmosphereFBOs( void )
{
	image_t *images[ATMO_LUT_COUNT] =
	{
		tr.atmosphereTransmittanceImage,
		tr.atmosphereMultiScatterImage,
		tr.atmosphereSkyViewImage,
	};
	static const char *names[ATMO_LUT_COUNT] =
	{
		"_atmosphereTransmittance",
		"_atmosphereMultiScatter",
		"_atmosphereSkyView",
	};

	tr.atmosphereCompositeFbo = NULL;
	for ( int i = 0; i < ATMO_LUT_COUNT; i++ )
	{
		s_atmoLutFbo[i] = NULL;
		if ( !images[i] )
			continue;
		s_atmoLutFbo[i] = FBO_Create(names[i], images[i]->width, images[i]->height);
		FBO_Bind(s_atmoLutFbo[i]);
		FBO_AttachTextureImage(images[i], 0);
		qglDrawBuffer(GL_COLOR_ATTACHMENT0);
		R_CheckFBO(s_atmoLutFbo[i]);
	}

	if ( !tr.renderFbo )
		return;

	// composite: color and glow of renderFbo only, the sampled depth must not
	// be attached (as the froxel composite)
	tr.atmosphereCompositeFbo = FBO_Create("_atmosphereComposite", tr.renderFbo->width, tr.renderFbo->height);
	FBO_Bind(tr.atmosphereCompositeFbo);
	if ( tr.msaaResolveFbo )
	{
		for ( int i = 0; i < 2; i++ )
		{
			qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
				GL_RENDERBUFFER, tr.renderFbo->colorBuffers[i]);
			glState.currentFBO->colorBuffers[i] = tr.renderFbo->colorBuffers[i];
		}
	}
	else
	{
		FBO_AttachTextureImage(tr.renderImage, 0);
		FBO_AttachTextureImage(tr.glowImage, 1);
	}
	{
		const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		qglDrawBuffers(2, bufs);
	}
	R_CheckFBO(tr.atmosphereCompositeFbo);
}

/*
============================================================

Model (CPU)

============================================================
*/

static double R_AtmosphereRho2( double h )
{
	return h * (2.0 * ATMO_R_GROUND + h);
}

// transmittance from altitude h (km) towards cos zenith mu to space, as the
// transmittance LUT (more steps); 0 below the horizon
static void R_AtmosphereTransmittance( const atmosphereParams_t *p, double h, double mu, vec3_t out )
{
	h = MAX(h, 0.0);
	const double r = ATMO_R_GROUND + h;
	const double horizon = -sqrt(R_AtmosphereRho2(h)) / r;
	if ( mu < horizon )
	{
		VectorClear(out);
		return;
	}

	const double disc = (ATMO_R_TOP - ATMO_R_GROUND - h) * (ATMO_R_TOP + r) + r * r * mu * mu;
	const double d = MAX(-r * mu + sqrt(MAX(disc, 0.0)), 0.0);
	const int steps = 200;
	const double dt = d / steps;
	double tau[3] = { 0.0, 0.0, 0.0 };
	for ( int i = 0; i < steps; i++ )
	{
		const double t = (i + 0.5) * dt;
		const double hs = sqrt(r * r + t * t + 2.0 * r * mu * t) - ATMO_R_GROUND;
		const double rayleigh = p->rayleigh * exp(-hs / ATMO_RAYLEIGH_H);
		const double mie = p->mie * exp(-hs / ATMO_MIE_H);
		const double ozone = p->ozone * MAX(0.0, 1.0 - fabs(hs - 25.0) / 15.0);
		for ( int c = 0; c < 3; c++ )
			tau[c] += (atmoRayleighScattering[c] * rayleigh + ATMO_MIE_EXTINCTION * mie +
				atmoOzoneAbsorption[c] * ozone) * dt;
	}
	for ( int c = 0; c < 3; c++ )
		out[c] = (float)exp(-tau[c]);
}

static float R_AtmosphereLuminance( const vec3_t c )
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

/*
=================
R_AtmosphereSunColor

The map's sun in buffer units: what a white Lambert surface lit by the sun at
normal incidence shows (r_sunlightMode 2 sunCol, or the froxel fog's estimate
from the sunlit light grid cells)
=================
*/
static void R_AtmosphereSunColor( const trRefdef_t *refdef, vec3_t out )
{
	if ( r_forceSun->integer == 2 )
	{
		// animated sun (tr_scene.cpp): its own colour
		VectorCopy(refdef->sunCol, out);
		return;
	}
	if ( tr.world && R_AtmosphereLuminance(tr.world->volumetricSunRadiance) > 0.0f )
	{
		VectorCopy(tr.world->volumetricSunRadiance, out);
		return;
	}
	float scale = powf(2.0f, (float)(r_mapOverBrightBits->integer - tr.overbrightBits - 8));
	if ( r_forceSun->integer )
		scale *= r_forceSunLightScale->value;
	VectorScale(tr.sunLight, scale, out);
}

float R_AtmosphereGroundZ( void )
{
	if ( !Q_stricmp(r_atmosphereGroundZ->string, "auto") || !r_atmosphereGroundZ->string[0] )
		return tr.world ? tr.world->heightFogAutoBase : 0.0f;
	return r_atmosphereGroundZ->value;
}

// the map's sun in buffer units, for the clouds without the atmosphere (tr_clouds.cpp)
void R_AtmosphereSunGround( const trRefdef_t *refdef, vec3_t out )
{
	R_AtmosphereSunColor(refdef, out);
}

static uint32_t R_AtmosphereHash( uint32_t h, float v )
{
	uint32_t bits;
	memcpy(&bits, &v, sizeof(bits));
	h ^= bits + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

static uint32_t R_AtmosphereMediumKey( const atmosphereParams_t *p )
{
	uint32_t h = 0x41544d4fu;
	h = R_AtmosphereHash(h, p->rayleigh);
	h = R_AtmosphereHash(h, p->mie);
	h = R_AtmosphereHash(h, p->ozone);
	h = R_AtmosphereHash(h, p->mieG);
	// the planet shadow edge is softened over the sun's angular radius
	h = R_AtmosphereHash(h, p->cosSunRadius);
	return h;
}

static uint32_t R_AtmosphereSkyKey( const atmosphereParams_t *p, uint32_t mediumKey )
{
	uint32_t h = mediumKey;
	// ~0.05 degree sun steps, 10 m altitude buckets
	for ( int i = 0; i < 3; i++ )
		h = R_AtmosphereHash(h, floorf(p->sunDir[i] * 1024.0f + 0.5f));
	h = R_AtmosphereHash(h, floorf(p->cameraAltitude * 100.0f));
	return h;
}

/*
=================
R_AtmosphereOffReason

NULL when this frame's main view can use the atmosphere
=================
*/
static const char *R_AtmosphereOffReason( qboolean froxelComposite )
{
	if ( !r_atmosphere->integer )
		return "r_atmosphere 0";
	if ( !tr.world )
		return "no world";
	if ( !tr.atmosphereCompositeFbo || !tr.atmosphereTransmittanceImage ||
		!tr.atmosphereTransmittanceShader.program || !tr.atmosphereCompositeShader.program )
		return "resources unavailable";
	if ( !tr.sunParsed && !r_forceSun->integer )
		return "the map has no sun (q3map_sun / sun in a shader); r_forceSun enables it";
	// the legacy global fog is baked into the surfaces (and the fog cap) before
	// the composite: the atmosphere would land in front of it
	if ( tr.world->globalFog && !froxelComposite )
		return "the map has a global fog and the froxel fog (r_volumetricFog 2) is not composited";
	return NULL;
}

static void R_AtmosphereBuildParams( const trRefdef_t *refdef, const viewParms_t *view, atmosphereParams_t *p )
{
	Com_Memset(p, 0, sizeof(*p));
	p->rayleigh = MAX(0.0f, r_atmosphereRayleigh->value);
	p->mie = MAX(0.0f, r_atmosphereMie->value);
	p->ozone = MAX(0.0f, r_atmosphereOzone->value);
	p->mieG = Com_Clamp(-0.95f, 0.95f, r_atmosphereMieG->value);
	p->unitScale = MAX(1e-4f, r_atmosphereUnitScale->value);
	p->aerialScale = Com_Clamp(0.01f, 1000.0f, r_atmosphereAerialScale->value);
	p->groundZ = R_AtmosphereGroundZ();
	p->groundAltitude = r_atmosphereAltitude->value * 0.001f;

	VectorCopy(refdef->sunDir, p->sunDir);
	if ( VectorNormalize(p->sunDir) < 1e-4f )
		VectorSet(p->sunDir, 0.0f, 0.0f, 1.0f);
	const float sunRadius = DEG2RAD(Com_Clamp(0.05f, 10.0f, r_atmosphereSunSize->value) * 0.5f);
	p->cosSunRadius = cosf(sunRadius);

	p->cameraAltitude = p->groundAltitude + (view->ori.origin[2] - p->groundZ) * p->unitScale * 0.001f;

	R_AtmosphereSunColor(refdef, p->sunGround);
	const float intensity = MAX(0.0f, r_atmosphereSunIntensity->value);
	switch ( r_atmosphereSunColor->integer )
	{
	case 0:	// the map sun is the sun at the top of the atmosphere
		VectorCopy(p->sunGround, p->sunToa);
		break;
	case 2:	// white, as bright as the map sun
		{
			const float lum = R_AtmosphereLuminance(p->sunGround);
			VectorSet(p->sunToa, lum, lum, lum);
		}
		break;
	default:	// the map sun is the sun at the ground: undo the air above the camera
		{
			vec3_t T;
			R_AtmosphereTransmittance(p, MAX(p->cameraAltitude, 0.0f), p->sunDir[2], T);
			for ( int c = 0; c < 3; c++ )
				p->sunToa[c] = p->sunGround[c] / MAX(T[c], 1e-4f);
			const float groundLum = R_AtmosphereLuminance(p->sunGround);
			const float toaLum = R_AtmosphereLuminance(p->sunToa);
			if ( toaLum > groundLum * ATMO_SUN_TOA_MAX_GAIN && toaLum > 0.0f )
				VectorScale(p->sunToa, groundLum * ATMO_SUN_TOA_MAX_GAIN / toaLum, p->sunToa);
		}
		break;
	}
	VectorScale(p->sunToa, intensity, p->sunToa);

	// the sky of a sky portal map is the portal's scene: overlay only
	p->skyMode = Com_Clampi(0, 2, r_atmosphereSky->integer);
	if ( tr.world->skyboxportal )
		p->skyMode = 0;
	p->skyBlend = Com_Clamp(0.0f, 1.0f, r_atmosphereSkyBlend->value);
	p->sunGlow = MAX(0.0f, r_atmosphereSunGlow->value);
	p->start = MAX(0.0f, r_atmosphereStart->value);

	// the sky distance of the froxel composite (legacy fog cap of a global fog)
	p->skyDistance = view->zFar;
	if ( tr.world->globalFog )
		p->skyDistance = MAX(p->skyDistance, tr.world->globalFog->parms.depthForOpaque);

	p->debug = r_atmosphereDebug->integer;
	// one sun disc: RB_DrawSun owns it with r_drawSun 1, the skybox may have
	// a painted one in the overlay / blend modes
	p->drawDisc = (qboolean)(p->skyMode == 2 && !r_drawSun->integer);
}

static void RB_AtmosphereUniforms( const atmosphereParams_t *p, const viewParms_t *view, float draw,
	qboolean froxelLookup, vec4_t u[ATMO_UNIFORM_VEC4S] )
{
	VectorSet4(u[0], p->rayleigh, p->mie, p->ozone, p->mieG);
	VectorSet4(u[1], p->unitScale, p->aerialScale, p->groundZ, p->groundAltitude);
	VectorSet4(u[2], p->sunDir[0], p->sunDir[1], p->sunDir[2], p->cosSunRadius);
	VectorSet4(u[3], p->sunToa[0], p->sunToa[1], p->sunToa[2], p->drawDisc ? 1.0f : 0.0f);
	VectorSet4(u[4], view->ori.origin[0], view->ori.origin[1], view->ori.origin[2], p->skyDistance);
	VectorSet4(u[5], (float)p->skyMode, p->skyBlend, p->sunGlow, (float)p->debug);
	VectorSet4(u[6],
		view->viewportX / (float)tr.renderFbo->width,
		view->viewportY / (float)tr.renderFbo->height,
		view->viewportWidth / (float)tr.renderFbo->width,
		view->viewportHeight / (float)tr.renderFbo->height);
	VectorSet4(u[7], p->start, p->cameraAltitude, draw, froxelLookup ? 1.0f : 0.0f);
}

/*
============================================================

Back end

============================================================
*/

/*
=================
RB_AtmosphereActive

Does the current view get the atmosphere composite? Main world view only,
as the froxel fog: portals, mirrors, the sky portal and cubemap captures keep
their look.
=================
*/
qboolean RB_AtmosphereActive( void )
{
	if ( !r_atmosphere->integer || backEnd.atmosphereComposited )
		return qfalse;
	if ( backEnd.projection2D || backEnd.depthFill || backEnd.refractionFill || backEnd.framePostProcessed )
		return qfalse;
	const viewParms_t& view = backEnd.viewParms;
	if ( view.viewParmType != VPT_MAIN || view.isPortal || view.isSkyPortal ||
		(view.flags & (VPF_DEPTHSHADOW | VPF_NOCUBEMAPS | VPF_NODIFFUSEIBL)) )
		return qfalse;
	if ( view.targetFbo != NULL && view.targetFbo != tr.renderFbo )
		return qfalse;
	if ( glState.currentFBO != tr.renderFbo )
		return qfalse;
	if ( backEnd.refdef.rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE) )
		return qfalse;

	const char *reason = R_AtmosphereOffReason(RB_VolumetricCompositeActive());
	if ( reason )
	{
		if ( s_atmo.reasonWorld != tr.world || s_atmo.reason != reason )
		{
			ri.Printf(PRINT_ALL, "r_atmosphere: off, %s\n", reason);
			s_atmo.reasonWorld = tr.world;
		}
		s_atmo.reason = reason;
		return qfalse;
	}
	if ( s_atmo.reason )
		s_atmo.reasonWorld = NULL;
	s_atmo.reason = NULL;
	return qtrue;
}

static void RB_AtmosphereDrawLut( int lut, shaderProgram_t *sp, const vec4_t u[ATMO_UNIFORM_VEC4S] )
{
	FBO_Bind(s_atmoLutFbo[lut]);
	GL_SetViewportAndScissor(0, 0, s_atmoLutFbo[lut]->width, s_atmoLutFbo[lut]->height);
	GLSL_BindProgram(sp);
	GLSL_SetUniformVec4N(sp, UNIFORM_ATMOSPHERE, &u[0][0], ATMO_UNIFORM_VEC4S);
	GL_BindToTMU(tr.atmosphereTransmittanceImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.atmosphereMultiScatterImage, TB_NORMALMAP);
	RB_InstantTriangle();
}

static void RB_AtmosphereUpdateLuts( const atmosphereParams_t *p, const viewParms_t *view, qboolean needSkyView )
{
	const uint32_t mediumKey = R_AtmosphereMediumKey(p);
	const qboolean medium = (qboolean)(!s_atmo.lutValid || s_atmo.mediumKey != mediumKey);
	const uint32_t skyKey = R_AtmosphereSkyKey(p, mediumKey);
	const qboolean skyView = (qboolean)(needSkyView &&
		(medium || !s_atmo.skyViewValid || s_atmo.skyKey != skyKey));
	if ( !medium && !skyView )
		return;

	const int timer = RB_VolumetricBeginTimer("Atmosphere LUTs");
	GL_State(GLS_DEPTHTEST_DISABLE);
	GL_Cull(CT_TWO_SIDED);

	vec4_t u[ATMO_UNIFORM_VEC4S];
	RB_AtmosphereUniforms(p, view, 0.0f, qfalse, u);
	if ( medium )
	{
		// in this order: the multiple scattering LUT reads the transmittance
		// LUT (a pass never samples its own target)
		RB_AtmosphereDrawLut(ATMO_LUT_TRANSMITTANCE, &tr.atmosphereTransmittanceShader, u);
		RB_AtmosphereDrawLut(ATMO_LUT_MULTISCATTER, &tr.atmosphereMultiScatterShader, u);
		s_atmo.lutValid = qtrue;
		s_atmo.mediumKey = mediumKey;
		s_atmo.skyViewValid = qfalse;
		s_atmo.mediumBuilds++;
	}
	if ( needSkyView )
	{
		RB_AtmosphereDrawLut(ATMO_LUT_SKYVIEW, &tr.atmosphereSkyViewShader, u);
		s_atmo.skyViewValid = qtrue;
		s_atmo.skyKey = skyKey;
		s_atmo.skyViewBuilds++;
	}

	RB_VolumetricEndTimer(timer);
}

/*
=================
RB_AtmosphereComposite

Aerial perspective of everything drawn so far (sort <= SS_FOG, the sky
included) from the depth buffer: color * T + S, glow * T, or the analytic sky.
Called by RB_SubmitRenderPass with renderFbo bound, before the froxel fog
composite.
=================
*/
void RB_AtmosphereComposite( void )
{
	if ( !RB_AtmosphereActive() )
		return;

	backEnd.atmosphereComposited = qtrue;

	const viewParms_t *view = &backEnd.viewParms;
	atmosphereParams_t params;
	R_AtmosphereBuildParams(&backEnd.refdef, view, &params);
	s_atmo.last = params;
	s_atmo.hasLast = qtrue;

	FBO_t *oldFbo = glState.currentFBO;

	// the clouds' ambient light reads the sky-view LUT too (tr_clouds.cpp)
	const qboolean needSkyView = (qboolean)(params.skyMode > 0 ||
		params.debug == 5 || params.debug == 7 || RB_CloudsWantSkyView());
	RB_AtmosphereUpdateLuts(&params, view, needSkyView);

	const int timer = RB_VolumetricBeginTimer("Atmosphere composite");

	// MSAA: the depth texture is the resolve target
	if ( tr.msaaResolveFbo )
	{
		GL_SetViewportAndScissor(0, 0, tr.renderFbo->width, tr.renderFbo->height);
		FBO_FastBlit(tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	matrix_t viewProjection, invViewProjection;
	Matrix16Multiply(view->projectionMatrix, view->world.modelViewMatrix, viewProjection);
	if ( !R_VolumetricInvertMatrix(viewProjection, invViewProjection) )
		Matrix16Identity(invViewProjection);

	shaderProgram_t *sp = &tr.atmosphereCompositeShader;
	FBO_Bind(tr.atmosphereCompositeFbo);
	GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
	GL_Cull(CT_TWO_SIDED);
	GLSL_BindProgram(sp);
	GLSL_SetUniformMatrix4x4(sp, UNIFORM_ATMOSPHEREINVVIEWPROJECTION, invViewProjection, 1);

	// debug 6 reads the froxel volume at the pixel
	const qboolean froxelLookup = (qboolean)(params.debug == 6 && RB_VolumetricBindLookup());

	GL_BindToTMU(tr.renderDepthImage, TB_COLORMAP);
	GL_BindToTMU(tr.atmosphereTransmittanceImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.atmosphereMultiScatterImage, TB_NORMALMAP);
	GL_BindToTMU(tr.atmosphereSkyViewImage, TB_DELUXEMAP);

	// color * T.rgb (blend ZERO, SRC_COLOR), then + S (ONE, ONE); the same for
	// the glow. The destination alpha is kept: GL_State only masks all
	// channels, so mask alpha directly and restore the full mask afterwards.
	vec4_t u[ATMO_UNIFORM_VEC4S];
	for ( int pass = 0; pass < 2; pass++ )
	{
		RB_AtmosphereUniforms(&params, view, (float)pass, froxelLookup, u);
		GLSL_SetUniformVec4N(sp, UNIFORM_ATMOSPHERE, &u[0][0], ATMO_UNIFORM_VEC4S);
		GL_State(GLS_DEPTHTEST_DISABLE | (pass == 0 ?
			(GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR) : (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)));
		qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		RB_InstantTriangle();
	}
	qglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	GL_ResetScreenAuxWrite();

	// a debug view replaces the frame: no froxel fog over it
	if ( params.debug > 0 )
		backEnd.volumetricComposited = qtrue;

	RB_VolumetricEndTimer(timer);

	FBO_Bind(oldFbo);
	GL_SetViewportAndScissor(view->viewportX, view->viewportY, view->viewportWidth, view->viewportHeight);
}

/*
=================
RB_AtmosphereCloudUniforms

The cloud programs (tr_clouds.cpp) light and fog the clouds with this view's
atmosphere: u_Atmosphere of the last composite, with the aerial distance
scale at 1 (the clouds are at their physical distance), and the LUTs on the
units of the atmosphere programs. False when the atmosphere did not composite
this view.
=================
*/
qboolean RB_AtmosphereCloudUniforms( shaderProgram_t *sp )
{
	if ( !backEnd.atmosphereComposited || !s_atmo.hasLast || !s_atmo.lutValid )
		return qfalse;

	atmosphereParams_t params = s_atmo.last;
	params.aerialScale = 1.0f;
	vec4_t u[ATMO_UNIFORM_VEC4S];
	RB_AtmosphereUniforms(&params, &backEnd.viewParms, 1.0f, qfalse, u);
	GLSL_SetUniformVec4N(sp, UNIFORM_ATMOSPHERE, &u[0][0], ATMO_UNIFORM_VEC4S);
	GL_BindToTMU(tr.atmosphereTransmittanceImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.atmosphereMultiScatterImage, TB_NORMALMAP);
	GL_BindToTMU(tr.atmosphereSkyViewImage, TB_DELUXEMAP);
	return (qboolean)s_atmo.skyViewValid;
}

/*
============================================================

Console

============================================================
*/

/*
=================
R_AtmosphereInfo_f

r_atmosphereInfo: parameters, sun, units and LUT builds
=================
*/
void R_AtmosphereInfo_f( void )
{
	ri.Printf(PRINT_ALL, "r_atmosphere %d: %s\n", r_atmosphere->integer,
		!r_atmosphere->integer ? "off" : s_atmo.reason ? s_atmo.reason : "on");
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "no map loaded\n");
		return;
	}
	ri.Printf(PRINT_ALL, "map sun: %s, direction %.3f %.3f %.3f\n", tr.sunParsed ? "parsed" : "none (default)",
		tr.sunDirection[0], tr.sunDirection[1], tr.sunDirection[2]);
	ri.Printf(PRINT_ALL, "ground z %.1f (%s), sky portal %s, global fog %s\n", R_AtmosphereGroundZ(),
		Q_stricmp(r_atmosphereGroundZ->string, "auto") ? "cvar" : "lowest floor",
		tr.world->skyboxportal ? "yes" : "no", tr.world->globalFog ? "yes" : "no");
	ri.Printf(PRINT_ALL, "LUT builds: medium %d, sky-view %d\n", s_atmo.mediumBuilds, s_atmo.skyViewBuilds);
	if ( !s_atmo.hasLast )
		return;

	const atmosphereParams_t *p = &s_atmo.last;
	vec3_t T;
	R_AtmosphereTransmittance(p, MAX(p->cameraAltitude, 0.0f), p->sunDir[2], T);
	ri.Printf(PRINT_ALL, "last composite:\n");
	ri.Printf(PRINT_ALL, "  units: %g m per unit, aerial scale %g: 1000 units = %g m of air\n",
		p->unitScale, p->aerialScale, 1000.0f * p->unitScale * p->aerialScale);
	ri.Printf(PRINT_ALL, "  camera altitude %.1f m (ground %.1f m), sky distance %.0f units = %.0f m of air\n",
		p->cameraAltitude * 1000.0f, p->groundAltitude * 1000.0f, p->skyDistance,
		p->skyDistance * p->unitScale * p->aerialScale);
	ri.Printf(PRINT_ALL, "  sun elevation %.1f deg, transmittance to the sun %.3f %.3f %.3f\n",
		RAD2DEG(asinf(Com_Clamp(-1.0f, 1.0f, p->sunDir[2]))), T[0], T[1], T[2]);
	ri.Printf(PRINT_ALL, "  sun (map) %.3f %.3f %.3f, top of the atmosphere %.3f %.3f %.3f (r_atmosphereSunColor %d)\n",
		p->sunGround[0], p->sunGround[1], p->sunGround[2], p->sunToa[0], p->sunToa[1], p->sunToa[2],
		r_atmosphereSunColor->integer);
	ri.Printf(PRINT_ALL, "  sky mode %d%s, sun disc %s, debug %d\n", p->skyMode,
		(tr.world->skyboxportal && r_atmosphereSky->integer) ? " (sky portal: overlay forced)" : "",
		p->drawDisc ? "atmosphere" : (r_drawSun->integer ? "RB_DrawSun" : "none"), p->debug);
	ri.Printf(PRINT_ALL, "  HDR buffer: %s\n", tr.linearLight ? "scene linear" :
		"display encoded (composited in buffer space, as the froxel fog)");
}
