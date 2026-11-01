// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "photometry.h"
#include "../tr_pbr_contract.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Rend2LightRecon { namespace Photometric {
namespace {
const double pi = 3.14159265358979323846;
template<class T> Conversion<T> Fail(Code code, Reason reason) {
	Conversion<T> r; r.code = code; r.reason = reason; return r;
}
template<class T> Conversion<T> Good(const T& value, bool approximate = false) {
	Conversion<T> r; r.code = approximate ? Code::Approximate : Code::Ok;
	r.reason = Reason::None; r.value = value; return r;
}
bool Positive(double x) { return std::isfinite(x) && x > 0; }
bool Nonnegative(double x) { return std::isfinite(x) && x >= 0; }
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Energy(const Vec3& v) { return Finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0; }
double Dot(const Vec3& a, const Vec3& b) { return double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z; }
bool Unit(const Vec3& v) { return Finite(v) && std::abs(Dot(v,v)-1) <= kUnitTolerance; }
bool FrameOK(const Frame& f) {
	if (!f.known || !Unit(f.right) || !Unit(f.up) || !Unit(f.forward)) return false;
	const Vec3 c(f.right.y*f.up.z-f.right.z*f.up.y, f.right.z*f.up.x-f.right.x*f.up.z,
		f.right.x*f.up.y-f.right.y*f.up.x);
	return std::abs(Dot(f.right,f.up)) <= kUnitTolerance &&
		std::abs(Dot(f.right,f.forward)) <= kUnitTolerance &&
		std::abs(Dot(f.up,f.forward)) <= kUnitTolerance && Dot(c,f.forward) >= 1-kUnitTolerance;
}
bool Store(double d, float& f) {
	if (!Nonnegative(d) || d > std::numeric_limits<float>::max()) return false;
	f = static_cast<float>(d); return d == 0 || f != 0;
}
bool ScaleRGB(const Vec3& in, double scale, Vec3& out) {
	return Store(double(in.x)*scale,out.x) && Store(double(in.y)*scale,out.y) && Store(double(in.z)*scale,out.z);
}
bool Interval(const Estimate& e, bool positive = false) {
	if (!std::isfinite(e.value) || !std::isfinite(e.lower) || !std::isfinite(e.upper)) return false;
	const double minimum = positive ? std::numeric_limits<float>::denorm_min() : 0;
	return (!e.known || e.value >= minimum) &&
		(!e.bounded || (e.lower >= minimum && e.upper >= e.lower &&
		(!e.known || (e.value >= e.lower && e.value <= e.upper))));
}
PhotometricUnit UnitFor(PhotometricQuantity q) {
	switch(q) {
	case PhotometricQuantity::EmittedIntensity: return PhotometricUnit::Candela;
	case PhotometricQuantity::EmittedRadiance: case PhotometricQuantity::IncidentRadiance: return PhotometricUnit::Nits;
	default: return PhotometricUnit::Lux;
	}
}
PhotometricQuantity QuantityFor(EmitterKind k) {
	return k == EmitterKind::Point || k == EmitterKind::Spot ? PhotometricQuantity::EmittedIntensity :
		k == EmitterKind::Directional ? PhotometricQuantity::DirectionalIlluminance : PhotometricQuantity::EmittedRadiance;
}
bool ValueOK(const Value& v) {
	if (unsigned(v.quantity) > unsigned(PhotometricQuantity::ReceiverIrradiance) ||
		unsigned(v.domain) > unsigned(Domain::SceneScaled) || unsigned(v.unit) > unsigned(PhotometricUnit::Lux) ||
		!Energy(v.rgb) || !Energy(v.lower) || !Energy(v.upper)) return false;
	if ((v.lowerKnown && (v.lower.x > v.rgb.x || v.lower.y > v.rgb.y || v.lower.z > v.rgb.z)) ||
		(v.upperKnown && (v.upper.x < v.rgb.x || v.upper.y < v.rgb.y || v.upper.z < v.rgb.z))) return false;
	if (v.domain == Domain::CompilerRelativeQ || v.domain == Domain::SourceLinearRelative)
		return v.unit == PhotometricUnit::Relative && v.appliedSceneScale == 0;
	return v.unit == UnitFor(v.quantity) && (v.domain == Domain::SceneScaled ?
		Positive(v.appliedSceneScale) : v.appliedSceneScale == 0);
}
const Calibration* CalibrationAt(const ReconstructedLightingAsset& a, Id id) {
	const Calibration* found = nullptr;
	for (const auto& c : a.calibrations) if (c.id == id) { if (found) return nullptr; found = &c; }
	return found;
}
void Attach(const ReconstructedLightingAsset& a, Value& v, const Source& source) {
	v.receipt.source = source;
	v.receipt.originalCalibration = v.calibration;
	for (const auto& u : a.uncertainty) {
		bool include = u.source == source.id;
		for (const auto& root : v.receipt.membership.roots) include = include || root.source == u.source;
		for (Id id : v.receipt.sources) include = include || id == u.source;
		if (include) v.receipt.uncertainty.push_back(u);
	}
}
Conversion<Value> Checked(const ReconstructedLightingAsset& a, const Value& v) {
	if (!ValueOK(v)) return Fail<Value>(Code::Invalid, Reason::InvalidNumber);
	if (!Validate(a)) return Fail<Value>(Code::Invalid, Reason::InvalidCalibration);
	if (v.domain == Domain::SceneScaled) return Fail<Value>(Code::Incomparable, Reason::SceneScaledInput);
	const auto* c = CalibrationAt(a,v.calibration);
	if (!c) return Fail<Value>(Code::Unavailable, Reason::UnknownAbsoluteScale);
	if (v.domain == Domain::PhysicalSI && c->kind == CalibrationKind::PhysicallyAuthored) return Good(v);
	if (v.domain == Domain::SceneEquivalent && c->kind == CalibrationKind::LegacySceneEquivalent) return Good(v);
	return Fail<Value>(Code::Unavailable, Reason::UnknownAbsoluteScale);
}
Conversion<Value> Scale(const Value& v, double gain, double lo, double hi, bool bounded) {
	if (!ValueOK(v) || !Nonnegative(gain) || (bounded && (!Nonnegative(lo) || !Nonnegative(hi) || lo > gain || hi < gain)))
		return Fail<Value>(Code::Invalid, Reason::InvalidNumber);
	Value out = v;
	if (!ScaleRGB(v.rgb,gain,out.rgb) ||
		(v.lowerKnown && !ScaleRGB(v.lower,bounded ? lo : gain,out.lower)) ||
		(v.upperKnown && !ScaleRGB(v.upper,bounded ? hi : gain,out.upper)))
		return Fail<Value>(Code::Overflow, Reason::NumericalOverflow);
	// Unknown multiplicative uncertainty cannot create a certified bound.
	return Good(out);
}
Conversion<Value> Photo(const ReconstructedLightingAsset& a, const Photometry& p, PhotometricQuantity q, const Source& s) {
	if (!Validate(a) || !Interval(p.intensity) || (p.unit != PhotometricUnit::Relative && p.unit != UnitFor(q)))
		return Fail<Value>(Code::Invalid, Reason::InvalidCalibration);
	if (!p.intensity.known) return Fail<Value>(Code::Unavailable, Reason::MissingValue);
	const Calibration* c = CalibrationAt(a,p.calibration);
	if (!c) return Fail<Value>(Code::Unavailable, Reason::UnknownAbsoluteScale);
	Value v; v.quantity=q; v.unit=p.unit; v.calibration=p.calibration;
	v.domain = c->kind == CalibrationKind::PhysicallyAuthored ? Domain::PhysicalSI :
		c->kind == CalibrationKind::LegacySceneEquivalent ? Domain::SceneEquivalent : Domain::SourceLinearRelative;
	Vec3 colour;
	if (p.color.known) {
		if (!Energy(p.color.rgb) || std::abs(Rend2PBR::kLumaR*double(p.color.rgb.x) +
			Rend2PBR::kLumaG*double(p.color.rgb.y) + Rend2PBR::kLumaB*double(p.color.rgb.z)-1) > kUnitTolerance)
			return Fail<Value>(Code::Invalid, Reason::InvalidNumber);
		colour = p.color.rgb;
	} else if (p.intensity.value != 0 || (p.intensity.bounded && p.intensity.upper != 0))
		return Fail<Value>(Code::Unavailable, Reason::UnknownPrimaries);
	if (!ScaleRGB(colour,p.intensity.value,v.rgb)) return Fail<Value>(Code::Overflow, Reason::NumericalOverflow);
	if (p.intensity.bounded) {
		v.lowerKnown = v.upperKnown = true;
		if (!ScaleRGB(colour,p.intensity.lower,v.lower) || !ScaleRGB(colour,p.intensity.upper,v.upper))
			return Fail<Value>(Code::Overflow, Reason::NumericalOverflow);
	}
	v.receipt.rec709PrimariesAttested = v.receipt.linearTransferAttested = p.color.known;
	Attach(a,v,s); return Good(v);
}
bool ReceiverOK(const Receiver& r) { return Finite(r.positionWorld) && Unit(r.normalWorld) &&
	Nonnegative(r.transmittance) && r.transmittance <= 1; }
bool SameReceipt(const Receipt& a, const Receipt& b) {
	const auto& x=a.membership; const auto& y=b.membership;
	if (a.source.id!=b.source.id || a.source.provenance!=b.source.provenance || a.sources!=b.sources ||
		a.fromField!=b.fromField || a.fromContribution!=b.fromContribution ||
		a.fieldComponent!=b.fieldComponent || a.contributionComponent!=b.contributionComponent ||
		a.configuration!=b.configuration || a.sampleDomain!=b.sampleDomain || a.dataset!=b.dataset ||
		a.domain!=b.domain || a.profile!=b.profile || a.originalCalibration!=b.originalCalibration ||
		a.observationProfile!=b.observationProfile || a.observationFlags!=b.observationFlags || a.censoredChannels!=b.censoredChannels ||
		x.version!=y.version || x.model!=y.model || x.style!=y.style || x.roots.size()!=y.roots.size() ||
		x.directLocal!=y.directLocal || x.directSun!=y.directSun || x.directSky!=y.directSky ||
		x.legacyAmbient!=y.legacyAmbient || x.reflected!=y.reflected || x.unresolved!=y.unresolved ||
		x.reflectionOrderKnown!=y.reflectionOrderKnown || x.minimumReflectionOrder!=y.minimumReflectionOrder ||
		x.maximumReflectionOrder!=y.maximumReflectionOrder || a.uncertainty.size()!=b.uncertainty.size()) return false;
	for (size_t i=0;i<x.roots.size();++i) if (x.roots[i].source!=y.roots[i].source || x.roots[i].kind!=y.roots[i].kind) return false;
	for (size_t i=0;i<a.uncertainty.size();++i) if (a.uncertainty[i].id!=b.uncertainty[i].id) return false;
	return true;
}
Conversion<Value> Irradiance(const Value& v, double factor, bool approximate=false) {
	auto r=Scale(v,factor,factor,factor,false);
	if (r) { r.value.unit=PhotometricUnit::Lux; r.value.quantity=PhotometricQuantity::ReceiverIrradiance;
		if (approximate) r.code=Code::Approximate; }
	return r;
}
Conversion<Value> EmitterValue(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Value* converted) {
	auto v=converted ? Good(*converted) : Emission(a,e);
	if (!v) return v;
	if (v.value.quantity!=QuantityFor(e.kind) || (converted && v.value.receipt.source.id!=e.source.id))
		return Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
	return Checked(a,v.value);
}
} // namespace

