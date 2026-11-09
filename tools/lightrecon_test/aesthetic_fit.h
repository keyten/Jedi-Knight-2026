// ASR20 experimental offline fitting. No canonical asset writer or renderer link.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/emitter_shape.h"
namespace Rend2LightRecon
{
namespace Aesthetic
{
using namespace Core;
enum class Domain
{
	Unknown,
	RelativeLinearAttested,
	ForwardEncodedRgb8
};
struct Targets
{
	Domain domain = Domain::Unknown;
	// Fixed gauge, never optimized alongside RGB. Powers are arbitrary relative units.
	double gain = 1;
	Bake::ProfileHypothesis encoding;
	bool comparableMaterialResponseAttested = false;
	bool sameMaterialResponseAssumed = false; // encoded appearance only, never relative irradiance
	Id material = 0;
	std::string provenance;
};
struct Fixture
{
	// First entry is the supplied fixture geometry. Optional finite spot axis/cone
	// alternatives require independent evidence and explicit bounds; XYZ never moves.
	std::vector<Shape::Emitter> variants;
	Vec3 upperPower = Vec3(100000, 100000, 100000);
	std::string provenance, directionProvenance;
	double maximumAxisRadians = 0, minimumOuterRadians = 0, maximumOuterRadians = 0;
};
struct Configuration
{
	std::string name;
	std::vector<size_t> fixtures;
};
struct Options
{
	size_t maxRows = 4096, maxSweeps = 60, maxGeometrySweeps = 3;
	uint64_t maxWork = 100000000, maxRays = 4000000, maxBytes = 64ull * 1024 * 1024;
	double huber = 4, tolerance = 1e-8, directionImprovement = 1e-5;
	double validationPenaltyPerFixture = 0;
	double validationEquivalentSlack = 1e-4; // standardized robust-loss units, not confidence
	Vec3 backgroundUpper; // nonnegative spatially constant nuisance, in relative units
};
struct Request
{
	Shape::Model model; // DiffuseIntegral + RelativeIncident only
	Targets targets;
	PointRefine::SceneInput scene;
	Bake::EvaluationContext geometry;
	Shape::Sampling sampling;
	std::vector<Fixture> fixtures;
	std::vector<Configuration> configurations;
	Options options;
};
struct Score
{
	bool available = false;
	double robust = 0, mse = 0, maximumError = 0;
	size_t rows = 0, blocks = 0, normalBins = 0;
};
struct SourceDiagnostic
{
	Id fixture = 0;
	size_t variant = 0;
	Shape::Emitter emitter;
	Vec3 relativePower;
	double columnRms = 0, maximumColumnCorrelation = 0, directionTrainingGain = 0,
		   quadratureDifference = 0;
	bool atPowerBound = false, directionConstrained = false;
	SourceSupport::Result placement;
};
struct Fit
{
	std::string name, limitation;
	bool available = false, converged = false, backgroundAtBound = false;
	Vec3 background;
	Score training, validation;
	std::vector<SourceDiagnostic> sources;
};
class Experiment
{
  public:
	struct Data;
	const std::vector<Fit> &Fits() const;
	const Fit &Baseline() const;
	size_t Selected() const; // SIZE_MAX means no validated local-light improvement
	const std::vector<size_t> &ValidationAlternatives() const;
	uint64_t Work() const;

  private:
	std::shared_ptr<const Data> data;
	friend Error FitAndValidate(const Request &, const std::vector<Shape::Observation> &,
								const std::vector<Shape::Observation> &, const ReadContext &,
								Experiment &);
	friend Error AuditHeldout(const Experiment &, const std::vector<Shape::Observation> &,
							  const ReadContext &, std::vector<Score> &, Score &);
};
// Snapshots bounded rows. Validation is used only after every training fit freezes.
Error FitAndValidate(const Request &, const std::vector<Shape::Observation> &training,
					 const std::vector<Shape::Observation> &validation, const ReadContext &,
					 Experiment &);
// Same frozen nuisance, geometry, powers, weights and transfer. Cannot refit/rerank.
Error AuditHeldout(const Experiment &, const std::vector<Shape::Observation> &, const ReadContext &,
				   std::vector<Score> &, Score &baseline);
} // namespace Aesthetic
} // namespace Rend2LightRecon
