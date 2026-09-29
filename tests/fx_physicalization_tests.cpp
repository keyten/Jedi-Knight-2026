// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Standalone C++11 regression harness: no game/assets/renderer required.
#include "fx/FxPhysicalization.h"
#include "fx/FxPhysicalizationEmission.h"
#include "fx/FxPhysicalizationAggregate.h"
#include "fx/FxPhysicalizationSources.h"
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
    Check(Strength(1) == 1 && Strength(8) == 8, "optical strength preserves valid calibration");
    Check(Strength(-1) == 0 && Strength(100) == 16, "optical strength bounded");
    Check(Strength(std::numeric_limits<float>::quiet_NaN()) == 0, "nonfinite optical strength disabled");
    Descriptor puff;
    puff.kind = SpriteKind; puff.input = generic;
    puff.input.effect = EffectPath("mods/explosion"); puff.input.name = "Smoke";
    puff.input.maxCount = 4; puff.input.maxSize = 32; puff.startSize = 8;
    puff.shaders = {"gfx/misc/black_smoke"}; puff.profiles = {Analyze(puff.input)};
    Check(puff.profiles[0].material == None, "stage 1 excludes explosion context");
    Descriptor light, fire, child, dots;
    light.kind = LightKind; light.input.maxLife = 500; light.input.maxCount = 1; light.input.maxSize = 300;
    fire.kind = SpriteKind; fire.input.maxLife = 1000; fire.input.maxCount = 1;
    fire.shaders = {"gfx/exp/rocket_explosion", "gfx/exp/explosion1"};
    child.kind = ChildKind;
    dots.kind = SpriteKind; dots.shaders = {"gfx/misc/dotfill_a"};
    const Composition scene = Compose({puff, light, fire, child, dots});
    Check(scene.litBurst && scene.roles[LightRole] == 1 && scene.roles[FireballRole] == 1 &&
          scene.roles[ChildRole] == 1 && scene.roles[DiscreteRole] == 1, "compound roles separate media/light/fire/children/points");
    const Profile contextual = CompositeProfile(puff, puff.shaders[0], scene);
    Check(contextual.material == DarkSmoke && contextual.evidence == HighConfidenceComposite, "high-confidence smoke in lit explosion");
    Check(CompositeProfile(puff, puff.shaders[0], Compose({puff, light})).material == None, "light alone cannot relax exclusions");
    Check(CompositeProfile(puff, puff.shaders[0], Compose({puff, fire})).material == None, "flash alone cannot relax exclusions");
    Check(CompositeProfile(puff, "gfx/misc/steam", scene).material == None, "composite cannot guess steam");
    puff.authored = true;
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == None && Describe(puff) == AuthoredRole, "authored medium protected");
    puff.authored = false; puff.genericUnsafe = true;
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == None, "unknown parametric spawn rejected");
    puff.genericUnsafe = false; puff.input.unsafe = true;
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == None, "view/physics veto beats context");
    puff.input.unsafe = false; puff.input.effect = EffectPath("mods/muzzle_smoke");
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == None, "muzzle never promoted by sibling light");
    puff.input.effect = EffectPath("mods/explosion"); puff.input.name = "Glow";
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == None, "known cloud shader alone not enough");
    puff.input.name = "LingeringSmoke";
    Check(CompositeProfile(puff, puff.shaders[0], scene).material == DarkSmoke, "explicit lingering smoke name");
    light.input.maxSize = 0;
    Check(!Compose({light, fire}).litBurst, "zero-radius light not burst evidence");
    fire.shaders.push_back("gfx/misc/steam");
    Check(Describe(fire) == UnknownRole, "mixed shader alternatives are not a fireball");

    for (const ShaderShape& shape : shaderShapes) {
        Check(Shape(shape.shader) == &shape && shape.textureHash && shape.scriptHash, "texture metadata guarded by both hashes");
        const Profile calibrated = Calibrate(contextual, shape, puff, 4);
        Check(calibrated.material == DarkSmoke && calibrated.opticalDepth >= contextual.opticalDepth * .25f &&
            calibrated.opticalDepth <= contextual.opticalDepth * 1.5f && calibrated.radiusScale >= .45f &&
            calibrated.radiusScale <= .95f && calibrated.softness >= .55f && calibrated.softness <= .85f, "calibration bounded");
        Check(Calibrate(contextual, shape, puff, 16).opticalDepth <= calibrated.opticalDepth, "burst count controls additional density");
        Check(Calibrate({}, shape, puff, 4).material == None, "metadata cannot classify rejected effect");
        Check(Calibrate(contextual, shape, puff, 0).material == None, "invalid population falls back");
    }
    Check(!Shape("gfx/misc/dotfill_a"), "points have no adaptive cloud shape");
    puff.startSize = std::numeric_limits<float>::quiet_NaN();
    Check(Calibrate(contextual, shaderShapes[0], puff, 4).material == None, "nonfinite calibration input rejected");
    puff.startSize = 8;
    smoke.advanced = std::make_shared<AdvancedProfile>();
    auto advanced = std::make_shared<AdvancedProfile>();
    advanced->composite = contextual; advanced->adaptive = Calibrate(contextual, shaderShapes[0], puff, 4);
    advanced->adaptiveForComposite = true; smoke.advanced = advanced;
    alternatives = {smoke};
    Check(ResolveAdvanced(alternatives, smoke.handle).composite.material == DarkSmoke, "actual handle chooses advanced profile");
    sparkle.handle = smoke.handle; alternatives.push_back(sparkle);
    Check(ResolveAdvanced(alternatives, smoke.handle).composite.material == None, "advanced aliases fail closed");
    std::srand(81); const int rng = std::rand();
    std::srand(81); Compose({puff, light, fire}); CompositeProfile(puff, puff.shaders[0], scene);
    Calibrate(contextual, shaderShapes[0], puff, 4); ResolveAdvanced(alternatives, smoke.handle);
    Check(std::rand() == rng, "advanced analysis never consumes RNG");
    for (const EmissionRule& rule : emissionRules) {
        Descriptor d; d.kind = SpriteKind; d.input.effect = rule.effect;
        d.input.fingerprint = rule.fingerprint; d.input.ordinal = rule.ordinal;
        Check(EmissionCandidate(d, rule.shader) == &rule, "reviewed emission exact match");
        d.input.fingerprint ^= 1;
        Check(!EmissionCandidate(d, rule.shader), "modified effect never gets stale glow");
        d.input.fingerprint = rule.fingerprint; d.authored = true;
        Check(!EmissionCandidate(d, rule.shader), "authored emission takes precedence");
        d.authored = false; d.input.unsafe = true;
        Check(!EmissionCandidate(d, rule.shader), "view/physics emission veto");
        Check(rule.numAssets > 0 && rule.firstAsset + rule.numAssets <= sizeof(emissionAssets)/sizeof(emissionAssets[0]), "emission guard range bounded");
    }
    Alternative glowA, glowB; glowA.handle = glowB.handle = 9;
    auto ga = std::make_shared<AdvancedProfile>(); ga->emissionRadiance = .18f; glowA.advanced = ga;
    Check(ResolveAdvanced({glowA, glowB}, 9).emissionRadiance == 0, "glow/unknown shader aliases fail closed");
    auto gb = std::make_shared<AdvancedProfile>(); gb->emissionRadiance = .12f; glowB.advanced = gb;
    Check(ResolveAdvanced({glowA, glowB}, 9).emissionRadiance == 0, "different glow aliases fail closed");
    gb->emissionRadiance = ga->emissionRadiance;
    Check(ResolveAdvanced({glowA, glowB}, 9).emissionRadiance == .18f, "identical glow aliases remain deterministic");
    Check(!EmissionCandidate(puff, "gfx/misc/steam"), "orange steam is not fire");
    Check(!EmissionCandidate(puff, "gfx/effects/fire2"), "unknown flame receives no emission");
    Check(EmissionEnvelope(0, 1000) == 0 && EmissionEnvelope(1000, 1000) == 0 &&
          EmissionEnvelope(-1, 1000) == 0, "glow birth/end/invalid time");
    Check(EmissionEnvelope(100, 1000) > EmissionEnvelope(800, 1000), "glow drains conservatively");
    Check(EmissionEnvelope(100, 0) == 0 && EmissionEnvelope(100, std::numeric_limits<float>::quiet_NaN()) == 0, "nonfinite glow time fails closed");
    Check(LinearTint(0) == 0 && LinearTint(255) == 1 && LinearTint(128) < .3f, "bounded linear RGB tint");
    struct Eval {
        int id; bool automaticDensity;
        float center[3], invExtent[3], radius, extinction, inner, albedo[3], emission[3], anisotropy;
    };
    Eval one = {}; one.id = 3; one.automaticDensity = true; one.radius = 10;
    one.extinction = .02f; one.inner = .3f;
    for (int i = 0; i < 3; ++i) { one.invExtent[i] = .1f; one.albedo[i] = .2f; }
    Eval two = one; two.id = 1; two.extinction = .03f;
    Eval cluster[] = {one, two};
    Check(CoalesceExact(cluster, 2) == 1 && cluster[0].id == 1 && std::fabs(cluster[0].extinction-.05f) < 1e-7f, "one exact density owner and deterministic id");
    Check(cluster[0].radius == one.radius && cluster[0].center[0] == one.center[0], "merge never expands support");
    const float samples[] = {0.0f, .5f, .9f, 1.0f};
    for (float shape : samples) Check(std::fabs((one.extinction+two.extinction)*shape - cluster[0].extinction*shape) < 1e-7f, "pointwise extinction conserved");
    two.center[0] = .001f; Eval separated[] = {one,two};
    Check(CoalesceExact(separated, 2) == 2, "even tiny gaps are never bridged");
    two = one; two.id = 1; two.automaticDensity = false; Eval authored[] = {one,two};
    Check(CoalesceExact(authored, 2) == 2, "authored medium cannot be coalesced");
    two.automaticDensity = true; two.emission[0] = .1f; Eval emits[] = {one,two};
    Check(CoalesceExact(emits, 2) == 2, "emissive media excluded from coalescing");
    two = one; two.albedo[0] = .21f; Eval colored[] = {one,two};
    Check(CoalesceExact(colored, 2) == 2, "different optical responses excluded");
    two = one; two.extinction = std::numeric_limits<float>::infinity(); Eval invalid[] = {one,two};
    Check(CoalesceExact(invalid, 2) == 2, "nonfinite density never merged");
    two = one; two.id = 1; Eval reordered[] = {two,one};
    Check(CoalesceExact(reordered, 2) == 1 && reordered[0].id == cluster[0].id && reordered[0].extinction == .04f, "input order does not choose representative");
    std::srand(91); const int unchanged = std::rand(); std::srand(91);
    EmissionCandidate(puff, "gfx/effects/fire2"); EmissionEnvelope(100,1000); CoalesceExact(reordered, 1);
    Check(std::rand() == unchanged, "emission/coalescing do not consume RNG");
    SourceTracker sources;
    Check(!sources.New(SourceWorld).id && !sources.Capture().id, "source tracking defaults off");
    sources.Enable(true);
    SourceContext burst = sources.Play(SourceWorld), other = sources.Play(SourceWorld);
    Check(burst.id && other.id != burst.id && burst.generation == other.generation, "independent coincident bursts have distinct owners");
    SourceContext delayed;
    {
        SourceTracker::Scope parent(sources, burst);
        Check(sources.Capture().id == burst.id && sources.Play(SourceWorld).id == burst.id, "synchronous child inherits owner");
        delayed = sources.Capture();
        SourceContext attached = sources.Play(SourceAttached | SourcePhysics);
        Check(attached.id == burst.id && attached.domain == (SourceAttached | SourcePhysics), "unsafe child retains owner and restrictions");
        {
            SourceTracker::Scope child(sources, attached);
            Check(sources.Play(SourceWorld).domain == attached.domain, "descendants cannot clear unsafe restrictions");
            Check(!sources.Play(SourcePortal).id, "portal boundary fails closed");
        }
        Check(sources.Capture().domain == SourceWorld, "child scope restores parent domain");
    }
    Check(!sources.Capture().id, "root scope restores empty context");
    {
        SourceTracker::Scope scheduled(sources, delayed);
        Check(sources.Play(SourceWorld).id == burst.id, "delayed creation keeps birth owner");
        SourceTracker::Scope empty(sources, SourceContext());
        Check(!sources.Play(SourceWorld).id, "untracked direct primitive never invents a parent");
    }
    sources.Reset();
    Check(!sources.Valid(delayed), "clean/load/time reset invalidates old particles and schedules");
    {
        SourceTracker::Scope stale(sources, delayed);
        Check(!sources.Play(SourceWorld).id, "stale deferred child fails closed after reset");
    }
    SourceContext renewed = sources.Play(SourceAttached);
    Check(renewed.id && renewed.generation != burst.generation, "restored loop starts in new generation");
    sources.Enable(false); sources.Enable(true);
    Check(!sources.Valid(renewed), "live disable/enable cannot reconnect old sources");
    {
        SourceTracker::Scope parent(sources, sources.Play(SourceWorld));
        sources.Reset();
    }
    Check(!sources.Capture().id, "reset during scope never restores a valid old owner");
    SourceContext portal = sources.Play(SourcePortal);
    {
        SourceTracker::Scope scoped(sources, portal);
        Check(sources.Play(SourcePortal).id == portal.id, "portal child stays in own scope");
    }
    std::srand(123); const int sourceRng = std::rand(); std::srand(123);
    sources.New(SourceWorld); sources.Reset(); sources.Enable(false);
    Check(std::rand() == sourceRng, "tracking never consumes legacy RNG");
    SourceLedger ledger;
    ledger.Begin(burst.generation, 20);
    ledger.Submit(burst, false); ledger.Submit(burst, false); ledger.Submit(burst, true);
    ledger.Submit(other, false); ledger.Submit(SourceContext(), false);
    auto rows = ledger.Rows();
    Check(rows.size() == 2 && rows[0].density == 2 && rows[0].glow == 1 && ledger.untracked == 1,
        "ledger counts real submissions by owner without inventing untracked sources");
    SourceContext restricted = burst; restricted.domain = SourcePhysics;
    ledger.Submit(restricted, false);
    Check(ledger.Rows().size() == 3, "unsafe domains have separate ledger rows");
    ledger.Begin(other.generation, 30);
    Check(ledger.Rows().empty() && !ledger.untracked && ledger.time == 30, "new scene clears the snapshot and startup history");
    for (int i=0; i<1201; ++i) ledger.Submit(other, false);
    Check(ledger.Rows()[0].density == 1200 && ledger.overflow == 1, "ledger stays bounded and exposes dropped diagnostics");
    std::cout << "PASS: " << checks << " policy/classification/optics/composition checks\n";
    return 0;
}
