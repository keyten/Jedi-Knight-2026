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

// Volumetric FX particles of the froxel fog (r_volumetricFog 2, r_volumetricParticles),
// see docs/rend2-volumetric-fog.md, "FX particle media".
//
// FX particles whose .efx primitive has a "volumetricMedia" block (smoke,
// steam, gas) submit a soft ellipsoid proxy around the particle every frame
// through the optional renderer extension GetRefVolParticleAPI
// (RE_AddVolumetricParticleToScene, tr_scene.cpp). They are one more medium
// of FroxelMedium (volumetric_inject.glsl): every light of the froxel fog
// applies unchanged. Nothing here guesses smoke from a shader name, and
// RF_VOLUMETRIC (legacy fake volumetric shading of models) is not involved.
//
// Culling (once per frame, for the froxel view): the bounding sphere against
// the view frustum up to the local fade distance, then the most important
// (projected optical footprint extinction * r^2 / depth^2, ties by id, so the
// choice is deterministic) MAX_GPU_VOL_PARTICLES / r_volumetricParticlesMax are
// uploaded, and per depth slice a packed list (16 bit indices in a pool of
// VOL_PARTICLE_POOL) of the particles whose sphere overlaps the slice. When
// the pool is full the far slices lose their entries first.
//
// Temporal: each particle is paired with its previous frame state by id.
// Moved / grown / faded particles upload their previous shape; the injection
// lowers the history weight where the particle density changed (down to the
// floor r_volumetricParticlesHistory), so moving smoke leaves no long ghost and a
// slowly drifting puff does not flicker. Vanished particles get one more
// frame with no density, which clears the history of their last place.

#include "tr_local.h"

#include <algorithm>
#include <chrono>
#include "fx/FxPhysicalizationAggregate.h"

extern int r_volumetricParticlesRejected;	// tr_scene.cpp

// narrowest soft edge in world units (thinner than a froxel it would alias)
#define VOLPARTICLE_MIN_FADE		4.0f
#define VOLPARTICLE_MIN_SOFTNESS	0.1f
#define VOLPARTICLE_MIN_ASPECT		0.25f
#define VOLPARTICLE_MAX_ASPECT		4.0f

// particles fade out over the last part of the slices, as the local fog volumes
#define VOLPARTICLE_FADE_START		0.8f

// what counts as a change for the temporal filter
#define VOLPARTICLE_MOVE_EPSILON	0.05f		// world units
#define VOLPARTICLE_DENSITY_EPSILON	0.01f		// relative

// a particle converted for the GPU
struct volParticleEval_t
{
	int id;
	bool automaticDensity, automaticGlow;
	vec3_t center;
	vec3_t invExtent;			// 1 / (radius * aspect)
	float radius;				// bounding sphere
	float extinction;			// per world unit at the center
	float inner;				// 1 - softness
	vec3_t albedo;
	vec3_t emission;			// emission per world unit at the center (0: no glow)
	float anisotropy;			// Henyey-Greenstein g (own or r_volumetricFogAnisotropy)
};

// a candidate of this frame
struct volParticleCandidate_t
{
	volParticleEval_t current;			// extinction 0 when it vanished
	const volParticleEval_t *previous;	// NULL: new this frame
	qboolean changed;
	vec3_t center;						// bounding sphere, current and previous state
	float radius;
	float depth;						// view depth of the sphere center
	float importance;
};

static struct
{
	// the particles of the last froxel frame, sorted by id (temporal pairing)
	volParticleEval_t previous[MAX_REF_VOL_PARTICLES];
	int numPrevious;
	int previousFrame;
	const world_t *previousWorld;

	// automatic glows holding an emission slot last frame (hysteresis)
	int autoGlowIds[4];
	int numAutoGlowIds;

