// Prepared read-only CPU field adapter. Topology never creates lighting values.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe_internal.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Probes
{
struct Prepared::Data
{
	std::shared_ptr<const Topology> topology;
	Disk::Digest asset{};
	TransportField field;
	std::vector<Value> values;
};
namespace
{
const Value *Find(const std::vector<Value> &v, Id id)
{
	auto i =
		std::lower_bound(v.begin(), v.end(), id, [](const Value &a, Id b) { return a.probe < b; });
	return i != v.end() && i->probe == id ? &*i : nullptr;
}
Status Sample(const Prepared::Data &d, Vec3 p, Vec3 direction, Id field, Angular::Quantity quantity,
			  LightingSample &out)
{
	if (field && field != d.field.source.id)
		return Status::NotFound;
	if (!field && d.field.source.state != HypothesisState::Resolved)
		return Status::Unavailable;
	if (!Finite(p) || !Normal(direction))
		return Status::InvalidArgument;
	// Canonical float Vec3 already passed its unit tolerance. Re-express only
	// that accepted direction as a double unit vector for P03's stricter basis.
	auto angularDirection = Angular::Unit(Angular::V{{direction.x, direction.y, direction.z}});
	bool irradiance = d.field.quantity == FieldQuantity::Irradiance;
	if (irradiance != (quantity == Angular::Quantity::DiffuseIrradiance))
		return Status::Unsupported;
	auto support = d.topology->Lookup(p);
	if (support.status != Status::Ok)
		return support.status;
	double sum[3] = {};
	for (unsigned j = 0; j < support.count; ++j)
	{
		auto value = Find(d.values, support.probes[j]);
		if (!value)
			return Status::Unavailable;
		const auto &f = value->angular;
		double y[81] = {};
		if (!f.sh.empty() &&
			Angular::BasisInto(angularDirection, f.degree, y, 81) != Angular::Code::Ok)
			return Status::Corrupt;
		double rgb[3] = {};
		for (size_t k = 0; k < f.sh.size(); ++k)
			for (unsigned c = 0; c < 3; ++c)
				rgb[c] += f.sh[k][c] * y[k];
		for (const auto &l : f.sg)
		{
			if (quantity != Angular::Quantity::IncomingRadiance)
				return Status::Unsupported;
			double w = Angular::SG(angularDirection, l.axis, l.sharpness);
			for (unsigned c = 0; c < 3; ++c)
				rgb[c] += l.energy[c] * w;
		}
		// No hidden positivity clamp or cancellation of negative SH ringing.
		for (unsigned c = 0; c < 3; ++c)
		{
			if (!std::isfinite(rgb[c]))
				return Status::Corrupt;
			if (rgb[c] < 0)
				return Status::Unsupported;
			sum[c] += support.weights[j] * rgb[c];
		}
	}
	out.source = d.field.source.id;
	out.unit = d.field.unit;
	out.calibration = d.field.calibration;
	out.rgb = Vec3(float(sum[0]), float(sum[1]), float(sum[2]));
	return Finite(out.rgb) ? Status::Ok : Status::Corrupt;
}
} // namespace
Prepared::Prepared(std::shared_ptr<const Data> d) : data_(std::move(d))
{
}
Status Prepared::AcceptsAsset(const ReconstructedLightingAsset &a) const
{
	Disk::Digest key;
	return Disk::MetadataDigest(a, key) && key == data_->asset ? Status::Ok : Status::Corrupt;
}
Status Prepared::Supports(Capability c) const
{
	if (data_->values.empty())
		return Status::Unavailable;
	if (c == Capability::IndirectSampling)
		return data_->field.quantity == FieldQuantity::Irradiance ? Status::Ok
																  : Status::Unsupported;
	if (c == Capability::IncomingRadianceSampling)
		return data_->field.quantity == FieldQuantity::Radiance ? Status::Ok : Status::Unsupported;
	return Status::Unavailable;
}
Status Prepared::Indirect(const ReconstructedLightingAsset &, const IrradianceQuery &q,
						  LightingSample &out) const
{
	return Sample(*data_, q.positionWorld, q.normalWorld, q.field.id,
				  Angular::Quantity::DiffuseIrradiance, out);
}
Status Prepared::IncomingRadiance(const ReconstructedLightingAsset &, const RadianceQuery &q,
								  LightingSample &out) const
{
	return Sample(*data_, q.positionWorld, q.directionToSource, q.field.id,
				  Angular::Quantity::IncomingRadiance, out);
}
Support Prepared::GeometrySupport(Vec3 p, const QueryPolicy &q) const
{
	return data_->topology->Lookup(p, q);
}
Error Prepare(std::shared_ptr<const Topology> t, const Values &v,
			  const ReconstructedLightingAsset &a, const ReadContext &read,
			  std::shared_ptr<const Prepared> &out)
{
	out.reset();
	try
	{
		if (Cancelled(read))
			return Failure(Code::Cancelled, "Probe values preparation cancelled");
		if (!t || v.topology != t->Key() || !v.field || v.records.size() > t->Probes().size())
			return Failure(Code::InvalidState, "Probe values/topology generation mismatch");
		auto d = std::make_shared<Prepared::Data>();
		d->topology = t;
		std::string why;
		if (!Validate(a, &why))
			return Failure(Code::InvalidInput, "Probe asset: " + why);
		auto hash = Disk::MetadataDigest(a, d->asset);
		if (!hash)
			return Failure(Code::InvalidInput, hash.message);
		auto f = std::find_if(a.transport.begin(), a.transport.end(),
							  [&](const TransportField &x) { return x.source.id == v.field; });
		if (f == a.transport.end() || !f->dataset || f->quantity==FieldQuantity::ApproximateAppearance ||
			f->membership.colour != ContributionColour::LinearRec709D65 ||
			f->membership.generation != Disk::Hex(t->Key()) ||
			f->membership.geometry != Disk::Hex(t->GeometryKey()) ||
			f->membership.alternativeGroup || f->source.hypothesisGroup)
			return Failure(Code::InvalidInput,
						   "Probe field needs selected, linear, generation-scoped membership");
		auto valid = ValidateData(*t->Storage(), read);
		if (!valid)
			return valid;
		uint64_t needed = sizeof(*d) + v.records.size() * sizeof(Value) +
						  t->Probes().size() * sizeof(const Probe *);
		for (const auto &x : v.records)
		{
			if (Angular::Validate(x.angular) != Angular::Code::Ok)
				return Failure(Code::InvalidInput, "Invalid angular field before copy");
			needed += x.angular.sh.size() * sizeof(Angular::V) +
					  x.angular.sg.size() * sizeof(Angular::Lobe);
		}
		if (needed > (read.maxBytes ? read.maxBytes : 64ull << 20))
			return Failure(Code::MemoryLimit, "Probe values copy budget");
		d->field = *f;
		d->values = v.records;
		std::sort(d->values.begin(), d->values.end(),
				  [](const Value &x, const Value &y) { return x.probe < y.probe; });
		std::vector<const Probe *> probeIndex;
		for (const auto &p : t->Probes())
			probeIndex.push_back(&p);
		std::sort(probeIndex.begin(), probeIndex.end(),
				  [](const Probe *x, const Probe *y) { return x->id < y->id; });
		uint64_t bytes = sizeof(*d) + d->values.size() * sizeof(Value) +
						 probeIndex.size() * sizeof(const Probe *);
		Id last = 0;
		for (auto &x : d->values)
		{
			if (Cancelled(read))
				return Failure(Code::Cancelled, "Probe values preparation cancelled");
			auto found = std::lower_bound(probeIndex.begin(), probeIndex.end(), x.probe,
										  [](const Probe *y, Id id) { return y->id < id; });
			const Probe *p =
				found != probeIndex.end() && (*found)->id == x.probe ? *found : nullptr;
			if (x.probe <= last || !p || x.evaluatedPositionWorld.x != p->positionWorld.x ||
				x.evaluatedPositionWorld.y != p->positionWorld.y ||
				x.evaluatedPositionWorld.z != p->positionWorld.z ||
				Angular::Validate(x.angular) != Angular::Code::Ok ||
				((f->quantity == FieldQuantity::Irradiance) !=
				 (x.angular.quantity == Angular::Quantity::DiffuseIrradiance)))
				return Failure(Code::InvalidInput,
							   "Stale position, missing/invalid angular values or wrong quantity");
			last = x.probe;
			bytes += x.angular.sh.size() * sizeof(Angular::V) +
					 x.angular.sg.size() * sizeof(Angular::Lobe);
			if (bytes > (read.maxBytes ? read.maxBytes : 64ull << 20))
				return Failure(Code::MemoryLimit, "Probe value memory budget");
		}
		out = std::make_shared<Prepared>(d);
		return {};
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Prepared probe values allocation");
	}
}
} // namespace Probes
} // namespace Core
} // namespace Rend2LightRecon
