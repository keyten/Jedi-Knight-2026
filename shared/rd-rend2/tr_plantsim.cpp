/*
 * MD3 plant simulation (FOLIAGE_PLANT / FOLIAGE_GRASS map objects with a frond
 * skeleton, tr_plantskel.cpp): wind (r_plantWind) and character contact
 * (r_foliageInteraction) bend every frond of every nearby plant as a chain of
 * rigid segments, on the CPU, once per frame.
 *
 * Model of one frond (XPBD, Macklin et al. 2016, fixed 120 Hz sub steps):
 *   - node 0 is pinned to its attachment (hub, base edge, stem);
 *   - every segment keeps its rest length exactly (hard constraint): a frond
 *     can bend, it can't stretch or shrink;
 *   - every joint pulls the next segment towards its rest direction in the
 *     frame of the previous segment (parallel transported), with a compliance
 *     from a frequency profile: the petiole / base is stiff, the tip soft, a
 *     grass card is soft all along; joints are limited (no folding over);
 *   - the character capsules are hard one-sided contacts on the segments
 *     (radius + part of the lamina width, no force field ahead of the body);
 *     the whole frond is pushed to one side of the body (hysteresis), so it
 *     swings aside around its base instead of wrapping around the leg; a frond
 *     whose base is inside the body (character standing in the plant) is
 *     splayed out from its base instead; friction on the body;
 *   - wind is a drag towards the coherent breeze of the grass
 *     (glsl/surface_sprites.glsl FoliageWind, same gust field), across the
 *     segments; aerodynamic and internal damping;
 *   - nothing goes below the ground under the plant.
 * The result per segment is a bone (rotation + translation of the rest world
 * position), uploaded with the previous frame's bones into a buffer texture
 * (glsl/plant_skin.glsl skins the mesh in every pass).
 *
 * Instances are keyed by model and origin (stock plants are static map
 * objects); the state persists across frames, so a frond swings back after a
 * character left. A plant cut by a saber (tr_plantcut.cpp) keeps its cut mesh
 * in its instance, never evicted; its fronds' chains end where they were cut. Plants farther than r_plantWindDistance stay at rest, farther
 * than r_plantSimDistance (and away from every character) take one coarse
 * step per frame.
 */
#include "tr_local.h"
#include <vector>
#include <unordered_map>
#include <chrono>

