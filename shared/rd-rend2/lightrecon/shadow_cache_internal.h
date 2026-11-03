// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_math.h"
#include "geometry_key.h"
#include "shadow_cache.h"
#include <algorithm>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace ShadowCache
{
using namespace Bake::Detail;
inline void Need(bool v, Code c, const char *s)
{
	if (!v)
		throw Failure(c, s);
}
inline void Check(Error e)
{
	if (!e)
		throw e;
}
inline const PhysicalEmitter *Emitter(const ReconstructedLightingAsset &a, Id id)
{
	for (const auto &e : a.emitters)
		if (e.source.id == id)
			return &e;
	return nullptr;
}
inline bool Same(Vec3 a, Vec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
inline Disk::Digest ItemKey(const Item &i, const Disk::Digest &policy, const Disk::Digest &source,
							const Disk::Digest &support)
{
	GeometryKey k;
	k.U(kVersion);
	k.Digest(policy);
	k.Digest(source);
	k.Digest(support);
	k.U(i.source);
	k.U(unsigned(i.kind));
	k.U(i.receiver.id);
	k.U(i.receiver.region);
	k.U(i.receiver.surface);
	k.V(i.receiver.positionWorld);
	k.U(i.receiver.normalKnown);
	k.V(i.receiver.geometricNormal);
	k.F(i.receiver.footprintWorld);
	k.V(i.targetOrDirection);
	return k.Finish();
}
inline bool RecordLess(const Record &a, const Record &b)
{
	return std::make_tuple(unsigned(a.item.kind), a.item.source, a.item.receiver.id) <
		   std::make_tuple(unsigned(b.item.kind), b.item.source, b.item.receiver.id);
}
inline Disk::Digest Content(const Descriptor &d, const std::vector<Record> &rows)
{
	GeometryKey k;
	k.U(kVersion);
	k.U(d.dataset);
	k.U(d.scenario);
	k.Digest(d.map);
	k.Digest(d.geometry);
	k.Digest(d.policy);
	k.U(unsigned(d.storage));
	k.U(d.areaDivisions);
	k.U(d.tubeSides);
	k.U(d.tubeSegments);
	k.U(unsigned(d.optical.facing));
	k.U(unsigned(d.optical.alphaTested));
	k.U(unsigned(d.optical.translucent));
	k.U(d.optical.acceptPatchApproximation);
	k.U(d.optical.requireKnownFreeOrigin);
	k.F(d.bias.absoluteWorld);
	k.F(d.bias.relative);
	k.F(d.bias.maximumWorld);
	k.F(d.skyDistanceWorld);
	k.F(d.maximumSamplingDifference);
	Text(k, d.scope);
	Text(k, d.occupancyProvenance);
	for (auto &r : rows)
	{
		k.Digest(r.key);
		const auto &v = r.result;
		for (auto e : {v.staticGeometry, v.scenarioGeometry, v.staticResponse, v.scenarioResponse})
		{
			k.U(unsigned(e.state));
			k.U(e.estimateKnown);
			k.F(e.value);
			k.F(e.lower);
			k.F(e.upper);
		}
		k.U(v.conditional);
		k.U(v.responseApplicable);
		k.U(v.runtimeUpdateRequired);
		k.U(v.occluderDistanceKnown);
		k.F(v.nearestStaticOccluderWorld);
		k.F(v.samplingDifference);
		k.F(v.quantizationBound);
		k.U(v.issues);
		k.U(v.rayIssues);
		k.U(v.coarseSamples);
		k.U(v.referenceSamples);
	}
	return k.Finish();
}
inline Disk::Digest Metadata(const ReconstructedLightingAsset &a, uint64_t maximum)
{
	Disk::Limits l;
	l.maxFileBytes = l.maxMetadataBytes = std::min<uint64_t>(32ull << 20, maximum / 4);
	Disk::Digest k;
	Need(bool(Disk::MetadataDigest(a, k, l)), Code::MemoryLimit, "Shadow metadata copy budget");
	return k;
}
inline bool Valid(const VisibilityEstimate &e)
{
	return unsigned(e.state) <= unsigned(ShadowState::Unknown) && std::isfinite(e.value) &&
		   std::isfinite(e.lower) && std::isfinite(e.upper) && e.lower >= 0 && e.upper <= 1 &&
		   e.lower <= e.upper && (!e.estimateKnown || (e.value >= e.lower && e.value <= e.upper)) &&
		   (e.estimateKnown ? e.state != ShadowState::Unknown : e.state == ShadowState::Unknown) &&
		   (e.state != ShadowState::Visible || e.value == 1) &&
		   (e.state != ShadowState::Blocked || e.value == 0);
}
Error Validate(const Artifact &, const ReadContext &);
} // namespace ShadowCache
} // namespace Core
} // namespace Rend2LightRecon
