// CPU reconstruction input, independent of BSP storage and renderer state.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_types.h"
#include <cstddef>
#include <memory>

namespace Rend2LightRecon { namespace Core {

static constexpr const char* kInputSchema = "org.openjk.lightrecon-input";
static constexpr uint32_t kInputVersion = 1;
enum class Code { Ok, Unavailable, InvalidInput, UnsupportedSchema, MissingGeometry,
	ProviderError, Cancelled, MemoryLimit, WorkLimit, InvalidState, AlgorithmError };
struct Error
{
	Code code = Code::Ok;
	std::string message, path;
	Id record = 0;
	uint64_t ordinal = 0;
	explicit operator bool() const { return code == Code::Ok; }
};
Error Failure(Code code, const std::string& message, const std::string& path = std::string());

class Cancellation
{
public:
	virtual ~Cancellation() = default;
	virtual bool IsCancelled() const noexcept = 0;
};
struct ReadLimits
{
	size_t maxRecords = 1024;
	uint64_t maxBytes = 8u * 1024u * 1024u; // fixed record span per request, not total process RSS
};
struct ReadContext
{
	uint64_t maxBytes = 0;
	const Cancellation* cancellation = nullptr; // borrowed only for this call
};

// Read-only span + strong backing-store lease. Provider freezes the memory for
// the lease lifetime. The session never takes a naked borrowed observation array.
// Caller-held chunks can outlive a reader/session. No automatic prefetch occurs.
template<typename T> struct Chunk
{
	std::shared_ptr<const void> owner;
	const T* data = nullptr;
	size_t count = 0;
};
template<typename T> Chunk<T> VectorChunk(std::shared_ptr<const std::vector<T>> records, size_t first, size_t count)
{
	Chunk<T> chunk;
	if (!records || first > records->size() || count > records->size() - first) return chunk;
	chunk.owner = records;
	chunk.data = count ? records->data() + first : nullptr;
	chunk.count = count;
	return chunk;
}
template<typename T> class ChunkSource
{
public:
	virtual ~ChunkSource() = default;
	// Stable ordinal access: exactly count records or an explicit error. IDs must
	// be strictly ascending in stream order. Inputs/revision cannot change while
	// sessions hold this provider. Honour context before decoding a bounded block.
	virtual Error Read(uint64_t first, size_t count, const ReadContext& context, Chunk<T>& out) const = 0;
};

struct ObservedLighting
{
	bool known = false;
	Vec3 rgb; // finite nonnegative linear Rec.709; illuminance-equivalent or relative
	PhotometricUnit unit = PhotometricUnit::Relative; // Lux or Relative
	Id calibration = 0;
	uint32_t issues = 0; // clipped/missing/quantized/etc.; NOT silently repaired
};
struct SurfaceObservation
{
	Id id = 0, surface = 0, material = 0, provenance = 0;
	Vec3 positionWorld, normalWorld; // normal points out of receiver, unit when known
	bool normalKnown = false;
	ObservedLighting lighting;
	float areaWorldSquared = 0.0f; // optional footprint, >=0; zero means unspecified
};
struct GridObservation
{
	Id id = 0, provenance = 0;
	Vec3 positionWorld, directionToSource; // unit receiver -> light when directionKnown
	bool directionKnown = false;
	ObservedLighting ambient, directed; // kept separate; never assume A+D for historical LDR
};
enum class GridMeaning { Unknown, SeparateComponents, LegacyLdrMaximum, LegacyHdrSum };
struct Triangle
{
	Id id = 0, material = 0, region = 0, provenance = 0;
	Vec3 verticesWorld[3]; // right-handed winding, nondegenerate triangle
};
enum class MaterialSupport { Unknown, DiffuseApproximation, PhysicalPbr, Unsupported };
struct Material
{
	Id id = 0, provenance = 0;
	MaterialSupport support = MaterialSupport::Unknown;
	Vec3 baseColorLinear; // [0,1] Rec.709, not display RGB
	float metallic = 0.0f, perceptualRoughness = 1.0f;
};
enum class HintKind { PhysicalEmitter, Sun, EmissiveSurface, TransportAperture };
struct SourceHint
{
	Id id = 0, provenance = 0;
	HintKind kind = HintKind::PhysicalEmitter;
	PhysicalEmitter emitter; // active only for PhysicalEmitter/Sun; never auto-promoted
	Id geometry = 0; // stable surface/triangle key for surface/aperture hints
	Photometry surfaceEmission; // Nits/Relative for EmissiveSurface only
};

struct InputManifest
{
	std::string schema = kInputSchema;
	uint32_t version = kInputVersion;
	std::string lineage, revision; // revision/content identity pins deterministic input
	Units units;
	Id provenance = 0;
	std::vector<Provenance> provenanceRecords;
	std::vector<Calibration> calibrations;
	std::vector<GeometryRegion> regions;
	std::vector<HypothesisGroup> hypothesisGroups;
	std::vector<Dataset> referencedDatasets; // small canonical references, no observation payloads
	uint64_t surfaces = 0, grid = 0, triangles = 0, materials = 0, hints = 0;
	GridMeaning gridMeaning = GridMeaning::Unknown;
};

class VisibilityProvider
{
public:
	virtual ~VisibilityProvider() = default;
	// Explicit transmittance [0,1]. Segment endpoints are world positions, sky
	// direction is unit receiver -> sky. Defaults report Unavailable, never clear.
	virtual Error Segment(const Vec3& receiver, const Vec3& target, const ReadContext&, float& transmittance) const;
	virtual Error Sky(const Vec3& receiver, const Vec3& directionToSky, const ReadContext&, float& transmittance) const;
};
class LightmapObservations;
struct InputDataset
{
	InputManifest manifest;
	std::shared_ptr<const ChunkSource<SurfaceObservation>> surfaces;
	std::shared_ptr<const ChunkSource<GridObservation>> grid;
	std::shared_ptr<const ChunkSource<Triangle>> triangles;
	std::shared_ptr<const ChunkSource<Material>> materials;
	std::shared_ptr<const ChunkSource<SourceHint>> hints;
	std::shared_ptr<const VisibilityProvider> visibility;
	// Original image-space observations, distinct from fitted surface samples.
	// Immutable independently owned buffers; see lightmaps.h for encoding/UV rules.
	std::shared_ptr<const LightmapObservations> lightmaps;
};

// No chunk reads or visibility calls: validate only manifest/ownership contracts.
Error ValidateInput(const InputDataset& input);
ReconstructedLightingAsset AssetMetadata(const InputManifest& manifest);

class InputReader
{
public:
	InputReader(InputDataset input, ReadLimits limits = ReadLimits(),
		std::shared_ptr<const Cancellation> cancellation = std::shared_ptr<const Cancellation>());
	const InputManifest& Metadata() const { return input_.manifest; }
	const ReadLimits& Limits() const { return limits_; }
	bool HasVisibility() const { return !!input_.visibility; }
	std::shared_ptr<const LightmapObservations> Lightmaps() const { return input_.lightmaps; }
	Error ValidateMetadata() const;
	Error ReadSurfaces(uint64_t first, size_t count, Chunk<SurfaceObservation>& out);
	Error ReadGrid(uint64_t first, size_t count, Chunk<GridObservation>& out);
	Error ReadTriangles(uint64_t first, size_t count, Chunk<Triangle>& out);
	Error ReadMaterials(uint64_t first, size_t count, Chunk<Material>& out);
	Error ReadHints(uint64_t first, size_t count, Chunk<SourceHint>& out);
	Error TraceSegment(const Vec3& receiver, const Vec3& target, float& transmittance) const;
	Error TraceSky(const Vec3& receiver, const Vec3& directionToSky, float& transmittance) const;
private:
	struct Ordering { uint64_t end = 0; Id last = 0; };
	template<typename T> Error Read(const std::shared_ptr<const ChunkSource<T>>& source,
		uint64_t total, uint64_t first, size_t count, Chunk<T>& out, Ordering& order, const char* path);
	InputDataset input_;
	ReadLimits limits_;
	std::shared_ptr<const Cancellation> cancellation_;
	Error metadataError_;
	Ordering surfaceOrder_, gridOrder_, triangleOrder_, materialOrder_, hintOrder_;
};

} } // namespace Rend2LightRecon::Core
