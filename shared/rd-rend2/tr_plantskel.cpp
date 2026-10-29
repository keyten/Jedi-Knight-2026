/*
 * Frond skeletons of MD3 plants (FOLIAGE_PLANT / FOLIAGE_GRASS surfaces),
 * built once at model load, never per frame.
 *
 * A stock fern is not one rigid object around one root: it is a set of fronds
 * (leaves, cards), each a thin, nearly inextensible strip that bends along its
 * own centerline. The analysis finds them without any authoring:
 *   - the vertices are welded by position (stock MD3s split the UV seams);
 *   - fronds meeting in a shared vertex next to the model root (a "hub",
 *     yavin/fern3_b) are separated at it; separate surfaces (nar_shaddar/plant)
 *     and separate pieces (yavin/plant_b) are separate fronds anyway;
 *   - the attachment of a frond is its hub, else its base: the bottom edge of a
 *     standing card, the inner end of a radial leaf;
 *   - the centerline is a kernel regression of the vertex positions over their
 *     distance to the attachment, extended to the tip and resampled into equal
 *     segments (about 16 units, at most 7);
 *   - every vertex is projected on it: two neighbouring segments (bones) and a
 *     weight, blended across the joint between them (glsl/plant_skin.glsl).
 * The CPU simulation (tr_plantsim.cpp) moves the segments; the vertex shaders
 * move the mesh with them.
 */
#include "tr_local.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace
{

const float PLANTSKEL_WELD = 0.05f;			// position weld grid (units)
const float PLANTSKEL_HUB_RADIUS = 0.08f;	// hub search radius, fraction of the plant size
const float PLANTSKEL_SEGMENT = 16.0f;		// target segment length (units)
const int PLANTSKEL_REGRESSION = 16;		// dense centerline samples

struct Vec3
{
	float x, y, z;
};

inline Vec3 V(const float *v) { return { v[0], v[1], v[2] }; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec3 a) { return sqrtf(Dot(a, a)); }

struct UnionFind
{
	std::vector<int> parent;
	explicit UnionFind(int n) : parent(n) { for (int i = 0; i < n; ++i) parent[i] = i; }
	int Find(int a)
	{
		while (parent[a] != a)
		{
			parent[a] = parent[parent[a]];
			a = parent[a];
		}
		return a;
	}
	void Join(int a, int b) { parent[Find(a)] = Find(b); }
};

struct Candidate
{
	int surface;
	int frondClass;
};

struct FrondBuild
{
	std::vector<int> verts;		// welded ids, hubs excluded
	std::vector<int> hubs;		// welded hub ids it touches
	int frondClass;
	float length;
};

} // namespace

// the class a surface would get with the broad r_autoFoliage mode; the draw
// decides per frame with the current mode, the skeleton covers both
static int PlantSkelSurfaceClass(const mdvModel_t *model, const mdvSurface_t *surface)
{
	if (surface->numShaderIndexes <= 0)
		return FOLIAGE_NONE;
	const shader_t *sh = R_GetShaderByHandle(surface->shaderIndexes[0]);
	if (!sh || sh->defaultShader)
		return FOLIAGE_NONE;
	return R_ResolveAutoFoliage(model, surface, sh, 2).cls;
}

