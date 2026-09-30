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

// Water on the camera lens (r_rainLens), see docs/rend2-rain-lens.md.
//
// Optional polish, off by default. The persistent lens state lives in
// tr_lenswater.cpp (droplet agents, wetness / film field, sheets, weather
// controller). This file feeds it the weather, camera and world input,
// rasterises it into a lower resolution lens field (glsl/rainlens.glsl:
// RG = unscaled UV offset, B = optical weight, A = blur radius) and runs one
// fullscreen HDR composite (glsl/rainlens_composite.glsl) in RB_PostProcess
// after the SMAA T2x temporal resolve and motion blur, before bloom
// extraction and tone mapping. The composite refracts the scene, reflects the
// nearest environment cubemap (or the light grid ambient) with Schlick
// Fresnel and adds glints of the dominant light and the brightest nearby
// dynamic light. It writes rainLensImage, a dedicated full resolution HDR
// target that the caller uses as the scene source for the remaining color
// passes; modern bloom also reads the lens field to refract the emissive MRT.
//
// World water reaches the lens through RE_AddLensWaterEvent (renderer
// extension GetRefLensWaterAPI: cgame water events, the efx LensWater
// primitive), the "lenswater" world effect command and the map's env.json
// LensWaterEmitters. Rain exposure only controls new rain: under cover the
// existing water keeps moving, merging and drying.

#include "tr_local.h"
#include "tr_weather.h"
#include "tr_lenswater.h"
#include "json.h"

#include <chrono>
#include <cstring>
#include <vector>

using namespace lenswater;

namespace {
LensWater s_water;
std::vector<float> s_instanceData;
int s_maxInstances;
int s_lastRefdefTime;
qboolean s_clockValid;
qboolean s_debugOutput;
unsigned s_lastFrame;

// front end water transition state (R_RainLensInput)
qboolean s_inWaterValid;
qboolean s_inWater;
int s_waterEnterTime;
int s_lastEmergeTime;
qboolean s_submergePending;
qboolean s_emergePending;
// The game reports the player emerging (LENSWATER_F_LOCAL, first person)
// and the contents test of the camera detects it too (third person, older
// games): whichever comes first within a second wins.
int s_lastLocalEmergeTime;

// world space events posted since the last main view (front end)
const int MAX_WORLD_EVENTS = 16;
refLensWaterEvent_t s_worldEvents[MAX_WORLD_EVENTS];
int s_numWorldEvents;

// camera motion for r_rainLensInertia (back end)
qboolean s_motionValid;
vec3_t s_prevOrigin;
vec3_t s_prevVelocity;
vec3_t s_accel;

// r_rainLensPBO upload rings
const int PBO_RING = 3;
GLuint s_filmPbo[PBO_RING];
GLuint s_instancePbo[PBO_RING];
size_t s_filmPboSize, s_instancePboSize;
int s_pboIndex;

// own GPU timestamps for rainlens_stats (independent of r_speeds)
const int GPU_RING = 3;
struct gpuTiming_t
{
	GLuint queries[3];	// field start, composite start, end
	qboolean issued;
};
gpuTiming_t s_gpuTimings[GPU_RING];
int s_gpuTimingIndex;
qboolean s_gpuTimingsCreated;
float s_gpuFieldMs, s_gpuCompositeMs;
float s_uploadMicroseconds;

const int MAX_LENS_DROPS = 128;
const int MAX_LENS_MICRO = 256;
const int MAX_LENS_SHEETS = 8;
const int OPTICS_VEC4S = 10;
const int DEBUG_VEC4S = 4;

int QualityLevel( void )
{
	return Com_Clampi(0, 2, r_rainLensQuality->integer);
}

int DropLimit( void )
{
	static const int limits[3] = { 48, 96, 128 };
	if ( r_rainLensAgentLimit->integer > 0 )
		return Com_Clampi(8, MAX_LENS_DROPS, r_rainLensAgentLimit->integer);
	return limits[QualityLevel()];
}

Params CurrentParams( void )
{
	Params p;
	p.density = Com_Clamp(0.0f, 2.0f, r_rainLensDensity->value);
	p.dropSize = Com_Clamp(0.25f, 4.0f, r_rainLensDropSize->value);
	p.pinning = Com_Clamp(0.1f, 4.0f, r_rainLensPinning->value);
	p.merge = Com_Clamp(0.0f, 2.0f, r_rainLensMerge->value);
	p.filmDecay = Com_Clamp(0.1f, 4.0f, r_rainLensFilmDecay->value);
	p.wetDecay = Com_Clamp(0.1f, 4.0f, r_rainLensWetDecay->value);
	p.heavyFlow = Com_Clamp(0.0f, 4.0f, r_rainLensHeavyFlow->value);
	p.peripheralBias = Com_Clamp(0.0f, 2.0f, r_rainLensPeripheralBias->value);
	p.inertia = Com_Clamp(0.0f, 2.0f, r_rainLensInertia->value);
	p.maxDrops = DropLimit();
	p.maxMicro = MAX_LENS_MICRO;
	p.maxSheets = MAX_LENS_SHEETS;
	return p;
}

Profile ProfileFromWeather( int weather, float intensity )
{
	switch ( weather )
	{
	case RAIN_WEATHER_LIGHT: return PROFILE_LIGHT;
	case RAIN_WEATHER_NORMAL: return PROFILE_NORMAL;
	case RAIN_WEATHER_HEAVY: return PROFILE_HEAVY;
	case RAIN_WEATHER_ACID: return PROFILE_ACID;
	default:
		// unknown source: classify by particle count
		return intensity < 0.3f ? PROFILE_LIGHT : (intensity < 0.7f ? PROFILE_NORMAL : PROFILE_HEAVY);
	}
}

// nominal intensity of a forced profile (rainlens_profile without rain)
float ProfileIntensity( Profile profile )
{
	switch ( profile )
	{
	case PROFILE_LIGHT: return 0.2f;
	case PROFILE_HEAVY: return 1.0f;
	default: return 0.4f;
	}
}

// lens space (x right, y up, z toward the viewer) of a world direction
void WorldToLens( const vec3_t axis[3], const vec3_t world, float out[3] )
{
	out[0] = -DotProduct(world, axis[1]);
	out[1] = DotProduct(world, axis[2]);
	out[2] = -DotProduct(world, axis[0]);
}

float Luma( const vec3_t c )
{
	return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

void ResetState( void )
{
	s_lastRefdefTime = 0;
	s_clockValid = qfalse;
	s_debugOutput = qfalse;
	s_lastFrame = 0;
	s_inWaterValid = qfalse;
	s_inWater = qfalse;
	s_waterEnterTime = 0;
	s_lastEmergeTime = -100000;
	s_submergePending = qfalse;
	s_emergePending = qfalse;
	s_lastLocalEmergeTime = -100000;
	s_numWorldEvents = 0;
	s_motionValid = qfalse;
}

void DeleteGLObjects( void )
{
	for ( int i = 0; i < PBO_RING; i++ )
	{
		if ( s_filmPbo[i] )
			qglDeleteBuffers(1, &s_filmPbo[i]);
		if ( s_instancePbo[i] )
			qglDeleteBuffers(1, &s_instancePbo[i]);
		s_filmPbo[i] = s_instancePbo[i] = 0;
	}
	s_filmPboSize = s_instancePboSize = 0;
	if ( s_gpuTimingsCreated )
	{
		for ( int i = 0; i < GPU_RING; i++ )
			qglDeleteQueries(3, s_gpuTimings[i].queries);
	}
	Com_Memset(s_gpuTimings, 0, sizeof(s_gpuTimings));
	s_gpuTimingsCreated = qfalse;
}

// Uploads a float texture region, directly or through a PBO ring slot
// (r_rainLensPBO): the driver can copy from the buffer asynchronously.
void UploadFloatTexture( image_t *image, GLuint *pbos, size_t *pboSize,
	int width, int height, GLenum format, const float *data, size_t bytes )
{
	GL_BindToTMU(image, TB_COLORMAP);
	// GL_BindToTMU skips the unit switch when the image is already bound
	GL_SelectTexture(TB_COLORMAP);

	if ( !r_rainLensPBO->integer )
	{
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, GL_FLOAT, data);
		return;
	}

	GLuint &pbo = pbos[s_pboIndex];
	if ( !pbo || *pboSize < bytes )
	{
		// (re)allocate every slot at the largest size seen
		for ( int i = 0; i < PBO_RING; i++ )
		{
			if ( !pbos[i] )
				qglGenBuffers(1, &pbos[i]);
			qglBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
			qglBufferData(GL_PIXEL_UNPACK_BUFFER, bytes, NULL, GL_STREAM_DRAW);
		}
		*pboSize = bytes;
	}
	qglBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
	void *mapped = qglMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, bytes,
		GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
	if ( mapped )
	{
		memcpy(mapped, data, bytes);
		qglUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, GL_FLOAT, NULL);
	}
	qglBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	if ( !mapped )
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, GL_FLOAT, data);
}

