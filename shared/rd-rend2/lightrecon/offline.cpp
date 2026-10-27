// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "offline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <locale>
#include <type_traits>
namespace Rend2LightRecon
{
namespace Core
{
namespace Offline
{
namespace
{
void Need(bool condition, const char *message, Code code = Code::InvalidInput)
{
	if (!condition)
		throw Failure(code, message, "offline dataset");
}
void Check(Error e)
{
	if (!e)
		throw e;
}
void Check(Disk::Error e)
{
	if (!e)
		throw Failure(Code::ProviderError, e.message, "offline I/O");
}
struct Codec;
template <class T> void Fields(Codec &, T &);
struct Codec
{
	bool reading;
	uint32_t regionVersion = kLightingRegionGraphVersion;
	std::vector<uint8_t> bytes;
	size_t pos = 0;
	Options limits;
	uint64_t allocated = 0;
	Codec(const Options &l) : reading(false), limits(l) {}
	Codec(std::vector<uint8_t> b, const Options &l) : reading(true), bytes(std::move(b)), limits(l)
	{
	}
	void Charge(uint64_t n)
	{
		Need(n <= limits.maxResidentBytes - std::min(allocated, limits.maxResidentBytes),
			 "Decoded allocation limit", Code::MemoryLimit);
		allocated += n;
	}
	void Raw(void *p, size_t n)
	{
		if (reading)
		{
			Need(pos <= bytes.size() && n <= bytes.size() - pos, "Truncated section");
			if (n)
				std::memcpy(p, bytes.data() + pos, n);
			pos += n;
		}
		else
		{
			Need(n <= limits.maxChunkBytes - std::min<uint64_t>(bytes.size(), limits.maxChunkBytes),
				 "Section chunk limit", Code::MemoryLimit);
			const auto *b = static_cast<const uint8_t *>(p);
			if (n)
				bytes.insert(bytes.end(), b, b + n);
		}
	}
	template <class T>
	typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type
	One(T &v)
	{
		uint8_t b[sizeof(T)] = {};
		if (!reading)
		{
			typename std::make_unsigned<T>::type u;
			std::memcpy(&u, &v, sizeof(T));
			for (unsigned i = 0; i < sizeof(T); ++i)
				b[i] = uint8_t(u >> (8 * i));
		}
		Raw(b, sizeof(T));
		if (reading)
		{
			typename std::make_unsigned<T>::type u = 0;
			for (unsigned i = 0; i < sizeof(T); ++i)
				u |= typename std::make_unsigned<T>::type(b[i]) << (8 * i);
			std::memcpy(&v, &u, sizeof(T));
		}
	}
	void One(bool &v)
	{
		uint8_t b = v ? 1 : 0;
		One(b);
		if (reading)
		{
			Need(b <= 1, "Invalid boolean");
			v = b != 0;
		}
	}
	void One(float &v)
	{
		uint32_t b = 0;
		if (!reading)
			std::memcpy(&b, &v, 4);
		One(b);
		if (reading)
			std::memcpy(&v, &b, 4);
	}
	void One(double &v)
	{
		uint64_t b = 0;
		if (!reading)
			std::memcpy(&b, &v, 8);
		One(b);
		if (reading)
			std::memcpy(&v, &b, 8);
	}
	template <class T> typename std::enable_if<std::is_enum<T>::value>::type One(T &v)
	{
		uint32_t b = uint32_t(v);
		One(b);
		if (reading)
			v = T(b);
	}
	void One(std::string &v)
	{
		uint64_t n = v.size();
		One(n);
		Need(n <= limits.maxStringBytes, "String limit", Code::MemoryLimit);
		if (reading)
		{
			Charge(n);
			v.resize(size_t(n));
		}
		if (n)
			Raw(&v[0], size_t(n));
	}
	template <class T, size_t N> void One(T (&v)[N])
	{
		for (auto &x : v)
			One(x);
	}
	template <class T, size_t N> void One(std::array<T, N> &v)
	{
		for (auto &x : v)
			One(x);
	}
	template <class T> void One(std::vector<T> &v)
	{
		uint64_t n = v.size();
		One(n);
		Need(n <= limits.maxRecords && n <= limits.maxResidentBytes / sizeof(T),
			 "Record count/allocation limit", Code::MemoryLimit);
		if (reading)
		{
			Charge(n * sizeof(T));
			v.resize(size_t(n));
		}
		for (auto &x : v)
			One(x);
	}
	template <class T>
	typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_enum<T>::value>::type
	One(T &v)
	{
		Fields(*this, v);
	}
	template <class... T> void operator()(T &... v)
	{
		int unused[] = {0, (One(v), 0)...};
		(void)unused;
	}
	void Size(size_t &v)
	{
		uint64_t n = v;
		One(n);
		Need(n <= std::numeric_limits<size_t>::max(), "Size overflow");
		if (reading)
			v = size_t(n);
	}
	void Sizes(std::vector<size_t> &v)
	{
		uint64_t n = v.size();
		One(n);
		Need(n <= limits.maxRecords && n <= limits.maxResidentBytes / sizeof(size_t),
			 "Index allocation limit", Code::MemoryLimit);
		if (reading)
		{
			Charge(n * sizeof(size_t));
			v.resize(size_t(n));
		}
		for (auto &x : v)
			Size(x);
	}
	void Done()
	{
		if (reading)
			Need(pos == bytes.size(), "Trailing/unsupported section fields");
	}
};
void Fields(Codec &, Vec3 &);
void Fields(Codec &, Units &);
void Fields(Codec &, Bounds &);
void Fields(Codec &, LightmapMapIdentity &);
void Fields(Codec &, Decode::SourcePolicy &);
void Fields(Codec &, Decode::RendererSettings &);
void Fields(Codec &, Decode::SceneCalibration &);
void Fields(Codec &, Decode::Profile &);
void Fields(Codec &, Metadata &);
void Fields(Codec &, Summary &);
void Fields(Codec &, MaterialFacts &);
void Fields(Codec &, LightmapUv &);
void Fields(Codec &, BakedVertex &);
void Fields(Codec &, BakedSurface &);
void Fields(Codec &, GridLayout &);
void Fields(Codec &, Decode::Raw &);
void Fields(Codec &, Decode::Bounds &);
void Fields(Codec &, Decode::Value &);
void Fields(Codec &, Decode::Observation &);
void Fields(Codec &, StyleChannel &);
void Fields(Codec &, ObservationClassification &);
void Fields(Codec &, DeluxeDirection &);
void Fields(Codec &, Luxel &);
void Fields(Codec &, SpatialPlane &);
void Fields(Codec &, SpatialNode &);
void Fields(Codec &, SpatialLeaf &);
void Fields(Codec &, SpatialBrush &);
void Fields(Codec &, BspSpaceInput &);
void Fields(Codec &, DynamicBoundary &);
void Fields(Codec &, SpatialLocation &);
void Fields(Codec &, RegionCell &);
void Fields(Codec &, LightingRegion &);
void Fields(Codec &, CellConnection &);
void Fields(Codec &, RegionConnection &);
void Fields(Codec &, RegionBspLink &);
void Fields(Codec &, RegionBoundary &);
void Fields(Codec &, RegionReport &);
void Fields(Codec &, RegionSnapshot &);
void Fields(Codec &, EmitterEvidence &);
void Fields(Codec &, EvidenceReport &);
void Fields(Codec &, Entry &);
void Fields(Codec &, SurfaceLightingObservation &);
void Fields(Codec &, RegionOptions &);
void Fields(Codec &, EmitterCandidate &);
void Fields(Codec &a, Vec3 &v)
{
	a(v.x, v.y, v.z);
}
void Fields(Codec &a, Units &v)
{
	a(v.worldUnitsPerMeter, v.physicalToScene);
}
void Fields(Codec &a, Bounds &v)
{
	a(v.known, v.minWorld, v.maxWorld);
}
void Fields(Codec &a, LightmapMapIdentity &v)
{
	a(v.path, v.sha256, v.bytes);
}
void Fields(Codec &a, Decode::SourcePolicy &v)
{
	a(v.documentedTransfer, v.assumedTransfer, v.gamma, v.primaries, v.assumeRec709,
	  v.transferProvenance, v.assumptionRationale, v.encodedGain, v.linearGain, v.bakeOverbright,
	  v.bakeOverbrightBits, v.exposure, v.preExposureStops, v.magnitudeHistoryKnown, v.quantization,
	  v.nearZeroCode, v.nearSaturatedCode, v.hdrNearZero);
}
void Fields(Codec &a, Decode::RendererSettings &v)
{
	a(v.known, v.linearLighting, v.physicalPbr, v.hdr, v.toneMap, v.floatLightmaps,
	  v.mapOverbrightBits, v.requestedOverbrightBits, v.effectiveOverbrightBits, v.hdrLightmapSeen,
	  v.displayGamma, v.cameraExposure, v.exposureCompensation, v.autoExposure, v.exposureKnown,
	  v.lightmapDebug);
}
void Fields(Codec &a, Decode::SceneCalibration &v)
{
	a(v.enabled, v.relativeToScene, v.reference, v.method, v.provenance, v.presentation,
	  v.assertedUnit);
}
void Fields(Codec &a, Decode::Profile &v)
{
	a(v.version, v.identity, v.sourceProvenance, v.relativeDomain, v.source, v.renderer,
	  v.calibration);
}
void Fields(Codec &a, Metadata &v)
{
	a(v.map, v.producer, v.producerVersion, v.compiler, v.compilerVersion, v.recipe, v.recipeSha256,
	  v.compilerKnown, v.synthetic, v.hints, v.splitSeed, v.spatialBlockWorld,
	  v.visibilityPatchToleranceWorld, v.units, v.decoding);
}
void Fields(Codec &a, Summary &v)
{
	a(v.fileBytes, v.rawImageBytes, v.imagePixels, v.images, v.missingImages, v.deluxeImages,
	  v.surfaces, v.vertices, v.triangles, v.gridCells, v.missingGridCells, v.invalidGrid,
	  v.mappedSamples, v.coveredSamples, v.paddingSamples, v.ambiguousSamples, v.candidates,
	  v.evidence, v.regions, v.regionCells, v.derivedComplete);
}
void Fields(Codec &a, MaterialFacts &v)
{
	a(v.available, v.cull, v.signals, v.uncertainty, v.meaning, v.surfaceFlags, v.contentFlags,
	  v.baseTexture, v.emissionTexture, v.emissionCoordinatesKnown, v.emissionStaticCoordinates,
	  v.emissionClamp, v.baseTextureSrgb, v.baseModulationKnown, v.baseModulation,
	  v.baseColourKnown, v.baseColourLinear, v.baseLayerCount, v.emissionLayerCount,
	  v.emissionProvenance, v.emissionColourKnown, v.emissionColour, v.authoredLuminance,
	  v.legacySurfaceLight, v.legacySourceColour);
}
void Fields(Codec &a, LightmapUv &v)
{
	a(v.u, v.v);
}
void Fields(Codec &a, BakedVertex &v)
{
	a(v.position, v.shadingNormal, v.lightmap, v.textureUv, v.sourceVertex, v.colourPresent,
	  v.colour);
}
void Fields(Codec &a, BakedSurface &v)
{
	a(v.ordinal, v.model, v.kind, v.motion, v.material, v.materialIndex, v.excludedMaterial,
	  v.lightmap, v.chartX, v.chartY, v.styles, v.vertexStyles, v.materialFacts, v.patchWidth,
	  v.patchHeight, v.chartWidth, v.chartHeight, v.lightmapOrigin, v.lightmapBasis, v.uvMin,
	  v.uvMax, v.uvBoundsKnown, v.vertices, v.indices);
}
void Fields(Codec &a, GridLayout &v)
{
	a(v.originWorld, v.spacingWorld, v.bounds);
}
void Fields(Codec &a, Decode::Raw &v)
{
	a(v.encoding, v.component, v.hdrOrigin, v.missing, v.directionData, v.bytes, v.hdr);
}
void Fields(Codec &a, Decode::Bounds &v)
{
	a(v.lowerKnown, v.upperKnown, v.lower, v.upper);
}
void Fields(Codec &a, Decode::Value &v)
{
	a(v.state, v.reason, v.space, v.rgb, v.bounds, v.flags, v.censoredChannels);
}
void Fields(Codec &a, Decode::Observation &v)
{
	a(v.raw, v.channels, v.flags, v.absoluteScale, v.resolvedTransfer, v.resolvedPrimaries,
	  v.encoded, v.source, v.renderer, v.scene);
}
void Fields(Codec &a, StyleChannel &v)
{
	a(v.slot, v.id, v.status);
}
void Fields(Codec &a, ObservationClassification &v)
{
	a(v.path, v.style, v.materialSignals, v.materialUncertainty, v.meaning, v.reasons,
	  v.diffuseFitCandidate, v.supplementaryEvidence, v.emitterHint);
}
void Fields(Codec &a, DeluxeDirection &v)
{
	a(v.state, v.raw, v.signedEncoded, v.toSource, v.componentStep, v.angularBoundRadians,
	  v.confidenceKnown);
}
void Fields(Codec &a, Luxel &v)
{
	a(v.surface, v.classification, v.colour, v.deluxe, v.positiveRegion, v.negativeRegion,
	  v.partition);
}
void Fields(Codec &a, SpatialPlane &v)
{
	a(v.normal, v.distance);
}
void Fields(Codec &a, SpatialNode &v)
{
	a(v.plane, v.children);
}
void Fields(Codec &a, SpatialLeaf &v)
{
	a(v.cluster, v.area, v.firstBrush, v.brushCount);
}
void Fields(Codec &a, SpatialBrush &v)
{
	a(v.firstSide, v.sideCount, v.solid, v.sky, v.world);
}
void Fields(Codec &a, BspSpaceInput &v)
{
	a(v.identity, v.worldBounds, v.planes, v.nodes, v.leaves, v.brushes, v.sides, v.leafBrushes);
}
void Fields(Codec &a, DynamicBoundary &v)
{
	a(v.id, v.model, v.bounds, v.sweptExtentKnown);
}
void Fields(Codec &a, SpatialLocation &v)
{
	a(v.space, v.leaf, v.cluster, v.area, v.boundary, v.work);
}
void Fields(Codec &a, RegionCell &v)
{
	a(v.id, v.region, v.origin, v.units, v.representativeWorld, v.space, v.bsp, v.issues,
	  v.boundarySurfaces, v.boundaryCount);
}
void Fields(Codec &a, LightingRegion &v)
{
	a(v.id, v.staticComponent, v.bounds, v.representativeWorld, v.issues, v.firstMember,
	  v.memberCount, v.firstNeighbour, v.neighbourCount);
}
void Fields(Codec &a, CellConnection &v)
{
	a(v.a, v.b, v.axis, v.faceAreaWorldSquared, v.openingCentreWorld, v.openingBounds, v.direct,
	  v.transport);
}
void Fields(Codec &a, RegionConnection &v)
{
	a(v.a, v.b, v.clearSamples, v.blockedSamples, v.unknownSamples, v.transport,
	  v.sampledOpeningBounds, v.sampledOpeningCentreWorld, v.sampledAreaWorldSquared,
	  v.apertureCandidate, v.roomBoundaryCandidate, v.dynamicPossible);
}
void Fields(Codec &a, RegionBspLink &v)
{
	a(v.region, v.leaf, v.cluster, v.area);
}
void Fields(Codec &a, RegionBoundary &v)
{
	a(v.region, v.surface);
}
void Fields(Codec &a, RegionReport &v)
{
	a(v.freeCells, v.solidCells, v.outsideCells, v.skyCells, v.unknownCells, v.dynamicCells,
	  v.regions, v.staticComponents, v.connections, v.apertures, v.skyRegions, v.domainWork,
	  v.geometryWork, v.rays, v.ownedBytes, v.peakBuildBytes, v.resolutionLimited,
	  v.dynamicExtentUnknown, v.staticOnly);
	if (a.regionVersion >= 2)
		a(v.bspFree, v.bspSolid, v.bspUnknown, v.localInvalidated, v.globalOnlyInvalidated,
		  v.minimumLimited, v.movingUncertain, v.opticalUncertain, v.unsupportedUncertain,
		  v.representativeDisagrees, v.locallyClearUnassigned, v.bspUnclustered, v.bspPossibleBrush,
		  v.bspOutsideWorld, v.bspRefinedDisjoint);
}
void Fields(Codec &a, RegionSnapshot &v)
{
	a(v.map, v.options, v.report, v.cells, v.regions, v.connections, v.cellConnections, v.members,
	  v.neighbours, v.bspLinks, v.boundaries);
	if (a.reading)
		v.schemaVersion = a.regionVersion;
	if (a.regionVersion >= 2)
		a(v.certificationKey);
}
void Fields(Codec &a, EmitterEvidence &v)
{
	a(v.id, v.origin, v.source, v.material, v.emissionTexture, v.name, v.mode, v.raw, v.ordinal,
	  v.shapes, v.issues, v.materialSignals, v.surfaces, v.entities, v.positionKnown,
	  v.orientationKnown, v.colourKnown, v.intensityKnown, v.position, v.right, v.up, v.outward,
	  v.toSource, v.colour, v.halfWidth, v.halfHeight, v.radius, v.areaWorld, v.sourceRadiusHint,
	  v.coneDegrees, v.coneKnown, v.chromaticityKnown, v.chromaticity, v.intensity, v.range, v.unit,
	  v.style, v.twoSided, v.maskSampled, v.staticEligible, v.geometricCoverage);
}
void Fields(Codec &a, EvidenceReport &v)
{
	a(v.triangles, v.groups, v.maskBlobs, v.unsupported, v.animated, v.moving, v.authored,
	  v.entities, v.suns, v.skies, v.bytes);
}
void Fields(Codec &a, Entry &v)
{
	a(v.kind, v.flags, v.id, v.first, v.count, v.offset, v.bytes, v.sha256);
}
void Fields(Codec &a, SurfaceLightingObservation &v)
{
	a(v.x, v.y, v.logicalLightmap, v.surface, v.styleSlot, v.materialIndex);
	a.Size(v.image);
	a(v.centreUv, v.footprintMin, v.footprintMax, v.coverage, v.issues, v.positionKnown,
	  v.shadingNormalKnown, v.basisValidated, v.normalOrientationKnown, v.confidenceKnown,
	  v.positionWorld, v.geometricNormal, v.shadingNormal, v.tangentWorld, v.coveredFraction,
	  v.areaWorldSquared, v.areaErrorEstimate, v.basisErrorWorld, v.mappingErrorBoundWorld);
}
void Fields(Codec &a, RegionOptions &v)
{
	a(v.bounds, v.coarseCellWorld, v.minimumCellWorld, v.constrictionWorld);
	a.Size(v.maxNodes);
	a.Size(v.maxCells);
	a.Size(v.maxLinks);
	a.Size(v.maxDynamicBounds);
	a(v.maxBytes, v.maxWork, v.maxRayWork, v.inspectSky, v.skySamplesPerRegion);
}
void Fields(Codec &a, EmitterCandidate &v)
{
	a(v.id, v.shapes, v.issues, v.staticEligible);
	a.Size(v.primary);
	a.Sizes(v.evidence);
	a(v.sourceIds);
}

struct ImageInfo
{
	uint32_t logical = 0, width = 0, height = 0, channels = 0, pair = UINT32_MAX;
	LightmapRole role = LightmapRole::Lighting;
	LightmapEncoding encoding = LightmapEncoding::BspRgb8;
	std::string source;
	uint64_t sourceOffset = 0, elements = 0;
	bool missing = false;
};
void Fields(Codec &a, ImageInfo &v)
{
	a(v.logical, v.width, v.height, v.channels, v.pair, v.role, v.encoding, v.source,
	  v.sourceOffset, v.elements, v.missing);
}
struct ImageTable
{
	std::vector<ImageInfo> images;
};
void Fields(Codec &a, ImageTable &v)
{
	a(v.images);
}
struct GridInfo
{
	GridLayout layout;
	GridHdrState hdr = GridHdrState::NotRequested;
	std::string source;
	uint64_t recordsOffset = 0, indicesOffset = 0, recordBytes = 0, indexBytes = 0, hdrValues = 0;
};
void Fields(Codec &a, GridInfo &v)
{
	a(v.layout, v.hdr, v.source, v.recordsOffset, v.indicesOffset, v.recordBytes, v.indexBytes,
	  v.hdrValues);
}
struct SurfaceInfo
{
	VertexHdrState hdr = VertexHdrState::NotRequested;
	std::string source;
	Disk::Digest hash{};
	uint64_t values = 0;
};
void Fields(Codec &a, SurfaceInfo &v)
{
	a(v.hdr, v.source, v.hash, v.values);
}
struct TileInfo
{
	uint64_t image = 0;
	uint32_t x = 0, y = 0, width = 0, height = 0;
	std::vector<Luxel> samples;
};
void Fields(Codec &a, TileInfo &v)
{
	a(v.image, v.x, v.y, v.width, v.height, v.samples);
}
struct EvidenceInfo
{
	std::vector<EmitterEvidence> evidence;
	std::vector<EmitterCandidate> candidates;
	EvidenceReport report;
};
void Fields(Codec &a, EvidenceInfo &v)
{
	a(v.evidence, v.candidates, v.report);
}
struct Document
{
	Metadata metadata;
	Summary summary;
	bool images = false, grid = false, geometry = false, evidence = false, domain = false,
		 regions = false;
	uint32_t surfaceVersion = kSurfaceObservationVersion, evidenceVersion = kEmitterEvidenceVersion,
			 regionVersion = kLightingRegionGraphVersion;
};
void Fields(Codec &a, Document &v)
{
	a(v.metadata, v.summary, v.images, v.grid, v.geometry, v.evidence, v.domain, v.regions,
	  v.surfaceVersion, v.evidenceVersion, v.regionVersion);
}
enum Kind : uint32_t
{
	Meta = 1,
	Images = 2,
	ImagePixels = 3,
	Grid = 4,
	GridRecords = 5,
	GridIndices = 6,
	GridHdr = 7,
	Surface = 8,
	SurfaceHdrInfo = 9,
	SurfaceHdr = 10,
	Space = 11,
	Dynamic = 12,
	Regions = 13,
	Evidence = 14,
	Tile = 15
};
template <class T> std::vector<uint8_t> Pack(const T &v, const Options &o)
{
	Codec a(o);
	a.One(const_cast<T &>(v));
	return std::move(a.bytes);
}
template <class T>
T Unpack(std::vector<uint8_t> b, const Options &o,
		 uint32_t regionVersion = kLightingRegionGraphVersion)
{
	Codec a(std::move(b), o);
	a.regionVersion = regionVersion;
	T v{};
	a.One(v);
	a.Done();
	return v;
}
std::vector<uint8_t> Read(const Disk::Reader &r, const Entry &e, const Options &o)
{
	Need(e.bytes <= o.maxChunkBytes && e.bytes <= o.maxResidentBytes, "Chunk read limit",
		 Code::MemoryLimit);
	std::vector<uint8_t> b(size_t(e.bytes));
	Check(r.Read(e.offset, b.data(), b.size()));
	Need(Disk::Hash(b.data(), b.size()) == e.sha256, "Section integrity checksum mismatch");
	return b;
}
void Identity(const LightmapMapIdentity &a, const LightmapMapIdentity &b)
{
	Need(a.path == b.path && a.bytes == b.bytes && a.sha256 == b.sha256,
		 "Source identity mismatch");
}
void MetaValid(const Metadata &m)
{
	Need(!m.map.path.empty() && m.map.bytes && m.map.sha256 != Disk::Digest{},
		 "Source map identity required");
	Need(!m.producer.empty() && !m.producerVersion.empty(), "Producer metadata required");
	Need(m.hints == HintPolicy::ObservedPriors || m.hints == HintPolicy::BlindBenchmark,
		 "Unknown hint policy");
	Need(!m.synthetic || m.hints == HintPolicy::BlindBenchmark,
		 "Synthetic benchmark must withhold authored source answers");
	Need(!m.compilerKnown ||
			 (!m.compiler.empty() && !m.compilerVersion.empty() && !m.recipe.empty() &&
			  m.recipeSha256 == Disk::Hex(Disk::Hash(m.recipe.data(), m.recipe.size()))),
		 "Incomplete asserted compiler/bake provenance");
	Need(std::isfinite(m.units.worldUnitsPerMeter) && m.units.worldUnitsPerMeter > 0 &&
			 std::isfinite(m.units.physicalToScene) && m.units.physicalToScene > 0,
		 "Invalid unit convention");
	Need(std::isfinite(m.spatialBlockWorld) && m.spatialBlockWorld > 0, "Invalid split block size");
	Need(std::isfinite(m.visibilityPatchToleranceWorld) && m.visibilityPatchToleranceWorld > 0,
		 "Invalid static patch approximation tolerance");
	Check(Decode::ValidateProfile(m.decoding));
}
void Blind(BakedSurface &s)
{
	auto &f = s.materialFacts;
	bool hint =
		f.legacySurfaceLight > 0 || f.emissionProvenance == EmissionProvenance::AuthoredLuminance;
	f.legacySurfaceLight = 0;
	f.legacySourceColour = {};
	f.authoredLuminance = 0;
	if (hint)
		f.emissionProvenance = EmissionProvenance::LegacyShader;
}

EvidenceInfo FilterEvidence(const EmitterEvidenceSet &s, HintPolicy policy)
{
	EvidenceInfo v;
	std::map<size_t, size_t> remap;
	for (size_t i = 0; i < s.Evidence().size(); ++i)
	{
		auto e = s.Evidence()[i];
		if (policy == HintPolicy::BlindBenchmark)
		{
			if (e.origin != EvidenceOrigin::GlowSurface && e.origin != EvidenceOrigin::SkyShader)
				continue;
			e.raw.clear();
			e.entities.clear();
			e.intensityKnown = false;
			e.intensity = 0;
			e.unit = EvidenceUnit::Unknown;
			e.issues &= ~AuthoredPrior;
			e.issues |= UncalibratedEmission;
			if (e.origin == EvidenceOrigin::SkyShader)
			{
				e.positionKnown = e.orientationKnown = false;
				e.toSource = e.outward = {};
			}
		}
		remap[i] = v.evidence.size();
		v.evidence.push_back(std::move(e));
	}
	for (const auto &c : s.Candidates())
	{
		EmitterCandidate t;
		t.shapes = c.shapes;
		t.issues = c.issues;
		t.staticEligible = c.staticEligible;
		for (size_t old : c.evidence)
		{
			auto found = remap.find(old);
			if (found != remap.end())
			{
				t.evidence.push_back(found->second);
				t.sourceIds.push_back(v.evidence[found->second].id);
			}
		}
		if (t.evidence.empty())
			continue;
		t.primary = t.evidence[0];
		t.id = v.evidence[t.primary].id;
		if (policy == HintPolicy::ObservedPriors)
		{
			auto p = remap.find(c.primary);
			Need(p != remap.end(), "Candidate primary missing");
			t.primary = p->second;
			t.id = c.id;
		}
		else
		{
			t.shapes = 0;
			for (size_t n : t.evidence)
				t.shapes |= v.evidence[n].shapes;
			t.issues &= ~AuthoredPrior;
		}
		v.candidates.push_back(std::move(t));
	}
	v.report = s.Report();
	v.report.authored = v.report.entities = v.report.suns = 0;
	for (const auto &e : v.evidence)
	{
		v.report.authored += e.origin == EvidenceOrigin::AuthoredArea;
		v.report.entities += e.origin == EvidenceOrigin::BspEntity;
		v.report.suns += e.origin == EvidenceOrigin::SunShader;
	}
	return v;
}
RegionSnapshot SnapshotGraph(const LightingRegionGraph &g)
{
	RegionSnapshot r;
	r.certificationKey = g.CertificationKey();
	r.map = g.Identity();
	r.options = g.Options();
	r.report = g.Report();
	r.cells = g.Cells();
	r.regions = g.Regions();
	r.connections = g.Connections();
	r.cellConnections = g.CellConnections();
	r.members = g.Members();
	r.neighbours = g.Neighbours();
	r.bspLinks = g.BspLinks();
	r.boundaries = g.Boundaries();
	return r;
}
struct Sink
{
	Writer &writer;
	const Options &o;
	uint64_t position = kHeaderBytes;
	std::vector<Entry> directory;
	Sink(Writer &w, const Options &l) : writer(w), o(l) {}
	void Bytes(uint32_t kind, uint64_t id, uint64_t first, uint64_t count, std::vector<uint8_t> b)
	{
		Need(directory.size() < o.maxSections, "Section directory limit", Code::MemoryLimit);
		Need(b.size() <= o.maxChunkBytes, "Section size limit", Code::MemoryLimit);
		uint64_t aligned = (position + 7) & ~uint64_t(7);
		Need(aligned <= o.maxFileBytes && b.size() <= o.maxFileBytes - aligned, "File size limit",
			 Code::MemoryLimit);
		uint8_t zeros[8] = {};
		if (aligned > position)
			Check(writer.Write(position, zeros, size_t(aligned - position)));
		Entry e;
		e.kind = kind;
		e.flags = 1;
		e.id = id;
		e.first = first;
		e.count = count;
		e.offset = aligned;
		e.bytes = b.size();
		e.sha256 = Disk::Hash(b.data(), b.size());
		Check(writer.Write(aligned, b.data(), b.size()));
		position = aligned + b.size();
		directory.push_back(e);
	}
	template <class T> void Object(uint32_t k, uint64_t id, const T &v, uint64_t n = 1)
	{
		Bytes(k, id, 0, n, Pack(v, o));
	}
	template <class T> void Array(uint32_t kind, uint64_t id, const std::vector<T> &v)
	{
		const size_t per = size_t(std::min<uint64_t>(o.maxChunkBytes / sizeof(T), 65536));
		Need(per > 0, "Zero chunk capacity");
		for (size_t first = 0; first < v.size(); first += per)
		{
			Codec a(o);
			size_t end = std::min(v.size(), first + per);
			for (size_t i = first; i < end; ++i)
				a.One(const_cast<T &>(v[i]));
			Bytes(kind, id, first, end - first, std::move(a.bytes));
		}
	}
};
bool Finite(Vec3);
void ValidBounds(const Bounds &b)
{
	Need(!b.known || (Finite(b.minWorld) && Finite(b.maxWorld) && b.minWorld.x <= b.maxWorld.x &&
					  b.minWorld.y <= b.maxWorld.y && b.minWorld.z <= b.maxWorld.z),
		 "Invalid spatial bounds");
}
void ValidateRegion(const RegionSnapshot &r, const Options &o)
{
	Need(r.cells.size() <= o.maxRecords && r.regions.size() <= o.maxRecords, "Region count limit");
	ValidBounds(r.options.bounds);
	Need(std::isfinite(r.options.minimumCellWorld) && r.options.minimumCellWorld > 0 &&
			 std::isfinite(r.options.coarseCellWorld) &&
			 r.options.coarseCellWorld >= r.options.minimumCellWorld &&
			 std::isfinite(r.options.constrictionWorld) && r.options.constrictionWorld >= 0,
		 "Invalid stored region resolution");
	std::set<Id> ids;
	for (const auto &v : r.regions)
	{
		Need(v.id && ids.insert(v.id).second, "Invalid region ID");
		ValidBounds(v.bounds);
		Need(Finite(v.representativeWorld), "Non-finite region representative");
		Need(v.firstMember <= r.members.size() && v.memberCount <= r.members.size() - v.firstMember,
			 "Region membership range");
		Need(v.firstNeighbour <= r.neighbours.size() &&
				 v.neighbourCount <= r.neighbours.size() - v.firstNeighbour,
			 "Region neighbour range");
	}
	for (auto n : r.members)
		Need(n < r.cells.size(), "Region cell reference");
	for (auto n : r.neighbours)
		Need(n < r.connections.size(), "Region edge reference");
	for (const auto &v : r.cells)
	{
		Need(!v.region || ids.count(v.region), "Dangling cell region");
		Need(v.boundaryCount <= 8 && v.units, "Invalid region cell");
		Need(v.id && Finite(v.representativeWorld) && uint32_t(v.space) <= 5 &&
				 uint32_t(v.bsp.space) <= 5,
			 "Invalid region cell coordinates/class");
	}
	for (const auto &e : r.connections)
	{
		Need(e.a != e.b && ids.count(e.a) && ids.count(e.b), "Invalid region connection");
		ValidBounds(e.sampledOpeningBounds);
		Need(Finite(e.sampledOpeningCentreWorld) && std::isfinite(e.sampledAreaWorldSquared) &&
				 e.sampledAreaWorldSquared >= 0 && uint32_t(e.transport) <= 3,
			 "Invalid region aperture");
	}
	for (const auto &e : r.cellConnections)
	{
		Need(e.a < r.cells.size() && e.b < r.cells.size(), "Invalid cell connection");
		ValidBounds(e.openingBounds);
		Need(e.axis < 3 && Finite(e.openingCentreWorld) && std::isfinite(e.faceAreaWorldSquared) &&
				 e.faceAreaWorldSquared >= 0 && uint32_t(e.direct) <= 2 &&
				 uint32_t(e.transport) <= 3,
			 "Invalid cell aperture");
	}
	for (const auto &v : r.bspLinks)
		Need(ids.count(v.region), "Invalid region BSP link");
	for (const auto &v : r.boundaries)
		Need(ids.count(v.region), "Invalid region boundary link");
}
} // namespace
// Private snapshot restoration: all loaded facts are checked before any return.
struct Access
{
	static std::shared_ptr<const LightmapObservations> Images(LightmapMapIdentity map,
															  std::vector<LightmapImage> images)
	{
		std::shared_ptr<LightmapObservations> p(new LightmapObservations);
		p->map_ = std::move(map);
		p->images_ = std::move(images);
		return p;
	}
	static std::shared_ptr<const EmitterEvidenceSet> Evidence(LightmapMapIdentity map,
															  EvidenceInfo info)
	{
		std::shared_ptr<EmitterEvidenceSet> p(new EmitterEvidenceSet);
		p->map_ = std::move(map);
		p->evidence_ = std::move(info.evidence);
		p->candidates_ = std::move(info.candidates);
		p->report_ = info.report;
		return p;
	}
};
Disk::Digest SampleIdentity(const LightmapMapIdentity &map, const LightmapImage &image,
						   const SurfaceLightingObservation &sample, uint8_t style)
{
	Codec a{Options()};
	auto hash = map.sha256;
	auto pixels = image.pixelHash;
	auto source = image.source;
	auto encoding = image.encoding;
	uint32_t version = 1, page = image.logicalIndex, x = sample.x, y = sample.y,
			 surface = sample.surface, slot = sample.styleSlot, width = image.width, height = image.height;
	a(version, hash, source, pixels, encoding, width, height, page, x, y, surface, slot, style);
	return Disk::Hash(a.bytes.data(), a.bytes.size());
}
Partition Split(const Metadata &m, Vec3 p)
{
	Need(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z),
		 "Non-finite partition position");
	uint64_t h = m.splitSeed ^ 0x9e3779b97f4a7c15ull;
	for (float x : {p.x, p.y, p.z})
	{
		double c = std::floor(double(x) / m.spatialBlockWorld);
		Need(c >= -9e15 && c <= 9e15, "Partition coordinate overflow");
		int64_t cell = int64_t(c);
		uint64_t v;
		std::memcpy(&v, &cell, 8);
		v += 0x9e3779b97f4a7c15ull;
		v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ull;
		v = (v ^ (v >> 27)) * 0x94d049bb133111ebull;
		h ^= v ^ (v >> 31);
		h = (h << 21) | (h >> 43);
	}
	unsigned n = unsigned(h % 10);
	return n < 6 ? Partition::Train : n < 8 ? Partition::Validation : Partition::HeldOut;
}
Error Export(const Sources &s, const Options &o, const ReadContext &ctx, Writer &writer,
			 Summary &out)
{
	try
	{
		Need(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4 && sizeof(double) == 8,
			 "IEEE binary32/64 required");
		MetaValid(s.metadata);
		Need(!s.regionSnapshot || s.regions,
			 "Re-export derived region assignments requires explicit ReplayGeometry");
		Need(!s.regions || s.regions->MatchesSources(s.geometry, s.domain, s.dynamic),
			 "Region certification/source leases changed; explicit ReplayGeometry required");
		Need(o.tileSide > 0 && o.tileSide <= 128 && o.maxChunkBytes >= 4096 &&
				 o.maxChunkBytes <= o.maxResidentBytes,
			 "Invalid export limits");
		if (ctx.cancellation && ctx.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Dataset export cancelled");
		Document d;
		d.metadata = s.metadata;
		d.images = bool(s.images);
		d.grid = bool(s.grid);
		d.geometry = bool(s.geometry);
		d.domain = bool(s.domain);
		d.evidence = bool(s.evidence);
		d.regions = bool(s.regions) || bool(s.regionSnapshot);
		Sink sink{writer, o};
		uint8_t header[kHeaderBytes] = {};
		Check(writer.Write(0, header, sizeof(header)));
		ImageTable table;
		uint64_t totalLighting = 0;
		if (s.images)
		{
			Identity(s.metadata.map, s.images->Map());
			for (const auto &im : s.images->Images())
			{
				ImageInfo t;
				t.logical = im.logicalIndex;
				t.width = im.width;
				t.height = im.height;
				t.channels = im.channels;
				t.role = im.role;
				t.encoding = im.encoding;
				t.source = im.source;
				t.sourceOffset = im.sourceOffset;
				t.pair = im.pairedLightingIndex;
				t.missing = im.missing;
				t.elements = im.bytes ? im.bytes->size() : im.floats ? im.floats->size() : 0;
				table.images.push_back(t);
				d.summary.images++;
				d.summary.missingImages += im.missing;
				d.summary.deluxeImages += im.role == LightmapRole::DeluxeDirection;
				d.summary.imagePixels += uint64_t(im.width) * im.height;
				d.summary.rawImageBytes +=
					im.bytes ? im.bytes->size() : im.floats ? im.floats->size() * 4 : 0;
				totalLighting += !im.missing && im.role == LightmapRole::Lighting;
			}
			sink.Object(Images, 0, table);
			for (size_t i = 0; i < s.images->Images().size(); ++i)
			{
				const auto &im = s.images->Images()[i];
				if (im.bytes)
					sink.Array(ImagePixels, i, *im.bytes);
				if (im.floats)
					sink.Array(ImagePixels, i, *im.floats);
			}
		}
		if (s.grid)
		{
			Identity(s.metadata.map, s.grid->Map());
			GridInfo g;
			g.layout = s.grid->Layout();
			g.hdr = s.grid->HdrState();
			g.source = s.grid->HdrSource();
			g.recordsOffset = s.grid->RecordOffset();
			g.indicesOffset = s.grid->IndexOffset();
			g.recordBytes = s.grid->Records().size();
			g.indexBytes = s.grid->Indices().size();
			g.hdrValues = s.grid->Hdr().size();
			sink.Object(Grid, 0, g);
			sink.Array(GridRecords, 0, s.grid->Records());
			sink.Array(GridIndices, 0, s.grid->Indices());
			sink.Array(GridHdr, 0, s.grid->Hdr());
			d.summary.gridCells = s.grid->Report().cells;
			d.summary.missingGridCells = s.grid->Report().missingCells;
			d.summary.invalidGrid = s.grid->Report().invalidIndices;
		}
		if (s.geometry)
		{
			Identity(s.metadata.map, s.geometry->Map());
			for (const auto &source : s.geometry->Surfaces())
			{
				auto v = source;
				if (s.metadata.hints == HintPolicy::BlindBenchmark)
					Blind(v);
				sink.Object(Surface, v.ordinal, v);
				d.summary.surfaces++;
				d.summary.vertices += v.vertices.size();
				d.summary.triangles += v.indices.size() / 3;
			}
			SurfaceInfo v;
			v.hdr = s.geometry->HdrState();
			v.source = s.geometry->HdrSource();
			v.hash = s.geometry->HdrHash();
			v.values = s.geometry->VertexHdr().size();
			sink.Object(SurfaceHdrInfo, 0, v);
			sink.Array(SurfaceHdr, 0, s.geometry->VertexHdr());
		}
		if (s.domain)
		{
			Identity(s.metadata.map, s.domain->Source().identity);
			sink.Object(Space, 0, s.domain->Source());
			sink.Object(Dynamic, 0, s.dynamic, s.dynamic.size());
		}
		if (s.evidence)
		{
			Identity(s.metadata.map, s.evidence->Map());
			auto e = FilterEvidence(*s.evidence, s.metadata.hints);
			d.summary.evidence = e.evidence.size();
			d.summary.candidates = e.candidates.size();
			sink.Object(Evidence, 0, e);
		}
		if (s.regions || s.regionSnapshot)
		{
			Need(!s.regions || s.regions->PortableCertification(),
				 "Custom region domain has no portable certification");
			auto g = s.regions ? SnapshotGraph(*s.regions) : *s.regionSnapshot;
			Identity(s.metadata.map, g.map);
			ValidateRegion(g, o);
			d.summary.regions = g.regions.size();
			d.summary.regionCells = g.cells.size();
			sink.Object(Regions, 0, g, g.cells.size());
		}
		uint64_t selected = 0, eligible = 0, tileOrdinal = 0;
		SurfaceMappingIndex mappingIndex;
		if (s.geometry) Check(SurfaceMappingIndex::Build(s.geometry, ctx, mappingIndex));
		Decode::Decoder decoder(s.metadata.decoding);
		Check(decoder.Validation());
		if (s.images && s.geometry)
			for (size_t i = 0; i < s.images->Images().size(); ++i)
			{
				const auto &im = s.images->Images()[i];
				if (im.missing || im.role != LightmapRole::Lighting)
					continue;
				if (eligible++ < o.firstMappedImage) continue;
				if (o.mappedImages && selected >= o.mappedImages)
					continue;
				++selected;
				Need(uint64_t(im.width) * im.height <= o.maxMappedTexels - d.summary.mappedSamples,
					 "Mapped-texel export limit (use explicit page-range shards)", Code::MemoryLimit);
				for (uint32_t y = 0; y < im.height; y += o.tileSide)
					for (uint32_t x = 0; x < im.width; x += o.tileSide)
					{
						if (ctx.cancellation && ctx.cancellation->IsCancelled())
							throw Failure(Code::Cancelled, "Dataset export cancelled");
						LuxelTile tile;
						MappingOptions mapping;
						mapping.maxTexels = uint64_t(o.tileSide) * o.tileSide;
						mapping.maxScratchBytes = std::min(o.maxResidentBytes, ctx.maxBytes);
						Check(MapLuxels(s.geometry, s.images, i, x, y,
										std::min(o.tileSide, im.width - x),
										std::min(o.tileSide, im.height - y), mapping, ctx, tile, &mappingIndex));
						TileInfo data;
						data.image = i;
						data.x = x;
						data.y = y;
						data.width = tile.width;
						data.height = tile.height;
						for (size_t n = 0; n < tile.samples.size(); ++n)
						{
							Luxel l;
							l.surface = tile.samples[n];
							Check(ClassifyLuxel(tile, n, l.classification));
							Decode::LightmapSample colour;
							auto e = Decode::Lightmap(
								decoder, s.images, i,
								uint64_t(l.surface.y) * im.width + l.surface.x, ctx, colour);
							if (!e && e.code != Code::InvalidInput && e.code != Code::Unavailable)
								throw e;
							l.colour = std::move(colour.value);
							DeluxeSample deluxe;
							e = ReadLuxelDeluxe(tile, n, ctx, deluxe);
							if (e)
								l.deluxe = deluxe.direction;
							l.partition = l.surface.positionKnown
											  ? Split(s.metadata, l.surface.positionWorld)
											  : Partition::Train;
							if (s.regions && l.surface.positionKnown &&
								l.surface.normalOrientationKnown)
							{
								SurfaceRegionSides sides;
								Check(s.regions->AssignSurface(tile, n, ctx, sides));
								l.positiveRegion = sides.positive.region;
								l.negativeRegion = sides.negative.region;
							}
							++d.summary.mappedSamples;
							d.summary.coveredSamples += l.surface.coverage == LuxelCoverage::Mapped;
							d.summary.paddingSamples +=
								l.surface.coverage == LuxelCoverage::Uncovered;
							d.summary.ambiguousSamples +=
								l.surface.coverage == LuxelCoverage::Ambiguous;
							data.samples.push_back(std::move(l));
						}
						sink.Object(Tile, tileOrdinal++, data, data.samples.size());
						if (o.progress) o.progress->Tile(im.logicalIndex, x, y, d.summary.mappedSamples);
					}
			}
		d.summary.derivedComplete = !o.firstMappedImage && selected == totalLighting && bool(s.geometry);
		uint64_t metaOffset =
			sink.position; // metadata includes final file size, whose encoded width is fixed
		d.summary.fileBytes = 0;
		auto meta = Pack(d, o);
		uint64_t aligned = (metaOffset + 7) & ~uint64_t(7), end = aligned + meta.size();
		uint64_t dirOffset = (end + 7) & ~uint64_t(7);
		d.summary.fileBytes = dirOffset + (sink.directory.size() + 1) * kEntryBytes;
		Need(d.summary.fileBytes <= o.maxFileBytes, "Final file size limit", Code::MemoryLimit);
		sink.Object(Meta, 0, d);
		if (dirOffset > sink.position)
		{
			uint8_t zero[8] = {};
			Check(writer.Write(sink.position, zero, size_t(dirOffset - sink.position)));
		}
		Codec directory(o);
		directory.limits.maxChunkBytes = std::min(o.maxResidentBytes, o.maxFileBytes);
		for (auto &e : sink.directory)
			directory.One(e);
		Need(directory.bytes.size() == sink.directory.size() * kEntryBytes,
			 "Directory layout mismatch");
		Check(writer.Write(dirOffset, directory.bytes.data(), directory.bytes.size()));
		Codec h(o);
		uint8_t magic[8] = {'J', 'K', 'O', 'B', 'S', '0', '1', 0};
		h.Raw(magic, 8);
		uint32_t major = kMajor, minor = kMinor, convention = 1, decode = Decode::kVersion,
				 hsize = kHeaderBytes, n = uint32_t(sink.directory.size());
		uint64_t dirBytes = directory.bytes.size(), reserved = 0;
		auto mapHash = s.metadata.map.sha256,
			 dirHash = Disk::Hash(directory.bytes.data(), directory.bytes.size());
		h(major, minor, convention, decode, hsize, n, d.summary.fileBytes, dirOffset, dirBytes,
		  reserved, mapHash, dirHash);
		Need(h.bytes.size() == 128, "Header layout mismatch");
		auto checksum = Disk::Hash(h.bytes.data(), h.bytes.size());
		h(checksum);
		Check(writer.Write(0, h.bytes.data(), h.bytes.size()));
		out = d.summary;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Offline export allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::ProviderError, e.what());
	}
}

Error Dataset::Open(std::shared_ptr<const Disk::Reader> reader, const Options &o,
					std::shared_ptr<const Dataset> &out)
{
	out.reset();
	try
	{
		Need(bool(reader), "Missing dataset reader");
		Need(o.maxChunkBytes >= kHeaderBytes && o.maxChunkBytes <= o.maxResidentBytes,
			 "Invalid import chunk/resident limits");
		Need(reader->Size() >= kHeaderBytes && reader->Size() <= o.maxFileBytes,
			 "Truncated or oversized dataset");
		std::vector<uint8_t> h(kHeaderBytes);
		Check(reader->Read(0, h.data(), h.size()));
		Need(std::memcmp(h.data(), "JKOBS01", 8) == 0, "Unknown dataset magic");
		Need(Disk::Hash(h.data(), 128) ==
				 Unpack<Disk::Digest>(std::vector<uint8_t>(h.begin() + 128, h.end()), o),
			 "Header checksum mismatch");
		Codec c(std::vector<uint8_t>(h.begin() + 8, h.end()), o);
		uint32_t major, minor, convention, decode, header, count;
		uint64_t total, offset, bytes, reserved;
		Disk::Digest map, dir, hash;
		c(major, minor, convention, decode, header, count, total, offset, bytes, reserved, map, dir,
		  hash);
		c.Done();
		Need(major == kMajor && minor <= kMinor && convention == 1 && decode == Decode::kVersion &&
				 header == kHeaderBytes,
			 "Incompatible dataset/convention/decoder version", Code::UnsupportedSchema);
		Need(!reserved && total == reader->Size() && count && count <= o.maxSections &&
				 bytes == uint64_t(count) * kEntryBytes && offset >= kHeaderBytes &&
				 !(offset % 8) && offset <= total && bytes == total - offset,
			 "Invalid directory header");
		Need(bytes <= o.maxResidentBytes, "Directory allocation limit", Code::MemoryLimit);
		std::vector<uint8_t> b(size_t(bytes), uint8_t{});
		Check(reader->Read(offset, b.data(), b.size()));
		Need(Disk::Hash(b.data(), b.size()) == dir, "Directory checksum mismatch");
		Codec d(std::move(b), o);
		std::unique_ptr<Dataset> file(new Dataset);
		file->reader_ = reader;
		file->limits_ = o;
		uint64_t end = kHeaderBytes;
		std::set<std::pair<uint32_t, uint64_t>> single;
		std::map<std::pair<uint32_t, uint64_t>, uint64_t> chunks;
		bool meta = false;
		Document document;
		for (uint32_t i = 0; i < count; ++i)
		{
			Entry e;
			d(e);
			Need(e.flags <= 1 && e.offset >= end && e.offset - end < 8 && !(e.offset % 8) &&
					 e.offset <= offset && e.bytes <= offset - e.offset &&
					 e.bytes <= o.maxChunkBytes,
				 "Overlapping, invalid or oversized section");
			Need(e.count <= o.maxRecords && e.first <= o.maxRecords - e.count,
				 "Section count overflow/limit");
			end = e.offset + e.bytes;
			if (e.kind < Meta || e.kind > Tile)
				Need(!e.flags, "Unknown required section", Code::UnsupportedSchema);
			bool array = e.kind == ImagePixels || e.kind == GridRecords || e.kind == GridIndices ||
						 e.kind == GridHdr || e.kind == SurfaceHdr;
			auto key = std::make_pair(e.kind, e.id);
			if (array)
			{
				Need(chunks[key] == e.first && e.count, "Noncontiguous raw chunks");
				chunks[key] += e.count;
			}
			else
				Need(single.insert(key).second, "Duplicate section identity");
			auto payload = Read(*reader, e, o);
			if (e.kind == Meta)
			{
				Need(!meta, "Duplicate metadata");
				document = Unpack<Document>(std::move(payload), o);
				meta = true;
			}
			file->entries_.push_back(e);
		}
		d.Done();
		Need(meta && end <= offset && offset - end < 8,
			 "Missing metadata or invalid directory gap");
		MetaValid(document.metadata);
		Need(document.metadata.map.sha256 == map, "Header/source-map digest mismatch");
		Need(document.summary.fileBytes == total, "Metadata file size mismatch");
		Need((document.surfaceVersion == 4 ||
			  document.surfaceVersion == kSurfaceObservationVersion) &&
				 document.evidenceVersion == kEmitterEvidenceVersion &&
				 (document.regionVersion == 1 ||
				  document.regionVersion == kLightingRegionGraphVersion),
			 "Incompatible observation sub-schema", Code::UnsupportedSchema);
		file->regionVersion_ = document.regionVersion;
		file->surfaceVersion_ = document.surfaceVersion;
		file->metadata_ = std::move(document.metadata);
		file->summary_ = document.summary;
		out = std::shared_ptr<const Dataset>(file.release());
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Dataset open allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::ProviderError, e.what());
	}
}
namespace
{
const Entry *Find(const std::vector<Entry> &entries, uint32_t kind, uint64_t id = 0)
{
	for (const auto &e : entries)
		if (e.kind == kind && e.id == id)
			return &e;
	return nullptr;
}
template <class T>
T Object(const Disk::Reader &r, const std::vector<Entry> &entries, uint32_t kind, const Options &o,
		 uint64_t id = 0)
{
	auto e = Find(entries, kind, id);
	Need(e != nullptr, "Missing required source section");
	return Unpack<T>(Read(r, *e, o), o);
}
template <class T>
std::vector<T> Array(const Disk::Reader &r, const std::vector<Entry> &entries, uint32_t kind,
					 uint64_t id, uint64_t expected, const Options &o)
{
	Need(expected <= o.maxRecords && expected <= o.maxResidentBytes / sizeof(T),
		 "Raw array allocation limit", Code::MemoryLimit);
	std::vector<T> v;
	v.reserve(size_t(expected));
	uint64_t count = 0;
	for (const auto &e : entries)
		if (e.kind == kind && e.id == id)
		{
			Need(e.first == count && e.count <= expected - count && e.bytes == e.count * sizeof(T),
				 "Raw array extent mismatch");
			Codec a(Read(r, e, o), o);
			for (uint64_t i = 0; i < e.count; ++i)
			{
				T x{};
				a(x);
				v.push_back(x);
			}
			a.Done();
			count += e.count;
		}
	Need(count == expected, "Missing/truncated raw array");
	return v;
}
bool Finite(Vec3 p)
{
	return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
void EvidenceValid(const EvidenceInfo &e, const Metadata &m, uint64_t surfaces)
{
	std::set<Id> ids, candidates;
	for (const auto &v : e.evidence)
	{
		Need(v.id && ids.insert(v.id).second, "Duplicate evidence source ID");
		Need(uint32_t(v.origin) <= uint32_t(EvidenceOrigin::SkyShader) &&
				 uint32_t(v.unit) <= uint32_t(EvidenceUnit::IlluminanceLux),
			 "Invalid evidence kind/unit");
		for (auto n : v.surfaces)
			Need(n < surfaces, "Invalid candidate surface reference");
		for (auto p : {v.position, v.right, v.up, v.outward, v.toSource, v.colour, v.chromaticity})
			Need(Finite(p), "Non-finite candidate geometry/colour");
		Need(std::isfinite(v.intensity) && v.intensity >= 0, "Invalid candidate intensity");
		for (float value : {v.halfWidth, v.halfHeight, v.radius, v.areaWorld, v.sourceRadiusHint,
							v.coneDegrees, v.range, v.geometricCoverage})
			Need(std::isfinite(value) && value >= 0, "Invalid candidate dimensions");
		if (m.hints == HintPolicy::BlindBenchmark)
			Need((v.origin == EvidenceOrigin::GlowSurface ||
				  v.origin == EvidenceOrigin::SkyShader) &&
					 v.raw.empty() && v.entities.empty() && !v.intensityKnown && v.intensity == 0 &&
					 v.unit == EvidenceUnit::Unknown,
				 "Ground-truth/source-answer leak in blind dataset");
	}
	for (const auto &v : e.candidates)
	{
		Need(v.id && candidates.insert(v.id).second && v.primary < e.evidence.size() &&
				 !v.evidence.empty(),
			 "Invalid candidate identity/primary");
		for (size_t n : v.evidence)
			Need(n < e.evidence.size(), "Invalid candidate evidence reference");
		for (Id id : v.sourceIds)
			Need(ids.count(id), "Invalid candidate source alias");
		Need(std::find(v.evidence.begin(), v.evidence.end(), v.primary) != v.evidence.end(),
			 "Unreferenced candidate primary");
	}
}
} // namespace
Error Dataset::Load(Sources &out, RegionSnapshot &regions) const
{
	out = Sources();
	regions = RegionSnapshot();
	try
	{
		auto doc = Object<Document>(*reader_, entries_, Meta, limits_);
		Sources s;
		s.metadata = metadata_;
		uint64_t resident = 0;
		for (const auto &e : entries_)
			if (e.kind != Tile)
			{
				Need(e.bytes <= (limits_.maxResidentBytes - resident) / 4,
					 "Source snapshot resident memory limit", Code::MemoryLimit);
				resident += e.bytes * 4;
			}
		if (doc.images)
		{
			auto table = Object<ImageTable>(*reader_, entries_, Images, limits_);
			Need(table.images.size() == summary_.images, "Image count mismatch");
			std::vector<LightmapImage> images;
			for (size_t i = 0; i < table.images.size(); ++i)
			{
				const auto &v = table.images[i];
				Need(uint32_t(v.role) <= 1 && uint32_t(v.encoding) <= 2,
					 "Unsupported image encoding/role");
				LightmapImage im;
				im.logicalIndex = v.logical;
				im.width = v.width;
				im.height = v.height;
				im.channels = v.channels;
				im.role = v.role;
				im.encoding = v.encoding;
				im.source = v.source;
				im.sourceOffset = v.sourceOffset;
				im.pairedLightingIndex = v.pair;
				im.missing = v.missing;
				if (im.missing)
					Need(!im.width && !im.height && !v.elements,
						 "Missing image has invented pixels");
				else
				{
					Need(im.width && im.height && im.width <= 16384 && im.height <= 16384 &&
							 im.channels ==
								 (im.encoding == LightmapEncoding::ExternalRgba8 ? 4u : 3u) &&
							 v.elements == uint64_t(im.width) * im.height * im.channels,
						 "Image dimensions/channel count mismatch");
					if (im.encoding == LightmapEncoding::ExternalRgb32F)
					{
						auto pixels =
							Array<float>(*reader_, entries_, ImagePixels, i, v.elements, limits_);
						for (float f : pixels)
							Need(std::isfinite(f) && f >= 0, "Invalid HDR lightmap values");
						im.pixelHash = Disk::Hash(pixels.data(), pixels.size() * 4);
						im.floats = std::make_shared<const std::vector<float>>(std::move(pixels));
					}
					else
					{
						auto pixels =
							Array<uint8_t>(*reader_, entries_, ImagePixels, i, v.elements, limits_);
						im.pixelHash = Disk::Hash(pixels.data(), pixels.size());
						im.bytes = std::make_shared<const std::vector<uint8_t>>(std::move(pixels));
					}
				}
				images.push_back(std::move(im));
			}
			s.images = Access::Images(metadata_.map, std::move(images));
		}
		if (doc.grid)
		{
			auto g = Object<GridInfo>(*reader_, entries_, Grid, limits_);
			auto rb = Array<uint8_t>(*reader_, entries_, GridRecords, 0, g.recordBytes, limits_),
				 ib = Array<uint8_t>(*reader_, entries_, GridIndices, 0, g.indexBytes, limits_);
			auto hdr = Array<float>(*reader_, entries_, GridHdr, 0, g.hdrValues, limits_);
			GridLimits l;
			l.maxBytes = limits_.maxResidentBytes;
			l.maxCells = limits_.maxRecords;
			LightgridCapture capture(l);
			capture.Begin(BuildPolicy::ForceRebuild);
			Check(capture.SetSource(metadata_.map, rb.data(), rb.size(), g.recordsOffset, ib.data(),
									ib.size(), g.indicesOffset));
			Check(capture.SetLayout(g.layout));
			Check(capture.SetHdr(g.hdr, g.source, hdr.data(), hdr.size() * 4));
			Check(capture.Freeze());
			s.grid = capture.Acquire();
			Need(s.grid->Report().cells == summary_.gridCells, "Grid cell count mismatch");
		}
		if (doc.geometry)
		{
			SurfaceLimits l;
			l.maxBytes = limits_.maxResidentBytes;
			l.maxVertices = limits_.maxRecords;
			l.maxIndices = limits_.maxRecords;
			SurfaceCapture capture(l);
			capture.Begin(BuildPolicy::ForceRebuild);
			Check(capture.SetMap(metadata_.map));
			uint64_t count = 0;
			for (const auto &e : entries_)
				if (e.kind == Surface)
				{
					auto v = Unpack<BakedSurface>(Read(*reader_, e, limits_), limits_);
					Need(v.ordinal == count && e.id == count, "Surface ID/order mismatch");
					Need(uint32_t(v.kind) <=
								 (surfaceVersion_ >= 5 ? uint32_t(BakedSurfaceKind::Flare) : 3) &&
							 uint32_t(v.motion) <= 3 && uint32_t(v.materialFacts.cull) <= 3 &&
							 uint32_t(v.materialFacts.emissionProvenance) <= 3,
						 "Unsupported surface/material enum");
					for (const auto &p : v.vertices)
						Need(Finite(p.position) && Finite(p.shadingNormal),
							 "Non-finite source geometry");
					if (metadata_.hints == HintPolicy::BlindBenchmark)
						Need(v.materialFacts.authoredLuminance == 0 &&
								 v.materialFacts.legacySurfaceLight == 0,
							 "Authored intensity leaked through material facts");
					auto before = capture.Report().malformed;
					Check(capture.Add(std::move(v)));
					Need(capture.Report().malformed == before, "Malformed source surface geometry");
					++count;
				}
			Need(count == summary_.surfaces && capture.Report().vertices == summary_.vertices &&
					 capture.Report().indices / 3 == summary_.triangles,
				 "Surface/vertex/triangle count mismatch");
			auto v = Object<SurfaceInfo>(*reader_, entries_, SurfaceHdrInfo, limits_);
			auto hdr = Array<float>(*reader_, entries_, SurfaceHdr, 0, v.values, limits_);
			Check(capture.VertexHdr(v.hdr, v.source, hdr.data(), hdr.size() * 4, hdr.size() / 3));
			Check(capture.Freeze());
			s.geometry = capture.Acquire();
		}
		if (doc.domain)
		{
			auto space = Object<BspSpaceInput>(*reader_, entries_, Space, limits_);
			Identity(metadata_.map, space.identity);
			ReadContext ctx;
			ctx.maxBytes = limits_.maxResidentBytes;
			Check(BspRegionDomain::Create(std::move(space), ctx, s.domain));
			s.dynamic = Object<std::vector<DynamicBoundary>>(*reader_, entries_, Dynamic, limits_);
			std::set<Id> dynamicIds;
			for (const auto &v : s.dynamic)
			{
				Need(v.id && dynamicIds.insert(v.id).second, "Duplicate dynamic boundary ID");
				ValidBounds(v.bounds);
			}
		}
		if (doc.regions)
		{
			auto entry = Find(entries_, Regions, 0);
			Need(entry != nullptr, "Missing region snapshot");
			auto g =
				Unpack<RegionSnapshot>(Read(*reader_, *entry, limits_), limits_, regionVersion_);
			Identity(metadata_.map, g.map);
			ValidateRegion(g, limits_);
			Need(g.regions.size() == summary_.regions && g.cells.size() == summary_.regionCells,
				 "Region count mismatch");
			regions = std::move(g);
			s.regionSnapshot = std::make_shared<const RegionSnapshot>(regions);
		}
		if (doc.evidence)
		{
			auto e = Object<EvidenceInfo>(*reader_, entries_, Evidence, limits_);
			EvidenceValid(e, metadata_, summary_.surfaces);
			Need(e.evidence.size() == summary_.evidence &&
					 e.candidates.size() == summary_.candidates,
				 "Evidence count mismatch");
			s.evidence = Access::Evidence(metadata_.map, std::move(e));
		}
		out = std::move(s);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Dataset source allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::ProviderError, e.what());
	}
}
Error Dataset::ReadLuxels(size_t index, std::vector<Luxel> &out) const
{
	out.clear();
	try
	{
		Need(index < entries_.size() && entries_[index].kind == Tile, "Not a luxel tile section");
		auto t = Unpack<TileInfo>(Read(*reader_, entries_[index], limits_), limits_);
		Need(t.width && t.height && t.width <= 128 && t.height <= 128 &&
				 uint64_t(t.width) * t.height == t.samples.size() &&
				 t.samples.size() == entries_[index].count,
			 "Invalid luxel tile extent");
		auto profile = std::make_shared<const Decode::Profile>(metadata_.decoding);
		for (size_t n = 0; n < t.samples.size(); ++n)
		{
			auto &v = t.samples[n];
			Need(v.surface.image == t.image && v.surface.x == t.x + n % t.width &&
					 v.surface.y == t.y + n / t.width &&
					 (!v.surface.positionKnown || v.surface.surface < summary_.surfaces) &&
					 v.surface.styleSlot < 4 && uint32_t(v.surface.coverage) <= 4 &&
					 uint32_t(v.partition) <= 2,
				 "Invalid luxel source/cell reference");
			Need(!v.surface.positionKnown ||
					 (Finite(v.surface.positionWorld) && Finite(v.surface.geometricNormal) &&
					  std::isfinite(v.surface.areaWorldSquared) && v.surface.areaWorldSquared >= 0),
				 "Invalid mapped geometry/area");
			Need(v.partition == (v.surface.positionKnown ? Split(metadata_, v.surface.positionWorld)
														 : Partition::Train),
				 "Invalid deterministic sample partition");
			for (const auto *value :
				 {&v.colour.encoded, &v.colour.source, &v.colour.renderer, &v.colour.scene})
			{
				Need(uint32_t(value->state) <= 2 && uint32_t(value->reason) <= 7 &&
						 uint32_t(value->space) <= 4,
					 "Invalid decoded stage enum");
				Need(value->state != Decode::State::Available ||
						 (Finite(value->rgb) && value->rgb.x >= 0 && value->rgb.y >= 0 &&
						  value->rgb.z >= 0),
					 "Non-finite or negative decoded observation");
				for (const auto &bound : value->bounds)
					Need(
						(!bound.lowerKnown || (std::isfinite(bound.lower) && bound.lower >= 0)) &&
							(!bound.upperKnown ||
							 (std::isfinite(bound.upper) && bound.upper >= 0)) &&
							(!(bound.lowerKnown && bound.upperKnown) || bound.lower <= bound.upper),
						"Invalid decoded quantization/censoring bounds");
			}
			v.colour.metadata = profile;
		}
		out = std::move(t.samples);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Luxel tile allocation failed");
	}
}
namespace
{
bool Near(double a, double b, double absolute = 1e-5)
{
	return std::isfinite(a) && std::isfinite(b) &&
		   std::abs(a - b) <= absolute + 1e-5 * std::max(std::abs(a), std::abs(b));
}
bool Near(Vec3 a, Vec3 b, double absolute = 1e-5)
{
	return Near(a.x, b.x, absolute) && Near(a.y, b.y, absolute) && Near(a.z, b.z, absolute);
}
void DecodedEqual(const Decode::Observation &a, const Decode::Observation &b)
{
	Need(a.flags == b.flags && a.absoluteScale == b.absoluteScale &&
			 a.resolvedTransfer == b.resolvedTransfer && a.resolvedPrimaries == b.resolvedPrimaries,
		 "Decoded metadata/flags mismatch");
	Need(a.raw.encoding == b.raw.encoding && a.raw.component == b.raw.component &&
			 a.raw.hdrOrigin == b.raw.hdrOrigin && a.raw.missing == b.raw.missing &&
			 a.raw.directionData == b.raw.directionData,
		 "Raw decoded source mismatch");
	for (unsigned i = 0; i < 4; ++i)
		Need(a.raw.bytes[i] == b.raw.bytes[i], "Raw decoded bytes mismatch");
	if (a.raw.encoding == Decode::Encoding::LinearRgb32F)
		Need(Near(a.raw.hdr, b.raw.hdr), "Raw HDR mismatch");
	for (unsigned i = 0; i < 3; ++i)
		Need(a.channels[i] == b.channels[i], "Decoded channel flags mismatch");
	const Decode::Value *av[] = {&a.encoded, &a.source, &a.renderer, &a.scene};
	const Decode::Value *bv[] = {&b.encoded, &b.source, &b.renderer, &b.scene};
	for (unsigned i = 0; i < 4; ++i)
	{
		const auto &x = *av[i];
		const auto &y = *bv[i];
		Need(x.state == y.state && x.reason == y.reason && x.space == y.space &&
				 x.flags == y.flags && x.censoredChannels == y.censoredChannels &&
				 (x.state != Decode::State::Available || Near(x.rgb, y.rgb)),
			 "Decoded colour/stage mismatch");
		for (unsigned c = 0; c < 3; ++c)
		{
			Need(x.bounds[c].lowerKnown == y.bounds[c].lowerKnown &&
					 x.bounds[c].upperKnown == y.bounds[c].upperKnown,
				 "Decoded censoring/quantization bound presence mismatch");
			if (x.bounds[c].lowerKnown)
				Need(Near(x.bounds[c].lower, y.bounds[c].lower), "Decoded lower bound mismatch");
			if (x.bounds[c].upperKnown)
				Need(Near(x.bounds[c].upper, y.bounds[c].upper), "Decoded upper bound mismatch");
		}
	}
}
} // namespace
Error Dataset::Validate(bool remap, uint64_t &checked) const
{
	checked = 0;
	try
	{
		Sources s;
		RegionSnapshot graph;
		Check(Load(s, graph));
		ReadContext ctx;
		ctx.maxBytes = limits_.maxResidentBytes;
		Decode::Decoder decoder(metadata_.decoding);
		SurfaceMappingIndex mappingIndex;
		if (remap && s.geometry) Check(SurfaceMappingIndex::Build(s.geometry, ctx, mappingIndex));
		uint64_t total = 0;
		std::set<Id> regionIds;
		for (const auto &r : graph.regions)
			regionIds.insert(r.id);
		for (size_t i = 0; i < entries_.size(); ++i)
			if (entries_[i].kind == Tile)
			{
				std::vector<Luxel> samples;
				Check(ReadLuxels(i, samples));
				Need(s.images && s.geometry, "Derived observations without source images/geometry");
				auto tile = Unpack<TileInfo>(Read(*reader_, entries_[i], limits_), limits_);
				Need(tile.image < s.images->Images().size(), "Invalid tile image reference");
				const auto &image = s.images->Images()[size_t(tile.image)];
				Need(image.role == LightmapRole::Lighting && tile.x <= image.width &&
						 tile.width <= image.width - tile.x && tile.y <= image.height &&
						 tile.height <= image.height - tile.y,
					 "Tile outside original colour image");
				LuxelTile mapped;
				if (remap)
				{
					MappingOptions m;
					m.maxTexels = 128 * 128;
					m.maxScratchBytes = limits_.maxResidentBytes;
					Check(MapLuxels(s.geometry, s.images, size_t(tile.image), tile.x, tile.y,
									tile.width, tile.height, m, ctx, mapped, &mappingIndex));
				}
				for (size_t n = 0; n < samples.size(); ++n)
				{
					const auto &v = samples[n];
					Need((!v.positiveRegion || regionIds.count(v.positiveRegion)) &&
							 (!v.negativeRegion || regionIds.count(v.negativeRegion)),
						 "Invalid luxel region reference");
					Decode::LightmapSample colour;
					auto e = Decode::Lightmap(decoder, s.images, size_t(tile.image),
											  uint64_t(v.surface.y) * image.width + v.surface.x,
											  ctx, colour);
					Need(bool(e) || e.code == Code::Unavailable || e.code == Code::InvalidInput,
						 "Unexpected decoder failure");
					DecodedEqual(v.colour, colour.value);
					if (remap)
					{
						const auto &g = mapped.samples[n];
						Need(g.coverage == v.surface.coverage &&
								 g.positionKnown == v.surface.positionKnown &&
								 g.surface == v.surface.surface &&
								 g.logicalLightmap == v.surface.logicalLightmap &&
								 g.styleSlot == v.surface.styleSlot &&
								 g.materialIndex == v.surface.materialIndex,
							 "Luxel correspondence changed");
						if (g.positionKnown)
							Need(Near(g.positionWorld, v.surface.positionWorld, .002) &&
									 Near(g.geometricNormal, v.surface.geometricNormal) &&
									 Near(g.areaWorldSquared, v.surface.areaWorldSquared),
								 "Luxel world geometry/area changed");
						ObservationClassification cls;
						Check(ClassifyLuxel(mapped, n, cls));
						Need(cls.path == v.classification.path &&
								 cls.reasons == v.classification.reasons &&
								 cls.style.id == v.classification.style.id &&
								 cls.materialUncertainty == v.classification.materialUncertainty &&
								 cls.materialSignals == v.classification.materialSignals &&
								 cls.diffuseFitCandidate == v.classification.diffuseFitCandidate,
							 "Material/style classification mismatch");
						DeluxeSample deluxe;
						auto de = ReadLuxelDeluxe(mapped, n, ctx, deluxe);
						if (de)
						{
							Need(deluxe.direction.state == v.deluxe.state &&
									 Near(deluxe.direction.toSource, v.deluxe.toSource),
								 "Deluxe correspondence changed");
							for (unsigned c = 0; c < 4; ++c)
								Need(deluxe.direction.raw[c] == v.deluxe.raw[c],
									 "Deluxe source bytes changed");
						}
						else
							Need(v.deluxe.state == DeluxeState::Missing,
								 "Unpaired deluxe was fabricated");
					}
					++checked;
				}
				total += samples.size();
			}
		Need(total == summary_.mappedSamples, "Derived sample count mismatch");
		if (s.grid)
			for (uint64_t i = 0; i < s.grid->Report().cells; ++i)
			{
				GridCell cell;
				Check(s.grid->Cell(i, cell));
				if (cell.presence == GridPresence::Present)
				{
					Need(cell.sourceRecord < s.grid->Report().records,
						 "Grid indirection inconsistent");
					Need(Finite(cell.centreWorld), "Non-finite grid position");
				}
				++checked;
			}
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Dataset validation allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::ProviderError, e.what());
	}
}
std::string InspectText(const Dataset &d)
{
	std::ostringstream out;
	out.imbue(std::locale::classic());
	const auto &m = d.Info();
	const auto &s = d.Counts();
	out << "schema=org.openjk.lighting-observations version=" << kMajor << '.' << kMinor
		<< "\nsource=" << m.map.path << " sha256=" << Disk::Hex(m.map.sha256)
		<< " bytes=" << m.map.bytes << "\nproducer=" << m.producer
		<< " version=" << m.producerVersion << "\ncompiler=" << m.compiler
		<< " version=" << m.compilerVersion << " known=" << m.compilerKnown
		<< "\nsynthetic=" << m.synthetic << " hint-policy=" << uint32_t(m.hints)
		<< " world-units-per-metre=" << m.units.worldUnitsPerMeter
		<< " absolute-scale=unknown\nfile-bytes=" << s.fileBytes
		<< " raw-image-bytes=" << s.rawImageBytes << " images=" << s.images
		<< " pixels=" << s.imagePixels << " missing-images=" << s.missingImages
		<< " deluxe=" << s.deluxeImages << "\nsurfaces=" << s.surfaces << " vertices=" << s.vertices
		<< " triangles=" << s.triangles << " grid-cells=" << s.gridCells
		<< " missing-grid=" << s.missingGridCells << " invalid-grid=" << s.invalidGrid
		<< "\nderived-samples=" << s.mappedSamples << " covered=" << s.coveredSamples
		<< " padding=" << s.paddingSamples << " ambiguous=" << s.ambiguousSamples
		<< " complete=" << s.derivedComplete << "\ncandidates=" << s.candidates
		<< " evidence=" << s.evidence << " regions=" << s.regions
		<< " region-cells=" << s.regionCells << "\npartition-seed=" << m.splitSeed
		<< " spatial-block=" << m.spatialBlockWorld << "\n";
	return out.str();
}
Error ReplayGeometry(const Sources &s, const RegionSnapshot &snapshot, const ReadContext &ctx,
					 std::shared_ptr<const StaticVisibility> &rays,
					 std::shared_ptr<const LightingRegionGraph> &graph,
					 const VisibilityBuildOptions *explicitBuildOptions)
{
	rays.reset();
	graph.reset();
	if (!s.geometry || !s.domain)
		return Failure(Code::Unavailable, "Replay requires visual and BSP geometry");
	VisibilityBuildOptions o;
	o.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
	o.maxBytes = ctx.maxBytes;
	if (explicitBuildOptions)
		o = *explicitBuildOptions;
	auto e = StaticVisibility::Build(s.geometry, o, ctx, rays, s.domain);
	if (!e)
		return e;
	RegionBuildInput input;
	input.identity = s.metadata.map;
	input.domain = s.domain;
	input.geometry = rays;
	input.dynamic = s.dynamic;
	return LightingRegionGraph::Build(input, snapshot.options, ctx, graph);
}
} // namespace Offline
} // namespace Core
} // namespace Rend2LightRecon