/*
=============
R_BuildPlantSkeleton

R_LoadMD3, after the surfaces and before the VBO: the frond skeleton of a map
object plant and the per vertex skin of its plant surfaces.
=============
*/
void R_BuildPlantSkeleton(mdvModel_t *model)
{
	model->plantSkel = NULL;
	if (!(model->foliageSignals & FOLIAGE_MODEL_OBJECT) || model->numFrames != 1)
		return;

	std::vector<Candidate> candidates;
	for (int s = 0; s < model->numSurfaces; ++s)
	{
		mdvSurface_t *surf = &model->surfaces[s];
		surf->plantSkin = NULL;
		surf->plantU = NULL;
		surf->plantFrond = NULL;
		const int cls = PlantSkelSurfaceClass(model, surf);
		if ((cls == FOLIAGE_PLANT || cls == FOLIAGE_GRASS) && surf->numVerts > 0)
			candidates.push_back({ s, cls == FOLIAGE_GRASS ? PLANTFROND_GRASS : PLANTFROND_LEAF });
	}
	if (candidates.empty())
		return;

	// plant root: bottom center of the model, its size for the hub radius
	const Vec3 root = {
		(model->foliageMins[0] + model->foliageMaxs[0]) * 0.5f,
		(model->foliageMins[1] + model->foliageMaxs[1]) * 0.5f,
		model->foliageMins[2] };
	const float halfX = (model->foliageMaxs[0] - model->foliageMins[0]) * 0.5f;
	const float halfY = (model->foliageMaxs[1] - model->foliageMins[1]) * 0.5f;
	const float size = MAX(MAX(model->foliageMaxs[2] - model->foliageMins[2],
		sqrtf(halfX * halfX + halfY * halfY)), 1.0f);
	const float hubRadius = PLANTSKEL_HUB_RADIUS * size;

	// weld
	std::unordered_map<uint64_t, int> weld;
	std::vector<Vec3> wpos;
	std::vector<std::vector<int>> vmap(candidates.size());
	for (size_t c = 0; c < candidates.size(); ++c)
	{
		const mdvSurface_t *surf = &model->surfaces[candidates[c].surface];
		vmap[c].resize(surf->numVerts);
		for (int v = 0; v < surf->numVerts; ++v)
		{
			const float *p = surf->verts[v].xyz;
			const int64_t qx = (int64_t)floorf(p[0] / PLANTSKEL_WELD + 0.5f);
			const int64_t qy = (int64_t)floorf(p[1] / PLANTSKEL_WELD + 0.5f);
			const int64_t qz = (int64_t)floorf(p[2] / PLANTSKEL_WELD + 0.5f);
			const uint64_t key = ((uint64_t)(qx & 0x1fffff) << 42) | ((uint64_t)(qy & 0x1fffff) << 21) |
				(uint64_t)(qz & 0x1fffff);
			auto it = weld.find(key);
			if (it == weld.end())
			{
				it = weld.emplace(key, (int)wpos.size()).first;
				wpos.push_back(V(p));
			}
			vmap[c][v] = it->second;
		}
	}
	const int numWelded = (int)wpos.size();

	// welded triangles (and the candidate they come from)
	struct Tri { int c; int v[3]; };
	std::vector<Tri> tris;
	for (size_t c = 0; c < candidates.size(); ++c)
	{
		const mdvSurface_t *surf = &model->surfaces[candidates[c].surface];
		for (int i = 0; i + 2 < surf->numIndexes; i += 3)
		{
			Tri t = { (int)c, { vmap[c][surf->indexes[i]], vmap[c][surf->indexes[i + 1]], vmap[c][surf->indexes[i + 2]] } };
			if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[2] == t.v[0])
				continue;
			tris.push_back(t);
		}
	}
	if (tris.empty())
		return;

	// hub candidates: welded vertices next to the root
	std::vector<char> nearRoot(numWelded, 0);
	for (int w = 0; w < numWelded; ++w)
		nearRoot[w] = Length(wpos[w] - root) < hubRadius;

	// triangle groups connected through vertices that are not near the root,
	// never across surfaces
	UnionFind groups((int)tris.size());
	{
		std::unordered_map<uint64_t, int> owner;
		for (int t = 0; t < (int)tris.size(); ++t)
		{
			for (int k = 0; k < 3; ++k)
			{
				const int w = tris[t].v[k];
				if (nearRoot[w])
					continue;
				const uint64_t key = ((uint64_t)tris[t].c << 32) | (uint32_t)w;
				auto it = owner.find(key);
				if (it == owner.end())
					owner.emplace(key, t);
				else
					groups.Join(t, it->second);
			}
		}
	}

	// a near vertex is a hub when it joins two groups; otherwise it is the base
	// of its only frond and joins it back
	std::unordered_map<int, std::vector<int>> nearGroups;
	for (int t = 0; t < (int)tris.size(); ++t)
	{
		for (int k = 0; k < 3; ++k)
		{
			const int w = tris[t].v[k];
			if (!nearRoot[w])
				continue;
			std::vector<int> &list = nearGroups[w];
			const int g = groups.Find(t);
			if (std::find(list.begin(), list.end(), g) == list.end())
				list.push_back(g);
		}
	}
	std::vector<char> hub(numWelded, 0);
	for (const auto &entry : nearGroups)
		hub[entry.first] = entry.second.size() >= 2;

	// fronds
	std::unordered_map<int, int> groupFrond;
	std::vector<FrondBuild> builds;
	std::vector<std::vector<int>> frondTris;
	for (int t = 0; t < (int)tris.size(); ++t)
	{
		const int g = groups.Find(t);
		auto it = groupFrond.find(g);
		if (it == groupFrond.end())
		{
			it = groupFrond.emplace(g, (int)builds.size()).first;
			builds.push_back(FrondBuild());
			builds.back().frondClass = candidates[tris[t].c].frondClass;
			builds.back().length = 0.0f;
			frondTris.push_back(std::vector<int>());
		}
		frondTris[it->second].push_back(t);
	}

	std::vector<plantFrond_t> fronds;
	std::vector<int> frondOfBuild(builds.size(), -1);
	// per welded vertex: frond index and position along it in node units
	std::vector<int> vertFrond(numWelded, -1);
	std::vector<float> vertU(numWelded, 0.0f);

	for (size_t b = 0; b < builds.size(); ++b)
	{
		FrondBuild &fb = builds[b];
		for (int t : frondTris[b])
		{
			for (int k = 0; k < 3; ++k)
			{
				const int w = tris[t].v[k];
				std::vector<int> &list = hub[w] ? fb.hubs : fb.verts;
				if (std::find(list.begin(), list.end(), w) == list.end())
					list.push_back(w);
			}
		}
		if (fb.verts.empty())
			continue;

		// attachment: the hub, else the base band of the frond
		std::vector<Vec3> seeds;
		std::vector<int> seedVerts;
		Vec3 attach = { 0.0f, 0.0f, 0.0f };
		if (!fb.hubs.empty())
		{
			for (int h : fb.hubs)
				attach = attach + wpos[h];
			attach = attach * (1.0f / fb.hubs.size());
			seeds.push_back(attach);
		}
		else
		{
			float zMin = FLT_MAX, zMax = -FLT_MAX, rMin = FLT_MAX, rMax = -FLT_MAX;
			std::vector<float> radial(fb.verts.size());
			for (size_t i = 0; i < fb.verts.size(); ++i)
			{
				const Vec3 &p = wpos[fb.verts[i]];
				radial[i] = sqrtf((p.x - root.x) * (p.x - root.x) + (p.y - root.y) * (p.y - root.y));
				zMin = MIN(zMin, p.z); zMax = MAX(zMax, p.z);
				rMin = MIN(rMin, radial[i]); rMax = MAX(rMax, radial[i]);
			}
			// a standing card hangs from its bottom edge, a radial leaf from
			// its inner end
			const bool standing = zMax - zMin > rMax - rMin;
			const float extent = standing ? zMax - zMin : rMax - rMin;
			const float band = MAX(0.08f * extent, 0.5f);
			for (size_t i = 0; i < fb.verts.size(); ++i)
			{
				const float score = standing ? wpos[fb.verts[i]].z - zMin : radial[i] - rMin;
				if (score <= band)
				{
					seeds.push_back(wpos[fb.verts[i]]);
					seedVerts.push_back(fb.verts[i]);
					attach = attach + wpos[fb.verts[i]];
				}
			}
			attach = attach * (1.0f / seeds.size());
		}

		// parameter: distance to the nearest seed (the base itself is 0)
		std::vector<float> s0(fb.verts.size());
		float sMax = 0.0f;
		for (size_t i = 0; i < fb.verts.size(); ++i)
		{
			float d = FLT_MAX;
			for (const Vec3 &q : seeds)
				d = MIN(d, Length(wpos[fb.verts[i]] - q));
			if (std::find(seedVerts.begin(), seedVerts.end(), fb.verts[i]) != seedVerts.end())
				d = 0.0f;
			s0[i] = d;
			sMax = MAX(sMax, d);
		}
		if (sMax < 1.0f)
			continue;

		// dense kernel regression of the positions over s0: the centerline
		const float bandwidth = sMax / 5.0f;
		std::vector<Vec3> arc;
		arc.push_back(attach);
		for (int k = 1; k <= PLANTSKEL_REGRESSION; ++k)
		{
			const float u = sMax * k / PLANTSKEL_REGRESSION;
			Vec3 acc = { 0.0f, 0.0f, 0.0f };
			float sum = 0.0f;
			for (size_t i = 0; i < fb.verts.size(); ++i)
			{
				const float e = (s0[i] - u) / bandwidth;
				const float wt = expf(-e * e);
				acc = acc + wpos[fb.verts[i]] * wt;
				sum += wt;
			}
			if (sum > 1e-12f)
			{
				const Vec3 p = acc * (1.0f / sum);
				if (Length(p - arc.back()) > 0.25f)
					arc.push_back(p);
			}
		}
		if (arc.size() < 2)
			continue;
		std::vector<float> cum(arc.size(), 0.0f);
		for (size_t i = 1; i < arc.size(); ++i)
			cum[i] = cum[i - 1] + Length(arc[i] - arc[i - 1]);

		// the kernel pulls the tip in: extend the arc along its direction over
		// the last quarter to the farthest vertex projection
		{
			size_t j = arc.size() - 1;
			while (j > 0 && cum.back() - cum[j] < 0.25f * cum.back())
				--j;
			Vec3 dir = arc.back() - arc[j];
			const float dirLen = Length(dir);
			if (dirLen > 1e-3f)
			{
				dir = dir * (1.0f / dirLen);
				float ext = 0.0f;
				for (int w : fb.verts)
					ext = MAX(ext, Dot(wpos[w] - arc.back(), dir));
				if (ext > 0.0f)
				{
					arc.push_back(arc.back() + dir * ext);
					cum.push_back(cum.back() + ext);
				}
			}
		}
		const float length = cum.back();
		if (length < 1.0f)
			continue;

		int segments = (int)floorf(length / PLANTSKEL_SEGMENT + 0.5f);
		segments = MAX(1, MIN(MIN(segments, PLANTSKEL_MAX_NODES - 1), (int)fb.verts.size() - 1));

		plantFrond_t frond = {};
		frond.numNodes = segments + 1;
		frond.frondClass = fb.frondClass;
		frond.length = length;
		for (int k = 0; k <= segments; ++k)
		{
			const float target = length * k / segments;
			size_t i = 0;
			while (i + 2 < arc.size() && cum[i + 1] < target)
				++i;
			const float span = MAX(cum[i + 1] - cum[i], 1e-6f);
			const float t = Com_Clamp(0.0f, 1.0f, (target - cum[i]) / span);
			const Vec3 p = arc[i] + (arc[i + 1] - arc[i]) * t;
			VectorSet(frond.nodes[k], p.x, p.y, p.z);
		}

		// project the vertices: node units along the chain and the lamina width
		const int frondIndex = (int)fronds.size();
		float cov[3][3] = {};
		for (int w : fb.verts)
		{
			const Vec3 p = wpos[w];
			float best = FLT_MAX, bestU = 0.0f;
			for (int k = 0; k < segments; ++k)
			{
				const Vec3 a = V(frond.nodes[k]);
				const Vec3 ab = V(frond.nodes[k + 1]) - a;
				const float t = Com_Clamp(0.0f, 1.0f, Dot(p - a, ab) / MAX(Dot(ab, ab), 1e-9f));
				const float d = Length(p - (a + ab * t));
				if (d < best - 1e-4f)
				{
					best = d;
					bestU = k + t;
				}
			}
			const int node = (int)floorf(bestU + 0.5f);
			frond.halfWidth[node] = MAX(frond.halfWidth[node], best);
			vertFrond[w] = frondIndex;
			vertU[w] = bestU;

			// offset across the centerline, for the lamina direction
			const int k = MIN((int)bestU, segments - 1);
			const Vec3 a = V(frond.nodes[k]);
			const Vec3 ab = V(frond.nodes[k + 1]) - a;
			const float abLen = MAX(Length(ab), 1e-6f);
			const Vec3 t = ab * (1.0f / abLen);
			Vec3 o = p - (a + ab * (bestU - k));
			o = o - t * Dot(o, t);
			const float oc[3] = { o.x, o.y, o.z };
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c)
					cov[r][c] += oc[r] * oc[c];
		}

		// lamina direction: the principal direction of those offsets over the
		// whole frond (power iteration), across each segment; horizontal across
		// the frond when it has no width. The lamina bends easily out of its
		// plane, hardly within it (tr_plantsim.cpp).
		{
			const Vec3 chord = V(frond.nodes[segments]) - V(frond.nodes[0]);
			Vec3 axis = { -chord.y, chord.x, 0.0f };
			if (Dot(axis, axis) < 1e-6f)
				axis = { 1.0f, 0.0f, 0.0f };
			axis = axis * (1.0f / Length(axis));
			if (cov[0][0] + cov[1][1] + cov[2][2] > 1e-3f)
			{
				for (int it = 0; it < 16; ++it)
				{
					const Vec3 next = {
						cov[0][0] * axis.x + cov[0][1] * axis.y + cov[0][2] * axis.z,
						cov[1][0] * axis.x + cov[1][1] * axis.y + cov[1][2] * axis.z,
						cov[2][0] * axis.x + cov[2][1] * axis.y + cov[2][2] * axis.z };
					const float len = Length(next);
					if (len < 1e-9f)
						break;
					axis = next * (1.0f / len);
				}
			}
			for (int k = 0; k < segments; ++k)
			{
				const Vec3 t = V(frond.nodes[k + 1]) - V(frond.nodes[k]);
				const Vec3 tn = t * (1.0f / MAX(Length(t), 1e-6f));
				Vec3 l = axis - tn * Dot(axis, tn);
				if (Dot(l, l) < 1e-6f)
				{
					l = { -tn.y, tn.x, 0.0f };
					if (Dot(l, l) < 1e-6f)
						l = { 1.0f, 0.0f, 0.0f };
				}
				l = l * (1.0f / Length(l));
				VectorSet(frond.lateral[k], l.x, l.y, l.z);
			}
		}
		frondOfBuild[b] = frondIndex;
		fronds.push_back(frond);
	}

	if (fronds.empty())
		return;

	// keep the longest fronds within the bone budget; the others stay at rest
	if ((int)fronds.size() > PLANTSKEL_MAX_FRONDS)
	{
		std::vector<int> order(fronds.size());
		for (size_t i = 0; i < order.size(); ++i)
			order[i] = (int)i;
		std::sort(order.begin(), order.end(), [&](int a, int b) { return fronds[a].length > fronds[b].length; });
		std::vector<int> remap(fronds.size(), -1);
		std::vector<plantFrond_t> kept;
		for (int i = 0; i < PLANTSKEL_MAX_FRONDS; ++i)
		{
			remap[order[i]] = (int)kept.size();
			kept.push_back(fronds[order[i]]);
		}
		for (int w = 0; w < numWelded; ++w)
			if (vertFrond[w] >= 0)
				vertFrond[w] = remap[vertFrond[w]];
		fronds.swap(kept);
	}

	// a frond's half width spreads to the neighbouring nodes (the lamina is
	// sampled sparsely)
	for (plantFrond_t &frond : fronds)
	{
		float widths[PLANTSKEL_MAX_NODES];
		for (int k = 0; k < frond.numNodes; ++k)
		{
			widths[k] = frond.halfWidth[k];
			if (k > 0) widths[k] = MAX(widths[k], 0.5f * frond.halfWidth[k - 1]);
			if (k + 1 < frond.numNodes) widths[k] = MAX(widths[k], 0.5f * frond.halfWidth[k + 1]);
		}
		for (int k = 0; k < frond.numNodes; ++k)
			frond.halfWidth[k] = widths[k];
	}

	plantSkeleton_t *skel = (plantSkeleton_t *)Hunk_Alloc(sizeof(*skel), h_low);
	skel->numFronds = (int)fronds.size();
	skel->fronds = (plantFrond_t *)Hunk_Alloc(sizeof(plantFrond_t) * skel->numFronds, h_low);
	skel->numBones = 1;
	skel->maxLength = 0.0f;
	for (int f = 0; f < skel->numFronds; ++f)
	{
		skel->fronds[f] = fronds[f];
		skel->fronds[f].firstBone = skel->numBones;
		skel->numBones += fronds[f].numNodes - 1;
		skel->maxLength = MAX(skel->maxLength, fronds[f].length);
	}

	// per vertex skin of the plant surfaces, and the frond parameter the saber
	// cut (tr_plantcut.cpp) interpolates and skins its new vertices with
	for (size_t c = 0; c < candidates.size(); ++c)
	{
		mdvSurface_t *surf = &model->surfaces[candidates[c].surface];
		surf->plantSkin = (uint32_t *)Hunk_Alloc(sizeof(uint32_t) * surf->numVerts, h_low);
		surf->plantU = (float *)Hunk_Alloc(sizeof(float) * surf->numVerts, h_low);
		surf->plantFrond = (signed char *)Hunk_Alloc(surf->numVerts, h_low);
		for (int v = 0; v < surf->numVerts; ++v)
		{
			const int w = vmap[c][v];
			const int f = vertFrond[w];
			surf->plantFrond[v] = (signed char)f;
			surf->plantU[v] = f >= 0 ? vertU[w] : 0.0f;
			surf->plantSkin[v] = f >= 0 ? R_PlantSkinPack(&skel->fronds[f], skel->fronds[f].numNodes - 1, vertU[w]) :
				R_PlantSkinPack(NULL, 0, 0.0f);
		}
	}

	model->plantSkel = skel;
}