void GpuTimestamp( int which )
{
	if ( !glRefConfig.timerQuery )
		return;
	if ( !s_gpuTimingsCreated )
	{
		for ( int i = 0; i < GPU_RING; i++ )
			qglGenQueries(3, s_gpuTimings[i].queries);
		s_gpuTimingsCreated = qtrue;
	}
	qglQueryCounter(s_gpuTimings[s_gpuTimingIndex].queries[which], GL_TIMESTAMP);
	if ( which == 2 )
		s_gpuTimings[s_gpuTimingIndex].issued = qtrue;
}

// Reads the oldest ring entry if the GPU is done with it; never blocks.
void GpuReadTimings( void )
{
	if ( !s_gpuTimingsCreated )
		return;
	gpuTiming_t &timing = s_gpuTimings[(s_gpuTimingIndex + 1) % GPU_RING];
	if ( !timing.issued )
		return;
	GLint available = 0;
	qglGetQueryObjectiv(timing.queries[2], GL_QUERY_RESULT_AVAILABLE, &available);
	if ( !available )
		return;
	GLuint64 t[3];
	for ( int i = 0; i < 3; i++ )
		qglGetQueryObjectui64v(timing.queries[i], GL_QUERY_RESULT, &t[i]);
	s_gpuFieldMs = (float)(t[1] - t[0]) * 1e-6f;
	s_gpuCompositeMs = (float)(t[2] - t[1]) * 1e-6f;
	timing.issued = qfalse;
}

/*
Resolves the world events posted since the last main view against it:
distance falloff, facing, lens side. Events of the local viewer (the player
emerging) take over from the contents based emerge detection.
*/
void ResolveWorldEvents( const trRefdef_t *refdef, qboolean submerged, rainLensInput_t *input )
{
	vec3_t right;
	VectorScale(refdef->viewaxis[1], -1.0f, right);

	for ( int i = 0; i < s_numWorldEvents; i++ )
	{
		const refLensWaterEvent_t &event = s_worldEvents[i];
		if ( event.type == LENSWATER_EMERGE && (event.flags & LENSWATER_F_LOCAL) )
		{
			// the contents test already emerged the camera: one burst only
			if ( refdef->time - s_lastEmergeTime >= 0 && refdef->time - s_lastEmergeTime < 1000 )
				continue;
			s_lastLocalEmergeTime = refdef->time;
			s_emergePending = qfalse;
		}
		if ( submerged || input->numEvents >= RAINLENS_MAX_EVENTS )
			continue;

		float strength = Com_Clamp(0.0f, 2.0f, event.strength > 0.0f ? event.strength : 1.0f);
		float side[2] = { 0.0f, 0.0f };
		if ( event.flags & LENSWATER_F_ORIGIN )
		{
			Vec2 lensSide;
			strength *= ResolveWorldEvent(event.origin, event.radius, refdef->vieworg,
				refdef->viewaxis[0], right, refdef->viewaxis[2], lensSide);
			side[0] = lensSide.x;
			side[1] = lensSide.y;
		}
		else if ( event.flags & LENSWATER_F_DIR )
		{
			// water travelling along dir comes from the opposite side
			float dir[3];
			WorldToLens(refdef->viewaxis, event.dir, dir);
			side[0] = -dir[0];
			side[1] = -dir[1];
		}
		if ( strength < 0.02f )
			continue;

		rainLensEvent_t &out = input->events[input->numEvents++];
		out.type = event.type;
		out.strength = strength;
		out.dirLens[0] = side[0];
		out.dirLens[1] = side[1];
		out.duration = event.duration;
	}
	s_numWorldEvents = 0;
}

/*
Map emitters (env.json LensWaterEmitters): the strongest spray near the camera
feeds a continuous spray input; splash emitters fire at random intervals.
*/
void EvaluateEmitters( const trRefdef_t *refdef, rainLensInput_t *input )
{
	world_t *world = tr.world;
	if ( !world || !world->numLensWaterEmitters )
		return;

	vec3_t right;
	VectorScale(refdef->viewaxis[1], -1.0f, right);

	for ( int i = 0; i < world->numLensWaterEmitters; i++ )
	{
		lensWaterEmitter_t &emitter = world->lensWaterEmitters[i];
		Vec2 side;
		float strength = emitter.strength * ResolveWorldEvent(emitter.origin, emitter.radius,
			refdef->vieworg, refdef->viewaxis[0], right, refdef->viewaxis[2], side);
		if ( VectorLengthSquared(emitter.dir) > 0.0f )
		{
			// only water travelling toward the camera reaches it
			vec3_t toCamera;
			VectorSubtract(refdef->vieworg, emitter.origin, toCamera);
			VectorNormalize(toCamera);
			strength *= Com_Clamp(0.0f, 1.0f, 0.25f + DotProduct(toCamera, emitter.dir));
		}

		if ( emitter.type == LENSWATER_SPRAY )
		{
			if ( strength > input->sprayStrength )
			{
				input->sprayStrength = strength;
				input->sprayDir[0] = side.x;
				input->sprayDir[1] = side.y;
			}
			continue;
		}

		// splash emitter: exponential intervals in game time
		const int interval = (int)(Q_max(emitter.interval, 0.1f) * 1000.0f);
		if ( emitter.nextTime == 0 || emitter.nextTime - refdef->time > interval * 10 )
			emitter.nextTime = refdef->time + (int)(interval * (0.25f + flrand(0.0f, 1.0f)));
		if ( refdef->time < emitter.nextTime )
			continue;
		emitter.nextTime = refdef->time + (int)(-logf(1.0f - 0.999f * flrand(0.0f, 1.0f)) * interval);
		if ( strength >= 0.02f && input->numEvents < RAINLENS_MAX_EVENTS )
		{
			rainLensEvent_t &out = input->events[input->numEvents++];
			out.type = LENSWATER_SPLASH;
			out.strength = strength;
			out.dirLens[0] = side.x;
			out.dirLens[1] = side.y;
			out.duration = 0.0f;
		}
	}
}

