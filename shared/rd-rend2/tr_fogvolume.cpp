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

// Local fog volumes of the froxel fog (r_volumetricFog 2), see
// docs/rend2-volumetric-fog.md, "Local fog volumes".
//
// Small analytic participating media (smoke pocket, steam, haze, dust): a
// soft ellipsoid (a sphere with equal extents) or a soft oriented box, with
// an extinction (fogParms depthForOpaque), a scattering color and an edge
// softness. They are one more medium of FroxelMedium (volumetric_inject.glsl):
// extinctions add, albedos are extinction weighted, every light of the froxel
// fog (baked grid, sun and its cascades, dynamic lights and their shadows,
// HG phase) and the temporal filter and integration apply unchanged.
//
// Sources, all per scene with the lifetime of a dynamic light
// (RE_AddFogVolumeToScene, tr_scene.cpp):
//   - game / FX code through the optional renderer extension GetRefFogVolumeAPI
//   - the map: "FogVolumes" of the optional cubemaps/<map>/env.json
//   - r_fogvol (debug spawn at the camera / trace point, no asset needed)
//
// Culling (once per frame, for the froxel view): bounding sphere against the
// view frustum, the most important MAX_GPU_FOG_VOLUMES are uploaded. Each 8x8x1
// cluster stores a 64-bit membership mask in an R32UI buffer texture.
// Per-slice packed lists are retained only for console diagnostics.
//
// Temporal: each volume is paired with its previous frame state (by id, or by
// identical parameters for anonymous volumes). Appearance changes break
// history locally. Moved, appeared and vanished volumes upload their previous
// transform too; the injection drops the
// history in proportion to the change of the local density, so a moving
// volume leaves no ghost while a static one keeps its full history.

#include "tr_local.h"
#include "json.h"

#include <algorithm>
#include <vector>

// ids of the renderer's own volumes (game ids are used as they are)
#define FOGVOLUME_ID_MAP		0x40000000
#define FOGVOLUME_ID_DEBUG		0x20000000
#define FOGVOLUME_KEY_ANONYMOUS	0x80000000u

#define MAX_DEBUG_FOG_VOLUMES	64

// narrowest soft edge in world units (thinner than a froxel it would alias)
#define FOGVOLUME_MIN_FADE		8.0f
#define FOGVOLUME_MIN_SOFTNESS	0.05f

// Local volumes fade before the last slice: the analytic tail contains
// BSP and height fog only, without local shape, phase, noise or emission.
#define FOGVOLUME_FADE_START	0.8f

// what counts as a change for the temporal filter
#define FOGVOLUME_MOVE_EPSILON	0.01f		// world units
#define FOGVOLUME_AXIS_EPSILON	1e-4f
#define FOGVOLUME_DENSITY_EPSILON 0.01f		// relative

struct debugFogVolume_t
{
	refFogVolume_t volume;
	vec3_t angles;
	vec3_t swing;				// sine motion amplitude (world units)
	float swingPeriod;			// seconds, 0 = static
};

// a volume converted for the GPU
struct fogVolumeEval_t
{
	unsigned int key;
	int shape;
	vec3_t origin;
	vec3_t axis[3];
	vec3_t extents;
	vec4_t rows[3];				// world to unit local space
	float extinction;			// per world unit
	float inner;				// 1 - softness
	float invWidth;				// 1 / softness
	vec3_t albedo;
	vec3_t emission;			// source per world unit at full shape density (0: no glow)
	float anisotropy;			// Henyey-Greenstein g (own or r_volumetricFogAnisotropy)
	vec3_t extinctionColor;		// relative sigma_t.rgb, mean 1 (r_volumetricFogRGBExtinction)
	qboolean noisy;
	float radius;				// bounding sphere around origin
};

// a candidate of this frame
struct fogVolumeCandidate_t
{
	fogVolumeEval_t current;	// extinction 0 when it vanished
	const fogVolumeEval_t *previous;	// NULL: new this frame
	qboolean changed;
	qboolean historyBreak;
	vec3_t center;				// bounding sphere, current and previous state
	float radius;
	float depth;				// view depth of the sphere center
	float sortKey;
};

static struct
{
	// r_fogvol volumes
	debugFogVolume_t debug[MAX_DEBUG_FOG_VOLUMES];
	int numDebug;
	int nextDebugId;
	const world_t *debugWorld;

	// the last world view (r_fogvol placement)
	qboolean hasCamera;
	vec3_t cameraOrigin;
	vec3_t cameraAxis[3];

	// the volumes of the last froxel frame (temporal pairing)
	fogVolumeEval_t previous[MAX_REF_FOG_VOLUMES];
	int numPrevious;
	const world_t *previousWorld;

	// statistics of the last froxel frame (r_fogvol list / slices)
	int statFrame;
	int statSubmitted;
	int statInvalid;
	int statVanished;
	int statVisible;
	int statUploaded;
	int statDropped;
	int statChanged;
	int statPoolUsed;
	int statPoolOverflow;
	int statSlices;
	float statNear, statFar;
	int statSliceHeaders[FROXEL_MAX_SLICES];
	byte statPool[FROXEL_LOCAL_POOL];
	unsigned int statKeys[MAX_GPU_FOG_VOLUMES];
} s_fv;

// Two R32UI texels per 8x8x1 cluster: one bit per uploaded volume.
// No per-cluster cap or index-pool overflow; shared by GL3.2 and compute.
static struct
{
	GLuint buffers[MAX_FRAMES];
	image_t images[MAX_FRAMES];
	int slot;
	std::vector<uint32_t> masks;
} s_fvc;

void R_FogVolumesBindClusters( void )
{
	if (s_fvc.buffers[s_fvc.slot])
		GL_BindToTMU(&s_fvc.images[s_fvc.slot], TB_FPLUS_INDICES);
}

void R_FogVolumesShutdown( void )
{
	for (int f = 0; f < MAX_FRAMES; f++)
	{
		if (!s_fvc.buffers[f])
			continue;
		for (int u = 0; u < MAX_TEXTURE_UNITS; u++)
			if (glState.currenttextures[u] == (int)s_fvc.images[f].texnum)
				glState.currenttextures[u] = 0;
		qglDeleteTextures(1, &s_fvc.images[f].texnum);
	}
	qglDeleteBuffers(MAX_FRAMES, s_fvc.buffers);
	Com_Memset(s_fvc.buffers, 0, sizeof(s_fvc.buffers));
	s_fvc.masks.clear();
	s_fv.numPrevious = 0;
}

static void R_FogVolumesUploadClusters( void )
{
	if (!s_fvc.buffers[0])
	{
		qglGenBuffers(MAX_FRAMES, s_fvc.buffers);
		for (int f = 0; f < MAX_FRAMES; f++)
		{
			image_t *image = &s_fvc.images[f];
			Com_Memset(image, 0, sizeof(*image));
			Q_strncpyz(image->imgName, va("*froxelLocalClusters%d", f), sizeof(image->imgName));
			image->flags = IMGFLAG_TEXBUFFER;
			qglBindBuffer(GL_TEXTURE_BUFFER, s_fvc.buffers[f]);
			qglBufferData(GL_TEXTURE_BUFFER, 8, NULL, GL_STREAM_DRAW);
			qglGenTextures(1, &image->texnum);
			GL_BindToTMU(image, TB_FPLUS_INDICES);
			qglTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, s_fvc.buffers[f]);
		}
	}
	s_fvc.slot = backEndData->realFrameNumber % MAX_FRAMES;
	qglBindBuffer(GL_TEXTURE_BUFFER, s_fvc.buffers[s_fvc.slot]);
	qglBufferData(GL_TEXTURE_BUFFER, s_fvc.masks.size() * sizeof(uint32_t),
		s_fvc.masks.data(), GL_STREAM_DRAW);
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);
}

// Project the world AABB of the current/previous union sphere. A sphere
// touching the camera plane conservatively occupies the whole screen.
static void R_FogVolumeTileBounds( const VolumetricFogBlock *block,
	const fogVolumeCandidate_t *c, int tilesX, int tilesY, int bounds[4] )
{
	bounds[0] = bounds[2] = 0;
	bounds[1] = tilesX - 1;
	bounds[3] = tilesY - 1;
	if (c->depth - c->radius <= 1.0f)
		return;
	const float *m = block->viewProjection;
	float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
	for (int corner = 0; corner < 8; corner++)
	{
		vec3_t p;
		for (int a = 0; a < 3; a++)
			p[a] = c->center[a] + ((corner & (1 << a)) ? c->radius : -c->radius);
		float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
		if (w <= 1e-4f)
			return;
		for (int a = 0; a < 2; a++)
		{
			float v = (m[a] * p[0] + m[4+a] * p[1] + m[8+a] * p[2] + m[12+a]) / w;
			lo[a] = MIN(lo[a], v);
			hi[a] = MAX(hi[a], v);
		}
	}
	for (int a = 0; a < 2; a++)
	{
		const int tiles = a ? tilesY : tilesX;
		const float scale = block->gridSize[a] / block->localClusters[0];
		// One froxel of padding for jitter and floating-point boundary error.
		bounds[2*a] = Com_Clampi(0, tiles-1, (int)floorf((lo[a]*0.5f+0.5f)*scale - 1.0f / block->localClusters[0]));
		bounds[2*a+1] = Com_Clampi(0, tiles-1, (int)floorf((hi[a]*0.5f+0.5f)*scale + 1.0f / block->localClusters[0]));
	}
}

