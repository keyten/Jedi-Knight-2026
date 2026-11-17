// Periodic material detail synthesized once per renderer initialization.
#include "tr_local.h"

#include <vector>

namespace {

constexpr int TileSize = 128;
constexpr float Tau = 6.28318530718f;

enum Pattern { None, Granules, Stone, Coating, MetalGrain, Fiber, Veins, GlassMarks,
	Weave, Pores, Frost, PlasterGrain };

// Keep these generator IDs in sync with the analytic microdetail in lightall.glsl.
enum MicroPattern { MicroNone, MicroGrain, MicroPit, MicroFinish, MicroScratch, MicroFiber, MicroWeave };
struct MicroProfile {
	MicroPattern pattern;
	float longitudinalScale;
};

constexpr MicroProfile MicroProfiles[] = {
	{MicroNone,    1.00f}, // None
	{MicroGrain,   1.00f}, // Granules
	{MicroPit,     1.00f}, // Stone
	{MicroFinish,  1.00f}, // Coating
	{MicroScratch, 0.20f}, // MetalGrain
	{MicroFiber,   0.24f}, // Fiber
	{MicroFiber,  -0.25f}, // Veins (elongate along V)
	{MicroScratch, 0.20f}, // GlassMarks
	{MicroWeave,   1.00f}, // Weave
	{MicroPit,     1.00f}, // Pores
	{MicroGrain,   1.00f}, // Frost
	{MicroFinish,  1.00f}, // PlasterGrain
};
static_assert(ARRAY_LEN(MicroProfiles) == PlasterGrain + 1, "Update microdetail profiles");

struct Profile {
	Pattern pattern;
	int cells;
	float repeat, normal, roughness, color;
};

constexpr Profile Profiles[] = {
	{None},                                         // Unknown
	{Granules, 30, 4.8f, 0.36f, 0.05f, 0.12f},     // Sand
	{Granules, 18, 3.6f, 0.48f, 0.10f, 0.14f},     // Soil
	{Stone, 9, 3.2f, 0.26f, 0.08f, 0.07f},         // Mud
	{Granules, 12, 2.4f, 0.65f, 0.10f, 0.19f},     // Gravel
	{Stone, 8, 3.8f, 0.53f, 0.11f, 0.15f},         // Rock
	{Pores, 20, 3.8f, 0.40f, 0.09f, 0.13f},        // Concrete
	{PlasterGrain, 26, 4.0f, 0.22f, 0.06f, 0.07f}, // Plaster
	{Stone, 12, 3.0f, 0.48f, 0.09f, 0.14f},        // Masonry
	{Coating, 19, 3.4f, 0.20f, 0.07f, 0.06f},      // Tile
	{MetalGrain, 14, 4.4f, 0.22f, 0.14f, 0.00f},   // Metal
	{Coating, 16, 4.1f, 0.19f, 0.11f, 0.06f},      // PaintedMetal
	{Fiber, 15, 3.4f, 0.43f, 0.09f, 0.15f},        // Wood
	{Fiber, 9, 2.3f, 0.63f, 0.11f, 0.17f},         // Bark
	{Veins, 27, 2.8f, 0.21f, 0.05f, 0.07f},        // Grass
	{Veins, 13, 2.4f, 0.18f, 0.05f, 0.06f},        // Foliage
	{GlassMarks, 11, 2.4f, 0.10f, 0.12f, 0.02f},   // Glass (opaque stages)
	{None},                                         // Water
	{Frost, 23, 4.2f, 0.27f, 0.05f, 0.09f},        // Snow
	{Frost, 11, 2.6f, 0.26f, 0.09f, 0.08f},        // Ice
	{Weave, 26, 3.0f, 0.41f, 0.07f, 0.13f},        // Fabric
	{Weave, 17, 2.8f, 0.54f, 0.08f, 0.16f},        // Carpet
	{Pores, 16, 3.0f, 0.36f, 0.08f, 0.09f},        // Leather
	{Pores, 28, 3.4f, 0.26f, 0.06f, 0.07f},        // Rubber
	{Coating, 26, 3.2f, 0.18f, 0.07f, 0.05f},      // Plastic
	{Pores, 30, 2.7f, 0.14f, 0.04f, 0.04f},        // Skin
	{Fiber, 31, 2.7f, 0.15f, 0.05f, 0.05f},        // Hair
	{Coating, 22, 3.2f, 0.21f, 0.09f, 0.05f},      // Electronics
	{None},                                         // Sky
	{None},                                         // Effect
};
static_assert(ARRAY_LEN(Profiles) == Rend2Material::Count, "Update detail material profiles");

float Smooth(float lo, float hi, float x)
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - lo) / (hi - lo));
	return t * t * (3.0f - 2.0f * t);
}