/*
Light sources of the lens optics, lens space: the dominant light (the sun when
the camera is outside under a sky with a sun, otherwise the light grid
direction), its radiance at the camera from the light grid, the ambient that
stands in for a missing cubemap, and the brightest nearby dynamic light.
*/
void GatherOptics( const trRefdef_t *refdef, qboolean outside, rainLensInput_t *input )
{
	vec3_t ambient, directed, gridDir;
	VectorClear(ambient);
	VectorClear(directed);
	VectorSet(gridDir, 0.0f, 0.0f, 1.0f);
	vec3_t origin;
	VectorCopy(refdef->vieworg, origin);
	if ( R_LightForPoint(origin, ambient, directed, gridDir) )
	{
		VectorScale(ambient, 1.0f / 255.0f, ambient);
		VectorScale(directed, 1.0f / 255.0f, directed);
	}
	VectorCopy(ambient, input->ambient);
	vec3_t total;
	VectorAdd(ambient, directed, total);
	input->cameraLuma = Luma(total);

	vec3_t keyDir;
	if ( tr.sunParsed && outside )
	{
		VectorCopy(tr.sunDirection, keyDir);
		VectorCopy(directed, input->keyColor);
	}
	else
	{
		// indoors: whatever lights the camera, less punchy than the sun
		VectorCopy(gridDir, keyDir);
		VectorScale(directed, 0.5f, input->keyColor);
	}
	VectorNormalize(keyDir);
	WorldToLens(refdef->viewaxis, keyDir, input->keyDir);

	// brightest dynamic light near the camera (sabers, muzzle flashes, lamps)
	float best = 0.0f;
	for ( int i = 0; i < refdef->num_dlights; i++ )
	{
		const dlight_t *light = &refdef->dlights[i];
		if ( light->areaType != 0 || light->radius <= 0.0f )
			continue;
		vec3_t delta;
		VectorSubtract(light->origin, refdef->vieworg, delta);
		const float dist = VectorLength(delta);
		float falloff = Com_Clamp(0.0f, 1.0f, 1.0f - dist / (light->radius * 2.0f));
		falloff *= falloff;
		const float score = Luma(light->color) * falloff;
		if ( score <= best )
			continue;
		best = score;
		if ( dist > 1e-3f )
			VectorScale(delta, 1.0f / dist, delta);
		else
			VectorSet(delta, refdef->viewaxis[0][0], refdef->viewaxis[0][1], refdef->viewaxis[0][2]);
		WorldToLens(refdef->viewaxis, delta, input->pointDir);
		VectorScale(light->color, falloff * 1.5f, input->pointColor);
	}

	vec3_t cubePoint;
	VectorCopy(refdef->vieworg, cubePoint);
	input->cubemapIndex = R_CubemapForPoint(cubePoint);
}

qboolean JsonVector( const char *json, const char *jsonEnd, const char *name, vec3_t out )
{
	const char *value = JSON_ObjectGetNamedValue(json, jsonEnd, name);
	if ( !value || JSON_ValueGetType(value, jsonEnd) != JSONTYPE_ARRAY )
		return qfalse;
	const char *indexes[3];
	if ( JSON_ArrayGetIndex(value, jsonEnd, indexes, 3) < 3 )
		return qfalse;
	for ( int i = 0; i < 3; i++ )
		out[i] = JSON_ValueGetFloat(indexes[i], jsonEnd);
	return qtrue;
}

qboolean JsonFloat( const char *json, const char *jsonEnd, const char *name, float *out )
{
	const char *value = JSON_ObjectGetNamedValue(json, jsonEnd, name);
	if ( !value || JSON_ValueGetType(value, jsonEnd) != JSONTYPE_VALUE )
		return qfalse;
	*out = JSON_ValueGetFloat(value, jsonEnd);
	return qtrue;
}

int EventTypeFromName( const char *name )
{
	if ( !Q_stricmp(name, "splash") )
		return LENSWATER_SPLASH;
	if ( !Q_stricmp(name, "spray") )
		return LENSWATER_SPRAY;
	if ( !Q_stricmp(name, "emerge") )
		return LENSWATER_EMERGE;
	return -1;
}
} // namespace

void R_CreateRainLensImages( int width, int height, int hdrFormat )
{
	ResetState();
	tr.rainLensImage = NULL;
	tr.rainLensFieldImage = NULL;
	tr.rainLensFilmImage = NULL;
	tr.rainLensInstanceImage = NULL;
	tr.rainLensMipImage = NULL;

	if ( !r_rainLens->integer )
	{
		// no allocation, no simulation
		s_water = LensWater();
		s_instanceData.clear();
		s_instanceData.shrink_to_fit();
		return;
	}

	if ( !r_hdr->integer )
	{
		ri.Printf(PRINT_WARNING, "r_rainLens needs r_hdr 1, lens rain disabled\n");
		return;
	}

	tr.rainLensImage = R_CreateImage(
		"*rainLens", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);

	// The lens field is quality bounded, not proportional to the display:
	// 256 / 360 / 540 texels high; only the composite runs at full resolution.
	static const int fieldHeights[3] = { 256, 360, 540 };
	int fieldHeight = r_rainLensFieldHeight->integer > 0 ?
		Com_Clampi(64, 2160, r_rainLensFieldHeight->integer) : fieldHeights[QualityLevel()];
	fieldHeight = Q_min(height, fieldHeight);
	const int fieldWidth = Q_max(1, width * fieldHeight / height);
	tr.rainLensFieldImage = R_CreateImage(
		"*rainLensField", NULL, fieldWidth, fieldHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);

	// persistent wetness (R) and thin film (G)
	const int filmHeight = Q_min(height, 256);
	const int filmWidth = Q_max(1, width * filmHeight / height);
	tr.rainLensFilmImage = R_CreateImage(
		"*rainLensFilm", NULL, filmWidth, filmHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RG16F);

	// droplet / sheet instance records, fetched by gl_InstanceID
	s_maxInstances = MAX_LENS_DROPS + MAX_LENS_MICRO + MAX_LENS_SHEETS;
	tr.rainLensInstanceImage = R_CreateImage(
		"*rainLensInstances", NULL, s_maxInstances, INSTANCE_TEXELS, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA32F);
	s_instanceData.assign((size_t)s_maxInstances * INSTANCE_FLOATS, 0.0f);

	// r_rainLensMipBlur: drop defocus from a mipped half resolution copy of
	// the scene instead of the 3 / 5 tap disc
	if ( r_rainLensMipBlur->integer )
	{
		tr.rainLensMipImage = R_CreateImage(
			"*rainLensMip", NULL, Q_max(1, width / 2), Q_max(1, height / 2), IMGTYPE_COLORALPHA,
			IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE | IMGFLAG_MIPMAP, GL_RGBA16F);
	}

	const Profile override = s_water.GetProfileOverride();
	s_water = LensWater();
	s_water.Init(filmWidth, filmHeight);
	s_water.SetProfileOverride(override);
}