/*
============================================================

Conversion

============================================================
*/

static float R_FogVolumeSRGBToLinear( float c )
{
	return (float)sRGBtoRGB(c);
}

static unsigned int R_FogVolumeKey( const refFogVolume_t *volume )
{
	if ( volume->id != 0 )
		return (unsigned int)volume->id & ~FOGVOLUME_KEY_ANONYMOUS;

	// anonymous: identical parameters are the same volume
	unsigned int key = 2166136261u;
	const byte *bytes = (const byte *)volume;
	for ( size_t i = 0; i < sizeof(*volume); i++ )
		key = (key ^ bytes[i]) * 16777619u;
	return key | FOGVOLUME_KEY_ANONYMOUS;
}

// orthonormal axes (Gram-Schmidt), the world axes when unset or degenerate
static void R_FogVolumeAxes( const vec3_t in[3], vec3_t out[3] )
{
	VectorCopy(in[0], out[0]);
	VectorCopy(in[1], out[1]);
	if ( VectorNormalize(out[0]) < 1e-6f )
	{
		AxisClear(out);
		return;
	}
	VectorMA(out[1], -DotProduct(out[1], out[0]), out[0], out[1]);
	if ( VectorNormalize(out[1]) < 1e-6f )
	{
		AxisClear(out);
		return;
	}
	CrossProduct(out[0], out[1], out[2]);
}

// a volume that glows without extinction: explicit emissive density and color
static qboolean R_FogVolumeEmitsAlone( const refFogVolume_t *volume )
{
	return (qboolean)(volume->emissiveDensity > 0.0f &&
		MAX(volume->emissive[0], MAX(volume->emissive[1], volume->emissive[2])) > 0.0f);
}

qboolean R_FogVolumeHasMedium( const refFogVolume_t *volume )
{
	return (qboolean)(volume->depthForOpaque > 0.0f || R_FogVolumeEmitsAlone(volume));
}

static qboolean R_FogVolumeAppearanceChanged( const fogVolumeEval_t *a, const fogVolumeEval_t *b )
{
	if (a->noisy != b->noisy || fabsf(a->anisotropy - b->anisotropy) > 1e-4f)
		return qtrue;
	for (int i = 0; i < 3; i++)
		if (fabsf(a->albedo[i] - b->albedo[i]) > 1e-4f ||
			fabsf(a->extinctionColor[i] - b->extinctionColor[i]) > 1e-4f)
			return qtrue;
	return qfalse;
}

static qboolean R_FogVolumeEvaluate( const refFogVolume_t *volume, qboolean noise, fogVolumeEval_t *out )
{
	Com_Memset(out, 0, sizeof(*out));
	const qboolean scatters = (qboolean)(volume->depthForOpaque > 0.0f);
	if ( !scatters && !R_FogVolumeEmitsAlone(volume) )
		return qfalse;

	out->key = R_FogVolumeKey(volume);
	out->shape = (volume->shape == FOGVOLUME_BOX) ? FOGVOLUME_BOX : FOGVOLUME_ELLIPSOID;
	VectorCopy(volume->origin, out->origin);
	R_FogVolumeAxes(volume->axis, out->axis);

	float minExtent = 1e30f;
	for ( int i = 0; i < 3; i++ )
	{
		out->extents[i] = MAX(1.0f, volume->extents[i]);
		minExtent = MIN(minExtent, out->extents[i]);
	}

	// the same unit as the BSP fog volumes and the height fog
	if ( scatters )
	{
		out->extinction = (-logf(1.5f / 255.0f)) / volume->depthForOpaque *
			tr.volumetricFogScale * r_volumetricFogScale->value;
		if ( !(out->extinction > 0.0f) )
			return qfalse;
	}

	// emission j = emissive * density per unit: explicit, or coupled to the extinction (an
	// opaque volume then shows exactly the emissive radiance). Scene linear, no conversion.
	const float emissiveDensity = (volume->emissiveDensity > 0.0f) ? volume->emissiveDensity : out->extinction;
	for ( int c = 0; c < 3; c++ )
	{
		const float e = MAX(volume->emissive[c], 0.0f) * emissiveDensity;
		out->emission[c] = (e > 0.0f && e < 1e6f) ? e : 0.0f;	// NaN, Inf -> 0
	}

	float softness = Com_Clamp(FOGVOLUME_MIN_SOFTNESS, 1.0f, volume->softness);
	softness = MIN(1.0f, MAX(softness, FOGVOLUME_MIN_FADE / minExtent));
	out->inner = 1.0f - softness;
	out->invWidth = 1.0f / softness;

	for ( int i = 0; i < 3; i++ )
	{
		const float invExtent = 1.0f / out->extents[i];
		VectorScale(out->axis[i], invExtent, out->rows[i]);
		out->rows[i][3] = -DotProduct(out->axis[i], out->origin) * invExtent;
	}

	// albedo in the fogParms convention (R_LoadFogs, R_VolumetricHeightFog)
	for ( int c = 0; c < 3; c++ )
	{
		float albedo = Com_Clamp(0.0f, 1.0f, volume->color[c]);
		if ( tr.linearLight )
			albedo = R_FogVolumeSRGBToLinear(albedo);
		out->albedo[c] = albedo * tr.identityLight;
	}

	// phase: its own g (FOGVOLUME_ANISOTROPY), else the global one as the other media
	out->anisotropy = Com_Clamp(-0.9f, 0.9f, (volume->flags & FOGVOLUME_ANISOTROPY) ?
		volume->anisotropy : r_volumetricFogAnisotropy->value);
	if ( out->anisotropy != out->anisotropy )
		out->anisotropy = 0.0f;	// NaN
	R_VolumetricExtinctionColor((volume->flags & FOGVOLUME_EXTINCTION) ? volume->extinctionColor : NULL,
		out->extinctionColor);
	out->noisy = (qboolean)(noise && (volume->flags & FOGVOLUME_NOISE));
	out->radius = (out->shape == FOGVOLUME_BOX) ?
		VectorLength(out->extents) :
		MAX(out->extents[0], MAX(out->extents[1], out->extents[2]));
	return qtrue;
}

static qboolean R_FogVolumeContainsPoint( const fogVolumeEval_t *e, const vec3_t p )
{
	vec3_t q;
	for (int a = 0; a < 3; a++)
		q[a] = DotProduct(e->rows[a], p) + e->rows[a][3];
	return (qboolean)(e->shape == FOGVOLUME_BOX ?
		MAX(fabsf(q[0]), MAX(fabsf(q[1]), fabsf(q[2]))) < 1.0f : DotProduct(q, q) < 1.0f);
}

static qboolean R_FogVolumeChanged( const fogVolumeEval_t *a, const fogVolumeEval_t *b )
{
	if ( a->shape != b->shape )
		return qtrue;
	for ( int i = 0; i < 3; i++ )
	{
		if ( fabsf(a->origin[i] - b->origin[i]) > FOGVOLUME_MOVE_EPSILON ||
			fabsf(a->extents[i] - b->extents[i]) > FOGVOLUME_MOVE_EPSILON )
			return qtrue;
		for ( int j = 0; j < 3; j++ )
		{
			if ( fabsf(a->axis[i][j] - b->axis[i][j]) > FOGVOLUME_AXIS_EPSILON )
				return qtrue;
		}
	}
	if ( fabsf(a->extinction - b->extinction) > FOGVOLUME_DENSITY_EPSILON * MAX(a->extinction, b->extinction) )
		return qtrue;
	if ( fabsf(a->inner - b->inner) > 0.005f )
		return qtrue;
	return qfalse;
}

// smallest sphere around two spheres
static void R_FogVolumeSphereUnion( const vec3_t c0, float r0, const vec3_t c1, float r1, vec3_t c, float *r )
{
	vec3_t d;
	VectorSubtract(c1, c0, d);
	const float dist = VectorLength(d);
	if ( dist + r1 <= r0 )
	{
		VectorCopy(c0, c);
		*r = r0;
		return;
	}
	if ( dist + r0 <= r1 )
	{
		VectorCopy(c1, c);
		*r = r1;
		return;
	}
	const float radius = 0.5f * (dist + r0 + r1);
	VectorMA(c0, (radius - r0) / dist, d, c);
	*r = radius;
}

/*
============================================================

Frame build (RB_UpdateVolumetricConstants)

============================================================
*/

// view distance of the near side of slice k, as R_VolumetricSliceDistance
static float R_FogVolumeSliceDistance( int k, float nearZ, float farZ, int numSlices )
{
	if ( k <= 0 )
		return 0.0f;
	return nearZ * powf(farZ / nearZ, (float)k / (float)numSlices);
}

qboolean R_FogVolumeSphereInFrustum( const viewParms_t *view, const vec3_t forward,
	const vec3_t center, float radius, float maxDepth, float *depth )
{
	vec3_t delta;
	VectorSubtract(center, view->ori.origin, delta);
	*depth = DotProduct(delta, forward);
	if ( *depth + radius < 0.0f || *depth - radius > maxDepth )
		return qfalse;

	for ( int p = 0; p < 4; p++ )
	{
		const cplane_t *plane = &view->frustum[p];
		if ( DotProduct(center, plane->normal) - plane->dist < -radius )
			return qfalse;
	}
	return qtrue;
}

