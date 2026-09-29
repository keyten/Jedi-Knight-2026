// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Standalone C++11 regression harness: no game/assets/renderer required.
#include "fx/FxPhysicalization.h"
#include <cstdlib>
#include <iostream>
#include <limits>

static int checks = 0;
static void Check(bool condition, const char* what) {
    ++checks;
    if (!condition) { std::cerr << "FAILED: " << what << '\n'; std::exit(1); }
}

int main(int argc, char** argv) {
    using namespace FxPhysical;
    // Optional corpus protocol used by the Python asset replay below.
    if (argc > 1 && std::string(argv[1]) == "--corpus") {
        Input in;
        while (std::cin >> in.effect >> in.fingerprint >> in.ordinal >> in.shader) {
            const Profile p = Analyze(in);
            std::cout << int(p.material) << ' ' << int(p.evidence) << '\n';
        }
        return 0;
    }
    Check(EffectPath("ROCKET\\Explosion.EFX") == "effects/rocket/explosion.efx", "canonical paths");
    Check(ShaderPath("gfx/effects/Wcloud.TGA") == "gfx/effects/wcloud", "shader extension");
    Check(EffectPath("../smoke").empty() && EffectPath("smoke*").empty(), "no wildcard/parent paths");
    Check(Fingerprint("hello", 5) == 0xa430d84680aabd0bULL, "FNV-1a known vector");

    const char* a = "effects/rocket/explosion.efx";
    const char* b = "effects/volumetric/black_smoke.efx";
    const char* c = "effects/env/fire.efx";
    Policy policy;
    Check(!policy.Enabled(a), "default off");
    for (int mode = 0; mode <= 3; ++mode) {
        policy.Update(mode, "ROCKET\\Explosion.EFX, volumetric/black_smoke;rocket/explosion", "rocket/explosion");
        Check(policy.Enabled(a) == (mode == 1 || mode == 2), "four modes with both memberships");
        Check(policy.Enabled(b) == (mode != 0), "four modes with opt-in only");
        Check(policy.Enabled(c) == (mode == 1 || mode == 3), "four modes with no membership");
    }
    policy.Update(2, "env/fire", "");
    Check(policy.Enabled(c) && !policy.Enabled(a), "live list replacement");
    policy.Update(2, "", "");
    Check(!policy.Enabled(c), "live list clear");
    policy.Update(3, "", "env/fire");
    Check(!policy.Enabled(c) && policy.Enabled(a), "live opt-out update");
    policy.Update(99, "env/fire", "");
    Check(!policy.Enabled(c), "invalid mode fails closed");

    for (const StockRule& rule : stockRules) {
        Input input;
        input.effect = rule.effect; input.fingerprint = rule.fingerprint;
        input.ordinal = rule.ordinal; input.shader = rule.shader;
        const Profile p = Analyze(input);
        Check(p.material == rule.material && p.evidence == StockSignature, "stock rule matches exact content");
        input.unsafe = true;
        Check(Analyze(input).material == None, "explicit/view/physics veto beats stock");
        input.unsafe = false; input.fingerprint ^= 1;
        Check(Analyze(input).material == None, "stale exact signature rejected");
    }
    Input generic;
    generic.effect = EffectPath("mods/my_smoke");
    generic.shader = "gfx/misc/black_smoke";
    generic.minLife = 800; generic.maxLife = 1200;
    generic.maxSize = 24; generic.maxSpeed = 30; generic.maxCount = 5; generic.fades = true;
    Check(Analyze(generic).evidence == HighConfidenceFamily, "unknown high-confidence smoke");
    generic.effect = EffectPath("mods/muzzle_smoke");
    Check(Analyze(generic).material == None, "muzzle smoke generic exclusion");
    generic.effect = EffectPath("mods/my_smoke"); generic.shader = "gfx/misc/steam";
    Check(Analyze(generic).material == None, "steam reused for glow, no generic detection");
    generic.shader = "gfx/misc/black_smoke"; generic.maxSpeed = 700;
    Check(Analyze(generic).material == None, "fast streaks excluded");
    generic.maxSpeed = 30; generic.fades = false;
    Check(Analyze(generic).material == None, "unbounded fade excluded");
    generic.fades = true; generic.maxSpeed = std::numeric_limits<float>::quiet_NaN();
    Check(Analyze(generic).material == None, "nonfinite generic speed excluded");
    generic.maxSpeed = 30; generic.maxCount = 0;
    Check(Analyze(generic).material == None, "empty generic emitter excluded");

    Alternative smoke, sparkle;
    smoke.handle = 2; smoke.profile = Archetype(DarkSmoke, StockSignature);
    sparkle.handle = 3;
    std::vector<Alternative> alternatives = {smoke, sparkle};
    Check(Resolve(alternatives, 2).material == DarkSmoke, "chosen smoke shader");
    Check(Resolve(alternatives, 3).material == None, "chosen sparkle stays legacy");
    Check(Resolve(alternatives, 0).material == None, "invalid shader handle");
    sparkle.handle = 2; alternatives.push_back(sparkle);
    Check(Resolve(alternatives, 2).material == None, "conflicting handle alias rejected");
    std::srand(17); const int expected = std::rand();
    std::srand(17); Analyze(generic); Resolve(alternatives, 2);
    Check(std::rand() == expected, "analysis/resolution do not consume RNG");

    for (float radius : {1.0f, 4.0f, 24.0f, 128.0f}) {
        const float sigma = Extinction(0.1f, radius, 0.7f, 0.6f);
        const float tau = sigma * 2 * radius * (1 - 0.7f / 2);
        Check(std::fabs(tau - 0.06f) < 1e-6f, "size-independent optical thickness");
    }
    Check(Extinction(0.1f, 10, 0.7f, 0) == 0, "expired alpha emits no medium");
    Check(Extinction(0.1f, -1, 0.7f, 1) == 0, "negative radius rejected");
    Check(Extinction(0.1f, std::numeric_limits<float>::quiet_NaN(), 0.7f, 1) == 0, "NaN radius rejected");
    Check(Extinction(0.1f, 10, 0.7f, std::numeric_limits<float>::infinity()) == 0, "infinite alpha rejected");
    std::cout << "PASS: " << checks << " policy/classification/optics checks\n";
    return 0;
}
