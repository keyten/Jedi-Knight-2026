// Map owner, synchronous CPU loading only. No world_t, hunk or GPU dependencies.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cache.h"
#include "control.h"
#include "persistent.h"
#include "session.h"

namespace Rend2LightRecon
{

enum class WorldState
{
	Unloaded,
	Disabled,
	Loading,
	BuildRequired,
	Preparing,
	Building,
	Available,
	Unavailable,
	InvalidCache
};
const char *WorldStateName(WorldState);
const char *CacheCodeName(Disk::Code);
struct WorldFacts
{
	bool geometryReady = false; // all BSP/material/area-light loading finished, not "nonempty"
	bool areaLightsReady = false;
	uint64_t surfaces = 0, gridSamples = 0,
			 lightmapTextures = 0; // facts, not observation providers
};
struct WorldReport
{
	WorldState state = WorldState::Unloaded;
	BuildPolicy policy = BuildPolicy::Disabled;
	Status result = Status::Unloaded;
	Disk::Code cacheStatus = Disk::Code::NotCacheable;
	Disk::Code writeStatus = Disk::Code::NotCacheable;
	uint64_t ticket = 0, generation = 0, steps = 0;
	std::string world, assetIdentity, cacheKey, message;
	WorldFacts facts;
	bool described = false, keyComplete = false, cacheCalled = false, buildCalled = false;
	bool sessionPrepared = false, writeCalled = false;
	bool earlyPreflight = false, pendingCacheHit = false;
	Disk::Persistent::Work cacheWork;
	double secondsToPublication = 0; // Begin -> first valid immutable publication; not semantic
	// Logical owned metadata estimate (including dynamic capacities, excluding
	// allocator overhead/API indexes). No resident observation or chunk arrays.
	uint64_t assetBytes = 0, transientBytes = 0;
	bool transientBytesKnown = true; // false while a host/provider may hold build scratch
};
struct BuildPlan
{
	Core::InputDataset input; // immutable providers must OWN their backing snapshots
	Core::Options options;
	Core::Services services;
	std::unique_ptr<Core::Algorithm> algorithm;
};
class WorldSource
{
  public:
	virtual ~WorldSource() = default;
	// Policy != 0, optionally at the early Preflight boundary before BSP capture.
	// Must depend on immutable source identities, not loaded world pointers.
	// A missing dependency leaves coverage incomplete.
	virtual Disk::Error Describe(const std::string &world, Disk::Fingerprint &) = 0;
	// Cache hit/cache-only/disabled never call this. Prepare transfers immutable
	// input/provider/algorithm ownership; no live world/hunk views are permitted.
	virtual Core::Error Prepare(const std::string &world, BuildPlan &) = 0;
};
class WorldCache
{
  public:
	virtual ~WorldCache() = default;
	virtual Disk::Error Load(const Disk::Fingerprint &, ReconstructedLightingAsset &,
							 const Disk::Limits &) = 0;
	virtual Disk::Error Store(const Disk::Fingerprint &, const ReconstructedLightingAsset &,
							  const Disk::Limits &) = 0;
	// Compatibility default for metadata-only hosts. Concrete prepared adapters
	// override this and finish all decoding before returning a product.
	virtual Disk::Error LoadPrepared(const Disk::Fingerprint &, BuildProduct &,
									 const Disk::Limits &, Disk::Persistent::Work &);
};
// Explicit prepared selection (default: metadata only). All selected chunks are
// validated/prepared before return; file/index scratch is released. Queries
// never read files. Load is retained as the legacy metadata-only owner operation.
class NativeWorldCache : public WorldCache
{
  public:
	explicit NativeWorldCache(std::string root, Disk::Persistent::Selection selection = {})
		: cache_(std::move(root)), selection_(std::move(selection))
	{
	}
	Disk::Error Load(const Disk::Fingerprint &, ReconstructedLightingAsset &,
					 const Disk::Limits &) override;
	Disk::Error Store(const Disk::Fingerprint &, const ReconstructedLightingAsset &,
					  const Disk::Limits &) override;
	Disk::Error LoadPrepared(const Disk::Fingerprint &, BuildProduct &, const Disk::Limits &,
							 Disk::Persistent::Work &) override;

  private:
	Disk::Cache cache_;
	Disk::Persistent::Selection selection_;
};
struct WorldLimits
{
	Disk::Limits disk;
	uint64_t maxBuildSteps = 1024; // nonzero; bounded synchronous work, never a background job
};

class WorldLighting
{
  public:
	WorldLighting();
	WorldLighting(const WorldLighting &) = delete;
	WorldLighting &operator=(const WorldLighting &) = delete;
	// Single owner thread serializes all lifecycle methods. Reentrant mutation is
	// rejected. Acquire is the only cross-thread operation; leases are immutable.
	Result<uint64_t> Begin(const std::string &world, BuildPolicy);
	// Optional early owner boundary: identity and cache only; never Prepare or
	// publish. Complete commits the pending product after successful world load.
	Status Preflight(uint64_t ticket, WorldSource &, WorldCache *,
					 const WorldLimits & = WorldLimits());
	Status Complete(uint64_t ticket, const WorldFacts &, WorldSource &, WorldCache * = nullptr,
					const WorldLimits & = WorldLimits());
	Status Abort(
		uint64_t ticket) noexcept; // failed world load; stale tickets cannot affect a new map
	Status Unload() noexcept;	   // also called unconditionally on renderer reset/shutdown
	Snapshot Acquire() const
	{
		return published_.Acquire();
	}
	const WorldReport &Report() const
	{
		return report_;
	} // owner-thread diagnostic, not a consumer view
	const Disk::Fingerprint &InputFingerprint() const
	{
		return fingerprint_;
	} // explicit diagnostics
  private:
	Status End(WorldState, Status, const std::string &);
	Status Publish(const ReconstructedLightingAsset &, Completeness = Completeness::Ready,
				   std::shared_ptr<const CpuEvaluator> = {});
	void Lookup(WorldSource &, WorldCache *, const WorldLimits &);
	Manager published_;
	WorldReport report_;
	bool busy_ = false;
	bool preflightDone_ = false;
	Disk::Fingerprint fingerprint_;
	Disk::Error lookup_;
	BuildProduct pending_;
	double begunSeconds_ = 0;
};

// RAII around the WHOLE RE_LoadWorldMap call, including ri.Error exceptions.
// Failed/abandoned loads cannot publish a previous or half-initialized result.
class WorldLoadGuard
{
  public:
	WorldLoadGuard(WorldLighting &, const std::string &world, BuildPolicy);
	~WorldLoadGuard();
	WorldLoadGuard(const WorldLoadGuard &) = delete;
	WorldLoadGuard &operator=(const WorldLoadGuard &) = delete;
	Status Finish(const WorldFacts &, WorldSource &, WorldCache * = nullptr,
				  const WorldLimits & = WorldLimits());
	uint64_t Ticket() const
	{
		return ticket_;
	}

  private:
	WorldLighting &owner_;
	uint64_t ticket_ = 0;
	bool finished_ = false;
};
} // namespace Rend2LightRecon
