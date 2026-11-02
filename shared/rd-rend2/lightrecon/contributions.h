// P06 prepared attribution over S13 codecs. No renderer or light fitting.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "producer.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Attribution
{
constexpr uint32_t kVersion = 1;
struct Ledger
{
	Id sample = 0, sampleDomain = 0, configuration = 0;
	Producer::ObservationDomain observationDomain = Producer::ObservationDomain::Unknown;
	std::string contributionDomain;
	PhotometricUnit contributionUnit = PhotometricUnit::Relative;
	Id calibration = 0;
	PhotometricUnit observationUnit = PhotometricUnit::Relative;
	Id observationCalibration = 0;
	uint32_t style = 0;
	Vec3 observedLower, observedUpper, modelObservation, modelMinusObservedInterval;
	Vec3 direct, sun, sky, ambient, indirect, unresolved, preencodingTotal;
	uint8_t presentMask = 0, unboundedUpperMask = 0;
	uint32_t issues = 0;
	bool predictionAvailable = false, componentTotalsAvailable = false, attributionComplete = false,
		 physicalConservationTested = false;
	bool discrepancyAvailable =
		false; // independent target/operator comparability, not just a value
	// A display diagnostic, not source energy fraction: model/observation only
	// when the observation is exact, positive, linear and in the SAME domain.
	bool ratioAvailable = false, luminanceAvailable = false;
	Vec3 modelObservedRatio;
	double modelLuminance = 0, observedLuminanceLower = 0, observedLuminanceUpper = 0;
};
struct PreparedWork
{
	uint64_t samples = 0, records = 0, bytes = 0;
};
class Prepared final : public CpuEvaluator
{
	struct Data;
	std::shared_ptr<const Data> data_;
	explicit Prepared(std::shared_ptr<const Data>);
	friend Error Prepare(const Producer::Artifact &, const ReadContext &,
						 std::shared_ptr<const Prepared> &, PreparedWork *);

  public:
	Status AcceptsAsset(const ReconstructedLightingAsset &) const override;
	Status Supports(Capability) const override;
	Status Contribution(const ReconstructedLightingAsset &, const ContributionQuery &,
						ContributionSample &) const override;
	// Stable sample/domain IDs; no I/O or cached lazy evaluation.
	Status Audit(Id sampleDomain, Id sample, Ledger &) const;
};
Error Prepare(const Producer::Artifact &, const ReadContext &, std::shared_ptr<const Prepared> &,
			  PreparedWork * = nullptr);
struct Component
{
	Id dataset = 0; // contribution dataset or transport-field dataset, never runtime index
	bool field = false;
};
struct Decision
{
	Status status = Status::Unavailable;
	std::string reason;
	uint32_t missingProofs = 0, contradictedProofs = 0, uncertainty = 0;
	Estimate maximumAbsoluteError, leakageRisk;
	explicit operator bool() const
	{
		return status == Status::Ok;
	}
};
// Validates declared algebraic membership on overlapping support. Does NOT claim
// the model is historically true or suitable for runtime shading.
Decision CanAdd(const ReconstructedLightingAsset &, const Component &, const Component &);
struct ReplacementRequest
{
	ContributionQuery contribution;
	std::string targetDomain, runtimeOperator;
	ContributionColour colour = ContributionColour::Unknown;
	PhotometricUnit unit = PhotometricUnit::Relative;
	Id calibration = 0;
	double maximumAbsoluteError = 0,
		   maximumLeakageRisk = 0; // independently declared acceptance budgets
};
Decision MayReplace(const ReconstructedLightingAsset &, const Prepared &,
					const ReplacementRequest &);
} // namespace Attribution
} // namespace Core
} // namespace Rend2LightRecon
