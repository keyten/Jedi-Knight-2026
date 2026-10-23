// Map owner, synchronous CPU loading only. No world_t, hunk or GPU dependencies.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cache.h"
#include "control.h"
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
	// Called only after the world is ready and policy != 0. A missing dependency
	// must leave coverage incomplete. Does not borrow world pointers in the result.
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
};
// Metadata-only cache adapter. Open file/index/decode buffers are local and freed
// before return. Future field decoders must finish/pin their own data before an
// evaluator is published; canonical read-only queries never perform file I/O.
class NativeWorldCache : public WorldCache
{
  public:
	explicit NativeWorldCache(std::string root) : cache_(std::move(root)) {}
	Disk::Error Load(const Disk::Fingerprint &, ReconstructedLightingAsset &,
					 const Disk::Limits &) override;
	Disk::Error Store(const Disk::Fingerprint &, const ReconstructedLightingAsset &,
					  const Disk::Limits &) override;

  private:
	Disk::Cache cache_;
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
	Status Complete(uint64_t ticket, const WorldFacts &, WorldSource &, WorldCache * = nullptr,
					const WorldLimits & = WorldLimits());
	Status
	Abort(uint64_t ticket) noexcept; // failed world load; stale tickets cannot affect a new map
	Status Unload() noexcept;		 // also called unconditionally on renderer reset/shutdown
	Snapshot Acquire() const
	{
		return published_.Acquire();
	}
	const WorldReport &Report() const
	{
		return report_;
	} // owner-thread diagnostic, not a consumer view
  private:
	Status End(WorldState, Status, const std::string &);
	Status Publish(const ReconstructedLightingAsset &);
	Manager published_;
	WorldReport report_;
	bool busy_ = false;
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
