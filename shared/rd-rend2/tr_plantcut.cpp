/*
 * Saber cut of the MD3 plants (r_foliageSaberCut): an attacking lightsaber
 * blade (the game / cgame sends every step of a strike, not a blade merely
 * touching) cuts the fronds of the plants it sweeps through.
 *
 *   - A plant hit for the first time gets its own copy of the plant surfaces
 *     (the frond skeleton surfaces of tr_plantskel.cpp, lod 0): rest geometry,
 *     UV, normal, tangent and the frond parameter u of every vertex.
 *   - A blade step is the quad of the blade at the start and at the end of the
 *     step, two triangles; the mesh is cut by each in the pose on screen (the
 *     current bones skin the rest positions), tr_plantcut_mesh.h: a leaf cut
 *     all the way is split, one cut part of the way keeps a slit.
 *   - Pieces that no longer reach an anchor (hub, stem, the base of a frond)
 *     leave the plant: each falls as a rigid piece of its own (gravity, plate
 *     drag and flutter of a leaf, Force waves, the ground), lies for
 *     r_foliageSaberCutPieceTime seconds and sinks away.
 *   - The fronds of the plant are cut short at the highest u they keep: the
 *     chain of the simulation (tr_plantsim.cpp) is truncated there and the
 *     vertices are skinned to the truncated chain, so the rest of the plant
 *     keeps swinging with wind and characters.
 * The cut plant draws its surfaces from its own buffers (same layout as the
 * MD3 VBO, the skin in the bone weight slot), the pieces are entities of the
 * plant model whose only surfaces are their ranges of those buffers.
 */
#include "tr_local.h"
#include "tr_plantcut_mesh.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

using plantcut::V3;
using plantcut::MakeV3;

namespace
{

const int PLANTCUT_MAX_STRIKES = 64;		// blade steps per frame
const int PLANTCUT_MAX_VERTS = 1 << 16;		// per plant (no more cuts beyond)
const float PLANTCUT_WELD = 0.05f;			// as tr_plantskel.cpp
const float PLANTCUT_ANCHOR_U = 0.15f;		// frond base (node units): holds a piece
const float PLANTCUT_MIN_PIECE_AREA = 0.5f;	// smaller pieces just vanish (units^2)
const float PIECE_STEP = 1.0f / 60.0f;
const float PIECE_GRAVITY = 600.0f;
const float PIECE_SINK_TIME = 2.0f;			// s
const int PIECE_HULL = 32;					// points tested against the ground

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

inline V3 Rotate(Quat q, V3 v)
{
	const V3 u = MakeV3(q.x, q.y, q.z);
	const V3 t = plantcut::Cross(u, v) * 2.0f;
	return v + t * q.w + plantcut::Cross(u, t);
}

inline Quat Normalized(Quat q)
{
	const float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (n < 1e-9f)
		return QUAT_IDENTITY;
	const float s = 1.0f / n;
	Quat r = { q.x * s, q.y * s, q.z * s, q.w * s };
	return r;
}

// q turned by the angular velocity w (world) over h
inline Quat Integrate(Quat q, V3 w, float h)
{
	const float angle = plantcut::Length(w) * h;
	if (angle < 1e-7f)
		return q;
	const V3 axis = w * (1.0f / plantcut::Length(w));
	const float s = sinf(0.5f * angle);
	Quat d = { axis.x * s, axis.y * s, axis.z * s, cosf(0.5f * angle) };
	return Normalized(Mul(d, q));
}

inline V3 FromVec(const float *v) { return MakeV3(v[0], v[1], v[2]); }

inline float Hash01(uint32_t h)
{
	h *= 2654435761u;
	h ^= h >> 15u;
	h *= 2246822519u;
	h ^= h >> 13u;
	return (float)h * (1.0f / 4294967295.0f);
}

struct Piece
{
	plantCut_t *cut;
	std::vector<plantcut::Triangle> tris;	// its triangles of the cut mesh
	std::vector<srfVBOMDVMesh_t> surfaces;	// per model surface, its range
	plantCutDraw_t draw;
	refEntity_t ent;		// the plant's refEntity (skin, shader, colour)
	V3 restOrigin;			// plant transform at the cut: object -> rest world
	V3 restAxis[3];
	V3 pivot;				// centroid, rest world
	V3 normal;				// lamina normal, rest world
	V3 along;				// longest direction, rest world
	float flat;				// 1 = leaf (plate drag), 0 = rod (blade of grass)
	std::vector<V3> hull;	// points relative to the pivot, rest world
	Quat q;					// rest world -> world: q (p - pivot) + x
	V3 x, v, w;
	float groundZ;
	V3 groundAt;			// where the ground was traced
	float age;				// s
	bool resting;
};

} // namespace

struct plantCut_s
{
	const mdvModel_t *mdv;
	plantcut::Mesh mesh;			// tris: the part still on the plant
	std::vector<uint32_t> skin;		// per vertex, skinned to the truncated chains
	std::vector<float> keepU;		// per frond: the u it keeps
	std::vector<Piece *> pieces;
	int cuts;

	// GPU: the plant's surfaces and every piece in one vertex / index buffer
	VBO_t vbo;
	IBO_t ibo;
	std::vector<srfVBOMDVMesh_t> surfaces;
	plantCutDraw_t draw;
};

static struct
{
	std::vector<foliageSaberCut_t> strikes;
	std::vector<Piece *> pieces;		// all live pieces, oldest first
	int frame;
	int lastTime;

	// r_plantDebug 32: the recent blade steps
	struct Sweep
	{
		vec3_t p[4];
		int time;
		bool hit;
	};
	Sweep sweeps[32];
	int numSweeps;
	int nextSweep;
	int firstStrikeSweep;	// slot of this frame's first strike
} s_pc;

bool R_PlantCutEnabled(void)
{
	return r_foliageSaberCut && r_foliageSaberCut->integer && R_PlantSimActive();
}

