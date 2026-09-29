// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Included after each frontend's FxScheduler.h. Keeps SP/MP analysis and
// attachment identical without depending on either frontend's parser types.
#ifndef FX_PHYSICALIZATION_INTEGRATION_H
#define FX_PHYSICALIZATION_INTEGRATION_H
#include "FxPhysicalizationEmission.h"
#include <map>

static FxPhysical::SourceContext FX_SourceForPrimitive(const CPrimitiveTemplate* fx) {
    FxPhysical::SourceContext source = theFxHelper.mPhysicalSources.Capture();
    if (fx->mFlags & FX_DEPTH_HACK) source.domain |= FxPhysical::SourceView;
#ifdef FX_PLAYER_VIEW
    if (fx->mFlags & FX_PLAYER_VIEW) source.domain |= FxPhysical::SourceView;
#endif
    if (fx->mFlags & FX_RELATIVE) source.domain |= FxPhysical::SourceAttached;
    if (fx->mFlags & FX_APPLY_PHYSICS) source.domain |= FxPhysical::SourcePhysics;
    return source;
}

static uint64_t FX_PhysicalFingerprint(const char* path, int limit = 65536) {
    fileHandle_t file;
    const int length = theFxHelper.OpenFile(path, &file, FS_READ);
    if (length < 0) return 0;
    if (length == 0 || length >= limit) { theFxHelper.CloseFile(file); return 0; }
    std::vector<char> bytes(length);
    theFxHelper.ReadFile(bytes.data(), length, file);
    theFxHelper.CloseFile(file);
    return FxPhysical::Fingerprint(bytes.data(), bytes.size());
}

static void FX_AnalyzeAdvanced(SEffectTemplate* effect, const std::vector<FxPhysical::Descriptor>& ds) {
    const bool composite = theFxHelper.PhysicalizationComposite();
    const bool adaptive = theFxHelper.PhysicalizationAdaptive();
    const bool emission = theFxHelper.PhysicalizationEmission();
    FxPhysical::Composition composition = FxPhysical::Compose(ds);
    std::vector<std::vector<FxPhysical::Profile>> added(ds.size());
    for (size_t i = 0; i < ds.size(); ++i) {
        effect->mPrimitives[i]->mPhysicalAdvancedReady = true;
        bool medium = false;
        for (size_t j = 0; j < ds[i].shaders.size(); ++j) {
            const FxPhysical::Profile p = composite ? FxPhysical::CompositeProfile(ds[i], ds[i].shaders[j], composition) : FxPhysical::Profile();
            added[i].push_back(p);
            medium = medium || ds[i].profiles[j].material != FxPhysical::None || p.material != FxPhysical::None;
        }
        if (medium && std::isfinite(ds[i].input.maxCount) && ds[i].input.maxCount > 0)
            composition.mediumCount += std::min(4096.0f, ds[i].input.maxCount);
    }
    // Cache only within this registration. Map reload rechecks changed files.
    std::map<std::string, uint64_t> hashes;
    const auto hash = [&hashes](const char* path) {
        auto it = hashes.find(path);
        if (it == hashes.end()) it = hashes.insert(std::make_pair(std::string(path), FX_PhysicalFingerprint(path, 2 * 1024 * 1024))).first;
        return it->second;
    };
    for (size_t i = 0; i < ds.size(); ++i) {
        if (theFxHelper.PhysicalizationDebug())
            theFxHelper.PhysicalizationPrint("FX composition: %s #%d -> %s; lit burst %d, automatic batch count %.1f\n",
                ds[i].input.effect.c_str(), int(i), FxPhysical::RoleName(FxPhysical::Describe(ds[i])),
                int(composition.litBurst), composition.mediumCount);
        for (size_t j = 0; j < ds[i].shaders.size(); ++j) {
            FxPhysical::AdvancedProfile advanced;
            // Existing exact/generic candidates always retain stage-1 behavior.
            if (ds[i].profiles[j].material == FxPhysical::None) advanced.composite = added[i][j];
            const FxPhysical::Profile base = ds[i].profiles[j].material != FxPhysical::None ? ds[i].profiles[j] : advanced.composite;
            const FxPhysical::ShaderShape* shape = adaptive && base.material != FxPhysical::None ? FxPhysical::Shape(ds[i].shaders[j]) : nullptr;
            if (shape && hash(shape->texture) == shape->textureHash && hash(shape->script) == shape->scriptHash) {
                advanced.adaptive = FxPhysical::Calibrate(base, *shape, ds[i], composition.mediumCount);
                advanced.adaptiveForComposite = advanced.composite.material != FxPhysical::None;
            }
            const FxPhysical::EmissionRule* glow = emission ? FxPhysical::EmissionCandidate(ds[i], ds[i].shaders[j]) : nullptr;
            if (glow) {
                bool valid = true;
                for (unsigned a = glow->firstAsset; a < glow->firstAsset + glow->numAssets; ++a)
                    if (hash(FxPhysical::emissionAssets[a].path) != FxPhysical::emissionAssets[a].fingerprint) valid = false;
                if (valid) advanced.emissionRadiance = glow->radiance;
            }
            if (advanced.composite.material != FxPhysical::None || advanced.adaptive.material != FxPhysical::None || advanced.emissionRadiance > 0)
                effect->mPrimitives[i]->mPhysicalShaders[j].advanced = std::make_shared<FxPhysical::AdvancedProfile>(advanced);
            if (theFxHelper.PhysicalizationDebug())
                theFxHelper.PhysicalizationPrint("FX advanced: %s #%d %s composite %d adaptive %d tau %.5f radius %.3f softness %.3f emission %d\n",
                    ds[i].input.effect.c_str(), int(i), ds[i].shaders[j].c_str(),
                    int(advanced.composite.material != FxPhysical::None), int(advanced.adaptive.material != FxPhysical::None),
                    advanced.adaptive.opticalDepth, advanced.adaptive.radiusScale, advanced.adaptive.softness, int(advanced.emissionRadiance > 0));
        }
    }
}

