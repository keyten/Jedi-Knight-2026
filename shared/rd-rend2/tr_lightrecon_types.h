// Canonical reconstructed static lighting. See docs/rend2-reconstructed-lighting.md.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Rend2LightRecon {

static constexpr const char* kSchema = "org.openjk.reconstructed-lighting";
static constexpr uint32_t kSchemaVersion = 2;
static constexpr float kUnitTolerance = 1.0e-4f;

// Persistent producer-assigned identities, never vector indexes or addresses.
// Zero is an absent reference. IDs are scoped to an asset lineage. See the doc
// for allocation, collision checking and preservation across revisions.
typedef uint64_t Id;

struct Vec3
{
	float x = 0.0f, y = 0.0f, z = 0.0f;
	Vec3() = default;
	Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Unknown is different from zero. Bounds may be known without a central value.
// All payloads must be finite even when unknown; unused values default to zero.
struct Estimate
{
	bool known = false;
	float value = 0.0f;
	bool bounded = false;
	float lower = 0.0f, upper = 0.0f;
};

struct Position
{
	bool known = false;
	Vec3 world;
	Estimate sigmaWorld; // isotropic one-sigma positional uncertainty, not confidence
};

// JA right-handed world space, +Z up. Local X=right, Y=up, Z=forward;
// cross(right, up)=forward. No camera/view transform. Known frames are unit
// orthogonal. Forward is photon travel for spot/directional; a receiver's
// direction towards that source is -forward. Rectangles emit along forward;
// tubes extend along right. Incoming field directions point towards the light.
struct Frame
{
	bool known = false;
	Vec3 right = Vec3(1.0f, 0.0f, 0.0f);
	Vec3 up = Vec3(0.0f, 1.0f, 0.0f);
	Vec3 forward = Vec3(0.0f, 0.0f, 1.0f);
};

struct Units
{
	float worldUnitsPerMeter = 33.3333333333f;
	float physicalToScene = 1.0e-4f; // fixed scene scale, never exposure
};

enum ObservationIssue : uint32_t
{
	ObservationClipped = 1u << 0,
	ObservationMissing = 1u << 1,
	ObservationQuantized = 1u << 2,
	ObservationUnsupportedMaterial = 1u << 3,
	ObservationAnimated = 1u << 4,
	ObservationUnreliable = 1u << 5
};

enum class ProvenanceKind { Authored, LegacyLightmap, LegacyLightgrid, Geometry, Reconstruction, External };
struct Provenance
{
	Id id = 0;
	ProvenanceKind kind = ProvenanceKind::Reconstruction;
	std::string locator; // input identifier/URI, not an owned observation buffer
	std::string contentHash; // algorithm-prefixed digest when available
	std::string producer; // tool and version
	std::string method;
	std::vector<Id> parents; // provenance DAG; no renderer or BSP object pointers
	uint32_t observationIssues = 0; // aggregate issues, not inferred error bars
	Id observationQuality = 0; // optional external per-sample issue-mask dataset
};

enum class CalibrationKind { PhysicallyAuthored, LegacySceneEquivalent, UncalibratedRelative };
struct Calibration
{
	Id id = 0;
	CalibrationKind kind = CalibrationKind::UncalibratedRelative;
	Id provenance = 0;
	std::string method; // required for legacy calibration; includes reference/scale rationale
};

enum class PhotometricUnit { Relative, Candela, Nits, Lux };
struct Chromaticity
{
	bool known = false;
	Vec3 rgb; // nonnegative linear sRGB/Rec.709 D65, Y=1; components may exceed 1
};
struct Photometry
{
	PhotometricUnit unit = PhotometricUnit::Relative;
	Estimate intensity; // nonnegative Y; bounds in the same unit, not a confidence
	Chromaticity color;
	Id calibration = 0; // zero allowed only for unknown, relative photometry
};

enum class HypothesisState { Candidate, Resolved, Rejected };
struct HypothesisGroup
{
	Id id = 0;
	std::string description; // mutually exclusive alternatives, possibly incomplete
};
enum class Suitability { Unknown, Unsuitable, Candidate };
struct Evidence
{
	Estimate physicalEmitterConfidence; // [0,1], emitter existence/classification
	Estimate transportConfidence; // [0,1], observed lighting/transport reliability
	uint32_t observationIssues = 0;
	Suitability exactLight = Suitability::Unknown;
	Suitability approximateIrradiance = Suitability::Unknown; // independent assessment
};
struct Source
{
	Id id = 0; // shared source namespace: emitters, environments AND transport fields
	Id provenance = 0;
	Id hypothesisGroup = 0; // zero = independent source
	HypothesisState state = HypothesisState::Candidate;
	Estimate hypothesisWeight; // [0,1], not required to sum to 1 for an incomplete group
	std::vector<Id> regions;
	Evidence evidence;
};

enum class EmitterKind : uint32_t { Point = 1, Spot, Rectangle, Tube, Directional, Extension = 0x10000 };
struct EmitterGeometry
{
	bool known = false;
	float halfWidthWorld = 0.0f, halfHeightWorld = 0.0f; // rectangle, strictly positive
	float halfLengthWorld = 0.0f, radiusWorld = 0.0f; // open finite cylinder, strictly positive
	Estimate effectiveRadiusWorld; // point/spot softening/emitting size; >0 when known
	Estimate angularRadiusRadians; // directional apparent disk radius [0,pi/2)
	Id extension = 0; // external geometry for extension types
};
enum class AngularKind { Unknown, Isotropic, SmoothCone, Lambertian, Parallel, Tabulated };
struct AngularDistribution
{
	AngularKind kind = AngularKind::Unknown;
	float cosInner = 1.0f, cosOuter = 0.0f; // smooth cone: -1<=outer<inner<=1
	bool twoSided = false; // Lambertian rectangle only; tube emits its curved side
	Id dataset = 0; // tabulated dimensionless profile; peak=1, directions in local frame
};
struct PhysicalEmitter
{
	Source source;
	EmitterKind kind = EmitterKind::Point;
	std::string extensionType; // namespaced versioned key for Extension (or future >=0x10000)
	Position position; // centre; directional has no finite position
	Frame frame;
	EmitterGeometry geometry;
	Photometry emission;
	AngularDistribution angular;
	Estimate rangeWorld; // optional finite culling extent >0; NOT brightness or physical cutoff
};
struct SunSource
{
	Id emitter = 0; // directional PhysicalEmitter ID, including candidate hypotheses
};

struct Bounds
{
	bool known = false;
	Vec3 minWorld, maxWorld; // inclusive AABB; zero extent permitted for surfaces
};
struct GeometryRegion
{
	Id id = 0;
	Id provenance = 0;
	std::string key; // geometry-independent region name; not a runtime BSP cluster index
	Bounds bounds;
	Id membership = 0; // optional external geometry membership dataset
};

// Large data is ALWAYS referenced, never obligatorily resident. The asset owns
// only these descriptors/strings. A future loader owns decoded chunks/leases;
// no borrowed input memory, GL objects, texture slots or device offsets live here.
enum class DatasetPurpose { AngularProfile, EmitterGeometry, Environment, Transport, Visibility,
	Contributions, RegionMembership, SampleDomain, ObservationQuality };
enum class DatasetLayout { Dense, Sparse, Chunked, Procedural };
struct Dataset
{
	Id id = 0;
	DatasetPurpose purpose = DatasetPurpose::Transport;
	DatasetLayout layout = DatasetLayout::Chunked;
	std::string locator;
	std::string contentHash;
	std::string encoding; // namespaced CPU payload schema, not a graphics format
	uint32_t encodingVersion = 1;
	uint64_t elementCount = 0; // logical records; zero means empty, not unknown
	uint64_t chunkElements = 0; // >0 for Chunked; maximum decoded records per chunk
};

enum class SunCoverage { Unknown, ExcludesExplicitSun, IncludesExplicitSun };
struct EnvironmentLighting
{
	Source source;
	Photometry meanLuminance; // Nits or Relative; unit/calibration also describe radiance dataset
	Id radiance = 0; // optional external directional scene-linear radiance distribution
	SunCoverage sunCoverage = SunCoverage::Unknown; // adapter must resolve double counting
};
enum class TransportComponent { IndirectIrradiance, ReflectedRadiance, ApertureTransport,
	UnresolvedDirectIrradiance, UnattributedIncidentRadiance, UnresolvedIrradiance };
enum class FieldQuantity { Irradiance, Radiance };
enum class FieldMembership { Unknown, Excluded, Included };
enum class TransportRootKind { LocalEmitter, Sun, Sky, LegacyAmbient, Unknown };
struct TransportRoot
{
	Id source = 0; // zero only for an explicitly unknown root
	TransportRootKind kind = TransportRootKind::Unknown;
};
struct TransportMembership
{
	uint32_t version = 1;
	std::string model; // namespaced operator/hypothesis identity, not a physical-certainty label
	std::vector<TransportRoot> roots;
	FieldMembership directLocal = FieldMembership::Unknown, directSun = FieldMembership::Unknown,
		directSky = FieldMembership::Unknown, legacyAmbient = FieldMembership::Unknown,
		reflected = FieldMembership::Unknown, unresolved = FieldMembership::Unknown;
	bool reflectionOrderKnown = false;
	uint32_t minimumReflectionOrder = 0, maximumReflectionOrder = 0;
	uint32_t style = 0; // canonical portable integer, 0..255
};
struct TransportField
{
	Source source; // transport estimates are NEVER physical emitter records
	TransportComponent component = TransportComponent::IndirectIrradiance;
	FieldQuantity quantity = FieldQuantity::Irradiance;
	PhotometricUnit unit = PhotometricUnit::Relative; // Lux for irradiance, Nits for radiance
	Id calibration = 0;
	Bounds bounds;
	uint32_t sampleDimensions[3] = { 0, 0, 0 }; // optional world-space regular lattice; all zero or all >0
	Id dataset = 0; // absent data is valid incomplete metadata, not zero illumination
	TransportMembership membership; // consumers must resolve Unknown before composing fields
};
enum class VisibilityKind { SegmentTransmittance, RegionConnectivity, SkyTransmittance };
struct VisibilityReference
{
	Id dataset = 0;
	VisibilityKind kind = VisibilityKind::SegmentTransmittance;
	Id source = 0; // zero = dataset keyed by source IDs
	Id region = 0; // zero = dataset keyed by region IDs
	// Missing entries ALWAYS mean unknown. PVS/region connectivity is not exact visibility.
};
enum class ContributionComponent { Direct, Indirect, Unresolved };
enum class ContributionQuantity { IrradianceRGB, RadianceRGB, AttributionFraction };
struct ContributionReference
{
	Id dataset = 0;
	Id sampleDomain = 0; // external stable sample IDs/positions, independent of BSP observations
	std::vector<Id> sources; // allowed source IDs; each payload record names one of these
	ContributionComponent component = ContributionComponent::Unresolved;
	ContributionQuantity quantity = ContributionQuantity::IrradianceRGB;
	PhotometricUnit unit = PhotometricUnit::Relative;
	Id calibration = 0; // fractions require zero; RGB values follow field photometry rules
	// Sparse absence means unknown, not zero. Explicit zero records may be stored.
	// Values are attribution evidence, never an instruction to subtract/promote energy.
};
enum class Severity { Info, Warning, Error };
struct Diagnostic
{
	Severity severity = Severity::Info;
	std::string code, message;
	Id source = 0, region = 0;
	Estimate metric; // signed finite diagnostic, unknown allowed
	std::string metricUnit; // explicit e.g. "world", "relative-rms", "lux"
};

struct ReconstructedLightingAsset
{
	std::string schema = kSchema;
	uint32_t version = kSchemaVersion;
	std::string lineage; // producer-assigned persistent asset identity; may be empty for empty asset
	Units units;
	Id provenance = 0;
	std::vector<Provenance> provenanceRecords;
	std::vector<Calibration> calibrations;
	std::vector<HypothesisGroup> hypothesisGroups;
	std::vector<PhysicalEmitter> emitters;
	std::vector<SunSource> suns;
	std::vector<EnvironmentLighting> environments;
	std::vector<TransportField> transport;
	std::vector<GeometryRegion> regions;
	std::vector<Dataset> datasets;
	std::vector<VisibilityReference> visibility;
	std::vector<ContributionReference> contributions;
	std::vector<Diagnostic> diagnostics;
};

// Strict boundary helpers: reject non-finite/invalid scales and overflow,
// leave output unchanged on failure. Normalization matches the PBR contract's
// negative clamp / near-black boundary, but rejects NaN/Inf before clamping.
bool WorldToMeters(float world, const Units& units, float& meters);
bool MetersToWorld(float meters, const Units& units, float& world);
bool NormalizeChromaticity(const Vec3& rgb, Chromaticity& result);
bool SceneEmissionRGB(const ReconstructedLightingAsset& asset, const Photometry& emission, Vec3& rgb);

// Structural validation, no I/O and no estimator/renderer policy. First error
// with a field path is sufficient; unknown estimates are valid, NaN is not.
// Does not decode external payloads. Consumers must validate their encodings.
bool Validate(const ReconstructedLightingAsset& asset, std::string* error = nullptr);

} // namespace Rend2LightRecon