static void R_FogVolumeViewForward( const viewParms_t *view, vec3_t forward )
{
	VectorCopy(view->ori.axis[0], forward);
	VectorNormalize(forward);
}

/*
=================
R_FogVolumesInFrustum

Does the froxel view need a volume for the local fog volumes: one of this
scene, or one of the previous frame that vanished (its history is dropped).
=================
*/
qboolean R_FogVolumesInFrustum( const viewParms_t *view, const trRefdef_t *refdef, float farZ )
{
	if ( !view || !tr.world )
		return qfalse;

	vec3_t forward;
	R_FogVolumeViewForward(view, forward);
	float depth;

	for ( int i = 0; i < refdef->num_fogVolumes; i++ )
	{
		const refFogVolume_t *volume = &refdef->fogVolumes[i];
		if ( volume->depthForOpaque <= 0.0f && !R_FogVolumeEmitsAlone(volume) )
			continue;
		const float radius = (volume->shape == FOGVOLUME_BOX) ?
			VectorLength(volume->extents) :
			MAX(volume->extents[0], MAX(volume->extents[1], volume->extents[2]));
		if ( R_FogVolumeSphereInFrustum(view, forward, volume->origin, MAX(1.0f, radius), farZ, &depth) )
			return qtrue;
	}

	if ( s_fv.previousWorld == tr.world )
	{
		for ( int i = 0; i < s_fv.numPrevious; i++ )
		{
			const fogVolumeEval_t *previous = &s_fv.previous[i];
			if ( R_FogVolumeSphereInFrustum(view, forward, previous->origin, previous->radius, farZ, &depth) )
				return qtrue;
		}
	}

	return qfalse;
}

/*
=================
R_FogVolumesBuild

Converts, culls, sorts and packs the local fog volumes of the froxel view
into the VolumetricFog block. Called once per frame by
RB_UpdateVolumetricConstants for the scene that builds the froxel volume.
Returns the number of uploaded volumes.
=================
*/
int R_FogVolumesBuild( VolumetricFogBlock *block, const viewParms_t *view, const trRefdef_t *refdef,
	const vec3_t forward, float nearZ, float farZ, int numSlices, qboolean noise )
{
	static fogVolumeCandidate_t candidates[MAX_REF_FOG_VOLUMES * 2];
	static fogVolumeEval_t current[MAX_REF_FOG_VOLUMES];
	static qboolean previousMatched[MAX_REF_FOG_VOLUMES];
	static int order[MAX_REF_FOG_VOLUMES * 2];

	VectorSet4(block->localParams, 0.0f, 0.0f, 0.0f, 0.0f);
	Com_Memset(block->extinctionPalette, 0, sizeof(block->extinctionPalette));
	VectorSet4(block->extinctionPalette[0], 1.0f, 1.0f, 1.0f, 0.0f);
	int paletteSize = 1;
	int paletteOverflow = 0;
	VectorSet4(block->localClusters, 8.0f, 1.0f, 1.0f, 0.0f);

	if ( s_fv.previousWorld != tr.world )
	{
		s_fv.numPrevious = 0;
		s_fv.previousWorld = tr.world;
	}

	numSlices = Com_Clampi(1, FROXEL_MAX_SLICES, numSlices);
	const float fadeEnd = R_FogVolumeSliceDistance(numSlices - 1, nearZ, farZ, numSlices);
	const float fadeStart = FOGVOLUME_FADE_START * fadeEnd;

	s_fv.statFrame = backEndData->realFrameNumber;
	s_fv.statSubmitted = refdef->num_fogVolumes;
	s_fv.statInvalid = 0;
	s_fv.statVanished = 0;
	s_fv.statVisible = 0;
	s_fv.statUploaded = 0;
	s_fv.statDropped = 0;
	s_fv.statChanged = 0;
	s_fv.statPoolUsed = 0;
	s_fv.statPoolOverflow = 0;
	s_fv.statSlices = numSlices;
	s_fv.statNear = nearZ;
	s_fv.statFar = farZ;
	Com_Memset(s_fv.statSliceHeaders, 0, sizeof(s_fv.statSliceHeaders));

	// convert and pair with the previous frame
	Com_Memset(previousMatched, 0, sizeof(previousMatched));
	int numCurrent = 0;
	int numCandidates = 0;
	for ( int i = 0; i < refdef->num_fogVolumes && numCurrent < MAX_REF_FOG_VOLUMES; i++ )
	{
		fogVolumeEval_t *e = &current[numCurrent];
		if ( !R_FogVolumeEvaluate(&refdef->fogVolumes[i], noise, e) )
		{
			s_fv.statInvalid++;
			continue;
		}
		numCurrent++;

		fogVolumeCandidate_t *c = &candidates[numCandidates++];
		c->current = *e;
		c->previous = NULL;
		c->changed = qtrue;
		c->historyBreak = qfalse;
		for ( int j = 0; j < s_fv.numPrevious; j++ )
		{
			if ( !previousMatched[j] && s_fv.previous[j].key == e->key )
			{
				previousMatched[j] = qtrue;
				c->previous = &s_fv.previous[j];
				c->changed = R_FogVolumeChanged(e, c->previous);
				c->historyBreak = R_FogVolumeAppearanceChanged(e, c->previous);
				break;
			}
		}

		VectorCopy(e->origin, c->center);
		c->radius = e->radius;
		if ( c->changed && c->previous )
		{
			R_FogVolumeSphereUnion(e->origin, e->radius, c->previous->origin, c->previous->radius,
				c->center, &c->radius);
		}
	}

	// vanished since the previous frame: one more frame with no density, so
	// the history of their old place is dropped
	for ( int j = 0; j < s_fv.numPrevious && numCandidates < MAX_REF_FOG_VOLUMES * 2; j++ )
	{
		if ( previousMatched[j] )
			continue;

		fogVolumeCandidate_t *c = &candidates[numCandidates++];
		c->current = s_fv.previous[j];
		c->current.extinction = 0.0f;
		VectorClear(c->current.emission);	// no glow after it vanished
		c->previous = &s_fv.previous[j];
		c->changed = qtrue;
		c->historyBreak = qfalse;
		VectorCopy(c->previous->origin, c->center);
		c->radius = c->previous->radius;
		s_fv.statVanished++;
	}

	// Honor small GL3.2 texture-buffer limits by widening XY tiles only.
	int tileSize = 8;
	int tilesX = ((int)block->gridSize[0] + tileSize-1) / tileSize;
	int tilesY = ((int)block->gridSize[1] + tileSize-1) / tileSize;
	while (2 * tilesX * tilesY * numSlices > glRefConfig.maxTextureBufferSize)
	{
		tileSize *= 2;
		tilesX = ((int)block->gridSize[0] + tileSize-1) / tileSize;
		tilesY = ((int)block->gridSize[1] + tileSize-1) / tileSize;
	}
	VectorSet4(block->localClusters, (float)tileSize, (float)tilesX, (float)tilesY, 0.0f);

	// Frustum and importance: projected coverage times optical opacity.
	// Explicit glow remains selectable even without extinction.
	int numVisible = 0;
	for ( int i = 0; i < numCandidates; i++ )
	{
		fogVolumeCandidate_t *c = &candidates[i];
		if ( !R_FogVolumeSphereInFrustum(view, forward, c->center, c->radius, fadeEnd, &c->depth) )
			continue;
		int b[4];
		R_FogVolumeTileBounds(block, c, tilesX, tilesY, b);
		const float area = (float)((b[1]-b[0]+1) * (b[3]-b[2]+1));
		const float extinction = MAX(c->current.extinction, c->previous ? c->previous->extinction : 0.0f);
		const float opacity = -expm1f(-extinction * 2.0f * c->radius);
		const float glow = MAX(c->current.emission[0], MAX(c->current.emission[1], c->current.emission[2]));
		c->sortKey = area * MAX(opacity, MIN(1.0f, glow * 2.0f * c->radius));
		// Protect the medium at the camera, including its previous state.
		if (R_FogVolumeContainsPoint(&c->current, view->ori.origin) ||
			(c->previous && R_FogVolumeContainsPoint(c->previous, view->ori.origin)))
			c->sortKey += block->gridSize[0] * block->gridSize[1];
		order[numVisible++] = i;
	}
	std::sort(order, order + numVisible, [&]( int a, int b ) {
		if (candidates[a].sortKey != candidates[b].sortKey)
			return candidates[a].sortKey > candidates[b].sortKey;
		return candidates[a].current.key < candidates[b].current.key;
	});

	const int numUploaded = MIN(numVisible, MAX_GPU_FOG_VOLUMES);
	s_fv.statVisible = numVisible;
	s_fv.statUploaded = numUploaded;
	s_fv.statDropped = numVisible - numUploaded;
	if ( s_fv.statDropped > 0 )
	{
		ri.Printf(PRINT_DEVELOPER, "local fog volumes: %d in view, the most important %d are used\n",
			numVisible, MAX_GPU_FOG_VOLUMES);
	}

	// volume data
	qboolean anyEmission = qfalse;
	for ( int n = 0; n < numUploaded; n++ )
	{
		const fogVolumeCandidate_t *c = &candidates[order[n]];
		const fogVolumeEval_t *e = &c->current;
		const fogVolumeEval_t *p = c->previous ? c->previous : e;

		VectorCopy4(e->rows[0], block->localX[n]);
		VectorCopy4(e->rows[1], block->localY[n]);
		VectorCopy4(e->rows[2], block->localZ[n]);
		VectorCopy4(p->rows[0], block->localPrevX[n]);
		VectorCopy4(p->rows[1], block->localPrevY[n]);
		VectorCopy4(p->rows[2], block->localPrevZ[n]);
		VectorSet4(block->localColor[n], e->albedo[0], e->albedo[1], e->albedo[2], e->extinction);
		// extinction color (r_volumetricFogRGBExtinction): an entry of the palette, 0 = neutral
		static const vec3_t neutral = { 1.0f, 1.0f, 1.0f };
		int palette = 0;
		if ( R_VolumetricFroxelRGB() && !VectorCompare(e->extinctionColor, neutral) )
		{
			for ( palette = 1; palette < paletteSize; palette++ )
			{
				if ( VectorCompare(block->extinctionPalette[palette], e->extinctionColor) )
					break;
			}
			if ( palette == paletteSize )
			{
				if ( paletteSize < FROXEL_EXTINCTION_PALETTE )
				{
					VectorSet4(block->extinctionPalette[paletteSize++], e->extinctionColor[0],
						e->extinctionColor[1], e->extinctionColor[2], 0.0f);
				}
				else
				{
					palette = 0;	// full: neutral (the most important volumes keep their colors)
					paletteOverflow++;
				}
			}
		}
		VectorSet4(block->localShape[n], (float)e->shape, e->inner, e->invWidth,
			(e->noisy ? 1.0f : 0.0f) + 2.0f * (float)palette + (c->historyBreak ? 32.0f : 0.0f));
		VectorSet4(block->localEmission[n], e->emission[0], e->emission[1], e->emission[2], e->anisotropy);
		if ( !VectorCompare(e->emission, vec3_origin) )
			anyEmission = qtrue;
		// x: 0 unchanged, else 1 + the shape of the previous state (the id may change shape)
		VectorSet4(block->localMotion[n],
			c->changed ? 1.0f + (float)p->shape : 0.0f,
			c->previous ? c->previous->extinction : 0.0f,
			p->inner,
			p->invWidth);

		s_fv.statKeys[n] = e->key;
		if ( c->changed || c->historyBreak )
			s_fv.statChanged++;
	}

	if ( paletteOverflow > 0 )
	{
		ri.Printf(PRINT_DEVELOPER, "local fog volumes: more than %d extinction colors, %d volumes neutral\n",
			FROXEL_EXTINCTION_PALETTE - 1, paletteOverflow);
	}

	// XYZ cluster masks; keep the slice summaries for r_fogvol diagnostics.
	if (numUploaded > 0)
		s_fvc.masks.assign(2 * tilesX * tilesY * numSlices, 0);
	int tileBounds[MAX_GPU_FOG_VOLUMES][4];
	for (int n = 0; n < numUploaded; n++)
		R_FogVolumeTileBounds(block, &candidates[order[n]], tilesX, tilesY, tileBounds[n]);

	// per slice lists, near to far; the last slice has none (see the fade)
	int poolUsed = 0;
	for ( int k = 0; k < numSlices - 1; k++ )
	{
		const float sliceNear = R_FogVolumeSliceDistance(k, nearZ, farZ, numSlices);
		const float sliceFar = R_FogVolumeSliceDistance(k + 1, nearZ, farZ, numSlices);
		const int first = poolUsed;
		int count = 0;

		for ( int n = 0; n < numUploaded; n++ )
		{
			const fogVolumeCandidate_t *c = &candidates[order[n]];
			if ( c->depth + c->radius < sliceNear || c->depth - c->radius > sliceFar )
				continue;
			const int *b = tileBounds[n];
			for (int y = b[2]; y <= b[3]; y++)
				for (int x = b[0]; x <= b[1]; x++)
					s_fvc.masks[2 * ((k * tilesY + y) * tilesX + x) + (n >> 5)] |= uint32_t(1) << (n & 31);
			if ( poolUsed >= FROXEL_LOCAL_POOL )
			{
				s_fv.statPoolOverflow++;
				continue;
			}

			s_fv.statPool[poolUsed] = (byte)n;
			poolUsed++;
			count++;
		}

		s_fv.statSliceHeaders[k] = first | (count << 16);
	}
	if (numUploaded > 0)
		R_FogVolumesUploadClusters();
	s_fv.statPoolUsed = poolUsed;
	if ( s_fv.statPoolOverflow > 0 )
	{
		ri.Printf(PRINT_DEVELOPER, "local fog volumes: diagnostic slice pool full, %d entries omitted (rendering unaffected)\n",
			s_fv.statPoolOverflow);
	}

	VectorSet4(block->localParams,
		(float)numUploaded,
		fadeStart,
		1.0f / MAX(fadeEnd - fadeStart, 1.0f),
		anyEmission ? 1.0f : 0.0f);

	// Pair against what was actually rendered. A stable-ID volume admitted
	// after culling must be new to history, even if its submitted state did
	// not change. Vanished candidates are retained for this frame only.
	s_fv.numPrevious = 0;
	for (int n = 0; n < numUploaded; n++)
	{
		const fogVolumeEval_t *e = &candidates[order[n]].current;
		if (e->extinction > 0.0f || !VectorCompare(e->emission, vec3_origin))
			s_fv.previous[s_fv.numPrevious++] = *e;
	}

	return numUploaded;
}

