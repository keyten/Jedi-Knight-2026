/*[Vertex]*/
// MD3 plant root bend (FOLIAGE_PLANT: ferns and bushes, tr_foliageinteract.cpp).
// Pasted after foliage_interact.glsl into lightall, generic, fogpass and
// velocity. The whole plant turns around its root: the root stays put, the
// tips move most. Two sources, added as bend vectors and applied once:
//   wind:        a slow coherent breeze per plant (r_plantWind, 0 = none)
//   interaction: the character colliders (r_foliageInteraction)
// Pure function of the rest position, the entity and time; no camera input.

uniform vec4 u_PlantBend;		// root in object space xyz, 1 / plant size (0 = not a bending plant)
uniform vec4 u_PlantBendParams;	// wind dir x, y (unit), wind bend amplitude, interaction (0 / 1)
uniform vec4 u_PlantBendTime;	// time, previous frame time, wind speed, unused

bool PlantBendEnabled()
{
	return u_PlantBend.w > 0.0;
}

// World space root of the plant for a model matrix
vec3 PlantBendRoot(in mat4 modelMatrix)
{
	return (modelMatrix * vec4(u_PlantBend.xyz, 1.0)).xyz;
}

// 0 at the root, 1 at the plant size away from it (object space, so the scale
// of the entity doesn't matter); t^1.5 keeps the base of the fronds stiff
float PlantBendWeight(in vec3 objectPosition)
{
	float t = clamp(length(objectPosition - u_PlantBend.xyz) * u_PlantBend.w, 0.0, 1.0);
	return t * sqrt(t);
}

// Breeze bend of one plant: a downwind lean that swells and eases (gust
// fronts travel downwind, ~500 unit wavelength) plus a small side sway.
// The same for every vertex of the plant, so the fronds move together.
vec2 PlantWindBend(in vec3 root, in float t)
{
	vec2 dir = u_PlantBendParams.xy;
	vec2 side = vec2(-dir.y, dir.x);
	float tw = t * u_PlantBendTime.z;
	float lean = 0.55 + 0.45 * sin(tw * 1.3 - dot(root.xy, dir) * 0.012);
	float sway = 0.35 * sin(tw * 2.7 + dot(root.xy, side) * 0.021 + root.z * 0.05);
	return (dir * lean + side * sway) * u_PlantBendParams.z;
}

// Bent world position of the rest world position p. previous selects the
// colliders of the previous frame (velocity pass, with the previous time and
// the previous root). heat: collider contact for r_foliageInteractionDebug 8.
vec3 PlantBendPosition(in vec3 p, in vec3 root, in float weight, in float t, in bool previous,
	out float heat)
{
	vec2 bend = vec2(0.0);
	heat = 0.0;
	bool interaction = u_PlantBendParams.w > 0.0;
	if (!interaction || !FoliageInteractionNoWind())
		bend += PlantWindBend(root, t);
	// the persistent field at the root: the whole plant turns together
	if (interaction)
		bend += FoliageCharacterBend(p, root.xy, previous, heat);
	return root + FoliageApplyBend(p - root, bend, weight);
}