namespace
{

const float PLANTSIM_STEP = 1.0f / 120.0f;	// sub step (s)
const int PLANTSIM_MAX_STEPS = 8;			// sub steps per frame (else time is dropped)
const int PLANTSIM_ITERATIONS = 4;			// constraint iterations per sub step
const int PLANTSIM_COARSE_ITERATIONS = 2;
const float PLANTSIM_COARSE_STEP = 1.0f / 30.0f;
const int PLANTSIM_ARENA_TEXELS = 1 << 16;	// bone texels per frame (1 MB)
const int PLANTSIM_EVICT_FRAMES = 300;		// unseen instances are dropped after this
// wind drag at r_plantWindStrength 1: ~14 degrees at the tip in the strongest gust
const float PLANTSIM_WIND_ACCEL = 450.0f;
const float PLANTSIM_AIR_DRAG = 2.0f;		// 1/s
const float PLANTSIM_CONTACT_FRICTION = 30.0f;	// 1/s, velocity of a node on a body towards the body's

struct Vec3
{
	float x, y, z;
};
inline Vec3 V3(float x, float y, float z) { Vec3 r = { x, y, z }; return r; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(Vec3 a, Vec3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(Vec3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { a = a + b; return a; }
inline Vec3 &operator-=(Vec3 &a, Vec3 b) { a = a - b; return a; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float Length(Vec3 a) { return sqrtf(Dot(a, a)); }
inline Vec3 Normalized(Vec3 a, Vec3 fallback)
{
	const float l = Length(a);
	return l > 1e-6f ? a * (1.0f / l) : fallback;
}

// rotation quaternion, (x, y, z) vector part and w
struct Quat
{
	float x, y, z, w;
};
const Quat QUAT_IDENTITY = { 0.0f, 0.0f, 0.0f, 1.0f };

inline Quat Mul(Quat a, Quat b)
{
	Quat r;
	r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
	r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
	r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
	r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
	return r;
}

inline Vec3 Rotate(Quat q, Vec3 v)
{
	const Vec3 u = V3(q.x, q.y, q.z);
	const Vec3 t = Cross(u, v) * 2.0f;
	return v + t * q.w + Cross(u, t);
}

// shortest arc taking the direction a to the direction b
Quat Between(Vec3 a, Vec3 b)
{
	a = Normalized(a, V3(0.0f, 0.0f, 1.0f));
	b = Normalized(b, a);
	const float c = Dot(a, b);
	if (c < -0.9999f)
	{
		Vec3 axis = Cross(a, V3(1.0f, 0.0f, 0.0f));
		if (Dot(axis, axis) < 1e-6f)
			axis = Cross(a, V3(0.0f, 1.0f, 0.0f));
		axis = Normalized(axis, V3(0.0f, 0.0f, 1.0f));
		Quat q = { axis.x, axis.y, axis.z, 0.0f };
		return q;
	}
	const Vec3 axis = Cross(a, b);
	Quat q = { axis.x, axis.y, axis.z, 1.0f + c };
	const float n = 1.0f / sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	q.x *= n; q.y *= n; q.z *= n; q.w *= n;
	return q;
}

// closest points of the segments p0-p1 and q0-q1: parameter s on the first,
// the points c (first) and a (second)
void ClosestSegmentSegment(Vec3 p0, Vec3 p1, Vec3 q0, Vec3 q1, float &s, Vec3 &c, Vec3 &a)
{
	const Vec3 d1 = p1 - p0, d2 = q1 - q0, r = p0 - q0;
	const float aa = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
	const float cc = Dot(d1, r), b = Dot(d1, d2);
	const float denom = aa * e - b * b;
	float t;
	s = denom > 1e-9f ? Com_Clamp(0.0f, 1.0f, (b * f - cc * e) / denom) : 0.0f;
	t = e > 1e-9f ? (b * s + f) / e : 0.0f;
	if (t < 0.0f)
	{
		t = 0.0f;
		s = aa > 1e-9f ? Com_Clamp(0.0f, 1.0f, -cc / aa) : 0.0f;
	}
	else if (t > 1.0f)
	{
		t = 1.0f;
		s = aa > 1e-9f ? Com_Clamp(0.0f, 1.0f, (b - cc) / aa) : 0.0f;
	}
	c = p0 + d1 * s;
	a = q0 + d2 * t;
}

struct Capsule
{
	Vec3 a0, a1;		// segment (feet + r .. head - r)
	float radius;
	Vec3 velocity;		// horizontal
};

struct FrondSim
{
	int segments;
	int firstBone;
	float keepU;						// node units of the chain kept (saber cut), all of it uncut
	Vec3 X[PLANTSKEL_MAX_NODES];		// rest, world
	Vec3 D[PLANTSKEL_MAX_NODES - 1];	// rest segment vectors
	Vec3 T[PLANTSKEL_MAX_NODES - 1];	// rest basis of every segment: tangent,
	Vec3 B[PLANTSKEL_MAX_NODES - 1];	// across the lamina,
	Vec3 N[PLANTSKEL_MAX_NODES - 1];	// lamina normal
	float L[PLANTSKEL_MAX_NODES - 1];
	float alpha[PLANTSKEL_MAX_NODES - 1];	// joint compliance (before 1 / h^2)
	float lateralStiffness[PLANTSKEL_MAX_NODES - 1];	// in plane / out of plane bending stiffness
	float maxCos[PLANTSKEL_MAX_NODES - 1];	// joint limit
	float invMass[PLANTSKEL_MAX_NODES];
	float margin[PLANTSKEL_MAX_NODES - 1];	// contact margin of each segment (lamina)
	Vec3 x[PLANTSKEL_MAX_NODES];
	Vec3 prevX[PLANTSKEL_MAX_NODES];		// state before the latest fixed step
	Vec3 v[PLANTSKEL_MAX_NODES];
	Vec3 side;							// horizontal across the frond (rest)
	float damping;						// internal, 1/s
	int contactSide;					// -1 / +1 while in contact, 0 free
	float contactTimer;					// s left before the side is forgotten
	bool contact;						// touched in the last frame
	uint32_t pressed;					// nodes a body pushed in the last sub step
	bool splayed;						// base inside a body: splayed, not pushed aside
};

struct PlantInstance
{
	qhandle_t hModel;
	const plantSkeleton_t *skel;
	Vec3 origin;
	Vec3 axis[3];
	Vec3 center;			// bounding sphere of the plant, world
	float radius;
	float groundZ;
	std::vector<FrondSim> fronds;
	std::vector<float> bones;		// current pose, 8 floats per bone (q, t)
	std::vector<float> prevBones;	// previous frame's pose
	int seenFrame;
	int simFrame;
	int lastTime;			// refdef time of the last step
	float accumulator;
	float waveTime;			// refdef time (s) the Force wave kicks are integrated to
	float scale;			// largest axis length of the entity
	int arenaOffset;		// first texel this frame, -1 none
	bool atRest;			// bones are the rest pose (current and previous)
	bool valid;
	plantCut_t *cut;		// saber cut mesh (tr_plantcut.cpp), NULL uncut
	bool cutFailed;			// the model can't be cut
	refEntity_t refEnt;		// last refEntity of the plant (the pieces cut off copy it)
};

} // namespace

static struct
{
	std::vector<PlantInstance *> instances;
	std::unordered_map<uint64_t, PlantInstance *> lookup;
	int numCut;				// instances drawn from a cut mesh

	// bones of this frame, uploaded per view in front of its draws
	std::vector<float> arena;
	int arenaFrame;
	int uploaded;
	int simulated;			// instances stepped this frame
	int frameTime;
	bool overflowWarned;

	// this frame's characters (world capsules)
	Capsule capsules[MAX_FOLIAGE_INTERACTORS];
	int numCapsules;
	int capsuleFrame;

	// GPU buffer textures, one per frame in flight
	bool created;
	bool failed;
	GLuint buffers[MAX_FRAMES];
	image_t images[MAX_FRAMES];

	// timing (r_plantDebug 16)
	int lastMicroseconds;
} s_ps;

enum
{
	PLANTDEBUG_CENTERLINES = 1,	// frond centerlines and nodes
	PLANTDEBUG_SEGMENTS    = 2,	// color by segment
	PLANTDEBUG_BONES       = 4,	// color by bone
	PLANTDEBUG_CONTACT     = 8,	// centerlines colored by contact side
	PLANTDEBUG_STATS       = 16	// print the simulation cost
};

static int PlantDebug(void)
{
	return r_plantDebug ? r_plantDebug->integer : 0;
}

bool R_PlantSimActive(void)
{
	return r_autoFoliage->integer &&
		(R_FoliageInteractionActive() || (r_plantWind->integer && r_plantWindStrength->value > 0.0f));
}

/*
=============================================================

Wind: the coherent breeze of the grass sprites (surface_sprites.glsl
FoliageWind), evaluated at the frond base

=============================================================
*/

static float WindHash(int x, int y)
{
	uint32_t h = (uint32_t)(x & 1023) | ((uint32_t)(y & 1023) << 10u);
	h *= 2654435761u;
	h ^= h >> 15u;
	h *= 2246822519u;
	h ^= h >> 13u;
	return (float)h * (1.0f / 4294967295.0f);
}

static float WindNoise(float x, float y)
{
	const float ix = floorf(x), iy = floorf(y);
	float fx = x - ix, fy = y - iy;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	const int cx = (int)ix, cy = (int)iy;
	const float a = WindHash(cx, cy), b = WindHash(cx + 1, cy);
	const float d = WindHash(cx, cy + 1), e = WindHash(cx + 1, cy + 1);
	return (a + (b - a) * fx) + ((d + (e - d) * fx) - (a + (b - a) * fx)) * fy;
}

// downwind lean and cross wind flutter at anchor (world XY), seed in [0, 1]
static void WindAt(float ax, float ay, float seed, float t, float dirX, float dirY, float &lean, float &flutter)
{
	const float sideX = -dirY, sideY = dirX;
	const float tw = t * r_foliageWindSpeed->value;
	const float along = ax * dirX + ay * dirY;
	const float across = ax * sideX + ay * sideY;
	const float gustL = WindNoise(ax / 512.0f - dirX * tw * 0.12f, ay / 512.0f - dirY * tw * 0.12f);
	const float gustS = sinf(along * 0.035f - tw * 2.45f + 1.3f * sinf(across * 0.013f + tw * 0.21f));
	const float gust = MAX(0.25f + 0.75f * gustL + 0.18f * gustS, 0.0f);
	const float ripple = sinf(tw * 3.0f - along * 0.05f + seed * 1.5f);
	lean = gust * (0.6f + 0.3f * ripple);
	flutter = 0.2f * gust * sinf(tw * (4.5f + 2.0f * seed) + seed * 6.2832f);
}

/*
=============================================================

Instances

=============================================================
*/

static uint64_t PlantKey(qhandle_t hModel, const float *origin)
{
	const uint64_t x = (uint64_t)(uint32_t)(int)floorf(origin[0] + 0.5f) & 0xfffff;
	const uint64_t y = (uint64_t)(uint32_t)(int)floorf(origin[1] + 0.5f) & 0xfffff;
	const uint64_t z = (uint64_t)(uint32_t)(int)floorf(origin[2] + 0.5f) & 0xfffff;
	return ((uint64_t)(uint32_t)hModel << 60) ^ (x << 40) ^ (y << 20) ^ z ^ ((uint64_t)(uint32_t)hModel * 0x9E3779B97F4A7C15ull);
}

static Vec3 LocalToWorld(const PlantInstance &inst, const float *p)
{
	return inst.origin + inst.axis[0] * p[0] + inst.axis[1] * p[1] + inst.axis[2] * p[2];
}

static void SetRestBones(std::vector<float> &bones)
{
	for (size_t b = 0; b + 8 <= bones.size(); b += 8)
	{
		bones[b + 0] = 0.0f; bones[b + 1] = 0.0f; bones[b + 2] = 0.0f; bones[b + 3] = 1.0f;
		bones[b + 4] = 0.0f; bones[b + 5] = 0.0f; bones[b + 6] = 0.0f; bones[b + 7] = 0.0f;
	}
}

static void SetupFrond(const PlantInstance &inst, FrondSim &fr, const plantFrond_t &src, float keepU);

static void InitInstance(PlantInstance &inst, const trRefEntity_t *ent, const mdvModel_t *mdv)
{
	const plantSkeleton_t *skel = mdv->plantSkel;
	inst.hModel = ent->e.hModel;
	inst.skel = skel;
	inst.origin = V3(ent->e.origin[0], ent->e.origin[1], ent->e.origin[2]);
	for (int i = 0; i < 3; ++i)
		inst.axis[i] = V3(ent->e.axis[i][0], ent->e.axis[i][1], ent->e.axis[i][2]);

	const vec3_t mid = {
		(mdv->foliageMins[0] + mdv->foliageMaxs[0]) * 0.5f,
		(mdv->foliageMins[1] + mdv->foliageMaxs[1]) * 0.5f,
		(mdv->foliageMins[2] + mdv->foliageMaxs[2]) * 0.5f };
	inst.center = LocalToWorld(inst, mid);
	float scale = 0.0f;
	for (int i = 0; i < 3; ++i)
		scale = MAX(scale, Length(inst.axis[i]));
	scale = MAX(scale, 1e-3f);
	vec3_t half;
	VectorSubtract(mdv->foliageMaxs, mid, half);
	inst.radius = VectorLength(half) * scale + skel->maxLength * scale * 0.5f;
	const vec3_t bottom = { mid[0], mid[1], mdv->foliageMins[2] };
	inst.groundZ = LocalToWorld(inst, bottom).z - 1.0f;

	inst.scale = scale;
	inst.fronds.resize(skel->numFronds);
	for (int f = 0; f < skel->numFronds; ++f)
	{
		const plantFrond_t &src = skel->fronds[f];
		FrondSim &fr = inst.fronds[f];
		SetupFrond(inst, fr, src, (float)(src.numNodes - 1));
		for (int k = 0; k <= fr.segments; ++k)
		{
			fr.x[k] = fr.X[k];
			fr.prevX[k] = fr.X[k];
			fr.v[k] = V3(0.0f, 0.0f, 0.0f);
		}
		fr.contactSide = 0;
		fr.contactTimer = 0.0f;
		fr.contact = false;
		fr.pressed = 0;
		fr.splayed = false;
	}

	inst.bones.assign(skel->numBones * 8, 0.0f);
	inst.prevBones.assign(skel->numBones * 8, 0.0f);
	SetRestBones(inst.bones);
	SetRestBones(inst.prevBones);
	inst.seenFrame = -1;
	inst.simFrame = -1;
	inst.lastTime = 0;
	inst.accumulator = 0.0f;
	inst.waveTime = 0.0f;
	inst.arenaOffset = -1;
	inst.atRest = true;
	inst.valid = true;
	// another model here: the cut of the old one is gone
	if (inst.cut)
	{
		if (R_PlantCutDrawOf(inst.cut))
			--s_ps.numCut;
		R_PlantCutFree(inst.cut);
		inst.cut = NULL;
	}
	inst.cutFailed = false;
}

/*
=============
SetupFrond

Rest shape and dynamics of a frond, its chain ending at keepU (node units;
less than the whole frond after a saber cut, the last segment shortened, at
least 1 unit); the state (x, v, contact) is left alone
=============
*/
static void SetupFrond(const PlantInstance &inst, FrondSim &fr, const plantFrond_t &src, float keepU)
{
	const float scale = inst.scale;
	const float stiffness = MAX(r_plantStiffness->value, 0.05f);
	const int whole = src.numNodes - 1;
	fr.keepU = Com_Clamp(0.0f, (float)whole, keepU);
	fr.segments = Com_Clampi(1, whole, (int)ceilf(fr.keepU - 1e-4f));
	fr.firstBone = src.firstBone;
	for (int k = 0; k <= fr.segments; ++k)
		fr.X[k] = LocalToWorld(inst, src.nodes[k]);
	{
		const int j = fr.segments - 1;
		const Vec3 seg = fr.X[j + 1] - fr.X[j];
		const float len = MAX(Length(seg), 1e-3f);
		const float part = MAX(Com_Clamp(0.0f, 1.0f, fr.keepU - j), MIN(1.0f, 1.0f / len));
		fr.X[j + 1] = fr.X[j] + seg * part;
	}
	{
		// lineal density tapering to the tip, lumped into the nodes
		float mass[PLANTSKEL_MAX_NODES] = {};
		float total = 0.0f;
		for (int j = 0; j < fr.segments; ++j)
		{
			fr.D[j] = fr.X[j + 1] - fr.X[j];
			fr.L[j] = MAX(Length(fr.D[j]), 1e-3f);
			total += fr.L[j];
		}
		for (int j = 0; j < fr.segments; ++j)
		{
			fr.T[j] = fr.D[j] * (1.0f / fr.L[j]);
			const Vec3 lateral = inst.axis[0] * src.lateral[j][0] + inst.axis[1] * src.lateral[j][1] +
				inst.axis[2] * src.lateral[j][2];
			fr.B[j] = Normalized(lateral - fr.T[j] * Dot(lateral, fr.T[j]),
				Normalized(Cross(V3(0.0f, 0.0f, 1.0f), fr.T[j]), V3(1.0f, 0.0f, 0.0f)));
			fr.N[j] = Cross(fr.T[j], fr.B[j]);
		}
		float s = 0.0f;
		for (int j = 0; j < fr.segments; ++j)
		{
			const float sMid = (s + 0.5f * fr.L[j]) / total;
			const float density = src.frondClass == PLANTFROND_GRASS ? 1.0f - 0.3f * sMid : 1.0f - 0.6f * sMid;
			mass[j] += 0.5f * fr.L[j] * density;
			mass[j + 1] += 0.5f * fr.L[j] * density;
			s += fr.L[j];
		}
		fr.invMass[0] = 0.0f;
		for (int k = 1; k <= fr.segments; ++k)
			fr.invMass[k] = 1.0f / MAX(mass[k], 1e-3f);

		// joint stiffness from a natural frequency profile along the frond:
		// k = m_carried (2 pi f)^2, compliance 1 / k
		const float rootFreq = src.frondClass == PLANTFROND_GRASS ? 2.6f : 4.0f;
		const float tipFreq = src.frondClass == PLANTFROND_GRASS ? 1.8f : 1.6f;
		s = 0.0f;
		for (int j = 0; j < fr.segments; ++j)
		{
			const float sj = s / total;
			float carried = 0.0f;
			for (int k = j + 1; k <= fr.segments; ++k)
				carried += mass[k];
			const float freq = (rootFreq + (tipFreq - rootFreq) * sj) * sqrtf(stiffness);
			const float omega = 2.0f * (float)M_PI * freq;
			fr.alpha[j] = 1.0f / (MAX(carried, 1e-3f) * omega * omega);
			// a lamina bends out of its plane, hardly within it (its width);
			// the round petiole / base turns either way
			const float e = Com_Clamp(0.0f, 1.0f, sj / 0.3f);
			fr.lateralStiffness[j] = 1.0f + 24.0f * e * e * (3.0f - 2.0f * e);
			// the base turns furthest (the whole frond swings aside), the
			// joints along it bend less
			fr.maxCos[j] = cosf(DEG2RAD(j == 0 ? 75.0f : 55.0f));
			// part of the lamina width: the leaflets give way a little
			fr.margin[j] = MIN(0.25f * (src.halfWidth[j] + src.halfWidth[j + 1]) * scale, 12.0f);
			s += fr.L[j];
		}
		// internal damping ratio ~0.3 at the tip frequency (one visible swing back)
		fr.damping = 2.0f * 0.3f * 2.0f * (float)M_PI * tipFreq * sqrtf(stiffness);

		// the sides a body pushes the frond to: across a lying frond, along the
		// lamina normal of a standing one (a grass card)
		const Vec3 chord = fr.X[fr.segments] - fr.X[0];
		const float horizontal = sqrtf(chord.x * chord.x + chord.y * chord.y);
		if (horizontal > 0.3f * Length(chord))
			fr.side = V3(-chord.y / horizontal, chord.x / horizontal, 0.0f);
		else
			fr.side = Normalized(V3(fr.N[0].x, fr.N[0].y, 0.0f), V3(1.0f, 0.0f, 0.0f));
	}
}

static PlantInstance *FindInstance(const trRefEntity_t *ent, const mdvModel_t *mdv)
{
	const uint64_t key = PlantKey(ent->e.hModel, ent->e.origin);
	auto it = s_ps.lookup.find(key);
	PlantInstance *inst = it != s_ps.lookup.end() ? it->second : NULL;
	// same place, other model or turned: start over
	if (inst && (inst->skel != mdv->plantSkel || inst->hModel != ent->e.hModel ||
		fabsf(inst->axis[0].x - ent->e.axis[0][0]) > 1e-3f || fabsf(inst->axis[0].y - ent->e.axis[0][1]) > 1e-3f ||
		fabsf(inst->axis[2].z - ent->e.axis[2][2]) > 1e-3f))
		inst->valid = false;
	if (!inst)
	{
		inst = new PlantInstance();
		inst->valid = false;
		s_ps.instances.push_back(inst);
		s_ps.lookup.emplace(key, inst);
	}
	if (!inst->valid)
		InitInstance(*inst, ent, mdv);
	return inst;
}

// Do not create a far-away instance merely to draw a rest plant.  An existing
// moving instance is useful, though: keeping its last pose avoids a bent ->
// rest -> bent pop while it is outside the simulation range.
static PlantInstance *FindExistingInstance(const trRefEntity_t *ent, const mdvModel_t *mdv)
{
	const uint64_t key = PlantKey(ent->e.hModel, ent->e.origin);
	auto it = s_ps.lookup.find(key);
	if (it == s_ps.lookup.end())
		return NULL;
	PlantInstance *inst = it->second;
	if (!inst->valid || inst->skel != mdv->plantSkel || inst->hModel != ent->e.hModel ||
		fabsf(inst->axis[0].x - ent->e.axis[0][0]) > 1e-3f || fabsf(inst->axis[0].y - ent->e.axis[0][1]) > 1e-3f ||
		fabsf(inst->axis[2].z - ent->e.axis[2][2]) > 1e-3f)
		return NULL;
	return inst;
}

void R_PlantSimReset(void)
{
	for (PlantInstance *inst : s_ps.instances)
	{
		R_PlantCutFree(inst->cut);
		delete inst;
	}
	s_ps.instances.clear();
	s_ps.lookup.clear();
	s_ps.numCut = 0;
	s_ps.arena.clear();
	s_ps.arenaFrame = -1;
	s_ps.uploaded = 0;
	s_ps.capsuleFrame = -1;
	R_PlantCutReset();
}

// drop the instances not seen for a while (another area, another map)
static void EvictInstances(void)
{
	for (size_t i = 0; i < s_ps.instances.size(); )
	{
		PlantInstance *inst = s_ps.instances[i];
		// a cut plant stays cut
		if (tr.frameCount - inst->seenFrame > PLANTSIM_EVICT_FRAMES && !R_PlantCutDrawOf(inst->cut))
		{
			for (auto it = s_ps.lookup.begin(); it != s_ps.lookup.end(); ++it)
			{
				if (it->second == inst)
				{
					s_ps.lookup.erase(it);
					break;
				}
			}
			R_PlantCutFree(inst->cut);
			delete inst;
			s_ps.instances[i] = s_ps.instances.back();
			s_ps.instances.pop_back();
		}
		else
			++i;
	}
}

/*
=============================================================

Per frame

=============================================================
*/

static void BeginFrame(void)
{
	if (s_ps.arenaFrame == tr.frameCount)
		return;
	if (PlantDebug() & PLANTDEBUG_STATS)
	{
		static int lastPrint = 0;
		if (ri.Milliseconds() - lastPrint > 1000)
		{
			lastPrint = ri.Milliseconds();
			ri.Printf(PRINT_ALL, "plant sim: %d instances, %d stepped, %d texels, %d us\n",
				(int)s_ps.instances.size(), s_ps.simulated, (int)s_ps.arena.size() / 4, s_ps.lastMicroseconds);
		}
	}
	s_ps.arenaFrame = tr.frameCount;
	s_ps.arena.clear();
	s_ps.uploaded = 0;
	s_ps.simulated = 0;
	s_ps.lastMicroseconds = 0;
	if ((tr.frameCount & 63) == 0)
		EvictInstances();
}

static void LatchCapsules(void)
{
	if (s_ps.capsuleFrame == tr.frameCount)
		return;
	s_ps.capsuleFrame = tr.frameCount;
	s_ps.numCapsules = 0;
	const vec4_t *colliders = NULL;
	const int count = R_FoliageInteractionColliders(&colliders);
	for (int i = 0; i < count && i < MAX_FOLIAGE_INTERACTORS; ++i)
	{
		const float *axis = colliders[i * 2 + 0];
		const float *body = colliders[i * 2 + 1];
		Capsule &c = s_ps.capsules[s_ps.numCapsules++];
		c.radius = body[0];
		const float z0 = axis[2] + c.radius;
		const float z1 = MAX(axis[3] - c.radius, z0);
		c.a0 = V3(axis[0], axis[1], z0);
		c.a1 = V3(axis[0], axis[1], z1);
		c.velocity = V3(body[1], body[2], 0.0f);
	}
}

static bool CapsuleNear(const Capsule &c, Vec3 center, float radius)
{
	float s;
	Vec3 a, p;
	ClosestSegmentSegment(center, center, c.a0, c.a1, s, p, a);
	const float reach = radius + c.radius + 8.0f;
	return Dot(p - a, p - a) < reach * reach;
}

// one sub step of one frond: XPBD, h seconds, with the capsules near the plant
static void StepFrond(FrondSim &fr, const PlantInstance &inst, float h, int iterations,
	const Capsule *capsules, int numCapsules, Vec3 windAccel)
{
	const int K = fr.segments;
	Vec3 p[PLANTSKEL_MAX_NODES];
	float lambda[PLANTSKEL_MAX_NODES - 1][3];

	// external forces, damping, prediction
	const float decay = expf(-(fr.damping + PLANTSIM_AIR_DRAG) * h);
	for (int k = 1; k <= K; ++k)
	{
		// wind acts across the segment that ends at the node
		const Vec3 dir = Normalized(fr.x[k] - fr.x[k - 1], V3(0.0f, 0.0f, 1.0f));
		const Vec3 across = windAccel - dir * Dot(windAccel, dir);
		fr.v[k] += across * h;
		fr.v[k] = fr.v[k] * decay;
		p[k] = fr.x[k] + fr.v[k] * h;
	}
	p[0] = fr.X[0];
	for (int j = 0; j < K; ++j)
		lambda[j][0] = lambda[j][1] = lambda[j][2] = 0.0f;

	// rest arc length at every node
	float arc[PLANTSKEL_MAX_NODES];
	arc[0] = 0.0f;
	for (int j = 0; j < K; ++j)
		arc[j + 1] = arc[j] + fr.L[j];

	bool contact = false;
	bool splayed = false;
	bool touched[PLANTSKEL_MAX_NODES] = {};		// node pushed by a body (friction)
	Vec3 touchVelocity[PLANTSKEL_MAX_NODES];
	auto solveContact = [&]() {
		// Character contact: one side for the whole frond.
		for (int ci = 0; ci < numCapsules; ++ci)
		{
			const Capsule &cap = capsules[ci];
			// A character standing in the plant has the pinned base inside its
			// body: no pose of the frond clears it, and pushing it to one side
			// (or away from the axis) has no solution either, so it spins
			// around the leg for as long as the character stands there.  Such
			// a frond is splayed instead: every node is pushed along its own
			// line from the base out to the edge of the body, at most as far
			// as its arc allows (a 45 degree droop), which always has a pose.
			// Hysteresis: a body swaying at the base would flip the frond
			// between the two models.
			const Vec3 b = V3(fr.X[0].x - cap.a0.x, fr.X[0].y - cap.a0.y, 0.0f);
			const float body = cap.radius + fr.margin[0];
			const float splayReach = fr.splayed ? body + 0.5f * cap.radius : body;
			if (Dot(b, b) < splayReach * splayReach && fr.X[0].z < cap.a1.z + cap.radius &&
				fr.X[0].z > cap.a0.z - 2.0f * cap.radius)
			{
				splayed = true;
				const Vec3 restHeading = Normalized(V3(fr.side.y, -fr.side.x, 0.0f), V3(1.0f, 0.0f, 0.0f));
				for (int k = 1; k <= K; ++k)
				{
					if (p[k].z > cap.a1.z + cap.radius)
						continue;
					const Vec3 q = V3(p[k].x - p[0].x, p[k].y - p[0].y, 0.0f);
					const float l = Length(q);
					const Vec3 u = Normalized(q, restHeading);
					// the line from the base crosses the body circle at
					// t0 .. t1 (the base is outside it in the hysteresis band:
					// a node short of the body is free)
					const float r = cap.radius + fr.margin[k - 1];
					const float bu = Dot(b, u);
					const float disc = bu * bu - Dot(b, b) + r * r;
					if (disc <= 0.0f)
						continue;
					const float t0 = -bu - sqrtf(disc), t1 = -bu + sqrtf(disc);
					if (t1 <= 0.0f || l <= t0)
						continue;
					const float need = MIN(t1, 0.7f * arc[k]);
					if (l >= need)
						continue;
					contact = true;
					touched[k] = true;
					touchVelocity[k] = cap.velocity;
					p[k].x = p[0].x + u.x * need;
					p[k].y = p[0].y + u.y * need;
				}
				continue;
			}
			for (int j = 0; j < K; ++j)
			{
				float s;
				Vec3 c, a;
				ClosestSegmentSegment(p[j], p[j + 1], cap.a0, cap.a1, s, c, a);
				const float reach = cap.radius + fr.margin[j];
				Vec3 d = c - a;
				// the legs reach the feet: a vertical cylinder down there, not
				// the bottom of the capsule pressing the frond into the ground
				if (c.z < cap.a0.z && c.z > cap.a0.z - cap.radius)
					d.z = 0.0f;
				if (Dot(d, d) >= reach * reach)
					continue;
				contact = true;
				if (s < 0.999f)
				{
					touched[j] = true;
					touchVelocity[j] = cap.velocity;
				}
				if (s > 0.001f)
				{
					touched[j + 1] = true;
					touchVelocity[j + 1] = cap.velocity;
				}
				if (fr.contactSide == 0)
				{
					const float g = Dot(V3(d.x, d.y, 0.0f), fr.side);
					if (fabsf(g) > 0.3f * cap.radius)
						fr.contactSide = g > 0.0f ? 1 : -1;
					else
						fr.contactSide = Dot(cap.velocity, fr.side) >= 0.0f ? 1 : -1;
				}
				const Vec3 sideDir = fr.side * (float)fr.contactSide;
				Vec3 n;
				const float horizontal = sqrtf(d.x * d.x + d.y * d.y);
				if (horizontal < 1e-3f * reach && fabsf(d.z) < 1e-3f * reach)
					n = sideDir;
				else
				{
					n = Normalized(d, sideDir);
					const float wrong = Dot(n, sideDir);
					if (wrong < 0.0f)
						n = Normalized(n - sideDir * (2.0f * wrong), sideDir);
				}
				const float penetration = reach - Dot(d, n);
				if (penetration <= 0.0f)
					continue;
				const float w0 = fr.invMass[j] * (1.0f - s);
				const float w1 = fr.invMass[j + 1] * s;
				const float wsum = w0 * (1.0f - s) + w1 * s;
				if (wsum <= 1e-9f)
					continue;
				const float scale = penetration / wsum;
				p[j] += n * (scale * w0);
				p[j + 1] += n * (scale * w1);
			}
		}
	};
	auto solveGround = [&]() {
		for (int k = 1; k <= K; ++k)
			p[k].z = MAX(p[k].z, inst.groundZ);
	};
	auto solveLengths = [&]() {
		for (int j = 0; j < K; ++j)
		{
			const Vec3 d = p[j + 1] - p[j];
			const float len = Length(d);
			const float wsum = fr.invMass[j] + fr.invMass[j + 1];
			if (len < 1e-6f || wsum <= 0.0f)
				continue;
			const Vec3 corr = d * ((len - fr.L[j]) / (wsum * len));
			p[j] += corr * fr.invMass[j];
			p[j + 1] -= corr * fr.invMass[j + 1];
		}
	};
	for (int it = 0; it < iterations; ++it)
	{
		// bending: the next node towards the rest direction in the frame of
		// the previous segment, per axis of the transported rest basis (the
		// lamina is stiff across, soft out of its plane)
		Quat frame = QUAT_IDENTITY;
		for (int j = 0; j < K; ++j)
		{
			const Vec3 target = p[j] + Rotate(frame, fr.D[j]);
			const Vec3 C = p[j + 1] - target;
			const float wsum = fr.invMass[j] + fr.invMass[j + 1];
			const Vec3 basis[3] = { Rotate(frame, fr.T[j]), Rotate(frame, fr.B[j]), Rotate(frame, fr.N[j]) };
			const float stiffness[3] = { 1.0f, fr.lateralStiffness[j], 1.0f };
			for (int axis = 0; axis < 3; ++axis)
			{
				const float at = fr.alpha[j] / (stiffness[axis] * h * h);
				const float dl = (-Dot(C, basis[axis]) - lambda[j][axis] * at) / (wsum + at);
				lambda[j][axis] += dl;
				p[j + 1] += basis[axis] * (dl * fr.invMass[j + 1]);
				p[j] -= basis[axis] * (dl * fr.invMass[j]);
			}

			// joint limit around the rest direction of this joint; not on a
			// node a body holds (both are hard: the contact wins the last
			// pass, the limit snapped it back in the next step, a cycle)
			const Vec3 restDir = Normalized(Rotate(frame, fr.D[j]), V3(0.0f, 0.0f, 1.0f));
			Vec3 cur = p[j + 1] - p[j];
			const Vec3 curDir = Normalized(cur, restDir);
			const float c = Dot(curDir, restDir);
			if (c < fr.maxCos[j] && !(fr.pressed & (1u << (j + 1))))
			{
				Vec3 ortho = curDir - restDir * c;
				ortho = Normalized(ortho, Normalized(Cross(restDir, V3(0.0f, 0.0f, 1.0f)), V3(1.0f, 0.0f, 0.0f)));
				const float sinMax = sqrtf(MAX(1.0f - fr.maxCos[j] * fr.maxCos[j], 0.0f));
				const Vec3 limited = restDir * fr.maxCos[j] + ortho * sinMax;
				const float len = Length(cur);
				p[j + 1] = p[j] + limited * len;
			}
			frame = Mul(Between(Rotate(frame, fr.D[j]), p[j + 1] - p[j]), frame);
		}

		solveContact();
		solveGround();
		solveLengths();
	}

	// A hard length projection can pull a just-separated segment back into a
	// capsule (or below the ground).  End on the visually important unilateral
	// constraints; this pass only runs for plants near an interactor.
	if (numCapsules > 0)
	{
		solveLengths();
		solveContact();
		solveGround();
	}

	// friction on the body: a node held against a character moves with it,
	// the contact projection does not turn into a swing of its own
	const float friction = expf(-PLANTSIM_CONTACT_FRICTION * h);
	fr.splayed = splayed;
	fr.pressed = 0;
	for (int k = 1; k <= K; ++k)
	{
		fr.v[k] = (p[k] - fr.x[k]) * (1.0f / h);
		if (touched[k])
		{
			fr.v[k] = touchVelocity[k] + (fr.v[k] - touchVelocity[k]) * friction;
			fr.pressed |= 1u << k;
		}
		fr.x[k] = p[k];
	}
	fr.x[0] = fr.X[0];
	fr.v[0] = V3(0.0f, 0.0f, 0.0f);

	if (contact)
	{
		fr.contact = true;
		fr.contactTimer = 0.3f;
	}
	else
	{
		fr.contactTimer -= h;
		if (fr.contactTimer <= 0.0f)
			fr.contactSide = 0;
	}
}

// bones of the current state; true when the plant is at rest
static bool WriteBones(PlantInstance &inst, float renderAlpha)
{
	bool rest = true;
	float *bones = inst.bones.data();
	for (const FrondSim &fr : inst.fronds)
	{
		Vec3 renderX[PLANTSKEL_MAX_NODES];
		for (int k = 0; k <= fr.segments; ++k)
			renderX[k] = fr.prevX[k] + (fr.x[k] - fr.prevX[k]) * renderAlpha;
		Quat frame = QUAT_IDENTITY;
		for (int j = 0; j < fr.segments; ++j)
		{
			frame = Mul(Between(Rotate(frame, fr.D[j]), renderX[j + 1] - renderX[j]), frame);
			const Vec3 t = renderX[j] - Rotate(frame, fr.X[j]);
			float *b = bones + (fr.firstBone + j) * 8;
			b[0] = frame.x; b[1] = frame.y; b[2] = frame.z; b[3] = frame.w;
			b[4] = t.x; b[5] = t.y; b[6] = t.z; b[7] = 0.0f;
		}
		for (int k = 1; k <= fr.segments; ++k)
		{
			if (Dot(fr.x[k] - fr.X[k], fr.x[k] - fr.X[k]) > 0.05f * 0.05f || Dot(fr.v[k], fr.v[k]) > 0.25f)
				rest = false;
		}
	}
	return rest;
}

// a Force push / pull front reaches the plant this frame (or did within the
// last 0.25 s: the frame time step)
static bool ForceWaveNear(const PlantInstance &inst)
{
	const float now = tr.refdef.floatTime;
	return R_FoliageForceWavesEnabled() &&
		R_FoliageForceWaveReaches(&inst.center.x, inst.radius, now - 0.25f, now);
}

static void SimulateInstance(PlantInstance &inst, float distance, bool waveNear)
{
	const int now = tr.refdef.time;
	const bool consecutive = inst.simFrame == tr.frameCount - 1 && now >= inst.lastTime &&
		now - inst.lastTime <= 250;
	float dt = consecutive ? (now - inst.lastTime) * 0.001f : 0.0f;
	inst.lastTime = now;
	inst.simFrame = tr.frameCount;

	// the previous frame's pose (no history: no motion)
	if (consecutive)
		inst.prevBones = inst.bones;

	// r_foliageInteractionDebug 4: interaction only
	const bool wind = r_plantWind->integer && r_plantWindStrength->value > 0.0f &&
		!(R_FoliageInteractionActive() && (r_foliageInteractionDebug->integer & 4));
	Capsule nearCapsules[MAX_FOLIAGE_INTERACTORS];
	int numNear = 0;
	for (int i = 0; i < s_ps.numCapsules; ++i)
	{
		if (CapsuleNear(s_ps.capsules[i], inst.center, inst.radius))
			nearCapsules[numNear++] = s_ps.capsules[i];
	}

	bool touched = false;
	for (FrondSim &fr : inst.fronds)
	{
		touched = touched || fr.contact || fr.contactSide != 0;
		fr.contact = false;
	}

	// asleep: nothing moves it and it is back at rest
	if (!wind && numNear == 0 && inst.atRest && !touched && !waveNear)
	{
		if (!consecutive)
			inst.prevBones = inst.bones;
		return;
	}

	if (dt > 0.0f)
	{
		const bool detailed = numNear > 0 || waveNear || distance <= r_plantSimDistance->value;
		int steps;
		float h;
		int iterations;
		float renderAlpha;
		if (detailed)
		{
			inst.accumulator += dt;
			steps = (int)(inst.accumulator / PLANTSIM_STEP);
			inst.accumulator -= steps * PLANTSIM_STEP;
			if (steps > PLANTSIM_MAX_STEPS)
			{
				steps = PLANTSIM_MAX_STEPS;
				inst.accumulator = 0.0f;
			}
			h = PLANTSIM_STEP;
			iterations = PLANTSIM_ITERATIONS;
			renderAlpha = inst.accumulator / PLANTSIM_STEP;
		}
		else
		{
			steps = 1;
			h = MIN(dt, PLANTSIM_COARSE_STEP);
			iterations = PLANTSIM_COARSE_ITERATIONS;
			inst.accumulator = 0.0f;
			renderAlpha = 1.0f;
		}

		const float yaw = DEG2RAD(r_foliageWindDirection->value);
		const float dirX = cosf(yaw), dirY = sinf(yaw);
		const float strength = wind ? r_plantWindStrength->value : 0.0f;
		for (int step = 0; step < steps; ++step)
		{
			const float t = tr.refdef.floatTime - (steps - 1 - step) * h;
			// the kicks over [waveFrom, t]: back to back over the steps of all
			// frames, so the whole kick arrives whatever the step pattern
			if (inst.waveTime > t || t - inst.waveTime > 0.25f)
				inst.waveTime = t - h;
			const float waveFrom = inst.waveTime;
			inst.waveTime = t;
			for (FrondSim &fr : inst.fronds)
			{
				for (int k = 0; k <= fr.segments; ++k)
					fr.prevX[k] = fr.x[k];
				// Force push / pull: the part of the kick the front gives each
				// node during this step
				if (waveNear)
				{
					for (int k = 1; k <= fr.segments; ++k)
					{
						float dv[3] = { 0.0f, 0.0f, 0.0f };
						if (R_FoliageForceWaveImpulse(&fr.x[k].x, waveFrom, t, dv))
							fr.v[k] += V3(dv[0], dv[1], dv[2]);
					}
				}
				Vec3 windAccel = V3(0.0f, 0.0f, 0.0f);
				if (strength > 0.0f)
				{
					float lean, flutter;
					const float seed = WindHash((int)fr.X[0].x, (int)fr.X[0].y);
					WindAt(fr.X[0].x, fr.X[0].y, seed, t, dirX, dirY, lean, flutter);
					windAccel = V3(dirX * lean - dirY * flutter, dirY * lean + dirX * flutter, 0.0f) *
						(PLANTSIM_WIND_ACCEL * strength);
				}
				StepFrond(fr, inst, h, iterations, nearCapsules, numNear, windAccel);
			}
		}
		inst.atRest = WriteBones(inst, renderAlpha) && !wind;
	}
	else
		inst.atRest = WriteBones(inst, 1.0f) && !wind;
	if (inst.atRest)
	{
		// snap exactly to rest (no creeping)
		for (FrondSim &fr : inst.fronds)
		{
			for (int k = 0; k <= fr.segments; ++k)
			{
				fr.x[k] = fr.X[k];
				fr.prevX[k] = fr.X[k];
				fr.v[k] = V3(0.0f, 0.0f, 0.0f);
			}
		}
		SetRestBones(inst.bones);
	}
	if (!consecutive)
		inst.prevBones = inst.bones;
	++s_ps.simulated;
}

/*
=============
R_PlantSimAcquire

R_AddMD3Surfaces of a plant with a frond skeleton, every view: steps the plant
once per frame and puts its bones (current and previous) into this frame's
bone buffer. Sets ent->plantBoneOffset (first texel, -1: drawn at rest).
=============
*/
void R_PlantSimAcquire(trRefEntity_t *ent, const mdvModel_t *mdv)
{
	ent->plantBoneOffset = -1;
	ent->plantBoneTexels = 0;
	ent->plantCutDraw = NULL;
	if (!mdv->plantSkel || (tr.refdef.rdflags & RDF_NOWORLDMODEL))
		return;
	if (!R_PlantSimActive())
	{
		// a cut plant stays cut (at rest)
		PlantInstance *inst = s_ps.numCut > 0 ? FindExistingInstance(ent, mdv) : NULL;
		if (inst && inst->cut)
		{
			inst->seenFrame = tr.frameCount;
			ent->plantCutDraw = R_PlantCutDrawOf(inst->cut);
		}
		return;
	}

	BeginFrame();
	LatchCapsules();

	const float distance = Distance(tr.refdef.vieworg, ent->e.origin);
	const bool beyondWind = distance > r_plantWindDistance->value;
	// the waves reach farther than the characters: a plant hit by one gets an
	// instance even beyond r_plantWindDistance
	const bool wavesLive = R_FoliageForceWaveReaches(ent->e.origin, 512.0f,
		tr.refdef.floatTime - 0.25f, tr.refdef.floatTime) && R_FoliageForceWavesEnabled();
	PlantInstance *inst = beyondWind && s_ps.numCapsules == 0 && !wavesLive ?
		FindExistingInstance(ent, mdv) : FindInstance(ent, mdv);
	if (!inst)
		return;
	inst->refEnt = ent->e;
	ent->plantCutDraw = R_PlantCutDrawOf(inst->cut);
	if (inst->seenFrame != tr.frameCount)
	{
		inst->seenFrame = tr.frameCount;
		inst->arenaOffset = -1;
		const bool nearCharacter = [&]() {
			for (int i = 0; i < s_ps.numCapsules; ++i)
				if (CapsuleNear(s_ps.capsules[i], inst->center, inst->radius))
					return true;
			return false;
		}();
		// touched plants always step, like the ones near a character
		const bool waveNear = wavesLive && ForceWaveNear(*inst);
		const bool simulate = nearCharacter || waveNear || (!beyondWind &&
			s_ps.simulated < MAX(r_plantSimMaxPlants->integer, 0));
		if (simulate)
		{
			const auto start = std::chrono::steady_clock::now();
			SimulateInstance(*inst, distance, waveNear);
			s_ps.lastMicroseconds += (int)std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - start).count();
		}
		else
		{
			// The current pose was also the pose rendered last frame.  Reusing an
			// older prevBones block would manufacture motion on every skipped frame.
			inst->prevBones = inst->bones;
		}

		// at rest in both poses: drawn without skinning
		bool still = inst->atRest;
		if (still)
		{
			for (size_t i = 0; i < inst->prevBones.size() && still; ++i)
				still = inst->prevBones[i] == inst->bones[i];
		}
		if (still)
			return;

		const int texels = inst->skel->numBones * 2;
		const int first = (int)s_ps.arena.size() / 4;
		if (first + texels * 2 > PLANTSIM_ARENA_TEXELS)
		{
			if (!s_ps.overflowWarned)
			{
				s_ps.overflowWarned = true;
				ri.Printf(PRINT_WARNING, "plant sim: bone buffer full, lower r_plantSimMaxPlants\n");
			}
			return;
		}
		s_ps.arena.insert(s_ps.arena.end(), inst->bones.begin(), inst->bones.end());
		s_ps.arena.insert(s_ps.arena.end(), inst->prevBones.begin(), inst->prevBones.end());
		inst->arenaOffset = first;
	}

	if (inst->arenaOffset >= 0)
	{
		ent->plantBoneOffset = inst->arenaOffset;
		ent->plantBoneTexels = inst->skel->numBones * 2;
	}
}

/*
=============================================================

GPU

=============================================================
*/

typedef struct plantBonesCommand_s
{
	int commandId;
	int first;		// texel
	int count;
} plantBonesCommand_t;

static bool RB_PlantSimCreateBuffers(void)
{
	if (s_ps.created)
		return true;
	if (s_ps.failed)
		return false;
	if (!qglTexBuffer || glRefConfig.maxTextureBufferSize < PLANTSIM_ARENA_TEXELS)
	{
		s_ps.failed = true;
		ri.Printf(PRINT_WARNING, "plant sim: buffer textures unavailable, plants are drawn at rest\n");
		return false;
	}
	qglGenBuffers(MAX_FRAMES, s_ps.buffers);
	for (int f = 0; f < MAX_FRAMES; ++f)
	{
		qglBindBuffer(GL_TEXTURE_BUFFER, s_ps.buffers[f]);
		qglBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)PLANTSIM_ARENA_TEXELS * 16, NULL, GL_DYNAMIC_DRAW);
		image_t *image = &s_ps.images[f];
		Com_Memset(image, 0, sizeof(*image));
		Q_strncpyz(image->imgName, va("*plantBones%d", f), sizeof(image->imgName));
		image->flags = IMGFLAG_TEXBUFFER;
		qglGenTextures(1, &image->texnum);
		GL_BindToTMU(image, TB_PLANTBONES);
		qglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, s_ps.buffers[f]);
	}
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);
	GL_SelectTexture(0);
	s_ps.created = true;
	return true;
}

