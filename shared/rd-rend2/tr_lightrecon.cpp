// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"
#include "tr_lightrecon.h"

// Nontrivial ownership stays outside hunk-allocated/memset world_t. One writer
// follows the main world's lifetime; leases keep old publications safely alive.
static Rend2LightRecon::Manager s_reconstructedLighting;
static Rend2LightRecon::BuildReport s_buildReport;
static bool s_policyApplied = false;

Rend2LightRecon::Snapshot R_ReconstructedLighting(void)
{
	return s_reconstructedLighting.Acquire();
}

void R_LoadReconstructedLighting(void)
{
	const auto policy = static_cast<Rend2LightRecon::BuildPolicy>(r_lightReconBuild ? r_lightReconBuild->integer : 0);
	// No cache backend or algorithm exists yet. Do not call the old lightgrid
	// reconstruction or manufacture a successful empty result as a substitute.
	s_buildReport = Rend2LightRecon::ApplyBuildPolicy(policy, s_reconstructedLighting);
	s_policyApplied = true;
}

void R_ClearReconstructedLighting(void)
{
	s_reconstructedLighting.SetUnavailable(Rend2LightRecon::Availability::Unloaded);
	s_policyApplied = false;
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
	if (s_policyApplied)
		ri.Printf(PRINT_ALL, "  build applied=%d result=%s cache-called=%d build-called=%d: %s\n",
			static_cast<int>(s_buildReport.policy), StatusName(s_buildReport.status),
			s_buildReport.cacheCalled ? 1 : 0, s_buildReport.buildCalled ? 1 : 0, s_buildReport.message.c_str());
	else ri.Printf(PRINT_ALL, "  build policy has not been applied to a loaded world\n");
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const Consumer c = static_cast<Consumer>(i);
		const ConsumerState state = R_LightReconConsumer(snapshot, c);
		ri.Printf(PRINT_ALL, "  %s requested=%d effective=%d capability=%s feature=%d: %s\n", ConsumerCvar(c),
			state.requested ? 1 : 0, state.effective ? 1 : 0, StatusName(state.capabilityStatus),
			state.featureAvailable ? 1 : 0, GateReasonName(state.reason));
	}
}
