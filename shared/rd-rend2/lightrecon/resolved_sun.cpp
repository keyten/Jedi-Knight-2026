// SPDX-License-Identifier: GPL-2.0-or-later
#include "resolved_sun.h"
#include "photometry.h"
#include "contributions.h"
#include <cmath>
#include "../tr_pbr_contract.h"

namespace Rend2LightRecon { namespace SunLighting {
namespace {
bool Admission(const ReconstructedLightingAsset& a, Id id) {
    bool found = false;
    for (const auto& admission : a.assessment.admissions) if (admission.source == id &&
        admission.use == SourceUse::ExactEmitter) {
        if (admission.status != AssessmentStatus::Validated) return false;
        found = true;
    }
    return found;
}
// Only an actual P06 composition proof allows an additive diffuse channel.
// These domains identify the renderer's ORIGINAL baseline, not an arbitrary field.
bool CanCombine(const ReconstructedLightingAsset& a, Id sun, Id calibration, const char* domain) {
    const ContributionReference *direct = nullptr, *base = nullptr;
    for (const auto& c : a.contributions) {
        if (c.domain != domain) continue;
        if (c.sources.size() == 1 && c.sources[0] == sun && c.membership.directSun == FieldMembership::Included) {
            if (direct) return false;
            direct = &c;
        } else {
            if (base) return false;
            base = &c;
        }
    }
    if (!direct || !base || !base->completeSampleCoverage || !direct->completeSampleCoverage ||
        direct->sources.size() != 1 || direct->sources[0] != sun ||
        direct->component != ContributionComponent::Direct ||
        direct->quantity != ContributionQuantity::IrradianceRGB ||
        direct->unit != PhotometricUnit::Lux || direct->calibration != calibration ||
        direct->membership.colour != ContributionColour::LinearRec709D65 ||
        direct->membership.directSun != FieldMembership::Included ||
        base->membership.directSun != FieldMembership::Excluded ||
        direct->configuration != a.assessment.selectedConfiguration ||
        base->configuration != direct->configuration ||
        direct->membership.geometry != a.assessment.geometryKey ||
        base->membership.geometry != a.assessment.geometryKey ||
        direct->membership.profile != a.assessment.selectedProfile ||
        base->membership.profile != a.assessment.selectedProfile) return false;
    // Algebraic disjointness alone is not a certificate for the original GPU
    // baseline. Require a separately scoped, exact runtime-operator receipt.
    const auto& proof = base->replacement;
    if (proof.supported != ReplacementAllProofs || proof.contradicted ||
        proof.runtimeOperator != "rend2/original-baked-linear/v1" || proof.scope != domain ||
        !proof.maximumAbsoluteError.known || proof.maximumAbsoluteError.value != 0 ||
        !proof.leakageRisk.known || proof.leakageRisk.value != 0) return false;
    Core::Attribution::Component x, y; x.dataset = direct->dataset; y.dataset = base->dataset;
    return bool(Core::Attribution::CanAdd(a, x, y));
}
}
State Resolve(const Snapshot& snapshot) {
    State r; r.generation = snapshot.Generation();
    const auto sun = snapshot.Sun(); r.status = sun.status;
    const auto asset = snapshot.Asset();
    if (!asset) return r;
    const auto& a = *asset.value;
    if (sun.status == Status::Ambiguous) { r.reason = Reason::Ambiguous; return r; }
    const Id resolvedGroup = sun ? sun.value.emitter->source.hypothesisGroup : 0;
    size_t unresolved = 0;
    bool unknownDirection = false;
    for (const auto& hypothesis : a.suns) {
        const auto source = snapshot.IdentifySource(hypothesis.emitter);
        if (!source) continue;
        const auto emitter = snapshot.GetEmitter(source.value);
        if (!emitter) continue;
        const auto& e = *emitter.value;
        if (e.source.state == HypothesisState::Resolved && !e.frame.known) unknownDirection = true;
        if (e.source.state != HypothesisState::Candidate ||
            (resolvedGroup && resolvedGroup == e.source.hypothesisGroup)) continue;
        ++unresolved;
        if (e.emission.unit == PhotometricUnit::Relative) r.kind = Kind::ConditionalRelative;
    }
    if (unresolved > 1 || (sun && unresolved)) {
        r.reason = Reason::Ambiguous; r.status = Status::Ambiguous; return r;
    }
    if (!sun) {
        r.reason = unknownDirection ? Reason::UnknownDirection : Reason::NoResolvedSun;
        if (r.kind == Kind::Unavailable) {
            for (const auto& field : a.transport) {
                if (field.quantity == FieldQuantity::ApproximateAppearance &&
                    field.source.state != HypothesisState::Rejected) {
                    r.kind = Kind::ApproximateAppearance;
                    break;
                }
            }
        }
        return r;
    }
    const auto& e = *sun.value.emitter;
    r.source = e.source.id; r.provenance = e.source.provenance; r.calibration = e.emission.calibration;
    r.directionToSun = sun.value.directionToSun; r.photonDirection = e.frame.forward;
    r.confidenceKnown = e.source.evidence.physicalEmitterConfidence.known;
    r.confidence = e.source.evidence.physicalEmitterConfidence.value;
    r.angularRadiusKnown = e.geometry.angularRadiusRadians.known;
    r.angularRadius = e.geometry.angularRadiusRadians.value;
    const Calibration* calibration = nullptr;
    const Provenance* provenance = nullptr;
    for (const auto& c : a.calibrations) if (c.id == r.calibration) calibration = &c;
    for (const auto& p : a.provenanceRecords) if (p.id == r.provenance) provenance = &p;
    r.reason = Reason::UnsupportedPhotometry;
    if (calibration && calibration->kind == CalibrationKind::LegacySceneEquivalent) {
        r.kind = Kind::LegacySceneEquivalent; return r;
    }
    if (!calibration || calibration->kind == CalibrationKind::UncalibratedRelative ||
        e.emission.unit != PhotometricUnit::Lux) { r.kind = Kind::ConditionalRelative; return r; }
    const bool authored = provenance && (provenance->kind == ProvenanceKind::Authored || provenance->kind == ProvenanceKind::External);
    r.kind = authored ? Kind::AuthoredPhysical : Kind::ReconstructedPhysical;
    r.reason = Reason::Unsuitable;
    if (e.source.evidence.exactLight != Suitability::Candidate ||
        e.source.evidence.observationIssues || !e.source.regions.empty()) return r;
    r.reason = Reason::Configuration;
    bool configuration = false;
    for (const auto& c : a.lightingConfigurations) if (c.id == a.assessment.selectedConfiguration &&
        !c.profile.empty() && c.profile == a.assessment.selectedProfile) configuration = true;
    if (!a.assessment.present || !configuration || a.assessment.geometryKey.empty()) return r;
    r.reason = Reason::Uncertainty;
    if (r.confidenceKnown && r.confidence < 0.95f) return r;
    if (!authored && (!Admission(a, r.source) || a.assessment.sunEvidence == AssessmentStatus::Conditional)) return r;
    bool supportedUncertainty = authored;
    for (const auto& u : a.uncertainty) if (u.source == r.source) {
        if (!u.complete || !u.absoluteScaleKnown || u.exactLight != Suitability::Candidate) return r;
        for (const auto& factor : u.factors) if (factor.status != UncertaintyStatus::Supported) return r;
        if (!authored) {
            const auto& confidence = u.exactUse;
            if (confidence.meaning != ConfidenceMeaning::IndependentEmpiricalBinRate ||
                confidence.event != "rend2/directional-sun:0.25deg-5pct" ||
                !confidence.probability.known || !confidence.probability.bounded ||
                confidence.probability.lower < 0.95f) return r;
            bool scoped = false;
            for (Id configuration : u.configurations) scoped = scoped || configuration == a.assessment.selectedConfiguration;
            if (!scoped || !u.directionSpreadRadians.known || u.directionSpreadRadians.value > 0.004363323f ||
                !e.emission.intensity.bounded || e.emission.intensity.lower < 0.95f * e.emission.intensity.value ||
                e.emission.intensity.upper > 1.05f * e.emission.intensity.value) return r;
            r.confidenceKnown = true; r.confidence = confidence.probability.value;
        }
        supportedUncertainty = true;
    }
    if (!supportedUncertainty) return r;
    const auto emission = Photometric::Emission(a, e);
    r.reason = Reason::UnsupportedPhotometry;
    if (emission.code != Photometric::Code::Ok ||
        emission.value.quantity != PhotometricQuantity::DirectionalIlluminance ||
        !Photometric::RequirePhysical(a, emission.value)) return r;
    r.illuminanceLux = emission.value.rgb;
    r.worldDiffuse = CanCombine(a, r.source, r.calibration, "rend2/world-diffuse");
    r.entityDiffuse = CanCombine(a, r.source, r.calibration, "rend2/entity-grid");
    r.volumetricDirect = CanCombine(a, r.source, r.calibration, "rend2/fog-grid");
    r.policy = Policy::Canonical; r.canonical = r.available = true;
    r.reason = Reason::Accepted; r.status = Status::Ok;
    return r;
}
State Select(const State& candidate, const PolicyInput& p) {
    State r; r.reason = candidate.reason; r.status = candidate.status; r.generation = candidate.generation;
    r.rejectedKind = candidate.kind; r.available = p.shaderSun || p.forceSun || p.sceneOverride;
    r.kind = r.available ? Kind::LegacyRenderer : Kind::Unavailable;
    r.policy = p.shaderSun ? Policy::Shader : Policy::Fallback;
    r.explicitOverride = p.sceneOverride || p.forceSun;
    if (p.sceneOverride) { r.policy = Policy::Scene; r.reason = Reason::Scene; }
    else if (p.forceSun) { r.policy = Policy::Forced; r.reason = Reason::Forced; }
    else if (!p.master || !p.requested) r.reason = Reason::Disabled;
    else if (!p.world) r.reason = Reason::Worldless;
    else if (!p.physical) r.reason = Reason::PhysicalModeRequired;
    else if (!p.adapterReady) r.reason = Reason::FeatureUnavailable;
    else if (candidate.canonical) return candidate;
    return r;
}
bool SceneIlluminance(const State& s, float scale, Vec3& out) {
    if (!s.canonical || !std::isfinite(scale) || scale <= 0) return false;
    Vec3 v(Rend2PBR::PhysicalToScene(s.illuminanceLux.x, scale),
        Rend2PBR::PhysicalToScene(s.illuminanceLux.y, scale),
        Rend2PBR::PhysicalToScene(s.illuminanceLux.z, scale));
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return false;
    out = v; return true;
}
const char* ReasonName(Reason r) {
    switch (r) {
#define SUN_REASON(x) case Reason::x: return #x;
    SUN_REASON(Disabled) SUN_REASON(Missing) SUN_REASON(NoResolvedSun) SUN_REASON(Ambiguous) SUN_REASON(UnknownDirection)
    SUN_REASON(UnsupportedPhotometry) SUN_REASON(Unsuitable) SUN_REASON(Uncertainty)
    SUN_REASON(Configuration) SUN_REASON(PhysicalModeRequired) SUN_REASON(Worldless)
    SUN_REASON(Forced) SUN_REASON(Scene) SUN_REASON(ScaleMismatch) SUN_REASON(FeatureUnavailable) SUN_REASON(Accepted)
#undef SUN_REASON
    } return "Unknown";
}
const char* KindName(Kind k) {
    switch (k) {
#define SUN_KIND(x) case Kind::x: return #x;
    SUN_KIND(Unavailable) SUN_KIND(LegacyRenderer) SUN_KIND(AuthoredPhysical) SUN_KIND(ReconstructedPhysical)
    SUN_KIND(ConditionalRelative) SUN_KIND(LegacySceneEquivalent) SUN_KIND(ApproximateAppearance)
#undef SUN_KIND
    } return "Unknown";
}
} }