void R_ShutdownRainLens( void )
{
	DeleteGLObjects();
	s_numWorldEvents = 0;
}

float R_RainLensRefractionScale( void )
{
	// the profile scale is valid after the first update
	const float profile = s_water.Current().refraction;
	return Com_Clamp(0.0f, 4.0f, r_rainLensRefraction->value) * (profile > 0.0f ? profile : 1.0f);
}

/*
=============
RE_AddLensWaterEvent

Renderer extension (GetRefLensWaterAPI), front end: queues a world space
water event for the next main view.
=============
*/
void RE_AddLensWaterEvent( const refLensWaterEvent_t *event )
{
	if ( !event || !tr.rainLensImage )
		return;
	if ( event->type < LENSWATER_SPLASH || event->type > LENSWATER_EMERGE )
		return;

	if ( s_numWorldEvents < MAX_WORLD_EVENTS )
	{
		s_worldEvents[s_numWorldEvents++] = *event;
		return;
	}
	// full: replace the weakest
	int weakest = 0;
	for ( int i = 1; i < MAX_WORLD_EVENTS; i++ )
	{
		if ( s_worldEvents[i].strength < s_worldEvents[weakest].strength )
			weakest = i;
	}
	if ( s_worldEvents[weakest].strength < event->strength )
		s_worldEvents[weakest] = *event;
}

/*
=============
R_LensWaterCommand

"lenswater <splash|spray|emerge> [strength] [duration] [x y z radius]" of
RE_WorldEffectCommand (r_we, cgame world effect commands, scripts).
=============
*/
void R_LensWaterCommand( const char *args )
{
	char buffer[256];
	Q_strncpyz(buffer, args ? args : "", sizeof(buffer));

	const char *tokens[8];
	int count = 0;
	for ( char *p = strtok(buffer, " \t\r\n"); p && count < 8; p = strtok(NULL, " \t\r\n") )
		tokens[count++] = p;

	const int type = count > 0 ? EventTypeFromName(tokens[0]) : -1;
	if ( type < 0 )
	{
		ri.Printf(PRINT_ALL, "usage: lenswater <splash|spray|emerge> [strength] [duration] [x y z radius]\n");
		return;
	}

	refLensWaterEvent_t event = {};
	event.type = type;
	event.strength = count > 1 ? (float)atof(tokens[1]) : 1.0f;
	event.duration = count > 2 ? (float)atof(tokens[2]) : 0.0f;
	if ( count >= 7 )
	{
		VectorSet(event.origin, (float)atof(tokens[3]), (float)atof(tokens[4]), (float)atof(tokens[5]));
		event.radius = (float)atof(tokens[6]);
		event.flags |= LENSWATER_F_ORIGIN;
	}
	RE_AddLensWaterEvent(&event);
}

/*
=============
R_LoadLensWaterEmittersJson

env.json "LensWaterEmitters": [ { "Origin": [x, y, z], "Radius": r,
"Strength": s, "Type": "spray" | "splash", "Direction": [x, y, z],
"Interval": seconds } ]
=============
*/
void R_LoadLensWaterEmittersJson( world_t *world, const char *json, const char *jsonEnd, const char *filename )
{
	world->numLensWaterEmitters = 0;
	world->lensWaterEmitters = NULL;

	const char *array = JSON_ObjectGetNamedValue(json, jsonEnd, "LensWaterEmitters");
	if ( !array )
		return;
	if ( JSON_ValueGetType(array, jsonEnd) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: LensWaterEmitters is not an array\n", filename);
		return;
	}

	const int count = Q_min((int)JSON_ArrayGetIndex(array, jsonEnd, NULL, 0), 64);
	if ( count <= 0 )
		return;

	world->lensWaterEmitters = (lensWaterEmitter_t *)Hunk_Alloc(count * sizeof(lensWaterEmitter_t), h_low);
	for ( int i = 0; i < count; i++ )
	{
		const char *entry = JSON_ArrayGetValue(array, jsonEnd, i);
		if ( !entry || JSON_ValueGetType(entry, jsonEnd) != JSONTYPE_OBJECT )
		{
			ri.Printf(PRINT_WARNING, "%s: LensWaterEmitters[%d] is not an object\n", filename, i);
			continue;
		}

		lensWaterEmitter_t emitter = {};
		if ( !JsonVector(entry, jsonEnd, "Origin", emitter.origin) )
		{
			ri.Printf(PRINT_WARNING, "%s: LensWaterEmitters[%d] has no Origin\n", filename, i);
			continue;
		}
		emitter.radius = 256.0f;
		JsonFloat(entry, jsonEnd, "Radius", &emitter.radius);
		emitter.strength = 1.0f;
		JsonFloat(entry, jsonEnd, "Strength", &emitter.strength);
		emitter.interval = 2.0f;
		JsonFloat(entry, jsonEnd, "Interval", &emitter.interval);
		if ( JsonVector(entry, jsonEnd, "Direction", emitter.dir) )
			VectorNormalize(emitter.dir);

		char type[32] = "spray";
		const char *typeValue = JSON_ObjectGetNamedValue(entry, jsonEnd, "Type");
		if ( typeValue )
			JSON_ValueGetString(typeValue, jsonEnd, type, sizeof(type));
		emitter.type = EventTypeFromName(type);
		if ( emitter.type != LENSWATER_SPRAY && emitter.type != LENSWATER_SPLASH )
		{
			ri.Printf(PRINT_WARNING, "%s: LensWaterEmitters[%d]: Type must be spray or splash\n", filename, i);
			continue;
		}
		if ( emitter.radius <= 0.0f || emitter.strength <= 0.0f )
		{
			ri.Printf(PRINT_WARNING, "%s: LensWaterEmitters[%d]: Radius and Strength must be > 0\n", filename, i);
			continue;
		}
		world->lensWaterEmitters[world->numLensWaterEmitters++] = emitter;
	}

	ri.Printf(PRINT_ALL, "%s: %d lens water emitter%s\n", filename, world->numLensWaterEmitters,
		(world->numLensWaterEmitters == 1) ? "" : "s");
}

