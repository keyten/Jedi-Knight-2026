/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

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

// tr_waterparticles.cpp -- underwater suspended particles and micro-bubbles
// (r_waterParticles, glsl/waterparticles.glsl, docs/rend2-water-particles.md)
//
// A cheap depth cue under water: a few hundred specks and small bubbles in a
// sphere around the camera, only while the camera is inside a liquid brush.
// They are not media of the froxel fog (no volumetric blob per particle):
// tiny lit billboards that the medium in front of them fogs like any other
// transparent draw.
//
// Placement: the water body of the camera is the connected set of liquid
// brushes (tr_liquid.cpp, world_t::liquids) of the camera's class whose
// bounds touch. A candidate position is kept only inside a brush of that body
// and in sight of the camera (collision trace against solids), so nothing
// spawns above the surface, in another pool or behind a wall. Particles that
// drift out of the body or into a solid fade out where they are.
//
// Simulation (CPU, main view of scene 0, once per frame): slow drift with a
// per particle wander for the motes, buoyant rise with a zigzag for the
// bubbles. No fluid solver. Particles leaving the sphere reappear on its far
// side (behind the far fade), so the density follows the camera.
//
// Lighting (CPU): the light grid of the world (R_LightForPoint, the entity
// lighting path) with a phase for the view angle, plus the dynamic lights of
// the scene absorbed by the water between the light and the particle. The
// color is a reflectance times that light: specks show by contrast with the
// medium, they do not glow.
//
// Body profile: none / clean / murky / dirty (density, bubble fraction,
// opacity, albedo). Every body is "clean" (very low density) unless the map's
// env.json says otherwise:
//
//	"WaterParticles": [ { "Shader": "textures/yavin/water*", "Profile": "murky" } ]
//
// matched against the BSP shader of a brush of the body or its upward side
// (the same rule form as "Liquids"). r_waterParticlesProfile forces one.

#include "tr_local.h"
#include "tr_pbr_contract.h"
#include "json.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#define WATERPARTICLE_MAX			2048
#define WATERPARTICLE_LIGHT_FRAMES	8		// light grid refresh period of a particle (frames)
#define WATERPARTICLE_SOLID_CHECKS	32		// contents tests of live particles per frame (round robin)

enum { WPPROFILE_NONE, WPPROFILE_CLEAN, WPPROFILE_MURKY, WPPROFILE_DIRTY, WPPROFILE_COUNT };
static const char *s_wpProfileNames[WPPROFILE_COUNT] = { "none", "clean", "murky", "dirty" };

struct waterParticleProfile_t
{
	float	density;		// particles per million cubic units at r_waterParticlesDensity 1
	float	bubbleFraction;	// share of the particles that are micro-bubbles
	float	moteOpacity;
	float	bubbleOpacity;
	vec3_t	moteAlbedo;		// reflectance of a speck
	float	moteRadius;		// world units at r_waterParticlesSize 1
	float	bubbleRadius;	// largest micro-bubble
};

static const waterParticleProfile_t s_wpProfiles[WPPROFILE_COUNT] = {
	{  0.0f, 0.0f,  0.0f,  0.0f,  { 0.0f,  0.0f,  0.0f  }, 0.0f,  0.0f  },
	// clean: an indoor pool, a handful of pale specks and the odd bubble
	{  3.0f, 0.12f, 0.30f, 0.45f, { 0.60f, 0.64f, 0.62f }, 0.10f, 0.30f },
	// murky: lake or harbour water
	{ 12.0f, 0.06f, 0.40f, 0.45f, { 0.50f, 0.52f, 0.42f }, 0.13f, 0.30f },
	// dirty: silt laden swamp or sewer water
	{ 32.0f, 0.04f, 0.50f, 0.40f, { 0.42f, 0.38f, 0.28f }, 0.16f, 0.25f },
};

enum { WPSTATE_DEAD, WPSTATE_ALIVE };

struct waterParticle_t
{
	vec3_t	pos;
	float	radius;			// world units (size cvar applied at draw)
	float	age, life;		// seconds
	float	fadeOut;		// fade out length at the end of life
	float	phase[3];		// wander / zigzag phases
	float	freq;			// wander / zigzag frequency (Hz)
	float	buoyancy;		// vertical drift of a mote, rise speed of a bubble (units / s)
	vec3_t	ambient, directed, lightDir;	// light grid at the particle (scene light units)
	int		lightFrame;		// frame of the last light grid sample, -1 = none
	byte	state;
	byte	bubble;
};

struct waterParticleVertex_t
{
	vec4_t	position;		// xyz, w radius (negative: bubble)
	vec4_t	color;			// rgb lit color, a opacity
};

// env.json "WaterParticles" rules of the current map
struct waterParticleRule_t
{
	std::string	pattern;	// lower case, trailing '*' = prefix
	int			profile;	// WPPROFILE_*
};

static struct
{
	const world_t	*world;			// bodies below are for this world
	std::vector<int>	brushBody;	// world_t::liquids index -> body
	std::vector<std::vector<int>>	bodyBrushes;
	std::vector<int>	bodyProfile;	// WPPROFILE_* from env.json, -1 = none matched (clean)
	std::vector<waterParticleRule_t>	rules;

	image_t			*bubbleImage;	// gfx/effects/bubble, NULL = procedural ring
	qboolean		bubbleImageChecked;

