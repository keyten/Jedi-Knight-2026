/*[Vertex]*/
// MD3 plant skinning (FOLIAGE_PLANT: ferns, bushes, grass cards;
// tr_plantskel.cpp, tr_plantsim.cpp). Pasted after leaf_flutter.glsl into
// lightall, generic, fogpass and velocity, so every pass (main, depth prepass,
// sun / point shadows, fog, motion vectors) draws the same pose.
//
// Each frond of the plant is a chain of rigid segments simulated on the CPU
// (inextensible centerline, bending stiffness along it, character contact,
// wind). A segment is a bone: a rotation q and a translation t that move the
// rest world position to the current one, p' = q p q* + t. A vertex follows
// two neighbouring bones of its frond (blended across the joint between them),
// so the lamina rides on the centerline: no stretching, the width is kept and
// the normal turns with its section. Bone 0 is the rest pose (hubs, stems).

// rend2 binds attr_BoneWeights to its fixed location; the MD3 plant VBO keeps
// the skin there (G2 skeletal programs never draw plants): bone A, bone B,
// weight of B, 1 when skinned, as normalized bytes
#if !defined(USE_SKELETAL_ANIMATION)
in vec4 attr_BoneWeights;
#define PLANT_SKIN_ATTRIBUTE attr_BoneWeights
#else
#define PLANT_SKIN_ATTRIBUTE vec4(0.0)
#endif

// bones of this frame's plants, two texels per bone (q, then t), one block
// for the current pose and one for the previous (motion vectors)
uniform samplerBuffer u_PlantBones;
// x = first texel of this plant, y = texels of one pose (2 * bones),
// z = 1 when skinned, w = debug view (r_plantDebug colors, main pass only)
uniform vec4 u_PlantSkin;

bool PlantSkinEnabled()
{
	return u_PlantSkin.z > 0.5;
}

vec3 PlantQuatRotate(in vec4 q, in vec3 v)
{
	vec3 t = 2.0 * cross(q.xyz, v);
	return v + q.w * t + cross(q.xyz, t);
}

void PlantBone(in int bone, in bool previous, out vec4 q, out vec3 t)
{
	int texel = int(u_PlantSkin.x) + (previous ? int(u_PlantSkin.y) : 0) + 2 * bone;
	q = texelFetch(u_PlantBones, texel);
	t = texelFetch(u_PlantBones, texel + 1).xyz;
}

// the two bones and the weight of the second of this vertex
void PlantSkinBones(out int boneA, out int boneB, out float weightB)
{
	vec4 skin = PLANT_SKIN_ATTRIBUTE * 255.0 + 0.5;
	boneA = int(skin.x);
	boneB = int(skin.y);
	weightB = floor(skin.z) * (1.0 / 255.0);
}

// Skinned world position of the rest world position ws; rotation: the blended
// rotation of the vertex, for its normal and tangent
vec3 PlantSkinPosition(in vec3 ws, in bool previous, out vec4 rotation)
{
	int boneA, boneB;
	float weightB;
	PlantSkinBones(boneA, boneB, weightB);

	vec4 qa, qb;
	vec3 ta, tb;
	PlantBone(boneA, previous, qa, ta);
	PlantBone(boneB, previous, qb, tb);

	vec3 pa = PlantQuatRotate(qa, ws) + ta;
	vec3 pb = PlantQuatRotate(qb, ws) + tb;
	// shortest arc between the two rotations, normalized lerp
	qb *= dot(qa, qb) < 0.0 ? -1.0 : 1.0;
	rotation = normalize(mix(qa, qb, weightB));
	return mix(pa, pb, weightB);
}

vec3 PlantSkinDirection(in vec4 rotation, in vec3 v)
{
	return PlantQuatRotate(rotation, v);
}

// r_plantDebug 2 / 4: s along the frond (bone index parity) and the frond
// (bone index hash) as a color, 0 when off
vec4 PlantSkinDebugColor()
{
	if (u_PlantSkin.w < 0.5)
		return vec4(0.0);
	int boneA, boneB;
	float weightB;
	PlantSkinBones(boneA, boneB, weightB);
	float bone = mix(float(boneA), float(boneB), weightB);
	if (u_PlantSkin.w < 3.0)
	{
		// bone 0 (rest) gray, the frond bones alternate
		if (bone < 0.5)
			return vec4(0.4, 0.4, 0.4, 1.0);
		float stripe = fract(bone * 0.5);
		return vec4(mix(vec3(1.0, 0.55, 0.1), vec3(0.1, 0.45, 1.0), stripe), 1.0);
	}
	float h = fract(sin(float(boneA) * 12.9898) * 43758.547);
	return vec4(0.5 + 0.5 * cos(6.2832 * (h + vec3(0.0, 0.33, 0.67))), 1.0);
}
