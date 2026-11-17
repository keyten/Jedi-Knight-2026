// Renderer adapter for the GL-independent semantic classifier.
#include "tr_local.h"

static Rend2Material::Type JA_SurfaceMaterial(int material)
{
	using namespace Rend2Material;
	switch (material & MATERIAL_MASK) {
	case MATERIAL_SOLIDWOOD: case MATERIAL_HOLLOWWOOD: return Wood;
	case MATERIAL_SOLIDMETAL: case MATERIAL_HOLLOWMETAL: return Metal;
	case MATERIAL_SHORTGRASS: case MATERIAL_LONGGRASS: return Grass;
	case MATERIAL_DIRT: return Soil;
	case MATERIAL_SAND: return Sand;
	case MATERIAL_GRAVEL: return Gravel;
	case MATERIAL_GLASS: case MATERIAL_BPGLASS: case MATERIAL_SHATTERGLASS: return Glass;
	case MATERIAL_CONCRETE: return Concrete;
	case MATERIAL_MARBLE: case MATERIAL_ROCK: return Rock;
	case MATERIAL_WATER: return Water;
	case MATERIAL_SNOW: return Snow;
	case MATERIAL_ICE: return Ice;
	case MATERIAL_FLESH: return Skin;
	case MATERIAL_MUD: return Mud;
	case MATERIAL_DRYLEAVES: case MATERIAL_GREENLEAVES: return Foliage;
	case MATERIAL_FABRIC: case MATERIAL_CANVAS: return Fabric;
	case MATERIAL_RUBBER: return Rubber;
	case MATERIAL_PLASTIC: case MATERIAL_ARMOR: return Plastic;
	case MATERIAL_TILES: return Tile;
	case MATERIAL_CARPET: return Carpet;
	case MATERIAL_PLASTER: return Plaster;
	case MATERIAL_COMPUTER: return Electronics;
	default: return Rend2Material::Unknown;
	}
}

void R_ClassifySurfaceMaterial(shader_t *shader, shaderStage_t *stages, int count)
{
	using namespace Rend2Material;
	if (!r_materialClassification->integer) return;
	Input in;
	in.shaderName = shader->name;
	in.hasOverride = shader->surfaceMaterialOverrideSet != qfalse;
	in.overrideType = shader->surfaceMaterialOverride;
	in.material = JA_SurfaceMaterial(shader->surfaceFlags);
	if (shader->isSky) in.keyword = Sky;
	else if (shader->contentFlags & CONTENTS_WATER) in.keyword = Water;

	// Prefer a lit base over a lightmap, reflection, detail or additive overlay.
	shaderStage_t *base = nullptr;
	int best = -1;
	for (int i = 0; i < count && stages[i].active; ++i) {
		shaderStage_t *s = &stages[i];
		if (s->bundle[0].isLightmap || !s->bundle[0].image[0] || s->glow || s->isDetail ||
			s->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED ||
			s->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED_SP ||
			s->bundle[0].tcGen == TCGEN_ENVIRONMENT_MAPPED_SP_FP) continue;
		const int score = (s->glslShaderGroup == tr.lightallShader ? 4 : 0) +
			(R_IsGouraudStage(s) ? 2 : 0) + ((s->stateBits & GLS_DEPTHMASK_TRUE) ? 1 : 0);
		if (score > best) { base = s; best = score; }
	}
	if (base) {
		in.diffuseName = base->bundle[0].image[0]->imgName;
		if (in.keyword == Rend2Material::Unknown) {
			if (base->materialTypeSet && base->materialType == MATERIAL_TYPE_HAIR) in.keyword = Hair;
			else if (base->materialTypeSet && base->materialType == MATERIAL_TYPE_EYE) in.keyword = Skin;
			else if (base->cloth) in.keyword = Fabric;
		}
		// AutoPBR's generated masks/default values are hypotheses, not independent
		// physical evidence. Never feed them back into semantic classification.
		if (base->pbrSource == PBR_SOURCE_EXPLICIT || base->pbrSource == PBR_SOURCE_DISCOVERED) {
			const image_t *packed = base->bundle[TB_ORMSMAP].image[0];
			if (base->specularType != SPEC_SPECGLOSS && packed && packed->surfaceMaterialPBR.samples) {
				in.pbr = packed->surfaceMaterialPBR;
				// Packed maps reset these scales to 1 in R_LoadPackedMaterialImage.
				// Non-unit authored scales invalidate coverage thresholds; do not guess.
				if (base->specularScale[0] != 1.0f || base->specularScale[3] != 1.0f) in.pbr = {};
			}
		}
		if (base->packedSpecularScaleAuthored && !base->bundle[TB_ORMSMAP].image[0]) {
			const float m = Com_Clamp(0.0f, 1.0f, base->specularScale[0]);
			in.pbr = {m, Com_Clamp(0.0f, 1.0f, base->specularScale[3]),
				m >= 0.8f ? 1.0f : 0.0f, m <= 0.2f ? 1.0f : 0.0f, 1};
		}
		materialMatch_t match = {};
		R_ClassifyMaterialName(&match, in.shaderName, in.diffuseName);
		// Keep uncertain whole-weapon/droid assumptions out of the semantic API.
		// Body-part and explicit material tokens are still useful model fallbacks.
		if (match.reason && (!Q_stricmp(match.reason, "part") || !Q_stricmp(match.reason, "character"))) {
			switch (match.cls) {
			case MATCLASS_SKIN: in.model = Skin; break;
			case MATCLASS_CLOTH: in.model = Fabric; break;
			case MATCLASS_HAIR: in.model = Hair; break;
			case MATCLASS_PLASTIC: in.model = Plastic; break;
			default: break;
			}
		}
		if (best == 0 && (!Q_stricmpn(shader->name, "gfx/", 4) || !Q_stricmpn(shader->name, "effects/", 8)))
			in.keyword = Effect;
	}
	shader->surfaceMaterial = Classify(in);
	if (base && tr.detailTextureArray && base->glslShaderGroup == tr.lightallShader &&
		(base->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) && base->bundle[0].tcGen == TCGEN_TEXTURE &&
		!base->bundle[0].numTexMods && !(base->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)))
		base->synthesizeDetail = qtrue;
}

