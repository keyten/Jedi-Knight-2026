// Profile-scoped CPU observation prediction. No fitting, GL, cvars or world_t.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_profile.h"
#include "visibility.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{

enum class Falloff
{
	InverseSquare,
	Linear
};
enum class NormalResponse
{
	Cosine,
	None,
	HalfLambert
};
struct Recipe
{
	Falloff falloff = Falloff::InverseSquare;
	NormalResponse normal = NormalResponse::Cosine;
	double photonsPerParameter = 7500, minimumDistanceWorld = 16, fade = 1;
	double extraDistanceWorld = 0, angleScale = 0, tolerance = 1;
	bool fast = true;
	double receiverPushWorld = 1, areaParameterScale = .75;
	double gamma = 1, compensate = 1, exposure = 0;
	bool srgbOutput = false;
	Vec3 explicitSurfaceAmbient; // zero by default, never inferred from target RGB
	unsigned skyIterations = 3;	 // only the verified 17-direction J=3 subset
};
// Constructs explicit rules/evidence, including unsupported features. It does
// not attest that an arbitrary dataset was baked with this recipe.
ProfileHypothesis NetRadiantProfile(const Recipe & = Recipe());

struct Support
{
	bool implemented = false;
	Fidelity fidelity = Fidelity::Unsupported;
	std::string operation, scope, reason;
};
Support Capability(const ProfileHypothesis &, SourceKind, const Receiver &, const Options &);
Error Encode(const ProfileHypothesis &, Vec3 relative255, Vec3 &prequantized, uint8_t packed[3]);
// Numerical implementation metadata, not Require() alone. No default recipe for
// UnknownProfile. These helpers reuse the production kernels and factor identity.
Error DescribeRecipe(const ProfileHypothesis &, Recipe &);
// Already validated recipe only; identical encoding/value kernels, no new model.
Error Encode(const Recipe &, Vec3 relative255, Vec3 &prequantized, uint8_t packed[3]);
Disk::Digest FactorDependencyKey(const ProfileHypothesis &, const Chunk<SourceHypothesis> &,
								 const Chunk<Receiver> &, const EvaluationContext &, const Options &);
struct LinearResponse
{
	bool proven = false, inactive = false;
	double coefficient = 0; // pre-encoding compiler-relative RGB / Q, fixed geometry
	std::string reason;
};
struct Factor;
Error ProveLinearResponse(const ProfileHypothesis &, const SourceHypothesis &, const Factor &, Vec3 lowerQ,
						  Vec3 upperQ, LinearResponse &);
Error ProveLinearResponse(const Recipe &, const SourceHypothesis &, const Factor &, Vec3 lowerQ, Vec3 upperQ,
						  LinearResponse &);
// Geometry-only adapter; deliberately accepts no DecodedSurfaceObservation or RGB.
Error ReceiverFromLuxel(Id observation, const SurfaceLightingObservation &, const BakedSurface &,
						bool compilerPlacementCertified, Receiver &);

struct Factor
{
	Id source = 0, observation = 0;
	double distanceWorld = 0, cosine = 0, cone = 1, polygon = 0;
	Vec3 incoming;
	VisibilityState visibility = VisibilityState::Unknown;
	uint32_t issues = 0;
};
class PreparedFactors
{
  public:
	const std::vector<Factor> &Records() const { return records_; }
	const Disk::Digest &Key() const { return key_; }
	const Work &PreparationWork() const { return preparation_; }

  private:
	friend class CpuForwardEvaluator;
	Disk::Digest key_{};
	std::vector<Factor> records_;
	Work preparation_;
};
class CpuForwardEvaluator final : public ForwardEvaluator
{
  public:
	Error Evaluate(const ProfileHypothesis &, const Chunk<SourceHypothesis> &, const Chunk<Receiver> &,
				   const Decode::Profile &, const EvaluationContext &, const Options &, const ReadContext &,
				   Result &) const override;
	// Optional bounded receiver block; no permanent map-wide dense matrix. Key
	// excludes RGB amplitude but includes source XYZ/shape/units, sample geometry,
	// profile recipe, trace mode and all context generations. Active cutoffs are
	// recomputed at Evaluate, NOT cached as an amplitude-independent matrix.
	Error Prepare(const ProfileHypothesis &, const Chunk<SourceHypothesis> &, const Chunk<Receiver> &,
				  const EvaluationContext &, const Options &, const ReadContext &,
				  std::shared_ptr<const PreparedFactors> &) const;
};

struct TracePrimitive
{
	Triangle triangle;
	ObstacleKind optical = ObstacleKind::Unsupported;
};
struct IntegralScene
{
	// Bounded local reference geometry; explicit complete coverage assertion.
	// Caller owns immutable lease. Unknown blockers keep the integral unavailable.
	Chunk<TracePrimitive> triangles;
	bool complete = false;
	Disk::Digest geometryKey{};
};
// Physical irradiance over exact supplied footprint triangles. Separate from
// q3map super1 and from a smooth/point surrogate. Uses canonical SI photometry.
Error PhysicalReference(const Chunk<SourceHypothesis> &, const Receiver &, const EvaluationContext &,
						const Options &, const ReadContext &, Prediction &);

} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