void R_ShutdownPlantSim(void)
{
	if (s_ps.created)
	{
		for (int f = 0; f < MAX_FRAMES; ++f)
		{
			for (int u = 0; u < MAX_TEXTURE_UNITS; ++u)
			{
				if (glState.currenttextures[u] == (int)s_ps.images[f].texnum)
					glState.currenttextures[u] = 0;
			}
			qglDeleteTextures(1, &s_ps.images[f].texnum);
		}
		qglDeleteBuffers(MAX_FRAMES, s_ps.buffers);
	}
	s_ps.created = false;
	s_ps.failed = false;
	R_PlantSimReset();
}

static int RB_PlantSimFrameSlot(void)
{
	return (int)((backEndData->currentFrame - backEndData->frames) % MAX_FRAMES);
}

// R_AddDrawSurfCmd: the bones added by this view, in front of its draws
void R_PlantSimQueueUpload(void)
{
	if (s_ps.arenaFrame != tr.frameCount)
		return;
	const int total = (int)s_ps.arena.size() / 4;
	if (total <= s_ps.uploaded)
		return;
	plantBonesCommand_t *cmd = (plantBonesCommand_t *)R_GetCommandBuffer(sizeof(*cmd));
	if (!cmd)
		return;
	cmd->commandId = RC_PLANT_BONES;
	cmd->first = s_ps.uploaded;
	cmd->count = total - s_ps.uploaded;
	s_ps.uploaded = total;
}

