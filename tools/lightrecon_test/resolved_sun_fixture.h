// Controlled authored fixture ONLY; no stock recovery. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tr_lightrecon_types.h"
using namespace Rend2LightRecon;
inline Estimate Known(float x) { Estimate e; e.known = true; e.value = x; return e; }
inline ReconstructedLightingAsset Fixture() {
    ReconstructedLightingAsset a; a.lineage = "C01/controlled-authored"; a.provenance = 1;
    Provenance p; p.id = 1; p.kind = ProvenanceKind::Authored; p.producer = "C01 analytic fixture";
    a.provenanceRecords.push_back(p);
    Calibration c; c.id = 1; c.provenance = 1; c.kind = CalibrationKind::PhysicallyAuthored;
    a.calibrations.push_back(c);
    PhysicalEmitter e; e.source.id = 10; e.source.provenance = 1; e.source.state = HypothesisState::Resolved;
    e.source.evidence.exactLight = Suitability::Candidate;
    e.kind = EmitterKind::Directional; e.angular.kind = AngularKind::Parallel;
    e.frame.known = true; e.frame.up = Vec3(0,-1,0); e.frame.forward = Vec3(0,0,-1);
    e.emission.unit = PhotometricUnit::Lux; e.emission.calibration = 1;
    e.emission.intensity = Known(100000); NormalizeChromaticity(Vec3(1,1,1), e.emission.color);
    e.geometry.angularRadiusRadians = Known(0.00465f);
    a.emitters.push_back(e); SunSource sun; sun.emitter = 10; a.suns.push_back(sun);
    LightingConfiguration config; config.id = 100; config.provenance = 1; config.key = "C01/physical";
    config.calibrationHypothesis = "specified SI";
    config.profile = "authored-parallel-lux"; a.lightingConfigurations.push_back(config);
    auto& b = a.assessment; b.present = true; b.producer = "C01"; b.algorithm = "authored";
    b.map = "maps/C01"; b.bspSha256 = std::string(64, '0'); b.settingsHash = "C01";
    b.decoder = "linear Rec709"; b.inputsLoaded = true; b.selectedConfiguration = 100;
    b.geometryKey = "C01/geometry"; b.selectedProfile = config.profile; b.resolvedSources = 1;
    for (unsigned i=0;i<7;++i) { ReadinessAssessment r; r.kind=ReadinessKind(i); r.scope="C01 controlled"; b.readiness.push_back(r); }
    for (unsigned i=0;i<6;++i) { SourceAdmission r; r.source=10; r.use=SourceUse(i); r.reason="C01 authored"; b.admissions.push_back(r); }
    return a;
}
