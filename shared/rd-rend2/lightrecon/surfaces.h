// CPU correspondence of ORIGINAL lightmap coordinates to immutable BSP geometry.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "classification.h"

namespace Rend2LightRecon
{
namespace Core
{
static constexpr const char *kSurfaceObservationSchema = "org.openjk.baked-surface-observations";
static constexpr uint32_t kSurfaceObservationVersion = 2;
struct LightmapUv
{
	double u = 0, v = 0;
	LightmapUv() = default;
	LightmapUv(double x, double y) : u(x), v(y) {}
};
enum class BakedSurfaceKind
{
	Face,
	Patch,
	Triangles,
	Unsupported
};
enum class SurfaceMotion
{
	StaticWorld,
	BrushModel,
	Deformed,
	Unknown
};
struct BakedVertex
{
	Vec3 position, shadingNormal;
	LightmapUv lightmap[4];
	LightmapUv textureUv;				// ORIGINAL base coordinates, not a runtime tcMod result
	uint32_t sourceVertex = UINT32_MAX; // original drawVert ordinal; unknown in hand-built fixtures
	bool colourPresent = false;
	uint8_t colour[4][4] = {}; // original style RGBA, including alpha; never overbright-transformed
};
struct BakedSurface
{
	uint32_t ordinal = 0, model = 0;
	BakedSurfaceKind kind = BakedSurfaceKind::Unsupported;
	SurfaceMotion motion = SurfaceMotion::Unknown;
	std::string material;
	uint32_t materialIndex = 0;
	bool excludedMaterial = false;
	int32_t lightmap[4] = {-1, -1, -1, -1}, chartX[4] = {}, chartY[4] = {};
	uint8_t styles[4] = {255, 255, 255, 255};
	uint8_t vertexStyles[4] = {255, 255, 255, 255};
	MaterialFacts materialFacts;
	uint32_t patchWidth = 0, patchHeight = 0;
	int32_t chartWidth = 0, chartHeight = 0; // original dsurface extent, in source BSP luxels
	Vec3 lightmapOrigin, lightmapBasis[3];	 // verbatim BSP basis; patches use [0/1] as LOD bounds
	LightmapUv uvMin[4], uvMax[4]; // conservative bounds retained for unsupported geometry too
	bool uvBoundsKnown[4] = {};
	std::vector<BakedVertex> vertices;
	std::vector<uint32_t> indices; // source winding and order, relative to this surface
};
struct SurfaceLimits
{
	uint64_t maxBytes = 64ull * 1024 * 1024, maxVertices = 1024 * 1024,
			 maxIndices = 4 * 1024 * 1024;
	size_t maxSurfaces = 131072, maxPathBytes = 4096;
};
struct SurfaceReport
{
	CaptureState state = CaptureState::NotRequested;
	uint64_t bytes = 0, vertices = 0, indices = 0, faces = 0, patches = 0, triangles = 0;
	uint64_t excluded = 0, malformed = 0, vertexLit = 0;
	std::string message;
};
class SurfaceCapture;
class SurfaceGeometry
{
  public:
	const LightmapMapIdentity &Map() const
	{
		return map_;
	}
	const std::vector<BakedSurface> &Surfaces() const
	{
		return surfaces_;
	}
	const SurfaceReport &Report() const
	{
		return report_;
	}
	VertexHdrState HdrState() const
	{
		return hdrState_;
	}
	const std::string &HdrSource() const
	{
		return hdrSource_;
	}
	const Disk::Digest &HdrHash() const
	{
		return hdrHash_;
	}
	const std::vector<float> &VertexHdr() const
	{
		return vertexHdr_;
	}

  private:
	friend class SurfaceCapture;
	LightmapMapIdentity map_;
	std::vector<BakedSurface> surfaces_;
	SurfaceReport report_;
	VertexHdrState hdrState_ = VertexHdrState::NotRequested;
	std::string hdrSource_;
	Disk::Digest hdrHash_{};
	std::vector<float> vertexHdr_; // one source RGB tuple per ORIGINAL drawVert, not per style
};
class SurfaceCapture
{
  public:
	explicit SurfaceCapture(SurfaceLimits limits = SurfaceLimits()) : limits_(limits) {}
	void Begin(BuildPolicy, bool validatedCacheHit = false);
	bool Needed() const
	{
		return report_.state == CaptureState::Collecting;
	}
	Error SetMap(const LightmapMapIdentity &);
	Error Add(BakedSurface); // owns copied/moved source geometry, ordered by BSP ordinal
	Error Material(uint32_t ordinal, bool deformed, bool excluded); // parse-time material facts
	Error Material(uint32_t ordinal, const MaterialFacts &);
	Error VertexHdr(VertexHdrState, const std::string &source, const void *data, size_t bytes,
					uint64_t sourceVertices);
	Error Freeze();
	Error Fail(Code, const std::string &);
	void Reset() noexcept;
	const SurfaceReport &Report() const
	{
		return report_;
	}
	const SurfaceLimits &Limits() const
	{
		return limits_;
	}
	std::shared_ptr<const SurfaceGeometry> Acquire() const
	{
		return std::atomic_load(&published_);
	}