/*
=============
RE_AddFoliageSaberCut

GetRefFoliageCutAPI: one step of an attacking saber blade, cut into the
plants at the start of the next world scene
=============
*/
void RE_AddFoliageSaberCut(const foliageSaberCut_t *cut)
{
	if (!cut || !R_PlantCutEnabled() || (int)s_pc.strikes.size() >= PLANTCUT_MAX_STRIKES)
		return;
	for (int i = 0; i < 3; ++i)
	{
		if (Q_isnan(cut->base0[i]) || Q_isnan(cut->tip0[i]) || Q_isnan(cut->base1[i]) || Q_isnan(cut->tip1[i]) ||
			fabsf(cut->base0[i]) > 1e6f || fabsf(cut->tip1[i]) > 1e6f)
			return;
	}
	// no blade, or a teleport between the two ends of the step
	if (DistanceSquared(cut->base0, cut->tip0) < 1.0f || DistanceSquared(cut->base0, cut->base1) > 256.0f * 256.0f)
	{
		ri.Printf(PRINT_DEVELOPER, "saber cut: blade step dropped (no blade or a teleport)\n");
		return;
	}
	s_pc.strikes.push_back(*cut);
}

const foliageSaberCut_t *R_PlantCutStrikes(int *count)
{
	*count = (int)s_pc.strikes.size();
	return s_pc.strikes.empty() ? NULL : s_pc.strikes.data();
}

void R_PlantCutClearStrikes(void)
{
	s_pc.strikes.clear();
}

/*
=============================================================

The plant's mesh

=============================================================
*/

/*
=============
R_PlantCutCreate

The cut mesh of a plant model (lod 0): its frond skeleton surfaces. NULL when
the plant can't be cut (no skeleton, CPU deformed shaders).
=============
*/
plantCut_t *R_PlantCutCreate(const mdvModel_t *mdv)
{
	if (!mdv || !mdv->plantSkel || mdv->numFrames != 1)
		return NULL;
	int numVerts = 0;
	for (int s = 0; s < mdv->numSurfaces; ++s)
	{
		const mdvSurface_t *surf = &mdv->surfaces[s];
		if (!surf->plantSkin || !surf->plantU || !surf->plantFrond)
			continue;
		if (surf->numShaderIndexes > 0)
		{
			const shader_t *sh = R_GetShaderByHandle(surf->shaderIndexes[0]);
			// drawn from the model's own vertices on the CPU: can't be cut
			if (sh && ShaderRequiresCPUDeforms(sh))
				return NULL;
		}
		numVerts += surf->numVerts;
	}
	if (numVerts == 0 || numVerts >= PLANTCUT_MAX_VERTS)
		return NULL;

	plantCut_t *cut = new plantCut_t();
	cut->mdv = mdv;
	cut->cuts = 0;
	Com_Memset(&cut->vbo, 0, sizeof(cut->vbo));
	Com_Memset(&cut->ibo, 0, sizeof(cut->ibo));
	cut->draw.numSurfaces = 0;
	cut->draw.surfaces = NULL;
	plantcut::Mesh &m = cut->mesh;
	m.verts.reserve(numVerts * 2);
	std::unordered_map<uint64_t, int> weld;
	for (int s = 0; s < mdv->numSurfaces; ++s)
	{
		mdvSurface_t *surf = &mdv->surfaces[s];
		if (!surf->plantSkin || !surf->plantU || !surf->plantFrond)
			continue;
		uint32_t *tangents = (uint32_t *)Hunk_AllocateTempMemory(sizeof(uint32_t) * surf->numVerts);
		R_CalcMikkTSpaceMD3Surface(surf->numIndexes / 3, surf->verts, tangents, surf->st, surf->indexes);
		const int base = (int)m.verts.size();
		for (int v = 0; v < surf->numVerts; ++v)
		{
			plantcut::Vertex vx;
			vx.rest = FromVec(surf->verts[v].xyz);
			vx.normal = FromVec(surf->verts[v].normal);
			vx.st[0] = surf->st[v].st[0];
			vx.st[1] = surf->st[v].st[1];
			vec4_t t;
			R_VboUnpackTangent(t, tangents[v]);
			vx.tangent[0] = t[0]; vx.tangent[1] = t[1]; vx.tangent[2] = t[2]; vx.tangent[3] = t[3];
			vx.frond = surf->plantFrond[v];
			vx.u = surf->plantU[v];
			vx.anchor = vx.frond < 0 || vx.u <= PLANTCUT_ANCHOR_U;
			const int64_t qx = (int64_t)floorf(vx.rest.x / PLANTCUT_WELD + 0.5f);
			const int64_t qy = (int64_t)floorf(vx.rest.y / PLANTCUT_WELD + 0.5f);
			const int64_t qz = (int64_t)floorf(vx.rest.z / PLANTCUT_WELD + 0.5f);
			const uint64_t key = ((uint64_t)(qx & 0x1fffff) << 42) | ((uint64_t)(qy & 0x1fffff) << 21) |
				(uint64_t)(qz & 0x1fffff);
			auto it = weld.find(key);
			if (it == weld.end())
				it = weld.emplace(key, m.nextWeld++).first;
			vx.weld = it->second;
			m.verts.push_back(vx);
			cut->skin.push_back(surf->plantSkin[v]);
		}
		Hunk_FreeTempMemory(tangents);
		for (int i = 0; i + 2 < surf->numIndexes; i += 3)
		{
			plantcut::Triangle t = { { base + (int)surf->indexes[i], base + (int)surf->indexes[i + 1],
				base + (int)surf->indexes[i + 2] }, s };
			if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[2] == t.v[0])
				continue;
			m.tris.push_back(t);
		}
	}
	m.world.resize(m.verts.size());

	// a piece of the model that holds nothing (no frond base) stays with the
	// plant anyway: only pieces a cut frees fall
	std::vector<int> triPiece;
	std::vector<char> anchored;
	plantcut::Components(m, std::vector<char>(), triPiece, anchored);
	for (size_t t = 0; t < m.tris.size(); ++t)
	{
		if (!anchored[triPiece[t]])
			m.verts[m.tris[t].v[0]].anchor = true;
	}

	const plantSkeleton_t *skel = mdv->plantSkel;
	cut->keepU.resize(skel->numFronds);
	for (int f = 0; f < skel->numFronds; ++f)
		cut->keepU[f] = (float)(skel->fronds[f].numNodes - 1);

	cut->surfaces.resize(mdv->numSurfaces);
	Com_Memset(cut->surfaces.data(), 0, sizeof(srfVBOMDVMesh_t) * mdv->numSurfaces);
	cut->draw.numSurfaces = mdv->numSurfaces;
	cut->draw.surfaces = cut->surfaces.data();
	return cut;
}

