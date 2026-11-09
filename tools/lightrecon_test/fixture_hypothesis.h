// ASR03 research adapter. Tool-only views of existing evidence, never canonical lights.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/evidence.h"
#include "lightrecon/geometry_key.h"
#include <algorithm>
#include <map>
#include <set>

namespace FixtureDiscovery
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
struct Membership
{
	std::string sourceKey; // explicit object/instance identity, NEVER proximity or colour
	std::vector<uint32_t> surfaces; // original BSP ordinals, including non-emitting housing
};
struct Region
{
	size_t candidate = 0, evidence = 0; // indices into the retained lease; no new evidence IDs
};
struct Fixture
{
	std::string id, sourceKey; // full diagnostic digest, NOT a canonical source/cache key
	std::vector<uint32_t> surfaces;
	std::vector<Region> regions;
	bool positionKnown = false, orientationKnown = false, colourKnown = false;
	bool staticEligible = false;
	Vec3 position, outward, colour;
	double sampledArea = 0;
	uint32_t issues = 0;
};
struct Views
{
	std::shared_ptr<const EmitterEvidenceSet> evidence; // owns all region references
	std::vector<Fixture> fixtures;
	size_t unassigned = 0, nonSurface = 0;
};
inline double Dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

// Small bounded diagnostic join. Membership is an input attestation; this does
// not discover object identity, complete hidden geometry, or certify placement.
// A region crossing membership boundaries fails atomically instead of being
// duplicated or assigned to whichever object happens to appear first.
inline Error Build(std::shared_ptr<const EmitterEvidenceSet> evidence,
				   const std::vector<Membership> &members, const ReadContext &ctx, Views &out)
{
	out = {};
	if (!evidence)
		return Failure(Code::InvalidInput, "Fixture evidence lease missing");
	if (ctx.cancellation && ctx.cancellation->IsCancelled())
		return Failure(Code::Cancelled, "Fixture join cancelled");
	// Fixed research limits bound work as well as allocations. Not a process RSS promise.
	if (members.size() > 4096 || evidence->Candidates().size() > 100000)
		return Failure(Code::WorkLimit, "Fixture join count limit");
	uint64_t bytes = sizeof(Views), references = 0;
	for (const auto &m : members)
	{
		if (m.sourceKey.empty() || m.sourceKey.size() > 4096 || m.surfaces.empty())
			return Failure(Code::InvalidInput, "Explicit fixture membership required");
		references += m.surfaces.size();
		bytes += sizeof(Fixture) + 128 + 2*m.sourceKey.size() + 128*m.surfaces.size();
	}
	bytes += evidence->Candidates().size() * sizeof(Region);
	if (references > 100000 || evidence->Evidence().size() > 200000)
		return Failure(Code::WorkLimit, "Fixture join reference limit");
	if (bytes > ctx.maxBytes)
		return Failure(Code::MemoryLimit, "Fixture join memory limit");
	Views result;
	result.evidence = evidence;
	std::map<uint32_t, size_t> owner;
	std::set<std::string> keys;
	for (const auto &m : members)
	{
		if (!keys.insert(m.sourceKey).second)
			return Failure(Code::InvalidInput, "Duplicate fixture identity");
		Fixture f;
		f.sourceKey = m.sourceKey;
		f.surfaces = m.surfaces;
		std::sort(f.surfaces.begin(), f.surfaces.end());
		GeometryKey key;
		key.U(3); // ASR03 tool experiment; no persisted schema/version changes
		key.Digest(evidence->Map().sha256);
		key.U(m.sourceKey.size());
		for (unsigned char c : m.sourceKey) key.U(c);
		key.U(f.surfaces.size());
		for (auto s : f.surfaces)
		{
			if (!owner.emplace(s, result.fixtures.size()).second)
				return Failure(Code::InvalidInput, "Overlapping fixture membership");
			key.U(s);
		}
		f.id = Disk::Hex(key.Finish());
		result.fixtures.push_back(std::move(f));
	}
	uint64_t work = 0;
	for (size_t ci = 0; ci < evidence->Candidates().size(); ++ci)
	{
		if (ctx.cancellation && ctx.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Fixture join cancelled");
		const auto &c = evidence->Candidates()[ci];
		size_t chosen = SIZE_MAX, fixture = SIZE_MAX;
		bool unmatched = false;
		for (auto ei : c.evidence)
		{
			if (++work > 1000000)
				return Failure(Code::WorkLimit, "Fixture association work limit");
			const auto &e = evidence->Evidence().at(ei);
			// Authored alternatives remain on the candidate; the visible-region
			// descriptor always selects original surface evidence, without overwriting it.
			if (e.origin != EvidenceOrigin::GlowSurface && e.origin != EvidenceOrigin::SurfaceLight)
				continue;
			if (e.surfaces.empty()) continue;
			if (chosen != SIZE_MAX)
			{
				const auto &previous = evidence->Evidence()[chosen];
				if (e.surfaces != previous.surfaces || e.positionKnown != previous.positionKnown ||
					e.orientationKnown != previous.orientationKnown ||
					e.position.x != previous.position.x || e.position.y != previous.position.y ||
					e.position.z != previous.position.z || e.outward.x != previous.outward.x ||
					e.outward.y != previous.outward.y || e.outward.z != previous.outward.z ||
					e.halfWidth != previous.halfWidth || e.halfHeight != previous.halfHeight ||
					e.areaWorld != previous.areaWorld)
					return Failure(Code::Unavailable, "Candidate has conflicting visible-region geometry");
				// Geometry aliases may carry different compiler colours. Prefer
				// original glow/mask colour, independent of hash ordering.
				if ((e.origin == EvidenceOrigin::GlowSurface && previous.origin != e.origin) ||
					(e.origin == previous.origin && e.id < previous.id)) chosen = ei;
			}
			else chosen = ei;
			for (auto s : e.surfaces)
			{
				if (++work > 1000000)
					return Failure(Code::WorkLimit, "Fixture association work limit");
				auto found = owner.find(s);
				if (found == owner.end()) { unmatched = true; continue; }
				if (fixture != SIZE_MAX && fixture != found->second)
					return Failure(Code::InvalidInput, "Region crosses fixture identities");
				fixture = found->second;
			}
		}
		if (chosen == SIZE_MAX) { ++result.nonSurface; continue; }
		if (fixture == SIZE_MAX) { ++result.unassigned; continue; }
		if (unmatched)
			return Failure(Code::InvalidInput, "Region only partly assigned to fixture");
		Region r; r.candidate = ci; r.evidence = chosen;
		result.fixtures[fixture].regions.push_back(r);
	}
	for (auto &f : result.fixtures)
	{
		f.positionKnown = f.orientationKnown = f.colourKnown = f.staticEligible = !f.regions.empty();
		double position[3] = {}, colour[3] = {};
		for (const auto &r : f.regions)
		{
			const auto &c = evidence->Candidates()[r.candidate];
			const auto &e = evidence->Evidence()[r.evidence];
			f.issues |= c.issues;
			f.staticEligible &= c.staticEligible;
			f.positionKnown &= e.positionKnown && e.areaWorld > 0;
			f.colourKnown &= e.colourKnown && e.areaWorld > 0;
			if (&r == &f.regions.front()) f.outward = e.outward;
			f.orientationKnown &= e.orientationKnown && Dot(f.outward, e.outward) > .9999;
			const float p[] = {e.position.x, e.position.y, e.position.z};
			const float rgb[] = {e.colour.x, e.colour.y, e.colour.z};
			for (unsigned k = 0; k < 3; ++k)
			{
				position[k] += e.areaWorld*p[k];
				colour[k] += e.areaWorld*rgb[k];
			}
			f.sampledArea += e.areaWorld;
		}
		if (f.positionKnown)
			f.position = Vec3(float(position[0]/f.sampledArea), float(position[1]/f.sampledArea), float(position[2]/f.sampledArea));
		if (f.colourKnown)
			f.colour = Vec3(float(colour[0]/f.sampledArea), float(colour[1]/f.sampledArea), float(colour[2]/f.sampledArea));
		if (!f.orientationKnown) f.outward = {};
	}
	std::sort(result.fixtures.begin(), result.fixtures.end(), [](const Fixture &a, const Fixture &b) { return a.id < b.id; });
	out = std::move(result);
	return {};
}
} // namespace FixtureDiscovery