	// simulation
	waterParticle_t	particles[WATERPARTICLE_MAX];
	int				activeBody;		// body of the last simulated frame, -1 none
	int				activeClass;
	int				activeMediumSlot;
	vec3_t			lastCamera;
	float			lastTime;
	float			bodyStartTime;	// the fill of the body began (staggered first lifetimes)
	int				simFrame;		// tr.frameCount of the last simulation
	unsigned int	rng;
	int				solidCursor;

	// what the back end draws (written by the front end once per frame)
	waterParticleVertex_t	vertices[WATERPARTICLE_MAX];
	int				numVertices;
	vec4_t			drawParams[2];

	// GPU buffer, created on first draw
	VBO_t			vbo;
	qboolean		vboValid;

	// diagnostics (r_waterParticlesDebug 3)
	int				target;
	int				alive;
	int				bubbles;
	int				spawned;
	int				rejectBody;
	int				rejectSight;
	int				killedBody;
	int				killedSolid;
	float			cpuMsec;
	int				lastPrint;
	const char		*inactiveReason;
} s_wp;

static surfaceType_t s_wpSurface = SF_WATER_PARTICLES;

/*
============================================================

Map load: water bodies and profiles

============================================================
*/

static unsigned int WP_Rand( void )
{
	// xorshift32
	unsigned int x = s_wp.rng ? s_wp.rng : 0x9e3779b9u;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s_wp.rng = x;
	return x;
}

static float WP_Random( void )
{
	return (float)(WP_Rand() >> 8) * (1.0f / 16777216.0f);
}

static float WP_RandomRange( float lo, float hi )
{
	return lo + (hi - lo) * WP_Random();
}

static void WP_LoadRules( const char *baseName )
{
	s_wp.rules.clear();

	char filename[MAX_QPATH];
	Com_sprintf(filename, sizeof(filename), "cubemaps/%s/env.json", baseName);
	union { char *c; void *v; } buffer;
	const int filelen = ri.FS_ReadFile(filename, &buffer.v);
	if ( !buffer.c )
		return;
	const char *jsonEnd = buffer.c + filelen;
	const char *array = (JSON_ValueGetType(buffer.c, jsonEnd) == JSONTYPE_OBJECT) ?
		JSON_ObjectGetNamedValue(buffer.c, jsonEnd, "WaterParticles") : NULL;
	if ( array && JSON_ValueGetType(array, jsonEnd) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: WaterParticles is not an array\n", filename);
		array = NULL;
	}

	const int count = array ? (int)JSON_ArrayGetIndex(array, jsonEnd, NULL, 0) : 0;
	for ( int i = 0; i < count; i++ )
	{
		const char *entry = JSON_ArrayGetValue(array, jsonEnd, i);
		const char *shaderValue = entry ? JSON_ObjectGetNamedValue(entry, jsonEnd, "Shader") : NULL;
		const char *profileValue = entry ? JSON_ObjectGetNamedValue(entry, jsonEnd, "Profile") : NULL;
		char shader[MAX_QPATH] = "", profile[16] = "";
		if ( shaderValue )
			JSON_ValueGetString(shaderValue, jsonEnd, shader, sizeof(shader));
		if ( profileValue )
			JSON_ValueGetString(profileValue, jsonEnd, profile, sizeof(profile));

		int wpProfile = -1;
		for ( int p = 0; p < WPPROFILE_COUNT; p++ )
		{
			if ( !Q_stricmp(profile, s_wpProfileNames[p]) )
				wpProfile = p;
		}
		if ( !shader[0] || wpProfile < 0 )
		{
			ri.Printf(PRINT_WARNING, "%s: WaterParticles[%d] needs a \"Shader\" and a \"Profile\" (none, clean, murky, dirty)\n",
				filename, i);
			continue;
		}
		Q_strlwr(shader);
		s_wp.rules.push_back({ shader, wpProfile });
	}
	ri.FS_FreeFile(buffer.v);
}

// index of the first rule matching a shader name, -1 none
static int WP_RuleForShader( const char *name )
{
	if ( !name || !name[0] )
		return -1;
	char lower[MAX_QPATH];
	Q_strncpyz(lower, name, sizeof(lower));
	Q_strlwr(lower);
	for ( size_t r = 0; r < s_wp.rules.size(); r++ )
	{
		const std::string& pattern = s_wp.rules[r].pattern;
		const size_t n = pattern.size();
		if ( n && pattern[n - 1] == '*' ? !strncmp(lower, pattern.c_str(), n - 1) : pattern == lower )
			return (int)r;
	}
	return -1;
}