static void FreePiece(Piece *piece)
{
	auto it = std::find(s_pc.pieces.begin(), s_pc.pieces.end(), piece);
	if (it != s_pc.pieces.end())
		s_pc.pieces.erase(it);
	if (piece->cut)
	{
		std::vector<Piece *> &own = piece->cut->pieces;
		own.erase(std::remove(own.begin(), own.end(), piece), own.end());
	}
	delete piece;
}

void R_PlantCutFree(plantCut_t *cut)
{
	if (!cut)
		return;
	while (!cut->pieces.empty())
		FreePiece(cut->pieces.back());
	if (cut->vbo.vertexesVBO || cut->ibo.indexesVBO)
	{
		// queued draws may still use the buffers
		R_IssuePendingRenderCommands();
		if (glState.currentVBO == &cut->vbo)
			glState.currentVBO = NULL;
		if (glState.currentIBO == &cut->ibo)
			glState.currentIBO = NULL;
		if (cut->vbo.vertexesVBO)
			qglDeleteBuffers(1, &cut->vbo.vertexesVBO);
		if (cut->ibo.indexesVBO)
			qglDeleteBuffers(1, &cut->ibo.indexesVBO);
	}
	delete cut;
}

const plantCutDraw_t *R_PlantCutDrawOf(const plantCut_t *cut)
{
	return cut && cut->vbo.vertexesVBO ? &cut->draw : NULL;
}

int R_PlantCutTriangles(const plantCut_t *cut)
{
	return cut ? (int)cut->mesh.tris.size() : 0;
}