/*
=============
R_RainLensInput

Front end, R_AddPostProcessCmd. The weather, world and camera input of the
lens: rain intensity and subtype, whether new rain reaches it (main world
view outside while it rains), how much it faces into the rain, water events
and emitters around the camera, water surface transitions of the camera and
the lights of the lens optics. R_IsOutside mutates a cache, so this runs
here rather than in the back end.
=============
*/
void R_RainLensInput( const trRefdef_t *refdef, const viewParms_t *viewParms, rainLensInput_t *input )
{
	Com_Memset(input, 0, sizeof(*input));

	if ( !r_rainLens->integer || !tr.rainLensImage || !tr.world )
		return;

	if ( refdef->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL) )
		return;

	if ( viewParms->viewParmType != VPT_MAIN || viewParms->isPortal || viewParms->isMirror
		|| (viewParms->flags & VPF_DEPTHSHADOW) )
		return;

	input->active = qtrue;

	// Leaving water seeds a lot of water at once; while submerged the lens
	// is not drawn and entering water wipes it. Eyes bobbing at the surface
	// must not emerge every frame: the camera has to be under for a moment,
	// and emerging repeats at most once a second.
	const qboolean inWater = (qboolean)((ri.CM_PointContents(refdef->vieworg, 0)
		& (CONTENTS_WATER | CONTENTS_SLIME)) != 0);
	if ( refdef->time < s_lastEmergeTime || refdef->time < s_waterEnterTime
		|| refdef->time < s_lastLocalEmergeTime )
	{
		// game time restarted (map change)
		s_waterEnterTime = 0;
		s_lastEmergeTime = -100000;
		s_lastLocalEmergeTime = -100000;
	}
	if ( s_inWaterValid && inWater != s_inWater )
	{
		if ( inWater )
		{
			s_submergePending = qtrue;
			s_waterEnterTime = refdef->time;
		}
		else if ( refdef->time - s_waterEnterTime >= 250 && refdef->time - s_lastEmergeTime >= 1000
			&& refdef->time - s_lastLocalEmergeTime >= 1000 )
		{
			s_emergePending = qtrue;
			s_lastEmergeTime = refdef->time;
		}
	}
	s_inWater = inWater;
	s_inWaterValid = qtrue;
	input->submerged = inWater;

	ResolveWorldEvents(refdef, inWater, input);
	if ( s_submergePending )
	{
		// first, so it clears before anything else of this frame lands
		if ( input->numEvents == RAINLENS_MAX_EVENTS )
			input->numEvents--;
		memmove(&input->events[1], &input->events[0], input->numEvents * sizeof(input->events[0]));
		input->events[0].type = RAINLENS_EVENT_SUBMERGE;
		input->events[0].strength = 1.0f;
		input->numEvents++;
		s_submergePending = qfalse;
	}
	if ( s_emergePending && input->numEvents < RAINLENS_MAX_EVENTS )
	{
		rainLensEvent_t &out = input->events[input->numEvents++];
		Com_Memset(&out, 0, sizeof(out));
		out.type = LENSWATER_EMERGE;
		out.strength = 1.0f;
		s_emergePending = qfalse;
	}
	if ( !inWater )
		EvaluateEmitters(refdef, input);

	const weatherObject_t *rain = tr.weatherSystem ? &tr.weatherSystem->weatherSlots[WEATHER_RAIN] : NULL;
	const Profile forced = s_water.GetProfileOverride();
	qboolean outside = qfalse;
	if ( forced != PROFILE_AUTO && !(rain && rain->active) )
	{
		// rainlens_profile forces rain for testing on dry maps
		input->intensity = ProfileIntensity(forced);
		input->exposed = 1.0f;
		input->facing = Com_Clamp(0.0f, 1.0f, refdef->viewaxis[0][2]);
		vec3_t origin;
		VectorCopy(refdef->vieworg, origin);
		outside = (qboolean)(tr.sunParsed && R_IsOutside(origin));
	}
	else if ( rain && rain->active && !inWater )
	{
		// "lightrain" 1000, "rain" / "acidrain" 2000, "heavyrain" 5000 particles
		input->intensity = Com_Clamp(0.0f, 1.0f, rain->particleCount / 5000.0f);
		input->weather = tr.weatherSystem->rainSubtype;

		vec3_t origin;
		VectorCopy(refdef->vieworg, origin);
		outside = (qboolean)R_IsOutside(origin);
		input->exposed = outside ? 1.0f : 0.0f;

		// Rain travels down, tilted by the wind; the lens gets more of it when
		// it faces into the rain.
		vec3_t fall;
		VectorSet(fall, tr.weatherSystem->windDirection[0], tr.weatherSystem->windDirection[1],
			-Q_max(rain->gravity, 0.1f) * 100.0f);
		const float horizontal = sqrtf(fall[0] * fall[0] + fall[1] * fall[1]);
		const float maxHorizontal = -fall[2];	// at most 45 degrees
		if ( horizontal > maxHorizontal )
		{
			fall[0] *= maxHorizontal / horizontal;
			fall[1] *= maxHorizontal / horizontal;
		}
		VectorNormalize(fall);
		input->facing = Com_Clamp(0.0f, 1.0f, -DotProduct(refdef->viewaxis[0], fall));

		// wind in lens space (viewaxis[1] points left), a hint for sheets
		vec3_t wind;
		VectorCopy(tr.weatherSystem->windDirection, wind);
		wind[2] = 0.0f;
		const float windSpeed = VectorNormalize(wind);
		const float windAmount = Com_Clamp(0.0f, 1.0f, windSpeed / 100.0f);
		input->windLens[0] = -DotProduct(wind, refdef->viewaxis[1]) * windAmount;
		input->windLens[1] = DotProduct(wind, refdef->viewaxis[2]) * windAmount;
	}
	else if ( tr.sunParsed && !inWater )
	{
		// no rain: the sun glint still needs to know whether the sky is open
		vec3_t origin;
		VectorCopy(refdef->vieworg, origin);
		outside = (qboolean)R_IsOutside(origin);
	}

	GatherOptics(refdef, outside, input);
}

static int RB_RainLensBeginTimer( const char *name )
{
	if ( !glRefConfig.timerQuery || r_speeds->integer != 100 )
		return -1;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	if ( tr.numTimedBlocks >= (MAX_GPU_TIMERS / 2) || frame->numTimers + 2 > MAX_GPU_TIMERS )
		return -1;

	const int handle = tr.numTimedBlocks++;
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	gpuTimedBlock_t *timedBlock = frame->timedBlocks + handle;
	timedBlock->beginTimer = timer->queryName;
	timedBlock->name = name;
	frame->numTimedBlocks++;

	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
	return handle;
}

static void RB_RainLensEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

// last input of the lens owner, for the optics of RB_RainLens
static rainLensInput_t s_lastInput;

