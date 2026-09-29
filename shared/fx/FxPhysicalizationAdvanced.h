// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
#ifndef FX_PHYSICALIZATION_ADVANCED_H
#define FX_PHYSICALIZATION_ADVANCED_H
#include "FxPhysicalization.h"

namespace FxPhysical {
enum Kind { OtherKind, SpriteKind, LightKind, FlashKind, ChildKind };
enum Role { UnknownRole, MediumRole, FireballRole, DiscreteRole, FlashRole, LightRole, ChildRole, AuthoredRole };
struct Descriptor {
    Input input;
    Kind kind = OtherKind;
    bool authored = false, genericUnsafe = false;
    float startSize = 0;
    std::vector<std::string> shaders;
    std::vector<Profile> profiles;
};
struct Composition {
    unsigned roles[8] = {};
    float mediumCount = 0;
    bool litBurst = false;
};
struct AdvancedProfile {
    Profile composite, adaptive;
    bool adaptiveForComposite = false;
    float emissionRadiance = 0;
};
struct ShaderShape {
    const char* shader;
    const char* texture;
    uint64_t textureHash;
    const char* script;
    uint64_t scriptHash;
    float meanMask, radius90, outerMean;
};
#include "FxPhysicalizationShapes.h"

inline bool SoftSmokeShader(const std::string& shader) {
    return shader == "gfx/misc/black_smoke" || shader == "gfx/misc/black_smoke2" ||
        shader == "gfx/effects/alpha_smoke" || shader == "gfx/effects/alpha_smoke2";
}
inline Role ShaderRole(const std::string& shader) {
    if (shader == "gfx/exp/rocket_explosion" || shader == "gfx/exp/slower_rocket_explosion" ||
        shader == "gfx/exp/explosion1" || shader == "gfx/effects/fire") return FireballRole;
    if (shader == "gfx/misc/dotfill_a" || shader == "gfx/misc/dust") return DiscreteRole;
    // Steam and whiteglow are deliberately ambiguous; not evidence of fire.
    return UnknownRole;
}
inline Role Describe(const Descriptor& d) {
    if (d.authored) return AuthoredRole;
    if (d.kind == LightKind) return LightRole;
    if (d.kind == FlashKind) return FlashRole;
    if (d.kind == ChildKind) return ChildRole;
    for (const Profile& p : d.profiles) if (p.material != None) return MediumRole;
    Role result = UnknownRole;
    bool first = true;
    for (const std::string& shader : d.shaders) {
        const Role role = ShaderRole(shader);
        if (!first && result != role) return UnknownRole;
        first = false; result = role;
    }
    return result;
}
inline Composition Compose(const std::vector<Descriptor>& descriptors) {
    Composition c;
    bool light = false, burst = false;
    for (const Descriptor& d : descriptors) {
        const Role r = Describe(d);
        ++c.roles[r];
        const bool alive = std::isfinite(d.input.maxLife) && d.input.maxLife > 0 &&
            std::isfinite(d.input.maxCount) && d.input.maxCount > 0;
        light = light || (r == LightRole && alive && std::isfinite(d.input.maxSize) && d.input.maxSize > 0);
        burst = burst || ((r == FireballRole || r == FlashRole) && alive && d.input.maxLife <= 1500);
    }
    c.litBurst = light && burst;
    return c;
}
inline Profile CompositeProfile(const Descriptor& d, const std::string& shader, const Composition& c) {
    if (!c.litBurst || d.input.effect.empty() || d.kind != SpriteKind || d.authored || d.genericUnsafe || d.input.unsafe ||
        !SoftSmokeShader(shader)) return {};
    const std::string name = LowerPath(d.input.name);
    if (!Token(name, "smoke") && name != "lingeringsmoke") return {};
    const std::string context = d.input.effect + " " + d.input.name;
    for (const char* veto : {"saber", "force", "spark", "muzzle", "shot", "flare", "glow", "blood"})
        if (Token(context, veto)) return {};
    Input in = d.input;
    // Context allows fire/explosion in the parent path; all numerical and
    // shader confidence checks remain the stage-1 high-confidence checks.
    in.effect = "effects/composite/smoke.efx"; in.name = "smoke";
    in.shader = shader; in.fingerprint = 0; in.ordinal = -1;
    Profile p = Analyze(in);
    if (p.material != None) p.evidence = HighConfidenceComposite;
    return p;
}
inline const ShaderShape* Shape(const std::string& shader) {
    for (const ShaderShape& s : shaderShapes) if (shader == s.shader) return &s;
    return nullptr;
}
inline Profile Calibrate(Profile p, const ShaderShape& s, const Descriptor& d, float mediumCount) {
    if (p.material == None || !std::isfinite(d.startSize) || d.startSize <= 0 ||
        !std::isfinite(d.input.maxSize) || d.input.maxSize <= 0 ||
        !std::isfinite(mediumCount) || mediumCount <= 0 ||
        !std::isfinite(s.meanMask) || s.meanMask <= 0 || s.meanMask > 1 ||
        !std::isfinite(s.radius90) || s.radius90 <= 0 || s.radius90 > 2 ||
        !std::isfinite(s.outerMean) || s.outerMean < 0 || s.outerMean > 1) return {};
    const float fill = std::max(0.6f, std::min(1.5f, std::sqrt(s.meanMask / 0.25f)));
    const float growth = std::max(1.0f, std::min(4.0f, d.input.maxSize / d.startSize));
    const float population = std::max(0.25f, 1.0f / std::sqrt(std::max(1.0f, mediumCount)));
    p.opticalDepth *= std::max(0.25f, std::min(1.5f, fill * population / std::sqrt(growth)));
    p.radiusScale = std::max(0.45f, std::min(0.95f, s.radius90));
    p.softness = std::max(0.55f, std::min(0.85f, 0.55f + 0.25f * s.outerMean / s.meanMask));
    return p;
}
inline bool SameProfile(const Profile& a, const Profile& b) {
    return a.material == b.material && a.evidence == b.evidence && a.opticalDepth == b.opticalDepth &&
        a.radiusScale == b.radiusScale && a.softness == b.softness && a.anisotropy == b.anisotropy &&
        a.albedo[0] == b.albedo[0] && a.albedo[1] == b.albedo[1] && a.albedo[2] == b.albedo[2];
}
inline AdvancedProfile ResolveAdvanced(const std::vector<Alternative>& alternatives, int handle) {
    AdvancedProfile result;
    bool first = true;
    if (handle <= 0) return result;
    for (const Alternative& a : alternatives) {
        if (a.handle != handle) continue;
        const AdvancedProfile p = a.advanced ? *a.advanced : AdvancedProfile();
        if (!first && (!SameProfile(result.composite, p.composite) || !SameProfile(result.adaptive, p.adaptive) ||
            result.adaptiveForComposite != p.adaptiveForComposite || result.emissionRadiance != p.emissionRadiance)) return {};
        first = false; result = p;
    }
    return result;
}
inline const char* RoleName(Role r) {
    const char* names[] = {"unknown/mixed", "medium", "fireball", "discrete points/streaks", "flash", "authored light", "child effect", "authored medium"};
    return names[r];
}
} // namespace FxPhysical
#endif
