/*[Vertex]*/
// Grass and character interaction (r_foliageInteraction, tr_foliageinteract.cpp,
// tr_foliagefield.cpp). Pasted into the vertex shader of surface_sprites
// (grass), so every pass (main, depth prepass, sun shadows, fog, motion
// vectors) bends a tuft the same way, and as a fragment library into the
// update pass of the bend field (foliage_field.glsl).
//
// The grass state lives in the bend field: per patch of ground the horizontal
// displacement delta (world units) the characters pushed the grass by, at the
// probe height above the ground, its velocity and a slow trampled part. A
// character is a hard one-sided contact there: the grass is pushed exactly out
// of the capsule, nothing moves ahead of the body.
//
// A blade is an inextensible beam (FoliageBladeBend): its tilt grows along its
// length with the bending moment over the stiffness, which drops towards the
// thin tip. The leg pushes at the probe height (above it the blade stays
// straight), the wind loads it all along; the position integrates the tilt,
// so the blade curves and keeps its length.

#define FOLIAGE_MAX_INTERACTORS 16
// Force push / pull waves (FOLIAGE_FORCEWAVE_GPU, also in leaf_flutter.glsl)
#ifndef FOLIAGE_MAX_FORCE_WAVES
#define FOLIAGE_MAX_FORCE_WAVES 4
#endif

// std140, FoliageInteractionBlock in tr_local.h. Per collider two vec4:
//   [2i]     axis x, y, bottom z (feet), top z
//   [2i + 1] radius, velocity x, velocity y, 1 = airborne
// Then the bend field: a player centered world XY texture of the grass state.
// Then the Force push / pull waves, the newest first, per wave three vec4:
//   (origin, radius), (horizontal aim x, y (0 = all around), cosine of the
//   cone, signed strength: < 0 pull), (start time, seconds since the start)
layout(std140) uniform FoliageInteraction
{
	vec4 u_FIParams;	// current count, previous count, strength, debug: 1 = no wind
	vec4 u_FICurrent[FOLIAGE_MAX_INTERACTORS * 2];
	vec4 u_FIPrevious[FOLIAGE_MAX_INTERACTORS * 2];
	vec4 u_FIField;			// field center x, y (world), 1 / extent, scale (0 = no field)
	vec4 u_FIFieldPrevious;	// the same for the previous frame's field (motion vectors)
	vec4 u_FIFieldUpdate;	// update pass: time step, spring k, damping c, impulse
	vec4 u_FIFieldShift;	// update pass: texel shift x, y, clear (0 / 1), unused
	vec4 u_FIFieldTrample;	// update pass: trample strength, 1 / recovery time, unused, unused
	vec4 u_FIWaveParams;	// wave count, front half width, grass kick (units / s), front speed
	vec4 u_FIWaves[FOLIAGE_MAX_FORCE_WAVES * 3];
};

// the field state: rg = grass displacement delta (world units), ba = its
// velocity. This frame's and the previous frame's (velocity pass); the update
// pass reads the previous state and the trample state through these.
uniform sampler2D u_FoliageFieldMap;
uniform sampler2D u_FoliageFieldPrevMap;

// height above the ground where a leg pushes the grass (shins)
const float FOLIAGE_PROBE_HEIGHT = 16.0;

// r_foliageInteractionDebug 4: interaction only, the wind stays at rest
bool FoliageInteractionNoWind()
{
	return u_FIParams.w > 0.5;
}

// Contact of one collider (its two vec4 of the block) with the grass patch at
// the probe point q: how deep q is inside the capsule (<= 0: no contact) and
// the horizontal direction out of it, n (zero right on the axis).
float FoliageColliderPenetration(in vec4 axis, in vec4 body, in vec3 q, out vec2 n)
{
	n = vec2(0.0);
	float radius = body.x;
	vec2 d = q.xy - axis.xy;
	float d2 = dot(d, d);
	if (d2 >= radius * radius)
		return 0.0;

	// capsule: vertical segment with round ends at the feet and the head
	float z0 = axis.z + radius;
	float z1 = max(axis.w - radius, z0);
	float dz = q.z - clamp(q.z, z0, z1);
	float dist = sqrt(d2 + dz * dz);
	float len = sqrt(d2);
	if (len > 1e-3)
		n = d / len;
	return radius - dist;
}

// Velocity kick (world units / s) of the Force push / pull waves on the grass
// patch at xy while their fronts moved over the last dt seconds: the part of
// the kick of a smoothstep pulse, away from (push) or towards (pull) the
// caster. R_FoliageForceWaveImpulse (tr_foliageinteract.cpp) without the
// height falloff: the field has no height.
vec2 FoliageForceWaveKick(in vec2 xy, in float dt)
{
	vec2 kick = vec2(0.0);
	int count = int(u_FIWaveParams.x);
	float width = u_FIWaveParams.y;
	for (int i = 0; i < FOLIAGE_MAX_FORCE_WAVES; ++i)
	{
		if (i >= count)
			break;
		vec4 origin = u_FIWaves[3 * i];
		vec4 aim = u_FIWaves[3 * i + 1];
		vec2 r = xy - origin.xy;
		float d = length(r);
		if (d >= origin.w)
			continue;
		float front = u_FIWaveParams.w * u_FIWaves[3 * i + 2].y;
		float part = smoothstep(-width, width, front - d) -
			smoothstep(-width, width, front - u_FIWaveParams.w * dt - d);
		if (part <= 0.0)
			continue;
		vec2 n = d > 1e-3 ? r / d : (dot(aim.xy, aim.xy) > 0.0 ? aim.xy : vec2(1.0, 0.0));
		float a = aim.z > -1.0 ? smoothstep(aim.z - 0.15, aim.z + 0.05, dot(n, aim.xy)) : 1.0;
		a *= (1.0 - smoothstep(0.6 * origin.w, origin.w, d)) * (1.0 - 0.5 * d / origin.w);
		kick += n * (u_FIWaveParams.z * aim.w * a * part);
	}
	return kick;
}

