// Rigid repeated MD3s: shared mesh buffers plus complete per-instance Entity data.
#include "tr_local.h"
#include "tr_allocator.h"
#include "tr_md3_instance_policy.h"
#include <cstring>
#include <vector>

static_assert(sizeof(EntityBlock) == 336, "Update the GLSL Md3Entity std140 layout with EntityBlock");
static_assert(sizeof(Md3InstancesBlock) <= 16384, "Fit the GL minimum uniform block limit");
static cvar_t *s_enabled, *s_stats;
static const EntityBlock *s_entities[MAX_REFENTITIES];
static struct { int tagged, fallback, groups, instances, saved, overflow, bytes; } s_counts;
static int s_lastStats;
static unsigned s_statsFrame = ~0u;
static void PrintStats();

void R_RegisterMd3InstanceCvars()
{
    s_enabled = R_CvarInt("r_md3Instancing", "1", CVAR_ARCHIVE, 0, 1,
        "Batch repeated rigid single-frame opaque MD3s; 0 restores individual draws");
    s_stats = R_CvarInt("r_md3InstancingStats", "0", CVAR_CHEAT, 0, 1,
        "Print MD3 candidates, instance groups, saved draw calls and capacity fallbacks for a sampled frame once per second");
}
static bool Enabled() {
    return s_enabled && s_enabled->integer && R_HasModernFeatures(MODERN_SSBO);
}
static bool RigidEntity(const trRefEntity_t& entity)
{
    const refEntity_t& e = entity.e;
    if (e.reType != RT_MODEL || e.ghoul2 || e.frame || e.oldframe ||
        (e.renderfx & ~(RF_LIGHTING_ORIGIN | RF_NOSHADOW)) || entity.mirrored ||
        entity.plantCutDraw || entity.plantCutDebris || entity.plantBoneOffset >= 0 ||
        entity.phongTessStrength > 0 || !Rend2Md3::UniformPositiveTransform(e.axis, e.modelScale))
        return false;
    const model_t *model = R_GetModelByHandle(e.hModel);
    if (!model || model->type != MOD_MESH || !model->data.mdv[0]) return false;
    for (int lod = 0; lod < model->numLods; ++lod)
        if (model->data.mdv[lod] && (model->data.mdv[lod]->numFrames != 1 ||
            model->data.mdv[lod]->plantSkel)) return false;
    return true;
}
void RB_Md3InstanceBeginScene() {
    if (s_statsFrame != backEndData->realFrameNumber) {
        PrintStats(); s_counts = {}; s_statsFrame = backEndData->realFrameNumber;
    }
    memset(s_entities, 0, sizeof(s_entities));
}
void RB_Md3InstanceCacheEntity(int index, const trRefEntity_t& entity, const EntityBlock& block)
{
    if (!Enabled() || index < 0 || index >= MAX_REFENTITIES || !RigidEntity(entity)) return;
    Allocator& allocator = *backEndData->perFrameMemory;
    const size_t used = (const char *)allocator.Mark() - (const char *)allocator.Base();
    if (!Rend2Md3::HasUploadSpace(used, allocator.GetSize(), sizeof(EntityBlock), 16)) {
        ++s_counts.overflow; return;
    }
    EntityBlock *copy = ojkAlloc<EntityBlock>(allocator);
    if (copy) { *copy = block; s_entities[index] = copy; }
}
static bool SupportedProgram(const shaderProgram_t *program)
{
    const uintptr_t p = reinterpret_cast<uintptr_t>(program);
    const uintptr_t generic = reinterpret_cast<uintptr_t>(tr.genericShader);
    const uintptr_t lightall = reinterpret_cast<uintptr_t>(tr.lightallShader);
    return (p >= generic && p < generic + sizeof(tr.genericShader)) ||
        (p >= lightall && p < lightall + sizeof(tr.lightallShader));
}
void RB_Md3InstanceTag(DrawItem& item)
{
    item.md3InstanceEntity = nullptr;
    if (!Enabled()) return;
    const uintptr_t e = reinterpret_cast<uintptr_t>(backEnd.currentEntity);
    const uintptr_t first = reinterpret_cast<uintptr_t>(backEnd.refdef.entities);
    if (e < first || e >= first + backEnd.refdef.num_entities * sizeof(trRefEntity_t)) return;
    const int index = int((e - first) / sizeof(trRefEntity_t));
    if (index >= MAX_REFENTITIES || !s_entities[index]) return;
    const bool material = tess.shader && tess.shader->sort == SS_OPAQUE &&
        tess.shader->numUnfoggedPasses == 1 && !tess.shader->numDeforms &&
        tess.foliageMotion == FOLIAGE_NONE && tess.pomMode == POM_MODE_NONE &&
        !item.renderState.waterSurface && !item.renderState.waterLegacyClip &&
        !item.renderState.phongCompareSide && !item.renderState.transformFeedback &&
        !item.particleOwner && !item.cubeFaceMask;
    bool opaque = !(item.renderState.stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS));
    // Fast depth prepass can remove the alpha-test state. Check the source too.
    if (tess.shader)
        for (int i = 0; i < tess.shader->numUnfoggedPasses; ++i)
            if (tess.xstages[i] && (tess.xstages[i]->alphaTestType != ALPHA_TEST_NONE ||
                (tess.xstages[i]->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)))) opaque = false;
    const Rend2Md3::Eligibility eligibility = {true, true, true, material && opaque,
        item.ibo && item.ibo != backEndData->currentFrame->dynamicIbo &&
        !tess.useInternalVBO && item.draw.type == DRAW_COMMAND_INDEXED &&
        item.draw.primitiveType == GL_TRIANGLES && item.draw.numInstances == 1,
        material, backEnd.viewParms.viewParmType == VPT_MAIN && !backEnd.viewParms.isPortal &&
            !(backEnd.viewParms.flags & VPF_DEPTHSHADOW) &&
            !(backEnd.refdef.rdflags & RDF_NOWORLDMODEL), SupportedProgram(item.program)};
    if (!Rend2Md3::Eligible(eligibility)) { ++s_counts.fallback; return; }
    for (uint32_t a = 0; a < item.numAttributes; ++a)
        if (!item.attributes[a].vbo || item.attributes[a].vbo == backEndData->currentFrame->dynamicVbo ||
            item.attributes[a].stepRate) { ++s_counts.fallback; return; }
    item.md3InstanceEntity = s_entities[index];
    ++s_counts.tagged;
}

