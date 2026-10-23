// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"
#include "tr_lightrecon.h"
#include "lightrecon/cache.h"
#include <algorithm>

std::string R_LightReconCacheRoot(void)
{
	char home[MAX_OSPATH]{};
	ri.Cvar_VariableStringBuffer("fs_homepath", home, sizeof(home));
	return Rend2LightRecon::Disk::CacheRoot(home);
}

// Nontrivial ownership stays outside hunk-allocated/memset world_t. One writer
// follows the main world's lifetime; leases keep old publications safely alive.
static Rend2LightRecon::WorldLighting s_reconstructedLighting;

Rend2LightRecon::Snapshot R_ReconstructedLighting(void)
{
	return s_reconstructedLighting.Acquire();
}

LightReconWorldLoad::LightReconWorldLoad(const char* name)
	: load_(s_reconstructedLighting, name ? name : "",
		static_cast<Rend2LightRecon::BuildPolicy>(r_lightReconBuild ? r_lightReconBuild->integer : 0))
{
}

namespace {
class RendererWorldSource : public Rend2LightRecon::WorldSource
{
public:
	Rend2LightRecon::Disk::Error Describe(const std::string&, Rend2LightRecon::Disk::Fingerprint&) override
	{
		// File/BSP/material dependency collection is deliberately NOT guessed from
		// world name, GPU textures or calibrated lightgrid buffers. No false cache hit.
		Rend2LightRecon::Disk::Error error;
		error.code = Rend2LightRecon::Disk::Code::NotCacheable;
		error.message = "Complete BSP/sidecar/material/settings fingerprints are unavailable";
		return error;
	}
	Rend2LightRecon::Core::Error Prepare(const std::string&, Rend2LightRecon::BuildPlan&) override
	{
		return Rend2LightRecon::Core::Failure(Rend2LightRecon::Core::Code::Unavailable,
			"Immutable BSP observation adapter and reconstruction algorithm are not installed");
	}
};
}
void LightReconWorldLoad::Finish(int surfaces, int gridSamples, int lightmapTextures)
{
	RendererWorldSource source;
	Rend2LightRecon::WorldFacts facts;
	if (s_reconstructedLighting.Report().policy == Rend2LightRecon::BuildPolicy::Disabled)
	{
		load_.Finish(facts, source); // no path lookup, dependency hashing, disk I/O or sessions
		return;
	}
	facts.geometryReady = facts.areaLightsReady = true;
	facts.surfaces = static_cast<uint64_t>(std::max(0, surfaces));
	facts.gridSamples = static_cast<uint64_t>(std::max(0, gridSamples));
	facts.lightmapTextures = static_cast<uint64_t>(std::max(0, lightmapTextures));
	Rend2LightRecon::NativeWorldCache cache(R_LightReconCacheRoot());
	load_.Finish(facts, source, &cache);
}

void R_ClearReconstructedLighting(void)
{
	s_reconstructedLighting.Unload();
}

Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot& snapshot, Rend2LightRecon::Consumer consumer)
{
	Rend2LightRecon::UseSettings settings;
	settings.master = r_lightReconUse && r_lightReconUse->integer != 0;
#define LIGHTRECON_CONTROL(name, maximum, description)
#define LIGHTRECON_CONSUMER(name, kind, description) settings.requested[static_cast<size_t>(Rend2LightRecon::Consumer::kind)] = name && name->integer != 0;
#include "tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
	// An existing fog/PBR/Forward+/LTC toggle alone is not an implementation of
	// canonical consumption. Future adapters must also check their real feature
	// prerequisites here, without setting any cvar or changing the shared asset.
	return Rend2LightRecon::EvaluateConsumer(snapshot, settings, consumer, Rend2LightRecon::FeatureSupport());
}

void R_LightReconInfo_f(void)
{
	using namespace Rend2LightRecon;
	const Snapshot snapshot = R_ReconstructedLighting();
	const auto asset = snapshot.Asset();
	ri.Printf(PRINT_ALL, "lightRecon: build requested=%d (next map load/reload); use=%d\n",
		r_lightReconBuild ? r_lightReconBuild->integer : 0,
		r_lightReconUse ? r_lightReconUse->integer : 0);
	ri.Printf(PRINT_ALL, "  asset=%s valid=%d emitters=%d transport=%d (metadata counts, not usable lights)\n",
		AvailabilityName(snapshot.State()), asset ? 1 : 0, asset ? static_cast<int>(asset.value->emitters.size()) : 0,
		asset ? static_cast<int>(asset.value->transport.size()) : 0);
	const auto& report = s_reconstructedLighting.Report();
	ri.Printf(PRINT_ALL, "  lifecycle=%s world=%s ticket=%llu generation=%llu identity=%s\n",
		WorldStateName(report.state), report.world.empty() ? "<none>" : report.world.c_str(),
		static_cast<unsigned long long>(report.ticket), static_cast<unsigned long long>(snapshot.Generation()),
		report.assetIdentity.empty() ? "<none>" : report.assetIdentity.c_str());
	ri.Printf(PRINT_ALL, "  cache=%s key-complete=%d key=%s write=%s; metadata-estimate=%llu temporary=%s\n",
		CacheCodeName(report.cacheStatus), report.keyComplete ? 1 : 0, report.cacheKey.empty() ? "<none>" : report.cacheKey.c_str(),
		CacheCodeName(report.writeStatus), static_cast<unsigned long long>(report.assetBytes),
		report.transientBytesKnown ? "0 bytes" : "host/provider scratch (not measured)");
	if (!report.world.empty())
		ri.Printf(PRINT_ALL, "  build applied=%d result=%s cache-called=%d build-called=%d: %s\n",
			static_cast<int>(report.policy), StatusName(report.result),
			report.cacheCalled ? 1 : 0, report.buildCalled ? 1 : 0, report.message.c_str());
	else ri.Printf(PRINT_ALL, "  build policy has not been applied to a loaded world\n");
	ri.Printf(PRINT_ALL, "  world-ready=%d area-ready=%d surfaces=%llu grid=%llu lightmap-textures=%llu session=%d steps=%llu\n",
		report.facts.geometryReady ? 1 : 0, report.facts.areaLightsReady ? 1 : 0,
		static_cast<unsigned long long>(report.facts.surfaces), static_cast<unsigned long long>(report.facts.gridSamples),
		static_cast<unsigned long long>(report.facts.lightmapTextures), report.sessionPrepared ? 1 : 0,
		static_cast<unsigned long long>(report.steps));
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const Consumer c = static_cast<Consumer>(i);
		const ConsumerState state = R_LightReconConsumer(snapshot, c);
		ri.Printf(PRINT_ALL, "  %s requested=%d effective=%d capability=%s feature=%d: %s\n", ConsumerCvar(c),
			state.requested ? 1 : 0, state.effective ? 1 : 0, StatusName(state.capabilityStatus),
			state.featureAvailable ? 1 : 0, GateReasonName(state.reason));
	}
}
