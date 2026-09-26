/*[Vertex]*/
// Foliage character interaction (r_foliageInteraction, tr_foliageinteract.cpp).
// Pasted into the vertex shader of surface_sprites (grass) and, with
// plant_bend.glsl, lightall, generic, fogpass and velocity (MD3 plants), so
// every pass (main, depth prepass, sun / point shadows, fog, motion vectors)
// bends a plant the same way.
//
// The direct push is stateless: a pure function of the rest position and the
// colliders of this frame (and of the previous frame, for motion vectors). The
// colliders are the real character bodies sent by cgame, never the camera.
// The optional persistent field (r_foliageField) adds a short history.

#define FOLIAGE_MAX_INTERACTORS 16

// std140, FoliageInteractionBlock in tr_local.h. Per collider two vec4:
//   [2i]     axis x, y, bottom z (feet), top z
//   [2i + 1] radius, velocity x, velocity y, 1 = airborne
// Then the persistent bend field (r_foliageField, tr_foliagefield.cpp): a
// player centered world XY texture of the bend the characters left behind.
layout(std140) uniform FoliageInteraction
{
	vec4 u_FIParams;	// current count, previous count, strength, debug: 1 = no wind
	vec4 u_FICurrent[FOLIAGE_MAX_INTERACTORS * 2];
	vec4 u_FIPrevious[FOLIAGE_MAX_INTERACTORS * 2];
	vec4 u_FIField;			// field center x, y (world), 1 / extent, scale (0 = no field)
	vec4 u_FIFieldPrevious;	// the same for the previous frame's field (motion vectors)
	vec4 u_FIFieldUpdate;	// update pass: time step, spring k, damping c, impulse
	vec4 u_FIFieldShift;	// update pass: texel shift x, y, clear (0 / 1); draws: w 1 = no direct term (debug)
};

// the field state: rg = bend (same units as FoliageInteractionBend), ba = its
// velocity. This frame's and the previous frame's (velocity pass).
uniform sampler2D u_FoliageFieldMap;
uniform sampler2D u_FoliageFieldPrevMap;

// the largest angle a stem turns away from its rest direction (65 degrees):
// a character can flatten a plant but never fold it over its root
const float FOLIAGE_MAX_BEND_COS = 0.42261826;
const float FOLIAGE_MAX_BEND_SIN = 0.90630779;

// r_foliageInteractionDebug 4: interaction only, the wind stays at rest
bool FoliageInteractionNoWind()
{
	return u_FIParams.w > 0.5;
}

// Push of one collider (its two vec4 of the block) on the reference point q:
// bend direction times magnitude, before the strength. Contact (distance to
// the capsule) is the main term, so a character standing still keeps the plant
// parted; velocity only biases the direction and adds a little push along the
// walk. contact in [0, 1]; drive = walk direction times the speed factor.
vec2 FoliageColliderPush(in vec4 axis, in vec4 body, in vec3 q, out float contact, out vec2 drive)
{
	contact = 0.0;
	drive = vec2(0.0);
	float radius = body.x;
	float reach = radius * 1.75;

	vec2 d = q.xy - axis.xy;
	float d2 = dot(d, d);
	if (d2 >= reach * reach)
		return vec2(0.0);

	// capsule: vertical segment with round ends at the feet and the head,
	// so the lower legs reach the grass and a jump lifts the contact off
	float z0 = axis.z + radius;
	float z1 = max(axis.w - radius, z0);
	float dz = q.z - clamp(q.z, z0, z1);
	float dist = sqrt(d2 + dz * dz);
	contact = max(1.0 - smoothstep(0.6 * radius, reach, dist), 0.0);
	if (contact <= 0.0)
		return vec2(0.0);

	vec2 velocity = body.yz;
	float speed = length(velocity);
	vec2 moveDir = speed > 1.0 ? velocity / speed : vec2(0.0);
	float len = sqrt(d2);
	vec2 n;
	if (len > 0.5)
		n = d / len;
	else if (speed > 1.0)
		n = moveDir;	// right on the axis: push along the walk
	else
	{
		float a = fract(sin(dot(q.xy, vec2(12.9898, 78.233))) * 43758.547) * 6.2832;
		n = vec2(cos(a), sin(a));
	}

	// walking through: bias along the movement, a bit more in front of
	// the body (|n + 0.6 s moveDir| >= 0.4, normalize is safe)
	float s = clamp(speed * (1.0 / 320.0), 0.0, 1.0);
	vec2 dir = normalize(n + (0.6 * s) * moveDir);
	float magnitude = contact * (1.0 + 0.35 * s * max(dot(n, moveDir), 0.0));
	drive = s * moveDir;
	return dir * magnitude;
}