/*
=============
R_PlantSkinPack

The skin of a vertex at u (node units) along a frond whose chain has the given
segments (fewer than the frond's after a saber cut): two neighbouring bones and
the weight of the second, as the bone weight attribute (RGBA8 in memory order:
bone A, bone B, weight, 255). NULL frond: bone 0 (rest).
=============
*/
uint32_t R_PlantSkinPack(const plantFrond_t *frond, int segments, float u)
{
	int boneA = 0, boneB = 0;
	float weightB = 0.0f;
	if (frond && segments > 0)
	{
		u = Com_Clamp(0.0f, (float)segments, u);
		const int j = MIN((int)floorf(u), segments - 1);
		const float t = u - j;
		// blended across the joint next to the vertex: from the middle of
		// one segment to the middle of the next; the base half of the
		// first segment turns with it around the attachment (both keep
		// the attachment point)
		if (t < 0.5f)
		{
			boneA = j == 0 ? frond->firstBone : frond->firstBone + j - 1;
			boneB = frond->firstBone + j;
			weightB = j == 0 ? 0.0f : t + 0.5f;
		}
		else
		{
			boneA = frond->firstBone + j;
			boneB = j + 1 < segments ? frond->firstBone + j + 1 : boneA;
			weightB = t - 0.5f;
		}
	}
	const uint32_t wB = (uint32_t)Com_Clampi(0, 255, (int)floorf(weightB * 255.0f + 0.5f));
	return LittleLong((uint32_t)boneA | ((uint32_t)boneB << 8) | (wB << 16) | (255u << 24));
}

// r_printAutoFoliage: the skeleton of a model
void R_PrintPlantSkeleton(const mdvModel_t *model)
{
	const plantSkeleton_t *skel = model->plantSkel;
	if (!skel)
		return;
	ri.Printf(PRINT_ALL, "  plant skeleton: %d fronds, %d bones, longest %.0f\n",
		skel->numFronds, skel->numBones, skel->maxLength);
	for (int f = 0; f < skel->numFronds; ++f)
	{
		const plantFrond_t *frond = &skel->fronds[f];
		ri.Printf(PRINT_ALL, "    frond %2d: %s, %d segments, length %.0f, bones %d-%d, base (%.0f %.0f %.0f)\n",
			f, frond->frondClass == PLANTFROND_GRASS ? "grass" : "leaf", frond->numNodes - 1,
			frond->length, frond->firstBone, frond->firstBone + frond->numNodes - 2,
			frond->nodes[0][0], frond->nodes[0][1], frond->nodes[0][2]);
	}
}