const void *RB_PlantBonesCommand(const void *data)
{
	const plantBonesCommand_t *cmd = (const plantBonesCommand_t *)data;
	// the arena of this frame is only appended to until the next frame begins
	if (RB_PlantSimCreateBuffers() && cmd->first + cmd->count <= (int)s_ps.arena.size() / 4)
	{
		qglBindBuffer(GL_TEXTURE_BUFFER, s_ps.buffers[RB_PlantSimFrameSlot()]);
		qglBufferSubData(GL_TEXTURE_BUFFER, (GLintptr)cmd->first * 16, (GLsizeiptr)cmd->count * 16,
			s_ps.arena.data() + cmd->first * 4);
		qglBindBuffer(GL_TEXTURE_BUFFER, 0);
	}
	return (const void *)(cmd + 1);
}

// every draw of a FOLIAGE_PLANT surface (RB_SetFoliageMotionUniforms)
bool RB_PlantSkinUniforms(vec4_t out)
{
	const trRefEntity_t *ent = backEnd.currentEntity;
	VectorSet4(out, 0.0f, 0.0f, 0.0f, 0.0f);
	if (!ent || ent == &tr.worldEntity || ent->plantBoneOffset < 0 || !s_ps.created)
		return false;
	int debug = 0;
	if (!backEnd.depthFill && !(backEnd.viewParms.flags & VPF_DEPTHSHADOW))
	{
		if (PlantDebug() & PLANTDEBUG_SEGMENTS)
			debug = 2;
		else if (PlantDebug() & PLANTDEBUG_BONES)
			debug = 4;
	}
	VectorSet4(out, (float)ent->plantBoneOffset, (float)ent->plantBoneTexels, 1.0f, (float)debug);
	return true;
}

