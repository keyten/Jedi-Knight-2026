// Current-frame exact-geometry model occlusion. Included by tr_backend.cpp.
// Queries wrap the existing depth draw, so no guessed skeletal bounds and no
// additional geometry pass are needed. Never use previous-view visibility.
#include "ModelOcclusionPolicy.h"
#include <utility>

static constexpr unsigned MODEL_OCCLUSION_QUERIES = 128;
struct ModelOcclusionQuery
{
	GLuint name = 0;
	bool pending = false;
	unsigned depthItems = 0;
	unsigned guardedDraws = 0;
	unsigned readyDraws = 0;
	uint64_t readyIndices = 0;
};
static ModelOcclusionQuery rb_modelQueries[MODEL_OCCLUSION_QUERIES];
static std::vector<std::pair<uintptr_t, unsigned>> rb_modelQuerySurfaces;
static unsigned rb_modelOcclusionTag;
static unsigned rb_modelQueryCount, rb_modelOccludedCount, rb_modelGuardedCount, rb_modelSkippedCount;
static uint64_t rb_modelSkippedIndices;

static void RB_ResetModelOcclusion()
{
	rb_modelOcclusionTag = 0;
	rb_modelQuerySurfaces.clear();
	// Availability is a nonblocking poll. Busy objects stay reserved and query
	// exhaustion fails open; no GL_QUERY_RESULT access until availability is true.
	for (auto& query : rb_modelQueries)
	{
		if (!query.pending) continue;
		GLint available = 0;
		qglGetQueryObjectiv(query.name, GL_QUERY_RESULT_AVAILABLE, &available);
		if (!available) continue;
		if (r_modelOcclusionDebug->integer)
		{
			GLuint visible = 1;
			qglGetQueryObjectuiv(query.name, GL_QUERY_RESULT, &visible);
			++rb_modelQueryCount;
			rb_modelGuardedCount += query.guardedDraws;
			if (!visible)
			{
				++rb_modelOccludedCount;
				rb_modelSkippedCount += query.readyDraws;
				rb_modelSkippedIndices += query.readyIndices;
			}
		}
		query.pending = false;
	}
	static int lastPrint;
	const int now = ri.Milliseconds();
	if (r_modelOcclusionDebug->integer && now - lastPrint >= 1000)
	{
		ri.Printf(PRINT_ALL, "model occlusion: resolved=%u hidden=%u guardedDraws=%u skippedDraws=%u skippedIndices=%llu (confirmed lower bound, current-frame no-wait)\n",
			rb_modelQueryCount, rb_modelOccludedCount, rb_modelGuardedCount, rb_modelSkippedCount,
			(unsigned long long)rb_modelSkippedIndices);
		rb_modelQueryCount = rb_modelOccludedCount = rb_modelGuardedCount = rb_modelSkippedCount = 0;
		rb_modelSkippedIndices = 0;
		lastPrint = now;
	}
}

static bool RB_ModelOcclusionView()
{
	const ModelOcclusionPolicy::View view = {
		r_modelOcclusion->integer != 0, r_depthPrepass->integer != 0, r_depthPrepassVerify->integer != 0,
		qglBeginConditionalRender != nullptr, backEnd.viewParms.viewParmType == VPT_MAIN,
		(backEnd.viewParms.flags & VPF_DEPTHSHADOW) != 0, backEnd.viewParms.isPortal != 0,
		backEnd.viewParms.isSkyPortal != 0, backEnd.skyPortalOccluders != 0,
		backEnd.refractionFill != 0, (backEnd.refdef.rdflags & RDF_NOWORLDMODEL) != 0};
	return ModelOcclusionPolicy::Eligible(view);
}

