// Producer/owner interface. Consumers include tr_lightrecon_api.h only.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tr_lightrecon_api.h"

namespace Rend2LightRecon {

// Optional immutable, prepared CPU evaluation boundary. Implementations must
// perform no I/O, reconstruction, lazy cache population, GPU allocation or renderer
// calls from these const methods. All data must be prepared before publication.
// No production evaluator is installed by this module. Defaults are Unavailable.
// Evaluation returns Ok, Unavailable, Unsupported, NotFound, Ambiguous or Corrupt;
// other statuses are invalid provider output. Supports returns only Ok,
// Unavailable or Unsupported. Successful RGB samples must identify their source.
class CpuEvaluator
{
public:
	virtual ~CpuEvaluator() = default;
	virtual Status Supports(Capability capability) const;
	virtual Status Indirect(const ReconstructedLightingAsset&, const IrradianceQuery&, LightingSample&) const;
	virtual Status IncomingRadiance(const ReconstructedLightingAsset&, const RadianceQuery&, LightingSample&) const;
	virtual Status RegionAt(const ReconstructedLightingAsset&, const Vec3&, Id&) const;
	virtual Status Visibility(const ReconstructedLightingAsset&, const VisibilityQuery&, VisibilitySample&) const;
	virtual Status SkyVisibility(const ReconstructedLightingAsset&, const SkyVisibilityQuery&, VisibilitySample&) const;
	virtual Status Contribution(const ReconstructedLightingAsset&, const ContributionQuery&, ContributionSample&) const;
};
enum class Completeness { Ready, Partial };
class Manager
{
public:
	Manager(); // Missing
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Snapshot Acquire() const; // O(1), safe concurrently with publication; never starts work
	// Deep copy once, then validate and index before atomic publication. This avoids
	// writable aliases into a moved-from builder's buffers. Invalid assets publish
	// Corrupt with a diagnostic, never stale current data. Old leases remain valid.
	Status Publish(const ReconstructedLightingAsset& asset, Completeness completeness = Completeness::Partial,
		std::shared_ptr<const CpuEvaluator> evaluator = std::shared_ptr<const CpuEvaluator>());
	// Only unavailable states allowed here; Ready/Partial require a validated asset.
	Status SetUnavailable(Availability state, const std::string& message = std::string());
private:
	// One owner serializes writes. Acquire/read methods support concurrent readers.
	std::shared_ptr<const detail::SnapshotData> current_;
};

} // namespace Rend2LightRecon