void RB_AddPlantSkinSampler(SamplerBindingsWriter &writer, uint8_t foliageMotion)
{
	const trRefEntity_t *ent = backEnd.currentEntity;
	if (foliageMotion != FOLIAGE_PLANT || !ent || ent == &tr.worldEntity || ent->plantBoneOffset < 0 || !s_ps.created)
		return;
	writer.AddStaticImage(&s_ps.images[RB_PlantSimFrameSlot()], TB_PLANTBONES);
}

// R_CullModel: a frond turns around its base, at most its length away
float R_PlantSimCullMargin(const mdvModel_t *model)
{
	if (!model || !model->plantSkel || !R_PlantSimActive())
		return 0.0f;
	return model->plantSkel->maxLength * 0.75f;
}

/*
=============
R_PlantSimDebugDraw

r_plantDebug 1 / 8 (RE_BeginScene): the frond centerlines of the plants of
the previous frame, white nodes; 8: yellow / cyan by contact side
=============
*/
void R_PlantSimDebugDraw(const refdef_t *fd)
{
	const int debug = PlantDebug();
	if (!(debug & (PLANTDEBUG_CENTERLINES | PLANTDEBUG_CONTACT)) || (fd->rdflags & RDF_NOWORLDMODEL))
		return;
	static const byte free_[4] = { 120, 255, 120, 255 };
	static const byte left[4] = { 255, 220, 40, 255 };
	static const byte right[4] = { 40, 220, 255, 255 };
	static const byte node[4] = { 255, 255, 255, 255 };
	for (const PlantInstance *inst : s_ps.instances)
	{
		if (tr.frameCount - inst->seenFrame > 1)
			continue;
		for (const FrondSim &fr : inst->fronds)
		{
			const byte *color = free_;
			if ((debug & PLANTDEBUG_CONTACT) && fr.contactSide != 0)
				color = fr.contactSide > 0 ? left : right;
			for (int j = 0; j < fr.segments; ++j)
			{
				const vec3_t a = { fr.x[j].x, fr.x[j].y, fr.x[j].z };
				const vec3_t b = { fr.x[j + 1].x, fr.x[j + 1].y, fr.x[j + 1].z };
				R_FoliageDebugSegment(fd, a, b, color);
				const vec3_t n0 = { b[0], b[1], b[2] - 1.0f };
				const vec3_t n1 = { b[0], b[1], b[2] + 1.0f };
				R_FoliageDebugSegment(fd, n0, n1, node);
			}
		}
	}
}

