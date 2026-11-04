// Optional solver receipts and explicitly requested diagnostic forward evaluation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_math.h"
#include "contributions.h"
#include "diagnostics.h"
#include "geometry_key.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Diagnostics
{
PlacementCapture::PlacementCapture(Scene &s, std::vector<std::string> p, size_t maximum, Limits l)
	: scene_(s), builder_(s, l), profiles_(std::move(p)), maximum_(maximum)
{
}
void PlacementCapture::Placement(const Joint::Source &s, const PointRefine::PlacementResult &p,
								 const char *predicate, Vec3 from, Vec3 to, double radius,
								 size_t profile) noexcept
{
	try
	{
		GeometryKey k;
		k.U(s.candidate.id);
		k.U(profile);
		k.U(unsigned(p.state));
		k.V(from);
		k.V(to);
		k.F(radius);
		k.U(p.region);
		k.U(p.dynamicBoundary);
		k.U(p.dynamicExtentUnknown);
		k.U(unsigned(s.value.geometry.kind));
		k.V(s.value.geometry.frame.right);
		k.V(s.value.geometry.frame.up);
		k.V(s.value.geometry.frame.forward);
		k.F(s.value.geometry.geometry.halfWidthWorld);
		k.F(s.value.geometry.geometry.halfHeightWorld);
		k.F(s.value.geometry.geometry.halfLengthWorld);
		k.F(s.value.geometry.geometry.radiusWorld);
		k.Digest(SourceSupport::Key(s.value.support));
		for (auto c : p.assumptions)
			k.U(uint8_t(c));
		for (auto &w : p.occupancy)
		{
			k.U(w.boundary);
			k.U(w.state);
			k.U(w.model);
			k.U(unsigned(w.reason));
			k.U(w.runtimeExtentUnknown);
			k.B(w.scope);
		}
		for (auto c : std::string(predicate))
			k.U(uint8_t(c));
		auto key = k.Finish();
		if (seen_.count(key))
			return;
		if (stored_ >= maximum_)
		{
			++scene_.omitted;
			scene_.complete = false;
			return;
		}
		seen_.insert(key);
		++stored_;
		auto name = profile < profiles_.size() ? profiles_[profile] : "unsupported-profile-index";
		auto state = PointRefine::Classify(p);
		State display = state == PointRefine::Feasibility::Collision
							? State::Rejected
							: state == PointRefine::Feasibility::Proven
								  ? State::Accepted
								  : state == PointRefine::Feasibility::Conditional
										? State::Conditional
										: State::Unknown;
		auto emitter = s.value.geometry;
		emitter.source.id = s.candidate.id;
		builder_.Emitter(
			emitter, display == State::Accepted ? State::Conditional : display, name,
			std::string("candidate; actual Joint::Place receipt (not source acceptance):") +
				predicate);
		builder_.Placement(s, p, predicate, from, to, radius, name);
	}
	catch (...)
	{
		++scene_.omitted;
		scene_.complete = false;
	}
}
Error TrainingPairs(const Joint::Request &q, const std::vector<Joint::Candidate> &candidates,
					size_t maxSources, size_t maxRows, const ReadContext &read, Scene &scene)
{
	if (!q.training || q.profiles.empty() || maxSources > 16 || maxRows > 128)
		return Failure(Code::InvalidInput, "Diagnostic pair request bounds/input");
	Builder builder(scene);
	size_t sources = 0;
	for (const auto &c : candidates)
	{
		if (c.root != TransportRootKind::LocalEmitter)
			continue;
		if (sources++ >= maxSources)
			break;
		for (size_t profile = 0; profile < q.profiles.size(); ++profile)
		{
			for (size_t row = 0; row < std::min<uint64_t>(maxRows, q.count); ++row)
			{
				if (read.cancellation && read.cancellation->IsCancelled())
					return Failure(Code::Cancelled, "Diagnostic pairs cancelled");
				Chunk<Joint::Observation> chunk;
				auto e = q.training->Read(row, 1, read, chunk);
				if (!e)
					return e;
				if (chunk.count != 1 || !chunk.data)
					return Failure(Code::ProviderError, "Diagnostic training reader span");
				const auto &o = chunk.data[0];
				if (o.role != Joint::Role::Training)
					return Failure(
						Code::InvalidInput,
						"Diagnostic fitting-side export refuses validation/heldout targets");
				Record r;
				r.id = o.id;
				r.source = c.id;
				r.sample = o.id;
				r.region = o.region;
				r.layer = Layer::Direct;
				r.state = State::Unavailable;
				r.a = o.receiver.world;
				r.geometryKnown = true;
				r.profile = q.profiles[profile].model.profile.id;
				auto output = q.profiles[profile].model.output;
				r.quantity = output == Shape::Output::EncodedRgb8
								 ? "candidate-prequantization-encoded-rgb"
								 : output == Shape::Output::PhysicalLux
									   ? "physical-incident-irradiance"
									   : "compiler-relative-incident";
				r.domain = output == Shape::Output::EncodedRgb8
							   ? "declared-historical-encoded-observation"
							   : output == Shape::Output::PhysicalLux
									 ? "physical-lux"
									 : "compiler-relative-incident";
				r.unit = output == Shape::Output::PhysicalLux ? PhotometricUnit::Lux
															  : PhotometricUnit::Relative;
				r.reason = "diagnostic candidate amplitude prior, NOT a recovered/accepted source";
				r.provenance = c.provenance;
				if (o.channel != Joint::Channel::Surface)
				{
					r.reason += ";observation operator unavailable";
					builder.Add(r);
					continue;
				}
				if (q.profiles[profile].fixedGain != 1)
				{
					r.reason += ";diagnostic calibration recipe unavailable";
					builder.Add(r);
					continue;
				}
				std::vector<Bake::Receiver> receivers{o.receiver};
				auto owned = std::make_shared<const std::vector<Bake::Receiver>>(receivers);
				std::shared_ptr<const Shape::Block> block;
				auto sampling = q.strictSampling;
				sampling.maxRays = std::min<uint64_t>(sampling.maxRays, 256);
				sampling.maxWork = std::min<uint64_t>(sampling.maxWork, 100000);
				e = Shape::Prepare(q.profiles[profile].model, {c.shape.initial},
								   VectorChunk<Bake::Receiver>(owned, 0, 1), q.geometry, sampling,
								   read, block);
				if (!e)
				{
					r.reason += ";" + e.message;
					builder.Add(r);
					++scene.unknownQueries;
					continue;
				}
				Shape::Work work;
				std::vector<Shape::Prediction> values;
				e = block->Evaluate({c.shape.initial.rgbAmplitude}, read, values, work);
				scene.rays += block->PreparationWork().rays + work.rays;
				if (!e || values.size() != 1 || !values[0].available)
				{
					r.reason += ";prediction unavailable";
					builder.Add(r);
					++scene.unknownQueries;
					continue;
				}
				r.state = State::Conditional;
				r.valueKnown = true;
				r.value = values[0].value;
				r.issues = values[0].visibilityIssues;
				builder.Add(r);
				// Isolated-source encoded diagnostic, using the SAME production
				// transfer as Joint. This is not additive after shared peak/gamma.
				Vec3 encoded = values[0].value;
				bool encodedDomain = output == Shape::Output::EncodedRgb8;
				if (output == Shape::Output::RelativeIncident &&
					q.profiles[profile].contract.operation ==
						Indirect::ObservationOperator::NetRadiantEncoded)
				{
					uint8_t packed[3];
					auto transfer = Bake::Encode(q.profiles[profile].model.profile, values[0].value,
												 encoded, packed);
					encodedDomain = bool(transfer);
					auto isolated = r;
					isolated.quantity = "isolated-candidate-prequantization-encoded-rgb";
					isolated.domain = "declared-historical-encoded-observation";
					isolated.value = encoded;
					isolated.valueKnown = encodedDomain;
					isolated.state = encodedDomain ? State::Conditional : State::Unavailable;
					isolated.reason = "Isolated candidate prior only; ambient/GI/other sources "
									  "omitted; NOT a joint total or additive encoded contribution";
					if (!transfer)
						isolated.reason += ";" + transfer.message;
					builder.Add(isolated);
				}
				// The Shape historical path outputs final encoded-domain values.
				// Label that domain explicitly; do not subtract these from physical E.
				// Material/transfer/target agreement is checked by the training contract.
				if (encodedDomain)
				{
					Vec3 lo(float(o.rgb[0].lower), float(o.rgb[1].lower), float(o.rgb[2].lower));
					Vec3 hi(float(o.rgb[0].censored ? o.rgb[0].lower : o.rgb[0].upper),
							float(o.rgb[1].censored ? o.rgb[1].lower : o.rgb[1].upper),
							float(o.rgb[2].censored ? o.rgb[2].lower : o.rgb[2].upper));
					uint8_t present = 0, unbounded = 0;
					for (unsigned j = 0; j < 3; ++j)
					{
						if (o.rgb[j].present)
							present |= 1 << j;
						if (o.rgb[j].censored)
							unbounded |= 1 << j;
					}
					auto residual = Compare(o.id, o.receiver.world, encoded, lo, hi, present,
											unbounded, "declared-historical-encoded-observation",
											PhotometricUnit::Relative, o.comparable);
					residual.source = c.id;
					residual.profile = r.profile;
					residual.reason += "; isolated candidate prior versus observed TOTAL, not "
									   "final joint prediction";
					builder.Add(residual);
				}
			}
		}
	}
	return {};
}
Error FrozenObservations(const Producer::Artifact &a, const ReadContext &context, Scene &scene,
						 size_t maximum)
{
	if (!a.asset || maximum > 4096)
		return Failure(Code::InvalidInput, "Frozen observation diagnostic bounds/asset");
	if (!a.asset->assessment.diagnostics)
		return Failure(Code::Unavailable, "No frozen observation diagnostic dataset");
	ReadContext read = context;
	read.maxBytes =
		context.maxBytes ? std::min<uint64_t>(context.maxBytes, 16ull << 20) : 16ull << 20;
	auto valid = Producer::ValidateArtifact(a, read, true);
	if (!valid)
		return valid;
	std::vector<Producer::ObservationRecord> observations;
	auto e = Producer::ReadObservations(a, a.asset->assessment.diagnostics, read, observations);
	if (!e)
		return e;
	Builder builder(scene);
	Id lastDomain = 0;
	std::vector<Producer::SampleRecord> samples;
	for (size_t i = 0; i < std::min(maximum, observations.size()); ++i)
	{
		const auto &v = observations[i];
		if (v.sampleDomain != lastDomain)
		{
			e = Producer::ReadSamples(a, v.sampleDomain, read, samples);
			if (!e)
				return e;
			lastDomain = v.sampleDomain;
		}
		Record r;
		r.id = r.sample = v.sample;
		r.configuration = a.asset->assessment.selectedConfiguration;
		r.profile = a.asset->assessment.selectedProfile;
		r.issues = v.issues;
		r.unit = v.unit;
		r.domain = v.domain == Producer::ObservationDomain::EncodedSourceRGB
					   ? "encoded-source-rgb"
					   : v.domain == Producer::ObservationDomain::LinearRec709Irradiance
							 ? "linear-Rec709-irradiance"
							 : v.domain == Producer::ObservationDomain::LinearRec709Radiance
								   ? "linear-Rec709-radiance"
								   : v.domain == Producer::ObservationDomain::LegacyLinearChannels
										 ? "legacy-linear-channels"
										 : v.domain == Producer::ObservationDomain::Direction
											   ? "direction"
											   : "unknown";
		r.reason = "Frozen S13 observation operator; role=" + std::to_string(unsigned(v.role)) +
				   "; no post-encoding component arithmetic";
		for (auto &s : samples)
			if (s.id == v.sample)
			{
				r.geometryKnown = true;
				r.a = s.world;
				r.region = s.region;
				break;
			}
		r.layer = Layer::Direct;
		r.quantity = "frozen-model-total-in-observation-domain";
		r.valueKnown = v.available;
		r.value = v.prediction;
		r.state = v.available ? State::Conditional : State::Unavailable;
		builder.Add(r);
		r.layer = Layer::Receivers;
		r.quantity = "observed-interval-lower";
		r.valueKnown = v.presentMask == 7;
		r.value = r.lower = v.observedLower;
		r.lowerKnown = r.valueKnown;
		r.upper = v.observedUpper;
		r.upperKnown = r.valueKnown && !v.unboundedUpperMask;
		r.reason += ";unbounded-upper-mask=" + std::to_string(v.unboundedUpperMask);
		builder.Add(r);
		auto discrepancy = Compare(v.sample, r.a, v.prediction, v.observedLower, v.observedUpper,
								   v.presentMask, v.unboundedUpperMask, r.domain, v.unit,
								   v.available && v.discrepancyKnown &&
									   v.domain != Producer::ObservationDomain::Unknown &&
									   v.domain != Producer::ObservationDomain::Direction);
		discrepancy.geometryKnown = r.geometryKnown;
		discrepancy.configuration = r.configuration;
		discrepancy.profile = r.profile;
		discrepancy.region = r.region;
		discrepancy.reason += "; frozen model minus observed interval (S13 stored "
							  "target-minus-model has opposite sign)";
		builder.Add(discrepancy);
	}
	if (observations.size() > maximum)
	{
		scene.omitted += observations.size() - maximum;
		scene.complete = false;
	}
	return {};
}
Error Contributions(const Producer::Artifact &a, const ReadContext &context, Scene &scene,
					size_t maximum)
{
	if (maximum > 4096 || !a.asset)
		return Failure(Code::InvalidInput, "Contribution diagnostic budget/asset");
	ReadContext read = context;
	read.maxBytes =
		context.maxBytes ? std::min<uint64_t>(context.maxBytes, 16ull << 20) : 16ull << 20;
	std::shared_ptr<const Attribution::Prepared> prepared;
	auto valid = Attribution::Prepare(a, read, prepared);
	if (!valid)
		return valid;
	Builder builder(scene);
	size_t rows = 0;
	for (const auto &ref : a.asset->contributions)
	{
		std::vector<Producer::SampleRecord> samples;
		auto e = Producer::ReadSamples(a, ref.sampleDomain, read, samples);
		if (!e)
			return e;
		std::vector<Producer::ContributionRecord> values;
		e = Producer::ReadContributions(a, ref.dataset, read, values);
		if (!e)
			return e;
		for (const auto &value : values)
		{
			if (rows++ >= maximum)
			{
				++scene.omitted;
				scene.complete = false;
				return {};
			}
			Record r;
			r.id = value.sample;
			r.source = value.source;
			r.sample = value.sample;
			r.configuration = ref.configuration;
			r.alternative = ref.membership.alternativeGroup;
			r.layer =
				ref.component == ContributionComponent::Indirect
					? Layer::Indirect
					: ref.component == ContributionComponent::Unresolved
						  ? Layer::Unresolved
						  : value.root == TransportRootKind::Sun
								? Layer::Sun
								: value.root == TransportRootKind::Sky ? Layer::Sky : Layer::Direct;
			r.state = value.available ? State::Conditional : State::Unavailable;
			r.valueKnown = value.available;
			r.value = value.value;
			r.lowerKnown = r.upperKnown = value.available;
			r.lower = value.lower;
			r.upper = value.upper;
			r.unit = ref.unit;
			r.domain = ref.domain;
			r.quantity = "contribution-quantity:" + std::to_string(unsigned(ref.quantity));
			r.issues = value.issues;
			r.reason =
				"source-root=" + std::to_string(value.source) +
				" reflection-order=" + std::to_string(value.reflectionOrder) +
				"; exact replacement requires independent P06 proof, never encoded subtraction";
			for (auto &s : samples)
				if (s.id == value.sample)
				{
					r.geometryKnown = true;
					r.a = s.world;
					r.region = s.region;
					break;
				}
			builder.Add(r);
		}
	}
	return {};
}
} // namespace Diagnostics
} // namespace Core
} // namespace Rend2LightRecon