	// statistics of the last froxel frame (r_volparticles, r_volumetricParticlesDebug)
	int statFrame;
	int statSubmitted;
	int statRejected;			// invalid or over MAX_REF_VOL_PARTICLES (RE_AddVolumetricParticleToScene)
	int statCulled;				// outside the frustum or beyond the fade
	int statCapped;				// in view, not among the uploaded
	int statUploaded;
	int statCoalesced;
	int statChanged;
	int statVanished;
	int statPoolUsed;
	int statPoolDropped;
	int statEmissive;			// uploaded with an emission slot
	int statEmissiveDropped;	// uploaded, emissive, no slot left (medium kept, no glow)
	int statMaxPerSlice;
	int statBuildMicroseconds;
	int statLastPrint;
} s_vp;

/*
============================================================

Conversion

============================================================
*/

static qboolean R_VolParticleEvaluate( const refVolParticle_t *particle, volParticleEval_t *out )
{
	Com_Memset(out, 0, sizeof(*out));
	if ( !(particle->radius > 0.0f) || !R_VolParticleHasMedium(particle) )
		return qfalse;

	out->id = particle->id;
	out->automaticDensity = (particle->flags & VOLPARTICLE_AUTODENSITY) != 0;
	out->automaticGlow = (particle->flags & VOLPARTICLE_AUTOGLOW) != 0;
	VectorCopy(particle->origin, out->center);

	float minExtent = 1e30f;
	float maxExtent = 0.0f;
	for ( int i = 0; i < 3; i++ )
	{
		float aspect = (particle->aspect[i] > 0.0f) ? particle->aspect[i] : 1.0f;
		aspect = Com_Clamp(VOLPARTICLE_MIN_ASPECT, VOLPARTICLE_MAX_ASPECT, aspect);
		const float extent = MAX(1.0f, particle->radius * aspect);
		out->invExtent[i] = 1.0f / extent;
		minExtent = MIN(minExtent, extent);
		maxExtent = MAX(maxExtent, extent);
	}
	out->radius = maxExtent;

	// emission (scene linear radiance per unit, the fades applied by the FX code): not scaled by
	// r_volumetricParticlesScale, a density scale, but by r_volumetricEmission in the injection
	qboolean emits = qfalse;
	for ( int c = 0; c < 3; c++ )
	{
		const float e = particle->emission[c];
		out->emission[c] = (e > 0.0f && e < 1e6f) ? e : 0.0f;	// NaN, Inf -> 0
		if ( out->emission[c] > 0.0f )
			emits = qtrue;
	}

	out->extinction = particle->extinction * r_volumetricParticlesScale->value;
	if ( Q_isnan(out->extinction) || !(out->extinction >= 0.0f) )
		out->extinction = 0.0f;
	if ( !(out->extinction > 0.0f) && !emits )
		return qfalse;

	float softness = Com_Clamp(VOLPARTICLE_MIN_SOFTNESS, 1.0f, particle->softness);
	softness = MIN(1.0f, MAX(softness, VOLPARTICLE_MIN_FADE / minExtent));
	out->inner = 1.0f - softness;

	// phase: its own g (VOLPARTICLE_ANISOTROPY), else the global one as the other media
	out->anisotropy = Com_Clamp(-0.9f, 0.9f, (particle->flags & VOLPARTICLE_ANISOTROPY) ?
		particle->anisotropy : r_volumetricFogAnisotropy->value);
	if ( Q_isnan(out->anisotropy) )
		out->anisotropy = 0.0f;

	// albedo in the fogParms convention, as the local fog volumes
	for ( int c = 0; c < 3; c++ )
	{
		float albedo = Com_Clamp(0.0f, 1.0f, particle->color[c]);
		if ( tr.linearLight )
			albedo = (float)sRGBtoRGB(albedo);
		out->albedo[c] = albedo * tr.identityLight;
	}
	return qtrue;
}

// invExtent.w of the GPU block: the inner shell (8 bit) and the anisotropy g in one
// float (the block has no room for another array, see VolumetricParticlesBlock):
// w = round(inner * 255) + 0.001 + 0.998 * (g + 1) / 2, decoded by
// FroxelParticleInner / FroxelParticleAnisotropy (volumetric_common.glsl)
static float R_VolParticlePackInner( float inner, float anisotropy )
{
	const float q = floorf(Com_Clamp(0.0f, 1.0f, inner) * 255.0f + 0.5f);
	return q + 0.001f + 0.998f * 0.5f * (Com_Clamp(-1.0f, 1.0f, anisotropy) + 1.0f);
}