static void RB_PrepareModelOcclusion(drawSurf_t *surfaces, int count)
{
	if (!RB_ModelOcclusionView()) return;
	unsigned next = 0;
	for (int i = 0; i < count; ++i)
	{
		auto& surface = surfaces[i];
		// MD3 remains available to the instancing path. Unknown/custom/deformed
		// geometry, gore, post render and depth hacks retain ordinary submission.
		if (*surface.surface != SF_MDX) continue;
		int entityNum, cube, post;
		shader_t *shader;
		R_DecomposeSort(surface.sort, &entityNum, &shader, &cube, &post);
		if (entityNum == REFENTITYNUM_WORLD) continue;
		const auto& entity = backEnd.refdef.entities[entityNum];
		const int safeFlags = RF_MINLIGHT | RF_THIRD_PERSON | RF_NOSHADOW | RF_LIGHTING_ORIGIN |
			RF_RGB_TINT | RF_CHARACTER | RF_CHARACTER_PLAYER | RF_CHARACTER_NO_CULL | RF_G2MINLOD | RF_MORELIGHT;
		// Keep close models on the original batching/submission path. This is an
		// admission fallback, never a guessed visibility or skeletal bound.
		vec3_t cameraOffset;
		VectorSubtract(entity.e.origin, backEnd.viewParms.ori.origin, cameraOffset);
		const float cameraDistanceSquared = DotProduct(cameraOffset, cameraOffset);
		const bool nearCamera = !std::isfinite(cameraDistanceSquared) || cameraDistanceSquared <= 256.0f * 256.0f;
		const auto *ghoul = (const CRenderableSurface *)surface.surface;
		bool gore = false;
#ifdef _G2_GORE
		gore = ghoul->alternateTex || ghoul->goreChain;
#endif
		const ModelOcclusionPolicy::Surface policy = {true, false, post != 0,
			shader->sort == SS_OPAQUE, shader->depthPrepass == DEPTHPREPASS_SIMPLE,
			shader->numDeforms != 0, shader->polygonOffset != 0, shader->useDistortion != 0,
			shader->cullType == tr.defaultShader->cullType, (entity.e.renderfx & ~safeFlags) == 0,
			entity.phongTessStrength > 0.0f, gore,
			ghoul->vboMesh && ghoul->vboMesh->vbo && ghoul->vboMesh->ibo, nearCamera,
			ghoul->vboMesh ? (unsigned)ghoul->vboMesh->numIndexes : 0};
		if (!ModelOcclusionPolicy::Eligible(policy)) continue;
		while (next < MODEL_OCCLUSION_QUERIES && rb_modelQueries[next].pending) ++next;
		if (next == MODEL_OCCLUSION_QUERIES) break;
		auto& query = rb_modelQueries[next];
		if (!query.name) qglGenQueries(1, &query.name);
		query.depthItems = query.guardedDraws = query.readyDraws = 0;
		query.readyIndices = 0;
		rb_modelQuerySurfaces.emplace_back((uintptr_t)surface.surface, ++next);
	}
	std::sort(rb_modelQuerySurfaces.begin(), rb_modelQuerySurfaces.end());
}

static unsigned RB_ModelOcclusionSlot(surfaceType_t *surface)
{
	if (rb_modelQuerySurfaces.empty() || !RB_ModelOcclusionView()) return 0;
	const auto key = std::make_pair((uintptr_t)surface, 0u);
	const auto it = std::lower_bound(rb_modelQuerySurfaces.begin(), rb_modelQuerySurfaces.end(), key);
	if (it == rb_modelQuerySurfaces.end() || it->first != key.first) return 0;
	const auto& query = rb_modelQueries[it->second - 1];
	return backEnd.depthFill || (query.depthItems == 1 && query.pending) ? it->second : 0;
}

static bool RB_ModelOcclusionDrawCompatible(const DrawItem& item)
{
	return item.draw.primitiveType == GL_TRIANGLES && !item.cubeFaceMask &&
		!item.renderState.transformFeedback &&
		!(item.renderState.stateBits & (GLS_DEPTHTEST_DISABLE | GLS_POLYGON_OFFSET_FILL)) &&
		(item.renderState.stateBits & GLS_DEPTHFUNC_BITS) != GLS_DEPTHFUNC_GREATER &&
		item.renderState.depthRange.minDepth == 0.0f && item.renderState.depthRange.maxDepth == 1.0f;
}

void RB_ShutdownModelOcclusion()
{
	for (auto& query : rb_modelQueries)
	{
		if (query.name) qglDeleteQueries(1, &query.name);
		query = {};
	}
	rb_modelQuerySurfaces.clear();
	rb_modelOcclusionTag = 0;
}