/*
============================================================

Scene submission: the map's volumes and r_fogvol's

============================================================
*/

static void R_FogVolumesCheckWorld( void )
{
	if ( s_fv.debugWorld != tr.world )
	{
		s_fv.numDebug = 0;
		s_fv.hasCamera = qfalse;
		s_fv.debugWorld = tr.world;
	}
}

void R_FogVolumesBeginScene( const refdef_t *fd )
{
	if ( !tr.world || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) )
		return;

	R_FogVolumesCheckWorld();

	s_fv.hasCamera = qtrue;
	VectorCopy(fd->vieworg, s_fv.cameraOrigin);
	for ( int i = 0; i < 3; i++ )
		VectorCopy(fd->viewaxis[i], s_fv.cameraAxis[i]);

	// nothing reads them outside the froxel fog
	if ( !R_VolumetricFroxelEnabled() )
		return;

	for ( int i = 0; i < tr.world->numFogVolumes; i++ )
		RE_AddFogVolumeToScene(&tr.world->fogVolumes[i]);
	if ( r_waterfallMist->integer && r_waterfallMistDensity->value > 0.0f &&
		r_waterfallMistBalance->value > 0.0f )
	{
		for ( int i = 0; i < tr.world->numWaterfallFogVolumes; i++ )
		{
			refFogVolume_t volume = tr.world->waterfallFogVolumes[i];
			const float radiusScale = r_waterfallMistRadius->value;
			VectorScale(volume.extents, radiusScale, volume.extents);
			const float densityScale = r_waterfallMistDensity->value * r_waterfallMistBalance->value;
			volume.depthForOpaque /= Q_max(0.001f, densityScale);
			RE_AddFogVolumeToScene(&volume);
		}
	}

	const float seconds = fd->time * 0.001f;
	for ( int i = 0; i < s_fv.numDebug; i++ )
	{
		const debugFogVolume_t *d = &s_fv.debug[i];
		if ( d->swingPeriod <= 0.0f )
		{
			RE_AddFogVolumeToScene(&d->volume);
			continue;
		}

		refFogVolume_t moving = d->volume;
		const float s = sinf(2.0f * (float)M_PI * seconds / d->swingPeriod);
		VectorMA(d->volume.origin, s, d->swing, moving.origin);
		RE_AddFogVolumeToScene(&moving);
	}
}

/*
============================================================

env.json "FogVolumes" (R_LoadEnvironmentJson, tr_bsp.cpp)

		"FogVolumes": [
				{ "Shape": "sphere", "Origin": [x, y, z], "Radius": 128,
						"Opaque": 600, "Color": [0.8, 0.8, 0.85], "Softness": 0.6 },
				{ "Shape": "box", "Origin": [x, y, z], "Size": [256, 128, 64],
						"Angles": [0, 45, 0], "Opaque": 1500, "Noise": 1 }
		]

Shape sphere | ellipsoid | box; Radius (sphere) or Size (half extents);
Angles pitch yaw roll; Opaque as fogParms depthForOpaque; Color as fogParms;
Softness 0..1; Noise 0 | 1 (r_volumetricFogNoise 8). Defaults as r_fogvol add.
"Anisotropy": Henyey-Greenstein g -0.9..0.9 of the scattering (> 0 forward, < 0
back); absent = r_volumetricFogAnisotropy. "Color" is the scattering albedo.
Emission (glowing gas, lights nothing): "Emissive": [r, g, b] scene linear HDR
radiance of the opaque medium, "EmissiveDensity": per unit (default: the
extinction of the volume). "Opaque": 0 = no extinction, a pure glow, which
needs an EmissiveDensity.
"Extinction": [r, g, b] relative extinction per channel (r_volumetricFogRGBExtinction),
normalized to mean 1 (Opaque stays the mean opaque distance): [3, 0.5, 0.5]
absorbs red, the medium and what is behind it turn cyan. Absent = neutral.

============================================================
*/