static qboolean R_VolParticleChanged( const volParticleEval_t *a, const volParticleEval_t *b )
{
	for ( int i = 0; i < 3; i++ )
	{
		if ( fabsf(a->center[i] - b->center[i]) > VOLPARTICLE_MOVE_EPSILON )
			return qtrue;
		// compared as extents
		if ( fabsf(1.0f / a->invExtent[i] - 1.0f / b->invExtent[i]) > VOLPARTICLE_MOVE_EPSILON )
			return qtrue;
	}
	if ( fabsf(a->extinction - b->extinction) > VOLPARTICLE_DENSITY_EPSILON * MAX(a->extinction, b->extinction) )
		return qtrue;
	if (a->automaticGlow || b->automaticGlow) {
		for (int i = 0; i < 3; ++i)
			if (fabsf(a->emission[i] - b->emission[i]) > VOLPARTICLE_DENSITY_EPSILON * MAX(a->emission[i], b->emission[i])) return qtrue;
	}
	if ( fabsf(a->inner - b->inner) > 0.005f )
		return qtrue;
	return qfalse;
}

// smallest sphere around two spheres
static void R_VolParticleSphereUnion( const vec3_t c0, float r0, const vec3_t c1, float r1, vec3_t c, float *r )
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

static const volParticleEval_t *R_VolParticleFindPrevious( int id, qboolean *matched )
{
	int lo = 0, hi = s_vp.numPrevious - 1;
	while ( lo <= hi )
	{
		const int mid = (lo + hi) >> 1;
		const int midId = s_vp.previous[mid].id;
		if ( midId < id )
			lo = mid + 1;
		else if ( midId > id )
			hi = mid - 1;
		else
		{
			// duplicate ids: the first free one
			int i = mid;
			while ( i > 0 && s_vp.previous[i - 1].id == id )
				i--;
			for ( ; i < s_vp.numPrevious && s_vp.previous[i].id == id; i++ )
			{
				if ( !matched[i] )
				{
					matched[i] = qtrue;
					return &s_vp.previous[i];
				}
			}
			return NULL;
		}
	}
	return NULL;
}

/*
============================================================

Frame build (RB_UpdateVolumetricConstants)

============================================================
*/

// view distance of the near side of slice k, as R_VolumetricSliceDistance
static float R_VolParticleSliceDistance( int k, float nearZ, float farZ, int numSlices )
{
	if ( k <= 0 )
		return 0.0f;
	return nearZ * powf(farZ / nearZ, (float)k / (float)numSlices);
}

// upload priority: authored media, automatic density, automatic glows
static int R_VolParticleRank( const volParticleEval_t& e )
{
	return e.automaticGlow ? 2 : e.automaticDensity ? 1 : 0;
}

static qboolean R_VolParticlesPreviousValid( void )
{
	return (qboolean)(s_vp.previousWorld == tr.world && s_vp.previousFrame + 1 == backEndData->realFrameNumber);
}

/*
=================
R_VolParticlesInFrustum

Does the froxel view need a volume for the FX particles: one of this scene,
or one of the previous frame that vanished (its history is dropped).
=================
*/
qboolean R_VolParticlesInFrustum( const viewParms_t *view, const trRefdef_t *refdef, float farZ )
{
	if ( !view || !tr.world || !r_volumetricParticles->integer )
		return qfalse;

	vec3_t forward;
	VectorCopy(view->ori.axis[0], forward);
	VectorNormalize(forward);
	float depth;

	for ( int i = 0; i < refdef->num_volParticles; i++ )
	{
		const refVolParticle_t *particle = &refdef->volParticles[i];
		const float aspect = MAX(1.0f, MAX(particle->aspect[0], MAX(particle->aspect[1], particle->aspect[2])));
		if ( R_FogVolumeSphereInFrustum(view, forward, particle->origin,
				MAX(1.0f, particle->radius * MIN(aspect, VOLPARTICLE_MAX_ASPECT)), farZ, &depth) )
			return qtrue;
	}

	if ( R_VolParticlesPreviousValid() )
	{
		for ( int i = 0; i < s_vp.numPrevious; i++ )
		{
			const volParticleEval_t *previous = &s_vp.previous[i];
			if ( R_FogVolumeSphereInFrustum(view, forward, previous->center, previous->radius, farZ, &depth) )
				return qtrue;
		}
	}

	return qfalse;
}