// Horizontal bend vector the colliders push the reference point q with: tip
// displacement per unit of stem length, before FoliageApplyBend limits it.
// heat = strongest contact in [0, 1].
vec2 FoliageInteractionBend(in vec3 q, in bool previous, out float heat)
{
	vec2 bend = vec2(0.0);
	heat = 0.0;
	int count = int(previous ? u_FIParams.y : u_FIParams.x);

	for (int i = 0; i < FOLIAGE_MAX_INTERACTORS; ++i)
	{
		if (i >= count)
			break;

		vec4 axis = previous ? u_FIPrevious[2 * i] : u_FICurrent[2 * i];
		vec4 body = previous ? u_FIPrevious[2 * i + 1] : u_FICurrent[2 * i + 1];
		float contact;
		vec2 drive;
		bend += FoliageColliderPush(axis, body, q, contact, drive);
		heat = max(heat, contact);
	}

	return bend * u_FIParams.z;
}

// Persistent bend field at the world position xy (r_foliageField): what the
// characters left behind, springing back to rest. Zero outside the covered
// square, faded over its border.
vec2 FoliageFieldBend(in vec2 xy, in bool previous)
{
	vec4 field = previous ? u_FIFieldPrevious : u_FIField;
	if (field.w == 0.0)
		return vec2(0.0);

	vec2 uv = (xy - field.xy) * field.z + 0.5;
	vec2 edge = min(uv, 1.0 - uv);
	float fade = smoothstep(0.0, 0.03, min(edge.x, edge.y));
	if (fade <= 0.0)
		return vec2(0.0);

	vec2 bend = previous ? textureLod(u_FoliageFieldPrevMap, uv, 0.0).xy
		: textureLod(u_FoliageFieldMap, uv, 0.0).xy;
	return bend * (field.w * fade);
}

// Everything the characters do to a plant: the direct push of the colliders
// at q and the persistent field at fieldXY (grass: the tuft anchor, MD3
// plants: the root). While a character touches the plant the direct term
// leads; as it leaves (heat -> 0) the field, which followed the same push,
// takes over smoothly and springs back. heat: direct contact.
vec2 FoliageCharacterBend(in vec3 q, in vec2 fieldXY, in bool previous, out float heat)
{
	vec2 direct = FoliageInteractionBend(q, previous, heat);
	vec2 field = FoliageFieldBend(fieldXY, previous);
	// r_foliageFieldDebug 16: the field alone
	if (u_FIFieldShift.w > 0.5)
		return field;
	return direct + field * (1.0 - heat);
}

// Bends the stem vector v (vertex - root) by the horizontal bend vector,
// scaled by weight (0 at the root, 1 at the tip). The length is kept (no
// stretching) and the turn is limited to 65 degrees, so no push can fold a
// plant over, whatever direction the stem had.
vec3 FoliageApplyBend(in vec3 v, in vec2 bend, in float weight)
{
	float len = length(v);
	vec2 b = bend * weight;
	if (len < 1e-3 || dot(b, b) < 1e-8)
		return v;

	vec3 nv = v / len;
	vec3 bent = normalize(nv + vec3(b, 0.0));
	float c = dot(nv, bent);
	if (c < FOLIAGE_MAX_BEND_COS)
	{
		vec3 side = bent - nv * c;
		float sideLen = length(side);
		// antiparallel: nothing sensible to turn towards, keep the rest pose
		if (sideLen < 1e-4)
			return v;
		bent = nv * FOLIAGE_MAX_BEND_COS + side * (FOLIAGE_MAX_BEND_SIN / sideLen);
	}
	return bent * len;
}

// Turns a normal (or tangent) with the rotation that took rest to bent
// (both stem vectors of the same length): Rodrigues around rest x bent.
vec3 FoliageRotateNormal(in vec3 n, in vec3 rest, in vec3 bent)
{
	vec3 a = normalize(rest);
	vec3 b = normalize(bent);
	vec3 axis = cross(a, b);
	float s = length(axis);
	float c = dot(a, b);
	if (s < 1e-5)
		return n;
	axis /= s;
	return n * c + cross(axis, n) * s + axis * (dot(axis, n) * (1.0 - c));
}