float Random(int x, int y, int nx, int ny, uint32_t seed)
{
	const uint32_t ix = uint32_t((x % nx + nx) % nx);
	const uint32_t iy = uint32_t((y % ny + ny) % ny);
	uint32_t h = ix * 0x9e3779b9u ^ iy * 0x85ebca6bu ^ seed * 0xc2b2ae35u;
	h = (h ^ (h >> 16)) * 0x7feb352du;
	h = (h ^ (h >> 15)) * 0x846ca68bu;
	return float((h ^ (h >> 16)) >> 8) * (1.0f / 16777216.0f);
}

float Noise(float u, float v, int nx, int ny, uint32_t seed)
{
	const float x = u * nx, y = v * ny;
	const int ix = int(floorf(x)), iy = int(floorf(y));
	const float a = Smooth(0.0f, 1.0f, x - ix), b = Smooth(0.0f, 1.0f, y - iy);
	const float p = Random(ix, iy, nx, ny, seed), q = Random(ix, iy + 1, nx, ny, seed);
	return (p + a * (Random(ix + 1, iy, nx, ny, seed) - p)) * (1.0f - b) +
		(q + a * (Random(ix + 1, iy + 1, nx, ny, seed) - q)) * b;
}

struct Cells { float center, edge; };

Cells Cellular(float u, float v, int cells, uint32_t seed)
{
	const float x = u * cells, y = v * cells;
	const int ix = int(floorf(x)), iy = int(floorf(y));
	float closest = 10.0f, next = 10.0f;
	for (int cy = iy - 1; cy <= iy + 1; ++cy) {
		for (int cx = ix - 1; cx <= ix + 1; ++cx) {
			const float dx = cx + 0.2f + 0.6f * Random(cx, cy, cells, cells, seed) - x;
			const float dy = cy + 0.2f + 0.6f * Random(cx, cy, cells, cells, seed + 1) - y;
			const float d = dx * dx + dy * dy;
			next = Q_min(next, Q_max(closest, d));
			closest = Q_min(closest, d);
		}
	}
	const float center = sqrtf(closest);
	return {center, sqrtf(next) - center};
}

float Scratches(float u, float v, int cells, uint32_t seed)
{
	const float x = u * cells, y = v * cells;
	const int ix = int(floorf(x)), iy = int(floorf(y));
	const float strength = Smooth(0.38f, 0.92f, Random(ix, iy, cells, cells, seed));
	const float angle = (Random(ix, iy, cells, cells, seed + 1) - 0.5f) * 2.2f;
	const float dx = x - ix - 0.38f - 0.24f * Random(ix, iy, cells, cells, seed + 2);
	const float dy = y - iy - 0.38f - 0.24f * Random(ix, iy, cells, cells, seed + 3);
	const float halfLength = 0.13f + 0.22f * Random(ix, iy, cells, cells, seed + 4);
	const float co = cosf(angle), si = sinf(angle);
	const float along = dx * co + dy * si, across = dy * co - dx * si;
	return strength * (1.0f - Smooth(0.05f, halfLength, fabsf(along))) *
		(1.0f - Smooth(0.006f, 0.095f, fabsf(across)));
}

struct Texel { float height, roughness, color; };

