/*
 * Renderer-side semantic hints for stock and compatible MD3 vegetation.
 * All name and geometry work is done during registration, never per vertex
 * or per frame. This file deliberately does not change rendering geometry.
 */
#include "tr_local.h"
#include <ctype.h>
#include <math.h>

static bool IsAlpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static uint16_t NameSignals(const char *name)
{
	uint16_t flags = 0;
	if (!name)
		return flags;

	// Split on digits and punctuation. Thus leaves01 yields "leaves",
	// while an unrelated word merely containing "leaf" is not a match.
	for (const char *p = name; *p; )
	{
		if (!IsAlpha(*p))
		{
			++p;
			continue;
		}
		char token[32];
		int length = 0;
		while (IsAlpha(*p))
		{
			if (length < (int)sizeof(token) - 1)
				token[length++] = (char)tolower((unsigned char)*p);
			++p;
		}
		token[length] = '\0';
		if (!strcmp(token, "leaf") || !strcmp(token, "leaves") || !strcmp(token, "foliage") || !strcmp(token, "canopee"))
			flags |= FOLIAGE_NAME_LEAF;
		else if (!strcmp(token, "fern") || !strcmp(token, "plant") || !strcmp(token, "plants"))
			flags |= FOLIAGE_NAME_PLANT;
		else if (!strcmp(token, "grass") || !strcmp(token, "reed") || !strcmp(token, "reeds") || !strcmp(token, "cattail"))
			flags |= FOLIAGE_NAME_GRASS;
		else if (!strcmp(token, "vine") || !strcmp(token, "vines") || !strcmp(token, "hangvine"))
			flags |= FOLIAGE_NAME_VINE;
		else if (!strcmp(token, "trunk") || !strcmp(token, "bark") || !strcmp(token, "stump") ||
			!strcmp(token, "trynk") || !strcmp(token, "truck") || !strcmp(token, "cylinder") ||
			!strcmp(token, "pot") ||
			!strcmp(token, "grate") || !strcmp(token, "grating") || !strcmp(token, "fence") ||
			!strcmp(token, "wire") || !strcmp(token, "railing") || !strcmp(token, "flag") ||
			!strcmp(token, "cloth") || !strcmp(token, "glass") || !strcmp(token, "screen") ||
			!strcmp(token, "decal") || !strcmp(token, "chain") || !strcmp(token, "icicle") ||
			!strcmp(token, "icicles"))
			flags |= FOLIAGE_NEGATIVE;
	}
	return flags;
}

static bool IsKnownYavinLeafMaterial(const char *name)
{
	// These ambiguous tree names were checked against assets1.pk3 MD3 surfaces
	// and models.shader/yavin.shader. The opaque trunk textures are absent.
	static const char *const names[] = {
		"tree01btga", "tree2", "tree2_b", "tree02_b", "tree06b",
		"tree06b_b", "tree08b", "tree08b_b", "tree09a", "tree09a_b",
		"tree09b", "tree09b_b", "tree09d", "tree09d_b"
	};
	static const char prefix[] = "models/map_objects/yavin/";
	if (Q_stricmpn(name, prefix, sizeof(prefix) - 1))
		return false;
	name += sizeof(prefix) - 1;
	for (const char *candidate : names)
		if (!Q_stricmp(name, candidate))
			return true;
	return false;
}

static foliageClass_t ClassFromNames(uint16_t flags)
{
	if (flags & FOLIAGE_NAME_GRASS)
		return FOLIAGE_GRASS;
	if (flags & FOLIAGE_NAME_PLANT)
		return FOLIAGE_PLANT;
	if (flags & (FOLIAGE_NAME_LEAF | FOLIAGE_NAME_VINE))
		return FOLIAGE_LEAF;
	return FOLIAGE_NONE;
}

void R_ClassifyFoliageShader(shader_t *sh, shaderStage_t *stages)
{
	if (!sh || sh->defaultShader)
		return;

	uint16_t signals = NameSignals(sh->name);
	if (!Q_stricmpn(sh->name, "models/map_objects/yavin/", 25))
		signals |= FOLIAGE_YAVIN;
	if (IsKnownYavinLeafMaterial(sh->name))
		signals |= FOLIAGE_TREE_MATERIAL;
	if (sh->cullType == CT_TWO_SIDED)
		signals |= FOLIAGE_TWO_SIDED;
	if (sh->alphaShadow)
		signals |= FOLIAGE_ALPHA_SHADOW;

	// Only the base color stage decides whether the material is cut out.
	// A later specular/effect stage must not turn an opaque prop into foliage.
	if (stages[0].active)
	{
		const shaderStage_t *base = &stages[0];
		if (base->alphaTestType != ALPHA_TEST_NONE)
			signals |= FOLIAGE_ALPHA_TEST;
		if (base->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS))
			signals |= FOLIAGE_ALPHA_BLEND;
		if (base->bundle[0].image[0])
			signals |= NameSignals(base->bundle[0].image[0]->imgName);
	}
	sh->foliageSignals = signals;
	sh->foliageHint = (uint8_t)ClassFromNames(signals);
	if (signals & FOLIAGE_TREE_MATERIAL)
		sh->foliageHint = FOLIAGE_LEAF;

	for (int i = 0; i < MAX_SHADER_STAGES; ++i)
	{
		shaderStage_t *stage = &stages[i];
		if (!stage->active || !stage->ss)
			continue;
		stage->ss->foliageClass = FOLIAGE_NONE;
		if (stage->ss->type == SURFSPRITE_EFFECT || stage->ss->type == SURFSPRITE_WEATHERFX ||
			!stage->bundle[0].image[0])
			continue;
		const uint16_t spriteNames = NameSignals(stage->bundle[0].image[0]->imgName);
		if (spriteNames & FOLIAGE_NEGATIVE)
			continue;
		stage->ss->foliageClass = (uint8_t)ClassFromNames(spriteNames);
	}
}

