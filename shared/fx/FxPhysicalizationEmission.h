// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
#ifndef FX_PHYSICALIZATION_EMISSION_H
#define FX_PHYSICALIZATION_EMISSION_H
#include "FxPhysicalizationAdvanced.h"
namespace FxPhysical {
struct EmissionAsset { const char* path; uint64_t fingerprint; };
struct EmissionRule {
    const char* effect; uint64_t fingerprint; int ordinal; const char* shader;
    float radiance; unsigned firstAsset, numAssets;
};
#include "FxPhysicalizationEmissionStock.h"
inline const EmissionRule* EmissionCandidate(const Descriptor& d, const std::string& shader) {
    if (d.authored || d.kind != SpriteKind || d.input.unsafe) return nullptr;
    for (const EmissionRule& r : emissionRules)
        if (d.input.effect == r.effect && d.input.fingerprint == r.fingerprint &&
            d.input.ordinal == r.ordinal && shader == r.shader) return &r;
    return nullptr;
}
inline float EmissionEnvelope(float age, float life) {
    if (!std::isfinite(age) || !std::isfinite(life) || life <= 0 || age < 0 || age >= life) return 0;
    // Smooth birth and drain, independent of an animation's opaque alpha.
    const float t = age / life;
    return std::min(1.0f, t * 10.0f) * (1.0f - t) * (1.0f - t);
}
inline float LinearTint(unsigned char value) {
    const float v = value / 255.0f;
    return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
}
// Sprite colour without the legacy fade. Additive (no useAlpha) sprites carry
// alpha in their byte RGB; the glow has its own envelope, so divide it out once.
inline float EmissionTint(unsigned char value, float alpha, bool rgbCarriesAlpha) {
    if (!rgbCarriesAlpha) return LinearTint(value);
    if (!std::isfinite(alpha) || alpha * 255.0f < 1.0f) return 0;
    const float v = std::min(1.0f, value / (255.0f * std::min(1.0f, alpha)));
    return LinearTint((unsigned char)(v * 255.0f + .5f));
}
}
#endif
