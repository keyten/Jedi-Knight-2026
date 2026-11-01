// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/lightrecon/photometry.h"
#include "../../shared/rd-rend2/tr_pbr_contract.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace Rend2LightRecon;
namespace P=Rend2LightRecon::Photometric;
namespace {
const double pi=3.14159265358979323846;
int checks=0, failures=0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
bool Near(double a,double b,double tolerance=2e-6) { return std::abs(a-b)<=tolerance*std::max(1.0,std::abs(b)); }
Estimate Known(float v) { Estimate e; e.known=true; e.value=v; return e; }
ReconstructedLightingAsset Asset() {
	ReconstructedLightingAsset a; a.lineage="p04/synthetic"; a.provenance=1; a.units.worldUnitsPerMeter=10;
	Provenance p; p.id=1; p.kind=ProvenanceKind::Authored; p.producer="photometry-test/1"; a.provenanceRecords.push_back(p);
	p.id=2; p.kind=ProvenanceKind::LegacyLightmap; p.parents.push_back(1); a.provenanceRecords.push_back(p);
	Calibration c; c.id=11; c.provenance=1; c.kind=CalibrationKind::PhysicallyAuthored; a.calibrations.push_back(c);
	c.id=12; c.provenance=2; c.kind=CalibrationKind::LegacySceneEquivalent; c.method="synthetic scene equivalent /1"; a.calibrations.push_back(c);
	c.id=13; c.kind=CalibrationKind::UncalibratedRelative; c.method="synthetic relative calibration /1"; a.calibrations.push_back(c);
	return a;
}
PhysicalEmitter Emitter(EmitterKind kind) {
	PhysicalEmitter e; e.kind=kind; e.source.id=100; e.source.provenance=1;
	e.position.known=kind!=EmitterKind::Directional; e.frame.known=true; e.geometry.known=true;
	e.geometry.halfWidthWorld=10; e.geometry.halfHeightWorld=20;
	e.geometry.halfLengthWorld=20; e.geometry.radiusWorld=5; e.geometry.effectiveRadiusWorld=Known(1);
	e.geometry.angularRadiusRadians=Known(.01f);
	e.angular.kind=kind==EmitterKind::Point ? AngularKind::Isotropic : kind==EmitterKind::Spot ? AngularKind::SmoothCone :
		kind==EmitterKind::Directional ? AngularKind::Parallel : AngularKind::Lambertian;
	e.angular.cosInner=.9f; e.angular.cosOuter=.7f;
	e.emission.intensity=Known(100); e.emission.intensity.bounded=true; e.emission.intensity.lower=80; e.emission.intensity.upper=120;
	e.emission.color.known=true; e.emission.color.rgb=Vec3(1,1,1); e.emission.calibration=11;
	e.emission.unit=kind==EmitterKind::Point || kind==EmitterKind::Spot ? PhotometricUnit::Candela :
		kind==EmitterKind::Directional ? PhotometricUnit::Lux : PhotometricUnit::Nits;
	return e;
}
P::Receiver Receiver(float distance=20) { P::Receiver r; r.positionWorld=Vec3(0,0,distance); r.normalWorld=Vec3(0,0,-1); return r; }
void UnitsColour() {
	auto a=Asset();
	for(double world:{0.0,1.0,33.333333,1e10}) {
		auto m=P::WorldDistanceMetres(world,a.units); CHECK(m);
		auto w=P::MetresWorldDistance(m.value,a.units); CHECK(w && Near(w.value,world));
	}
	float out=17; CHECK(WorldToMeters(10,a.units,out) && out==1); CHECK(MetersToWorld(1,a.units,out) && out==10);
	for(Vec3 rgb:{Vec3(1,1,1),Vec3(1,.2f,.01f),Vec3(0,0,3),Vec3(1e-20f,2e-20f,0)}) {
		auto photo=P::RGBToPhotometry(rgb,PhotometricUnit::Candela,11); CHECK(photo);
		const auto& p=photo.value; CHECK(p.intensity.known && p.color.known);
		const float c[]={p.color.rgb.x,p.color.rgb.y,p.color.rgb.z}; CHECK(Near(Rend2PBR::Luminance(c),1));
		CHECK(Near(p.color.rgb.x*p.intensity.value,rgb.x) && Near(p.color.rgb.z*p.intensity.value,rgb.z));
		if(rgb.x>1e-6f) { float in[]={rgb.x,rgb.y,rgb.z}, norm[3]; CHECK(Rend2PBR::NormalizeChromaticity(in,norm)); CHECK(Near(c[0],norm[0])); }
	}
	auto black=P::RGBToPhotometry(Vec3(),PhotometricUnit::Nits,11); CHECK(black && black.value.intensity.value==0 && !black.value.color.known);
	CHECK(!P::RGBToPhotometry(Vec3(-1,1,1),PhotometricUnit::Nits,11));
	CHECK(!P::RGBToPhotometry(Vec3(std::numeric_limits<float>::infinity(),1,1),PhotometricUnit::Nits,11));
	a.units.worldUnitsPerMeter=0; CHECK(!P::WorldDistanceMetres(1,a.units));
	a.units.worldUnitsPerMeter=10; CHECK(!P::MetresWorldDistance(std::numeric_limits<double>::max(),a.units));
	CHECK(!P::WorldDistanceMetres(-1,a.units)); CHECK(!P::WorldDistanceMetres(std::numeric_limits<double>::quiet_NaN(),a.units));
}
void PointSpot() {
	auto a=Asset(); auto e=Emitter(EmitterKind::Point); auto r=Receiver();
	auto two=P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare); CHECK(two && Near(two.value.rgb.x,25));
	CHECK(two.value.unit==PhotometricUnit::Lux && two.value.quantity==PhotometricQuantity::ReceiverIrradiance);
	CHECK(two.value.lowerKnown && Near(two.value.lower.x,20) && Near(two.value.upper.x,30));
	r.positionWorld.z=40; auto four=P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare);
	CHECK(four && Near(two.value.rgb.x/four.value.rgb.x,4));
	CHECK(Near(Rend2PBR::PointIrradiance(100,20,10000,10),two.value.rgb.x));
	r=Receiver(); r.normalWorld=Vec3(1,0,0); auto tangent=P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare); CHECK(tangent && tangent.value.rgb.x==0);
	r.normalWorld=Vec3(0,0,1); CHECK(P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare).value.rgb.x==0);
	r=Receiver(); r.transmittance=.25; CHECK(Near(P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare).value.rgb.x,6.25));
	r=Receiver(.5f); auto finite=P::PointReceiverIrradiance(a,e,r,P::PointModel::FiniteRadiusBoundedApproximation);
	CHECK(finite.code==P::Code::Approximate && Near(finite.value.rgb.x,10000));
	e.geometry.effectiveRadiusWorld=Estimate(); CHECK(!P::PointReceiverIrradiance(a,e,r,P::PointModel::FiniteRadiusBoundedApproximation));
	e=Emitter(EmitterKind::Spot); r=Receiver();
	auto solid=P::SpotSolidAngle(e.angular); CHECK(solid && Near(solid.value,2*pi*(1-(double(e.angular.cosInner)+e.angular.cosOuter)/2)));
	CHECK(Near(solid.value,Rend2PBR::SpotProfileSolidAngle(e.angular.cosInner,e.angular.cosOuter)));
	auto cd=P::SpotCandelaFromLumens(500,e.angular); CHECK(cd && Near(cd.value,500/solid.value));
	CHECK(Near(cd.value,Rend2PBR::LumensToSpotCandela(500,e.angular.cosInner,e.angular.cosOuter)));
	// Independent midpoint integration over cos(theta), including the core.
	double integrated=0; const unsigned n=100000;
	for(unsigned i=0;i<n;++i) { double mu=-1+2*(i+.5)/n;
		double t=std::max(0.0,std::min(1.0,(mu-e.angular.cosOuter)/(e.angular.cosInner-e.angular.cosOuter)));
		integrated+=t*t*(3-2*t)*4*pi/n; }
	CHECK(Near(integrated,solid.value,1e-7));
	CHECK(Near(P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare).value.rgb.x,25));
	r.positionWorld.z=-20; r.normalWorld=Vec3(0,0,1); CHECK(P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare).value.rgb.x==0);
	e.angular.cosInner=e.angular.cosOuter; CHECK(!P::SpotSolidAngle(e.angular));
	e.angular.kind=AngularKind::Tabulated; CHECK(P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare).reason==P::Reason::UnsupportedAngularProfile);
}
void ShapesSun() {
	auto a=Asset(); auto e=Emitter(EmitterKind::Rectangle); auto r=Receiver();
	CHECK(Near(P::EmittingAreaSquareMetres(e,a.units).value,8));
	auto le=P::LambertianEmittedRadiance(a,e,Vec3(0,0,1),Vec3(0,0,1)); CHECK(le && le.value.rgb.x==100 && le.value.unit==PhotometricUnit::Nits);
	CHECK(P::LambertianEmittedRadiance(a,e,Vec3(0,0,1),Vec3(0,0,-1)).value.rgb.x==0);
	e.angular.twoSided=true; CHECK(P::LambertianEmittedRadiance(a,e,Vec3(0,0,1),Vec3(0,0,-1)).value.rgb.x==100); e.angular.twoSided=false;
	auto area=P::AreaReceiverIrradiance(a,e,r,128); CHECK(area.code==P::Code::Approximate && area.value.rgb.x>0);
	// Analytic axis-aligned rectangle, half widths a,b and separation d:
	// E/L=2*(x/sqrt(1+x²)*atan(y/sqrt(1+x²))+ y/sqrt(1+y²)*atan(x/sqrt(1+y²))).
	double x=.5,y=1; double exact=2*(x/std::sqrt(1+x*x)*std::atan(y/std::sqrt(1+x*x))+y/std::sqrt(1+y*y)*std::atan(x/std::sqrt(1+y*y)));
	CHECK(Near(area.value.rgb.x,100*exact,3e-5));
	r.positionWorld.z=-20; r.normalWorld=Vec3(0,0,1); CHECK(P::AreaReceiverIrradiance(a,e,r).value.rgb.x==0);
	e=Emitter(EmitterKind::Tube); CHECK(Near(P::EmittingAreaSquareMetres(e,a.units).value,4*pi));
	r=Receiver(10000); auto far=P::AreaReceiverIrradiance(a,e,r,128); CHECK(far.code==P::Code::Approximate);
	// Far-field side-on projected area = diameter * length, no emitting caps.
	CHECK(Near(far.value.rgb.x,100*4/(1000.0*1000),5e-7));
	r.positionWorld=Vec3(10000,0,0); r.normalWorld=Vec3(-1,0,0); CHECK(P::AreaReceiverIrradiance(a,e,r).value.rgb.x==0);
	e.geometry.radiusWorld=0; CHECK(!P::EmittingAreaSquareMetres(e,a.units));
	e=Emitter(EmitterKind::Directional); r=Receiver();
	CHECK(Near(P::DirectionalReceiverIrradiance(a,e,r).value.rgb.x,100));
	r.normalWorld=Vec3(1,0,0); CHECK(P::DirectionalReceiverIrradiance(a,e,r).value.rgb.x==0);
	auto disk=P::UniformSunDiskIncidentRadiance(a,e,Vec3(0,0,-1)); CHECK(disk && Near(disk.value.rgb.x*pi*std::pow(std::sin(.01f),2),100));
	CHECK(P::UniformSunDiskIncidentRadiance(a,e,Vec3(1,0,0)).value.rgb.x==0);
	e.geometry.angularRadiusRadians.value=0; CHECK(!P::UniformSunDiskIncidentRadiance(a,e,Vec3(0,0,-1)));
}
void FieldsScaling() {
	auto a=Asset(); EnvironmentLighting env; env.source.id=200; env.source.provenance=1; env.meanLuminance.calibration=11; env.meanLuminance.unit=PhotometricUnit::Nits;
	a.environments.push_back(env);
	SourceUncertainty u; u.id=400; u.source=200; u.track=200; u.provenance=1;
	u.method="synthetic uncertainty/1"; u.inputHash="synthetic";
	for(unsigned i=0;i<13;++i) { UncertaintyFactor factor; factor.cause=static_cast<UncertaintyCause>(i); factor.reason="unknown"; u.factors.push_back(factor); }
	a.uncertainty.push_back(u); CHECK(Validate(a));
	auto l=P::EnvironmentRadiance(a,env,Vec3(2,2,2)); CHECK(l);
	CHECK(l.value.receipt.uncertainty.size()==1 && !l.value.receipt.uncertainty[0].absoluteScaleKnown);
	auto e=P::UniformHemisphereIrradiance(a,l.value); CHECK(e && Near(e.value.rgb.x,2*pi));
	std::vector<P::IncidentSample> samples;
	const unsigned muSteps=32, phiSteps=64;
	for(unsigned i=0;i<muSteps;++i) for(unsigned j=0;j<phiSteps;++j) {
		double mu=(i+.5)/muSteps, phi=2*pi*(j+.5)/phiSteps;
		P::IncidentSample s; s.radiance=l.value; s.directionToSource=Vec3(float(std::sqrt(1-mu*mu)*std::cos(phi)),float(std::sqrt(1-mu*mu)*std::sin(phi)),float(mu));
		s.solidAngle=2*pi/(muSteps*phiSteps); samples.push_back(s);
	}
	auto integral=P::IntegrateIncidentRadiance(a,samples,Vec3(0,0,1)); CHECK(integral && Near(integral.value.rgb.x,2*pi));
	for(auto& s:samples) { s.radiance.rgb=Vec3(s.directionToSource.z,s.directionToSource.z,s.directionToSource.z); s.transmittance=.5; }
	auto cosine=P::IntegrateIncidentRadiance(a,samples,Vec3(0,0,1)); CHECK(cosine && Near(cosine.value.rgb.x,pi/3,3e-4));
	auto scene=P::ToScene(a,e.value); CHECK(scene && Near(scene.value.rgb.x,2*pi*a.units.physicalToScene));
	CHECK(P::ToScene(a,scene.value).reason==P::Reason::DoubleSceneScaling);
	auto physical=P::FromScene(a,scene.value); CHECK(physical && Near(physical.value.rgb.x,2*pi));
	auto changed=a; changed.units.physicalToScene*=2; CHECK(!P::FromScene(changed,scene.value));
	samples[0].radiance=e.value; CHECK(P::IntegrateIncidentRadiance(a,samples,Vec3(0,0,1)).reason==P::Reason::IncompatibleQuantity);
	CHECK(P::UniformHemisphereIrradiance(a,e.value).reason==P::Reason::IncompatibleQuantity);
	samples.clear(); CHECK(!P::IntegrateIncidentRadiance(a,samples,Vec3(0,0,1)));
	TransportField f; f.source.id=300; f.source.provenance=1; f.calibration=12; f.unit=PhotometricUnit::Lux;
	f.membership.model="test/path/1"; TransportRoot root; root.source=200; root.kind=TransportRootKind::Sky; f.membership.roots.push_back(root);
	f.membership.reflectionOrderKnown=true; f.membership.minimumReflectionOrder=f.membership.maximumReflectionOrder=2;
	a.transport.push_back(f); auto field=P::Field(a,f,Vec3(4,3,2)); CHECK(field);
	auto fs=P::ToScene(a,field.value); CHECK(fs && fs.value.receipt.membership.roots[0].source==200 && fs.value.receipt.membership.maximumReflectionOrder==2);
	CHECK(!P::RequirePhysical(a,field.value));
	ContributionReference ref; ref.sources.push_back(200); ref.unit=PhotometricUnit::Nits; ref.quantity=ContributionQuantity::RadianceRGB; ref.calibration=11;
	ref.domain="test/incident"; ref.membership=f.membership;
	auto contribution=P::Contribution(a,ref,200,Vec3(1,2,3)); CHECK(contribution);
	CHECK(contribution.value.receipt.sources==ref.sources && contribution.value.receipt.membership.roots[0].source==200 && contribution.value.receipt.domain==ref.domain);
	CHECK(contribution.value.receipt.fromContribution && contribution.value.receipt.contributionComponent==ref.component);
	ref.quantity=ContributionQuantity::LegacyRelativeChannels; CHECK(!P::Contribution(a,ref,200,Vec3(1,2,3)));
}
void RelativeSafety() {
	auto a=Asset(); P::Value v; v.rgb=Vec3(2,3,4); v.calibration=13; v.receipt.domain="test/linear-relative";
	CHECK(P::CalibrateRelative(a,v).reason==P::Reason::UnknownPrimaries);
	v.receipt.rec709PrimariesAttested=true; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::UnknownTransfer);
	v.receipt.linearTransferAttested=true; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::UnknownAbsoluteScale);
	CHECK(!P::ToScene(a,v)); CHECK(!P::RequirePhysical(a,v));
	auto& m=a.calibrations[2].relativeMapping; m.version=1; m.inputDomain=v.receipt.domain; m.referenceDistribution="synthetic independent grey receiver population /1";
	m.referenceSamples=100; m.rec709PrimariesAttested=m.linearTransferAttested=true; m.outputUnit=PhotometricUnit::Lux;
	m.quantity=PhotometricQuantity::ReceiverIrradiance; m.targetCalibration=12; m.worldUnitsPerMeter=a.units.worldUnitsPerMeter;
	m.referenceP50=Known(2); m.referenceP95=Known(4); m.canonicalUnitsPerRelative=Known(10);
	m.canonicalUnitsPerRelative.bounded=true; m.canonicalUnitsPerRelative.lower=8; m.canonicalUnitsPerRelative.upper=12;
	CHECK(Validate(a)); v.lowerKnown=v.upperKnown=true; v.lower=Vec3(1,2,3); v.upper=Vec3(3,4,5);
	auto equivalent=P::CalibrateRelative(a,v); CHECK(equivalent && equivalent.value.domain==P::Domain::SceneEquivalent);
	CHECK(equivalent.value.rgb.x==20 && equivalent.value.lower.x==8 && equivalent.value.upper.x==36);
	CHECK(!P::RequirePhysical(a,equivalent.value)); CHECK(P::ToScene(a,equivalent.value));
	CHECK(equivalent.value.receipt.source.evidence.exactLight==v.receipt.source.evidence.exactLight);
	v.receipt.profile="different profile"; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::ProfileDependent); v.receipt.profile.clear();
	v.receipt.configuration=123; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::AmbiguousConfiguration); v.receipt.configuration=0;
	v.receipt.source.hypothesisGroup=99; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::AmbiguousConfiguration); v.receipt.source.hypothesisGroup=0;
	v.quantity=PhotometricQuantity::IncidentRadiance; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::IncompatibleQuantity); v.quantity=PhotometricQuantity::ReceiverIrradiance;
	v.domain=P::Domain::CompilerRelativeQ; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::ProfileDependent);
	m.profile=v.receipt.profile="test/compiler-profile/1"; CHECK(P::CalibrateRelative(a,v).reason==P::Reason::AmbiguousConfiguration);
	LightingConfiguration config; config.id=71; config.provenance=2; config.key="test"; config.profile=m.profile; config.calibrationHypothesis="conditional synthetic"; a.lightingConfigurations.push_back(config);
	m.configuration=v.receipt.configuration=71; CHECK(P::CalibrateRelative(a,v));
	// Q conversion cannot overwrite the original canonical object or its evidence.
	CHECK(v.unit==PhotometricUnit::Relative && v.rgb.x==2 && v.calibration==13);
	for(EmitterKind kind : {EmitterKind::Point,EmitterKind::Spot,EmitterKind::Rectangle,EmitterKind::Tube,EmitterKind::Directional}) {
		auto emitter=Emitter(kind); emitter.emission.unit=PhotometricUnit::Relative; emitter.emission.calibration=13;
		auto relative=P::Emission(a,emitter); CHECK(relative);
		relative.value.receipt.domain=m.inputDomain; relative.value.receipt.profile=m.profile; relative.value.receipt.configuration=71;
		m.quantity=relative.value.quantity; m.outputUnit=kind==EmitterKind::Point || kind==EmitterKind::Spot ? PhotometricUnit::Candela :
			kind==EmitterKind::Directional ? PhotometricUnit::Lux : PhotometricUnit::Nits;
		auto converted=P::CalibrateRelative(a,relative.value); CHECK(converted);
		P::Conversion<P::Value> receiver;
		if(kind==EmitterKind::Point || kind==EmitterKind::Spot) receiver=P::PointReceiverIrradiance(a,emitter,Receiver(),P::PointModel::IdealInverseSquare,&converted.value);
		else if(kind==EmitterKind::Directional) receiver=P::DirectionalReceiverIrradiance(a,emitter,Receiver(),&converted.value);
		else receiver=P::AreaReceiverIrradiance(a,emitter,Receiver(),32,&converted.value);
		CHECK(receiver && receiver.value.domain==P::Domain::SceneEquivalent && receiver.value.receipt.originalCalibration==13);
		CHECK(!P::RequirePhysical(a,receiver.value));
	}
	auto bad=a; bad.calibrations[2].relativeMapping.rec709PrimariesAttested=false; CHECK(!Validate(bad));
	bad=a; bad.calibrations[2].relativeMapping.targetCalibration=11; CHECK(!Validate(bad));
	CHECK(P::LegacyAdapter(v).reason==P::Reason::UnsupportedLegacyAttenuation);
}
void DecoderBoundary() {
	namespace D=Core::Decode;
	auto a=Asset(); D::Raw raw; raw.bytes[0]=255; raw.bytes[1]=30; raw.bytes[2]=1;
	D::Profile p; p.identity="source-hypothesis/1"; p.relativeDomain="test/source-linear";
	P::Receipt receipt; D::Observation o;
	CHECK(D::Decoder(p).Run(raw,o));
	CHECK(P::DecodedRelative(a,o,PhotometricQuantity::ReceiverIrradiance,13,receipt).reason==P::Reason::UnknownTransfer);
	p.source.documentedTransfer=D::Transfer::Linear; p.source.transferProvenance="synthetic documented source/1";
	CHECK(D::Decoder(p).Run(raw,o));
	CHECK(P::DecodedRelative(a,o,PhotometricQuantity::ReceiverIrradiance,13,receipt).reason==P::Reason::UnknownPrimaries);
	p.source.primaries=D::Primaries::Rec709D65;
	Vec3 reference;
	for(unsigned i=0;i<16;++i) {
		p.renderer.known=true; p.renderer.hdr=(i&1)!=0; p.renderer.physicalPbr=(i&2)!=0;
		p.renderer.linearLighting=(i&4)!=0; p.renderer.toneMap=(i&8)!=0;
		CHECK(D::Decoder(p).Run(raw,o));
		auto v=P::DecodedRelative(a,o,PhotometricQuantity::ReceiverIrradiance,13,receipt); CHECK(v);
		if(!i) reference=v.value.rgb;
		CHECK(v.value.rgb.x==reference.x && v.value.rgb.y==reference.y && v.value.rgb.z==reference.z);
		CHECK(v.value.receipt.censoredChannels==1 && v.value.lowerKnown && !v.value.upperKnown);
		CHECK(v.value.receipt.observationProfile==o.metadata && v.value.receipt.observationFlags==o.flags);
		CHECK(!P::ToScene(a,v.value) && !P::RequirePhysical(a,v.value));
	}
}
void InvalidAndBlack() {
	auto a=Asset(); auto e=Emitter(EmitterKind::Point); auto r=Receiver();
	e.emission.intensity=Known(0); e.emission.color=Chromaticity(); auto black=P::Emission(a,e); CHECK(black && black.value.rgb.x==0);
	e.emission.intensity=Estimate(); CHECK(P::Emission(a,e).reason==P::Reason::MissingValue);
	e=Emitter(EmitterKind::Point); e.emission.unit=PhotometricUnit::Relative; CHECK(!P::Emission(a,e));
	e=Emitter(EmitterKind::Point); e.emission.intensity=Known(0); e.emission.color.known=false;
	e.emission.color.rgb.x=std::numeric_limits<float>::quiet_NaN(); CHECK(!P::Emission(a,e));
	e=Emitter(EmitterKind::Point); e.emission.intensity.value=-1; CHECK(!P::Emission(a,e));
	e=Emitter(EmitterKind::Point); e.emission.intensity.value=std::numeric_limits<float>::quiet_NaN(); CHECK(!P::Emission(a,e));
	e=Emitter(EmitterKind::Point); e.emission.intensity=Known(1e10f);
	auto high=P::Emission(a,e); auto highScene=P::ToScene(a,high.value);
	CHECK(high && highScene && highScene.value.rgb.x>65504 && Near(highScene.value.rgb.x,1e6));
	e=Emitter(EmitterKind::Point); e.emission.intensity=Known(std::numeric_limits<float>::max()); e.emission.color.rgb=Vec3(1/Rend2PBR::kLumaR,0,0);
	CHECK(P::Emission(a,e).code==P::Code::Overflow);
	e=Emitter(EmitterKind::Point); r.positionWorld=Vec3(); CHECK(!P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare));
	r=Receiver(); r.normalWorld=Vec3(0,0,2); CHECK(!P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare));
	r=Receiver(); r.transmittance=std::numeric_limits<double>::infinity(); CHECK(!P::PointReceiverIrradiance(a,e,r,P::PointModel::IdealInverseSquare));
	e.kind=EmitterKind::Extension; CHECK(P::Emission(a,e).reason==P::Reason::UnsupportedGeometry);
	e=Emitter(EmitterKind::Rectangle); e.geometry.halfHeightWorld=0; CHECK(!P::AreaReceiverIrradiance(a,e,Receiver()));
	e=Emitter(EmitterKind::Rectangle); CHECK(!P::AreaReceiverIrradiance(a,e,Receiver(0)));
	e.geometry.halfWidthWorld=std::numeric_limits<float>::infinity(); CHECK(!P::EmittingAreaSquareMetres(e,a.units));
	e=Emitter(EmitterKind::Point); auto value=P::Emission(a,e); a.units.physicalToScene=std::numeric_limits<float>::max(); CHECK(P::ToScene(a,value.value).code==P::Code::Overflow);
}
} // namespace
int main() { UnitsColour(); PointSpot(); ShapesSun(); FieldsScaling(); RelativeSafety(); DecoderBoundary(); InvalidAndBlack();
	std::printf("photometry: %d checks, %d failures\n",checks,failures); return failures ? 1 : 0; }