/*
=============
RB_RainLensUpdate

Advances the lens water of the main scene by game time and tells whether
the pass runs this frame: while water is on the lens or new water reaches
it. A dry lens under cover costs nothing.
=============
*/
qboolean RB_RainLensUpdate( const rainLensInput_t *input )
{
	s_debugOutput = qfalse;

	if ( !input || !input->active )
		return qfalse;

	if ( !tr.rainLensImage || !tr.rainLensFbo || !tr.rainLensFieldImage || !tr.rainLensFieldFbo
		|| !tr.rainLensFilmImage || !tr.rainLensInstanceImage )
		return qfalse;

	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		return qfalse;

	// the first post processed main world scene of a frame owns the lens
	if ( s_clockValid && s_lastFrame == backEndData->realFrameNumber )
		return qfalse;
	s_lastFrame = backEndData->realFrameNumber;
	s_lastInput = *input;

	// game time: pauses with the game, restarts on map change
	const int now = backEnd.refdef.time;
	const int delta = now - s_lastRefdefTime;
	float dt = 0.0f;
	if ( !s_clockValid || delta < 0 || delta > 1000 )
	{
		s_water.Clear();	// cut: no water carried over
		s_motionValid = qfalse;
	}
	else
	{
		dt = delta * 0.001f;
	}
	s_lastRefdefTime = now;
	s_clockValid = qtrue;

	for ( int i = 0; i < input->numEvents; i++ )
	{
		const rainLensEvent_t &source = input->events[i];
		Event event = {};
		event.strength = source.strength;
		event.dir.x = source.dirLens[0];
		event.dir.y = source.dirLens[1];
		event.duration = source.duration;
		switch ( source.type )
		{
		case RAINLENS_EVENT_SUBMERGE: event.type = EVENT_SUBMERGE; break;
		case LENSWATER_SPRAY: event.type = EVENT_SPRAY; break;
		case LENSWATER_EMERGE: event.type = EVENT_EMERGE; break;
		default: event.type = EVENT_SPLASH; break;
		}
		s_water.QueueEvent(event);
	}

	Input in;
	in.intensity = input->intensity;
	in.exposed = input->exposed;
	in.facing = input->facing;
	in.weather = ProfileFromWeather(input->weather, input->intensity);
	// World gravity on the lens plane. viewaxis[1] points left; lens X
	// points right and lens Y up, so looking up or down leaves little
	// tangential gravity and rolling the camera turns it.
	in.gravity.x = backEnd.refdef.viewaxis[1][2];
	in.gravity.y = -backEnd.refdef.viewaxis[2][2];
	in.wind.x = input->windLens[0];
	in.wind.y = input->windLens[1];
	in.sprayStrength = input->sprayStrength;
	in.sprayDir.x = input->sprayDir[0];
	in.sprayDir.y = input->sprayDir[1];

	// r_rainLensInertia: camera acceleration from the view origin, filtered;
	// teleports and cuts restart the history
	if ( r_rainLensInertia->value > 0.0f && dt > 0.0f )
	{
		vec3_t velocity;
		VectorSubtract(backEnd.refdef.vieworg, s_prevOrigin, velocity);
		if ( !s_motionValid || VectorLength(velocity) > 256.0f )
		{
			VectorClear(s_accel);
			VectorClear(velocity);
			s_motionValid = qtrue;
		}
		else
		{
			VectorScale(velocity, 1.0f / dt, velocity);
			vec3_t accel;
			VectorSubtract(velocity, s_prevVelocity, accel);
			VectorScale(accel, 1.0f / dt, accel);
			const float k = 1.0f - expf(-dt / 0.05f);
			for ( int i = 0; i < 3; i++ )
				s_accel[i] += (accel[i] - s_accel[i]) * k;
		}
		VectorCopy(velocity, s_prevVelocity);
		in.cameraAccel.x = -DotProduct(s_accel, backEnd.refdef.viewaxis[1]);
		in.cameraAccel.y = DotProduct(s_accel, backEnd.refdef.viewaxis[2]);
	}
	else if ( r_rainLensInertia->value <= 0.0f )
	{
		s_motionValid = qfalse;
	}
	VectorCopy(backEnd.refdef.vieworg, s_prevOrigin);

	const qboolean active = (qboolean)s_water.Update(dt, in, CurrentParams());

	if ( input->submerged )
		return qfalse;

	return (qboolean)(active || r_rainLensDebug->integer);
}