#define FOGVOLUME_DEFAULT_OPAQUE	600.0f
#define FOGVOLUME_DEFAULT_SOFTNESS	0.5f
static const vec3_t fogVolumeDefaultColor = { 0.75f, 0.75f, 0.78f };

static qboolean R_FogVolumeJsonVector( const char *json, const char *jsonEnd, const char *name, vec3_t out )
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

static qboolean R_FogVolumeJsonFloat( const char *json, const char *jsonEnd, const char *name, float *out )
{
	const char *value = JSON_ObjectGetNamedValue(json, jsonEnd, name);
	if ( !value || JSON_ValueGetType(value, jsonEnd) != JSONTYPE_VALUE )
		return qfalse;

	// true / false for the flags
	if ( *value == 't' || *value == 'f' )
		*out = (*value == 't') ? 1.0f : 0.0f;
	else
		*out = JSON_ValueGetFloat(value, jsonEnd);
	return qtrue;
}

void R_LoadFogVolumesJson( world_t *world, const char *json, const char *jsonEnd, const char *filename )
{
	world->numFogVolumes = 0;
	world->fogVolumes = NULL;

	const char *array = JSON_ObjectGetNamedValue(json, jsonEnd, "FogVolumes");
	if ( !array )
		return;
	if ( JSON_ValueGetType(array, jsonEnd) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: FogVolumes is not an array\n", filename);
		return;
	}

	const int count = MIN((int)JSON_ArrayGetIndex(array, jsonEnd, NULL, 0), MAX_REF_FOG_VOLUMES);
	if ( count <= 0 )
		return;

	world->fogVolumes = (refFogVolume_t *)Hunk_Alloc(count * sizeof(refFogVolume_t), h_low);
	for ( int i = 0; i < count; i++ )
	{
		const char *entry = JSON_ArrayGetValue(array, jsonEnd, i);
		if ( !entry || JSON_ValueGetType(entry, jsonEnd) != JSONTYPE_OBJECT )
		{
			ri.Printf(PRINT_WARNING, "%s: FogVolumes[%d] is not an object\n", filename, i);
			continue;
		}

		refFogVolume_t volume = {};
		volume.id = FOGVOLUME_ID_MAP | i;
		if ( !R_FogVolumeJsonVector(entry, jsonEnd, "Origin", volume.origin) )
		{
			ri.Printf(PRINT_WARNING, "%s: FogVolumes[%d] has no Origin\n", filename, i);
			continue;
		}

		char shape[32] = "sphere";
		const char *shapeValue = JSON_ObjectGetNamedValue(entry, jsonEnd, "Shape");
		if ( shapeValue )
			JSON_ValueGetString(shapeValue, jsonEnd, shape, sizeof(shape));
		volume.shape = !Q_stricmp(shape, "box") ? FOGVOLUME_BOX : FOGVOLUME_ELLIPSOID;
		if ( Q_stricmp(shape, "box") && Q_stricmp(shape, "sphere") && Q_stricmp(shape, "ellipsoid") )
			ri.Printf(PRINT_WARNING, "%s: FogVolumes[%d]: unknown Shape '%s', sphere used\n", filename, i, shape);

		float radius = 64.0f;
		if ( !R_FogVolumeJsonVector(entry, jsonEnd, "Size", volume.extents) )
		{
			R_FogVolumeJsonFloat(entry, jsonEnd, "Radius", &radius);
			VectorSet(volume.extents, radius, radius, radius);
		}

		vec3_t angles = { 0.0f, 0.0f, 0.0f };
		R_FogVolumeJsonVector(entry, jsonEnd, "Angles", angles);
		AnglesToAxis(angles, volume.axis);

		volume.depthForOpaque = FOGVOLUME_DEFAULT_OPAQUE;
		R_FogVolumeJsonFloat(entry, jsonEnd, "Opaque", &volume.depthForOpaque);
		VectorCopy(fogVolumeDefaultColor, volume.color);
		R_FogVolumeJsonVector(entry, jsonEnd, "Color", volume.color);
		volume.softness = FOGVOLUME_DEFAULT_SOFTNESS;
		R_FogVolumeJsonFloat(entry, jsonEnd, "Softness", &volume.softness);
		float noise = 0.0f;
		R_FogVolumeJsonFloat(entry, jsonEnd, "Noise", &noise);
		volume.flags = (noise > 0.5f) ? FOGVOLUME_NOISE : 0;
		R_FogVolumeJsonVector(entry, jsonEnd, "Emissive", volume.emissive);
		R_FogVolumeJsonFloat(entry, jsonEnd, "EmissiveDensity", &volume.emissiveDensity);
		if ( R_FogVolumeJsonFloat(entry, jsonEnd, "Anisotropy", &volume.anisotropy) )
			volume.flags |= FOGVOLUME_ANISOTROPY;
		if ( R_FogVolumeJsonVector(entry, jsonEnd, "Extinction", volume.extinctionColor) )
			volume.flags |= FOGVOLUME_EXTINCTION;

		if ( volume.depthForOpaque <= 0.0f && !R_FogVolumeEmitsAlone(&volume) )
		{
			ri.Printf(PRINT_WARNING, "%s: FogVolumes[%d]: Opaque must be > 0 (or Emissive with an EmissiveDensity)\n",
				filename, i);
			continue;
		}

		world->fogVolumes[world->numFogVolumes++] = volume;
	}

	ri.Printf(PRINT_ALL, "%s: %d local fog volume%s\n", filename, world->numFogVolumes,
		(world->numFogVolumes == 1) ? "" : "s");
}

/*
============================================================

r_fogvol console command

============================================================
*/

static qboolean R_FogVolumeParseNumber( const char *s, float *out )
{
	char *end;
	const double value = strtod(s, &end);
	if ( end == s || *end != '\0' )
		return qfalse;
	*out = (float)value;
	return qtrue;
}

static qboolean R_FogVolumeParseNumbers( int first, int count, float *out )
{
	if ( first + count > ri.Cmd_Argc() )
		return qfalse;
	for ( int i = 0; i < count; i++ )
	{
		if ( !R_FogVolumeParseNumber(ri.Cmd_Argv(first + i), &out[i]) )
			return qfalse;
	}
	return qtrue;
}

static void R_FogVolumeUsage( void )
{
	ri.Printf(PRINT_ALL,
		"usage: r_fogvol                          local fog volumes and last frame statistics\n"
		"       r_fogvol add [sphere|ellipsoid|box] [<key> <value> ...]   (sv_cheats)\n"
		"         radius  <r>                     sphere radius / cube half size (default 128)\n"
		"         size    <x> <y> <z>             radii / half sizes along the axes\n"
		"         opaque  <units>                 opaque distance through full density (default 600)\n"
		"         color   <r> <g> <b>             scattering color 0..1 (default 0.75 0.75 0.78)\n"
		"         soft    <0..1>                  soft edge, part of the extents (default 0.5)\n"
		"         angles  <pitch> <yaw> <roll>    orientation\n"
		"         noise   0|1                     density noise (r_volumetricFogNoise 8)\n"
		"         aniso   <g>                     Henyey-Greenstein g -0.9..0.9 (default r_volumetricFogAnisotropy)\n"
		"         extinction <r> <g> <b>          relative extinction per channel, mean 1 (r_volumetricFogRGBExtinction)\n"
		"         emit    <r> <g> <b> [density]   glow: scene linear radiance of the opaque medium,\n"
		"                                         emissive density per unit (default: the extinction);\n"
		"                                         opaque 0 = no extinction (needs a density)\n"
		"         at      trace|view|eye|<x y z>  trace: in front of the wall under the crosshair\n"
		"                                         (default), view: 1.5 radii ahead, eye: on the camera\n"
		"         swing   <x> <y> <z> <seconds>   sine motion (moving volume test)\n"
		"       r_fogvol test <count> [spread]    spheres around the camera (timings, sv_cheats)\n"
		"       r_fogvol emittest                 pure glow (no extinction) + dense glowing smoke ahead\n"
		"       r_fogvol mediumtest               per-medium phase: g +0.8 and g -0.8 overlapping,\n"
		"                                         isotropic red albedo beside them\n"
		"       r_fogvol rgbtest                  RGB extinction (r_volumetricFogRGBExtinction): neutral, red absorbing,\n"
		"                                         blue absorbing and mixed white spheres in a row\n"
		"       r_fogvol remove <index> | clear\n"
		"       r_fogvol slices                   slice lists of the last frame\n"
		"       r_fogvol dump                     r_fogvol volumes as an env.json \"FogVolumes\" array\n"
		"example: r_fogvol add sphere radius 96 opaque 300 color 0.6 0.6 0.65 soft 0.7\n");
}

static void R_FogVolumeWarnings( void )
{
	if ( r_volumetricFog->integer != 2 || !R_VolumetricFroxelEnabled() )
		ri.Printf(PRINT_WARNING, "local fog volumes need r_volumetricFog 2 (then vid_restart)\n");
	else if ( !r_depthPrepass->integer )
		ri.Printf(PRINT_WARNING, "local fog volumes need r_depthPrepass 1\n");
}

static const char *R_FogVolumeShapeName( int shape, const vec3_t extents )
{
	if ( shape == FOGVOLUME_BOX )
		return "box";
	return (extents[0] == extents[1] && extents[1] == extents[2]) ? "sphere" : "ellipsoid";
}