Conversion<double> WorldDistanceMetres(double w, const Units& u) {
	if (!Nonnegative(w) || !Positive(u.worldUnitsPerMeter)) return Fail<double>(Code::Invalid,Reason::InvalidNumber);
	const double m=w/u.worldUnitsPerMeter;
	return Nonnegative(m) && (w==0 || m!=0) ? Good(m) : Fail<double>(Code::Overflow,Reason::NumericalOverflow);
}
Conversion<double> MetresWorldDistance(double m, const Units& u) {
	if (!Nonnegative(m) || !Positive(u.worldUnitsPerMeter)) return Fail<double>(Code::Invalid,Reason::InvalidNumber);
	const double w=m*u.worldUnitsPerMeter;
	return Nonnegative(w) && (m==0 || w!=0) ? Good(w) : Fail<double>(Code::Overflow,Reason::NumericalOverflow);
}
Conversion<Photometry> RGBToPhotometry(const Vec3& rgb, PhotometricUnit unit, Id calibration) {
	if (!Energy(rgb) || unsigned(unit)>unsigned(PhotometricUnit::Lux)) return Fail<Photometry>(Code::Invalid,Reason::InvalidNumber);
	Photometry p; p.unit=unit; p.calibration=calibration; p.intensity.known=true;
	const double y=Rend2PBR::kLumaR*double(rgb.x)+Rend2PBR::kLumaG*double(rgb.y)+Rend2PBR::kLumaB*double(rgb.z);
	if (!Store(y,p.intensity.value)) return Fail<Photometry>(Code::Overflow,Reason::NumericalOverflow);
	if (y>0) { p.color.known=true;
		if (!ScaleRGB(rgb,1/y,p.color.rgb)) return Fail<Photometry>(Code::Overflow,Reason::NumericalOverflow); }
	return Good(p);
}
Conversion<Value> DecodedRelative(const ReconstructedLightingAsset& a, const Core::Decode::Observation& o,
	PhotometricQuantity quantity, Id calibration, const Receipt& receipt) {
	namespace D=Core::Decode;
	if (!Validate(a) || !o.metadata || !D::ValidateProfile(*o.metadata)) return Fail<Value>(Code::Invalid,Reason::InvalidCalibration);
	if (o.resolvedTransfer==D::Transfer::Unknown || (o.flags&D::TransferAssumed))
		return Fail<Value>(Code::Unavailable,Reason::UnknownTransfer);
	if (o.resolvedPrimaries!=D::Primaries::Rec709D65 || (o.flags&D::PrimariesAssumed))
		return Fail<Value>(Code::Unavailable,Reason::UnknownPrimaries);
	if (o.source.state!=D::State::Available || o.source.space!=D::Space::LinearSourceRelative)
		return Fail<Value>(Code::Unavailable,Reason::MissingValue);
	if (o.metadata->relativeDomain.empty()) return Fail<Value>(Code::Incomparable,Reason::InvalidCalibration);
	const auto* c=CalibrationAt(a,calibration);
	if (!c || c->kind!=CalibrationKind::UncalibratedRelative) return Fail<Value>(Code::Invalid,Reason::InvalidCalibration);
	Value v; v.rgb=o.source.rgb; v.quantity=quantity; v.calibration=calibration; v.receipt=receipt;
	v.receipt.domain=o.metadata->relativeDomain; v.receipt.profile=o.metadata->identity;
	v.receipt.originalCalibration=calibration; v.receipt.observationProfile=o.metadata;
	v.receipt.observationFlags=o.flags; v.receipt.censoredChannels=o.source.censoredChannels;
	v.receipt.rec709PrimariesAttested=v.receipt.linearTransferAttested=true;
	v.lowerKnown=v.upperKnown=true;
	for(unsigned i=0;i<3;++i) { v.lowerKnown=v.lowerKnown&&o.source.bounds[i].lowerKnown; v.upperKnown=v.upperKnown&&o.source.bounds[i].upperKnown; }
	v.lower=Vec3(o.source.bounds[0].lower,o.source.bounds[1].lower,o.source.bounds[2].lower);
	v.upper=Vec3(o.source.bounds[0].upper,o.source.bounds[1].upper,o.source.bounds[2].upper);
	return ValueOK(v) ? Good(v) : Fail<Value>(Code::Invalid,Reason::InvalidNumber);
}
Conversion<Value> Emission(const ReconstructedLightingAsset& a, const PhysicalEmitter& e) {
	if (unsigned(e.kind)<unsigned(EmitterKind::Point) || unsigned(e.kind)>unsigned(EmitterKind::Directional))
		return Fail<Value>(Code::Incomparable,Reason::UnsupportedGeometry);
	return Photo(a,e.emission,QuantityFor(e.kind),e.source);
}
Conversion<Value> EnvironmentRadiance(const ReconstructedLightingAsset& a, const EnvironmentLighting& e, const Vec3& rgb) {
	if (!Energy(rgb)) return Fail<Value>(Code::Invalid,Reason::InvalidNumber);
	// A directional sample has its own colour/value, independent of the mean.
	Photometry descriptor=e.meanLuminance; descriptor.intensity.known=true; descriptor.intensity.value=0;
	descriptor.intensity.bounded=false;
	auto r=Photo(a,descriptor,PhotometricQuantity::IncidentRadiance,e.source);
	if (r) { r.value.rgb=rgb; r.value.lowerKnown=r.value.upperKnown=false; r.value.receipt.dataset=e.radiance; }
	return r;
}
Conversion<Value> Field(const ReconstructedLightingAsset& a, const TransportField& f, const Vec3& rgb) {
	if (!Energy(rgb) || !Validate(a)) return Fail<Value>(Code::Invalid,Reason::InvalidNumber);
	const auto* c=CalibrationAt(a,f.calibration);
	if (!c) return Fail<Value>(Code::Unavailable,Reason::UnknownAbsoluteScale);
	Value v; v.rgb=rgb; v.unit=f.unit; v.calibration=f.calibration;
	v.quantity=f.quantity==FieldQuantity::Radiance ? PhotometricQuantity::IncidentRadiance : PhotometricQuantity::ReceiverIrradiance;
	v.domain=c->kind==CalibrationKind::PhysicallyAuthored ? Domain::PhysicalSI :
		c->kind==CalibrationKind::LegacySceneEquivalent ? Domain::SceneEquivalent : Domain::SourceLinearRelative;
	v.receipt.membership=f.membership; v.receipt.dataset=f.dataset;
	v.receipt.fromField=true; v.receipt.fieldComponent=f.component; Attach(a,v,f.source);
	return ValueOK(v) ? Good(v) : Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
}
Conversion<Value> Contribution(const ReconstructedLightingAsset& a, const ContributionReference& ref, Id source, const Vec3& rgb) {
	if (ref.quantity!=ContributionQuantity::IrradianceRGB && ref.quantity!=ContributionQuantity::RadianceRGB)
		return Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
	if (std::find(ref.sources.begin(),ref.sources.end(),source)==ref.sources.end())
		return Fail<Value>(Code::Invalid,Reason::InvalidCalibration);
	TransportField f; f.unit=ref.unit; f.calibration=ref.calibration; f.dataset=ref.dataset;
	f.quantity=ref.quantity==ContributionQuantity::RadianceRGB ? FieldQuantity::Radiance : FieldQuantity::Irradiance;
	f.membership=ref.membership;
	bool found=false;
	for (const auto& e:a.emitters) if(e.source.id==source) { f.source=e.source; found=true; }
	for (const auto& e:a.environments) if(e.source.id==source) { f.source=e.source; found=true; }
	for (const auto& e:a.transport) if(e.source.id==source) { f.source=e.source; found=true; }
	if (!found) return Fail<Value>(Code::Unavailable,Reason::MissingValue);
	auto r=Field(a,f,rgb);
	if (r) { r.value.receipt.sources=ref.sources; r.value.receipt.configuration=ref.configuration;
		r.value.receipt.fromField=false; r.value.receipt.fromContribution=true; r.value.receipt.contributionComponent=ref.component;
		r.value.receipt.sampleDomain=ref.sampleDomain; r.value.receipt.domain=ref.domain;
		r.value.receipt.uncertainty.clear(); Attach(a,r.value,f.source); }
	return r;
}
Conversion<Value> CalibrateRelative(const ReconstructedLightingAsset& a, const Value& v) {
	if (!ValueOK(v) || !Validate(a)) return Fail<Value>(Code::Invalid,Reason::InvalidCalibration);
	if (v.domain!=Domain::CompilerRelativeQ && v.domain!=Domain::SourceLinearRelative)
		return Fail<Value>(Code::Incomparable,Reason::DoubleSceneScaling);
	if (!v.receipt.rec709PrimariesAttested) return Fail<Value>(Code::Unavailable,Reason::UnknownPrimaries);
	if (!v.receipt.linearTransferAttested) return Fail<Value>(Code::Unavailable,Reason::UnknownTransfer);
	const auto* c=CalibrationAt(a,v.calibration);
	if (!c || c->kind!=CalibrationKind::UncalibratedRelative || !c->relativeMapping.version)
		return Fail<Value>(Code::Unavailable,Reason::UnknownAbsoluteScale);
	const auto& m=c->relativeMapping;
	if (m.inputDomain!=v.receipt.domain || m.profile!=v.receipt.profile ||
		(v.domain==Domain::CompilerRelativeQ && m.profile.empty()))
		return Fail<Value>(Code::Incomparable,Reason::ProfileDependent);
	if (m.configuration!=v.receipt.configuration)
		return Fail<Value>(Code::Incomparable,Reason::AmbiguousConfiguration);
	if ((v.domain==Domain::CompilerRelativeQ || v.receipt.source.hypothesisGroup) && !v.receipt.configuration)
		return Fail<Value>(Code::Incomparable,Reason::AmbiguousConfiguration);
	if (m.quantity!=v.quantity || m.outputUnit!=UnitFor(v.quantity))
		return Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
	const auto* target=CalibrationAt(a,m.targetCalibration);
	if (!target || target->kind!=CalibrationKind::LegacySceneEquivalent || !m.canonicalUnitsPerRelative.known)
		return Fail<Value>(Code::Unavailable,Reason::InvalidCalibration);
	auto r=Scale(v,m.canonicalUnitsPerRelative.value,m.canonicalUnitsPerRelative.lower,
		m.canonicalUnitsPerRelative.upper,m.canonicalUnitsPerRelative.bounded);
	if (r) {
		r.value.domain=Domain::SceneEquivalent; r.value.unit=m.outputUnit; r.value.calibration=m.targetCalibration;
		// Unbounded gain uncertainty cannot manufacture finite output intervals.
		if (!m.canonicalUnitsPerRelative.bounded) r.value.lowerKnown=r.value.upperKnown=false;
	}
	return r;
}
Conversion<Value> RequirePhysical(const ReconstructedLightingAsset& a, const Value& v) {
	auto r=Checked(a,v);
	return r && v.domain!=Domain::PhysicalSI ? Fail<Value>(Code::Unavailable,Reason::UnknownAbsoluteScale) : r;
}
Conversion<Value> ToScene(const ReconstructedLightingAsset& a, const Value& v) {
	if (v.domain==Domain::SceneScaled || v.appliedSceneScale!=0) return Fail<Value>(Code::Incomparable,Reason::DoubleSceneScaling);
	auto r=Checked(a,v); if (!r) return r;
	r=Scale(v,a.units.physicalToScene,0,0,false);
	if (r) { r.value.domain=Domain::SceneScaled; r.value.appliedSceneScale=a.units.physicalToScene; }
	return r;
}
Conversion<Value> FromScene(const ReconstructedLightingAsset& a, const Value& v) {
	if (!ValueOK(v) || !Validate(a)) return Fail<Value>(Code::Invalid,Reason::InvalidCalibration);
	if (v.domain!=Domain::SceneScaled || v.appliedSceneScale!=a.units.physicalToScene)
		return Fail<Value>(Code::Incomparable,Reason::DoubleSceneScaling);
	const auto* c=CalibrationAt(a,v.calibration);
	if (!c || c->kind==CalibrationKind::UncalibratedRelative) return Fail<Value>(Code::Unavailable,Reason::UnknownAbsoluteScale);
	auto r=Scale(v,1/double(v.appliedSceneScale),0,0,false);
	if (r) { r.value.domain=c->kind==CalibrationKind::PhysicallyAuthored ? Domain::PhysicalSI : Domain::SceneEquivalent;
		r.value.appliedSceneScale=0; }
	return r;
}
Conversion<double> SpotSolidAngle(const AngularDistribution& angular) {
	if (angular.kind!=AngularKind::SmoothCone) return Fail<double>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	if (!std::isfinite(angular.cosInner) || !std::isfinite(angular.cosOuter) || angular.cosOuter < -1 ||
		angular.cosInner>1 || angular.cosOuter>=angular.cosInner || angular.twoSided || angular.dataset)
		return Fail<double>(Code::Invalid,Reason::InvalidGeometry);
	return Good(2*pi*(1-(double(angular.cosInner)+angular.cosOuter)/2));
}
Conversion<double> SpotCandelaFromLumens(double lumens, const AngularDistribution& angular) {
	if (!Nonnegative(lumens)) return Fail<double>(Code::Invalid,Reason::InvalidNumber);
	auto omega=SpotSolidAngle(angular); if (!omega) return omega;
	const double cd=lumens/omega.value;
	return Nonnegative(cd) ? Good(cd) : Fail<double>(Code::Overflow,Reason::NumericalOverflow);
}
Conversion<double> EmittingAreaSquareMetres(const PhysicalEmitter& e, const Units& u) {
	if (e.kind!=EmitterKind::Rectangle && e.kind!=EmitterKind::Tube) return Fail<double>(Code::Incomparable,Reason::UnsupportedGeometry);
	if (!e.geometry.known || !Positive(u.worldUnitsPerMeter)) return Fail<double>(Code::Invalid,Reason::InvalidGeometry);
	const auto& g=e.geometry;
	const double x=e.kind==EmitterKind::Rectangle ? g.halfWidthWorld : g.radiusWorld;
	const double y=e.kind==EmitterKind::Rectangle ? g.halfHeightWorld : g.halfLengthWorld;
	if (!Positive(x) || !Positive(y)) return Fail<double>(Code::Invalid,Reason::InvalidGeometry);
	const double area=4*(e.kind==EmitterKind::Tube ? pi : 1)*(x/u.worldUnitsPerMeter)*(y/u.worldUnitsPerMeter);
	return Positive(area) ? Good(area) : Fail<double>(Code::Overflow,Reason::NumericalOverflow);
}
Conversion<Value> PointReceiverIrradiance(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Receiver& r, PointModel model, const Value* converted) {
	if (e.kind!=EmitterKind::Point && e.kind!=EmitterKind::Spot) return Fail<Value>(Code::Incomparable,Reason::UnsupportedGeometry);
	if (!ReceiverOK(r) || !e.position.known || !Finite(e.position.world) ||
		unsigned(model)>unsigned(PointModel::FiniteRadiusBoundedApproximation)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	auto v=EmitterValue(a,e,converted); if (!v) return v;
	double x=double(e.position.world.x)-r.positionWorld.x, y=double(e.position.world.y)-r.positionWorld.y,
		z=double(e.position.world.z)-r.positionWorld.z;
	const double d=std::sqrt(x*x+y*y+z*z);
	if (!Positive(d)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	x/=d; y/=d; z/=d;
	double profile=1;
	if (e.kind==EmitterKind::Spot) {
		auto omega=SpotSolidAngle(e.angular);
		if (!omega) return Fail<Value>(omega.code,omega.reason);
		if (!FrameOK(e.frame)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
		const double cosine=-(x*e.frame.forward.x+y*e.frame.forward.y+z*e.frame.forward.z);
		const double t=std::max(0.0,std::min(1.0,(cosine-e.angular.cosOuter)/(e.angular.cosInner-e.angular.cosOuter)));
		profile=t*t*(3-2*t);
	} else if (e.angular.kind!=AngularKind::Isotropic || e.angular.dataset || e.angular.twoSided)
		return Fail<Value>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	const double dm=d/a.units.worldUnitsPerMeter;
	double denominator=dm*dm;
	if (model==PointModel::FiniteRadiusBoundedApproximation) {
		if (!Interval(e.geometry.effectiveRadiusWorld,true) || !e.geometry.effectiveRadiusWorld.known)
			return Fail<Value>(Code::Unavailable,Reason::InvalidGeometry);
		const double radius=e.geometry.effectiveRadiusWorld.value/a.units.worldUnitsPerMeter;
		denominator=std::max(denominator,radius*radius);
	}
	return Irradiance(v.value,profile*std::max(0.0,x*r.normalWorld.x+y*r.normalWorld.y+z*r.normalWorld.z)*r.transmittance/denominator,
		model==PointModel::FiniteRadiusBoundedApproximation);
}
Conversion<Value> LambertianEmittedRadiance(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Vec3& n, const Vec3& out, const Value* converted) {
	auto area=EmittingAreaSquareMetres(e,a.units);
	if (!area) return Fail<Value>(area.code,area.reason);
	if (!Unit(n) || !Unit(out)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (!FrameOK(e.frame) || (e.kind==EmitterKind::Rectangle && Dot(n,e.frame.forward)<1-kUnitTolerance) ||
		(e.kind==EmitterKind::Tube && std::abs(Dot(n,e.frame.right))>kUnitTolerance))
		return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (e.angular.kind!=AngularKind::Lambertian || e.angular.dataset || (e.kind==EmitterKind::Tube && e.angular.twoSided))
		return Fail<Value>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	auto v=EmitterValue(a,e,converted); if (!v) return v;
	return Scale(v.value,e.angular.twoSided || Dot(n,out)>0 ? 1 : 0,0,0,false);
}
Conversion<Value> AreaReceiverIrradiance(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Receiver& r, unsigned steps, const Value* converted) {
	auto area=EmittingAreaSquareMetres(e,a.units); if (!area) return Fail<Value>(area.code,area.reason);
	if (!ReceiverOK(r) || !e.position.known || !Finite(e.position.world) || !FrameOK(e.frame) || steps<2 || steps>512)
		return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (e.angular.kind!=AngularKind::Lambertian || e.angular.dataset || (e.kind==EmitterKind::Tube && e.angular.twoSided))
		return Fail<Value>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	auto v=EmitterValue(a,e,converted); if (!v) return v;
	// Reject receiver ON the emitting surface before sampling; a quadrature node
	// need not coincide with it. Interior of the open tube is not an emitting cap.
	const double dx=double(r.positionWorld.x)-e.position.world.x, dy=double(r.positionWorld.y)-e.position.world.y,
		dz=double(r.positionWorld.z)-e.position.world.z;
	const double lx=dx*e.frame.right.x+dy*e.frame.right.y+dz*e.frame.right.z;
	const double ly=dx*e.frame.up.x+dy*e.frame.up.y+dz*e.frame.up.z;
	const double lz=dx*e.frame.forward.x+dy*e.frame.forward.y+dz*e.frame.forward.z;
	if ((e.kind==EmitterKind::Rectangle && lz==0 && std::abs(lx)<=e.geometry.halfWidthWorld && std::abs(ly)<=e.geometry.halfHeightWorld) ||
		(e.kind==EmitterKind::Tube && std::abs(lx)<=e.geometry.halfLengthWorld &&
		std::abs(std::hypot(ly,lz)-e.geometry.radiusWorld)<=1e-12*e.geometry.radiusWorld))
		return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	double factor=0;
	for (unsigned i=0;i<steps;++i) for (unsigned j=0;j<steps;++j) {
		double px,py,pz,nx,ny,nz;
		const double u=2*(i+.5)/steps-1, w=2*(j+.5)/steps-1;
		if (e.kind==EmitterKind::Rectangle) {
			px=u*e.geometry.halfWidthWorld; py=w*e.geometry.halfHeightWorld; pz=0;
			nx=e.frame.forward.x; ny=e.frame.forward.y; nz=e.frame.forward.z;
		} else {
			const double phi=pi*(w+1), c=std::cos(phi), s=std::sin(phi);
			px=u*e.geometry.halfLengthWorld; py=e.geometry.radiusWorld*c; pz=e.geometry.radiusWorld*s;
			nx=c*e.frame.up.x+s*e.frame.forward.x; ny=c*e.frame.up.y+s*e.frame.forward.y; nz=c*e.frame.up.z+s*e.frame.forward.z;
		}
		double x=double(e.position.world.x)-r.positionWorld.x+px*e.frame.right.x+py*e.frame.up.x+pz*e.frame.forward.x;
		double y=double(e.position.world.y)-r.positionWorld.y+px*e.frame.right.y+py*e.frame.up.y+pz*e.frame.forward.y;
		double z=double(e.position.world.z)-r.positionWorld.z+px*e.frame.right.z+py*e.frame.up.z+pz*e.frame.forward.z;
		const double d2=x*x+y*y+z*z, d=std::sqrt(d2);
		if (!Positive(d2)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
		x/=d; y/=d; z/=d;
		const double emit=-(nx*x+ny*y+nz*z);
		factor+=(e.angular.twoSided ? std::abs(emit) : std::max(0.0,emit))*
			std::max(0.0,x*r.normalWorld.x+y*r.normalWorld.y+z*r.normalWorld.z)/d2;
	}
	factor*=area.value*double(a.units.worldUnitsPerMeter)*a.units.worldUnitsPerMeter/(double(steps)*steps)*r.transmittance;
	return Irradiance(v.value,factor,true);
}
Conversion<Value> DirectionalReceiverIrradiance(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Receiver& r, const Value* converted) {
	if (e.kind!=EmitterKind::Directional) return Fail<Value>(Code::Incomparable,Reason::UnsupportedGeometry);
	if (!ReceiverOK(r) || !FrameOK(e.frame)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (e.angular.kind!=AngularKind::Parallel || e.angular.dataset) return Fail<Value>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	auto v=EmitterValue(a,e,converted); if (!v) return v;
	return Irradiance(v.value,std::max(0.0,-Dot(r.normalWorld,e.frame.forward))*r.transmittance);
}
Conversion<Value> UniformSunDiskIncidentRadiance(const ReconstructedLightingAsset& a, const PhysicalEmitter& e, const Vec3& direction, const Value* converted) {
	if (e.kind!=EmitterKind::Directional) return Fail<Value>(Code::Incomparable,Reason::UnsupportedGeometry);
	if (!Unit(direction) || !FrameOK(e.frame)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (e.angular.kind!=AngularKind::Parallel || e.angular.dataset)
		return Fail<Value>(Code::Incomparable,Reason::UnsupportedAngularProfile);
	const auto& radius=e.geometry.angularRadiusRadians;
	if (!radius.known) return Fail<Value>(Code::Unavailable,Reason::MissingValue);
	if (!Interval(radius,true) || radius.value>=pi/2) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	auto v=EmitterValue(a,e,converted); if (!v) return v;
	const double alpha=radius.value;
	const double factor=-Dot(direction,e.frame.forward)>=std::cos(alpha) ? 1/(pi*std::sin(alpha)*std::sin(alpha)) : 0;
	auto result=Scale(v.value,factor,0,0,false);
	if (result) { result.value.unit=PhotometricUnit::Nits; result.value.quantity=PhotometricQuantity::IncidentRadiance; }
	return result;
}
Conversion<Value> IntegrateIncidentRadiance(const ReconstructedLightingAsset& a, const std::vector<IncidentSample>& samples, const Vec3& normal) {
	if (!Unit(normal)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
	if (samples.empty()) return Fail<Value>(Code::Unavailable,Reason::MissingValue);
	double rgb[3]={}, lower[3]={}, upper[3]={}, omega=0;
	bool lowerKnown=true,upperKnown=true;
	const Value& first=samples.front().radiance;
	for (const auto& s:samples) {
		const Value& v=s.radiance;
		if (v.quantity!=PhotometricQuantity::IncidentRadiance || v.unit!=PhotometricUnit::Nits)
			return Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
		auto checked=Checked(a,v); if (!checked) return checked;
		if (!SameReceipt(v.receipt,first.receipt) || v.domain!=first.domain || v.calibration!=first.calibration)
			return Fail<Value>(Code::Incomparable,Reason::AmbiguousConfiguration);
		if (!Unit(s.directionToSource) || !Positive(s.solidAngle) || !Nonnegative(s.transmittance) || s.transmittance>1)
			return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
		omega+=s.solidAngle;
		if (!std::isfinite(omega) || omega>4*pi*(1+kUnitTolerance)) return Fail<Value>(Code::Invalid,Reason::InvalidGeometry);
		const double weight=std::max(0.0,Dot(normal,s.directionToSource))*s.solidAngle*s.transmittance;
		const double in[3]={v.rgb.x,v.rgb.y,v.rgb.z}, lo[3]={v.lower.x,v.lower.y,v.lower.z}, hi[3]={v.upper.x,v.upper.y,v.upper.z};
		for (unsigned i=0;i<3;++i) { rgb[i]+=in[i]*weight; lower[i]+=lo[i]*weight; upper[i]+=hi[i]*weight; }
		lowerKnown=lowerKnown&&v.lowerKnown; upperKnown=upperKnown&&v.upperKnown;
	}
	Value out=first; out.lowerKnown=lowerKnown; out.upperKnown=upperKnown;
	if (!Store(rgb[0],out.rgb.x) || !Store(rgb[1],out.rgb.y) || !Store(rgb[2],out.rgb.z) ||
		(lowerKnown && (!Store(lower[0],out.lower.x) || !Store(lower[1],out.lower.y) || !Store(lower[2],out.lower.z))) ||
		(upperKnown && (!Store(upper[0],out.upper.x) || !Store(upper[1],out.upper.y) || !Store(upper[2],out.upper.z))))
		return Fail<Value>(Code::Overflow,Reason::NumericalOverflow);
	out.unit=PhotometricUnit::Lux; out.quantity=PhotometricQuantity::ReceiverIrradiance;
	return Good(out,true); // arbitrary supplied quadrature has no certified integration error
}
Conversion<Value> UniformHemisphereIrradiance(const ReconstructedLightingAsset& a, const Value& v) {
	if (v.quantity!=PhotometricQuantity::IncidentRadiance || v.unit!=PhotometricUnit::Nits)
		return Fail<Value>(Code::Incomparable,Reason::IncompatibleQuantity);
	auto checked=Checked(a,v); if (!checked) return checked;
	return Irradiance(v,pi);
}
Conversion<Value> LegacyAdapter(const Value&) { return Fail<Value>(Code::Incomparable,Reason::UnsupportedLegacyAttenuation); }
} } // namespace Rend2LightRecon::Photometric
