// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Included after each frontend's FxScheduler.h. Keeps SP/MP analysis and
// attachment identical without depending on either frontend's parser types.
#ifndef FX_PHYSICALIZATION_INTEGRATION_H
#define FX_PHYSICALIZATION_INTEGRATION_H

static uint64_t FX_PhysicalFingerprint(const char* path) {
    fileHandle_t file;
    const int length = theFxHelper.OpenFile(path, &file, FS_READ);
    if (length < 0) return 0;
    if (length == 0 || length >= 65536) { theFxHelper.CloseFile(file); return 0; }
    std::vector<char> bytes(length);
    theFxHelper.ReadFile(bytes.data(), length, file);
    theFxHelper.CloseFile(file);
    return FxPhysical::Fingerprint(bytes.data(), bytes.size());
}

static void FX_AnalyzePhysicalization(SEffectTemplate* effect, const char* path, uint64_t fingerprint) {
    const std::string canonical = FxPhysical::EffectPath(path);
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
    }
}

static void FX_AttachPhysicalization(CParticle* particle, const CPrimitiveTemplate* prim) {
    if (!particle || prim->mVolMedia) return;
    const FxPhysical::Profile profile = FxPhysical::Resolve(prim->mPhysicalShaders, particle->GetShader());
    if (profile.material == FxPhysical::None || prim->mPhysicalEffect.empty()) return;
    SFxVolumetricMedia media = {};
    media.opticalDepth = profile.opticalDepth;
    Q_strncpyz(media.autoEffect, prim->mPhysicalEffect.c_str(), sizeof(media.autoEffect));
    for (int i = 0; i < 3; ++i) { media.albedo[i] = profile.albedo[i]; media.aspect[i] = 1.0f; }
    media.radiusScale = profile.radiusScale;
    media.softness = profile.softness;
    media.hasAnisotropy = true;
    media.anisotropy = profile.anisotropy;
    // Attach while off too: a live policy toggle affects already living puffs.
    // No template pointers or extra random samples survive here.
    particle->SetVolumetricMedia(&media);
}

#endif