static int WP_FindRoot( std::vector<int>& parent, int i )
{
	while ( parent[i] != i )
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

/*
=================
R_WaterParticlesWorldLoaded

The water bodies of the new world: liquid brushes of one class whose bounds
overlap or touch (one unit tolerance) are one body. The profile of a body is
the first env.json rule matching the shader or the upward side shader of one
of its brushes (rule order wins), else clean.
=================
*/
void R_WaterParticlesWorldLoaded( void )
{
	s_wp.world = tr.world;
	s_wp.brushBody.clear();
	s_wp.bodyBrushes.clear();
	s_wp.bodyProfile.clear();
	s_wp.activeBody = -1;
	s_wp.numVertices = 0;
	for ( int i = 0; i < WATERPARTICLE_MAX; i++ )
		s_wp.particles[i].state = WPSTATE_DEAD;
	// looked up again on first use (the image of the previous map may be gone)
	s_wp.bubbleImage = NULL;
	s_wp.bubbleImageChecked = qfalse;
	if ( !tr.world )
		return;

	WP_LoadRules(tr.world->baseName);

	const int n = tr.world->numLiquids;
	std::vector<int> parent(n);
	for ( int i = 0; i < n; i++ )
		parent[i] = i;
	for ( int i = 0; i < n; i++ )
	{
		const liquidBrush_t *a = &tr.world->liquids[i];
		for ( int j = i + 1; j < n; j++ )
		{
			const liquidBrush_t *b = &tr.world->liquids[j];
			if ( a->liquidClass != b->liquidClass )
				continue;
			qboolean touch = qtrue;
			for ( int k = 0; k < 3 && touch; k++ )
			{
				if ( a->bounds[0][k] > b->bounds[1][k] + 1.0f || b->bounds[0][k] > a->bounds[1][k] + 1.0f )
					touch = qfalse;
			}
			if ( touch )
				parent[WP_FindRoot(parent, i)] = WP_FindRoot(parent, j);
		}
	}

	s_wp.brushBody.assign(n, -1);
	std::vector<int> rootBody(n, -1);
	for ( int i = 0; i < n; i++ )
	{
		const int root = WP_FindRoot(parent, i);
		if ( rootBody[root] < 0 )
		{
			rootBody[root] = (int)s_wp.bodyBrushes.size();
			s_wp.bodyBrushes.push_back(std::vector<int>());
			s_wp.bodyProfile.push_back(-1);
		}
		s_wp.brushBody[i] = rootBody[root];
		s_wp.bodyBrushes[rootBody[root]].push_back(i);
	}

	int matched = 0;
	for ( size_t body = 0; body < s_wp.bodyBrushes.size(); body++ )
	{
		int bestRule = -1;
		for ( int brushIndex : s_wp.bodyBrushes[body] )
		{
			const liquidBrush_t *brush = &tr.world->liquids[brushIndex];
			const int shaders[2] = { brush->shaderNum, brush->topShaderNum };
			for ( int s = 0; s < 2; s++ )
			{
				if ( shaders[s] < 0 || shaders[s] >= tr.world->numShaders )
					continue;
				const int rule = WP_RuleForShader(tr.world->shaders[shaders[s]].shader);
				if ( rule >= 0 && (bestRule < 0 || rule < bestRule) )
					bestRule = rule;
			}
		}
		if ( bestRule >= 0 )
		{
			s_wp.bodyProfile[body] = s_wp.rules[bestRule].profile;
			matched++;
		}
	}
	if ( !s_wp.rules.empty() )
		ri.Printf(PRINT_DEVELOPER, "r_waterParticles: %d water bodies, %d rule%s, %d bod%s with an env.json profile\n",
			(int)s_wp.bodyBrushes.size(), (int)s_wp.rules.size(), s_wp.rules.size() == 1 ? "" : "s",
			matched, matched == 1 ? "y" : "ies");
}

void R_WaterParticlesShutdown( void )
{
	if ( s_wp.vboValid )
	{
		if ( glState.currentVBO == &s_wp.vbo )
			glState.currentVBO = NULL;
		qglDeleteBuffers(1, &s_wp.vbo.vertexesVBO);
	}
	s_wp.vboValid = qfalse;
	Com_Memset(&s_wp.vbo, 0, sizeof(s_wp.vbo));
	s_wp.bubbleImage = NULL;
	s_wp.bubbleImageChecked = qfalse;
	s_wp.numVertices = 0;
	s_wp.activeBody = -1;
	s_wp.world = NULL;
}

/*
============================================================

Front end: simulation

============================================================
*/

// the liquid brush of a class holding p (half a unit tolerance), -1 none.
// Every brush of world_t::liquids counts, whatever r_volumetricWater says.
static int WP_PointBrush( const vec3_t p, int liquidClass )
{
	for ( int i = 0; i < tr.world->numLiquids; i++ )
	{
		const liquidBrush_t *brush = &tr.world->liquids[i];
		if ( brush->liquidClass != liquidClass )
			continue;
		qboolean inside = qtrue;
		for ( int k = 0; k < brush->numPlanes && inside; k++ )
		{
			const float *plane = tr.world->liquidPlanes[brush->firstPlane + k];
			if ( DotProduct(plane, p) - plane[3] > 0.5f )
				inside = qfalse;
		}
		if ( inside )
			return i;
	}
	return -1;
}

// p inside a brush of the active body, at least margin units from its sides
static qboolean WP_InsideBody( const vec3_t p, float margin )
{
	for ( int brushIndex : s_wp.bodyBrushes[s_wp.activeBody] )
	{
		const liquidBrush_t *brush = &tr.world->liquids[brushIndex];
		if ( p[0] < brush->bounds[0][0] || p[0] > brush->bounds[1][0] ||
			p[1] < brush->bounds[0][1] || p[1] > brush->bounds[1][1] ||
			p[2] < brush->bounds[0][2] || p[2] > brush->bounds[1][2] )
			continue;
		qboolean inside = qtrue;
		for ( int k = 0; k < brush->numPlanes && inside; k++ )
		{
			const float *plane = tr.world->liquidPlanes[brush->firstPlane + k];
			if ( DotProduct(plane, p) - plane[3] > -margin )
				inside = qfalse;
		}
		if ( inside )
			return qtrue;
	}
	return qfalse;
}

// nothing solid between the camera and p (and p not in a solid)
static qboolean WP_InSight( const vec3_t camera, const vec3_t p )
{
	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, camera, vec3_origin, vec3_origin, p, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, camera, p, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	return (qboolean)(!trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f);
}

static int WP_Profile( void )
{
	const int forced = r_waterParticlesProfile->integer;
	if ( forced > 0 && forced < WPPROFILE_COUNT )
		return forced;
	if ( forced < 0 )
		return WPPROFILE_NONE;
	const int profile = s_wp.bodyProfile[s_wp.activeBody];
	return profile >= 0 ? profile : WPPROFILE_CLEAN;
}

static void WP_InitParticle( waterParticle_t *p, const vec3_t pos, const waterParticleProfile_t *profile,
	float bubbleFraction, float ageFraction )
{
	VectorCopy(pos, p->pos);
	p->state = WPSTATE_ALIVE;
	p->bubble = (byte)(WP_Random() < bubbleFraction);
	for ( int k = 0; k < 3; k++ )
		p->phase[k] = WP_RandomRange(0.0f, 2.0f * (float)M_PI);
	p->lightFrame = -1;
	if ( p->bubble )
	{
		// micro-bubbles: smaller ones are more common and rise slower
		const float s = WP_Random();
		p->radius = profile->bubbleRadius * (0.25f + 0.75f * s * s);
		p->buoyancy = 3.0f + 9.0f * (p->radius / profile->bubbleRadius);
		p->freq = WP_RandomRange(1.2f, 2.6f);
		p->life = WP_RandomRange(6.0f, 12.0f);
		p->fadeOut = 0.4f;
	}
	else
	{
		p->radius = profile->moteRadius * WP_RandomRange(0.6f, 1.4f);
		// near neutral: some specks settle, some float
		p->buoyancy = WP_RandomRange(-0.35f, 0.15f);
		p->freq = WP_RandomRange(0.08f, 0.25f);
		p->life = WP_RandomRange(10.0f, 24.0f);
		p->fadeOut = 1.5f;
	}
	// a fresh fill gets random lifetimes, so the first generation does not
	// die (and respawn) in one go; every particle still fades in
	p->age = 0.0f;
	if ( ageFraction > 0.0f )
		p->life = MAX(p->fadeOut + 1.0f, p->life * ageFraction);
}

// velocity of a particle at time t (units / s)
static void WP_Velocity( const waterParticle_t *p, float t, float speed, vec3_t vel )
{
	// slow current through the body, its heading wanders over minutes
	const float heading = 0.31f * sinf(t * 0.021f) + 0.6f * sinf(t * 0.0083f + 1.3f);
	const float current = 0.9f * speed;
	vel[0] = cosf(heading) * current;
	vel[1] = sinf(heading) * current;
	vel[2] = 0.0f;
	if ( p->bubble )
	{
		// rise with a zigzag across, half the current
		const float w = 2.0f * (float)M_PI * p->freq * t;
		const float zig = 0.8f * p->radius / 0.3f;
		VectorScale(vel, 0.5f, vel);
		vel[0] += zig * cosf(w + p->phase[0]) * cosf(p->phase[1]);
		vel[1] += zig * cosf(w + p->phase[0]) * sinf(p->phase[1]);
		vel[2] += p->buoyancy * speed;
	}
	else
	{
		const float w = 2.0f * (float)M_PI * p->freq * t;
		vel[0] += 0.7f * speed * sinf(w + p->phase[0]);
		vel[1] += 0.7f * speed * sinf(0.83f * w + p->phase[1]);
		vel[2] += speed * (p->buoyancy + 0.25f * sinf(0.61f * w + p->phase[2]));
	}
}

// light grid at the particle in scene light units (1 = a surface at full
// texture brightness), decoded like the entity lighting
static void WP_SampleLight( waterParticle_t *p )
{
	vec3_t ambient, directed, dir;
	if ( tr.world->lightGridData && R_LightForPoint(p->pos, ambient, directed, dir) )
	{
		for ( int c = 0; c < 3; c++ )
		{
			float a = Com_Clamp(0.0f, 4.0f, ambient[c] / 255.0f);
			float d = Com_Clamp(0.0f, 4.0f, directed[c] / 255.0f);
			if ( tr.forcedLinearLight )
			{
				a = (float)sRGBtoRGB(MIN(a, 1.0f));
				d = (float)sRGBtoRGB(MIN(d, 1.0f));
			}
			p->ambient[c] = a;
			p->directed[c] = d;
		}
		VectorNormalize2(dir, p->lightDir);
	}
	else
	{
		VectorSet(p->ambient, 0.35f, 0.35f, 0.35f);
		VectorSet(p->directed, 0.25f, 0.25f, 0.25f);
		VectorSet(p->lightDir, 0.0f, 0.0f, 1.0f);
	}
	p->lightFrame = tr.frameCount;
}

// diffuse sphere seen from the camera: lit fraction for the angle between the
// directions to the camera and to the light (1 = light behind the camera)
static float WP_LambertSphere( float cosine )
{
	cosine = Com_Clamp(-1.0f, 1.0f, cosine);
	const float theta = acosf(cosine);
	return (((float)M_PI - theta) * cosine + sinf(theta)) / (float)M_PI;
}

// phase of a particle for light arriving from toLight, seen along viewDir
// (camera to particle)
static float WP_Phase( const waterParticle_t *p, const vec3_t viewDir, const vec3_t toLight )
{
	const float c = DotProduct(viewDir, toLight);	// 1: light straight behind the particle
	const float forward = powf(MAX(c, 0.0f), 8.0f);
	if ( p->bubble )
		return 0.35f + 1.6f * forward;
	return 0.9f * WP_LambertSphere(-c) + 0.8f * forward;
}

static void WP_Deactivate( const char *reason )
{
	if ( s_wp.activeBody >= 0 )
	{
		for ( int i = 0; i < WATERPARTICLE_MAX; i++ )
			s_wp.particles[i].state = WPSTATE_DEAD;
	}
	s_wp.activeBody = -1;
	s_wp.numVertices = 0;
	s_wp.alive = s_wp.bubbles = s_wp.target = 0;
	s_wp.inactiveReason = reason;
}

static void WP_PrintStats( void )
{
	if ( r_waterParticlesDebug->integer != 3 || tr.refdef.time - s_wp.lastPrint < 1000 )
		return;
	s_wp.lastPrint = tr.refdef.time;
	if ( s_wp.activeBody < 0 )
	{
		ri.Printf(PRINT_ALL, "r_waterParticles: inactive (%s)\n", s_wp.inactiveReason ? s_wp.inactiveReason : "off");
		return;
	}
	ri.Printf(PRINT_ALL, "r_waterParticles: body %d (%d brushes, class %d, profile %s%s), %d / %d alive, %d bubbles, "
		"%d spawned, rejected %d body / %d sight, killed %d body / %d solid, %.3f ms CPU\n",
		s_wp.activeBody, (int)s_wp.bodyBrushes[s_wp.activeBody].size(), s_wp.activeClass,
		s_wpProfileNames[WP_Profile()], r_waterParticlesProfile->integer ? " forced" :
		(s_wp.bodyProfile[s_wp.activeBody] >= 0 ? " env.json" : " default"),
		s_wp.alive, s_wp.target, s_wp.bubbles, s_wp.spawned, s_wp.rejectBody, s_wp.rejectSight,
		s_wp.killedBody, s_wp.killedSolid, s_wp.cpuMsec);
	s_wp.spawned = s_wp.rejectBody = s_wp.rejectSight = s_wp.killedBody = s_wp.killedSolid = 0;
}

/*
=================
WP_Simulate

One step for the frame: camera body, spawn / move / kill, light, vertices.
=================
*/
static void WP_Simulate( const trRefdef_t *refdef, const viewParms_t *view )
{
	const auto cpuStart = std::chrono::steady_clock::now();
	const float *camera = view->ori.origin;

	// Particles are a visual medium cue, so suppress them in a displaced trough
	// and allow them in a crest above the flat BSP top. Body/brush selection is
	// still anchored to BSP geometry by projecting only the membership sample;
	// gameplay contents itself is never changed.
	waterlineQuery_t waterline = {};
	const qboolean visualWaterline = R_WaterlineQuery(camera, refdef->floatTime, &waterline);
	if (visualWaterline && waterline.relativeHeight > 0.0f)
	{
		WP_Deactivate("camera above displaced waterline");
		return;
	}
	vec3_t membershipPoint;
	VectorCopy(camera, membershipPoint);
	if (visualWaterline)
		membershipPoint[2] = MIN(membershipPoint[2], waterline.baseHeight - 0.25f);
	const int cameraClass = R_LiquidPointClass(membershipPoint);
	if ( cameraClass != LIQUID_WATER && cameraClass != LIQUID_SLIME )
	{
		WP_Deactivate("camera not in water / slime");
		return;
	}
	const int cameraBrush = WP_PointBrush(membershipPoint, cameraClass);
	if ( cameraBrush < 0 )
	{
		WP_Deactivate("camera liquid has no kept brush (BSP fog water, brush model, shape)");
		return;
	}
	const int body = s_wp.brushBody[cameraBrush];

	const float radius = Com_Clamp(32.0f, 1024.0f, r_waterParticlesDistance->value);
	const float time = refdef->floatTime;
	float dt = time - s_wp.lastTime;
	if ( !(dt >= 0.0f) || dt > 0.1f )
		dt = (dt > 0.1f && dt < 1.0f) ? 0.1f : 0.0f;

	// a new body, or a teleport: start over (the fill fades in)
	if ( body != s_wp.activeBody || Distance(camera, s_wp.lastCamera) > radius )
	{
		for ( int i = 0; i < WATERPARTICLE_MAX; i++ )
			s_wp.particles[i].state = WPSTATE_DEAD;
		s_wp.activeBody = body;
		s_wp.bodyStartTime = time;
		s_wp.rng ^= (unsigned int)refdef->time * 2654435761u;
	}
	s_wp.activeClass = cameraClass;
	s_wp.activeMediumSlot = tr.world->liquids[cameraBrush].mediumSlot;
	s_wp.inactiveReason = NULL;
	VectorCopy(camera, s_wp.lastCamera);
	s_wp.lastTime = time;

	const int profileIndex = WP_Profile();
	const waterParticleProfile_t *profile = &s_wpProfiles[profileIndex];
	const float volume = (4.0f / 3.0f) * (float)M_PI * radius * radius * radius;
	const int target = Com_Clampi(0, WATERPARTICLE_MAX,
		(int)(profile->density * volume * 1e-6f * MAX(0.0f, r_waterParticlesDensity->value) + 0.5f));
	const float bubbleFraction = (r_waterParticlesBubbles->value >= 0.0f) ?
		Com_Clamp(0.0f, 1.0f, r_waterParticlesBubbles->value) : profile->bubbleFraction;
	const float speed = Com_Clamp(0.0f, 8.0f, r_waterParticlesSpeed->value);
	s_wp.target = target;

	// the dynamic lights reaching the volume
	vec4_t extinction, albedo;
	R_LiquidsMaterial(s_wp.activeMediumSlot, extinction, albedo);
	const float sigma = extinction[3];
	int lights[64];
	int numLights = 0;
	for ( int l = 0; l < refdef->num_dlights && numLights < (int)ARRAY_LEN(lights); l++ )
	{
		const dlight_t *dl = &refdef->dlights[l];
		if ( dl->areaType != 0 || dl->radius <= 0.0f )
			continue;
		// attenuation reaches 0 at the radius (lightall CalcLightAttenuation)
		if ( Distance(dl->origin, camera) > radius + dl->radius )
			continue;
		lights[numLights++] = l;
	}

	// spawn budget: plane tests are cheap, sight traces less so
	int attempts = 48 + target / 8;
	int traces = 24 + target / 16;
	// the first generation (the fill of a new body) gets random lifetimes
	const qboolean fresh = (qboolean)(time - s_wp.bodyStartTime < 1.0f);

	s_wp.alive = 0;
	s_wp.bubbles = 0;
	s_wp.numVertices = 0;
	const float sizeScale = Com_Clamp(0.1f, 8.0f, r_waterParticlesSize->value);
	for ( int i = 0; i < WATERPARTICLE_MAX; i++ )
	{
		waterParticle_t *p = &s_wp.particles[i];
		if ( i >= target )
		{
			// over the target (density lowered, profile changed): fade out
			if ( p->state == WPSTATE_ALIVE && p->life - p->age > p->fadeOut )
				p->life = p->age + p->fadeOut;
		}
		else if ( p->state == WPSTATE_DEAD )
		{
			if ( attempts <= 0 || traces <= 0 )
				continue;
			attempts--;
			// uniform in the sphere
			vec3_t offset;
			do
			{
				VectorSet(offset, WP_RandomRange(-1.0f, 1.0f), WP_RandomRange(-1.0f, 1.0f), WP_RandomRange(-1.0f, 1.0f));
			} while ( DotProduct(offset, offset) > 1.0f );
			vec3_t pos;
			VectorMA(camera, radius, offset, pos);
			if ( !WP_InsideBody(pos, 1.0f) )
			{
				s_wp.rejectBody++;
				continue;
			}
			traces--;
			if ( !WP_InSight(camera, pos) )
			{
				s_wp.rejectSight++;
				continue;
			}
			WP_InitParticle(p, pos, profile, bubbleFraction, fresh ? WP_Random() : 0.0f);
			s_wp.spawned++;
		}
		if ( p->state != WPSTATE_ALIVE )
			continue;

		// move; a particle that would leave the body stays and fades out
		p->age += dt;
		if ( dt > 0.0f )
		{
			vec3_t vel, next;
			WP_Velocity(p, time, speed, vel);
			VectorMA(p->pos, dt, vel, next);
			if ( WP_InsideBody(next, p->bubble ? 0.25f : 0.5f) )
				VectorCopy(next, p->pos);
			else if ( p->life - p->age > (p->bubble ? 0.12f : 0.8f) )
			{
				// bubbles pop at the surface, specks dissolve at the walls
				p->life = p->age + (p->bubble ? 0.12f : 0.8f);
				p->fadeOut = p->bubble ? 0.12f : 0.8f;
				s_wp.killedBody++;
			}
		}
		if ( p->age >= p->life )
		{
			p->state = WPSTATE_DEAD;
			continue;
		}

		// the sphere follows the camera: out of it, the particle goes to the
		// far side (behind the far fade) if that place is valid
		vec3_t delta;
		VectorSubtract(p->pos, camera, delta);
		const float distSq = DotProduct(delta, delta);
		if ( distSq > radius * radius )
		{
			const float dist = sqrtf(distSq);
			vec3_t wrapped;
			VectorMA(camera, -0.98f * radius / dist, delta, wrapped);
			if ( traces > 0 && WP_InsideBody(wrapped, 1.0f) && (traces--, WP_InSight(camera, wrapped)) )
			{
				VectorCopy(wrapped, p->pos);
				p->lightFrame = -1;
			}
			else
			{
				p->state = WPSTATE_DEAD;
				continue;
			}
		}

		if ( p->lightFrame < 0 || ((i + tr.frameCount) % WATERPARTICLE_LIGHT_FRAMES) == 0 )
			WP_SampleLight(p);

		s_wp.alive++;
		s_wp.bubbles += p->bubble;

		// lit color
		vec3_t viewDir;
		VectorSubtract(p->pos, camera, viewDir);
		VectorNormalize(viewDir);
		vec3_t light;
		VectorMA(p->ambient, WP_Phase(p, viewDir, p->lightDir), p->directed, light);
		for ( int l = 0; l < numLights; l++ )
		{
			const dlight_t *dl = &refdef->dlights[lights[l]];
			vec3_t toLight;
			VectorSubtract(dl->origin, p->pos, toLight);
			const float rawD2 = DotProduct(toLight, toLight);
			const float d2 = r_pbrPhysical->integer ? MAX(rawD2, 1.0e-8f) : MAX(rawD2, 1.0f);
			if ( d2 >= dl->radius * dl->radius )
				continue;
			const float d = sqrtf(d2);
			float atten = r_pbrPhysical->integer ?
				Rend2PBR::PointIrradiance(1.0f, d, dl->radius,
					r_pbrWorldUnitsPerMeter->value,
					dl->emitterRadius * R_PBRMetersPerWorldUnit()) :
				Com_Clamp(0.0f, 1.0f, 0.5f * dl->radius * dl->radius / d2 - 0.5f);
			VectorScale(toLight, 1.0f / d, toLight);
			if ( dl->spot )
			{
				const float cone = -DotProduct(toLight, dl->spotDir);
				const float t = Com_Clamp(0.0f, 1.0f, (cone - dl->spotCosOuter) /
					MAX(dl->spotCosInner - dl->spotCosOuter, 1e-4f));
				atten *= t * t * (3.0f - 2.0f * t);
			}
			// the water between the light and the particle (the camera
			// side is the froxel medium's job)
			atten *= expf(-sigma * d) * WP_Phase(p, viewDir, toLight);
			VectorMA(light, atten, dl->color, light);
		}

		vec3_t color;
		float alpha;
		if ( p->bubble )
		{
			VectorCopy(light, color);
			alpha = profile->bubbleOpacity;
		}
		else
		{
			color[0] = light[0] * profile->moteAlbedo[0];
			color[1] = light[1] * profile->moteAlbedo[1];
			color[2] = light[2] * profile->moteAlbedo[2];
			alpha = profile->moteOpacity;
		}
		// fade in, fade out
		alpha *= Com_Clamp(0.0f, 1.0f, p->age / 0.6f) * Com_Clamp(0.0f, 1.0f, (p->life - p->age) / p->fadeOut);

		waterParticleVertex_t *v = &s_wp.vertices[s_wp.numVertices++];
		const float r = p->radius * sizeScale;
		VectorSet4(v->position, p->pos[0], p->pos[1], p->pos[2], p->bubble ? -r : r);
		VectorSet4(v->color, color[0], color[1], color[2], alpha);
	}

	// round robin: live particles that drifted into a solid fade out
	for ( int n = 0; n < WATERPARTICLE_SOLID_CHECKS && target > 0; n++ )
	{
		s_wp.solidCursor = (s_wp.solidCursor + 1) % target;
		waterParticle_t *p = &s_wp.particles[s_wp.solidCursor];
		if ( p->state != WPSTATE_ALIVE || p->life - p->age <= 0.3f )
			continue;
		if ( ri.CM_PointContents(p->pos, 0) & CONTENTS_SOLID )
		{
			p->life = p->age + 0.3f;
			p->fadeOut = 0.3f;
			s_wp.killedSolid++;
		}
	}

	s_wp.cpuMsec = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
}

/*
=================
R_AddWaterParticleSurfaces

Front end, per view: the main view of the world scene simulates (once per
frame) and adds the draw surface.
=================
*/
void R_AddWaterParticleSurfaces( const trRefdef_t *refdef, const viewParms_t *view )
{
	if ( !r_waterParticles->integer || !tr.world || !tr.waterParticleInternalShader )
	{
		if ( s_wp.activeBody >= 0 )
			WP_Deactivate("r_waterParticles 0");
		return;
	}
	if ( view->viewParmType != VPT_MAIN || backEndData->currentFrame->currentScene != 0 ||
		(refdef->rdflags & (RDF_NOWORLDMODEL | RDF_HYPERSPACE)) )
		return;

	if ( s_wp.world != tr.world )
		R_WaterParticlesWorldLoaded();

	if ( s_wp.simFrame != tr.frameCount )
	{
		s_wp.simFrame = tr.frameCount;
		if ( tr.world->numLiquids > 0 )
			WP_Simulate(refdef, view);
		else
			WP_Deactivate("no liquid brushes on this map");
		WP_PrintStats();
	}
	if ( s_wp.numVertices <= 0 )
		return;

	// the bubble texture of the stock assets, else the procedural speck
	if ( !s_wp.bubbleImageChecked )
	{
		s_wp.bubbleImageChecked = qtrue;
		s_wp.bubbleImage = R_FindImageFile("gfx/effects/bubble", IMGTYPE_COLORALPHA,
			IMGFLAG_CLAMPTOEDGE | (tr.linearLight ? IMGFLAG_SRGB : 0));
		if ( !s_wp.bubbleImage )
			ri.Printf(PRINT_DEVELOPER, "r_waterParticles: gfx/effects/bubble missing, bubbles use the procedural ring\n");
	}

	// shader parameters of the frame
	const float pixel = 2.0f * tanf(DEG2RAD(view->fovY) * 0.5f) / (float)MAX(view->viewportHeight, 1);
	VectorSet4(s_wp.drawParams[0], pixel, 2.0f, 10.0f, Com_Clamp(32.0f, 1024.0f, r_waterParticlesDistance->value));
	VectorSet4(s_wp.drawParams[1], 0.2f, (float)r_waterParticlesDebug->integer, 0.75f, s_wp.bubbleImage ? 1.0f : 0.0f);

	R_AddDrawSurf(&s_wpSurface, REFENTITYNUM_WORLD, tr.waterParticleInternalShader,
		0 /* fogIndex */, qfalse /* dlightMap */, qfalse /* postRender */, 0 /* cubemap */);
}

/*
============================================================

Back end

============================================================
*/

void RB_SurfaceWaterParticles( surfaceType_t *surface )
{
	(void)surface;
	RB_EndSurface();

	if ( s_wp.numVertices <= 0 || backEnd.depthFill || backEnd.refractionFill ||
		(backEnd.viewParms.flags & VPF_DEPTHSHADOW) || backEnd.projection2D )
		return;

	// stream the vertices of this frame (orphaned, the size is small)
	const size_t bytes = s_wp.numVertices * sizeof(waterParticleVertex_t);
	if ( !s_wp.vboValid )
	{
		Com_Memset(&s_wp.vbo, 0, sizeof(s_wp.vbo));
		qglGenBuffers(1, &s_wp.vbo.vertexesVBO);
		s_wp.vbo.vertexesSize = sizeof(s_wp.vertices);
		s_wp.vboValid = qtrue;
	}
	R_BindVBO(&s_wp.vbo);
	qglBufferData(GL_ARRAY_BUFFER, sizeof(s_wp.vertices), NULL, GL_STREAM_DRAW);
	qglBufferSubData(GL_ARRAY_BUFFER, 0, bytes, s_wp.vertices);

	Allocator& frameAllocator = *backEndData->perFrameMemory;
	shaderProgram_t *program = &tr.waterParticleShader;

	DrawItem item = {};
	item.renderState.stateBits = GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
	if ( r_waterParticlesDebug->integer == 1 )
		item.renderState.stateBits = GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO;
	item.renderState.cullType = CT_TWO_SIDED;
	item.renderState.depthRange = { 0.0f, 1.0f };
	item.program = program;

	item.numAttributes = 2;
	item.attributes = ojkAllocArray<vertexAttribute_t>(frameAllocator, 2);
	Com_Memset(item.attributes, 0, sizeof(vertexAttribute_t) * 2);
	item.attributes[0].vbo = &s_wp.vbo;
	item.attributes[0].index = ATTR_INDEX_POSITION;
	item.attributes[0].numComponents = 4;
	item.attributes[0].type = GL_FLOAT;
	item.attributes[0].stride = sizeof(waterParticleVertex_t);
	item.attributes[0].offset = offsetof(waterParticleVertex_t, position);
	item.attributes[1].vbo = &s_wp.vbo;
	item.attributes[1].index = ATTR_INDEX_COLOR;
	item.attributes[1].numComponents = 4;
	item.attributes[1].type = GL_FLOAT;
	item.attributes[1].stride = sizeof(waterParticleVertex_t);
	item.attributes[1].offset = offsetof(waterParticleVertex_t, color);

	item.draw.type = DRAW_COMMAND_ARRAYS;
	item.draw.numInstances = 1;
	item.draw.primitiveType = GL_POINTS;
	item.draw.params.arrays.firstVertex = 0;
	item.draw.params.arrays.numVertices = s_wp.numVertices;

	const byte currentFrameScene = backEndData->currentFrame->currentScene;
	const GLuint currentFrameUbo = backEndData->currentFrame->ubo[currentFrameScene];
	const UniformBlockBinding uniformBlockBindings[] = {
		{ currentFrameUbo, (size_t)tr.cameraUboOffsets[tr.viewParms.currentViewParm], UNIFORM_BLOCK_CAMERA },
		RB_GetVolumetricFogBlockUniformBinding()
	};
	DrawItemSetUniformBlockBindings(item, uniformBlockBindings, frameAllocator);

	UniformDataWriter uniformDataWriter;
	SamplerBindingsWriter samplerBindingsWriter;
	uniformDataWriter.Start(program);
	uniformDataWriter.SetUniformVec4(UNIFORM_WATERPARTICLES, s_wp.drawParams[0], 2);
	samplerBindingsWriter.AddStaticImage(s_wp.bubbleImage ? s_wp.bubbleImage : tr.whiteImage, TB_DIFFUSEMAP);
	// the medium in front of the particles: the froxel volume lookup of the
	// transparent layers (mode 1, 3 with RGB extinction), else none
	uniformDataWriter.SetUniformInt(UNIFORM_FROXELFOGMODE, 0);
	const int froxelFogMode = RB_VolumetricFogMode(SS_UNDERWATER);
	if ( froxelFogMode == 1 )
		RB_VolumetricSetupFogDraw(froxelFogMode, uniformDataWriter, samplerBindingsWriter);
	item.uniformData = uniformDataWriter.Finish(frameAllocator);
	item.samplerBindings = samplerBindingsWriter.Finish(frameAllocator, &item.numSamplerBindings);

	// last of the transparent layers: seen from below, a legacy blended water
	// surface (SS_BLEND0) is behind every particle and must not cover them.
	// The price: the specks also draw over blended effects in front of them
	// (faint and small, unlike a surface across the screen).
	const uint32_t key = RB_CreateSortKey(item, 15, SS_BLEND3);
	RB_AddDrawItem(backEndData->currentPass, key, item);
}