static const char *R_FogVolumeKeyName( unsigned int key )
{
	if ( key & FOGVOLUME_KEY_ANONYMOUS )
		return "anonymous";
	if ( (key & 0xF0000000u) == FOGVOLUME_ID_MAP )
		return va("map %d", (int)(key & 0x0FFFFFFFu));
	if ( (key & 0xF0000000u) == FOGVOLUME_ID_DEBUG )
		return va("r_fogvol id %d", (int)(key & 0x0FFFFFFFu));
	return va("id %d", (int)key);
}

static void R_FogVolumePrintVolume( const char *label, const refFogVolume_t *v )
{
	const qboolean emits = (qboolean)(MAX(v->emissive[0], MAX(v->emissive[1], v->emissive[2])) > 0.0f);
	char emission[128] = "";
	if ( emits )
	{
		Com_sprintf(emission, sizeof(emission), " emit (%g %g %g) density %s", v->emissive[0], v->emissive[1],
			v->emissive[2], (v->emissiveDensity > 0.0f) ? va("%g", v->emissiveDensity) : "= extinction");
	}
	if ( v->flags & FOGVOLUME_EXTINCTION )
	{
		Q_strcat(emission, sizeof(emission), va(" extinction (%g %g %g)",
			v->extinctionColor[0], v->extinctionColor[1], v->extinctionColor[2]));
	}
	ri.Printf(PRINT_ALL, "  %s %s at (%.0f %.0f %.0f) extents (%.0f %.0f %.0f) opaque %g color (%g %g %g) soft %g%s%s%s\n",
		label, R_FogVolumeShapeName(v->shape, v->extents),
		v->origin[0], v->origin[1], v->origin[2], v->extents[0], v->extents[1], v->extents[2],
		v->depthForOpaque, v->color[0], v->color[1], v->color[2], v->softness,
		(v->flags & FOGVOLUME_NOISE) ? " noise" : "",
		(v->flags & FOGVOLUME_ANISOTROPY) ? va(" aniso %g", v->anisotropy) : "", emission);
}

static void R_FogVolumeList( void )
{
	R_FogVolumesCheckWorld();

	const int numMap = tr.world ? tr.world->numFogVolumes : 0;
	ri.Printf(PRINT_ALL, "map volumes (cubemaps/<map>/env.json): %d\n", numMap);
	for ( int i = 0; i < numMap; i++ )
		R_FogVolumePrintVolume(va("map %d:", i), &tr.world->fogVolumes[i]);

	ri.Printf(PRINT_ALL, "r_fogvol volumes: %d / %d\n", s_fv.numDebug, MAX_DEBUG_FOG_VOLUMES);
	for ( int i = 0; i < s_fv.numDebug; i++ )
	{
		const debugFogVolume_t *d = &s_fv.debug[i];
		R_FogVolumePrintVolume(va("%d:", i), &d->volume);
		if ( d->swingPeriod > 0.0f )
		{
			ri.Printf(PRINT_ALL, "     swing (%g %g %g) over %g s\n",
				d->swing[0], d->swing[1], d->swing[2], d->swingPeriod);
		}
	}

	if ( s_fv.statSlices > 0 )
	{
		const int age = backEndData ? (int)backEndData->realFrameNumber - s_fv.statFrame : 0;
		ri.Printf(PRINT_ALL,
			"last froxel frame (%d frames ago): %d submitted, %d invalid, %d vanished, %d in view,\n"
			"  %d uploaded (max %d, %d dropped), %d changed, slice lists %d / %d entries (%d dropped)\n",
			age, s_fv.statSubmitted, s_fv.statInvalid, s_fv.statVanished, s_fv.statVisible,
			s_fv.statUploaded, MAX_GPU_FOG_VOLUMES, s_fv.statDropped, s_fv.statChanged,
			s_fv.statPoolUsed, FROXEL_LOCAL_POOL, s_fv.statPoolOverflow);
	}
	else
	{
		ri.Printf(PRINT_ALL, "no froxel frame with local fog volumes yet\n");
	}

	R_FogVolumeWarnings();
}

static void R_FogVolumeSlices( void )
{
	if ( s_fv.statSlices <= 0 )
	{
		ri.Printf(PRINT_ALL, "no froxel frame with local fog volumes yet\n");
		return;
	}

	ri.Printf(PRINT_ALL, "uploaded volumes (GPU index, nearest first):\n");
	for ( int n = 0; n < s_fv.statUploaded; n++ )
		ri.Printf(PRINT_ALL, "  %2d: %s\n", n, R_FogVolumeKeyName(s_fv.statKeys[n]));

	ri.Printf(PRINT_ALL, "slices with volumes (view depth range: count: GPU indices):\n");
	int maxCount = 0;
	for ( int k = 0; k < s_fv.statSlices; k++ )
	{
		const int header = s_fv.statSliceHeaders[k];
		const int first = header & 0xffff;
		const int count = header >> 16;
		if ( count <= 0 )
			continue;
		maxCount = MAX(maxCount, count);

		char line[512];
		Com_sprintf(line, sizeof(line), "  %3d [%6.0f %6.0f]: %2d:", k,
			R_FogVolumeSliceDistance(k, s_fv.statNear, s_fv.statFar, s_fv.statSlices),
			R_FogVolumeSliceDistance(k + 1, s_fv.statNear, s_fv.statFar, s_fv.statSlices), count);
		for ( int j = 0; j < count && first + j < FROXEL_LOCAL_POOL; j++ )
			Q_strcat(line, sizeof(line), va(" %d", s_fv.statPool[first + j]));
		ri.Printf(PRINT_ALL, "%s\n", line);
	}
	ri.Printf(PRINT_ALL, "largest slice list: %d, pool %d / %d\n", maxCount, s_fv.statPoolUsed, FROXEL_LOCAL_POOL);
}

static void R_FogVolumeDump( void )
{
	ri.Printf(PRINT_ALL, "\"FogVolumes\": [\n");
	for ( int i = 0; i < s_fv.numDebug; i++ )
	{
		const debugFogVolume_t *d = &s_fv.debug[i];
		const refFogVolume_t *v = &d->volume;
		char emission[160] = "";
		if ( MAX(v->emissive[0], MAX(v->emissive[1], v->emissive[2])) > 0.0f )
		{
			Com_sprintf(emission, sizeof(emission), ", \"Emissive\": [%g, %g, %g], \"EmissiveDensity\": %g",
				v->emissive[0], v->emissive[1], v->emissive[2], v->emissiveDensity);
		}
		if ( v->flags & FOGVOLUME_ANISOTROPY )
			Q_strcat(emission, sizeof(emission), va(", \"Anisotropy\": %g", v->anisotropy));
		if ( v->flags & FOGVOLUME_EXTINCTION )
		{
			Q_strcat(emission, sizeof(emission), va(", \"Extinction\": [%g, %g, %g]",
				v->extinctionColor[0], v->extinctionColor[1], v->extinctionColor[2]));
		}
		const char *size = (v->shape != FOGVOLUME_BOX && v->extents[0] == v->extents[1] && v->extents[1] == v->extents[2]) ?
			va("\"Radius\": %g", v->extents[0]) :
			va("\"Size\": [%g, %g, %g]", v->extents[0], v->extents[1], v->extents[2]);
		ri.Printf(PRINT_ALL,
			"  { \"Shape\": \"%s\", \"Origin\": [%.1f, %.1f, %.1f], %s, \"Angles\": [%g, %g, %g], "
			"\"Opaque\": %g, \"Color\": [%g, %g, %g], \"Softness\": %g, \"Noise\": %d%s }%s\n",
			R_FogVolumeShapeName(v->shape, v->extents), v->origin[0], v->origin[1], v->origin[2], size,
			d->angles[0], d->angles[1], d->angles[2], v->depthForOpaque, v->color[0], v->color[1], v->color[2],
			v->softness, (v->flags & FOGVOLUME_NOISE) ? 1 : 0, emission, (i + 1 < s_fv.numDebug) ? "," : "");
	}
	ri.Printf(PRINT_ALL, "]\n");
}

// the first solid surface along the view axis, qfalse without one
static qboolean R_FogVolumeTrace( float *distance )
{
	vec3_t end;
	const float maxDistance = 8192.0f;
	VectorMA(s_fv.cameraOrigin, maxDistance, s_fv.cameraAxis[0], end);

	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, s_fv.cameraOrigin, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID,
		G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, s_fv.cameraOrigin, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	if ( trace.allsolid || trace.fraction >= 1.0f )
		return qfalse;
	*distance = trace.fraction * maxDistance;
	return qtrue;
}

static qboolean R_FogVolumeCheat( const char *what )
{
	if ( ri.Cvar_VariableIntegerValue("sv_cheats") )
		return qtrue;
	ri.Printf(PRINT_ALL, "r_fogvol %s is cheat protected (sv_cheats 1).\n", what);
	return qfalse;
}

