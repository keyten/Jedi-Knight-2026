// CPU forward-evaluator contract for feature-scoped bake hypotheses (X03).
// No evaluator/solver or renderer integration is provided by this header.
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
constexpr uint32_t kProfileSchemaVersion = 1, kFormulaVersion = 1;
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
	DeluxeWorldDirection
};
enum class SourceKind
{
	Point,
	Spot,
	Polygon,
	Tube,
	Directional,
	Unsupported
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
};
struct Receiver
{
	Id sample = 0, surface = 0, material = 0;
	Vec3 world, geometricNormal, shadingNormal; // +Z up, normals outward, world units
	uint8_t style = 0;
	Domain domain = Domain::EncodedRgb8BeforeQuantization;
};
enum class Status
{
	UnspecifiedModel,
	UnsupportedDomain,
	InvalidInput,
	AvailableRelative,
	AvailablePhysical
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
	std::vector<Feature> missing;
};
struct Options
{
	std::vector<Feature> required;
	Domain output = Domain::EncodedRgb8BeforeQuantization;
	PhotometricUnit requestedUnit = PhotometricUnit::Relative;
	bool externallyCertifiedVisibility = false; // explicit assumption, no native trace claim
};
struct Result
{
	std::string profileId;
	Disk::Digest profileKey{};
	Chunk<Prediction> predictions; // immutable owning lease, may outlive the evaluator
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