void R_PlantSimInfo_f(void)
{
	ri.Printf(PRINT_ALL, "plant sim: %s, %d instances (%d cut), %d stepped last frame, %d bone texels, buffers %s\n",
		R_PlantSimActive() ? "on" : "off", (int)s_ps.instances.size(), s_ps.numCut, s_ps.simulated,
		(int)s_ps.arena.size() / 4, s_ps.created ? "created" : (s_ps.failed ? "failed" : "not yet"));
	R_PlantCutInfo();
	for (const PlantInstance *inst : s_ps.instances)
	{
		int contacts = 0, shortened = 0;
		for (size_t f = 0; f < inst->fronds.size(); ++f)
		{
			contacts += inst->fronds[f].contactSide != 0;
			shortened += inst->fronds[f].keepU < (float)(inst->skel->fronds[f].numNodes - 1);
		}
		ri.Printf(PRINT_ALL, "  %s (%.0f %.0f %.0f): %d fronds, %d bones, %s, %d in contact, seen %d frames ago",
			R_GetModelByHandle(inst->hModel)->name, inst->origin.x, inst->origin.y, inst->origin.z,
			(int)inst->fronds.size(), inst->skel->numBones, inst->atRest ? "rest" : "moving",
			contacts, tr.frameCount - inst->seenFrame);
		if (R_PlantCutDrawOf(inst->cut))
			ri.Printf(PRINT_ALL, ", cut: %d triangles, %d fronds shortened", R_PlantCutTriangles(inst->cut), shortened);
		ri.Printf(PRINT_ALL, "\n");
	}
}