static void R_FogVolumeAdd( void )
{
	if ( !R_FogVolumeCheat("add") )
		return;
	if ( !s_fv.hasCamera || !tr.world )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol add: no world view yet (load a map)\n");
		return;
	}
	if ( s_fv.numDebug >= MAX_DEBUG_FOG_VOLUMES )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol add: %d volumes already (r_fogvol remove / clear)\n", MAX_DEBUG_FOG_VOLUMES);
		return;
	}

	debugFogVolume_t d = {};
	refFogVolume_t *v = &d.volume;
	v->shape = FOGVOLUME_ELLIPSOID;
	VectorSet(v->extents, 128.0f, 128.0f, 128.0f);
	v->depthForOpaque = FOGVOLUME_DEFAULT_OPAQUE;
	VectorCopy(fogVolumeDefaultColor, v->color);
	v->softness = FOGVOLUME_DEFAULT_SOFTNESS;

	enum { PLACE_TRACE, PLACE_VIEW, PLACE_EYE, PLACE_POINT } place = PLACE_TRACE;
	vec3_t point = { 0.0f, 0.0f, 0.0f };

	const int argc = ri.Cmd_Argc();
	int i = 2;
	if ( i < argc )
	{
		const char *shape = ri.Cmd_Argv(i);
		if ( !Q_stricmp(shape, "sphere") || !Q_stricmp(shape, "ellipsoid") )
			i++;
		else if ( !Q_stricmp(shape, "box") )
		{
			v->shape = FOGVOLUME_BOX;
			i++;
		}
	}

	while ( i < argc )
	{
		const char *key = ri.Cmd_Argv(i);
		float values[4];
		if ( !Q_stricmp(key, "radius") && R_FogVolumeParseNumbers(i + 1, 1, values) )
		{
			VectorSet(v->extents, values[0], values[0], values[0]);
			i += 2;
		}
		else if ( !Q_stricmp(key, "size") && R_FogVolumeParseNumbers(i + 1, 3, values) )
		{
			VectorCopy(values, v->extents);
			i += 4;
		}
		else if ( !Q_stricmp(key, "opaque") && R_FogVolumeParseNumbers(i + 1, 1, values) && values[0] >= 0.0f )
		{
			v->depthForOpaque = values[0];
			i += 2;
		}
		else if ( !Q_stricmp(key, "emit") && R_FogVolumeParseNumbers(i + 1, 3, values) )
		{
			VectorCopy(values, v->emissive);
			i += 4;
			if ( R_FogVolumeParseNumbers(i, 1, values) )
			{
				v->emissiveDensity = MAX(0.0f, values[0]);
				i++;
			}
		}
		else if ( !Q_stricmp(key, "color") && R_FogVolumeParseNumbers(i + 1, 3, values) )
		{
			VectorCopy(values, v->color);
			i += 4;
		}
		else if ( !Q_stricmp(key, "soft") && R_FogVolumeParseNumbers(i + 1, 1, values) )
		{
			v->softness = Com_Clamp(0.0f, 1.0f, values[0]);
			i += 2;
		}
		else if ( !Q_stricmp(key, "angles") && R_FogVolumeParseNumbers(i + 1, 3, values) )
		{
			VectorCopy(values, d.angles);
			i += 4;
		}
		else if ( !Q_stricmp(key, "noise") && R_FogVolumeParseNumbers(i + 1, 1, values) )
		{
			v->flags = (v->flags & ~FOGVOLUME_NOISE) | ((values[0] > 0.5f) ? FOGVOLUME_NOISE : 0);
			i += 2;
		}
		else if ( !Q_stricmp(key, "aniso") && R_FogVolumeParseNumbers(i + 1, 1, values) )
		{
			v->anisotropy = Com_Clamp(-0.9f, 0.9f, values[0]);
			v->flags |= FOGVOLUME_ANISOTROPY;
			i += 2;
		}
		else if ( !Q_stricmp(key, "extinction") && R_FogVolumeParseNumbers(i + 1, 3, values) )
		{
			VectorCopy(values, v->extinctionColor);
			v->flags |= FOGVOLUME_EXTINCTION;
			i += 4;
		}
		else if ( !Q_stricmp(key, "swing") && R_FogVolumeParseNumbers(i + 1, 4, values) && values[3] > 0.0f )
		{
			VectorCopy(values, d.swing);
			d.swingPeriod = values[3];
			i += 5;
		}
		else if ( !Q_stricmp(key, "at") && i + 1 < argc )
		{
			const char *where = ri.Cmd_Argv(i + 1);
			if ( !Q_stricmp(where, "trace") ) { place = PLACE_TRACE; i += 2; }
			else if ( !Q_stricmp(where, "view") ) { place = PLACE_VIEW; i += 2; }
			else if ( !Q_stricmp(where, "eye") ) { place = PLACE_EYE; i += 2; }
			else if ( R_FogVolumeParseNumbers(i + 1, 3, values) ) { place = PLACE_POINT; VectorCopy(values, point); i += 4; }
			else
			{
				ri.Printf(PRINT_WARNING, "r_fogvol add: at trace | view | eye | <x y z>\n");
				return;
			}
		}
		else
		{
			ri.Printf(PRINT_WARNING, "r_fogvol add: bad parameter '%s'\n", key);
			R_FogVolumeUsage();
			return;
		}
	}

	if ( v->depthForOpaque <= 0.0f && !R_FogVolumeEmitsAlone(v) )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol add: opaque 0 needs emit <r> <g> <b> <density>\n");
		return;
	}

	for ( int c = 0; c < 3; c++ )
		v->extents[c] = MAX(1.0f, v->extents[c]);
	AnglesToAxis(d.angles, v->axis);

	// the extent along the view axis, to keep the camera out of the volume
	const float reach = (v->shape == FOGVOLUME_BOX) ? VectorLength(v->extents) :
		MAX(v->extents[0], MAX(v->extents[1], v->extents[2]));
	const float *forward = s_fv.cameraAxis[0];
	float distance;
	switch ( place )
	{
	case PLACE_TRACE:
		if ( R_FogVolumeTrace(&distance) )
		{
			// pulled back from the wall by the radius, never behind the camera
			VectorMA(s_fv.cameraOrigin, MAX(0.0f, distance - reach), forward, v->origin);
			break;
		}
		ri.Printf(PRINT_ALL, "r_fogvol add: nothing under the crosshair, placed ahead\n");
		// fall through
	case PLACE_VIEW:
		VectorMA(s_fv.cameraOrigin, 1.5f * reach, forward, v->origin);
		break;
	case PLACE_EYE:
		VectorCopy(s_fv.cameraOrigin, v->origin);
		break;
	case PLACE_POINT:
		VectorCopy(point, v->origin);
		break;
	}

	v->id = FOGVOLUME_ID_DEBUG | (++s_fv.nextDebugId & 0x0FFFFFFF);
	s_fv.debug[s_fv.numDebug++] = d;
	R_FogVolumePrintVolume(va("%d:", s_fv.numDebug - 1), v);
	R_FogVolumeWarnings();
}

static void R_FogVolumeTest( void )
{
	if ( !R_FogVolumeCheat("test") )
		return;
	float values[2] = { 0.0f, 768.0f };
	if ( !R_FogVolumeParseNumbers(2, 1, values) )
	{
		ri.Printf(PRINT_ALL, "usage: r_fogvol test <count 1-%d> [spread radius]\n", MAX_DEBUG_FOG_VOLUMES);
		return;
	}
	R_FogVolumeParseNumbers(3, 1, &values[1]);
	if ( !s_fv.hasCamera || !tr.world )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol test: no world view yet (load a map)\n");
		return;
	}

	// deterministic spheres in front of the camera, like r_forwardPlusSpawnTestLights
	const int count = Com_Clampi(0, MAX_DEBUG_FOG_VOLUMES - s_fv.numDebug, (int)values[0]);
	const float spread = MAX(64.0f, values[1]);
	for ( int n = 0; n < count; n++ )
	{
		const int seed = s_fv.nextDebugId + 1;
		unsigned int h = (unsigned int)seed * 747796405u + 2891336453u;
		float r[5];
		for ( int k = 0; k < 5; k++ )
		{
			h = (h ^ (h >> 16)) * 0x45d9f3bu;
			h = (h ^ (h >> 16)) * 0x45d9f3bu;
			h ^= h >> 16;
			r[k] = (float)(h & 0xffff) / 65535.0f;
		}

		debugFogVolume_t d = {};
		refFogVolume_t *v = &d.volume;
		v->shape = FOGVOLUME_ELLIPSOID;
		const float radius = 48.0f + 112.0f * r[0];
		VectorSet(v->extents, radius, radius, radius);
		VectorMA(s_fv.cameraOrigin, spread * (0.2f + 0.8f * r[1]), s_fv.cameraAxis[0], v->origin);
		VectorMA(v->origin, spread * (r[2] - 0.5f), s_fv.cameraAxis[1], v->origin);
		VectorMA(v->origin, 0.3f * spread * (r[3] - 0.5f), s_fv.cameraAxis[2], v->origin);
		v->depthForOpaque = 300.0f + 900.0f * r[4];
		VectorCopy(fogVolumeDefaultColor, v->color);
		v->softness = 0.6f;
		AxisClear(v->axis);
		v->id = FOGVOLUME_ID_DEBUG | (++s_fv.nextDebugId & 0x0FFFFFFF);
		s_fv.debug[s_fv.numDebug++] = d;
	}
	ri.Printf(PRINT_ALL, "%d test volumes added, %d r_fogvol volumes\n", count, s_fv.numDebug);
	R_FogVolumeWarnings();
}