struct MaterialKey {
    std::vector<unsigned char> bytes;
    template<class T> void Add(const T& value) {
        const auto *p = reinterpret_cast<const unsigned char *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(value));
    }
    uint64_t Hash() const {
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char b : bytes) hash = (hash ^ b) * 1099511628211ull;
        return hash;
    }
};
static MaterialKey Key(const DrawItem& item)
{
    MaterialKey key;
    const RenderState& r = item.renderState;
    key.Add(item.program); key.Add(item.ibo); key.Add(r.stateBits); key.Add(r.cullType);
    key.Add(r.depthRange.minDepth); key.Add(r.depthRange.maxDepth); key.Add(r.screenAux);
    key.Add(item.draw.params.indexed.indexType); key.Add(item.draw.params.indexed.numIndices);
    key.Add(item.draw.params.indexed.firstIndex); key.Add(item.draw.params.indexed.baseVertex);
    key.Add(item.numAttributes);
    for (uint32_t a = 0; a < item.numAttributes; ++a) {
        const vertexAttribute_t& v = item.attributes[a];
        key.Add(v.vbo); key.Add(v.index); key.Add(v.numComponents); key.Add(v.integerAttribute);
        key.Add(v.type); key.Add(v.normalize); key.Add(v.stride); key.Add(v.offset); key.Add(v.stepRate);
    }
    key.Add(item.numSamplerBindings);
    for (uint32_t a = 0; a < item.numSamplerBindings; ++a) {
        const SamplerBinding& s = item.samplerBindings[a];
        key.Add(s.image); key.Add(s.videoMapHandle); key.Add(s.slot);
    }
    for (uint32_t a = 0; a < item.numUniformBlockBindings; ++a) {
        const UniformBlockBinding& u = item.uniformBlockBindings[a];
        if (u.block == UNIFORM_BLOCK_ENTITY || u.block == UNIFORM_BLOCK_PREVIOUS_ENTITY) continue;
        key.Add(u.block); key.Add(u.ubo); key.Add(u.offset);
    }
    key.Add(GLSL_UniformDataHash(item.uniformData));
    return key;
}
static void PrintStats()
{
    if (!s_stats || !s_stats->integer || ri.Milliseconds() - s_lastStats < 1000) return;
    ri.Printf(PRINT_ALL, "md3Instancing: tagged %d, fallback %d, groups %d, instances %d, saved %d draws, overflow %d, upload %d bytes (sampled frame)\n",
        s_counts.tagged, s_counts.fallback, s_counts.groups, s_counts.instances, s_counts.saved, s_counts.overflow, s_counts.bytes);
    s_counts = {}; s_lastStats = ri.Milliseconds();
}
void RB_Md3InstancePrepare(int count, const DrawItem *items, const uint32_t *order,
    std::vector<DrawItem>& prepared)
{
    if (!Enabled() || count < 2) return;
    int candidates = 0;
    for (int i = 0; i < count && candidates < 2; ++i)
        candidates += items[order[i]].md3InstanceEntity != nullptr;
    if (candidates < 2) return;
    std::vector<MaterialKey> keys(count);
    Rend2Md3::Planner planner;
    // Preserve opaque depth ties: groups never cross an intervening draw or
    // incompatible material. A later coplanar mesh keeps its original winner.
    for (int i = 0; i < count; ++i) {
        const DrawItem& item = items[order[i]];
        if (!item.md3InstanceEntity) continue;
        keys[i] = Key(item);
        planner.Add(i, keys[i].Hash(), [&](int a, int b) {
            return keys[a].bytes == keys[b].bytes &&
                GLSL_UniformDataEqual(items[order[a]].uniformData, items[order[b]].uniformData);
        });
    }
    for (const auto& group : planner.groups) {
        if (group.members.size() < 2) continue;
        const int leader = group.members.front();
        const DrawItem& original = items[order[leader]];
        shaderProgram_t *program = GLSL_GetDrawVariant(original.program, GLSLV_MD3_INSTANCING);
        if (program == original.program) { s_counts.fallback += (int)group.members.size(); continue; }
        gpuFrame_t *frame = backEndData->currentFrame;
        const int scene = frame->currentScene;
        Allocator& allocator = *backEndData->perFrameMemory;
        const size_t bindingBytes = (original.numUniformBlockBindings + 1) * sizeof(UniformBlockBinding);
        const size_t allocatorUsed = (const char *)allocator.Mark() - (const char *)allocator.Base();
        if (!Rend2Md3::HasUploadSpace(frame->uboWriteOffset[scene], frame->uboSize[scene],
                sizeof(Md3InstancesBlock), glRefConfig.uniformBufferOffsetAlignment) ||
            !Rend2Md3::HasUploadSpace(allocatorUsed, allocator.GetSize(), bindingBytes, 16)) {
            s_counts.overflow += (int)group.members.size(); continue;
        }
        if (prepared.empty()) {
            prepared.resize(count);
            for (int i = 0; i < count; ++i) prepared[i] = items[order[i]];
        }
        auto *bindings = ojkAllocArray<UniformBlockBinding>(allocator, original.numUniformBlockBindings + 1);
        if (!bindings) { s_counts.overflow += (int)group.members.size(); continue; }
        Md3InstancesBlock block = {};
        for (size_t i = 0; i < group.members.size(); ++i)
            block.entities[i] = *items[order[group.members[i]]].md3InstanceEntity;
        const size_t offset = RB_BindAndUpdateFrameUniformBlock(UNIFORM_BLOCK_MD3_INSTANCES, &block);
        DrawItem& result = prepared[leader];
        result.program = program;
        result.draw.numInstances = (int)group.members.size();
        const UniformBlockBinding binding = {frame->ubo[scene], offset, UNIFORM_BLOCK_MD3_INSTANCES};
        memcpy(bindings, result.uniformBlockBindings, result.numUniformBlockBindings * sizeof(*bindings));
        bindings[result.numUniformBlockBindings++] = binding;
        result.uniformBlockBindings = bindings;
        for (size_t i = 1; i < group.members.size(); ++i) prepared[group.members[i]].draw.numInstances = 0;
        ++s_counts.groups;
        s_counts.instances += (int)group.members.size();
        s_counts.saved += (int)group.members.size() - 1;
        s_counts.bytes += sizeof(block);
    }
}