void R_InitFoliageModel(mdvModel_t *model, const char *path)
{
	if (!model)
		return;
	model->foliageSignals = NameSignals(path);
	if (path && !Q_stricmpn(path, "models/map_objects/", 19))
		model->foliageSignals |= FOLIAGE_MODEL_OBJECT;
	if (path && !Q_stricmpn(path, "models/map_objects/yavin/", 25))
		model->foliageSignals |= FOLIAGE_YAVIN;
	const char *base = path ? strrchr(path, '/') : nullptr;
	base = base ? base + 1 : path;
	if (base && !Q_stricmpn(base, "tree", 4))
		model->foliageSignals |= FOLIAGE_MODEL_TREE;

	ClearBounds(model->foliageMins, model->foliageMaxs);
	for (int i = 0; i < model->numFrames; ++i)
	{
		AddPointToBounds(model->frames[i].bounds[0], model->foliageMins, model->foliageMaxs);
		AddPointToBounds(model->frames[i].bounds[1], model->foliageMins, model->foliageMaxs);
	}
}

void R_InitFoliageSurface(mdvSurface_t *surface, int numFrames)
{
	if (!surface)
		return;
	surface->foliageSignals = NameSignals(surface->name);
	if (surface->numVerts >= 4 && surface->numIndexes * 2 == surface->numVerts * 3)
		surface->foliageSignals |= FOLIAGE_CARD_GEOMETRY;
	ClearBounds(surface->foliageMins, surface->foliageMaxs);
	for (int i = 0; i < surface->numVerts * numFrames; ++i)
		AddPointToBounds(surface->verts[i].xyz, surface->foliageMins, surface->foliageMaxs);
	if (surface->numVerts <= 0 || numFrames <= 0)
	{
		VectorClear(surface->foliageMins);
		VectorClear(surface->foliageMaxs);
		return;
	}
	const float cx = (surface->foliageMins[0] + surface->foliageMaxs[0]) * 0.5f;
	const float cy = (surface->foliageMins[1] + surface->foliageMaxs[1]) * 0.5f;
	const float minZ = surface->foliageMins[2];
	const float baseBand = MAX(2.0f, (surface->foliageMaxs[2] - minZ) * 0.1f);
	float rootX = 0.0f, rootY = 0.0f, radius2 = 0.0f;
	int rootVerts = 0;
	for (int i = 0; i < surface->numVerts * numFrames; ++i)
	{
		const vec3_t &v = surface->verts[i].xyz;
		const float dx = v[0] - cx, dy = v[1] - cy;
		radius2 = MAX(radius2, dx * dx + dy * dy);
		if (v[2] <= minZ + baseBand)
		{
			rootX += v[0];
			rootY += v[1];
			++rootVerts;
		}
	}
	surface->foliageXYRadius = sqrtf(radius2);
	VectorSet(surface->foliageRoot,
		rootVerts ? rootX / rootVerts : cx,
		rootVerts ? rootY / rootVerts : cy, minZ);
}

foliageResult_t R_ResolveAutoFoliage(const mdvModel_t *model,
	const mdvSurface_t *surface, const shader_t *sh, int mode)
{
	foliageResult_t result = {};
	if (mode <= 0 || !model || !surface || !sh || sh->defaultShader)
		return result;
	const uint16_t material = sh->foliageSignals;
	const uint16_t surfaceBits = surface->foliageSignals;
	const uint16_t modelBits = model->foliageSignals;
	result.reasons = material | surfaceBits | modelBits;
	if (result.reasons & FOLIAGE_NEGATIVE)
	{
		result.score = -100;
		return result;
	}
	if (!(modelBits & FOLIAGE_MODEL_OBJECT))
		return result;

	const bool alpha = (material & FOLIAGE_ALPHA_TEST) != 0;
	const bool shadowBlend = (material & (FOLIAGE_ALPHA_SHADOW | FOLIAGE_ALPHA_BLEND)) ==
		(FOLIAGE_ALPHA_SHADOW | FOLIAGE_ALPHA_BLEND);
	const uint16_t names = material | surfaceBits | (modelBits & (FOLIAGE_NAME_PLANT | FOLIAGE_NAME_GRASS));
	const foliageClass_t namedClass = ClassFromNames(names);
	const bool knownTree = (material & FOLIAGE_TREE_MATERIAL) && (modelBits & FOLIAGE_YAVIN);
	int score = 0;
	if (namedClass != FOLIAGE_NONE) score += 6;
	if (knownTree) score += 5;
	if (alpha) score += 3;
	if (shadowBlend) score += 2;
	if (material & FOLIAGE_TWO_SIDED) score += 1;
	if (modelBits & FOLIAGE_YAVIN) score += 1;
	if (surfaceBits & FOLIAGE_CARD_GEOMETRY) score += 1;
	if (modelBits & FOLIAGE_MODEL_TREE) score += 2;
	result.score = (int8_t)score;

	if ((alpha || shadowBlend) && namedClass != FOLIAGE_NONE && score >= 9)
		result.cls = (uint8_t)namedClass;
	else if (alpha && knownTree && score >= 9)
		result.cls = FOLIAGE_LEAF;
	else if (mode >= 2 && alpha && (material & FOLIAGE_TWO_SIDED) &&
		(modelBits & FOLIAGE_MODEL_TREE) && (surfaceBits & FOLIAGE_CARD_GEOMETRY) && score >= 6)
		result.cls = FOLIAGE_LEAF;
	return result;
}

