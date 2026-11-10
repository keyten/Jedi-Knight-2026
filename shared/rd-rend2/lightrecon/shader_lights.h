// Authored q3map_surfacelight sources in the canonical lighting asset/cache.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "surfaces.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace ShaderLights
{
constexpr uint32_t kVersion = 1;
constexpr const char *kEncoding = "org.openjk.shader-surface-light";
constexpr const char *kProvenance = "org.openjk.shader-surface-extraction/1";
enum class Origin : uint32_t
{
	Shader = 1
};
struct Vertex
{
	Vec3 position, normal;
	LightmapUv uv;
};
struct Surface
{
	Origin origin = Origin::Shader;
	uint32_t ordinal = 0, model = 0, materialIndex = 0;
	BakedSurfaceKind type = BakedSurfaceKind::Unsupported;
	SurfaceMotion motion = SurfaceMotion::Unknown;
	std::string material, emissionTexture;
	float legacySurfaceLight = 0; // raw compiler units; NOT nits or a fitted amplitude
	Vec3 legacySourceColour;	  // raw q3map_lightRGB; zero means unspecified
	SurfaceCull cull = SurfaceCull::Unknown;
	uint32_t materialSignals = 0, materialUncertainty = 0;
	uint32_t patchWidth = 0, patchHeight = 0;
	uint8_t styles[4] = {255, 255, 255, 255};
	Vec3 lightmapBasis[3];		   // original face normal/orientation or patch LOD bounds
	std::vector<Vertex> vertices;  // original world vertices, or inline-model LOCAL vertices
	std::vector<uint32_t> indices; // original winding; patch vertices are a Bezier control net
};
// Complete source catalogue, independent of fitting ROIs, source budgets, masks
// and unsupported numerical shapes. Glow-only surfaces are intentionally absent.
struct Catalogue
{
	LightmapMapIdentity map;
	std::vector<Surface> surfaces;
	Disk::Digest contentKey{};
};
Error Extract(const SurfaceGeometry &, const ReadContext &, Catalogue &);
// The existing canonical extension/dataset mechanism carries full surface data.
// Exact rectangular meshes use Rectangle; other shapes keep their surface type.
// Never invent SI emission or promote authored metadata to fitted/Resolved light.
Error Append(const Catalogue &, ReconstructedLightingAsset &, std::vector<Disk::Chunk> &,
			 const ReadContext &);
bool IsShaderSource(const ReconstructedLightingAsset &, const PhysicalEmitter &);
Error Read(const ReconstructedLightingAsset &, const std::vector<Disk::Chunk> &,
		   const PhysicalEmitter &, const ReadContext &, Surface &);
// Payload validation is shared by producer sealing, cache loading and consumers.
Error Validate(const ReconstructedLightingAsset &, const std::vector<Disk::Chunk> &,
			   const ReadContext &);
} // namespace ShaderLights
} // namespace Core
} // namespace Rend2LightRecon
