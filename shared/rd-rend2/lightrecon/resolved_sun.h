// Renderer sun admission, independent of GL and world ownership.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_api.h"

namespace Rend2LightRecon { namespace SunLighting {
enum class Policy { Fallback, Shader, Forced, Scene, Canonical };
enum class Kind { Unavailable, LegacyRenderer, AuthoredPhysical, ReconstructedPhysical, ConditionalRelative,
    LegacySceneEquivalent, ApproximateAppearance };
enum class Reason { Disabled, Missing, NoResolvedSun, Ambiguous, UnknownDirection, UnsupportedPhotometry,
    Unsuitable, Uncertainty, Configuration, PhysicalModeRequired, Worldless, Forced, Scene,
    ScaleMismatch, FeatureUnavailable, Accepted };
// Value-only: safe in hunk/memset renderer structs and queued/recursive scenes.
// No canonical pointer, handle or lease survives resolution.
struct State {
    Policy policy = Policy::Fallback;
    Kind kind = Kind::Unavailable, rejectedKind = Kind::Unavailable;
    Reason reason = Reason::Missing;
    Status status = Status::Unavailable;
    Vec3 directionToSun, photonDirection, illuminanceLux;
    Id source = 0, provenance = 0, calibration = 0;
    uint64_t generation = 0;
    float angularRadius = 0, confidence = 0;
    bool available = false, canonical = false, explicitOverride = false;
    bool angularRadiusKnown = false, confidenceKnown = false;
    bool worldDiffuse = false, entityDiffuse = false, volumetricDirect = false;
};
struct PolicyInput {
    bool master = false, requested = false, physical = false, world = false;
    bool sceneOverride = false, shaderSun = false, adapterReady = true;
    int forceSun = 0;
};
// Expensive metadata/typed conversion only on publication change, never fitting/I/O.
State Resolve(const Snapshot&);
// Cheap per-scene precedence. Historical RDF_EXTRA sun remains above r_forceSun.
State Select(const State&, const PolicyInput&);
bool SceneIlluminance(const State&, float physicalToScene, Vec3&);
const char* ReasonName(Reason);
const char* KindName(Kind);
} }