static void FX_AnalyzePhysicalization(SEffectTemplate* effect, const char* path, uint64_t fingerprint) {
    const std::string canonical = FxPhysical::EffectPath(path);
    const bool advanced = theFxHelper.PhysicalizationComposite() || theFxHelper.PhysicalizationAdaptive() || theFxHelper.PhysicalizationEmission();
    std::vector<FxPhysical::Descriptor> descriptors;
    if (advanced) descriptors.reserve(effect->mPrimitiveCount);
    for (int ordinal = 0; ordinal < effect->mPrimitiveCount; ++ordinal) {
        CPrimitiveTemplate* prim = effect->mPrimitives[ordinal];
        prim->mPhysicalEffect = canonical;
        const bool particle = prim->mType == Particle || prim->mType == OrientedParticle;
        FxPhysical::Input input;
        input.effect = canonical;
        input.name = prim->mName;
        input.fingerprint = fingerprint;
        input.ordinal = ordinal;
        input.unsafe = !particle || prim->mVolMedia ||
            (prim->mFlags & (FX_DEPTH_HACK | FX_APPLY_PHYSICS | FX_RELATIVE));
#ifdef FX_PLAYER_VIEW
        input.unsafe = input.unsafe || (prim->mFlags & FX_PLAYER_VIEW);
#endif
        input.minLife = std::min(prim->mLife.GetMin(), prim->mLife.GetMax());
        input.maxLife = std::max(prim->mLife.GetMin(), prim->mLife.GetMax());
        input.maxCount = std::max(prim->mSpawnCount.GetMin(), prim->mSpawnCount.GetMax());
        input.maxSize = std::max(std::max(prim->mSizeStart.GetMin(), prim->mSizeStart.GetMax()),
                                 std::max(prim->mSizeEnd.GetMin(), prim->mSizeEnd.GetMax()));
        const float vx = std::max(std::fabs(prim->mVelX.GetMin()), std::fabs(prim->mVelX.GetMax()));
        const float vy = std::max(std::fabs(prim->mVelY.GetMin()), std::fabs(prim->mVelY.GetMax()));
        const float vz = std::max(std::fabs(prim->mVelZ.GetMin()), std::fabs(prim->mVelZ.GetMax()));
        input.maxSpeed = std::sqrt(vx * vx + vy * vy + vz * vz);
        input.fades = (prim->mFlags & FX_ALPHA_LINEAR) &&
            std::max(prim->mAlphaEnd.GetMin(), prim->mAlphaEnd.GetMax()) <= 0.05f;
        // Unknown wave size/alpha, radial/random axis spawning or arbitrary
        // acceleration are not high-confidence generic soft puffs.
        const bool genericUnsafe = (prim->mFlags & FX_SIZE_PARM_MASK) ||
            (prim->mFlags & FX_ALPHA_PARM_MASK) || prim->mSpawnFlags ||
            !std::isfinite(prim->mGravity.GetMin()) || !std::isfinite(prim->mGravity.GetMax()) ||
            std::fabs(prim->mGravity.GetMin()) > 100 || std::fabs(prim->mGravity.GetMax()) > 100 ||
            !std::isfinite(prim->mAccelX.GetMin()) || !std::isfinite(prim->mAccelX.GetMax()) ||
            !std::isfinite(prim->mAccelY.GetMin()) || !std::isfinite(prim->mAccelY.GetMax()) ||
            !std::isfinite(prim->mAccelZ.GetMin()) || !std::isfinite(prim->mAccelZ.GetMax()) ||
            std::fabs(prim->mAccelX.GetMin()) > 100 || std::fabs(prim->mAccelX.GetMax()) > 100 ||
            std::fabs(prim->mAccelY.GetMin()) > 100 || std::fabs(prim->mAccelY.GetMax()) > 100 ||
            std::fabs(prim->mAccelZ.GetMin()) > 100 || std::fabs(prim->mAccelZ.GetMax()) > 100;
        for (FxPhysical::Alternative& alternative : prim->mPhysicalShaders) {
            input.shader = alternative.shader;
            alternative.profile = FxPhysical::Analyze(input);
            if (genericUnsafe && alternative.profile.evidence == FxPhysical::HighConfidenceFamily)
                alternative.profile = {};
            if (theFxHelper.PhysicalizationDebug())
                theFxHelper.PhysicalizationPrint("FX physicalization: %s #%d %s shader %s -> %s (%s)\n",
                    canonical.c_str(), ordinal, prim->mName, alternative.shader.c_str(),
                    FxPhysical::MaterialName(alternative.profile.material),
                    alternative.profile.evidence == FxPhysical::StockSignature ? "stock signature" :
                    alternative.profile.evidence == FxPhysical::HighConfidenceFamily ? "high-confidence family" : "conservative fallback");
        }
        if (advanced) {
            FxPhysical::Descriptor d;
            d.input = input; d.authored = prim->mVolMedia;
            d.genericUnsafe = genericUnsafe || (prim->mFlags & (FX_ALPHA_RAND | FX_RGB_RAND | FX_SIZE_RAND));
            d.kind = particle ? FxPhysical::SpriteKind : prim->mType == Light ? FxPhysical::LightKind :
                prim->mType == ScreenFlash ? FxPhysical::FlashKind :
                (prim->mType == FxRunner || prim->mType == Emitter) ? FxPhysical::ChildKind : FxPhysical::OtherKind;
            d.startSize = std::max(prim->mSizeStart.GetMin(), prim->mSizeStart.GetMax());
            for (const FxPhysical::Alternative& a : prim->mPhysicalShaders) {
                d.shaders.push_back(a.shader); d.profiles.push_back(a.profile);
            }
            descriptors.push_back(d);
        }
    }
    if (advanced) FX_AnalyzeAdvanced(effect, descriptors);
}

