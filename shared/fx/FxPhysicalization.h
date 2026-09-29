// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Engine-independent, deterministic policy. No RNG, renderer calls or owned
// objects in SEffectTemplate; both JA frontends use this same implementation.
#ifndef FX_PHYSICALIZATION_H
#define FX_PHYSICALIZATION_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace FxPhysical {

enum Material { None, DarkSmoke, LightSmoke, Mist, DustCloud, Gas };
enum Evidence { Rejected, StockSignature, HighConfidenceFamily };

inline std::string LowerPath(const std::string& input) {
    std::string result;
    for (char c : input) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        result += c;
    }
    return result;
}

inline std::string EffectPath(const std::string& input) {
    std::string path = LowerPath(input);
    if (path.empty() || path.find("..") != std::string::npos ||
        path.find_first_of("*?\"\t\r\n ;,") != std::string::npos) return {};
    if (path.compare(0, 8, "effects/") != 0) path = "effects/" + path;
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".efx") != 0) path += ".efx";
    // Must fit the particle's independent identity (no dangling template refs).
    return path.size() < 64 ? path : std::string();
}

inline std::string ShaderPath(const std::string& input) {
    std::string path = LowerPath(input);
    const size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string ext = path.substr(dot);
        if (ext == ".tga" || ext == ".jpg" || ext == ".png" || ext == ".dds" || ext == ".jpeg") path.resize(dot);
    }
    return path;
}

inline uint64_t Fingerprint(const void* bytes, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    const unsigned char* p = static_cast<const unsigned char*>(bytes);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 1099511628211ULL;
    return hash;
}

inline bool Token(const std::string& text, const char* token) {
    const std::string lower = LowerPath(text);
    const std::string word(token);
    for (size_t p = lower.find(word); p != std::string::npos; p = lower.find(word, p + 1)) {
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
        if ((p == 0 || !letter(lower[p - 1])) &&
            (p + word.size() == lower.size() || !letter(lower[p + word.size()]))) return true;
    }
    return false;
}