const char *R_FoliageClassName(int cls)
{
	switch (cls)
	{
	case FOLIAGE_LEAF: return "LEAF";
	case FOLIAGE_PLANT: return "PLANT";
	case FOLIAGE_GRASS: return "GRASS";
	default: return "NONE";
	}
}

void R_FoliageDebugColor(int cls, vec4_t out)
{
	VectorSet4(out, 0.0f, 0.0f, 0.0f, 0.0f);
	switch (cls)
	{
	case FOLIAGE_LEAF: VectorSet4(out, 0.1f, 0.9f, 0.15f, 1.0f); break;
	case FOLIAGE_PLANT: VectorSet4(out, 1.0f, 0.65f, 0.05f, 1.0f); break;
	case FOLIAGE_GRASS: VectorSet4(out, 0.05f, 0.9f, 1.0f, 1.0f); break;
	case 4: VectorSet4(out, 0.9f, 0.1f, 0.9f, 1.0f); break;
	}
}

static void PrintReasons(uint16_t flags, char *out, size_t size)
{
	static const struct { uint16_t bit; const char *name; } reasons[] = {
		{FOLIAGE_NAME_LEAF, "leaf-name"}, {FOLIAGE_NAME_PLANT, "plant-name"},
		{FOLIAGE_NAME_GRASS, "grass-name"}, {FOLIAGE_NAME_VINE, "vine-name"},
		{FOLIAGE_ALPHA_TEST, "alpha-tested"}, {FOLIAGE_TWO_SIDED, "two-sided"},
		{FOLIAGE_ALPHA_SHADOW, "alpha-shadow"}, {FOLIAGE_YAVIN, "yavin"},
		{FOLIAGE_TREE_MATERIAL, "stock-leaf-material"}, {FOLIAGE_CARD_GEOMETRY, "card-like"},
		{FOLIAGE_NEGATIVE, "negative-name"}, {FOLIAGE_ALPHA_BLEND, "alpha-blended"},
		{FOLIAGE_MODEL_TREE, "tree-model"}, {FOLIAGE_MODEL_OBJECT, "map-object"}
	};
	out[0] = '\0';
	for (const auto &reason : reasons)
	{
		if (!(flags & reason.bit)) continue;
		if (out[0]) Q_strcat(out, size, ",");
		Q_strcat(out, size, reason.name);
	}
}

void R_PrintAutoFoliage_f(void)
{
	const char *filter = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
	ri.Printf(PRINT_ALL, "Auto foliage: registered MD3 default shaders; mode %d (safe=1, broad=2)\n",
		r_autoFoliage ? r_autoFoliage->integer : 0);
	for (int i = 1; i < tr.numModels; ++i)
	{
		const model_t *mod = tr.models[i];
		if (!mod || mod->type != MOD_MESH || !mod->data.mdv[0] ||
			(filter[0] && !strstr(mod->name, filter)))
			continue;
		const mdvModel_t *mdv = mod->data.mdv[0];
		for (int j = 0; j < mdv->numSurfaces; ++j)
		{
			const mdvSurface_t *surf = &mdv->surfaces[j];
			const shader_t *sh = surf->numShaderIndexes > 0 ?
				tr.shaders[surf->shaderIndexes[0]] : tr.defaultShader;
			if (sh->remappedShader) sh = sh->remappedShader;
			const foliageResult_t safe = R_ResolveAutoFoliage(mdv, surf, sh, 1);
			const foliageResult_t broad = R_ResolveAutoFoliage(mdv, surf, sh, 2);
			char why[256];
			PrintReasons(broad.reasons, why, sizeof(why));
			ri.Printf(PRINT_ALL, "%s | %s | %s | safe=%s broad=%s score=%d | %s\n",
				mod->name, surf->name, sh->name, R_FoliageClassName(safe.cls),
				R_FoliageClassName(broad.cls), broad.score, why[0] ? why : "no-signal");
		}
		// the frond skeleton of the plant models (tr_plantskel.cpp)
		R_PrintPlantSkeleton(mdv);
	}
}