Texel Synthesize(const Profile &profile, float u, float v, uint32_t seed)
{
	if (profile.pattern == None)
		return {};
	const int n = profile.cells;
	const float micro = Noise(u, v, 43, 43, seed) - 0.5f;
	const float uneven = Noise(u, v, n, n, seed + 2) - 0.5f;
	switch (profile.pattern) {
	case Granules: {
		const float grain = 1.0f - Smooth(0.07f, 0.63f, Cellular(u, v, n, seed + 3).center);
		return {0.86f * grain + 0.34f * uneven + 0.20f * micro, grain + uneven, grain + micro};
	}
	case Stone: {
		const float du = 0.075f * Noise(u, v, n, n, seed + 3);
		const float dv = 0.075f * Noise(u, v, n, n, seed + 4);
		const Cells cells = Cellular(u + du, v + dv, n, seed + 5);
		const float fissure = (1.0f - Smooth(0.01f, 0.16f, cells.edge)) *
			Smooth(0.36f, 0.76f, Noise(u, v, n + 5, n + 5, seed + 7));
		const float pitting = 1.0f - Smooth(0.05f, 0.30f, Cellular(u, v, n * 2, seed + 8).center);
		const float grit = Noise(u, v, n * 3, n * 3, seed + 10) - 0.5f;
		return {0.59f * uneven + 0.38f * grit + 0.35f * micro - 0.53f * fissure - 0.31f * pitting,
			0.5f * grit + 0.4f * micro + 0.45f * fissure,
			0.42f * grit + 0.42f * micro + 0.40f * uneven - 0.66f * fissure - 0.44f * pitting};
	}
	case Coating: {
		const float scuff = Scratches(u, v, n, seed + 3);
		return {0.38f * uneven + 0.38f * micro - 0.33f * scuff,
			0.52f * uneven + 0.9f * scuff, 0.40f * uneven + 0.72f * scuff};
	}
	case MetalGrain: {
		const float brush = Noise(u, v, 6, 47, seed + 3) - 0.5f;
		const float scratch = Scratches(u, v, n, seed + 4);
		return {0.31f * brush + 0.15f * micro - 0.59f * scratch,
			0.55f * brush + 0.95f * scratch, 0.28f * brush + 0.95f * scratch};
	}
	case Fiber: {
		const float warp = Noise(u, v, 4, n / 2, seed + 3);
		const float grain = Noise(u, v + 0.075f * warp, 5, n * 3, seed + 4) - 0.5f;
		const float grooves = Noise(u, v + 0.12f * warp, 5, n, seed + 5);
		const float split = 1.0f - Smooth(0.10f, 0.42f, grooves);
		return {0.62f * grain - 0.65f * split + 0.13f * micro,
			0.42f * grain + 0.62f * split, 0.61f * grain - 0.55f * split};
	}
	case Veins: {
		const float ribs = Noise(u + 0.042f * uneven, v, n, 4, seed + 3) - 0.5f;
		return {0.57f * ribs + 0.29f * micro, 0.55f * micro - 0.35f * ribs, 0.56f * ribs};
	}
	case GlassMarks: {
		const float scratch = Scratches(u, v, n, seed + 3);
		return {-0.37f * scratch, 0.74f * scratch + 0.15f * uneven, 0.45f * scratch};
	}
	case Weave: {
		const float x = sinf(Tau * u * n), y = sinf(Tau * v * n);
		const float weave = 0.54f * x * y + 0.22f * (x + y);
		return {weave + 0.16f * micro, 0.62f * weave + 0.35f * uneven, 0.78f * weave};
	}
	case Pores: {
		const float pore = 1.0f - Smooth(0.04f, 0.34f, Cellular(u, v, n, seed + 3).center);
		return {0.39f * uneven + 0.35f * micro - 0.76f * pore,
			0.62f * pore + 0.34f * micro, 0.30f * micro + 0.31f * uneven - 0.67f * pore};
	}
	case PlasterGrain: {
		const float grit = Noise(u, v, n * 2, n * 2, seed + 3) - 0.5f;
		const float patch = Noise(u, v, 9, 7, seed + 4);
		const float pore = (1.0f - Smooth(0.025f, 0.13f + 0.17f * patch,
			Cellular(u, v, n, seed + 5).center)) * Smooth(0.42f, 0.83f, patch);
		return {0.28f * uneven + 0.38f * grit + 0.22f * micro - 0.48f * pore,
			0.44f * grit + 0.52f * pore + 0.18f * uneven,
			0.42f * grit + 0.24f * micro - 0.53f * pore};
	}
	case Frost: {
		const Cells cells = Cellular(u, v, n, seed + 3);
		const float facet = 1.0f - Smooth(0.0f, 0.42f, cells.edge);
		return {0.62f * facet + 0.21f * micro, 0.53f * facet + 0.53f * uneven,
			0.44f * facet + 0.34f * micro};
	}
	default:
		return {};
	}
}

byte Encode(float x)
{
	// Exact zero at byte 128. Averaging slopes and offsets in linear mips
	// makes unresolved detail converge towards the original material.
	return byte(128 + int(roundf(127.0f * Com_Clamp(-1.0f, 1.0f, x))));
}

void FillTile(byte *rgba, const Profile &profile, uint32_t seed, std::vector<Texel> &tile)
{
	float roughnessMean = 0.0f, colorMean = 0.0f;
	for (int y = 0; y < TileSize; ++y) {
		for (int x = 0; x < TileSize; ++x) {
			Texel &t = tile[y * TileSize + x];
			t = Synthesize(profile, (x + 0.5f) / TileSize, (y + 0.5f) / TileSize, seed);
			roughnessMean += t.roughness;
			colorMean += t.color;
		}
	}
	roughnessMean /= TileSize * TileSize;
	colorMean /= TileSize * TileSize;
	for (int y = 0; y < TileSize; ++y) {
		for (int x = 0; x < TileSize; ++x, rgba += 4) {
			const Texel &t = tile[y * TileSize + x];
			const float dx = tile[y * TileSize + (x + TileSize - 1) % TileSize].height -
				tile[y * TileSize + (x + 1) % TileSize].height;
			const float dy = tile[((y + TileSize - 1) % TileSize) * TileSize + x].height -
				tile[((y + 1) % TileSize) * TileSize + x].height;
			rgba[0] = Encode(1.9f * dx);
			rgba[1] = Encode(1.9f * dy);
			rgba[2] = Encode(t.roughness - roughnessMean);
			rgba[3] = Encode(t.color - colorMean);
		}
	}
}

} // namespace