static void FX_AttachPhysicalization(CParticle* particle, const CPrimitiveTemplate* prim) {
    if (!particle || prim->mVolMedia) return;
    FxPhysical::Profile profile = FxPhysical::Resolve(prim->mPhysicalShaders, particle->GetShader());
    const FxPhysical::AdvancedProfile advanced = prim->mPhysicalAdvancedReady ?
        FxPhysical::ResolveAdvanced(prim->mPhysicalShaders, particle->GetShader()) : FxPhysical::AdvancedProfile();
    const bool compositeOnly = profile.material == FxPhysical::None && advanced.composite.material != FxPhysical::None;
    if (compositeOnly) profile = advanced.composite;
    if ((profile.material == FxPhysical::None && advanced.emissionRadiance <= 0) || prim->mPhysicalEffect.empty()) return;
    SFxVolumetricMedia media = {};
    media.opticalDepth = profile.opticalDepth;
    media.autoCompositeOnly = compositeOnly;
    media.autoEmissionRadiance = advanced.emissionRadiance;
    if (advanced.adaptive.material != FxPhysical::None && advanced.adaptiveForComposite == compositeOnly) {
        media.adaptiveOpticalDepth = advanced.adaptive.opticalDepth;
        media.adaptiveRadiusScale = advanced.adaptive.radiusScale;
        media.adaptiveSoftness = advanced.adaptive.softness;
    }
    Q_strncpyz(media.autoEffect, prim->mPhysicalEffect.c_str(), sizeof(media.autoEffect));
    for (int i = 0; i < 3; ++i) { media.albedo[i] = profile.albedo[i]; media.aspect[i] = 1.0f; }
    media.radiusScale = profile.radiusScale;
    media.softness = profile.softness;
    if (profile.material == FxPhysical::None && advanced.emissionRadiance > 0) { media.radiusScale = .45f; media.softness = .8f; }
    media.hasAnisotropy = true;
    media.anisotropy = profile.anisotropy;
    // Attach while off too: a live policy toggle affects already living puffs.
    // No template pointers or extra random samples survive here.
    particle->SetVolumetricMedia(&media);
}

#endif