/*
=============================================================

Saber cut (tr_plantcut.cpp)

=============================================================
*/

// the blade step passes within the plant's bounding sphere (its bounds grown
// by the sphere)
static bool StrikeNear(const foliageSaberCut_t &strike, const Vec3 &center, float radius)
{
	const float *corners[4] = { strike.base0, strike.tip0, strike.tip1, strike.base1 };
	const float pad = radius + strike.radius;
	const float c[3] = { center.x, center.y, center.z };
	for (int axis = 0; axis < 3; ++axis)
	{
		float lo = FLT_MAX, hi = -FLT_MAX;
		for (int k = 0; k < 4; ++k)
		{
			lo = MIN(lo, corners[k][axis]);
			hi = MAX(hi, corners[k][axis]);
		}
		if (c[axis] < lo - pad || c[axis] > hi + pad)
			return false;
	}
	return true;
}

// the frond's chain ends at keepU now; the state of the kept nodes stays, the
// shortened last segment keeps its current direction
static void TruncateFrond(PlantInstance &inst, int f, float keepU)
{
	FrondSim &fr = inst.fronds[f];
	SetupFrond(inst, fr, inst.skel->fronds[f], keepU);
	const int n = fr.segments;
	const float current = Length(fr.x[n] - fr.x[n - 1]);
	const float part = fr.L[n - 1] / MAX(current, 1e-3f);
	fr.x[n] = fr.x[n - 1] + (fr.x[n] - fr.x[n - 1]) * part;
	fr.prevX[n] = fr.prevX[n - 1] + (fr.prevX[n] - fr.prevX[n - 1]) * part;
	fr.pressed &= (2u << n) - 1u;
}

