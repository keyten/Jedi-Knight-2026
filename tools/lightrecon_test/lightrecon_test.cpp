// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/tr_lightrecon_types.h"
#include "../../shared/rd-rend2/tr_pbr_contract.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace Rend2LightRecon;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
bool Near(float a, float b) { return std::abs(a - b) < 1.0e-5f * std::max(1.0f, std::abs(b)); }
Estimate Known(float value)
{
	Estimate e; e.known = true; e.value = value; return e;
}
bool Valid(const ReconstructedLightingAsset& a)
{
	std::string error;
	if (Validate(a, &error)) return true;
	std::printf("Unexpected validation failure: %s\n", error.c_str());
	return false;
}
bool Invalid(const ReconstructedLightingAsset& a)
{
	std::string error;
	return !Validate(a, &error) && !error.empty();
}
ReconstructedLightingAsset Base()
{
	ReconstructedLightingAsset a;
	a.lineage = "test-map/reconstruction-1";
	a.provenance = 1;
	Provenance p; p.id = 1; p.kind = ProvenanceKind::Authored; p.producer = "contract-test/1";
	a.provenanceRecords.push_back(p);
	p.id = 2; p.kind = ProvenanceKind::LegacyLightmap; p.observationIssues = ObservationQuantized | ObservationClipped;
	a.provenanceRecords.push_back(p);
	Calibration c; c.id = 11; c.kind = CalibrationKind::PhysicallyAuthored; c.provenance = 1;
	a.calibrations.push_back(c);
	c.id = 12; c.kind = CalibrationKind::LegacySceneEquivalent; c.provenance = 2;
	c.method = "test reference white -> scene Y=1; equivalent only";
	a.calibrations.push_back(c);
	c.id = 13; c.kind = CalibrationKind::UncalibratedRelative; c.method.clear();
	a.calibrations.push_back(c);
	return a;
}
Source MakeSource(Id id)
{
	Source s; s.id = id; s.provenance = 1;
	s.evidence.physicalEmitterConfidence = Known(0.8f);
	s.evidence.transportConfidence = Known(0.9f);
	return s;
}
PhysicalEmitter Emitter(EmitterKind kind, Id id = 100)
{
	PhysicalEmitter e; e.source = MakeSource(id); e.kind = kind;
	e.source.evidence.exactLight = Suitability::Candidate;
	e.source.evidence.approximateIrradiance = Suitability::Candidate;
	e.position.known = kind != EmitterKind::Directional;
	e.position.world = Vec3(5.0f, -10.0f, 50.0f);
	e.position.sigmaWorld = Known(2.0f);
	e.frame.known = true;
	e.geometry.known = true;
	e.geometry.halfWidthWorld = 10.0f; e.geometry.halfHeightWorld = 5.0f;
	e.geometry.halfLengthWorld = 20.0f; e.geometry.radiusWorld = 0.5f;
	e.geometry.effectiveRadiusWorld = Known(1.0f / 3.0f);
	e.emission.intensity = Known(1000.0f); e.emission.calibration = 11;
	CHECK(NormalizeChromaticity(Vec3(1.0f, 0.5f, 0.1f), e.emission.color));
	e.emission.unit = kind == EmitterKind::Point || kind == EmitterKind::Spot ? PhotometricUnit::Candela :
		kind == EmitterKind::Directional ? PhotometricUnit::Lux : PhotometricUnit::Nits;
	e.angular.kind = kind == EmitterKind::Point ? AngularKind::Isotropic : kind == EmitterKind::Spot ? AngularKind::SmoothCone :
		kind == EmitterKind::Directional ? AngularKind::Parallel : AngularKind::Lambertian;
	if (kind != EmitterKind::Directional) e.rangeWorld = Known(1000.0f);
	else e.geometry.angularRadiusRadians = Known(0.00465f);
	return e;
}
Dataset Data(Id id, DatasetPurpose purpose)
{
	Dataset d; d.id = id; d.purpose = purpose; d.locator = "asset://test/chunks";
	d.encoding = "org.openjk.test/records"; d.chunkElements = 64; return d;
}
void EmptyAndIncomplete()
{
	ReconstructedLightingAsset empty;
	CHECK(Valid(empty)); CHECK(empty.emitters.empty() && empty.suns.empty() && empty.transport.empty());
	std::string error = "old"; CHECK(Validate(empty, &error) && error.empty());
	empty.version++; CHECK(Invalid(empty)); empty.version--;
	empty.schema = "different"; CHECK(Invalid(empty));
	auto a = Base();
	PhysicalEmitter e; e.source = MakeSource(100); // unknown geometry, position, color and intensity
	e.source.evidence.observationIssues = ObservationMissing | ObservationUnsupportedMaterial | ObservationUnreliable;
	e.emission.calibration = 13;
	e.position.sigmaWorld.bounded = true; e.position.sigmaWorld.upper = 1000.0f;
	e.emission.intensity.bounded = true; e.emission.intensity.upper = 100.0f;
	e.source.evidence.physicalEmitterConfidence = Estimate();
	e.source.evidence.transportConfidence = Known(0.7f);
	a.emitters.push_back(e); CHECK(Valid(a));
	CHECK(!a.emitters[0].emission.intensity.known && a.suns.empty());
	a.emitters[0].source.evidence.exactLight = Suitability::Candidate; CHECK(Invalid(a));
	a.emitters[0].source.evidence.exactLight = Suitability::Unknown;
	a.emitters[0].emission.intensity.value = 200.0f; CHECK(Valid(a)); // inactive finite value is not an estimate
	a.emitters[0].emission.intensity.known = true; CHECK(Invalid(a)); // outside interval
	a.emitters[0].emission.intensity = Known(0.0f); CHECK(Valid(a)); // explicitly dark
	a.emitters[0].emission.calibration = 0; CHECK(Invalid(a));
}
void TypesAndIdentity()
{
	auto a = Base();
	for (unsigned k = 1; k <= 5; ++k) a.emitters.push_back(Emitter(static_cast<EmitterKind>(k), 100 + k));
	SunSource sun; sun.emitter = 105; a.suns.push_back(sun); CHECK(Valid(a));
	std::reverse(a.emitters.begin(), a.emitters.end()); CHECK(Valid(a)); CHECK(a.suns[0].emitter == 105);
	a.emitters.erase(a.emitters.begin() + 1); CHECK(Valid(a)); // IDs do not depend on order/deletion
	a.emitters.push_back(a.emitters[0]); CHECK(Invalid(a)); a.emitters.pop_back();
	a.emitters[0].source.id = 0; CHECK(Invalid(a)); a.emitters[0].source.id = 105;
	a.suns[0].emitter = 101; CHECK(Invalid(a)); a.suns[0].emitter = 105;
	a.suns.push_back(sun); CHECK(Invalid(a)); a.suns.pop_back();
	HypothesisGroup group; group.id = 90; group.description = "two possible sun directions";
	a.hypothesisGroups.push_back(group); a.emitters[0].source.hypothesisGroup = 90;
	a.emitters.push_back(Emitter(EmitterKind::Directional, 106));
	a.emitters.back().source.hypothesisGroup = 90; sun.emitter = 106; a.suns.push_back(sun);
	CHECK(Valid(a));
	a.emitters[0].source.state = HypothesisState::Resolved; CHECK(Valid(a));
	a.emitters.back().source.state = HypothesisState::Resolved; CHECK(Invalid(a));
	a.emitters.back().source.state = HypothesisState::Rejected;
	a.emitters.back().source.evidence.exactLight = Suitability::Unsuitable; CHECK(Valid(a));
	a.emitters[0].source.hypothesisWeight = Known(1.1f); CHECK(Invalid(a));
	a.emitters[0].source.hypothesisWeight = Known(0.0f); CHECK(Valid(a));
	a = Base(); a.datasets.push_back(Data(30, DatasetPurpose::EmitterGeometry));
	a.emitters.push_back(Emitter(EmitterKind::Point));
	a.emitters[0].kind = static_cast<EmitterKind>(0x10001);
	a.emitters[0].extensionType = "org.example.disk/1"; a.emitters[0].geometry.extension = 30;
	a.emitters[0].source.evidence.exactLight = Suitability::Unknown;
	CHECK(Valid(a)); // unsupported future source is preserved without a renderer switch
	a.emitters[0].extensionType.clear(); CHECK(Invalid(a));
}
void UnitsAndColors()
{
	Units u; CHECK(Near(u.worldUnitsPerMeter, Rend2PBR::kDefaultWorldUnitsPerMeter));
	CHECK(Near(u.physicalToScene, Rend2PBR::kDefaultPhysicalToScene));
	float value = -123.0f;
	CHECK(WorldToMeters(64.0f, u, value) && Near(value, 1.92f));
	CHECK(MetersToWorld(value, u, value) && Near(value, 64.0f));
	CHECK(WorldToMeters(0.0f, u, value) && value == 0.0f);
	CHECK(!WorldToMeters(-1.0f, u, value));
	u.worldUnitsPerMeter = 0.0f; CHECK(!WorldToMeters(1.0f, u, value));
	u.worldUnitsPerMeter = std::numeric_limits<float>::max();
	CHECK(!MetersToWorld(2.0f, u, value));
	u.worldUnitsPerMeter = std::numeric_limits<float>::min();
	CHECK(!WorldToMeters(std::numeric_limits<float>::max(), u, value));
	Chromaticity color;
	CHECK(!NormalizeChromaticity(Vec3(), color));
	CHECK(!NormalizeChromaticity(Vec3(1.0e-7f, 1.0e-7f, 1.0e-7f), color));
	CHECK(!NormalizeChromaticity(Vec3(1.0e-6f, 1.0e-6f, 1.0e-6f), color));
	CHECK(NormalizeChromaticity(Vec3(1.01e-6f, 1.01e-6f, 1.01e-6f), color));
	CHECK(NormalizeChromaticity(Vec3(-1.0f, 0.0f, 1.0f), color));
	CHECK(color.rgb.x == 0.0f && color.rgb.z > 1.0f && Near(color.rgb.z * Rend2PBR::kLumaB, 1.0f));
	CHECK(!NormalizeChromaticity(Vec3(-1.0f, -2.0f, -3.0f), color));
	auto a = Base(); a.emitters.push_back(Emitter(EmitterKind::Point));
	Vec3 rgb;
	CHECK(SceneEmissionRGB(a, a.emitters[0].emission, rgb));
	CHECK(Near(rgb.x * Rend2PBR::kLumaR + rgb.y * Rend2PBR::kLumaG + rgb.z * Rend2PBR::kLumaB, 0.1f));
	a.emitters[0].emission.calibration = 12; CHECK(Valid(a)); CHECK(SceneEmissionRGB(a, a.emitters[0].emission, rgb));
	a.calibrations[1].method.clear(); CHECK(Invalid(a));
	a = Base(); a.emitters.push_back(Emitter(EmitterKind::Point));
	a.emitters[0].emission.calibration = 13; CHECK(Invalid(a));
	a.emitters[0].emission.unit = PhotometricUnit::Relative;
	a.emitters[0].source.evidence.exactLight = Suitability::Unknown; CHECK(Valid(a));
	CHECK(!SceneEmissionRGB(a, a.emitters[0].emission, rgb));
	a.emitters[0].emission.unit = PhotometricUnit::Candela;
	CHECK(!SceneEmissionRGB(a, a.emitters[0].emission, rgb));
	a.calibrations[0].provenance = 2; CHECK(Invalid(a));
}
void InvalidGeometry()
{
	auto a = Base(); a.emitters.push_back(Emitter(EmitterKind::Rectangle));
	a.emitters[0].geometry.halfWidthWorld = 0.0f; CHECK(Invalid(a));
	a.emitters[0].geometry.halfWidthWorld = -1.0f; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Rectangle); a.emitters[0].frame.up = a.emitters[0].frame.right; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Rectangle); a.emitters[0].frame.forward.z = -1.0f; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Rectangle);
	a.emitters[0].frame.forward = Vec3(0.01f, 0.0f, std::sqrt(0.9999f)); CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Spot); a.emitters[0].frame.forward = Vec3(); CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Spot); a.emitters[0].angular.cosOuter = 2.0f; CHECK(Invalid(a));
	a.emitters[0].angular.cosOuter = a.emitters[0].angular.cosInner; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Tube); a.emitters[0].geometry.radiusWorld = -1.0f; CHECK(Invalid(a));
	a.emitters[0].geometry.radiusWorld = 0.0f; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Tube); a.emitters[0].geometry.halfLengthWorld = 0.0f; CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Point); a.emitters[0].geometry.effectiveRadiusWorld = Known(-0.1f); CHECK(Invalid(a));
	a.emitters[0].geometry.effectiveRadiusWorld = Known(0.0f); CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Point); a.emitters[0].rangeWorld = Known(0.0f); CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Directional); a.emitters[0].rangeWorld = Known(100.0f); CHECK(Invalid(a));
	a.emitters[0] = Emitter(EmitterKind::Directional); a.emitters[0].geometry.angularRadiusRadians = Known(2.0f); CHECK(Invalid(a));
}
void Nonfinite()
{
	const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
	for (float x : bad)
	{
		auto a = Base(); a.emitters.push_back(Emitter(EmitterKind::Spot));
		float* fields[] = { &a.units.worldUnitsPerMeter, &a.units.physicalToScene, &a.emitters[0].position.world.x,
			&a.emitters[0].position.sigmaWorld.value, &a.emitters[0].frame.forward.y, &a.emitters[0].geometry.radiusWorld,
			&a.emitters[0].geometry.effectiveRadiusWorld.upper, &a.emitters[0].emission.intensity.value,
			&a.emitters[0].emission.color.rgb.z, &a.emitters[0].angular.cosInner, &a.emitters[0].rangeWorld.lower,
			&a.emitters[0].source.evidence.physicalEmitterConfidence.value, &a.emitters[0].source.evidence.transportConfidence.value };
		for (float* field : fields) { const float saved = *field; *field = x; CHECK(Invalid(a)); *field = saved; }
		a.emitters[0].emission.intensity.known = false; a.emitters[0].emission.intensity.value = x; CHECK(Invalid(a));
		Chromaticity c; CHECK(!NormalizeChromaticity(Vec3(x, 1.0f, 1.0f), c));
		float value = 77.0f; CHECK(!WorldToMeters(x, Units(), value) && value == 77.0f);
		Vec3 rgb(77.0f, 77.0f, 77.0f); CHECK(!SceneEmissionRGB(a, a.emitters[0].emission, rgb) && rgb.x == 77.0f);
	}
}
void FieldsAndReferences()
{
	auto a = Base();
	TransportField field; field.source = MakeSource(200); field.source.evidence.exactLight = Suitability::Unsuitable;
	field.component = TransportComponent::ApertureTransport; field.calibration = 13;
	field.source.evidence.observationIssues = ObservationUnsupportedMaterial;
	a.transport.push_back(field); CHECK(Valid(a)); CHECK(a.emitters.empty());
	a.transport[0].source.evidence.exactLight = Suitability::Candidate; CHECK(Invalid(a));
	a.transport[0].source.evidence.exactLight = Suitability::Unsuitable;
	EnvironmentLighting sky; sky.source = MakeSource(300); sky.meanLuminance.calibration = 13;
	a.environments.push_back(sky); CHECK(Valid(a)); CHECK(a.suns.empty());
	a.datasets.push_back(Data(30, DatasetPurpose::Transport));
	a.datasets[0].elementCount = 24; a.transport[0].dataset = 30;
	a.transport[0].bounds.known = true; a.transport[0].bounds.maxWorld = Vec3(10.0f, 20.0f, 30.0f);
	a.transport[0].sampleDimensions[0] = 2; a.transport[0].sampleDimensions[1] = 3;
	CHECK(Invalid(a)); a.transport[0].sampleDimensions[2] = 4; CHECK(Valid(a));
	a.transport[0].sampleDimensions[2] = 5; CHECK(Invalid(a));
	for (auto& d : a.transport[0].sampleDimensions) d = std::numeric_limits<uint32_t>::max();
	CHECK(Invalid(a));
	for (auto& d : a.transport[0].sampleDimensions) d = 0;
	CHECK(Valid(a));
	a.transport[0].quantity = FieldQuantity::Radiance; CHECK(Valid(a));
	a.transport[0].component = TransportComponent::IndirectIrradiance; CHECK(Invalid(a));
	a.transport[0].quantity = FieldQuantity::Irradiance;
	a.datasets.push_back(Data(31, DatasetPurpose::Visibility));
	a.datasets.push_back(Data(32, DatasetPurpose::Contributions)); a.datasets.back().layout = DatasetLayout::Sparse;
	a.datasets.push_back(Data(33, DatasetPurpose::SampleDomain));
	a.datasets.push_back(Data(34, DatasetPurpose::ObservationQuality)); a.provenanceRecords[1].observationQuality = 34;
	GeometryRegion region; region.id = 40; region.provenance = 1; a.regions.push_back(region);
	a.transport[0].source.regions.push_back(40);
	VisibilityReference visibility; visibility.dataset = 31; visibility.source = 200; visibility.region = 40;
	a.visibility.push_back(visibility);
	ContributionReference contribution; contribution.dataset = 32; contribution.sampleDomain = 33;
	contribution.sources.push_back(200); contribution.calibration = 13;
	contribution.component = ContributionComponent::Indirect; a.contributions.push_back(contribution); CHECK(Valid(a));
	a.contributions[0].component = ContributionComponent::Direct; CHECK(Invalid(a));
	a.emitters.push_back(Emitter(EmitterKind::Point)); a.contributions[0].sources[0] = 100; CHECK(Valid(a));
	a.contributions[0].quantity = ContributionQuantity::AttributionFraction; CHECK(Invalid(a));
	a.contributions[0].calibration = 0; CHECK(Valid(a));
	a.visibility[0].source = 999; CHECK(Invalid(a)); a.visibility[0].source = 200;
	a.datasets[0].chunkElements = 0; CHECK(Invalid(a)); a.datasets[0].chunkElements = 64;
	a.datasets[1].purpose = DatasetPurpose::Transport; CHECK(Invalid(a)); a.datasets[1].purpose = DatasetPurpose::Visibility;
	a.environments[0].source.id = 200; CHECK(Invalid(a)); a.environments[0].source.id = 300;
	a.provenanceRecords[0].parents.push_back(2); a.provenanceRecords[1].parents.push_back(1); CHECK(Invalid(a));
	a.provenanceRecords[0].parents.clear(); CHECK(Valid(a));
	Diagnostic diagnostic; diagnostic.code = "unsupported-material"; diagnostic.source = 200;
	diagnostic.metric = Known(0.4f); a.diagnostics.push_back(diagnostic); CHECK(Invalid(a));
	a.diagnostics[0].metricUnit = "relative-rms"; CHECK(Valid(a));
}
void PhotometricBoundaries()
{
	for (unsigned kind = 1; kind <= 5; ++kind)
	{
		auto a = Base(); a.emitters.push_back(Emitter(static_cast<EmitterKind>(kind)));
		Photometry& p = a.emitters[0].emission;
		CHECK(NormalizeChromaticity(Vec3(1.0f, 1.0f, 1.0f), p.color));
		p.intensity = Known(kind == 5 ? 100000.0f : 2000.0f);
		Vec3 rgb;
		CHECK(SceneEmissionRGB(a, p, rgb) && Near(rgb.x, kind == 5 ? 10.0f : 0.2f));
		const auto savedUnit = p.unit;
		p.unit = savedUnit == PhotometricUnit::Lux ? PhotometricUnit::Nits : PhotometricUnit::Lux;
		CHECK(Invalid(a)); p.unit = savedUnit;
		p.intensity.bounded = true; p.intensity.lower = p.intensity.value; p.intensity.upper = p.intensity.value;
		CHECK(Valid(a));
		p.intensity.lower += 1.0f; CHECK(Invalid(a));
		p.intensity.bounded = false; p.intensity.value = -1.0f; CHECK(Invalid(a));
		p.intensity = Known(0.0f); CHECK(Valid(a));
		CHECK(SceneEmissionRGB(a, p, rgb) && rgb.x == 0.0f);
		p.intensity.known = false; rgb.x = 44.0f;
		CHECK(!SceneEmissionRGB(a, p, rgb) && rgb.x == 44.0f);
		p.intensity.known = true; p.color.known = false;
		CHECK(!SceneEmissionRGB(a, p, rgb));
		p.color.known = true; p.color.rgb.x = -1.0f; CHECK(Invalid(a));
	}
	auto a = Base(); a.emitters.push_back(Emitter(EmitterKind::Point));
	a.emitters[0].source.evidence.physicalEmitterConfidence = Known(0.0f);
	a.emitters[0].source.evidence.transportConfidence = Known(1.0f);
	a.emitters[0].position.sigmaWorld = Known(10000.0f); CHECK(Valid(a)); // independent assessments, no threshold policy
	a.emitters[0].source.evidence.transportConfidence = Known(-0.1f); CHECK(Invalid(a));
	a.emitters[0].source.evidence.transportConfidence = Known(1.1f); CHECK(Invalid(a));
	a.emitters[0].source.evidence.transportConfidence = Estimate();
	a.emitters[0].position.sigmaWorld = Known(-1.0f); CHECK(Invalid(a));
	a.emitters[0].position.sigmaWorld = Estimate();
	a.emitters[0].emission.intensity = Known(std::numeric_limits<float>::max());
	a.units.physicalToScene = 10.0f;
	Vec3 rgb(17.0f, 18.0f, 19.0f);
	CHECK(!SceneEmissionRGB(a, a.emitters[0].emission, rgb) && rgb.x == 17.0f && rgb.y == 18.0f);
	a.units.physicalToScene = -1.0f; CHECK(Invalid(a));
	a = Base(); a.datasets.push_back(Data(50, DatasetPurpose::Environment));
	EnvironmentLighting sky; sky.source = MakeSource(300); sky.radiance = 50;
	a.environments.push_back(sky); CHECK(Invalid(a)); // dataset needs calibration even without mean estimate
	a.environments[0].meanLuminance.calibration = 13; CHECK(Valid(a));
	a.environments[0].meanLuminance.unit = PhotometricUnit::Nits; CHECK(Invalid(a));
	a.environments[0].meanLuminance.calibration = 12;
	a.environments[0].sunCoverage = SunCoverage::IncludesExplicitSun; CHECK(Valid(a));
}
void ExtensionAndAngularProfiles()
{
	auto a = Base(); a.emitters.push_back(Emitter(EmitterKind::Point));
	a.emitters[0].angular.kind = AngularKind::Tabulated; CHECK(Invalid(a));
	a.datasets.push_back(Data(70, DatasetPurpose::AngularProfile));
	a.emitters[0].angular.dataset = 70; CHECK(Valid(a));
	a.emitters[0].frame.known = false; CHECK(Invalid(a));
	a.emitters[0].frame.known = true;
	a.emitters[0].kind = EmitterKind::Extension;
	a.emitters[0].extensionType = "org.example.future/1";
	a.datasets.push_back(Data(71, DatasetPurpose::EmitterGeometry));
	a.emitters[0].geometry.extension = 71; CHECK(Invalid(a)); // common validator cannot certify unknown shape
	a.emitters[0].source.evidence.exactLight = Suitability::Unknown; CHECK(Valid(a));
	a.emitters[0].emission.color.rgb.z = std::numeric_limits<float>::infinity(); CHECK(Invalid(a));
	a.emitters[0].emission = Photometry(); CHECK(Valid(a)); // unknown extension photometry
	a.emitters[0].source.provenance = 999; CHECK(Invalid(a));
	a.emitters[0].source.provenance = 1;
	a.emitters[0].source.regions.push_back(999); CHECK(Invalid(a));
}
} // namespace
int main()
{
	EmptyAndIncomplete(); TypesAndIdentity(); UnitsAndColors(); InvalidGeometry(); Nonfinite(); FieldsAndReferences();
	PhotometricBoundaries(); ExtensionAndAngularProfiles();
	std::printf("Reconstructed lighting contract: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
