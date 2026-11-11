// C01: controlled authored data is not recovered stock lighting.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/resolved_sun.h"
#include "lightrecon/photometry.h"
#include "tr_lightrecon_manager.h"
#include <cstdio>
#include <cmath>
#include <type_traits>
#include <chrono>
using namespace Rend2LightRecon;
namespace S = SunLighting;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static_assert(std::is_trivially_copyable<S::State>::value, "queued scene state owns no publication");
#include "resolved_sun_fixture.h"
S::State Resolve(const ReconstructedLightingAsset& a) {
    Manager owner; std::string error;
    if (!Validate(a, &error)) std::printf("INVALID: %s\n", error.c_str());
    CHECK(owner.Publish(a, Completeness::Ready) == Status::Ok);
    return S::Resolve(owner.Acquire());
}
void Baseline(ReconstructedLightingAsset& a) {
    // P06 sample domains and all root/path/configuration metadata are explicit.
    Dataset d; d.id=1000; d.purpose=DatasetPurpose::SampleDomain; d.layout=DatasetLayout::Sparse;
    d.encoding="C01/test"; d.locator="not-read"; a.datasets.push_back(d);
    PhysicalEmitter other=a.emitters[0]; other.source.id=11; other.source.evidence.exactLight=Suitability::Unknown;
    other.kind=EmitterKind::Point; other.position.known=true; other.emission.unit=PhotometricUnit::Candela; other.angular.kind=AngularKind::Isotropic;
    a.emitters.push_back(other); a.assessment.resolvedSources=2;
    for (unsigned i=0;i<6;++i) { SourceAdmission r; r.source=11; r.use=SourceUse(i); r.reason="baseline test"; a.assessment.admissions.push_back(r); }
    for(unsigned i=0;i<6;++i) {
        d.id=2000+i; d.purpose=DatasetPurpose::Contributions; a.datasets.push_back(d);
        ContributionReference c; c.dataset=d.id; c.sampleDomain=1000;
        c.sources.push_back((i%2)?11:10); c.component=ContributionComponent::Direct;
        c.quantity=ContributionQuantity::IrradianceRGB; c.unit=PhotometricUnit::Lux;
        c.calibration=1; c.configuration=100;
        const char* domains[]={"rend2/world-diffuse","rend2/entity-grid","rend2/fog-grid"};
        c.domain=domains[i/2]; auto& m=c.membership; m.version=2; m.model="C01/operator";
        m.rootsComplete=true; TransportRoot root; root.source=(i%2)?11:10; root.kind=(i%2)?TransportRootKind::LocalEmitter:TransportRootKind::Sun;
        m.roots.push_back(root); m.reflectionOrderKnown=true; m.reflectionOrders.push_back(0);
        m.directLocal=m.directSky=m.legacyAmbient=m.reflected=m.unresolved=FieldMembership::Excluded;
        m.directSun=(i%2)?FieldMembership::Excluded:FieldMembership::Included;
        if(i%2) m.directLocal=FieldMembership::Included;
        m.colour=ContributionColour::LinearRec709D65; m.configuration=100; m.generation="C01";
        m.geometry=a.assessment.geometryKey; m.profile=a.assessment.selectedProfile; m.domain=c.domain;
        m.addition=CompositionRule::Permitted;
        if(i%2) { c.replacement.supported=ReplacementAllProofs; c.replacement.provenance=1;
            c.replacement.runtimeOperator="rend2/original-baked-linear/v1"; c.replacement.scope=c.domain;
            c.replacement.maximumAbsoluteError=Known(0); c.replacement.leakageRisk=Known(0); }
        a.contributions.push_back(c);
    }
}
int main() {
    auto a=Fixture(); auto state=Resolve(a);
    CHECK(state.canonical && state.kind==S::Kind::AuthoredPhysical);
    CHECK(state.directionToSun.z==1 && state.photonDirection.z==-1);
    CHECK(state.source==10 && state.provenance==1 && state.angularRadiusKnown);
    CHECK(!state.worldDiffuse && !state.entityDiffuse && !state.volumetricDirect);
    Vec3 scene; CHECK(S::SceneIlluminance(state,1e-4f,scene)); CHECK(std::fabs(scene.x-10)<1e-5f);
    CHECK(S::SceneIlluminance(state,2e-4f,scene)); CHECK(std::fabs(scene.x-20)<1e-5f);
    CHECK(!S::SceneIlluminance(state,0,scene));
    Photometric::Receiver receiver; receiver.normalWorld=Vec3(0,0,1);
    auto e=Photometric::DirectionalReceiverIrradiance(a,a.emitters[0],receiver);
    CHECK(e && e.value.rgb.z==100000);
    receiver.normalWorld=Vec3(0,0,-1); e=Photometric::DirectionalReceiverIrradiance(a,a.emitters[0],receiver);
    CHECK(e && e.value.rgb.z==0);
    S::PolicyInput input; input.master=input.requested=input.physical=input.world=input.shaderSun=true;
    CHECK(S::Select(state,input).canonical);
    for(int force : {1,2,3,-1}) { input.forceSun=force; auto r=S::Select(state,input); CHECK(!r.canonical && r.explicitOverride && r.policy==S::Policy::Forced); }
    input.sceneOverride=true; CHECK(S::Select(state,input).policy==S::Policy::Scene);
    input.forceSun=0; input.sceneOverride=false;
    input.master=false; CHECK(S::Select(state,input).reason==S::Reason::Disabled); input.master=true;
    input.requested=false; CHECK(!S::Select(state,input).canonical); input.requested=true;
    input.world=false; CHECK(S::Select(state,input).reason==S::Reason::Worldless); input.world=true;
    input.physical=false; CHECK(S::Select(state,input).reason==S::Reason::PhysicalModeRequired);
    Manager missing; CHECK(!S::Resolve(missing.Acquire()).canonical);
    auto b=a; b.emitters[0].frame.known=false; b.emitters[0].source.evidence.exactLight=Suitability::Unknown;
    CHECK(!Resolve(b).canonical);
    b=a; auto alt=b.emitters[0]; alt.source.id=11; b.emitters.push_back(alt); SunSource sun; sun.emitter=11; b.suns.push_back(sun);
    b.assessment.present=false; CHECK(Resolve(b).status==Status::Ambiguous);
    b=a; b.calibrations[0].kind=CalibrationKind::LegacySceneEquivalent; b.calibrations[0].method="conditional relative mapping";
    CHECK(Resolve(b).kind==S::Kind::LegacySceneEquivalent);
    b=a; b.emitters[0].emission.unit=PhotometricUnit::Relative; b.emitters[0].source.evidence.exactLight=Suitability::Unknown;
    b.calibrations[0].kind=CalibrationKind::UncalibratedRelative; CHECK(!Resolve(b).canonical);
    b=a; b.assessment.selectedProfile.clear(); CHECK(Resolve(b).reason==S::Reason::Configuration);
    b=a; Provenance rp=b.provenanceRecords[0]; rp.id=2; rp.kind=ProvenanceKind::Reconstruction;
    b.provenanceRecords.push_back(rp); b.emitters[0].source.provenance=2;
    CHECK(Resolve(b).reason==S::Reason::Uncertainty);
    // Reconstructed physical admission needs consumer-scoped independent evidence,
    // not a generic confidence score or a successful Snapshot query.
    b.assessment.sunEvidence=AssessmentStatus::Unknown;
    b.assessment.admissions[0].status=AssessmentStatus::Validated;
    auto& emission=b.emitters[0].emission.intensity;
    emission.bounded=true; emission.lower=98000; emission.upper=102000;
    JointParameterState joint; joint.track=10; joint.kind=EmitterKind::Directional;
    joint.root=TransportRootKind::Sun; joint.frame=b.emitters[0].frame;
    joint.angular=b.emitters[0].angular; joint.amplitude=Vec3(100000,100000,100000); joint.amplitudeDomain="lux";
    b.lightingConfigurations[0].sources.push_back(joint);
    SourceUncertainty u; u.id=20; u.source=u.track=10; u.provenance=2;
    u.method="controlled supplied evidence"; u.inputHash="C01"; u.configurations.push_back(100);
    u.complete=u.absoluteScaleKnown=true; u.exactLight=Suitability::Candidate;
    u.directionSpreadRadians=Known(.001f);
    for(unsigned cause=0;cause<13;++cause) { UncertaintyFactor factor; factor.cause=UncertaintyCause(cause);
        factor.status=UncertaintyStatus::Supported; factor.reason="controlled evidence"; u.factors.push_back(factor); }
    auto& confidence=u.exactUse; confidence.meaning=ConfidenceMeaning::IndependentEmpiricalBinRate;
    confidence.event="rend2/directional-sun:0.25deg-5pct"; confidence.population="controlled independent families";
    confidence.protocol="C01 supplied witness"; confidence.calibrationHash="C01"; confidence.independentFamilies=100;
    confidence.intervalNominal=.95f; confidence.probability=Known(.99f);
    confidence.probability.bounded=true; confidence.probability.lower=.98f; confidence.probability.upper=1;
    b.uncertainty.push_back(u);
    CHECK(Resolve(b).canonical);
    b.uncertainty[0].factors[0].status=UncertaintyStatus::Conditional; CHECK(!Resolve(b).canonical);
    b.uncertainty[0]=u; b.uncertainty[0].exactUse.probability.lower=.5f; CHECK(!Resolve(b).canonical);
    b=a; b.emitters[0].source.evidence.physicalEmitterConfidence=Known(.2f); CHECK(!Resolve(b).canonical);
    b=a; b.emitters[0].source.evidence.observationIssues=ObservationUnreliable; CHECK(!Resolve(b).canonical);
    b=a; Baseline(b); auto combined=Resolve(b);
    CHECK(combined.worldDiffuse && combined.entityDiffuse && combined.volumetricDirect);
    b.contributions[1].membership.directSun=FieldMembership::Included;
    CHECK(!Resolve(b).worldDiffuse);
    b=a; Baseline(b); b.contributions[1].replacement.supported=0; CHECK(!Resolve(b).worldDiffuse);
    b=a; Baseline(b);
    for (unsigned i=0; i<2; ++i) {
        b.contributions[i].quantity=ContributionQuantity::RadianceRGB;
        b.contributions[i].unit=PhotometricUnit::Nits;
    }
    CHECK(!Resolve(b).worldDiffuse);
    b=a; Baseline(b); b.contributions[3].membership.profile="unknown"; CHECK(!Resolve(b).entityDiffuse);
    b=a; Baseline(b); b.contributions[5].membership.rootsComplete=false; CHECK(!Resolve(b).volumetricDirect);
    b=a; Baseline(b); b.contributions[1].membership.roots=b.contributions[0].membership.roots;
    b.contributions[1].membership.directSun=FieldMembership::Included; b.contributions[1].membership.directLocal=FieldMembership::Excluded; b.contributions[1].sources[0]=10;
    CHECK(!Resolve(b).worldDiffuse);
    // State is a detached value; a queued scene has no stale asset pointer.
    Manager owner; CHECK(owner.Publish(a,Completeness::Ready)==Status::Ok);
    auto snapshot=owner.Acquire(); auto weak=snapshot.Weak(); auto retained=S::Resolve(snapshot);
    owner.SetUnavailable(Availability::Unloaded); CHECK(!S::Resolve(owner.Acquire()).canonical);
    CHECK(S::Resolve(snapshot).canonical); snapshot={}; CHECK(weak.Lock().status==Status::Expired);
    CHECK(retained.illuminanceLux.x==100000);
    auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<100000;++i) { auto r=S::Select(state,input); CHECK(!r.canonical); }
    double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/100000;
    std::printf("C01: %d checks, %d failures; Select %.1f ns/call (controlled CPU, no GPU claim)\n",checks,failures,ns);
    return failures?1:0;
}