/*
=============
RB_RainLens

srcFbo holds the HDR scene (MSAA resolved, temporally resolved and motion
blurred). Rasterises the lens water into the lower resolution field, then
refracts / reflects the HDR scene into rainLensImage. The caller uses
rainLensFbo for subsequent color passes. Debug views are drawn by
RB_RainLensDebugOverlay.
=============
*/
void RB_RainLens( FBO_t *srcFbo )
{
	if ( !srcFbo )
		return;

	const int debugView = r_rainLensDebug->integer;
	const Params params = CurrentParams();
	const float aspect = (float)tr.rainLensFieldFbo->width / (float)tr.rainLensFieldFbo->height;

	GpuReadTimings();
	int timer = RB_RainLensBeginTimer("Rain lens field");
	GpuTimestamp(0);

	const auto uploadStart = std::chrono::steady_clock::now();
	if ( s_water.FilmDirty() )
	{
		UploadFloatTexture(tr.rainLensFilmImage, s_filmPbo, &s_filmPboSize,
			s_water.FilmWidth(), s_water.FilmHeight(), GL_RG, s_water.FilmData(),
			(size_t)s_water.FilmWidth() * s_water.FilmHeight() * 2 * sizeof(float));
		s_water.ClearFilmDirty();
	}

	const int numInstances = s_water.BuildInstances(s_instanceData.data(), s_maxInstances, params.dropSize);
	if ( numInstances > 0 )
	{
		UploadFloatTexture(tr.rainLensInstanceImage, s_instancePbo, &s_instancePboSize,
			numInstances, INSTANCE_TEXELS, GL_RGBA, s_instanceData.data(),
			(size_t)numInstances * INSTANCE_FLOATS * sizeof(float));
	}
	s_pboIndex = (s_pboIndex + 1) % PBO_RING;
	s_uploadMicroseconds = std::chrono::duration<float, std::micro>(
		std::chrono::steady_clock::now() - uploadStart).count();

	GL_Cull(CT_TWO_SIDED);
	FBO_Bind(tr.rainLensFieldFbo);
	GL_SetViewportAndScissor(0, 0, tr.rainLensFieldFbo->width, tr.rainLensFieldFbo->height);

	vec4_t params1, params2;
	VectorSet4(params2, 0.0f, 0.0f,
		(float)tr.rainLensFieldFbo->width, (float)tr.rainLensFieldFbo->height);

	// thin film: weak refraction, low optical weight; it overwrites the field
	if ( s_water.FilmVisible() )
	{
		GL_State(GLS_DEPTHTEST_DISABLE);
		shaderProgram_t *film = &tr.rainLensShader[RAINLENSDEF_FILM];
		GLSL_BindProgram(film);
		GL_BindToTMU(tr.rainLensFilmImage, TB_COLORMAP);
		VectorSet4(params1, aspect, Com_Clamp(0.0f, 2.0f, r_rainLensFilm->value), 0.0f, 0.0f);
		GLSL_SetUniformVec4(film, UNIFORM_RAINLENSPARAMS, params1);
		GLSL_SetUniformVec4(film, UNIFORM_RAINLENSPARAMS2, params2);
		RB_InstantTriangle();
	}
	else
	{
		const vec4_t black = { 0.0f, 0.0f, 0.0f, 0.0f };
		qglClearBufferfv(GL_COLOR, 0, black);
	}

	// drops, micro drops and sheets: instanced analytic quads, added so
	// temporary overlaps sum their surface slopes instead of switching
	if ( numInstances > 0 )
	{
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		shaderProgram_t *drops = &tr.rainLensShader[RAINLENSDEF_DROPS];
		GLSL_BindProgram(drops);
		GL_BindToTMU(tr.rainLensInstanceImage, TB_COLORMAP);
		VectorSet4(params1, aspect, Com_Clamp(0.0f, 2.0f, r_rainLensBlur->value), 0.0f, 0.0f);
		GLSL_SetUniformVec4(drops, UNIFORM_RAINLENSPARAMS, params1);
		GLSL_SetUniformVec4(drops, UNIFORM_RAINLENSPARAMS2, params2);
		qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numInstances);
	}

	RB_RainLensEndTimer(timer);
	timer = RB_RainLensBeginTimer("Rain lens composite");
	GpuTimestamp(1);

	// r_rainLensMipBlur: half resolution mipped copy for the defocus
	const qboolean mipBlur = (qboolean)(r_rainLensMipBlur->integer && tr.rainLensMipImage && tr.rainLensMipFbo);
	if ( mipBlur )
	{
		FBO_FastBlit(srcFbo, NULL, tr.rainLensMipFbo, NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		GL_BindToTMU(tr.rainLensMipImage, TB_SPECULARMAP);
		GL_SelectTexture(TB_SPECULARMAP);
		qglGenerateMipmap(GL_TEXTURE_2D);
	}

	// Reflection source: the nearest environment cubemap, else the light
	// grid ambient at the camera.
	const rainLensInput_t &in = s_lastInput;
	const float reflection = Com_Clamp(0.0f, 2.0f, r_rainLensReflection->value);
	const cubemap_t *cubemap = NULL;
	if ( reflection > 0.0f && r_cubeMapping->integer && in.cubemapIndex > 0 && in.cubemapIndex <= tr.numCubemaps )
	{
		cubemap = &tr.cubemaps[in.cubemapIndex - 1];
		if ( !cubemap->image )
			cubemap = NULL;
	}

	static const float sampleCounts[3] = { 1.0f, 3.0f, 5.0f };
	GL_State(GLS_DEPTHTEST_DISABLE);
	FBO_Bind(tr.rainLensFbo);
	GL_SetViewportAndScissor(0, 0, tr.rainLensFbo->width, tr.rainLensFbo->height);
	shaderProgram_t *composite = &tr.rainLensCompositeShader[
		cubemap ? RAINLENSCOMPOSITE_CUBEMAP : RAINLENSCOMPOSITE_DEFAULT];
	GLSL_BindProgram(composite);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GL_BindToTMU(tr.rainLensFieldImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.rainLensFilmImage, TB_NORMALMAP);
	GL_BindToTMU(mipBlur ? tr.rainLensMipImage : tr.whiteImage, TB_SPECULARMAP);
	if ( cubemap )
		GL_BindToTMU(cubemap->image, TB_CUBEMAP);
	VectorSet4(params1, 0.0f, 0.0f, R_RainLensRefractionScale(), sampleCounts[QualityLevel()]);
	GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS, params1);
	VectorSet4(params2, 0.0f, 0.0f, (float)debugView, tr.linearLight ? 0.0f : 1.0f);
	GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS2, params2);

	// optics, lens space: x right, y up, z toward the viewer
	const LensWater::ProfileParams &profile = s_water.Current();
	const float *right = backEnd.viewParms.ori.axis[1];
	const float *up = backEnd.viewParms.ori.axis[2];
	const float *forward = backEnd.viewParms.ori.axis[0];
	float optics[OPTICS_VEC4S][4] = {
		{ in.keyDir[0], in.keyDir[1], in.keyDir[2], reflection },
		{ in.keyColor[0], in.keyColor[1], in.keyColor[2], CUBE_MAP_ROUGHNESS_MIPS * 0.35f },
		{ in.pointDir[0], in.pointDir[1], in.pointDir[2], in.cameraLuma },
		{ in.pointColor[0], in.pointColor[1], in.pointColor[2], mipBlur ? 1.0f : 0.0f },
		{ in.ambient[0], in.ambient[1], in.ambient[2],
			mipBlur ? (float)tr.rainLensMipImage->height : 0.0f },
		{ profile.tint[0], profile.tint[1], profile.tint[2], 0.0f },
		// lens to world: columns right, up, toward the viewer
		{ -right[0], -right[1], -right[2], 0.0f },
		{ up[0], up[1], up[2], 0.0f },
		{ -forward[0], -forward[1], -forward[2], 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
	};
	GLSL_SetUniformVec4N(composite, UNIFORM_RAINLENSOPTICS, &optics[0][0], OPTICS_VEC4S);

	if ( debugView == 9 )
	{
		// controller panel: normalised bars and state
		const Stats stats = s_water.GetStats();
		const LensWater::ProfileParams &rates = profile;
		static const float profileColors[PROFILE_COUNT][3] = {
			{ 1, 1, 1 }, { 0.4f, 0.8f, 1.0f }, { 0.2f, 0.5f, 1.0f }, { 0.1f, 0.1f, 0.9f }, { 0.6f, 1.0f, 0.2f } };
		const float *swatch = profileColors[stats.profile];
		float panel[DEBUG_VEC4S][4] = {
			{ in.intensity, in.exposed, in.facing, Com_Clamp(0.0f, 1.0f, stats.continuousSpray * 0.5f) },
			{ rates.microRate / 40.0f, rates.normalRate / 5.0f, rates.largeRate / 0.5f, rates.flowRate / 1.6f },
			{ rates.sheetRate / 1.2f, Com_Clamp(0.0f, 1.0f, stats.sprays / 4.0f),
				Com_Clamp(0.0f, 1.0f, 1.0f - stats.sinceEvent / 0.3f), (float)stats.drops / DropLimit() },
			{ swatch[0], swatch[1], swatch[2], s_water.GetProfileOverride() != PROFILE_AUTO ? 1.0f : 0.0f },
		};
		GLSL_SetUniformVec4N(composite, UNIFORM_RAINLENSDEBUG, &panel[0][0], DEBUG_VEC4S);
	}
	RB_InstantTriangle();

	// agent views over the composited image
	if ( (debugView == 5 || debugView == 7 || debugView == 8) && numInstances > 0 )
	{
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		shaderProgram_t *agents = &tr.rainLensShader[RAINLENSDEF_DEBUG_AGENTS];
		GLSL_BindProgram(agents);
		GL_BindToTMU(tr.rainLensInstanceImage, TB_COLORMAP);
		VectorSet4(params1, aspect, 1.0f, 0.0f, (float)debugView);
		GLSL_SetUniformVec4(agents, UNIFORM_RAINLENSPARAMS, params1);
		VectorSet4(params2, 0.0f, 0.0f, (float)tr.rainLensFbo->width, (float)tr.rainLensFbo->height);
		GLSL_SetUniformVec4(agents, UNIFORM_RAINLENSPARAMS2, params2);
		qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numInstances);
		GL_State(GLS_DEPTHTEST_DISABLE);
	}
	if ( debugView )
		s_debugOutput = qtrue;

	GpuTimestamp(2);
	s_gpuTimingIndex = (s_gpuTimingIndex + 1) % GPU_RING;
	RB_RainLensEndTimer(timer);
}

