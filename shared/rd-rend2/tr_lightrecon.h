// Renderer adapter for canonical CPU policy. No lighting integration yet.
#pragma once
#include "lightrecon/lifecycle.h"

// Retain this lease while using views and effective-state decisions. Loading is
// independent of consumption; acquiring a snapshot never runs work.
Rend2LightRecon::Snapshot R_ReconstructedLighting(void);
Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot&, Rend2LightRecon::Consumer);
void R_ClearReconstructedLighting(void);
void R_LightReconInfo_f(void);
// Resolves only the supported per-user fs_homepath; performs no I/O or loading.
std::string R_LightReconCacheRoot(void);

// Stack guard for the entire MAIN world load. No use from R_LoadBSP/sub-BSPs.
// Counts are copied facts only; no borrowed hunk/renderer pointer is retained.
class LightReconWorldLoad
{
public:
	explicit LightReconWorldLoad(const char* name);
	void Finish(int surfaces, int gridSamples, int lightmapTextures);
private:
	Rend2LightRecon::WorldLoadGuard load_;
};