qboolean R_SurfaceMaterialDebugActive(void)
{
	return (qboolean)(r_materialClassification && r_materialClassification->integer &&
		r_materialClassificationDebug && r_materialClassificationDebug->integer);
}

qboolean R_SurfaceMaterialDebugColor(const shader_t *shader, vec4_t out)
{
	if (!R_SurfaceMaterialDebugActive() || !shader || backEnd.projection2D ||
		backEnd.depthFill || (backEnd.viewParms.flags & VPF_DEPTHSHADOW)) return qfalse;
	VectorCopy(Rend2Material::DebugColor(shader->surfaceMaterial.type), out);
	out[3] = 3.0f; // exact flat class color, shared generic/lightall debug transport
	return qtrue;
}

void R_MaterialClassificationDump_f(void)
{
	using namespace Rend2Material;
	if (!r_materialClassification->integer) {
		ri.Printf(PRINT_ALL, "Set r_materialClassification 1 and vid_restart first (latched).\n");
		return;
	}
	const char *filter = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "all";
	Type type = Rend2Material::Unknown;
	const bool all = !Q_stricmp(filter, "all");
	if (!all && !ParseType(filter, type)) {
		ri.Printf(PRINT_ALL, "usage: r_materialClassificationDump [all|<type>]\n"); return;
	}
	int totals[Count] = {};
	ri.Printf(PRINT_ALL, "type confidence source flags metal rough metalCoverage dielectricCoverage shader (reason)\n");
	for (int i = 0; i < tr.numShaders; ++i) {
		const shader_t *s = tr.shaders[i];
		const Result &r = s->surfaceMaterial;
		++totals[r.type];
		if (!all && r.type != type) continue;
		ri.Printf(PRINT_ALL, "%s %u %s %u %.3f %.3f %.3f %.3f %s (%s%s)\n",
			TypeName(r.type), (unsigned)r.confidence, SourceName(r.source), (unsigned)r.flags,
			r.pbr.metalMean, r.pbr.roughnessMean, r.pbr.metalCoverage, r.pbr.dielectricCoverage,
			s->name, r.reason, r.pbr.samples ? "" : "; PBR stats unavailable");
	}
	for (int i = 0; i < Count; ++i) {
		const float *c = DebugColor((Type)i);
		ri.Printf(PRINT_ALL, "  %-16s %5d RGB %.2f %.2f %.2f\n", TypeName((Type)i), totals[i], c[0], c[1], c[2]);
	}
}