// Grass displacement of the bend field at the world position xy: zero outside
// the covered square, faded over its border.
vec2 FoliageFieldDisplacement(in vec2 xy, in bool previous)
{
	vec4 field = previous ? u_FIFieldPrevious : u_FIField;
	if (field.w == 0.0)
		return vec2(0.0);

	vec2 uv = (xy - field.xy) * field.z + 0.5;
	vec2 edge = min(uv, 1.0 - uv);
	float fade = smoothstep(0.0, 0.03, min(edge.x, edge.y));
	if (fade <= 0.0)
		return vec2(0.0);

	vec2 delta = previous ? textureLod(u_FoliageFieldPrevMap, uv, 0.0).xy
		: textureLod(u_FoliageFieldMap, uv, 0.0).xy;
	return delta * (field.w * fade);
}

// Beam profiles. Stiffness EI(s) = (1 - a s)^3 (a tapering blade, s = 0 root,
// 1 tip); the tilt is the integral of moment / EI:
//   wind (load all along, moment (1 - s)^2): FoliageBeamWind(s)
//   leg at sc (moment sc - s below it, 0 above): FoliageBeamContact(s, sc)
const float FOLIAGE_BEAM_A = 0.7;
// integral over the blade of FoliageBeamWind (its mean tilt per unit load)
const float FOLIAGE_BEAM_WIND_MEAN = 0.457828;
// joint limit of the blade tilt (80 degrees): flattened, never folded over
const float FOLIAGE_BEAM_MAX_TILT = 1.396263;

float FoliageBeamWindF(in float s)
{
	const float a = FOLIAGE_BEAM_A;
	const float b = FOLIAGE_BEAM_A - 1.0;
	float w = 1.0 - a * s;
	return -(log(w) - 2.0 * b / w - b * b / (2.0 * w * w)) / (a * a * a);
}

float FoliageBeamWind(in float s)
{
	return FoliageBeamWindF(s) - FoliageBeamWindF(0.0);
}

float FoliageBeamContactF(in float s, in float sc)
{
	const float a = FOLIAGE_BEAM_A;
	float w = 1.0 - a * s;
	return ((a * sc - 1.0) / (2.0 * w * w) + 1.0 / w) / (a * a);
}

float FoliageBeamContact(in float s, in float sc)
{
	return FoliageBeamContactF(min(s, sc), sc) - FoliageBeamContactF(0.0, sc);
}

// integral of FoliageBeamContact over [0, sc]: the mean tilt below the contact
// times sc, per unit load
float FoliageBeamContactMean(in float sc)
{
	const float a = FOLIAGE_BEAM_A;
	float w = 1.0 - a * sc;
	float h1 = ((a * sc - 1.0) / (2.0 * w) - log(w)) / (a * a * a);
	float h0 = (a * sc - 1.0) / (2.0 * a * a * a);
	return h1 - h0 - sc * FoliageBeamContactF(0.0, sc);
}

// Offset of the point s (0 root .. 1 tip) of the blade with the rest stem
// vector stem (straight, skew included) from its rest position, under the wind
// tip displacement windTip and the leg displacement delta at the probe height
// (both horizontal, world units). The blade length is kept.
vec3 FoliageBladeBend(in vec3 stem, in float s, in vec2 windTip, in vec2 delta)
{
	float L = length(stem);
	float windLen = length(windTip);
	float deltaLen = length(delta);
	if (L < 1e-3 || s <= 0.0 || (windLen < 1e-3 && deltaLen < 1e-3))
		return vec3(0.0);

	float up = stem.z >= 0.0 ? 1.0 : -1.0;
	float skewLen = length(stem.xy);
	vec2 tilt0 = skewLen > 1e-4 ? stem.xy * (atan(skewLen, abs(stem.z)) / skewLen) : vec2(0.0);

	// wind: the mean tilt that puts the tip windTip away
	vec2 windLoad = windLen > 1e-3 ? windTip * (asin(min(windLen / L, 0.95)) /
		(windLen * FOLIAGE_BEAM_WIND_MEAN)) : vec2(0.0);

	// leg: the mean tilt below the contact that moves the contact point delta
	// away (the grass taller than the probe is pushed at the probe)
	float sc = clamp(FOLIAGE_PROBE_HEIGHT / L, 0.05, 1.0);
	vec2 contactLoad = deltaLen > 1e-3 ? delta * (asin(min(deltaLen / (L * sc), 0.98)) * sc /
		(deltaLen * FoliageBeamContactMean(sc))) : vec2(0.0);

	// integrate the tangent from the root to s over a fixed grid of 6 straight
	// pieces (midpoint tilt), the last one partial: every point of the blade
	// lies on the same polyline, whose length is exactly L
	vec3 p = vec3(0.0);
	for (int i = 0; i < 6; ++i)
	{
		float u0 = float(i) * (1.0 / 6.0);
		float u1 = min(u0 + 1.0 / 6.0, s);
		if (u1 <= u0)
			break;
		float u = (float(i) + 0.5) * (1.0 / 6.0);
		vec2 tilt = tilt0 + windLoad * FoliageBeamWind(u) + contactLoad * FoliageBeamContact(u, sc);
		float angle = length(tilt);
		vec2 dir = angle > 1e-5 ? tilt / angle : vec2(0.0);
		angle = min(angle, FOLIAGE_BEAM_MAX_TILT);
		p += vec3(dir * sin(angle), up * cos(angle)) * (u1 - u0);
	}
	return p * L - stem * s;
}