/*
=============
RebuildBuffers

The vertex buffer (layout of the MD3 VBO, tr_model.cpp) and the index ranges
of the plant's surfaces and of all its pieces
=============
*/
static void RebuildBuffers(plantCut_t *cut)
{
	const plantcut::Mesh &m = cut->mesh;
	const int numVerts = (int)m.verts.size();
	const int numSurfaces = cut->mdv->numSurfaces;

	// position, normal, texcoord, tangent, skin; one colour, lightmap coord and
	// light direction for all
	const int stride = (int)(sizeof(vec3_t) + sizeof(uint32_t) + sizeof(vec2_t) + sizeof(uint32_t) + sizeof(uint32_t));
	const int tail = (int)(sizeof(vec4_t) + sizeof(vec2_t) + sizeof(uint32_t));
	std::vector<byte> data((size_t)numVerts * stride + tail);
	for (int i = 0; i < numVerts; ++i)
	{
		const plantcut::Vertex &v = m.verts[i];
		byte *p = data.data() + (size_t)i * stride;
		vec3_t xyz = { v.rest.x, v.rest.y, v.rest.z };
		vec3_t n = { v.normal.x, v.normal.y, v.normal.z };
		vec4_t t = { v.tangent[0], v.tangent[1], v.tangent[2], v.tangent[3] };
		const uint32_t packedN = R_VboPackNormal(n);
		const uint32_t packedT = R_VboPackTangent(t);
		const vec2_t st = { v.st[0], v.st[1] };
		Com_Memcpy(p, xyz, sizeof(vec3_t)); p += sizeof(vec3_t);
		Com_Memcpy(p, &packedN, sizeof(uint32_t)); p += sizeof(uint32_t);
		Com_Memcpy(p, st, sizeof(vec2_t)); p += sizeof(vec2_t);
		Com_Memcpy(p, &packedT, sizeof(uint32_t)); p += sizeof(uint32_t);
		Com_Memcpy(p, &cut->skin[i], sizeof(uint32_t));
	}
	const int ofsColor = numVerts * stride;
	const int ofsLMCoords = ofsColor + (int)sizeof(vec4_t);
	const int ofsLightDir = ofsLMCoords + (int)sizeof(vec2_t);
	{
		const vec4_t color = { 1.0f, 1.0f, 1.0f, 1.0f };
		Com_Memcpy(data.data() + ofsColor, color, sizeof(color));
		Com_Memset(data.data() + ofsLMCoords, 0, sizeof(vec2_t) + sizeof(uint32_t));
	}

	// indices: the plant's surfaces, then every piece's, per surface
	std::vector<glIndex_t> indexes;
	indexes.reserve(m.tris.size() * 3);
	auto fill = [&](srfVBOMDVMesh_t &srf, int s, const std::vector<plantcut::Triangle> &tris, bool keepEmpty) {
		const int first = (int)indexes.size();
		glIndex_t lo = ~0u, hi = 0;
		for (const plantcut::Triangle &t : tris)
		{
			if (t.surface != s)
				continue;
			for (int k = 0; k < 3; ++k)
			{
				const glIndex_t i = (glIndex_t)t.v[k];
				indexes.push_back(i);
				lo = MIN(lo, i);
				hi = MAX(hi, i);
			}
		}
		Com_Memset(&srf, 0, sizeof(srf));
		const int count = (int)indexes.size() - first;
		if (count == 0 && !keepEmpty)
			return;
		srf.surfaceType = SF_VBO_MDVMESH;
		srf.mdvModel = (mdvModel_t *)cut->mdv;
		srf.mdvSurface = &cut->mdv->surfaces[s];
		srf.vbo = &cut->vbo;
		srf.ibo = &cut->ibo;
		srf.indexOffset = first;
		srf.numIndexes = count;
		srf.minIndex = count ? lo : 0;
		srf.maxIndex = count ? hi : 0;
		srf.numVerts = count ? (int)(hi - lo + 1) : 0;
	};
	for (int s = 0; s < numSurfaces; ++s)
	{
		// a skeleton surface left without triangles draws nothing
		const bool cutSurface = cut->mdv->surfaces[s].plantSkin != NULL;
		if (cutSurface)
			fill(cut->surfaces[s], s, m.tris, true);
		else
			Com_Memset(&cut->surfaces[s], 0, sizeof(srfVBOMDVMesh_t));
	}
	for (Piece *piece : cut->pieces)
	{
		piece->surfaces.resize(numSurfaces);
		for (int s = 0; s < numSurfaces; ++s)
			fill(piece->surfaces[s], s, piece->tris, false);
		piece->draw.numSurfaces = numSurfaces;
		piece->draw.surfaces = piece->surfaces.data();
	}

	// the draws queued so far use the old buffers: run them first
	R_IssuePendingRenderCommands();
	if (!cut->vbo.vertexesVBO)
		qglGenBuffers(1, &cut->vbo.vertexesVBO);
	if (!cut->ibo.indexesVBO)
		qglGenBuffers(1, &cut->ibo.indexesVBO);
	qglBindBuffer(GL_ARRAY_BUFFER, cut->vbo.vertexesVBO);
	qglBufferData(GL_ARRAY_BUFFER, data.size(), data.data(), GL_STATIC_DRAW);
	qglBindBuffer(GL_ARRAY_BUFFER, 0);
	qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cut->ibo.indexesVBO);
	qglBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX(indexes.size(), (size_t)3) * sizeof(glIndex_t),
		indexes.empty() ? NULL : indexes.data(), GL_STATIC_DRAW);
	qglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glState.currentVBO = NULL;
	glState.currentIBO = NULL;
	GL_CheckErrors();

	VBO_t *vbo = &cut->vbo;
	vbo->vertexesSize = data.size();
	cut->ibo.indexesSize = MAX(indexes.size(), (size_t)3) * sizeof(glIndex_t);
	const int ofsPosition = 0;
	const int ofsNormals = ofsPosition + (int)sizeof(vec3_t);
	const int ofsTexcoords = ofsNormals + (int)sizeof(uint32_t);
	const int ofsTangents = ofsTexcoords + (int)sizeof(vec2_t);
	const int ofsSkin = ofsTangents + (int)sizeof(uint32_t);
	vbo->offsets[ATTR_INDEX_POSITION] = ofsPosition;
	vbo->offsets[ATTR_INDEX_NORMAL] = ofsNormals;
	vbo->offsets[ATTR_INDEX_TEXCOORD0] = ofsTexcoords;
	vbo->offsets[ATTR_INDEX_TANGENT] = ofsTangents;
	vbo->offsets[ATTR_INDEX_BONE_WEIGHTS] = ofsSkin;
	vbo->offsets[ATTR_INDEX_COLOR] = ofsColor;
	vbo->offsets[ATTR_INDEX_TEXCOORD1] = ofsLMCoords;
	vbo->offsets[ATTR_INDEX_TEXCOORD2] = ofsLMCoords;
	vbo->offsets[ATTR_INDEX_TEXCOORD3] = ofsLMCoords;
	vbo->offsets[ATTR_INDEX_TEXCOORD4] = ofsLMCoords;
	vbo->offsets[ATTR_INDEX_LIGHTDIRECTION] = ofsLightDir;

	vbo->strides[ATTR_INDEX_POSITION] = stride;
	vbo->strides[ATTR_INDEX_NORMAL] = stride;
	vbo->strides[ATTR_INDEX_TEXCOORD0] = stride;
	vbo->strides[ATTR_INDEX_TANGENT] = stride;
	vbo->strides[ATTR_INDEX_BONE_WEIGHTS] = stride;
	vbo->strides[ATTR_INDEX_COLOR] = sizeof(vec4_t);
	vbo->strides[ATTR_INDEX_TEXCOORD1] = sizeof(vec2_t);
	vbo->strides[ATTR_INDEX_TEXCOORD2] = sizeof(vec2_t);
	vbo->strides[ATTR_INDEX_TEXCOORD3] = sizeof(vec2_t);
	vbo->strides[ATTR_INDEX_TEXCOORD4] = sizeof(vec2_t);
	vbo->strides[ATTR_INDEX_LIGHTDIRECTION] = sizeof(uint32_t);

	vbo->sizes[ATTR_INDEX_POSITION] = sizeof(vec3_t);
	vbo->sizes[ATTR_INDEX_NORMAL] = sizeof(uint32_t);
	vbo->sizes[ATTR_INDEX_TEXCOORD0] = sizeof(vec2_t);
	vbo->sizes[ATTR_INDEX_TANGENT] = sizeof(uint32_t);
	vbo->sizes[ATTR_INDEX_BONE_WEIGHTS] = sizeof(uint32_t);
	vbo->sizes[ATTR_INDEX_COLOR] = sizeof(vec4_t);
	vbo->sizes[ATTR_INDEX_TEXCOORD1] = sizeof(vec2_t);
	vbo->sizes[ATTR_INDEX_TEXCOORD2] = sizeof(vec2_t);
	vbo->sizes[ATTR_INDEX_TEXCOORD3] = sizeof(vec2_t);
	vbo->sizes[ATTR_INDEX_TEXCOORD4] = sizeof(vec2_t);
	vbo->sizes[ATTR_INDEX_LIGHTDIRECTION] = sizeof(uint32_t);

	vbo->stepRates[ATTR_INDEX_COLOR] = MAX_INSTANCES;
	vbo->stepRates[ATTR_INDEX_TEXCOORD1] = MAX_INSTANCES;
	vbo->stepRates[ATTR_INDEX_TEXCOORD2] = MAX_INSTANCES;
	vbo->stepRates[ATTR_INDEX_TEXCOORD3] = MAX_INSTANCES;
	vbo->stepRates[ATTR_INDEX_TEXCOORD4] = MAX_INSTANCES;
	vbo->stepRates[ATTR_INDEX_LIGHTDIRECTION] = MAX_INSTANCES;
}

/*
=============================================================

Strikes

=============================================================
*/

static V3 RestWorld(const plantCutPose_t *pose, V3 p)
{
	return FromVec(pose->origin) + FromVec(pose->axis[0]) * p.x + FromVec(pose->axis[1]) * p.y +
		FromVec(pose->axis[2]) * p.z;
}

