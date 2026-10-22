// Renderer adapter for canonical CPU policy. No lighting integration yet.
#pragma once
#include "lightrecon/control.h"

// Retain this lease while using views and effective-state decisions. Loading is
// independent of consumption; acquiring a snapshot never runs work.
Rend2LightRecon::Snapshot R_ReconstructedLighting(void);
Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot&, Rend2LightRecon::Consumer);
void R_LoadReconstructedLighting(void); // main-world map load only
void R_ClearReconstructedLighting(void);
void R_LightReconInfo_f(void);