static void R_VolParticlesPrintStats( const char *prefix )
{
	ri.Printf(PRINT_ALL, "%svolumetric FX particles (frame %d): %d submitted, %d rejected, %d culled, "
		"%d capped, %d uploaded (%d changed, %d vanished, %d/%d emissive, %d glows dropped), pool %d/%d "
		"(%d dropped, max %d per slice), build %d us\n",
		prefix, s_vp.statFrame, s_vp.statSubmitted, s_vp.statRejected, s_vp.statCulled, s_vp.statCapped,
		s_vp.statUploaded, s_vp.statChanged, s_vp.statVanished, s_vp.statEmissive, MAX_GPU_EMISSIVE_PARTICLES,
		s_vp.statEmissiveDropped, s_vp.statPoolUsed, VOL_PARTICLE_POOL,
		s_vp.statPoolDropped, s_vp.statMaxPerSlice, s_vp.statBuildMicroseconds);
	ri.Printf(PRINT_ALL, "automatic exact coalescing: %d inputs folded (fx_physicalizationAggregate %d)\n", s_vp.statCoalesced, fx_physicalizationAggregate->integer);
}

/*
=================
R_VolParticlesBuild

Converts, culls, ranks and packs the FX particle media of the froxel view
into the VolumetricParticles block. Called once per frame by
RB_UpdateVolumetricConstants for the scene that builds the froxel volume.
Returns the number of uploaded particles.
=================
*/
int R_VolParticlesBuild( VolumetricParticlesBlock *block, const viewParms_t *view, const trRefdef_t *refdef,
	const vec3_t forward, float nearZ, float farZ, int numSlices )
{
	static volParticleCandidate_t candidates[MAX_REF_VOL_PARTICLES * 2];
	static volParticleEval_t current[MAX_REF_VOL_PARTICLES];
	static qboolean previousMatched[MAX_REF_VOL_PARTICLES];
	static int order[MAX_REF_VOL_PARTICLES * 2];

	const auto startTime = std::chrono::steady_clock::now();

	Com_Memset(block, 0, sizeof(*block));

	if ( !R_VolParticlesPreviousValid() )
	{
		s_vp.numPrevious = 0;
		s_vp.numAutoGlowIds = 0;
	}

	s_vp.statFrame = backEndData->realFrameNumber;
	s_vp.statSubmitted = r_volumetricParticles->integer ? refdef->num_volParticles : 0;
	s_vp.statRejected = r_volumetricParticlesRejected;
	s_vp.statCulled = 0;
	s_vp.statCapped = 0;
	s_vp.statUploaded = 0;
	s_vp.statCoalesced = 0;
	s_vp.statChanged = 0;
	s_vp.statVanished = 0;
	s_vp.statPoolUsed = 0;
	s_vp.statPoolDropped = 0;
	s_vp.statEmissive = 0;
	s_vp.statEmissiveDropped = 0;
	s_vp.statMaxPerSlice = 0;

	numSlices = Com_Clampi(1, FROXEL_MAX_SLICES, numSlices);
	const float fadeEnd = R_VolParticleSliceDistance(numSlices - 1, nearZ, farZ, numSlices);
	const float fadeStart = VOLPARTICLE_FADE_START * fadeEnd;

	// Convert actual accepted live records. Exact coalescing has one density
	// owner and unchanged support; original FX objects/sprites remain intact.
	Com_Memset(previousMatched, 0, s_vp.numPrevious * sizeof(previousMatched[0]));
	int numCurrent = 0;
	int numCandidates = 0;
	const bool aggregate = fx_physicalizationAggregate->integer == 1;
	const auto pair = [&](volParticleEval_t* e) {
		volParticleCandidate_t *c = &candidates[numCandidates++];
		c->current = *e;
		c->previous = R_VolParticleFindPrevious(e->id, previousMatched);
		c->changed = c->previous ? R_VolParticleChanged(e, c->previous) : qtrue;
		VectorCopy(e->center, c->center);
		c->radius = e->radius;
		if (c->changed && c->previous)
			R_VolParticleSphereUnion(e->center, e->radius, c->previous->center, c->previous->radius, c->center, &c->radius);
	};
	const int numSubmitted = r_volumetricParticles->integer ? refdef->num_volParticles : 0;
	for (int i = 0; i < numSubmitted && numCurrent < MAX_REF_VOL_PARTICLES; ++i) {
		volParticleEval_t* e = &current[numCurrent];
		if (R_VolParticleEvaluate(&refdef->volParticles[i], e)) {
			++numCurrent;
			if (!aggregate) pair(e);
		} else ++s_vp.statRejected;
	}
	if (aggregate) {
		const int before = numCurrent;
		numCurrent = FxPhysical::CoalesceExact(current, numCurrent);
		s_vp.statCoalesced = before - numCurrent;
		for (int i = 0; i < numCurrent; ++i) pair(&current[i]);
	}

	// vanished since the previous frame: one more frame with no density, so
	// the history of their old place is dropped
	for ( int j = 0; j < s_vp.numPrevious && numCandidates < MAX_REF_VOL_PARTICLES * 2; j++ )
	{
		if ( previousMatched[j] )
			continue;

		volParticleCandidate_t *c = &candidates[numCandidates++];
		c->current = s_vp.previous[j];
		c->current.extinction = 0.0f;
		VectorClear(c->current.emission);	// no glow after it vanished (no after-image)
		c->previous = &s_vp.previous[j];
		c->changed = qtrue;
		VectorCopy(c->previous->center, c->center);
		c->radius = c->previous->radius;
		s_vp.statVanished++;
	}

	// frustum up to the fade, then the most important first
	int numVisible = 0;
	for ( int i = 0; i < numCandidates; i++ )
	{
		volParticleCandidate_t *c = &candidates[i];
		if ( !R_FogVolumeSphereInFrustum(view, forward, c->center, c->radius, fadeEnd, &c->depth) )
		{
			s_vp.statCulled++;
			continue;
		}
		// a glow ranks like a medium whose extinction is its luminance per unit
		float extinction = MAX(MAX(c->current.extinction, c->previous ? c->previous->extinction : 0.0f),
			0.2126f * c->current.emission[0] + 0.7152f * c->current.emission[1] + 0.0722f * c->current.emission[2]);
		if (c->current.automaticGlow && c->previous)
			extinction = MAX(extinction, 0.2126f*c->previous->emission[0] + 0.7152f*c->previous->emission[1] + 0.0722f*c->previous->emission[2]);
		const float distance = MAX(c->depth, nearZ);
		c->importance = extinction * c->radius * c->radius / (distance * distance);
		order[numVisible++] = i;
	}
	std::sort(order, order + numVisible, [&]( int a, int b ) {
		const volParticleCandidate_t *ca = &candidates[a];
		const volParticleCandidate_t *cb = &candidates[b];
		// Automatic media never displace authored media: authored, then
		// automatic density, then automatic glows.
		const int ra = R_VolParticleRank(ca->current), rb = R_VolParticleRank(cb->current);
		if (ra != rb) return ra < rb;
		if ( ca->importance != cb->importance )
			return ca->importance > cb->importance;
		if ( ca->current.id != cb->current.id )
			return ca->current.id < cb->current.id;
		// a vanished particle and a new one with the same id: the live one first
		return ca->current.extinction > cb->current.extinction;
	});

	const int maxUploaded = Com_Clampi(0, MAX_GPU_VOL_PARTICLES, r_volumetricParticlesMax->integer);

	// Automatic glows rank last. They are uploaded only while they hold one of
	// at most four spare emission slots (an empty record only costs slots and
	// slice entries), plus one frame after losing it so its history is dropped.
	// Last frame's holders keep their slots first: no flicker between fires.
	int firstGlow = numVisible;
	for (int n = 0; n < numVisible; ++n)
		if (candidates[order[n]].current.automaticGlow) { firstGlow = n; break; }
	int numUploaded = MIN(firstGlow, maxUploaded);
	s_vp.statCapped = firstGlow - numUploaded;
	int authoredEmitters = 0;
	for (int n = 0; n < numUploaded; ++n)
		if (!VectorCompare(candidates[order[n]].current.emission, vec3_origin)) ++authoredEmitters;
	const auto heldGlow = [&](int id) {
		for (int i = 0; i < s_vp.numAutoGlowIds; ++i) if (s_vp.autoGlowIds[i] == id) return true;
		return false;
	};
	int autoGlowIds[4], numAutoGlowIds = 0;
	const int autoSlots = MIN(4, MAX(0, MAX_GPU_EMISSIVE_PARTICLES - authoredEmitters));
	for (int pass = 0; pass < 2; ++pass) {
		for (int n = firstGlow; n < numVisible && numAutoGlowIds < autoSlots && numUploaded + numAutoGlowIds < maxUploaded; ++n) {
			const volParticleEval_t& e = candidates[order[n]].current;
			if (VectorCompare(e.emission, vec3_origin) || heldGlow(e.id) != (pass == 0)) continue;
			bool taken = false;
			for (int i = 0; i < numAutoGlowIds; ++i) taken = taken || autoGlowIds[i] == e.id;
			if (!taken) autoGlowIds[numAutoGlowIds++] = e.id;
		}
	}
	const auto slotted = [&](int id) {
		for (int i = 0; i < numAutoGlowIds; ++i) if (autoGlowIds[i] == id) return true;
		return false;
	};
	for (int n = firstGlow; n < numVisible; ++n) {
		volParticleCandidate_t *c = &candidates[order[n]];
		const bool slot = !VectorCompare(c->current.emission, vec3_origin) && slotted(c->current.id);
		const bool held = heldGlow(c->current.id);
		if (!slot && !held) { if (!VectorCompare(c->current.emission, vec3_origin)) ++s_vp.statEmissiveDropped; continue; }
		if (numUploaded >= maxUploaded) { ++s_vp.statCapped; continue; }
		if (!slot) VectorClear(c->current.emission);	// lost its slot: clear its glow once
		if (slot != held) c->changed = qtrue;
		order[numUploaded++] = order[n];
	}
	Com_Memcpy(s_vp.autoGlowIds, autoGlowIds, sizeof(autoGlowIds));
	s_vp.numAutoGlowIds = numAutoGlowIds;
	s_vp.statUploaded = numUploaded;

	// particle data
	for ( int n = 0; n < numUploaded; n++ )
	{
		const volParticleCandidate_t *c = &candidates[order[n]];
		const volParticleEval_t *e = &c->current;
		const volParticleEval_t *p = c->previous ? c->previous : e;

		VectorSet4(block->center[n], e->center[0], e->center[1], e->center[2], e->extinction);
		VectorSet4(block->invExtent[n], e->invExtent[0], e->invExtent[1], e->invExtent[2],
			R_VolParticlePackInner(e->inner, e->anisotropy));
		VectorSet4(block->color[n], e->albedo[0], e->albedo[1], e->albedo[2],
			c->previous ? c->previous->extinction : 0.0f);
		// w: changed (0/1) + 2 * (emission slot + 1), the slots to the most important emitters
		float slotCode = 0.0f;
		if ( !VectorCompare(e->emission, vec3_origin) ) {
			if (s_vp.statEmissive < MAX_GPU_EMISSIVE_PARTICLES) {
				const int slot = s_vp.statEmissive++;
				VectorSet4(block->emission[slot], e->emission[0], e->emission[1], e->emission[2], 0.0f);
				slotCode = 2.0f * float(slot + 1);
			} else ++s_vp.statEmissiveDropped;
		}

		VectorSet4(block->prevCenter[n], p->center[0], p->center[1], p->center[2],
			(c->changed ? 1.0f : 0.0f) + slotCode);
		VectorSet4(block->prevInvExtent[n], p->invExtent[0], p->invExtent[1], p->invExtent[2],
			R_VolParticlePackInner(p->inner, e->anisotropy));

		if ( c->changed )
			s_vp.statChanged++;
	}

	// per slice lists, near to far (a full pool drops the far slices); the
	// last slice has none (see the fade)
	int poolUsed = 0;
	for ( int k = 0; k < numSlices - 1; k++ )
	{
		const float sliceNear = R_VolParticleSliceDistance(k, nearZ, farZ, numSlices);
		const float sliceFar = R_VolParticleSliceDistance(k + 1, nearZ, farZ, numSlices);
		const int first = poolUsed;
		int count = 0;

		for ( int n = 0; n < numUploaded; n++ )
		{
			const volParticleCandidate_t *c = &candidates[order[n]];
			if ( c->depth + c->radius < sliceNear || c->depth - c->radius > sliceFar )
				continue;
			if ( poolUsed >= VOL_PARTICLE_POOL )
			{
				s_vp.statPoolDropped++;
				continue;
			}

			block->index[poolUsed >> 1] |= n << ((poolUsed & 1) * 16);
			poolUsed++;
			count++;
		}

		block->slices[k] = first | (count << 16);
		s_vp.statMaxPerSlice = MAX(s_vp.statMaxPerSlice, count);
	}
	s_vp.statPoolUsed = poolUsed;

	VectorSet4(block->params,
		(float)numUploaded,
		fadeStart,
		1.0f / MAX(fadeEnd - fadeStart, 1.0f),
		Com_Clamp(0.0f, 1.0f, r_volumetricParticlesHistory->value));

	// the state the next frame is compared with, sorted by id
	std::stable_sort(current, current + numCurrent, []( const volParticleEval_t& a, const volParticleEval_t& b ) {
		return a.id < b.id;
	});
	Com_Memcpy(s_vp.previous, current, numCurrent * sizeof(current[0]));
	s_vp.numPrevious = numCurrent;
	s_vp.previousFrame = backEndData->realFrameNumber;
	s_vp.previousWorld = tr.world;

	s_vp.statBuildMicroseconds = (int)std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - startTime).count();

	if ( r_volumetricParticlesDebug->integer && (s_vp.statLastPrint > s_vp.statFrame || s_vp.statFrame - s_vp.statLastPrint >= 60) )
	{
		s_vp.statLastPrint = s_vp.statFrame;
		R_VolParticlesPrintStats("");
	}

	return numUploaded;
}

/*
=================
R_VolParticles_f

r_volparticles: statistics of the last froxel frame
=================
*/
void R_VolParticles_f( void )
{
	if ( !r_volumetricParticles->integer )
		ri.Printf(PRINT_ALL, "r_volumetricParticles is 0: FX particles add no media\n");
	if ( !R_VolumetricFroxelEnabled() )
		ri.Printf(PRINT_ALL, "the froxel fog is off (r_volumetricFog 2 needed)\n");
	R_VolParticlesPrintStats("");
	ri.Printf(PRINT_ALL, "limits: %d per scene, %d uploaded (r_volumetricParticlesMax %d), %d slice list entries\n",
		MAX_REF_VOL_PARTICLES, MAX_GPU_VOL_PARTICLES, r_volumetricParticlesMax->integer, VOL_PARTICLE_POOL);
	ri.Printf(PRINT_ALL, "r_volumetricFogDebug 26: particle density, 27: particle history reduction, 28: proxy bounds, "
		"31/33: emission\n");
}