void R_InitDetailTextures()
{
	if (!r_detailTextureSynthesis->integer)
		return;
	if (!r_materialClassification->integer) {
		ri.Printf(PRINT_WARNING, "r_detailTextureSynthesis requires r_materialClassification 1\n");
		return;
	}
	if (!r_normalMapping->integer && !r_specularMapping->integer) {
		ri.Printf(PRINT_WARNING, "r_detailTextureSynthesis requires per-pixel lighting (normal or specular mapping)\n");
		return;
	}

	GLint fragmentUnits = 0, combinedUnits = 0;
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragmentUnits);
	qglGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &combinedUnits);
	if (fragmentUnits < 32 || combinedUnits <= TB_DETAILTEXTURE) {
		ri.Printf(PRINT_WARNING, "r_detailTextureSynthesis requires at least 32 fragment texture units\n");
		return;
	}

	tr.detailTextureArray = R_Create2DImageArray("*detailTextureSynthesis", nullptr,
		TileSize, TileSize, Rend2Material::Count, IMGTYPE_COLORALPHA,
		IMGFLAG_MUTABLE | IMGFLAG_NO_COMPRESSION, GL_RGBA8);
	GL_SelectTexture(0);
	qglBindTexture(GL_TEXTURE_2D_ARRAY, tr.detailTextureArray->texnum);
	std::vector<Texel> tile(TileSize * TileSize);
	std::vector<byte> rgba(TileSize * TileSize * 4);
	for (int layer = 0; layer < Rend2Material::Count; ++layer) {
		FillTile(rgba.data(), Profiles[layer], 73u + layer * 19u, tile);
		qglTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, TileSize, TileSize, 1,
			GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	}
	qglGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	if (r_ext_texture_filter_anisotropic->value > 0.0f && glConfig.maxTextureFilterAnisotropy > 1.0f)
		qglTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT,
			Q_min(4.0f, glConfig.maxTextureFilterAnisotropy));
	qglBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	glState.currenttextures[0] = 0;
}

void RB_SetDetailTexture(const shader_t *shader, const shaderStage_t *stage, unsigned stateBits,
	UniformDataWriter &uniforms, SamplerBindingsWriter &samplers)
{
	if (!tr.detailTextureArray)
		return;
	const Rend2Material::Result &material = shader->surfaceMaterial;
	const Profile &profile = Profiles[material.type];
	vec4_t settings = {};
	float strength = 0.0f;
	float relief = 1.0f;
	if (stage->synthesizeDetail && profile.pattern != None && !backEnd.projection2D &&
		!backEnd.depthFill && !(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_POINTSHADOW)) &&
		!(stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) && !r_lightmap->integer &&
		!r_fullbright->integer && R_ResolvedMaterialType(stage) != MATERIAL_TYPE_EYE) {
		strength = material.confidence * 0.01f;
		if (material.source != Rend2Material::Override) {
			if (material.flags & Rend2Material::MixedPBR)
				strength *= 0.35f;
			if (material.flags & Rend2Material::ConflictingPBR)
				strength *= 0.25f;
		}
		// Packed textures already tell us something about the finish. A
		// polished stone or varnished wood needs much less relief than a raw
		// one of the same semantic class. Only authored/discovered data is
		// present in these statistics.
		if (material.pbr.samples) {
			const float minimum = (profile.pattern == Stone || profile.pattern == Fiber) ? 0.32f : 0.60f;
			relief = minimum + (1.0f - minimum) * Smooth(0.12f, 0.52f, material.pbr.roughnessMean);
		}
		settings[0] = float(material.type);
		settings[1] = profile.repeat;
		settings[2] = profile.normal * strength * relief;
		settings[3] = profile.roughness * strength * r_detailTexturePBR->value;
	}
	uniforms.SetUniformVec4(UNIFORM_DETAILTEXTUREPARAMS, settings);
	uniforms.SetUniformFloat(UNIFORM_DETAILTEXTURECOLOR, profile.color * strength * relief);
	if (r_detailTextureMicro->value > 0.0f) {
		const MicroProfile &micro = MicroProfiles[profile.pattern];
		const float frequency = Com_Clamp(280.0f, 850.0f, profile.repeat * float(profile.cells) * 10.5f);
		uniforms.SetUniformVec4(UNIFORM_DETAILTEXTUREMICROPARAMS, float(micro.pattern),
			frequency, strength > 0.0f ? r_detailTextureMicro->value : 0.0f, micro.longitudinalScale);
	}
	if (strength > 0.0f)
		samplers.AddStaticImage(tr.detailTextureArray, TB_DETAILTEXTURE);
}