static void DecodeSkin(uint32_t packed, int &a, int &b, float &w)
{
	const uint32_t s = LittleLong(packed);
	a = (int)(s & 255u);
	b = (int)((s >> 8) & 255u);
	w = (float)((s >> 16) & 255u) * (1.0f / 255.0f);
}

static void Bone(const plantCutPose_t *pose, int bone, Quat &q, V3 &t)
{
	if (!pose->bones || bone <= 0 || bone >= pose->numBones)
	{
		q = QUAT_IDENTITY;
		t = MakeV3(0.0f, 0.0f, 0.0f);
		return;
	}
	const float *b = pose->bones + bone * 8;
	q.x = b[0]; q.y = b[1]; q.z = b[2]; q.w = b[3];
	t = MakeV3(b[4], b[5], b[6]);
}

// world position of vertex i in the pose (glsl/plant_skin.glsl)
static V3 SkinnedWorld(const plantCut_t *cut, const plantCutPose_t *pose, int i)
{
	const V3 ws = RestWorld(pose, cut->mesh.verts[i].rest);
	int a, b;
	float w;
	DecodeSkin(cut->skin[i], a, b, w);
	Quat qa, qb;
	V3 ta, tb;
	Bone(pose, a, qa, ta);
	Bone(pose, b, qb, tb);
	const V3 pa = Rotate(qa, ws) + ta;
	const V3 pb = Rotate(qb, ws) + tb;
	return plantcut::Lerp(pa, pb, w);
}

static void RecordSweep(const foliageSaberCut_t *strike)
{
	auto &sw = s_pc.sweeps[s_pc.nextSweep];
	VectorCopy(strike->base0, sw.p[0]);
	VectorCopy(strike->tip0, sw.p[1]);
	VectorCopy(strike->tip1, sw.p[2]);
	VectorCopy(strike->base1, sw.p[3]);
	sw.time = tr.refdef.time;
	sw.hit = false;
	s_pc.nextSweep = (s_pc.nextSweep + 1) % (int)ARRAY_LEN(s_pc.sweeps);
	s_pc.numSweeps = MIN(s_pc.numSweeps + 1, (int)ARRAY_LEN(s_pc.sweeps));
}

// this frame's strike cut something
static void MarkSweep(const foliageSaberCut_t *strike)
{
	const int k = (int)(strike - s_pc.strikes.data());
	if (k < 0 || k >= (int)s_pc.strikes.size() || k >= (int)ARRAY_LEN(s_pc.sweeps))
		return;
	s_pc.sweeps[(s_pc.firstStrikeSweep + k) % (int)ARRAY_LEN(s_pc.sweeps)].hit = true;
}

static float GroundBelow(V3 p, float fallback)
{
	vec3_t start = { p.x, p.y, p.z + 8.0f };
	vec3_t end = { p.x, p.y, p.z - 2048.0f };
	trace_t trace;
	Com_Memset(&trace, 0, sizeof(trace));
#ifdef REND2_SP
	ri.SV_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, CONTENTS_SOLID, G2_NOCOLLIDE, 0);
#else
	ri.CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID, 0);
#endif
	if (trace.allsolid || trace.startsolid || trace.fraction >= 1.0f)
		return fallback;
	return trace.endpos[2];
}

static void DropOldestPieces(int keep)
{
	while ((int)s_pc.pieces.size() > MAX(keep, 0))
	{
		// resting pieces go first
		Piece *victim = s_pc.pieces.front();
		for (Piece *p : s_pc.pieces)
		{
			if (p->resting)
			{
				victim = p;
				break;
			}
		}
		FreePiece(victim);
	}
}

