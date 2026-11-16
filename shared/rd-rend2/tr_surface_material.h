// Semantic surface classification. Independent of GL and of AutoPBR's BRDF classes.
#ifndef TR_SURFACE_MATERIAL_H
#define TR_SURFACE_MATERIAL_H

#include <cstddef>
#include <cstdint>

namespace Rend2Material {
enum Type : uint8_t {
	Unknown, Sand, Soil, Mud, Gravel, Rock, Concrete, Plaster, Masonry, Tile,
	Metal, PaintedMetal, Wood, Bark, Grass, Foliage, Glass, Water, Snow, Ice,
	Fabric, Carpet, Leather, Rubber, Plastic, Skin, Hair, Electronics, Sky, Effect, Count
};
enum Source : uint8_t { Unresolved, Override, ShaderMaterial, Keyword, PBR, Name, Model };
enum Flags { MixedPBR = 1, ConflictingPBR = 2 };
enum Layout { ORM, RMO, MOXR };

// Linear data channels, before upload/picmip/compression. Never inferred from albedo.
struct PBRStats {
	float metalMean, roughnessMean, metalCoverage, dielectricCoverage;
	uint32_t samples;
};
struct Result {
	Type type;
	Source source;
	uint8_t confidence; // heuristic strength, 0..100, not a statistical probability
	uint8_t flags;
	PBRStats pbr;
	char reason[32];
};
struct Input {
	const char *shaderName = nullptr;
	const char *diffuseName = nullptr;
	Type overrideType = Unknown;
	bool hasOverride = false;
	Type material = Unknown; // JA material/q3map_material, mapped by renderer adapter
	Type keyword = Unknown;  // cloth, materialType, surfaceparm water, skyParms
	Type model = Unknown;    // conservative existing character/body-part fallback
	PBRStats pbr = {};
};

const char *TypeName(Type type);
const char *SourceName(Source source);
bool ParseType(const char *name, Type &type);
const float *DebugColor(Type type);
PBRStats SamplePacked(const uint8_t *rgba, size_t pixels, Layout layout);
Result Classify(const Input &input);
}
#endif
