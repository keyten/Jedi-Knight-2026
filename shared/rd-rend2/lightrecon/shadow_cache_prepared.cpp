// Immutable exact-support lookups. No rays, disk, lazy mutation or per-query allocation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shadow_cache_internal.h"
#include <map>
namespace Rend2LightRecon
{
namespace Core
{
namespace ShadowCache
{
struct Prepared::Data
{
	ReconstructedLightingAsset asset;
	Descriptor descriptor;
	std::vector<Record> records;
	std::vector<const Record *> coordinateIndex;
	std::vector<Coverage> sources, regions;
	Disk::Digest metadata{};
};
namespace
{
auto Coordinates(const Record &r)
	-> std::tuple<unsigned, Id, float, float, float, float, float, float>
{
	auto &i = r.item;
	auto a = i.receiver.positionWorld, b = i.targetOrDirection;
	return std::make_tuple(unsigned(i.kind), i.source, a.x, a.y, a.z, b.x, b.y, b.z);
}
bool Strict(const ShadowSupportSample &v, const VisibilityEstimate &e)
{
	if (v.staticGeometry.estimateKnown && v.staticGeometry.upper == 0 && e.estimateKnown &&
		e.upper == 0 && !(v.issues & (FiniteSampling | UnsupportedSource)))
		return true;
	if (v.runtimeUpdateRequired && !(v.staticGeometry.estimateKnown && v.staticGeometry.upper == 0))
		return false;
	return e.estimateKnown && e.lower == e.upper && (e.value == 0 || e.value == 1) &&
		   !v.conditional &&
		   !(v.issues & (SourceUncertain | FiniteSampling | UnsupportedSource | OpticalUncertain));
}
} // namespace
Prepared::Prepared(std::shared_ptr<const Data> d) : data_(std::move(d))
{
}
namespace
{
Status FindCoverage(const std::vector<Coverage> &v, Id id, Coverage &out)
{
	auto found = std::lower_bound(v.begin(), v.end(), id,
								  [](const Coverage &c, Id key) { return c.id < key; });
	if (found == v.end() || found->id != id)
		return Status::NotFound;
	out = *found;
	return Status::Ok;
}
} // namespace
Status Prepared::SourceCoverage(Id id, Coverage &out) const
{
	return FindCoverage(data_->sources, id, out);
}
Status Prepared::RegionCoverage(Id id, Coverage &out) const
{
	return FindCoverage(data_->regions, id, out);
}
Status Prepared::AcceptsAsset(const ReconstructedLightingAsset &a) const
{
	Disk::Digest k;
	return Disk::MetadataDigest(a, k) && k == data_->metadata ? Status::Ok : Status::Corrupt;
}
Status Prepared::Supports(Capability c) const
{
	if (data_->records.empty())
		return Status::Unavailable;
	if (c == Capability::StaticShadowSupport)
		return Status::Ok;
	for (auto &r : data_->records)
	{
		if (c == Capability::VisibilityQueries && r.item.kind == ShadowQueryKind::Segment &&
			Strict(r.result, r.result.scenarioGeometry))
			return Status::Ok;
		if (c == Capability::SkyVisibilityQueries &&
			(r.item.kind == ShadowQueryKind::SkyDirection ||
			 r.item.kind == ShadowQueryKind::Source) &&
			Strict(r.result, r.result.scenarioGeometry))
		{
			auto e = Emitter(data_->asset, r.item.source);
			if (r.item.kind == ShadowQueryKind::SkyDirection ||
				(e && e->kind == EmitterKind::Directional))
				return Status::Ok;
		}
	}
	return Status::Unavailable;
}
Status Prepared::ShadowSupport(const ReconstructedLightingAsset &, const ShadowSupportQuery &q,
							   ShadowSupportSample &out) const
{
	if (q.dataset != data_->descriptor.dataset)
		return Status::NotFound;
	if (q.scenario != data_->descriptor.scenario)
		return q.scenario ? Status::NotFound : Status::Ambiguous;
	Record key;
	key.item.kind = q.kind;
	key.item.source = q.source.id;
	key.item.receiver.id = q.receiver;
	auto found = std::lower_bound(data_->records.begin(), data_->records.end(), key, RecordLess);
	if (found == data_->records.end() || RecordLess(key, *found))
		return Status::Unavailable;
	if (!Same(found->item.receiver.positionWorld, q.receiverWorld) ||
		!Same(found->item.targetOrDirection, q.targetOrDirection))
		return Status::Unavailable;
	out = found->result;
	return Status::Ok;
}
Status Prepared::Visibility(const ReconstructedLightingAsset &, const VisibilityQuery &q,
							VisibilitySample &out) const
{
	Record key;
	key.item.kind = ShadowQueryKind::Segment;
	key.item.source = q.source.id;
	key.item.receiver.positionWorld = q.receiverWorld;
	key.item.targetOrDirection = q.targetWorld;
	auto coordinates = Coordinates(key);
	auto first = std::lower_bound(
		data_->coordinateIndex.begin(), data_->coordinateIndex.end(), coordinates,
		[](const Record *r, const decltype(coordinates) &k) { return Coordinates(*r) < k; });
	if (first == data_->coordinateIndex.end() || Coordinates(**first) != coordinates)
		return Status::Unavailable;
	const auto &v = (*first)->result;
	if (first + 1 != data_->coordinateIndex.end() && Coordinates(**(first + 1)) == coordinates)
		return Status::Ambiguous;
	if (!Strict(v, v.scenarioGeometry))
		return Status::Unavailable;
	out.transmittance = float(v.scenarioGeometry.value);
	return Status::Ok;
}
Status Prepared::SkyVisibility(const ReconstructedLightingAsset &, const SkyVisibilityQuery &q,
							   VisibilitySample &out) const
{
	Record key;
	key.item.kind = ShadowQueryKind::SkyDirection;
	key.item.source = q.source.id;
	key.item.receiver.positionWorld = q.positionWorld;
	key.item.targetOrDirection = q.directionToSky;
	auto coordinates = Coordinates(key);
	auto first = std::lower_bound(
		data_->coordinateIndex.begin(), data_->coordinateIndex.end(), coordinates,
		[](const Record *r, const decltype(coordinates) &k) { return Coordinates(*r) < k; });
	if (first == data_->coordinateIndex.end() || Coordinates(**first) != coordinates)
	{
		auto emitter = Emitter(data_->asset, q.source.id);
		if (!emitter || emitter->kind != EmitterKind::Directional ||
			!Same(q.directionToSky, (V(emitter->frame.forward) * -1).Float()))
			return Status::Unavailable;
		key.item.kind = ShadowQueryKind::Source;
		key.item.targetOrDirection = Vec3();
		coordinates = Coordinates(key);
		first = std::lower_bound(
			data_->coordinateIndex.begin(), data_->coordinateIndex.end(), coordinates,
			[](const Record *r, const decltype(coordinates) &k) { return Coordinates(*r) < k; });
		if (first == data_->coordinateIndex.end() || Coordinates(**first) != coordinates)
			return Status::Unavailable;
	}
	const auto &v = (*first)->result;
	if (first + 1 != data_->coordinateIndex.end() && Coordinates(**(first + 1)) == coordinates)
		return Status::Ambiguous;
	if (!Strict(v, v.scenarioGeometry))
		return Status::Unavailable;
	out.transmittance = float(v.scenarioGeometry.value);
	return Status::Ok;
}
Error Prepare(const Artifact &a, const ReadContext &read, std::shared_ptr<const Prepared> &out)
{
	out.reset();
	try
	{
		Check(Validate(a, read));
		auto d = std::make_shared<Prepared::Data>();
		d->metadata = Metadata(a.asset, read.maxBytes ? read.maxBytes : 128ull << 20);
		d->asset = a.asset;
		d->descriptor = a.descriptor;
		d->records = a.records;
		for (auto &r : d->records)
			d->coordinateIndex.push_back(&r);
		std::map<Id, Coverage> sources, regions;
		for (const auto &r : d->records)
		{
			for (auto c : {&sources[r.item.source], &regions[r.item.receiver.region]})
			{
				++c->records;
				c->known += r.result.staticGeometry.estimateKnown;
				c->unknown += !r.result.staticGeometry.estimateKnown;
				c->conditional += r.result.conditional;
				c->unsupported += (r.result.issues & UnsupportedSource) != 0;
			}
		}
		for (auto &c : sources)
		{
			c.second.id = c.first;
			d->sources.push_back(c.second);
		}
		for (auto &c : regions)
		{
			c.second.id = c.first;
			d->regions.push_back(c.second);
		}
		std::sort(
			d->coordinateIndex.begin(), d->coordinateIndex.end(),
			[](const Record *a, const Record *b) { return Coordinates(*a) < Coordinates(*b); });
		out = std::shared_ptr<const Prepared>(new Prepared(d));
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Prepared visibility allocation");
	}
}
} // namespace ShadowCache
} // namespace Core
} // namespace Rend2LightRecon