  private:
	SurfaceLimits limits_;
	SurfaceReport report_;
	std::unique_ptr<SurfaceGeometry> staged_;
	std::shared_ptr<const SurfaceGeometry> published_;
};
enum class LuxelCoverage
{
	Uncovered,
	FootprintOnly,
	Mapped,
	Ambiguous,
	Unsupported
};
enum LuxelIssue : uint32_t
{
	LuxelBorder = 1u << 0,
	LuxelOverlap = 1u << 1,
	LuxelDuplicate = 1u << 2,
	LuxelDegenerate = 1u << 3,
	LuxelShadingMissing = 1u << 4,
	LuxelWindingReversed = 1u << 5,
	LuxelBasisMismatch = 1u << 6,
	LuxelAreaApproximate = 1u << 7,
	LuxelMultipleCharts = 1u << 8,
	LuxelUnsupported = 1u << 9,
	LuxelNormalSeam = 1u << 10,
	LuxelMixedFootprint = 1u << 11,
	LuxelOrientationUnknown = 1u << 12,
	LuxelPrecisionExceeded = 1u << 13
};
struct SurfaceLightingObservation
{
	// Identity = pinned geometry Map().sha256 + source image identity + (x,y).
	// surface/styleSlot identify chart; shared texels are ONE energy observation.
	uint32_t x = 0, y = 0, logicalLightmap = 0, surface = 0, styleSlot = 0, materialIndex = 0;
	size_t image = 0;
	LightmapUv centreUv, footprintMin, footprintMax;
	LuxelCoverage coverage = LuxelCoverage::Uncovered;
	uint32_t issues = 0;
	bool positionKnown = false, shadingNormalKnown = false, basisValidated = false;
	bool normalOrientationKnown = false;
	bool confidenceKnown = false; // finite mapping is not a bake/reconstruction confidence estimate
	Vec3 positionWorld, geometricNormal, shadingNormal, tangentWorld;
	double coveredFraction = 0, areaWorldSquared = 0, areaErrorEstimate = 0, basisErrorWorld = 0;
	double mappingErrorBoundWorld = 0; // analytic/affine numerical tolerance, not bake calibration
};
struct MappingOptions
{
	uint64_t maxScratchBytes =
		128ull * 1024 * 1024; // conservative triangle/piece accounting, not process RSS
	size_t maxTexels = 65536, maxTriangles = 262144, maxPieces = 1048576;
	uint64_t maxWork = 32ull * 1024 * 1024;
	double positionToleranceWorld = 0.002, uvTolerance = 1e-7;
	double areaRelativeTolerance = 1e-5;
	unsigned areaMaxDepth = 4;
};
struct MappingReport
{
	uint64_t mapped = 0, padding = 0, footprintOnly = 0, ambiguous = 0, borders = 0;
	uint64_t triangles = 0, duplicates = 0, degenerate = 0, unsupported = 0, excluded = 0;
	uint64_t patchSegments = 0, unsupportedPatchUv = 0, work = 0;
	uint64_t unsupportedTexels = 0;
	double maximumBasisError = 0;
};
struct LuxelTile
{
	std::shared_ptr<const SurfaceGeometry> geometry;
	std::shared_ptr<const LightmapObservations> images;
	uint32_t x = 0, y = 0, width = 0, height = 0;
	size_t image = 0;
	std::vector<SurfaceLightingObservation> samples; // row-major, includes padding coverage mask
	MappingReport report;
};
// Explicit tile computation only: no renderer state, I/O, automatic work or per-map
// permanent sample matrix. Source UVs are normalized for the ORIGINAL logical image.
// External image alternatives use their own dimensions; caller selects the image.
Error MapLuxels(std::shared_ptr<const SurfaceGeometry>, std::shared_ptr<const LightmapObservations>,
				size_t image, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
				const MappingOptions &, const ReadContext &, LuxelTile &);
// Joins a mapped texel to raw RGB + decoder recipe without discarding
// geometric coverage. Padding/ambiguous texels explicitly return Unavailable.
Error DecodeLuxel(const Decode::Decoder &, const LuxelTile &, size_t sample, const ReadContext &,
				  Decode::LightmapSample &);
// Complete canonical observation: geometric sample + raw RGB/decoded candidates,
// immutable recipe and pinned source image/map/material identities. Construct only
// requested records instead of storing decoded colour at every mapped texel.
struct DecodedSurfaceObservation
{
	SurfaceLightingObservation surface;
	std::shared_ptr<const SurfaceGeometry> geometry;
	Decode::LightmapSample colour;
	ObservationClassification classification;
	DeluxeSample deluxe;
};
Error ReadSurfaceObservation(const Decode::Decoder &, const LuxelTile &, size_t sample,
							 const ReadContext &, DecodedSurfaceObservation &);
// Later shadow rays: caller offsets a COPY along the oriented geometric normal. Canonical
// position never changes. World units, default 0.05, bounded by 1% sqrt(area), no
// minimum forcing large offsets on tiny geometry. This is a policy, not a trace.
Error LuxelRayOrigin(const SurfaceLightingObservation &, Vec3 &out, float requestedWorld = 0.05f);
// CPU debug export (OBJ text): valid points, geometric normal lines and chart
// borders. Invalid/padding have no invented world position; comments list them.
Error LuxelDebugObj(const LuxelTile &, size_t maxSamples, std::string &);
} // namespace Core
} // namespace Rend2LightRecon
