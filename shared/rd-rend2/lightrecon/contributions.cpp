// Immutable prepared data; all decoding/checks precede publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "contributions.h"
#include "../tr_pbr_contract.h"
#include "bake_math.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Attribution
{
using namespace Bake::Detail;
namespace P = Producer;
namespace
{
void Need(bool v, Code c, const char *why)
{
	if (!v)
		throw Failure(c, why);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
Decision No(Status s, const char *why)
{
	Decision d;
	d.status = s;
	d.reason = why;
	return d;
}
double Y(Vec3 v)
{
	return Rend2PBR::kLumaR * v.x + Rend2PBR::kLumaG * v.y + Rend2PBR::kLumaB * v.z;
}
const ContributionReference *Select(const ReconstructedLightingAsset &a, const ContributionQuery &q,
									bool &ambiguous)
{
	const ContributionReference *found = nullptr;
	ambiguous = false;
	for (const auto &r : a.contributions)
		if (r.sampleDomain == q.sampleDomain && r.component == q.component &&
			r.quantity == q.quantity && (!q.dataset || r.dataset == q.dataset) &&
			(!q.configuration || r.configuration == q.configuration) &&
			std::find(r.sources.begin(), r.sources.end(), q.source.id) != r.sources.end())
		{
			if (found)
			{
				ambiguous = true;
				return nullptr;
			}
			found = &r;
		}
	return found;
}
struct Description
{
	TransportMembership member;
	Id calibration = 0, domainSamples = 0;
	PhotometricUnit unit = PhotometricUnit::Relative;
	ContributionQuantity quantity = ContributionQuantity::IrradianceRGB;
	bool valid = false;
};
Description Describe(const ReconstructedLightingAsset &a, const Component &c)
{
	Description d;
	if (c.field)
	{
		for (const auto &f : a.transport)
			if (f.dataset == c.dataset)
			{
				if(f.quantity==FieldQuantity::ApproximateAppearance)return {};
				if (d.valid)
					return {};
				d.valid = true;
				d.member = f.membership;
				d.calibration = f.calibration;
				d.unit = f.unit;
				d.quantity = f.quantity == FieldQuantity::Irradiance
								 ? ContributionQuantity::IrradianceRGB
								 : ContributionQuantity::RadianceRGB;
			}
	}
	else
		for (const auto &r : a.contributions)
			if (r.dataset == c.dataset)
			{
				if (d.valid)
					return {};
				d.valid = true;
				d.member = r.membership;
				d.calibration = r.calibration;
				d.unit = r.unit;
				d.quantity = r.quantity;
				d.domainSamples = r.sampleDomain;
			}
	return d;
}
bool Complete(const TransportMembership &m)
{
	return m.version == 2 && m.rootsComplete && m.reflectionOrderKnown && !m.roots.empty() &&
		   !m.reflectionOrders.empty() && m.colour != ContributionColour::Unknown &&
		   m.addition == CompositionRule::Permitted && m.directLocal != FieldMembership::Unknown &&
		   m.directSun != FieldMembership::Unknown && m.directSky != FieldMembership::Unknown &&
		   m.legacyAmbient != FieldMembership::Unknown && m.reflected != FieldMembership::Unknown &&
		   m.unresolved == FieldMembership::Excluded && !(m.uncertainty & AttributionRootsUnknown);
}
} // namespace
struct Prepared::Data
{
	ReconstructedLightingAsset asset;
	Disk::Digest metadata{};
	std::map<Id, std::vector<P::SampleRecord>> samples;
	std::map<Id, std::vector<P::ContributionRecord>> rows;
	using SourceKey = std::tuple<Id, Id, unsigned, unsigned>;
	std::map<SourceKey, std::vector<size_t>> index;
	std::vector<P::ObservationRecord> observations;
};
Prepared::Prepared(std::shared_ptr<const Data> d) : data_(std::move(d))
{
}
Error Prepare(const P::Artifact &a, const ReadContext &read, std::shared_ptr<const Prepared> &out,
			  PreparedWork *work)
{
	out.reset();
	if (work)
		*work = {};
	try
	{
		Check(P::ValidateArtifact(a, read, true));
		auto d = std::make_shared<Prepared::Data>();
		d->asset = *a.asset;
		Disk::Limits limit;
		limit.maxFileBytes = limit.maxMetadataBytes =
			read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024;
		auto hashed = Disk::MetadataDigest(d->asset, d->metadata, limit);
		Need(bool(hashed), Code::MemoryLimit, "Attribution metadata binding budget");
		PreparedWork w;
		w.bytes = sizeof(*d);
		uint64_t maximum = read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024;
		Need(w.bytes <= maximum, Code::MemoryLimit, "Prepared attribution metadata budget");
		auto account = [&](uint64_t n, uint64_t size) {
			Need(n <= (maximum - w.bytes) / size, Code::MemoryLimit,
				 "Prepared attribution memory budget");
			w.bytes += n * size;
		};
		for (const auto &dataset : d->asset.datasets)
		{
			if (read.cancellation && read.cancellation->IsCancelled())
				throw Failure(Code::Cancelled, "Attribution preparation cancelled");
			if (dataset.purpose == DatasetPurpose::SampleDomain)
			{
				account(dataset.elementCount, sizeof(P::SampleRecord));
				auto &rows = d->samples[dataset.id];
				Check(P::ReadSamples(a, dataset.id, read, rows));
				w.samples += rows.size();
			}
		}
		for (size_t i = 0; i < d->asset.contributions.size(); ++i)
		{
			const auto &ref = d->asset.contributions[i];
			account(std::find_if(d->asset.datasets.begin(), d->asset.datasets.end(),
								 [&](const Dataset &x) { return x.id == ref.dataset; })
						->elementCount,
					sizeof(P::ContributionRecord));
			auto &rows = d->rows[ref.dataset];
			Check(P::ReadContributions(a, ref.dataset, read, rows));
			w.records += rows.size();
			account(ref.sources.size(), 128);
			for (Id source : ref.sources)
				d->index[Prepared::Data::SourceKey(ref.sampleDomain, source,
												   unsigned(ref.component), unsigned(ref.quantity))]
					.push_back(i);
		}
		if (d->asset.assessment.diagnostics)
		{
			Check(P::ReadObservations(a, d->asset.assessment.diagnostics, read, d->observations));
			account(d->observations.size(), sizeof(P::ObservationRecord));
		}
		out = std::shared_ptr<const Prepared>(new Prepared(d));
		if (work)
			*work = w;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Prepared attribution allocation");
	}
}
Status Prepared::AcceptsAsset(const ReconstructedLightingAsset &a) const
{
	Disk::Digest digest;
	auto e = Disk::MetadataDigest(a, digest);
	return e && digest == data_->metadata ? Status::Ok : Status::Corrupt;
}
Status Prepared::Supports(Capability c) const
{
	return c == Capability::ContributionSampling && !data_->asset.contributions.empty()
			   ? Status::Ok
			   : Status::Unavailable;
}
Status Prepared::Contribution(const ReconstructedLightingAsset &, const ContributionQuery &q,
							  ContributionSample &output) const
{
	output = {};
	const ContributionReference *ref = nullptr;
	auto candidates = data_->index.find(
		Data::SourceKey(q.sampleDomain, q.source.id, unsigned(q.component), unsigned(q.quantity)));
	if (candidates == data_->index.end())
		return Status::Unavailable;
	for (size_t i : candidates->second)
	{
		const auto &r = data_->asset.contributions[i];
		if ((!q.dataset || q.dataset == r.dataset) &&
			(!q.configuration || q.configuration == r.configuration))
		{
			if (ref)
				return Status::Ambiguous;
			ref = &r;
		}
	}
	if (!ref)
		return Status::Unavailable;
	if (q.quantity == ContributionQuantity::AttributionFraction)
		return Status::Unsupported;
	const auto &samples = data_->samples.at(ref->sampleDomain);
	auto s = std::lower_bound(samples.begin(), samples.end(), q.sample,
							  [](const P::SampleRecord &r, Id id) { return r.id < id; });
	if (s == samples.end() || s->id != q.sample)
		return Status::NotFound;
	const auto &rows = data_->rows.at(ref->dataset);
	auto v = std::lower_bound(rows.begin(), rows.end(), q.sample,
							  [](const P::ContributionRecord &r, Id id) { return r.sample < id; });
	if (v == rows.end() || v->sample != q.sample || v->source != q.source.id || !v->available)
		return Status::Unavailable;
	if (ref->quantity == ContributionQuantity::LegacyRelativeChannels &&
		!q.allowLegacyRelativeChannels)
		return Status::Unsupported;
	if (ref->quantity != ContributionQuantity::LegacyRelativeChannels &&
		ref->membership.colour != ContributionColour::LinearRec709D65)
		return Status::Unsupported;
	output.value = v->value;
	output.lower = v->lower;
	output.upper = v->upper;
	output.issues = v->issues;
	output.quantity = ref->quantity;
	output.domain = ref->domain;
	output.configuration = ref->configuration;
	output.dataset = ref->dataset;
	if (ref->quantity != ContributionQuantity::LegacyRelativeChannels)
	{
		output.lighting.rgb = v->value;
		output.lighting.unit = ref->unit;
		output.lighting.calibration = ref->calibration;
		output.lighting.source = v->source;
		output.lightingSampleAvailable = true;
	}
	return Status::Ok;
}
Status Prepared::Audit(Id domain, Id sample, Ledger &out) const
{
	out = {};
	auto found =
		std::lower_bound(data_->observations.begin(), data_->observations.end(), sample,
						 [](const P::ObservationRecord &r, Id id) { return r.sample < id; });
	if (found == data_->observations.end() || found->sample != sample ||
		found->sampleDomain != domain)
		return Status::Unavailable;
	const auto &v = *found;
	out.sample = sample;
	out.sampleDomain = domain;
	out.configuration = data_->asset.assessment.selectedConfiguration;
	const auto &samples = data_->samples.at(domain);
	auto geometry = std::lower_bound(samples.begin(), samples.end(), sample,
									 [](const P::SampleRecord &r, Id id) { return r.id < id; });
	if (geometry == samples.end() || geometry->id != sample)
		return Status::Corrupt;
	out.style = geometry->style;
	out.observationDomain = v.domain;
	out.presentMask = v.presentMask;
	out.unboundedUpperMask = v.unboundedUpperMask;
	out.issues = v.issues;
	out.observationUnit = v.unit;
	out.observationCalibration = v.calibration;
	out.observedLower = v.observedLower;
	out.observedUpper = v.observedUpper;
	out.modelObservation = v.prediction;
	out.modelMinusObservedInterval = (V(v.signedIntervalDiscrepancy) * -1).Float();
	out.predictionAvailable = v.available;
	out.discrepancyAvailable = v.available && v.discrepancyKnown;
	out.direct = v.direct;
	out.sun = v.sun;
	out.sky = v.sky;
	out.ambient = v.ambient;
	out.indirect = v.indirect;
	out.unresolved = v.unresolved;
	out.preencodingTotal =
		(V(v.direct) + V(v.sun) + V(v.sky) + V(v.ambient) + V(v.indirect) + V(v.unresolved))
			.Float();
	out.componentTotalsAvailable = v.available && v.channel == Joint::Channel::Surface;
	out.attributionComplete = true;
	bool hasContributions = false;
	for (const auto &ref : data_->asset.contributions)
		if (ref.sampleDomain == domain && ref.configuration == out.configuration)
		{
			if (!hasContributions)
			{
				out.contributionDomain = ref.domain;
				out.contributionUnit = ref.unit;
				out.calibration = ref.calibration;
			}
			hasContributions = true;
			const auto &rows = data_->rows.at(ref.dataset);
			auto r = std::lower_bound(
				rows.begin(), rows.end(), sample,
				[](const P::ContributionRecord &x, Id id) { return x.sample < id; });
			out.attributionComplete &= r != rows.end() && r->sample == sample && r->available;
		}
	out.attributionComplete &= hasContributions;
	out.luminanceAvailable = v.available &&
							 (v.domain == P::ObservationDomain::LinearRec709Irradiance ||
							  v.domain == P::ObservationDomain::LinearRec709Radiance) &&
							 v.presentMask == 7 && !v.unboundedUpperMask;
	if (out.luminanceAvailable)
	{
		out.modelLuminance = Y(v.prediction);
		out.observedLuminanceLower = Y(v.observedLower);
		out.observedLuminanceUpper = Y(v.observedUpper);
		out.ratioAvailable = true;
		V ratio;
		for (unsigned c = 0; c < 3; ++c)
		{
			double lo = V(v.observedLower)[c];
			out.ratioAvailable &= lo > 0 && lo == V(v.observedUpper)[c];
			if (lo > 0)
				ratio[c] = V(v.prediction)[c] / lo;
		}
		if (out.ratioAvailable)
			out.modelObservedRatio = ratio.Float();
	}
	return Status::Ok;
}
Decision CanAdd(const ReconstructedLightingAsset &a, const Component &x, const Component &y)
{
	std::string problem;
	if (!Validate(a, &problem))
		return No(Status::Corrupt, "Invalid canonical metadata");
	for(const auto&f:a.transport)if(f.quantity==FieldQuantity::ApproximateAppearance&&((x.field&&f.dataset==x.dataset)||(y.field&&f.dataset==y.dataset)))return No(Status::Unsupported,"Approximate appearance is not additive physical energy");
	auto p = Describe(a, x), q = Describe(a, y);
	if (!p.valid || !q.valid)
		return No(Status::NotFound, "Component dataset absent/ambiguous");
	const auto &m = p.member;
	const auto &n = q.member;
	if (!Complete(m) || !Complete(n))
		return No(Status::Unsupported,
				  "Root/path/colour/include metadata is incomplete or not additive");
	if (m.configuration != n.configuration || m.alternativeGroup != n.alternativeGroup)
		return No(Status::Ambiguous,
				  "Mutually exclusive or unscoped configuration branches cannot be summed");
	if (m.generation != n.generation || m.geometry != n.geometry || m.profile != n.profile ||
		m.domain != n.domain || m.style != n.style || m.colour != n.colour ||
		p.quantity != q.quantity || p.calibration != q.calibration || p.unit != q.unit)
		return No(Status::Unsupported, "Incompatible quantity/calibration/domain/style/generation");
	if (p.domainSamples && q.domainSamples && p.domainSamples != q.domainSamples)
		return No(Status::Unsupported, "Sample coverage domains differ; explicit mapping required");
	if (m.colour == ContributionColour::EncodedSourceRGB ||
		p.quantity == ContributionQuantity::AttributionFraction)
		return No(Status::Unsupported, "Encoded/fraction components do not add as light energy");
	for (const auto &r : m.roots)
		for (const auto &s : n.roots)
			if (r.source == s.source && r.kind == s.kind)
				for (auto order : m.reflectionOrders)
					if (std::find(n.reflectionOrders.begin(), n.reflectionOrders.end(), order) !=
						n.reflectionOrders.end())
						return No(
							Status::Unsupported,
							"Overlapping source root and reflection order would be double counted");
	auto group = [&](Id id) {
		for (const auto &e : a.emitters)
			if (e.source.id == id)
				return e.source.hypothesisGroup;
		for (const auto &e : a.environments)
			if (e.source.id == id)
				return e.source.hypothesisGroup;
		return Id(0);
	};
	for (const auto &r : m.roots)
		for (const auto &s : n.roots)
			if (r.source != s.source && group(r.source) && group(r.source) == group(s.source))
				return No(Status::Ambiguous,
						  "Source roots are mutually exclusive shape/source alternatives");
	return No(Status::Ok, "Declared disjoint roots/path orders compose algebraically; physical "
						  "model truth is not certified");
}
Decision MayReplace(const ReconstructedLightingAsset &a, const Prepared &prepared,
					const ReplacementRequest &q)
{
	if (prepared.AcceptsAsset(a) != Status::Ok)
		return No(Status::Corrupt, "Prepared attribution belongs to another asset generation");
	if (!std::isfinite(q.maximumAbsoluteError) || q.maximumAbsoluteError < 0 ||
		!std::isfinite(q.maximumLeakageRisk) || q.maximumLeakageRisk < 0 ||
		q.maximumLeakageRisk > 1)
		return No(Status::InvalidArgument, "Invalid independent replacement error budget");
	bool ambiguous = false;
	const auto *ref = Select(a, q.contribution, ambiguous);
	if (ambiguous)
		return No(Status::Ambiguous, "Alternative contribution records require explicit selection");
	if (!ref)
		return No(Status::Unavailable, "No contribution reference");
	Decision d;
	d.missingProofs = ReplacementAllProofs & ~ref->replacement.supported;
	d.contradictedProofs = ref->replacement.contradicted;
	d.uncertainty = ref->membership.uncertainty;
	d.maximumAbsoluteError = ref->replacement.maximumAbsoluteError;
	d.leakageRisk = ref->replacement.leakageRisk;
	auto reject = [&](const char *why) {
		d.status = Status::Unsupported;
		d.reason = why;
		return d;
	};
	ContributionSample value;
	auto status = prepared.Contribution(a, q.contribution, value);
	if (status != Status::Ok)
	{
		d.status = status;
		d.reason = "Missing/unknown contribution support";
		return d;
	}
	if (ref->quantity != ContributionQuantity::IrradianceRGB || ref->unit != PhotometricUnit::Lux ||
		ref->component != ContributionComponent::Direct || !Complete(ref->membership) ||
		ref->membership.reflectionOrders != std::vector<uint32_t>{0} ||
		ref->membership.unresolved != FieldMembership::Excluded)
		return reject("Replacement requires supported physical direct irradiance, never encoded or "
					  "compiler-relative channels");
	if (ref->membership.replacement != CompositionRule::Permitted)
		return reject("Dataset forbids replacement or lacks explicit composition permission");
	if (ref->configuration != a.assessment.selectedConfiguration ||
		ref->membership.alternativeGroup)
		return reject("Unselected/exclusive configuration cannot authorize exact replacement");
	if (ref->domain != q.targetDomain || ref->calibration != q.calibration || ref->unit != q.unit ||
		ref->membership.colour != q.colour || q.colour != ContributionColour::LinearRec709D65 ||
		q.runtimeOperator.empty() || ref->replacement.runtimeOperator != q.runtimeOperator)
		return reject(
			"Observation and requested replacement operator are not demonstrably comparable");
	auto calibration = std::find_if(a.calibrations.begin(), a.calibrations.end(),
									[&](const Calibration &c) { return c.id == ref->calibration; });
	if (calibration == a.calibrations.end() ||
		calibration->kind != CalibrationKind::PhysicallyAuthored)
		return reject("Unknown absolute physical calibration");
	auto source = std::find_if(a.emitters.begin(), a.emitters.end(), [&](const PhysicalEmitter &e) {
		return e.source.id == q.contribution.source.id;
	});
	bool authored = false;
	if (source != a.emitters.end())
		for (const auto &p : a.provenanceRecords)
			if (p.id == source->source.provenance)
				authored = p.kind == ProvenanceKind::Authored;
	if (source == a.emitters.end() || source->source.state != HypothesisState::Resolved ||
		source->source.hypothesisGroup ||
		(!authored && source->source.evidence.exactLight != Suitability::Candidate) ||
		(source->kind != EmitterKind::Directional && !source->position.known))
		return reject("Source identity/type/position is unresolved or an exclusive alternative");
	if (!source->emission.intensity.known || !source->emission.color.known ||
		source->emission.calibration != ref->calibration ||
		source->emission.unit == PhotometricUnit::Relative)
		return reject("Actual source photometry/colour is missing or not physically calibrated");
	bool point = source->kind == EmitterKind::Point &&
				 source->angular.kind == AngularKind::Isotropic &&
				 q.runtimeOperator == "physical-point-irradiance/1" &&
				 (!source->geometry.effectiveRadiusWorld.known ||
				  source->geometry.effectiveRadiusWorld.value == 0);
	bool sun = source->kind == EmitterKind::Directional &&
			   source->angular.kind == AngularKind::Parallel && source->frame.known &&
			   q.runtimeOperator == "physical-parallel-sun-irradiance/1" &&
			   (!source->geometry.angularRadiusRadians.known ||
				source->geometry.angularRadiusRadians.value == 0);
	if (!point && !sun)
		return reject("No validated replacement-operator contract for this source geometry/extent");
	auto uncertainty =
		std::find_if(a.uncertainty.begin(), a.uncertainty.end(),
					 [&](const SourceUncertainty &u) { return u.source == source->source.id; });
	if (uncertainty == a.uncertainty.end() || !uncertainty->complete ||
		!uncertainty->absoluteScaleKnown ||
		(!authored && uncertainty->exactLight != Suitability::Candidate))
		return reject("Source uncertainty/calibration assessment is incomplete");
	if (uncertainty->configurations.size() > 1)
		return reject("Source allocation remains dependent on alternative configurations");
	for (unsigned c = 0; c <= unsigned(UncertaintyCause::Numerical); ++c)
		if (std::none_of(uncertainty->factors.begin(), uncertainty->factors.end(),
						 [&](const UncertaintyFactor &f) {
							 return f.cause == UncertaintyCause(c) &&
									f.status == UncertaintyStatus::Supported;
						 }))
			return reject("Required source evidence factor is not Supported");
	Ledger ledger;
	if (prepared.Audit(ref->sampleDomain, q.contribution.sample, ledger) != Status::Ok ||
		!ledger.predictionAvailable || !ledger.discrepancyAvailable ||
		!ledger.attributionComplete ||
		ledger.observationDomain != P::ObservationDomain::LinearRec709Irradiance ||
		ledger.observationUnit != ref->unit || ledger.observationCalibration != ref->calibration ||
		ledger.presentMask != 7 || ledger.unboundedUpperMask || ledger.style != 0)
		return reject("Receiver is missing, censored, encoded, or incompletely attributed");
	if (d.missingProofs || d.contradictedProofs || d.uncertainty || value.issues || ledger.issues)
		return reject("Missing/contradicted replacement evidence or geometric/model uncertainty");
	if (!d.maximumAbsoluteError.bounded || !d.leakageRisk.bounded ||
		d.maximumAbsoluteError.upper > q.maximumAbsoluteError ||
		d.leakageRisk.upper > q.maximumLeakageRisk)
		return reject("Expected replacement error or leakage lacks acceptable bounds");
	for (unsigned c = 0; c < 3; ++c)
		if (std::abs(V(ledger.modelMinusObservedInterval)[c]) > d.maximumAbsoluteError.upper)
			return reject(
				"Observed model discrepancy exceeds the declared replacement error bound");
	d.status = Status::Ok;
	d.reason =
		"Scoped physical receiver replacement may be attempted; no renderer operation performed";
	return d;
}
} // namespace Attribution
} // namespace Core
} // namespace Rend2LightRecon