// a piece of the plant freed by the strike: its rigid start from the bone most
// of it rides on, the velocity of its frond plus part of the blade's
static void SpawnPiece(plantCut_t *cut, const plantCutPose_t *pose, const foliageSaberCut_t *strike,
	std::vector<plantcut::Triangle> &tris)
{
	const plantcut::Mesh &m = cut->mesh;
	std::vector<int> verts;
	for (const plantcut::Triangle &t : tris)
		for (int k = 0; k < 3; ++k)
			verts.push_back(t.v[k]);
	std::sort(verts.begin(), verts.end());
	verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
	if (verts.empty())
		return;

	Piece *piece = new Piece();
	piece->cut = cut;
	piece->tris.swap(tris);
	piece->ent = *pose->ent;
	piece->restOrigin = FromVec(pose->origin);
	for (int i = 0; i < 3; ++i)
		piece->restAxis[i] = FromVec(pose->axis[i]);

	// the bone it rides on, the centroid and the normal
	std::unordered_map<int, float> boneWeight;
	V3 pivot = MakeV3(0.0f, 0.0f, 0.0f);
	V3 normal = MakeV3(0.0f, 0.0f, 0.0f);
	V3 velocity = MakeV3(0.0f, 0.0f, 0.0f);
	int moving = 0;
	const int numFronds = cut->mdv->plantSkel->numFronds;
	for (int i : verts)
	{
		const plantcut::Vertex &v = m.verts[i];
		pivot = pivot + RestWorld(pose, v.rest);
		normal = normal + FromVec(pose->axis[0]) * v.normal.x + FromVec(pose->axis[1]) * v.normal.y +
			FromVec(pose->axis[2]) * v.normal.z;
		int a, b;
		float w;
		DecodeSkin(cut->skin[i], a, b, w);
		boneWeight[a] += 1.0f - w;
		boneWeight[b] += w;
		if (pose->nodeVelocity && v.frond >= 0 && v.frond < numFronds)
		{
			const plantFrond_t &frond = cut->mdv->plantSkel->fronds[v.frond];
			const int node = Com_Clampi(0, frond.numNodes - 1, (int)floorf(v.u + 0.5f));
			velocity = velocity + FromVec(pose->nodeVelocity[v.frond][node]);
			++moving;
		}
	}
	pivot = pivot * (1.0f / verts.size());
	if (moving)
		velocity = velocity * (1.0f / moving);
	int bone = 0;
	float best = -1.0f;
	for (const auto &bw : boneWeight)
	{
		if (bw.second > best)
		{
			best = bw.second;
			bone = bw.first;
		}
	}
	Quat q;
	V3 t;
	Bone(pose, bone, q, t);
	piece->pivot = pivot;
	piece->q = q;
	piece->x = Rotate(q, pivot) + t;
	const float nl = plantcut::Length(normal);
	piece->normal = nl > 1e-3f ? normal * (1.0f / nl) : MakeV3(0.0f, 0.0f, 1.0f);

	// leaf or rod: the spread across the normal against the length
	float extent[2] = { 0.0f, 0.0f };
	{
		V3 farthest = pivot;
		float farD = -1.0f;
		for (int i : verts)
		{
			const V3 p = RestWorld(pose, m.verts[i].rest);
			const float d = plantcut::Dot(p - pivot, p - pivot);
			if (d > farD)
			{
				farD = d;
				farthest = p;
			}
		}
		const V3 along = farD > 1e-6f ? (farthest - pivot) * (1.0f / sqrtf(farD)) : MakeV3(1.0f, 0.0f, 0.0f);
		piece->along = along;
		const V3 across = plantcut::Cross(piece->normal, along);
		for (int i : verts)
		{
			const V3 d = RestWorld(pose, m.verts[i].rest) - pivot;
			extent[0] = MAX(extent[0], fabsf(plantcut::Dot(d, along)));
			extent[1] = MAX(extent[1], fabsf(plantcut::Dot(d, across)));
		}
	}
	piece->flat = Com_Clamp(0.0f, 1.0f, extent[1] / MAX(0.25f * extent[0], 1e-3f));

	// hull samples
	const int stride = MAX(1, (int)verts.size() / PIECE_HULL);
	for (size_t i = 0; i < verts.size(); i += stride)
		piece->hull.push_back(RestWorld(pose, m.verts[verts[i]].rest) - pivot);

	// the blade's velocity at the piece, part of it given to the piece
	const float duration = Com_Clamp(0.008f, 0.25f, strike->duration);
	const V3 b0 = FromVec(strike->base0), t0 = FromVec(strike->tip0);
	const V3 b1 = FromVec(strike->base1), t1 = FromVec(strike->tip1);
	const V3 blade = t1 - b1;
	const float s = Com_Clamp(0.0f, 1.0f, plantcut::Dot(piece->x - b1, blade) / MAX(plantcut::Dot(blade, blade), 1e-6f));
	V3 bladeVelocity = plantcut::Lerp(b1 - b0, t1 - t0, s) * (1.0f / duration);
	V3 kick = bladeVelocity * 0.12f;
	const float kickLen = plantcut::Length(kick);
	if (kickLen > 220.0f)
		kick = kick * (220.0f / kickLen);
	piece->v = velocity + kick + MakeV3(0.0f, 0.0f, 40.0f);
	// spin: mostly about the vertical, a little tumbling across the blade's
	// motion (the flutter of a falling leaf does the rest), varied per piece
	const uint32_t seed = (uint32_t)(verts[0] * 7919 + cut->cuts * 104729 + strike->id);
	const float spin = 1.0f + 1.5f * Hash01(seed);
	V3 tumble = plantcut::Cross(MakeV3(0.0f, 0.0f, 1.0f), bladeVelocity);
	const float tumbleLen = plantcut::Length(tumble);
	tumble = tumbleLen > 1e-3f ? tumble * (1.0f / tumbleLen) : MakeV3(1.0f, 0.0f, 0.0f);
	const V3 axis = tumble * 0.4f + MakeV3(0.0f, 0.0f, Hash01(seed + 1) < 0.5f ? -1.0f : 1.0f);
	piece->w = axis * (spin / plantcut::Length(axis));
	piece->groundZ = GroundBelow(piece->x, pose->groundZ);
	piece->groundAt = piece->x;
	piece->age = 0.0f;
	piece->resting = false;

	cut->pieces.push_back(piece);
	s_pc.pieces.push_back(piece);
	DropOldestPieces(r_foliageSaberCutPieces->integer);
}