class Policy {
    int mode = 0;
    std::string inText, outText;
    std::vector<std::string> optIn, optOut;
    static std::vector<std::string> ParseList(const std::string& text) {
        std::vector<std::string> result;
        size_t pos = 0;
        while (pos < text.size()) {
            pos = text.find_first_not_of(" \t\r\n,;", pos);
            if (pos == std::string::npos) break;
            const size_t end = text.find_first_of(" \t\r\n,;", pos);
            std::string path = EffectPath(text.substr(pos, end == std::string::npos ? end : end - pos));
            if (!path.empty()) result.push_back(path);
            pos = end == std::string::npos ? text.size() : end;
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
public:
    void Update(int newMode, const std::string& in, const std::string& out) {
        mode = newMode >= 0 && newMode <= 3 ? newMode : 0;
        if (inText != in) { inText = in; optIn = ParseList(in); }
        if (outText != out) { outText = out; optOut = ParseList(out); }
    }
    bool Enabled(const char* canonicalEffect) const {
        const auto contains = [canonicalEffect](const std::vector<std::string>& list) {
            const auto it = std::lower_bound(list.begin(), list.end(), canonicalEffect,
                [](const std::string& a, const char* b) { return a.compare(b) < 0; });
            return it != list.end() && *it == canonicalEffect;
        };
        if (mode == 1) return true;
        if (mode == 2) return contains(optIn);
        if (mode == 3) return !contains(optOut);
        return false;
    }
};

struct Profile {
    Material material = None;
    Evidence evidence = Rejected;
    float opticalDepth = 0.0f;
    float albedo[3] = {};
    float anisotropy = 0.0f;
    float radiusScale = 0.75f;
    float softness = 0.7f;
};

inline Profile Archetype(Material material, Evidence evidence) {
    Profile p;
    p.material = material;
    p.evidence = evidence;
    // Supplemental optical thickness with legacy sprites preserved, not a
    // measurement of real smoke. Emission is deliberately absent in stage 1.
    switch (material) {
    case DarkSmoke: p.opticalDepth = 0.10f; p.albedo[0] = p.albedo[1] = p.albedo[2] = 0.10f; p.anisotropy = 0.35f; break;
    case LightSmoke: p.opticalDepth = 0.08f; p.albedo[0] = p.albedo[1] = p.albedo[2] = 0.45f; p.anisotropy = 0.4f; break;
    case Mist: p.opticalDepth = 0.10f; p.albedo[0] = p.albedo[1] = p.albedo[2] = 0.90f; p.anisotropy = 0.55f; break;
    case DustCloud: p.opticalDepth = 0.06f; p.albedo[0] = 0.55f; p.albedo[1] = 0.45f; p.albedo[2] = 0.30f; p.anisotropy = 0.2f; break;
    case Gas: p.opticalDepth = 0.10f; p.albedo[0] = 0.10f; p.albedo[1] = 0.55f; p.albedo[2] = 0.18f; p.anisotropy = 0.35f; break;
    default: break;
    }
    return p;
}

inline float Extinction(float opticalDepth, float radius, float softness, float alpha) {
    if (!std::isfinite(radius) || !std::isfinite(alpha) || radius <= 0.0f) return 0.0f;
    // Renderer clamps proxy extents to >= 1 unit. Use that same radius when
    // converting optical thickness, so shrinking sprites cannot become dense.
    const float length = 2.0f * std::max(1.0f, radius) * (1.0f - softness * 0.5f);
    return opticalDepth * std::max(0.0f, std::min(1.0f, alpha)) / length;
}

struct Alternative {
    int handle = 0;
    std::string shader;
    Profile profile;
};

struct Input {
    std::string effect, name, shader;
    uint64_t fingerprint = 0;
    int ordinal = -1;
    bool unsafe = false;
    float minLife = 0, maxLife = 0, maxSize = 0, maxSpeed = 0, maxCount = 0;
    bool fades = false;
};

struct StockRule {
    const char* effect;
    uint64_t fingerprint;
    int ordinal;
    const char* shader;
    Material material;
};

#include "FxPhysicalizationStock.h"

inline Profile Analyze(const Input& input) {
    if (input.unsafe || input.shader.empty() || input.effect.empty()) return {};
    for (const StockRule& rule : stockRules) {
        if (input.fingerprint == rule.fingerprint && input.ordinal == rule.ordinal &&
            input.effect == rule.effect && input.shader == rule.shader)
            return Archetype(rule.material, StockSignature);
    }
    // Unknown content: require a known soft-cloud shader, a bounded, fading
    // puff AND an unambiguous material token. No generic steam/glow guessing.
    const std::string context = input.effect + " " + input.name;
    for (const char* exclusion : {"saber", "force", "spark", "muzzle", "shot", "flare", "glow", "blood", "explosion", "fire"})
        if (Token(context, exclusion)) return {};
    if (!std::isfinite(input.minLife) || !std::isfinite(input.maxLife) ||
        !std::isfinite(input.maxSize) || !std::isfinite(input.maxSpeed) ||
        !std::isfinite(input.maxCount)) return {};
    if (!input.fades || input.minLife < 500 || input.maxLife > 8000 || input.minLife > input.maxLife ||
        input.maxSize < 3 || input.maxSize > 96 || input.maxSpeed < 0 || input.maxSpeed > 100 ||
        input.maxCount <= 0 || input.maxCount > 16) return {};
    if (!Token(context, "smoke")) return {};
    if (input.shader == "gfx/misc/black_smoke" || input.shader == "gfx/misc/black_smoke2")
        return Archetype(DarkSmoke, HighConfidenceFamily);
    if (input.shader == "gfx/effects/alpha_smoke" || input.shader == "gfx/effects/alpha_smoke2")
        return Archetype(LightSmoke, HighConfidenceFamily);
    return {};
}

// Reject handle aliases with conflicting material roles; selecting the actual
// spawned shader never consumes RNG or requires an extra GetHandle() call.
inline Profile Resolve(const std::vector<Alternative>& alternatives, int handle) {
    if (handle <= 0) return {};
    Profile result;
    bool found = false;
    for (const Alternative& a : alternatives) {
        if (a.handle != handle) continue;
        if (found && (result.material != a.profile.material || result.evidence != a.profile.evidence)) return {};
        found = true;
        result = a.profile;
    }
    return result;
}

inline const char* MaterialName(Material material) {
    switch (material) {
    case DarkSmoke: return "dark smoke";
    case LightSmoke: return "light smoke";
    case Mist: return "mist/steam";
    case DustCloud: return "dust cloud";
    case Gas: return "gas";
    default: return "legacy";
    }
}

} // namespace FxPhysical
#endif