// r_rainLensDebug views, drawn at the end of the post process chain
void RB_RainLensDebugOverlay( void )
{
	if ( !s_debugOutput || !r_rainLensDebug->integer )
		return;

	vec4i_t dstBox;
	VectorSet4(dstBox, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
	FBO_FastBlitFromTexture(tr.rainLensImage, NULL, dstBox, NULL, 0);
}

/*
=============
Console commands (developer testing, independent of the weather)
=============
*/
static qboolean R_RainLensAvailable( void )
{
	if ( !tr.rainLensImage )
	{
		ri.Printf(PRINT_ALL, "Lens water is not active (r_rainLens 1, r_hdr 1, vid_restart)\n");
		return qfalse;
	}
	return qtrue;
}

void R_RainLensClear_f( void )
{
	if ( R_RainLensAvailable() )
		s_water.Clear();
}

void R_RainLensEvent_f( void )
{
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: rainlens_event <splash|spray|emerge> [strength] [side: left|right|up|down]\n");
		return;
	}
	if ( !R_RainLensAvailable() )
		return;

	Event event = {};
	switch ( EventTypeFromName(ri.Cmd_Argv(1)) )
	{
	case LENSWATER_SPLASH: event.type = EVENT_SPLASH; break;
	case LENSWATER_SPRAY: event.type = EVENT_SPRAY; break;
	case LENSWATER_EMERGE: event.type = EVENT_EMERGE; break;
	default:
		ri.Printf(PRINT_ALL, "unknown lens water event '%s'\n", ri.Cmd_Argv(1));
		return;
	}
	event.strength = ri.Cmd_Argc() > 2 ? Com_Clamp(0.05f, 2.0f, atof(ri.Cmd_Argv(2))) : 1.0f;
	event.duration = 2.5f;
	if ( ri.Cmd_Argc() > 3 )
	{
		const char *side = ri.Cmd_Argv(3);
		if ( !Q_stricmp(side, "left") ) event.dir = { -1.0f, 0.0f };
		else if ( !Q_stricmp(side, "right") ) event.dir = { 1.0f, 0.0f };
		else if ( !Q_stricmp(side, "up") ) event.dir = { 0.0f, 1.0f };
		else if ( !Q_stricmp(side, "down") ) event.dir = { 0.0f, -1.0f };
	}
	s_water.QueueEvent(event);
}

void R_RainLensProfile_f( void )
{
	static const char *names[PROFILE_COUNT] = { "auto", "light", "rain", "heavy", "acid" };
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: rainlens_profile <auto|light|rain|heavy|acid> (current: %s)\n",
			names[s_water.GetProfileOverride()]);
		return;
	}
	for ( int i = 0; i < PROFILE_COUNT; i++ )
	{
		if ( !Q_stricmp(ri.Cmd_Argv(1), names[i]) )
		{
			s_water.SetProfileOverride((Profile)i);
			if ( i != PROFILE_AUTO )
				ri.Printf(PRINT_ALL, "Lens water profile forced to %s (rains on the lens even without weather)\n", names[i]);
			return;
		}
	}
	ri.Printf(PRINT_ALL, "unknown profile '%s'\n", ri.Cmd_Argv(1));
}

void R_RainLensStats_f( void )
{
	if ( !R_RainLensAvailable() )
		return;
	static const char *names[PROFILE_COUNT] = { "auto", "light", "rain", "heavy", "acid" };
	const Stats s = s_water.GetStats();
	const LensWater::ProfileParams &p = s_water.Current();
	ri.Printf(PRINT_ALL, "Lens water (%s profile%s):\n", names[s.profile],
		s_water.GetProfileOverride() != PROFILE_AUTO ? ", forced" : "");
	ri.Printf(PRINT_ALL, "  drops %d / %d (beads %d, residual %d, moving %d, flow heads %d)\n",
		s.drops, DropLimit(), s.beads, s.residuals, s.moving, s.flows);
	ri.Printf(PRINT_ALL, "  micro drops %d / %d, sheets %d / %d\n", s.micro, MAX_LENS_MICRO, s.sheets, MAX_LENS_SHEETS);
	ri.Printf(PRINT_ALL, "  rates/s micro %.1f normal %.2f large %.2f flow %.2f sheet %.2f\n",
		p.microRate, p.normalRate, p.largeRate, p.flowRate, p.sheetRate);
	ri.Printf(PRINT_ALL, "  sprays %d, emitter spray %.2f, last event %.1f s ago, map emitters %d\n",
		s.sprays, s.continuousSpray, s.sinceEvent, tr.world ? tr.world->numLensWaterEmitters : 0);
	ri.Printf(PRINT_ALL, "  lens field %dx%d, film %dx%d (%s, %s)%s\n",
		tr.rainLensFieldImage ? tr.rainLensFieldImage->width : 0,
		tr.rainLensFieldImage ? tr.rainLensFieldImage->height : 0,
		s.filmWidth, s.filmHeight, s.filmVisible ? "visible" : "dry", s.filmDirty ? "dirty" : "uploaded",
		r_rainLensPBO->integer ? ", PBO upload" : "");
	ri.Printf(PRINT_ALL, "  CPU us: update %.1f (agents %.1f, field %.1f, events %.1f), upload %.1f\n",
		s.updateMicroseconds, s.agentMicroseconds, s.fieldMicroseconds, s.eventMicroseconds,
		s_uploadMicroseconds);
	ri.Printf(PRINT_ALL, "  GPU ms: field %.3f, composite %.3f%s\n", s_gpuFieldMs, s_gpuCompositeMs,
		r_rainLensMipBlur->integer ? " (mip blur)" : "");
	ri.Printf(PRINT_ALL, "  reflection: %s, key light %.2f, nearby light %.2f\n",
		(s_lastInput.cubemapIndex > 0 && r_cubeMapping->integer) ? va("cubemap %d", s_lastInput.cubemapIndex - 1) : "light grid ambient",
		Luma(s_lastInput.keyColor), Luma(s_lastInput.pointColor));
}