/*
=============
R_PlantSimProcessCuts

R_PlantCutBeginScene, once per frame: this frame's attacking blade steps
against the plants seen last frame; a plant hit gets its cut mesh (lod 0 only)
and its fronds are cut short
=============
*/
void R_PlantSimProcessCuts(void)
{
	int count;
	const foliageSaberCut_t *strikes = R_PlantCutStrikes(&count);
	if (!count)
		return;
	static float velocity[PLANTSKEL_MAX_FRONDS][PLANTSKEL_MAX_NODES][3];
	for (PlantInstance *inst : s_ps.instances)
	{
		if (!inst->valid || tr.frameCount - inst->seenFrame > 2 || inst->cutFailed)
			continue;
		for (int k = 0; k < count; ++k)
		{
			const foliageSaberCut_t &strike = strikes[k];
			if (!StrikeNear(strike, inst->center, inst->radius))
				continue;
			if (!inst->cut)
			{
				const model_t *model = R_GetModelByHandle(inst->hModel);
				const mdvModel_t *mdv = model && model->type == MOD_MESH ? model->data.mdv[0] : NULL;
				// its bones belong to the skeleton of the lod it was drawn with
				if (mdv && mdv->plantSkel == inst->skel)
					inst->cut = R_PlantCutCreate(mdv);
				if (!inst->cut)
				{
					inst->cutFailed = mdv && mdv->plantSkel == inst->skel;
					ri.Printf(PRINT_DEVELOPER, "saber cut: %s can't be cut (%s)\n", model ? model->name : "?",
						inst->cutFailed ? "no cuttable surfaces" : "drawn with another lod");
					break;
				}
			}
			const bool wasDrawn = R_PlantCutDrawOf(inst->cut) != NULL;
			for (size_t f = 0; f < inst->fronds.size() && f < PLANTSKEL_MAX_FRONDS; ++f)
			{
				const FrondSim &fr = inst->fronds[f];
				for (int n = 0; n < PLANTSKEL_MAX_NODES; ++n)
				{
					const Vec3 v = n <= fr.segments ? fr.v[n] : fr.v[fr.segments];
					velocity[f][n][0] = v.x;
					velocity[f][n][1] = v.y;
					velocity[f][n][2] = v.z;
				}
			}
			plantCutPose_t pose;
			VectorSet(pose.origin, inst->origin.x, inst->origin.y, inst->origin.z);
			for (int i = 0; i < 3; ++i)
				VectorSet(pose.axis[i], inst->axis[i].x, inst->axis[i].y, inst->axis[i].z);
			pose.bones = inst->bones.data();
			pose.numBones = inst->skel->numBones;
			pose.nodeVelocity = velocity;
			pose.groundZ = inst->groundZ;
			pose.ent = &inst->refEnt;
			float keepU[PLANTSKEL_MAX_FRONDS];
			if (!R_PlantCutStrike(inst->cut, &pose, &strike, keepU))
				continue;
			if (!wasDrawn)
				++s_ps.numCut;
			for (size_t f = 0; f < inst->fronds.size() && f < PLANTSKEL_MAX_FRONDS; ++f)
			{
				if (keepU[f] < inst->fronds[f].keepU - 1e-4f)
					TruncateFrond(*inst, (int)f, keepU[f]);
			}
			// wake it: the stubs swing back
			inst->atRest = false;
		}
	}
}

// R_AddMD3Surfaces: a cut plant is drawn with lod 0 (its cut mesh)
bool R_PlantSimCutLod(const trRefEntity_t *ent)
{
	if (s_ps.numCut <= 0)
		return false;
	auto it = s_ps.lookup.find(PlantKey(ent->e.hModel, ent->e.origin));
	return it != s_ps.lookup.end() && it->second->hModel == ent->e.hModel && R_PlantCutDrawOf(it->second->cut);
}

/*
=============
R_PlantSimCutTest_f

r_foliageSaberCutTest [height] [partial]: a blade step through the plant
nearest to the view, as an attacking saber would send it: level, at height
(fraction of the plant, default 0.5), all the way through or (partial 1)
stopping in its middle
=============
*/
void R_PlantSimCutTest_f(void)
{
	const float height = ri.Cmd_Argc() > 1 ? Com_Clamp(0.0f, 1.0f, (float)atof(ri.Cmd_Argv(1))) : 0.5f;
	const bool partial = ri.Cmd_Argc() > 2 && atoi(ri.Cmd_Argv(2)) != 0;
	if (!R_PlantCutEnabled())
	{
		ri.Printf(PRINT_ALL, "r_foliageSaberCut is off (or no plant simulation: r_autoFoliage, r_plantWind / r_foliageInteraction)\n");
		return;
	}
	const PlantInstance *best = NULL;
	float bestD = FLT_MAX;
	const Vec3 view = V3(tr.refdef.vieworg[0], tr.refdef.vieworg[1], tr.refdef.vieworg[2]);
	for (const PlantInstance *inst : s_ps.instances)
	{
		if (!inst->valid || tr.frameCount - inst->seenFrame > 2)
			continue;
		const float d = Length(inst->center - view);
		if (d < bestD)
		{
			bestD = d;
			best = inst;
		}
	}
	if (!best)
	{
		ri.Printf(PRINT_ALL, "no plant in view\n");
		return;
	}
	const float top = 2.0f * best->center.z - best->groundZ;
	const float z = best->groundZ + height * (top - best->groundZ);
	const float r = best->radius * 1.2f;
	// steps of at most 64 units at 1200 u/s, like the frames of a swing
	const float x0 = best->center.x - r;
	const float x1 = partial ? best->center.x : best->center.x + r;
	const int steps = Com_Clampi(1, 32, (int)ceilf((x1 - x0) / 64.0f));
	for (int s = 0; s < steps; ++s)
	{
		const float a = x0 + (x1 - x0) * s / steps;
		const float b = x0 + (x1 - x0) * (s + 1) / steps;
		foliageSaberCut_t cut;
		Com_Memset(&cut, 0, sizeof(cut));
		cut.id = -1;
		VectorSet(cut.base0, a, best->center.y - r, z);
		VectorSet(cut.tip0, a, best->center.y + r, z);
		VectorSet(cut.base1, b, best->center.y - r, z);
		VectorSet(cut.tip1, b, best->center.y + r, z);
		cut.radius = 1.0f;
		cut.duration = (b - a) / 1200.0f;
		RE_AddFoliageSaberCut(&cut);
	}
	ri.Printf(PRINT_ALL, "blade step through %s (%.0f %.0f %.0f) at z %.0f, %s\n",
		R_GetModelByHandle(best->hModel)->name, best->origin.x, best->origin.y, best->origin.z, z,
		partial ? "to its middle" : "all the way");
}

// r_foliageSaberCutReset: every cut plant whole again, the pieces gone
void R_PlantSimRestoreCuts_f(void)
{
	int restored = 0;
	for (PlantInstance *inst : s_ps.instances)
	{
		if (!inst->cut)
			continue;
		restored += R_PlantCutDrawOf(inst->cut) != NULL;
		R_PlantCutFree(inst->cut);
		inst->cut = NULL;
		inst->cutFailed = false;
		// the whole chains back at the next draw
		inst->valid = false;
	}
	s_ps.numCut = 0;
	ri.Printf(PRINT_ALL, "%d cut plants restored\n", restored);
}