/*
=============
R_PlantCutStrike

A blade step against a plant (tr_plantsim.cpp, start of the frame): cuts its
mesh in the current pose, frees the pieces that lost their anchor and
rebuilds the buffers. keepU: per frond, the u it keeps (the simulation
truncates the chains there). Returns whether the plant changed.
=============
*/
bool R_PlantCutStrike(plantCut_t *cut, const plantCutPose_t *pose, const foliageSaberCut_t *strike,
	float keepU[PLANTSKEL_MAX_FRONDS])
{
	plantcut::Mesh &m = cut->mesh;
	if (m.tris.empty() || (int)m.verts.size() >= PLANTCUT_MAX_VERTS)
		return false;

	// the pose on screen
	m.world.resize(m.verts.size());
	vec3_t mins = { FLT_MAX, FLT_MAX, FLT_MAX }, maxs = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	for (size_t i = 0; i < m.verts.size(); ++i)
	{
		const V3 p = SkinnedWorld(cut, pose, (int)i);
		m.world[i] = p;
		mins[0] = MIN(mins[0], p.x); mins[1] = MIN(mins[1], p.y); mins[2] = MIN(mins[2], p.z);
		maxs[0] = MAX(maxs[0], p.x); maxs[1] = MAX(maxs[1], p.y); maxs[2] = MAX(maxs[2], p.z);
	}
	const float *corners[4] = { strike->base0, strike->tip0, strike->tip1, strike->base1 };
	const float pad = strike->radius + 1.0f;
	for (int axis = 0; axis < 3; ++axis)
	{
		float lo = FLT_MAX, hi = -FLT_MAX;
		for (int c = 0; c < 4; ++c)
		{
			lo = MIN(lo, corners[c][axis]);
			hi = MAX(hi, corners[c][axis]);
		}
		if (hi + pad < mins[axis] || lo - pad > maxs[axis])
		{
			ri.Printf(PRINT_DEVELOPER, "saber cut: blade step misses the plant's fronds (axis %d)\n", axis);
			return false;
		}
	}

	const V3 b0 = FromVec(strike->base0), t0 = FromVec(strike->tip0);
	const V3 b1 = FromVec(strike->base1), t1 = FromVec(strike->tip1);
	plantcut::CutStats stats;
	bool changed = plantcut::CutByTriangle(m, b0, t0, t1, &stats);
	changed = plantcut::CutByTriangle(m, b0, t1, b1, &stats) || changed;
	if (!changed)
	{
		ri.Printf(PRINT_DEVELOPER, "saber cut: blade step through the plant's bounds cuts no triangle\n");
		return false;
	}
	MarkSweep(strike);
	++cut->cuts;
	const size_t trisBefore = m.tris.size();

	// the new vertices: skin of their frond until the chains are truncated
	const plantSkeleton_t *skel = cut->mdv->plantSkel;
	auto segmentsOf = [&](int f) {
		const int full = skel->fronds[f].numNodes - 1;
		return Com_Clampi(1, full, (int)ceilf(cut->keepU[f] - 1e-4f));
	};
	for (size_t i = cut->skin.size(); i < m.verts.size(); ++i)
	{
		const plantcut::Vertex &v = m.verts[i];
		cut->skin.push_back(v.frond >= 0 && v.frond < skel->numFronds ?
			R_PlantSkinPack(&skel->fronds[v.frond], segmentsOf(v.frond), v.u) : R_PlantSkinPack(NULL, 0, 0.0f));
	}

	// pieces that lost their anchor fall
	std::vector<int> triPiece;
	std::vector<char> anchored;
	const int numPieces = plantcut::Components(m, std::vector<char>(), triPiece, anchored);
	std::vector<std::vector<plantcut::Triangle>> freed(numPieces);
	std::vector<plantcut::Triangle> kept;
	kept.reserve(m.tris.size());
	for (size_t t = 0; t < m.tris.size(); ++t)
	{
		if (anchored[triPiece[t]])
			kept.push_back(m.tris[t]);
		else
			freed[triPiece[t]].push_back(m.tris[t]);
	}
	if (kept.size() != m.tris.size())
	{
		m.tris.swap(kept);
		for (std::vector<plantcut::Triangle> &tris : freed)
		{
			if (tris.empty())
				continue;
			float area = 0.0f;
			for (const plantcut::Triangle &t : tris)
				area += 0.5f * plantcut::Length(plantcut::Cross(m.world[t.v[1]] - m.world[t.v[0]],
					m.world[t.v[2]] - m.world[t.v[0]]));
			if (area >= PLANTCUT_MIN_PIECE_AREA && r_foliageSaberCutPieces->integer > 0)
				SpawnPiece(cut, pose, strike, tris);
		}
	}

	// the u every frond keeps, and the skin of the plant on the truncated chains
	std::vector<float> keep(skel->numFronds, 0.0f);
	for (const plantcut::Triangle &t : m.tris)
	{
		for (int k = 0; k < 3; ++k)
		{
			const plantcut::Vertex &v = m.verts[t.v[k]];
			if (v.frond >= 0 && v.frond < skel->numFronds)
				keep[v.frond] = MAX(keep[v.frond], v.u);
		}
	}
	bool truncated = false;
	for (int f = 0; f < skel->numFronds; ++f)
	{
		keep[f] = MIN(keep[f], cut->keepU[f]);
		truncated = truncated || keep[f] < cut->keepU[f];
		cut->keepU[f] = keep[f];
	}
	for (int f = 0; f < skel->numFronds && f < PLANTSKEL_MAX_FRONDS; ++f)
		keepU[f] = cut->keepU[f];
	if (truncated)
	{
		for (size_t i = 0; i < m.verts.size(); ++i)
		{
			const plantcut::Vertex &v = m.verts[i];
			if (v.frond >= 0 && v.frond < skel->numFronds)
				cut->skin[i] = R_PlantSkinPack(&skel->fronds[v.frond], segmentsOf(v.frond), v.u);
		}
	}

	RebuildBuffers(cut);
	ri.Printf(PRINT_DEVELOPER, "saber cut: %d triangles split, %d slit; plant keeps %d of %d triangles, %d pieces live\n",
		stats.split, stats.slit, (int)m.tris.size(), (int)trisBefore, (int)s_pc.pieces.size());
	return true;
}

/*
=============================================================

Pieces

=============================================================
*/

