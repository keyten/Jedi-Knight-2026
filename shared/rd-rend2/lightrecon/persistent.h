// Explicit selective CPU preparation over the existing .jklr format.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cache.h"
#include "control.h"
#include "probes.h"
namespace Rend2LightRecon
{
namespace Disk
{
namespace Persistent
{
struct Item
{
	Id id = 0;		   // canonical field or visibility dataset ID
	Digest geometry{}; // expected immutable producer geometry generation
};
struct Selection
{
	std::vector<Item> fields, visibility;
	bool contributions = false;
	uint64_t maxPreparedBytes = 128ull << 20;
};
struct Work
{
	uint64_t fileBytes = 0, readBytes = 0, reads = 0, chunks = 0, payloadBytes = 0;
	uint64_t preparedBudgetBytes = 0; // conservative preparation reservation, not process RSS
	double openSeconds = 0, prepareSeconds = 0;
};
// All selected chunks are checked and decoded before return. The returned
// product owns only resident immutable CPU data; file leases close here.
// A failed selection never returns a partially installed evaluator.
Error Load(const Cache &, const Fingerprint &, const Selection &, BuildProduct &, Work &,
		   const Limits & = Limits());
// Validate numerical payloads using the same prepared codecs before durable
// publication. Opaque/unrequested data can be stored by Cache::Store separately,
// but are never advertised as prepared by this interface.
Error Store(const Cache &, const Fingerprint &, const ReconstructedLightingAsset &,
			const std::vector<Chunk> &, const Selection &, const Limits & = Limits());
// Geometry-only reuse. No photometric data or lighting capability is invented.
Error LoadProbes(const Cache &, const Fingerprint &, Id geometryDataset, Id topologyDataset,
				 const Digest &expectedGeometry, std::shared_ptr<const Core::Probes::Topology> &,
				 Work &, const Limits & = Limits());
} // namespace Persistent
} // namespace Disk
} // namespace Rend2LightRecon
