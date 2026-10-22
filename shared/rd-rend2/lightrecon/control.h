// CPU policy only. Renderer cvars and feature adapters live outside the core.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_manager.h"
#include <array>

namespace Rend2LightRecon {

enum class BuildPolicy { Disabled, CacheOnly, CacheOrBuild, ForceRebuild };

// Called only by the owner at a map/input-revision load boundary. No consumer
// query may call this interface. A cache backend must check input revision,
// calibration and algorithm compatibility, not just the asset's schema version.
// LoadCache must never reconstruct on a miss. The backend owns its immutable
// input/revision context; ApplyBuildPolicy does not infer it from renderer globals.
// Ok requires a product; supported failures: Missing, Unavailable, Unsupported,
// Corrupt. Exceptions are contained as Corrupt. No automatic cache write/retry.
struct BuildProduct
{
	std::shared_ptr<const ReconstructedLightingAsset> asset;
	std::shared_ptr<const CpuEvaluator> evaluator;
	Completeness completeness = Completeness::Partial;
	std::string message;
};
class BuildBackend
{
public:
	virtual ~BuildBackend() = default;
	virtual Status LoadCache(BuildProduct&) { return Status::Unavailable; }
	virtual Status Build(BuildProduct&) { return Status::Unavailable; }
};
struct BuildReport
{
	BuildPolicy policy = BuildPolicy::Disabled;
	Status status = Status::Unavailable;
	bool cacheCalled = false, buildCalled = false;
	Status cacheStatus = Status::Unavailable, buildStatus = Status::Unavailable;
	std::string message;
};
// 0: no calls, Disabled. 1: cache only, never Build. 2: cache then Build on
// any supported cache failure (including invalid/stale cache). 3: Build only.
// Failure clears the current publication; existing leases remain immutable.
// A valid empty product is a loaded asset, not evidence of usable lighting.
BuildReport ApplyBuildPolicy(BuildPolicy policy, Manager& owner, BuildBackend* backend = nullptr);

enum class Consumer { Fog, Entities, Specular, Shadows, Sun, Particles, Indirect,
	WorldDiffuse, Area, Environment, Count };
constexpr size_t kConsumerCount = static_cast<size_t>(Consumer::Count);
struct UseSettings
{
	bool master = false;
	std::array<bool, kConsumerCount> requested{};
};
// The adapter must report true only when its canonical-asset implementation AND
// all required technology/permutations/resources are actually ready. This never
// enables physical PBR, Forward+, fog, LTC or IBL. No adapter is installed yet.
struct FeatureSupport
{
	bool available = false;
};
enum class GateReason { Enabled, InvalidConsumer, NotRequested, MasterDisabled,
	AssetUnavailable, CapabilityUnavailable, FeatureUnavailable };
struct ConsumerState
{
	bool requested = false, master = false, assetLoaded = false;
	bool capabilityPresent = false, featureAvailable = false, effective = false;
	Status assetStatus = Status::Unavailable, capabilityStatus = Status::Unavailable;
	GateReason reason = GateReason::InvalidConsumer;
};
// Sole effective-state rule. Read-only, no allocation, I/O, work scheduling or
// publication. Capability checks describe minimum data, not integration support.
// Callers must retain/use this same snapshot while consuming the result.
ConsumerState EvaluateConsumer(const Snapshot&, const UseSettings&, Consumer, FeatureSupport);
const char* ConsumerCvar(Consumer);
const char* GateReasonName(GateReason);
const char* StatusName(Status);
const char* AvailabilityName(Availability);

} // namespace Rend2LightRecon
