// CPU forward-evaluator contract for feature-scoped bake hypotheses (X03).
// Concrete CPU implementation: bake_forward.h. No runtime lighting integration.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "decode.h"
#include "disk.h"
#include <array>

namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
constexpr uint32_t kProfileSchemaVersion = 2, kFormulaVersion = 2;
enum class Feature
{
	Attenuation,
	Angle,
	Spot,
	PointScale,
	SurfaceIntegration,
	SunSky,
	GridReduction,
	Deluxe,
	BounceFill,
	OutputEncoding,
	SamplingVisibility,
	AbsoluteCalibration,
	Ambient,
	LightStyles,
	ReceiverPlacement,
	Filtering,
	Count
};
enum class Knowledge
{
	Unknown,
	Assumed,
	SourceSupported,
	VerifiedSubset
};
enum class CompilerIdentification
{
	Unidentified,
	KnownControlledRecipe,
	IndependentHistoricalProvenance
};
enum class Applicability
{
	Unassessed,
	AssumedForDataset,
	VerifiedFeatureSubset
};
struct Parameter
{
	std::string name, unit;
	Estimate value; // same finite/bounded/unknown semantics as canonical assets
};
struct FeatureRule
{
	Knowledge knowledge = Knowledge::Unknown;
	std::string selector, provenance, verificationScope;
	uint32_t formulaVersion = kFormulaVersion;
	std::vector<Parameter> parameters;
	std::vector<std::string> unsupported;
};
struct ProfileHypothesis
{
	uint32_t schemaVersion = kProfileSchemaVersion, formulaVersion = kFormulaVersion;
	std::string id = "UnknownProfile", family = "UnknownProfile";
	std::string compilerSourceRevision, compilerBinarySha256; // empty when unidentified
	Disk::Digest contentKey{}, sourceLockKey{}, catalogueKey{};
	std::array<FeatureRule, static_cast<size_t>(Feature::Count)> rules;
	std::vector<Parameter> nuisanceBounds;
	CompilerIdentification compiler = CompilerIdentification::Unidentified;
	Applicability applicability = Applicability::Unassessed;
	const FeatureRule *Rule(Feature f) const
	{
		const size_t index = static_cast<size_t>(f);
		return index < rules.size() ? &rules[index] : nullptr;
	}
};
enum class Domain
{
	EncodedRgb8BeforeQuantization,
	CompilerLinearRelative255,
	SourceLinearRelative,
	GridAmbientDirected,
	DeluxeWorldDirection,
	PhysicalIrradianceLux // SI reference only, never decoded stock RGB
};
enum class SourceKind
{
	Point,
	Spot,
	Polygon,
	Tube,
	Directional,
	Unsupported,
	Sky // explicit environment hypothesis, never a finite physical emitter
};
enum class AmplitudeDomain
{
	CompilerRelative,
	CanonicalPhotometry
};
struct SourceHypothesis
{
	Id id = 0;
	SourceKind kind = SourceKind::Unsupported;
	Position position;
	Frame orientation; // spot forward = propagation; incoming direction = -forward
	AmplitudeDomain amplitudeDomain = AmplitudeDomain::CompilerRelative;
	Vec3 compilerRgbAmplitude;	  // Relative only: I=max(Q), c=Q/I. Not cd/lux/nits.
	bool amplitudeKnown = false;  // compiler-relative Q only
	Photometry canonicalEmission; // Active only for CanonicalPhotometry. Canonical Y=1
	// colour, intensity unit and calibration ID semantics apply without rescaling Q.
	EmitterGeometry geometry;
	AngularDistribution angular;
	Chunk<Vec3> polygon; // optional immutable world-space winding; source owns lease
	Calibration calibration;
	Units worldUnits;
	// Expanded legacy spot geometry, not canonical smooth-cone parameters.
	double spotTargetDistanceWorld = 0, spotRadiusWorld = 0;
	// Explicit mapping only. Neither unit nor current physicalToScene implies it.
	Estimate compilerParametersPerCanonicalUnit;
	std::string calibrationReference, calibrationMethod;
};
struct FootprintTriangle
{
	Vec3 world[3], normal[3]; // disjoint UV-clipped world triangles, covered area only
};
struct Receiver
{
	Id sample = 0, surface = 0, material = 0;
	Vec3 world, geometricNormal, shadingNormal; // +Z up, normals outward, world units
	uint8_t style = 0;
	Domain domain = Domain::EncodedRgb8BeforeQuantization;
	bool mappedPlanar = false, compilerPlacementCertified = false, twoSided = false;
	// Supplied original sample is unchanged. Recipe's explicit projection push
	// produces the compiler sample; no inferred nudge at padding/seams/solid cells.
	Chunk<FootprintTriangle> footprint;
	double geometryErrorWorld = 0;
	double mappingErrorWorld = 0; // numerical UV mapping bound, distinct from curved geometry envelope
};
enum class Status
{
	UnspecifiedModel,
	UnsupportedDomain,
	InvalidInput,
	AvailableRelative,
	AvailablePhysical,
	UnsupportedFeature,
	VisibilityUnknown,
	AvailableConditional
};
enum class Mode { CompilerFaithful, PhysicalIntegral, OptimizationSurrogate };
enum class TraceMode { StaticGeometry, CompilerTree };
enum class Fidelity { Unsupported, ConditionalGeometry, ScopedCompilerReplay, PhysicalIntegral, Surrogate };
enum PredictionIssue : uint32_t
{
	AssumedProfileApplicability = 1u << 0,
	ConditionalTracePolicy = 1u << 1,
	UnresolvedAbsolutePhotometricScale = 1u << 2,
	SourceParameterUncertainty = 1u << 3,
	ApproximationWithoutTotalErrorBound = 1u << 4
};
struct Work
{
	uint64_t pairs = 0, rays = 0, nodeTests = 0, triangleTests = 0, quadrature = 0, cacheHits = 0;
};
struct Prediction
{
	Status status = Status::UnspecifiedModel;
	bool available = false; // zero payload with available=false is NOT black lighting
	Domain domain = Domain::EncodedRgb8BeforeQuantization;
	PhotometricUnit unit = PhotometricUnit::Relative;
	Vec3 rgb;						// active for surface RGB domains only
	Vec3 gridAmbient, gridDirected; // active for GridAmbientDirected; compiler output
	// channels as specified by the selected output rule, not physical indirect/direct.
	Vec3 directionToSource; // active for grid/deluxe: unit receiver -> incoming light
	bool directionKnown = false, encodedDirectionKnown = false;
	uint8_t encodedDirection[2] = {0, 0};	   // grid lat/long only; selector defines quantizer
	Estimate angularErrorRadians;			   // quantization/model uncertainty; unknown by default
	Estimate numericalError, modelDiscrepancy; // unknown unless independently bounded
	Estimate integratedVisibility; // receiver/source area fraction; NOT irradiance ratio
	std::vector<Feature> missing;
	Id observation = 0;
	Fidelity fidelity = Fidelity::Unsupported;
	Vec3 direct, sun, sky, ambient; // PRE-encoding compatible domain only
	Vec3 compilerRelative, encodedPrequantization;
	uint8_t packedRgb[3] = {}, packedAmbient[3] = {}, packedDirected[3] = {}, deluxeRgb[3] = {};
	bool packedKnown = false, deluxeKnown = false, surrogate = false;
	uint32_t visibilityIssues = 0;
	uint32_t issues = 0; // model/scale assumptions, separate from optical visibility issues
	std::string reason;
	Work work;
};
struct Contribution
{
	Id observation = 0, source = 0;
	SourceKind kind = SourceKind::Unsupported;
	Vec3 preencodingRgb;
	Status status = Status::UnspecifiedModel;
	bool globallyLinearInAmplitude = false;
};
class ContributionSink
{
  public:
	virtual ~ContributionSink() = default;
	virtual Error Write(const Contribution &) = 0; // synchronous, no retained caller pointers
};
class PreparedFactors;
class CompilerTrace;
struct IntegralScene;
struct Options
{
	std::vector<Feature> required;
	Domain output = Domain::EncodedRgb8BeforeQuantization;
	PhotometricUnit requestedUnit = PhotometricUnit::Relative;
	bool externallyCertifiedVisibility = false; // explicit assumption, no native trace claim
	Mode mode = Mode::CompilerFaithful;
	TraceMode trace = TraceMode::StaticGeometry;
	bool allowAssumed = false, unpackedGridOnly = false, requireScopedCompilerReplay = false;
	uint64_t maxPairs = 1048576, maxRays = 1048576, maxWork = 32ull * 1024 * 1024;
	unsigned quadratureDepth = 6;
	double integralTolerance = 1e-7;
	ContributionSink *contributions = nullptr; // borrowed call-only; optional sparse stream
	std::shared_ptr<const PreparedFactors> factors;
};
struct Result
{
	std::string profileId;
	Disk::Digest profileKey{};
	Chunk<Prediction> predictions; // immutable owning lease, may outlive the evaluator
	Mode mode = Mode::CompilerFaithful;
	Work work;
};
struct EvaluationContext
{
	// Geometry-only inputs: do not pass target RGB or answer files to an evaluator.
	std::shared_ptr<const VisibilityProvider> visibility;
	std::shared_ptr<const ChunkSource<Triangle>> triangles;
	std::shared_ptr<const ChunkSource<Material>> materials;
	Disk::Digest geometryKey{}, materialKey{}, observationPolicyKey{};
	uint64_t samplingSeed = 0;
	bool samplingSeedKnown = false, orderedGridReduction = false;
	std::shared_ptr<const CompilerTrace> compilerTrace;
	std::shared_ptr<const IntegralScene> integralScene;
	// Chunk reads honour ReadContext budgets/cancellation. Missing inputs/rules
	// return Unavailable; neither visibility nor material reflectance is invented.
};
// Preflight has no numerical lighting, I/O, renderer state or reconstruction work.
inline Error Require(const ProfileHypothesis &p, const Options &o, Prediction &out)
{
	out = Prediction();
	if (p.schemaVersion != kProfileSchemaVersion || p.formulaVersion != kFormulaVersion)
	{
		out.status = Status::InvalidInput;
		return Failure(Code::UnsupportedSchema, "Unsupported bake-profile schema/formula");
	}
	auto required = o.required;
	if (o.requestedUnit != PhotometricUnit::Relative)
		required.push_back(Feature::AbsoluteCalibration);
	for (Feature f : required)
	{
		const auto *r = p.Rule(f);
		if (!r || r->knowledge == Knowledge::Unknown || r->selector.empty())
		{
			out.missing.push_back(f);
			continue;
		}
		if (r->formulaVersion != kFormulaVersion)
		{
			out.status = Status::InvalidInput;
			return Failure(Code::UnsupportedSchema, "Unsupported feature formula version");
		}
		if (r->provenance.empty() ||
			(r->knowledge == Knowledge::VerifiedSubset && r->verificationScope.empty()))
		{
			out.status = Status::InvalidInput;
			return Failure(Code::InvalidInput, "Feature evidence/verification scope required");
		}
	}
	if (!out.missing.empty())
		return Failure(Code::Unavailable, "UnspecifiedModel: required bake rules missing");
	return {};
}
class ForwardEvaluator
{
  public:
	virtual ~ForwardEvaluator() = default;
	// Borrow request spans for this call only. Output owns its immutable buffers.
	// Decode::Profile is the EXISTING observation policy, not a second calibration
	// system. Unknown primaries/transfer prohibit an unjustified linear/SI compare.
	// Full grid reduction requires ordered batch state; cannot be a fake point query.
	// On success predictions match receiver count/order (including explicit per-row
	// UnspecifiedModel/UnsupportedDomain statuses). On Error publish no partial result.
	// Evaluate must also validate finite geometry/photometry, selector support and
	// required context; successful Require alone does not certify implementation.
	virtual Error Evaluate(const ProfileHypothesis &, const Chunk<SourceHypothesis> &,
						   const Chunk<Receiver> &, const Decode::Profile &,
						   const EvaluationContext &, const Options &, const ReadContext &,
						   Result &) const = 0;
};
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
