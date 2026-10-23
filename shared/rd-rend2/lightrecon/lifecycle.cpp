// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lifecycle.h"
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>

namespace Rend2LightRecon
{
namespace
{
uint64_t NewTicket()
{
	static std::atomic<uint64_t> next{0};
	uint64_t v = next.load();
	while (v != std::numeric_limits<uint64_t>::max())
		if (next.compare_exchange_weak(v, v + 1))
			return v + 1;
	return 0;
}
struct Busy
{
	bool &flag;
	explicit Busy(bool &b) : flag(b)
	{
		flag = true;
	}
	~Busy()
	{
		flag = false;
	}
};
bool InvalidCache(Disk::Code c)
{
	return c == Disk::Code::Corrupt || c == Disk::Code::Mismatch || c == Disk::Code::Unsupported ||
		   c == Disk::Code::LimitExceeded || c == Disk::Code::InvalidAsset;
}
Status CacheStatus(Disk::Code c)
{
	if (c == Disk::Code::Ok)
		return Status::Ok;
	if (c == Disk::Code::Missing)
		return Status::Missing;
	if (c == Disk::Code::Unsupported)
		return Status::Unsupported;
	return InvalidCache(c) ? Status::Corrupt : Status::Unavailable;
}
Status CoreStatus(Core::Code c)
{
	if (c == Core::Code::UnsupportedSchema)
		return Status::Unsupported;
	if (c == Core::Code::InvalidInput || c == Core::Code::InvalidState ||
		c == Core::Code::ProviderError || c == Core::Code::AlgorithmError)
		return Status::Corrupt;
	return Status::Unavailable;
}
// An estimate of owned metadata, never an allocation or encoding of field data.
uint64_t Extra(const Provenance &p)
{
	return p.locator.capacity() + p.contentHash.capacity() + p.producer.capacity() +
		   p.method.capacity() + p.parents.capacity() * sizeof(Id);
}
uint64_t Extra(const Calibration &p)
{
	return p.method.capacity();
}
uint64_t Extra(const HypothesisGroup &p)
{
	return p.description.capacity();
}
uint64_t Extra(const PhysicalEmitter &p)
{
	return p.extensionType.capacity() + p.source.regions.capacity() * sizeof(Id);
}
uint64_t Extra(const EnvironmentLighting &p)
{
	return p.source.regions.capacity() * sizeof(Id);
}
uint64_t Extra(const TransportField &p)
{
	return p.source.regions.capacity() * sizeof(Id);
}
uint64_t Extra(const GeometryRegion &p)
{
	return p.key.capacity();
}
uint64_t Extra(const Dataset &p)
{
	return p.locator.capacity() + p.contentHash.capacity() + p.encoding.capacity();
}
uint64_t Extra(const Diagnostic &p)
{
	return p.code.capacity() + p.message.capacity() + p.metricUnit.capacity();
}
uint64_t Extra(const ContributionReference &p)
{
	return p.sources.capacity() * sizeof(Id);
}
template <class T> uint64_t Extra(const T &)
{
	return 0;
}
template <class T> uint64_t Size(const std::vector<T> &v)
{
	uint64_t n = v.capacity() * sizeof(T);
	for (const auto &x : v)
		n += Extra(x);
	return n;
}
uint64_t AssetBytes(const ReconstructedLightingAsset &a)
{
	return sizeof(a) + a.schema.capacity() + a.lineage.capacity() + Size(a.provenanceRecords) +
		   Size(a.calibrations) + Size(a.hypothesisGroups) + Size(a.emitters) + Size(a.suns) +
		   Size(a.environments) + Size(a.transport) + Size(a.regions) + Size(a.datasets) +
		   Size(a.visibility) + Size(a.contributions) + Size(a.diagnostics);
}
bool SameUnits(const ReconstructedLightingAsset &a, const Disk::Fingerprint &f)
{
	return a.units.worldUnitsPerMeter == f.units.worldUnitsPerMeter &&
		   a.units.physicalToScene == f.units.physicalToScene;
}
} // namespace

const char *WorldStateName(WorldState s)
{
	switch (s)
	{
	case WorldState::Unloaded:
		return "Unloaded";
	case WorldState::Disabled:
		return "Disabled";
	case WorldState::Loading:
		return "Loading";
	case WorldState::BuildRequired:
		return "BuildRequired";
	case WorldState::Preparing:
		return "Preparing";
	case WorldState::Building:
		return "Building";
	case WorldState::Available:
		return "Available";
	case WorldState::Unavailable:
		return "Unavailable";
	case WorldState::InvalidCache:
		return "InvalidCache";
	}
	return "Invalid";
}
const char *CacheCodeName(Disk::Code s)
{
	switch (s)
	{
	case Disk::Code::Ok:
		return "Ok";
	case Disk::Code::Missing:
		return "Missing";
	case Disk::Code::NotCacheable:
		return "NotCacheable";
	case Disk::Code::Mismatch:
		return "Mismatch";
	case Disk::Code::Unsupported:
		return "Unsupported";
	case Disk::Code::Corrupt:
		return "Corrupt";
	case Disk::Code::LimitExceeded:
		return "LimitExceeded";
	case Disk::Code::InvalidAsset:
		return "InvalidAsset";
	case Disk::Code::InvalidArgument:
		return "InvalidArgument";
	case Disk::Code::IoError:
		return "IoError";
	}
	return "Invalid";
}
Disk::Error NativeWorldCache::Load(const Disk::Fingerprint &f, ReconstructedLightingAsset &a,
								   const Disk::Limits &limits)
{
	Disk::Index index;
	std::unique_ptr<Disk::FileReader> file;
	return cache_.Load(f, a, index, file, limits); // all file/index scratch released here
}
Disk::Error NativeWorldCache::Store(const Disk::Fingerprint &f, const ReconstructedLightingAsset &a,
									const Disk::Limits &limits)
{
	return cache_.Store(f, a, {}, limits);
}
WorldLighting::WorldLighting()
{
	Unload();
}
Status WorldLighting::End(WorldState state, Status status, const std::string &message)
{
	report_.state = state;
	report_.result = status;
	report_.message = message;
	report_.assetBytes = 0;
	report_.assetIdentity.clear();
	published_.Reset();
	published_.SetUnavailable(
		state == WorldState::Disabled
			? Availability::Disabled
			: state == WorldState::Unloaded
				  ? Availability::Unloaded
				  : state == WorldState::InvalidCache || status == Status::Corrupt
						? Availability::Corrupt
						: state == WorldState::Loading ? Availability::Pending
													   : Availability::Missing,
		message);
	report_.generation = published_.Acquire().Generation();
	return status;
}
Result<uint64_t> WorldLighting::Begin(const std::string &world, BuildPolicy policy)
{
	Result<uint64_t> r;
	if (busy_)
	{
		r.status = Status::Pending;
		return r;
	}
	Busy guard(busy_);
	published_.Reset();
	report_ = WorldReport();
	report_.ticket = NewTicket();
	report_.policy = policy;
	try
	{
		if (!report_.ticket || world.empty() || world.size() > 4096 ||
			policy < BuildPolicy::Disabled || policy > BuildPolicy::ForceRebuild)
		{
			r.status = End(WorldState::Unavailable, Status::InvalidArgument,
						   "Invalid map identity/policy or exhausted ticket space");
			return r;
		}
		report_.world = world;
		End(policy == BuildPolicy::Disabled ? WorldState::Disabled : WorldState::Loading,
			policy == BuildPolicy::Disabled ? Status::Disabled : Status::Pending,
			policy == BuildPolicy::Disabled ? "Canonical build/load disabled"
											: "Waiting for complete world load");
		r.status = Status::Ok;
		r.value = report_.ticket;
		return r;
	}
	catch (...)
	{
		published_.Reset();
		report_.state = WorldState::Unavailable;
		report_.result = Status::Corrupt;
		r.status = Status::Corrupt;
		return r;
	}
}
Status WorldLighting::Publish(const ReconstructedLightingAsset &asset)
{
	Status s = published_.Publish(asset, Completeness::Ready);
	if (s != Status::Ok)
		return End(WorldState::Unavailable, s, "Validated asset publication failed");
	report_.state = WorldState::Available;
	report_.result = Status::Ok;
	auto a = published_.Acquire().Asset();
	report_.assetIdentity = a.value->lineage;
	report_.assetBytes = AssetBytes(*a.value);
	report_.generation = published_.Acquire().Generation();
	return Status::Ok;
}
Status WorldLighting::Complete(uint64_t ticket, const WorldFacts &facts, WorldSource &source,
							   WorldCache *cache, const WorldLimits &limits)
{
	if (busy_)
		return Status::Pending;
	if (!ticket || ticket != report_.ticket)
		return Status::StaleHandle;
	if (report_.state == WorldState::Disabled)
		return Status::Disabled; // no callbacks, cache or input copies
	if (report_.state != WorldState::Loading)
		return Status::InvalidArgument;
	Busy guard(busy_);
	report_.facts = facts;
	report_.transientBytesKnown = false;
	try
	{
		auto work = [&]() -> Status {
			if (!facts.geometryReady || !facts.areaLightsReady || !limits.maxBuildSteps)
				return End(WorldState::Unavailable, Status::Unavailable,
						   "World initialization is incomplete or work budget is zero");
			Disk::Fingerprint fingerprint;
			report_.described = true;
			Disk::Error description = source.Describe(report_.world, fingerprint);
			Disk::Digest key{};
			Disk::Error keyStatus =
				description ? Disk::MakeKey(fingerprint, key, limits.disk) : description;
			if (keyStatus)
				for (const auto &input : fingerprint.inputs)
					if (input.kind == Disk::InputKind::Bsp && input.name != report_.world)
					{
						keyStatus.code = Disk::Code::NotCacheable;
						keyStatus.message =
							"Fingerprint BSP identity does not match the current world";
					}
			report_.keyComplete = bool(keyStatus);
			report_.cacheStatus = keyStatus.code;
			if (keyStatus)
				report_.cacheKey = Disk::Hex(key);
			if (report_.policy != BuildPolicy::ForceRebuild && keyStatus)
			{
				ReconstructedLightingAsset candidate;
				Disk::Error loaded;
				if (cache)
				{
					report_.cacheCalled = true;
					try
					{
						loaded = cache->Load(fingerprint, candidate, limits.disk);
					}
					catch (...)
					{
						loaded.code = Disk::Code::IoError;
						loaded.message = "Cache load provider threw";
					}
				}
				else
				{
					loaded.code = Disk::Code::Unsupported;
					loaded.message = "No cache adapter supplied";
				}
				report_.cacheStatus = loaded.code;
				if (loaded)
				{
					std::string reason;
					if (!SameUnits(candidate, fingerprint) || !Validate(candidate, &reason))
					{
						report_.cacheStatus = Disk::Code::Corrupt;
						loaded.code = Disk::Code::Corrupt;
						loaded.message =
							"Cache provider returned invalid metadata/units: " + reason;
					}
					else
					{
						report_.message =
							"Validated cached metadata loaded; no field evaluator installed";
						return Publish(candidate);
					}
				}
				if (report_.policy == BuildPolicy::CacheOnly)
					return End(report_.cacheCalled && InvalidCache(loaded.code)
								   ? WorldState::InvalidCache
								   : WorldState::Unavailable,
							   CacheStatus(loaded.code), loaded.message);
			}
			else if (report_.policy == BuildPolicy::CacheOnly)
			{
				return End(WorldState::Unavailable, Status::Unavailable, keyStatus.message);
			}
			report_.state = WorldState::BuildRequired;
			report_.buildCalled = true;
			BuildPlan plan;
			report_.state = WorldState::Preparing;
			Core::Error prepared = source.Prepare(report_.world, plan);
			if (!prepared)
				return End(CoreStatus(prepared.code) == Status::Unavailable
							   ? WorldState::BuildRequired
							   : WorldState::Unavailable,
						   CoreStatus(prepared.code), prepared.message);
			if (keyStatus &&
				(plan.input.manifest.units.worldUnitsPerMeter !=
					 fingerprint.units.worldUnitsPerMeter ||
				 plan.input.manifest.units.physicalToScene != fingerprint.units.physicalToScene))
				return End(WorldState::Unavailable, Status::Corrupt,
						   "Build input units differ from fingerprint");
			plan.options.maxSteps = plan.options.maxSteps
										? std::min(plan.options.maxSteps, limits.maxBuildSteps)
										: limits.maxBuildSteps;
			const Units inputUnits = plan.input.manifest.units;
			Core::Session session(std::move(plan.input), plan.options, std::move(plan.services),
								  std::move(plan.algorithm));
			report_.sessionPrepared = true;
			Core::Error error = session.Begin();
			if (!error)
				return End(CoreStatus(error.code) == Status::Unavailable ? WorldState::BuildRequired
																		 : WorldState::Unavailable,
						   CoreStatus(error.code), error.message);
			report_.state = WorldState::Building;
			for (uint64_t step = 0;
				 step < plan.options.maxSteps && session.State() == Core::SessionState::Running;
				 ++step)
			{
				error = session.Step();
				report_.steps = session.Steps();
				if (!error)
					return End(WorldState::Unavailable, CoreStatus(error.code), error.message);
			}
			if (session.State() != Core::SessionState::Completed || !session.Output())
				return End(WorldState::BuildRequired, Status::Unavailable,
						   "Incomplete build discarded at synchronous work limit");
			auto output = session.Output();
			std::string reason;
			if (!Validate(*output, &reason) || (keyStatus && !SameUnits(*output, fingerprint)) ||
				output->units.worldUnitsPerMeter != inputUnits.worldUnitsPerMeter ||
				output->units.physicalToScene != inputUnits.physicalToScene)
				return End(WorldState::Unavailable, Status::Corrupt,
						   "Invalid build result: " + reason);
			// No stale writes: mutation/reentry is rejected throughout this call, and
			// the ticket is checked before both durable write and atomic publication.
			if (ticket != report_.ticket)
				return Status::StaleHandle;
			if (cache && keyStatus)
			{
				report_.writeCalled = true;
				try
				{
					report_.writeStatus = cache->Store(fingerprint, *output, limits.disk).code;
				}
				catch (...)
				{
					report_.writeStatus = Disk::Code::IoError;
				}
			}
			if (ticket != report_.ticket)
				return Status::StaleHandle;
			report_.message =
				report_.writeCalled && report_.writeStatus != Disk::Code::Ok
					? "Completed validated CPU build; cache write failed (asset remains available)"
					: "Completed validated CPU build; consumer controls remain independent";
			return Publish(*output);
		};
		Status status = work();
		report_.transientBytes = 0;
		report_.transientBytesKnown = true;
		return status;
	}
	catch (const std::exception &e)
	{
		published_.Reset();
		report_.state = WorldState::Unavailable;
		report_.result = Status::Corrupt;
		report_.assetBytes = 0;
		try
		{
			End(WorldState::Unavailable, Status::Corrupt, e.what());
		}
		catch (...)
		{
		}
	}
	catch (...)
	{
		published_.Reset();
		report_.state = WorldState::Unavailable;
		report_.result = Status::Corrupt;
		report_.assetBytes = 0;
		try
		{
			End(WorldState::Unavailable, Status::Corrupt, "World source/cache threw");
		}
		catch (...)
		{
		}
	}
	report_.transientBytes = 0;
	report_.transientBytesKnown = true;
	return Status::Corrupt;
}
Status WorldLighting::Abort(uint64_t ticket) noexcept
{
	if (busy_)
		return Status::Pending;
	if (!ticket || ticket != report_.ticket)
		return Status::StaleHandle;
	published_.Reset();
	report_.state = WorldState::Unavailable;
	report_.result = Status::Unavailable;
	report_.assetBytes = 0;
	try
	{
		return End(WorldState::Unavailable, Status::Unavailable,
				   "World load failed or was abandoned");
	}
	catch (...)
	{
		return Status::Unavailable;
	}
}
Status WorldLighting::Unload() noexcept
{
	if (busy_)
		return Status::Pending;
	published_.Reset();
	report_ = WorldReport();
	report_.ticket = NewTicket();
	try
	{
		return End(WorldState::Unloaded, Status::Unloaded, "World context unloaded");
	}
	catch (...)
	{
		return Status::Unloaded;
	}
}
WorldLoadGuard::WorldLoadGuard(WorldLighting &owner, const std::string &world, BuildPolicy policy)
	: owner_(owner)
{
	auto r = owner_.Begin(world, policy);
	if (r)
		ticket_ = r.value;
}
WorldLoadGuard::~WorldLoadGuard()
{
	if (!finished_ && ticket_)
		owner_.Abort(ticket_);
}
Status WorldLoadGuard::Finish(const WorldFacts &facts, WorldSource &source, WorldCache *cache,
							  const WorldLimits &limits)
{
	if (finished_)
		return Status::InvalidArgument;
	Status s = owner_.Complete(ticket_, facts, source, cache, limits);
	if (s != Status::StaleHandle && s != Status::Pending)
		finished_ = true;
	return s;
}
} // namespace Rend2LightRecon