static void StepPiece(Piece &p, float dt, float now)
{
	const float life = MAX(r_foliageSaberCutPieceTime->value, 0.0f);
	p.age += dt;
	if (p.age > life)
	{
		// sink into the ground
		p.x.z -= dt * 12.0f;
		return;
	}
	if (p.resting)
		return;

	const int steps = MAX(1, (int)ceilf(dt / PIECE_STEP - 1e-3f));
	const float h = dt / steps;
	for (int step = 0; step < steps; ++step)
	{
		const float t = now - (steps - 1 - step) * h;
		const V3 n = Rotate(p.q, p.normal);
		const float vn = plantcut::Dot(p.v, n);
		const V3 vt = p.v - n * vn;
		// a leaf falls flat and glides (plate drag), a blade of grass like a rod
		const float kN = 1.5f + 5.0f * p.flat;
		const float kT = 1.5f - 0.8f * p.flat;
		V3 accel = MakeV3(0.0f, 0.0f, -PIECE_GRAVITY) - n * (kN * vn) - vt * kT;
		// flutter: the centre of pressure ahead of the centre turns a falling
		// leaf over and over
		const V3 torque = plantcut::Cross(n, p.v) * (0.004f * vn * p.flat);
		p.w = (p.w + torque * h) * expf(-1.2f * h);
		p.v = p.v + accel * h;
		if (R_FoliageForceWavesEnabled())
		{
			float dv[3] = { 0.0f, 0.0f, 0.0f };
			const float at[3] = { p.x.x, p.x.y, p.x.z };
			if (R_FoliageForceWaveImpulse(at, t - h, t, dv))
				p.v = p.v + MakeV3(dv[0], dv[1], dv[2]) * 0.6f;
		}
		p.x = p.x + p.v * h;
		p.q = Integrate(p.q, p.w, h);

		// the ground under it
		if (fabsf(p.x.x - p.groundAt.x) + fabsf(p.x.y - p.groundAt.y) > 48.0f)
		{
			p.groundZ = GroundBelow(p.x, p.groundZ);
			p.groundAt = p.x;
		}
		float lowest = FLT_MAX;
		for (const V3 &c : p.hull)
			lowest = MIN(lowest, Rotate(p.q, c).z);
		lowest += p.x.z;
		if (lowest < p.groundZ)
		{
			p.x.z += p.groundZ - lowest;
			if (p.v.z < 0.0f)
				p.v.z *= -0.15f;
			const float friction = expf(-10.0f * h);
			p.v.x *= friction;
			p.v.y *= friction;
			// it falls over until it lies: a leaf on its face, a blade of grass
			// along the ground (the tilt goes, ~0.1 s); its spin about the
			// vertical stops
			V3 from, to;
			if (p.flat >= 0.5f)
			{
				from = n.z < 0.0f ? n * -1.0f : n;
				to = MakeV3(0.0f, 0.0f, 1.0f);
			}
			else
			{
				from = Rotate(p.q, p.along);
				const float hl = sqrtf(from.x * from.x + from.y * from.y);
				to = hl > 1e-3f ? MakeV3(from.x / hl, from.y / hl, 0.0f) : MakeV3(1.0f, 0.0f, 0.0f);
			}
			const V3 tilt = plantcut::Cross(from, to);
			const float s = plantcut::Length(tilt);
			const float angle = atan2f(s, plantcut::Dot(from, to));
			const V3 settle = s > 1e-5f ? tilt * (10.0f * angle / s) : MakeV3(0.0f, 0.0f, 0.0f);
			p.w = settle + MakeV3(0.0f, 0.0f, p.w.z * expf(-6.0f * h));
			if (angle < 0.1f && plantcut::Dot(p.v, p.v) < 16.0f && fabsf(p.w.z) < 0.3f)
			{
				p.resting = true;
				break;
			}
		}
	}
}

static void AddPieceEntity(const Piece &p)
{
	refEntity_t e = p.ent;
	const V3 origin = Rotate(p.q, p.restOrigin - p.pivot) + p.x;
	VectorSet(e.origin, origin.x, origin.y, origin.z);
	VectorCopy(e.origin, e.oldorigin);
	for (int i = 0; i < 3; ++i)
	{
		const V3 a = Rotate(p.q, p.restAxis[i]);
		VectorSet(e.axis[i], a.x, a.y, a.z);
	}
	VectorSet(e.lightingOrigin, p.x.x, p.x.y, p.x.z);
	e.renderfx |= RF_LIGHTING_ORIGIN;
	e.frame = e.oldframe = 0;
	trRefEntity_t *ent = R_AddSceneEntity(&e);
	if (!ent)
		return;
	ent->plantCutDraw = &p.draw;
	ent->plantCutDebris = qtrue;
}

static void DebugSweeps(const refdef_t *fd)
{
	if (!r_plantDebug || !(r_plantDebug->integer & 32))
		return;
	static const byte hit[4] = { 255, 60, 60, 255 };
	static const byte miss[4] = { 90, 90, 255, 255 };
	for (int i = 0; i < s_pc.numSweeps; ++i)
	{
		const auto &sw = s_pc.sweeps[i];
		if (fd->time - sw.time > 1500 || fd->time < sw.time)
			continue;
		for (int k = 0; k < 4; ++k)
			R_FoliageDebugSegment(fd, sw.p[k], sw.p[(k + 1) % 4], sw.hit ? hit : miss);
		R_FoliageDebugSegment(fd, sw.p[0], sw.p[2], sw.hit ? hit : miss);
	}
}

/*
=============
R_PlantCutBeginScene

RE_BeginScene of a world scene: once per frame the blade steps are cut into
the plants and the pieces fall; every world scene draws the pieces
=============
*/
void R_PlantCutBeginScene(const refdef_t *fd)
{
	if ((fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) || !tr.world)
		return;
	if (s_pc.frame != tr.frameCount)
	{
		s_pc.frame = tr.frameCount;
		if (!s_pc.strikes.empty())
		{
			s_pc.firstStrikeSweep = s_pc.nextSweep;
			for (const foliageSaberCut_t &strike : s_pc.strikes)
				RecordSweep(&strike);
			if (R_PlantCutEnabled())
				R_PlantSimProcessCuts();
			R_PlantCutClearStrikes();
		}
		const bool consecutive = fd->time >= s_pc.lastTime && fd->time - s_pc.lastTime <= 250;
		const float dt = consecutive ? (fd->time - s_pc.lastTime) * 0.001f : 0.0f;
		s_pc.lastTime = fd->time;
		if (dt > 0.0f)
		{
			const float life = MAX(r_foliageSaberCutPieceTime->value, 0.0f) + PIECE_SINK_TIME;
			for (size_t i = 0; i < s_pc.pieces.size(); )
			{
				Piece *p = s_pc.pieces[i];
				StepPiece(*p, dt, fd->time * 0.001f);
				if (p->age > life)
					FreePiece(p);
				else
					++i;
			}
		}
		DropOldestPieces(r_foliageSaberCutPieces->integer);
	}
	for (const Piece *p : s_pc.pieces)
		AddPieceEntity(*p);
	DebugSweeps(fd);
}

void R_PlantCutReset(void)
{
	// the pieces belong to their plants' cuts, freed with them
	s_pc.strikes.clear();
	s_pc.numSweeps = 0;
	s_pc.nextSweep = 0;
	s_pc.frame = -1;
}

void R_PlantCutInfo(void)
{
	int resting = 0;
	for (const Piece *p : s_pc.pieces)
		resting += p->resting;
	ri.Printf(PRINT_ALL, "saber cut: %s, %d pieces (%d resting), %d blade steps queued\n",
		R_PlantCutEnabled() ? "on" : "off", (int)s_pc.pieces.size(), resting, (int)s_pc.strikes.size());
}
