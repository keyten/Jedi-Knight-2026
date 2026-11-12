// Immutable prepared CPU field queries. No disk, rays, source fitting or GPU work.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "field_internal.h"
#include <algorithm>
#include <cmath>
namespace Rend2LightRecon
{
namespace Core
{
namespace Fields
{
struct Prepared::Data
{
	ReconstructedLightingAsset asset;
	Disk::Digest metadata{};
	std::shared_ptr<const Probes::Topology> topology;
	Probes::Values values;
	std::shared_ptr<const ConditionalFieldDescriptor> descriptor;
	TransportField field;
	bool reflectedSH = false;
};
bool SupportsReflectedIrradiance(const ReconstructedLightingAsset &asset, const TransportField &f)
{
	const auto &m = f.membership;
	const auto authored = [&](Id id) {
		return std::any_of(asset.provenanceRecords.begin(), asset.provenanceRecords.end(), [id](const Provenance &p) {
			return p.id == id && p.kind == ProvenanceKind::Authored && !p.observationIssues;
		});
	};
	const auto c = CalibrationAt(asset, f.calibration);
	// Replace the model's grid diffuse. Keep the renderer's independent direct
	// lights (including sun), so require explicit exclusion of every direct path.
	if (f.component != TransportComponent::IndirectIrradiance ||
		f.quantity != FieldQuantity::Irradiance || f.unit != PhotometricUnit::Lux ||
		f.source.state != HypothesisState::Resolved || f.source.hypothesisGroup ||
		f.source.evidence.observationIssues || !authored(f.source.provenance) ||
		!c || c->kind != CalibrationKind::PhysicallyAuthored || !authored(c->provenance) ||
		m.version != 2 || m.model != "source-conditioned-field/1" ||
		m.domain != "generated-probes/irradiance/1" || m.profile != "physical-emission/1" ||
		m.configuration || m.alternativeGroup || m.style || !m.rootsComplete || m.roots.empty() ||
		m.colour != ContributionColour::LinearRec709D65 || !m.numericalError.known ||
		(m.uncertainty & ~uint32_t(AttributionRepresentationError)) ||
		m.directLocal != FieldMembership::Excluded || m.directSun != FieldMembership::Excluded ||
		m.directSky != FieldMembership::Excluded || m.legacyAmbient != FieldMembership::Excluded ||
		m.unresolved != FieldMembership::Excluded || m.reflected != FieldMembership::Included ||
		!m.reflectionOrderKnown || m.minimumReflectionOrder == 0 || m.reflectionOrders.empty() ||
		m.reflectionOrders.front() == 0 || m.addition != CompositionRule::Permitted)
		return false;

	for (const auto &root : m.roots)
	{
		const Source *source = nullptr;
		const Photometry *emission = nullptr;
		if (root.kind == TransportRootKind::Sky)
		{
			for (const auto &sky : asset.environments)
				if (sky.source.id == root.source)
				{
					source = &sky.source;
					emission = &sky.meanLuminance;
				}
		}
		else if (root.kind == TransportRootKind::Sun || root.kind == TransportRootKind::LocalEmitter)
		{
			for (const auto &light : asset.emitters)
				if (light.source.id == root.source &&
					(light.kind == EmitterKind::Directional) == (root.kind == TransportRootKind::Sun))
				{
					const auto &sigma = light.position.sigmaWorld;
					if ((sigma.known && sigma.value > 0) || (sigma.bounded && sigma.upper > 0))
						return false;
					source = &light.source;
					emission = &light.emission;
				}
		}
		if (!source || !authored(source->provenance) || source->state != HypothesisState::Resolved ||
			source->hypothesisGroup || source->evidence.observationIssues ||
			emission->unit == PhotometricUnit::Relative || emission->calibration != c->id ||
			!emission->intensity.known || !emission->color.known ||
			(emission->intensity.bounded && emission->intensity.lower != emission->intensity.upper) ||
			std::any_of(asset.uncertainty.begin(), asset.uncertainty.end(),
				[&](const SourceUncertainty &u) { return u.source == root.source; }))
			return false;
	}
	return true;
}
namespace
{
bool Physical(const Prepared::Data &d)
{
	auto c = CalibrationAt(d.asset, d.descriptor->calibration);
	return d.descriptor->domain == FieldDomain::PhysicallyAuthored &&
		   !d.descriptor->alternativeGroup && d.field.source.state == HypothesisState::Resolved &&
		   c && c->kind == CalibrationKind::PhysicallyAuthored && d.descriptor->primariesKnown &&
		   d.descriptor->linearTransferKnown &&
		   d.descriptor->colour == ContributionColour::LinearRec709D65 &&
		   !(d.descriptor->uncertainty & ~uint32_t(AttributionRepresentationError));
}
Status Query(const Prepared::Data &d, const ConditionalFieldQuery &q, ConditionalFieldSample &out)
{
	if (!q.optIn || q.field.id != d.descriptor->field)
		return Status::InvalidArgument;
	if (q.quantity != d.descriptor->quantity)
		return Status::Unsupported;
	if (q.configuration != d.descriptor->configuration)
		return q.configuration ? Status::NotFound : Status::Ambiguous;
	if (!Normal(q.directionOrNormalWorld) || !Finite(q.positionWorld))
		return Status::InvalidArgument;
	Probes::QueryPolicy policy;
	policy.allowConditionalStatic = q.allowConditionalStaticGeometry;
	auto support = d.topology->Lookup(q.positionWorld, policy);
	if (support.status != Status::Ok)
		return support.status;
	V rgb;
	auto direction = Angular::Unit(AV(V(q.directionOrNormalWorld)));
	for (unsigned j = 0; j < support.count; ++j)
	{
		auto v = ValueAt(d.values, support.probes[j]);
		if (!v)
			return Status::Unavailable;
		double basis[81];
		Need(Angular::BasisInto(direction, v->angular.degree, basis, 81) == Angular::Code::Ok,
			 Code::AlgorithmError, "Prepared field basis");
		V value;
		for (size_t k = 0; k < v->angular.sh.size(); ++k)
			value = value + VV(v->angular.sh[k]) * basis[k];
		for (auto &l : v->angular.sg)
			value = value + VV(l.energy) * Angular::SG(direction, l.axis, l.sharpness);
		if (!Energy(value))
			return Finite(value) ? Status::Unsupported : Status::Corrupt;
		rgb = rgb + value * support.weights[j];
	}
	out.descriptor = d.descriptor;
	out.value = rgb.Float();
	if (!Energy(V(out.value)))
		return Status::Corrupt;
	out.lowerKnown = true;
	out.lower = Vec3();
	out.upperKnown = false;
	out.field = q.field.id;
	out.cell = support.cell;
	out.region = support.region;
	out.geometryIssues = support.issues;
	out.supportCount = support.count;
	for (unsigned j = 0; j < support.count; ++j)
	{
		out.support[j] = support.probes[j];
		out.weights[j] = support.weights[j];
	}
	out.outcome = d.descriptor->uncertainty || support.conditional || support.issues
					  ? FieldOutcome::Partial
					  : FieldOutcome::Complete;
	return Status::Ok;
}
} // namespace
Prepared::Prepared(std::shared_ptr<const Data> d) : data_(std::move(d))
{
}
Status Prepared::AcceptsAsset(const ReconstructedLightingAsset &a) const
{
	Disk::Digest key;
	return Disk::MetadataDigest(a, key) && key == data_->metadata ? Status::Ok : Status::Corrupt;
}
Status Prepared::Supports(Capability c) const
{
	if (data_->values.records.empty())
		return Status::Unavailable;
	if (c == Capability::ConditionalFieldSampling)
		return Status::Ok;
	if (c == Capability::ReflectedIrradianceSH)
		return data_->reflectedSH ? Status::Ok : Status::Unavailable;
	if (!Physical(*data_))
		return Status::Unavailable;
	if (c == Capability::IndirectSampling)
		return data_->descriptor->quantity == ConditionalQuantity::NormalIrradiance &&
					   data_->field.component == TransportComponent::IndirectIrradiance
				   ? Status::Ok
				   : Status::Unavailable;
	if (c == Capability::IncomingRadianceSampling)
		return data_->descriptor->quantity == ConditionalQuantity::IncomingRadiance
				   ? Status::Ok
				   : Status::Unavailable;
	return Status::Unavailable;
}
Status Prepared::ConditionalField(const ReconstructedLightingAsset &,
								  const ConditionalFieldQuery &q, ConditionalFieldSample &out) const
{
	try
	{
		return Query(*data_, q, out);
	}
	catch (...)
	{
		return Status::Corrupt;
	}
}
Status Prepared::Indirect(const ReconstructedLightingAsset &a, const IrradianceQuery &q,
						  LightingSample &out) const
{
	if (Supports(Capability::IndirectSampling) != Status::Ok)
		return Status::Unavailable;
	if (q.field.id && q.field.id != data_->field.source.id)
		return Status::NotFound;
	ConditionalFieldQuery x;
	x.optIn = true;
	x.field.id = data_->field.source.id;
	x.configuration = data_->descriptor->configuration;
	x.positionWorld = q.positionWorld;
	x.directionOrNormalWorld = q.normalWorld;
	x.quantity = ConditionalQuantity::NormalIrradiance;
	ConditionalFieldSample v;
	auto status = ConditionalField(a, x, v);
	if (status != Status::Ok)
		return status;
	out.source = x.field.id;
	out.rgb = v.value;
	out.unit = PhotometricUnit::Lux;
	out.calibration = data_->descriptor->calibration;
	return Status::Ok;
}
Status Prepared::IncomingRadiance(const ReconstructedLightingAsset &a, const RadianceQuery &q,
								  LightingSample &out) const
{
	if (Supports(Capability::IncomingRadianceSampling) != Status::Ok)
		return Status::Unavailable;
	if (q.field.id && q.field.id != data_->field.source.id)
		return Status::NotFound;
	ConditionalFieldQuery x;
	x.optIn = true;
	x.field.id = data_->field.source.id;
	x.configuration = data_->descriptor->configuration;
	x.positionWorld = q.positionWorld;
	x.directionOrNormalWorld = q.directionToSource;
	x.quantity = ConditionalQuantity::IncomingRadiance;
	ConditionalFieldSample v;
	auto status = ConditionalField(a, x, v);
	if (status != Status::Ok)
		return status;
	out.source = x.field.id;
	out.rgb = v.value;
	out.unit = PhotometricUnit::Nits;
	out.calibration = data_->descriptor->calibration;
	return Status::Ok;
}
Status Prepared::ReflectedIrradianceSH(const Vec3 &position, IrradianceSHSample &out) const
{
	if (!data_->reflectedSH)
		return Status::Unavailable;
	const auto support = data_->topology->Lookup(position);
	if (support.status != Status::Ok || !support.region || support.conditional ||
		(support.issues & ~uint32_t(Probes::ResolutionLimited)))
		return Status::Unavailable;

	V coefficients[9];
	for (unsigned j = 0; j < support.count; ++j)
	{
		const auto v = ValueAt(data_->values, support.probes[j]);
		if (!v)
			return Status::Unavailable;
		out.degree = std::max(out.degree, v->angular.degree);
		for (size_t k = 0; k < v->angular.sh.size(); ++k)
			coefficients[k] = coefficients[k] + VV(v->angular.sh[k]) * support.weights[j];
	}
	for (unsigned k = 0; k < 9; ++k)
		out.coefficients[k] = coefficients[k].Float();
	out.source = data_->field.source.id;
	out.calibration = data_->field.calibration;
	out.cell = support.cell;
	out.region = support.region;
	return Status::Ok;
}
Error Prepare(const Artifact &a, const ReadContext &read, std::shared_ptr<const Prepared> &out)
{
	out.reset();
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Field preparation cancelled");
		Need(a.sourceScene != Disk::Digest{} && a.sourceScene == SourceIdentity(a.asset),
			Code::InvalidState, "Field frozen source/configuration dependency mismatch");
		auto metadata = MetadataBinding(a.asset, read.maxBytes ? read.maxBytes : 128ull << 20);
		Need(a.topology && a.descriptor && a.values.topology == a.topology->Key() &&
				 a.descriptor->topology == Disk::Hex(a.topology->Key()) &&
				 a.descriptor->geometry == Disk::Hex(a.topology->GeometryKey()),
			 Code::InvalidState, "Field topology/generation mismatch");
		std::string why;
		auto assetValid = Validate(a.asset, &why);
		Need(assetValid, Code::InvalidInput, why.c_str());
		auto f = Field(a.asset, a.values.field);
		Need(f && f->dataset == a.descriptor->dataset &&
				 f->calibration == a.descriptor->calibration && f->unit == a.descriptor->unit &&
				 f->membership.configuration == a.descriptor->configuration &&
				 f->membership.alternativeGroup == a.descriptor->alternativeGroup &&
				 f->membership.profile == a.descriptor->profile &&
				 f->membership.colour == a.descriptor->colour,
			 Code::InvalidInput, "Field descriptor/canonical metadata mismatch");
		auto dataset = std::find_if(a.asset.datasets.begin(), a.asset.datasets.end(),
									[&](const Dataset &x) { return x.id == f->dataset; });
		Need(dataset != a.asset.datasets.end() && dataset->encoding == "org.openjk.spatial-field" &&
				 dataset->encodingVersion == kVersion &&
				 dataset->elementCount == a.values.records.size() + 1 &&
				 a.descriptor->requestedProbes == a.topology->Probes().size() &&
				 a.descriptor->availableProbes == a.values.records.size(),
			 Code::InvalidInput, "Field dataset coverage/encoding differs from prepared values");
		Need(MembershipKey(a.descriptor->membership) == MembershipKey(f->membership),
			 Code::InvalidInput, "Field root/path membership mismatch");
		auto calibration = CalibrationAt(a.asset, a.descriptor->calibration);
		Need(calibration, Code::InvalidInput, "Field calibration missing");
		Need((a.descriptor->domain == FieldDomain::PhysicallyAuthored &&
			  calibration->kind == CalibrationKind::PhysicallyAuthored) ||
				 (a.descriptor->domain == FieldDomain::SceneEquivalent &&
				  calibration->kind == CalibrationKind::LegacySceneEquivalent) ||
				 ((a.descriptor->domain == FieldDomain::SourceLinearRelative ||
				   a.descriptor->domain == FieldDomain::LegacyAppearance) &&
				  calibration->kind == CalibrationKind::UncalibratedRelative),
			 Code::InvalidInput, "Field domain/calibration mismatch");
		Need(a.descriptor->quantity == ConditionalQuantity::LegacyAppearance
				 ? f->quantity == FieldQuantity::ApproximateAppearance
				 : a.descriptor->quantity == ConditionalQuantity::IncomingRadiance
					   ? f->quantity == FieldQuantity::Radiance
					   : f->quantity == FieldQuantity::Irradiance,
			 Code::InvalidInput, "Field angular quantity mismatch");
		Need(f->membership.generation == Disk::Hex(SceneKey(a.asset)), Code::InvalidState,
			 "Field source/calibration/configuration generation is stale");
		auto d = std::make_shared<Prepared::Data>();
		d->asset = a.asset;
		d->field = *f;
		d->topology = a.topology;
		d->descriptor = std::make_shared<const ConditionalFieldDescriptor>(*a.descriptor);
		d->reflectedSH = Physical(*d) &&
			d->descriptor->quantity == ConditionalQuantity::NormalIrradiance &&
			d->descriptor->representationError.known && !a.asset.regions.empty() &&
			SupportsReflectedIrradiance(a.asset, *f);
		uint64_t bytes = sizeof(*d);
		Id last = 0;
		std::vector<const Probes::Probe *> probeIndex;
		for (const auto &probe : a.topology->Probes())
			probeIndex.push_back(&probe);
		std::sort(probeIndex.begin(), probeIndex.end(),
				  [](const Probes::Probe *x, const Probes::Probe *y) { return x->id < y->id; });
		for (auto &v : a.values.records)
		{
			Need(v.probe > last && Angular::Validate(v.angular) == Angular::Code::Ok,
				 Code::InvalidInput, "Field stable value IDs/encoding");
			last = v.probe;
			auto found = std::lower_bound(probeIndex.begin(), probeIndex.end(), v.probe,
										  [](const Probes::Probe *x, Id id) { return x->id < id; });
			const Probes::Probe *p =
				found != probeIndex.end() && (*found)->id == v.probe ? *found : nullptr;
			Need(p != nullptr && p->positionWorld.x == v.evaluatedPositionWorld.x &&
					 p->positionWorld.y == v.evaluatedPositionWorld.y &&
					 p->positionWorld.z == v.evaluatedPositionWorld.z,
				 Code::InvalidState, "Values were not computed at final probe positions");
			Need((a.descriptor->quantity == ConditionalQuantity::IncomingRadiance) ==
					 (v.angular.quantity == Angular::Quantity::IncomingRadiance),
				 Code::InvalidInput, "Radiance/irradiance codec mismatch");
			if (!v.angular.present || v.angular.degree > 2 || !v.angular.sg.empty())
				d->reflectedSH = false;
			bytes += sizeof(v) + v.angular.sh.size() * sizeof(Angular::V) +
					 v.angular.sg.size() * sizeof(Angular::Lobe);
		}
		Need(bytes <= (read.maxBytes ? read.maxBytes : 128ull << 20), Code::MemoryLimit,
			 "Prepared field budget");
		d->values = a.values;
		d->metadata = metadata;
		out = std::shared_ptr<const Prepared>(new Prepared(d));
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Prepared field allocation");
	}
}
} // namespace Fields
} // namespace Core
} // namespace Rend2LightRecon