// emission test without assets: A a pure glow (no extinction, the small extinction
// limit of the integration), B dense glowing smoke (emission coupled to the
// extinction, self absorbed: its core shows about the emissive radiance)
static void R_FogVolumeEmitTest( void )
{
	if ( !R_FogVolumeCheat("emittest") )
		return;
	if ( !s_fv.hasCamera || !tr.world )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol emittest: no world view yet (load a map)\n");
		return;
	}
	if ( s_fv.numDebug + 2 > MAX_DEBUG_FOG_VOLUMES )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol emittest: no room (r_fogvol clear)\n");
		return;
	}

	for ( int n = 0; n < 2; n++ )
	{
		debugFogVolume_t d = {};
		refFogVolume_t *v = &d.volume;
		v->shape = FOGVOLUME_ELLIPSOID;
		VectorSet(v->extents, 96.0f, 96.0f, 96.0f);
		VectorMA(s_fv.cameraOrigin, 400.0f, s_fv.cameraAxis[0], v->origin);
		VectorMA(v->origin, (n == 0) ? 130.0f : -130.0f, s_fv.cameraAxis[1], v->origin);
		VectorCopy(fogVolumeDefaultColor, v->color);
		v->softness = 0.6f;
		AxisClear(v->axis);
		if ( n == 0 )
		{
			// A: no extinction, 192 units through the center: about 0.01 * 192 = 1.9 x (0.5 0.8 2.0)
			v->depthForOpaque = 0.0f;
			VectorSet(v->emissive, 0.5f, 0.8f, 2.0f);
			v->emissiveDensity = 0.01f;
		}
		else
		{
			// B: opaque over 150 units, dark smoke glowing orange, emission = extinction * emissive
			v->depthForOpaque = 150.0f;
			VectorSet(v->color, 0.2f, 0.2f, 0.2f);
			VectorSet(v->emissive, 4.0f, 1.6f, 0.4f);
			v->emissiveDensity = 0.0f;
		}
		v->id = FOGVOLUME_ID_DEBUG | (++s_fv.nextDebugId & 0x0FFFFFFF);
		s_fv.debug[s_fv.numDebug++] = d;
		R_FogVolumePrintVolume(va("%d:", s_fv.numDebug - 1), v);
	}
	R_FogVolumeWarnings();
}

// per-medium phase test without assets (r_volumetricFogDebug 35-39): A forward
// scattering (g +0.8) and B back scattering (g -0.8) white spheres overlapping in
// the middle (the two lobe mixture: bright towards and away from the sun there),
// C an isotropic (g 0) colored absorptive medium (albedo 1 0.2 0.2: green and blue
// are absorbed, not scattered) beside them
static void R_FogVolumeMediumTest( void )
{
	if ( !R_FogVolumeCheat("mediumtest") )
		return;
	if ( !s_fv.hasCamera || !tr.world )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol mediumtest: no world view yet (load a map)\n");
		return;
	}
	if ( s_fv.numDebug + 3 > MAX_DEBUG_FOG_VOLUMES )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol mediumtest: no room (r_fogvol clear)\n");
		return;
	}

	static const float offsets[3] = { 70.0f, -70.0f, -300.0f };
	static const float anisotropy[3] = { 0.8f, -0.8f, 0.0f };
	for ( int n = 0; n < 3; n++ )
	{
		debugFogVolume_t d = {};
		refFogVolume_t *v = &d.volume;
		v->shape = FOGVOLUME_ELLIPSOID;
		VectorSet(v->extents, 110.0f, 110.0f, 110.0f);
		VectorMA(s_fv.cameraOrigin, 450.0f, s_fv.cameraAxis[0], v->origin);
		VectorMA(v->origin, offsets[n], s_fv.cameraAxis[1], v->origin);
		v->depthForOpaque = 400.0f;
		if ( n == 2 )
			VectorSet(v->color, 1.0f, 0.2f, 0.2f);
		else
			VectorSet(v->color, 0.9f, 0.9f, 0.9f);
		v->softness = 0.5f;
		v->anisotropy = anisotropy[n];
		v->flags = FOGVOLUME_ANISOTROPY;
		AxisClear(v->axis);
		v->id = FOGVOLUME_ID_DEBUG | (++s_fv.nextDebugId & 0x0FFFFFFF);
		s_fv.debug[s_fv.numDebug++] = d;
		R_FogVolumePrintVolume(va("%d:", s_fv.numDebug - 1), v);
	}
	R_FogVolumeWarnings();
}

// RGB extinction test without assets (r_volumetricFogRGBExtinction, r_volumetricFogDebug
// 51-56): four white scattering spheres in a row across the view, the same mean
// opacity: neutral (1 1 1), red absorbing (3 0.5 0.5: cyan), blue absorbing
// (0.5 0.5 3: yellow) and mixed (1 2 3: orange red). Put a white wall behind
// them; the two middle ones overlap slightly (overlapping media).
static void R_FogVolumeRGBTest( void )
{
	if ( !R_FogVolumeCheat("rgbtest") )
		return;
	if ( !s_fv.hasCamera || !tr.world )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol rgbtest: no world view yet (load a map)\n");
		return;
	}
	if ( s_fv.numDebug + 4 > MAX_DEBUG_FOG_VOLUMES )
	{
		ri.Printf(PRINT_WARNING, "r_fogvol rgbtest: no room (r_fogvol clear)\n");
		return;
	}

	static const float offsets[4] = { 270.0f, 90.0f, -70.0f, -250.0f };
	static const vec3_t extinction[4] = {
		{ 1.0f, 1.0f, 1.0f }, { 3.0f, 0.5f, 0.5f }, { 0.5f, 0.5f, 3.0f }, { 1.0f, 2.0f, 3.0f } };
	for ( int n = 0; n < 4; n++ )
	{
		debugFogVolume_t d = {};
		refFogVolume_t *v = &d.volume;
		v->shape = FOGVOLUME_ELLIPSOID;
		VectorSet(v->extents, 90.0f, 90.0f, 90.0f);
		VectorMA(s_fv.cameraOrigin, 450.0f, s_fv.cameraAxis[0], v->origin);
		VectorMA(v->origin, offsets[n], s_fv.cameraAxis[1], v->origin);
		v->depthForOpaque = 250.0f;
		VectorSet(v->color, 0.9f, 0.9f, 0.9f);
		v->softness = 0.4f;
		VectorCopy(extinction[n], v->extinctionColor);
		v->flags = (n > 0) ? FOGVOLUME_EXTINCTION : 0;
		AxisClear(v->axis);
		v->id = FOGVOLUME_ID_DEBUG | (++s_fv.nextDebugId & 0x0FFFFFFF);
		s_fv.debug[s_fv.numDebug++] = d;
		R_FogVolumePrintVolume(va("%d:", s_fv.numDebug - 1), v);
	}
	R_FogVolumeWarnings();
	if ( !R_VolumetricFroxelRGB() )
		ri.Printf(PRINT_WARNING, "r_fogvol rgbtest: r_volumetricFogRGBExtinction is off (set it to 1, then vid_restart): the spheres look alike\n");
}

/*
=================
R_FogVolume_f

r_fogvol: renderer side local fog volumes near the camera, so the feature can
be tested without any asset (see R_FogVolumeUsage)
=================
*/
void R_FogVolume_f( void )
{
	R_FogVolumesCheckWorld();

	if ( ri.Cmd_Argc() < 2 )
	{
		R_FogVolumeList();
		ri.Printf(PRINT_ALL, "(r_fogvol help for the commands)\n");
		return;
	}

	const char *cmd = ri.Cmd_Argv(1);
	if ( !Q_stricmp(cmd, "help") || !Q_stricmp(cmd, "?") )
		R_FogVolumeUsage();
	else if ( !Q_stricmp(cmd, "list") )
		R_FogVolumeList();
	else if ( !Q_stricmp(cmd, "add") )
		R_FogVolumeAdd();
	else if ( !Q_stricmp(cmd, "test") )
		R_FogVolumeTest();
	else if ( !Q_stricmp(cmd, "emittest") )
		R_FogVolumeEmitTest();
	else if ( !Q_stricmp(cmd, "mediumtest") )
		R_FogVolumeMediumTest();
	else if ( !Q_stricmp(cmd, "rgbtest") )
		R_FogVolumeRGBTest();
	else if ( !Q_stricmp(cmd, "slices") )
		R_FogVolumeSlices();
	else if ( !Q_stricmp(cmd, "dump") )
		R_FogVolumeDump();
	else if ( !Q_stricmp(cmd, "clear") )
	{
		s_fv.numDebug = 0;
		ri.Printf(PRINT_ALL, "r_fogvol volumes cleared\n");
	}
	else if ( !Q_stricmp(cmd, "remove") )
	{
		float index;
		if ( ri.Cmd_Argc() < 3 || !R_FogVolumeParseNumber(ri.Cmd_Argv(2), &index) ||
			(int)index < 0 || (int)index >= s_fv.numDebug )
		{
			ri.Printf(PRINT_ALL, "usage: r_fogvol remove <index 0-%d>\n", s_fv.numDebug - 1);
			return;
		}
		for ( int i = (int)index; i + 1 < s_fv.numDebug; i++ )
			s_fv.debug[i] = s_fv.debug[i + 1];
		s_fv.numDebug--;
		ri.Printf(PRINT_ALL, "r_fogvol volume %d removed, %d left\n", (int)index, s_fv.numDebug);
	}
	else
		R_FogVolumeUsage();
}
