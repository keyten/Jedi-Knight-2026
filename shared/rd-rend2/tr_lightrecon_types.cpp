// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_lightrecon_types.h"
#include "tr_pbr_contract.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <algorithm>

namespace Rend2LightRecon {
namespace {

bool Finite(float x) { return std::isfinite(x); }
bool Finite(const Vec3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
bool Nonnegative(float x) { return Finite(x) && x >= 0.0f; }
bool Positive(float x) { return Finite(x) && x > 0.0f; }
bool ValidUnits(const Units& u) { return Positive(u.worldUnitsPerMeter) && Positive(u.physicalToScene); }
double Dot(const Vec3& a, const Vec3& b)
{
	return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}
bool Unit(const Vec3& v) { return Finite(v) && std::abs(Dot(v, v) - 1.0) <= kUnitTolerance; }
bool ValidFrame(const Frame& f)
{
	if (!Finite(f.right) || !Finite(f.up) || !Finite(f.forward)) return false;
	if (!f.known) return true;
	const Vec3 cross(f.right.y * f.up.z - f.right.z * f.up.y,
		f.right.z * f.up.x - f.right.x * f.up.z, f.right.x * f.up.y - f.right.y * f.up.x);
	return Unit(f.right) && Unit(f.up) && Unit(f.forward) &&
		std::abs(Dot(f.right, f.up)) <= kUnitTolerance &&
		std::abs(Dot(f.right, f.forward)) <= kUnitTolerance &&
		std::abs(Dot(f.up, f.forward)) <= kUnitTolerance &&
		Dot(cross, f.forward) >= 1.0 - kUnitTolerance;
}
bool ValidEstimate(const Estimate& e, float minimum, float maximum)
{
	if (!Finite(e.value) || !Finite(e.lower) || !Finite(e.upper)) return false;
	if (e.known && (e.value < minimum || e.value > maximum)) return false;
	if (e.bounded && (e.lower < minimum || e.upper > maximum || e.lower > e.upper)) return false;
	return !(e.known && e.bounded && (e.value < e.lower || e.value > e.upper));
}
bool Nonnegative(const Estimate& e)
{
	return ValidEstimate(e, 0.0f, std::numeric_limits<float>::max());
}
bool Confidence(const Estimate& e) { return ValidEstimate(e, 0.0f, 1.0f); }
bool ValidColor(const Chromaticity& c)
{
	if (!Finite(c.rgb)) return false;
	if (!c.known) return true;
	return c.rgb.x >= 0.0f && c.rgb.y >= 0.0f && c.rgb.z >= 0.0f &&
		std::abs(double(Rend2PBR::kLumaR) * c.rgb.x + double(Rend2PBR::kLumaG) * c.rgb.y +
			double(Rend2PBR::kLumaB) * c.rgb.z - 1.0) <= kUnitTolerance;
}
bool ValidBounds(const Bounds& b)
{
	return Finite(b.minWorld) && Finite(b.maxWorld) && (!b.known ||
		(b.minWorld.x <= b.maxWorld.x && b.minWorld.y <= b.maxWorld.y && b.minWorld.z <= b.maxWorld.z));
}
template<typename T> bool EnumIn(T x, T last)
{
	return static_cast<unsigned>(x) <= static_cast<unsigned>(last);
}
bool StoreFloat(double value, float& out)
{
	if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
	out = static_cast<float>(value);
	return true;
}

struct Validator
{
	const ReconstructedLightingAsset& asset;
	std::string* error;
	std::map<Id, const Provenance*> provenance;
	std::map<Id, const Calibration*> calibrations;
	std::map<Id, const Dataset*> datasets;
	std::map<Id, const GeometryRegion*> regions;
	std::map<Id, const HypothesisGroup*> groups;
	std::map<Id, const Source*> sources;
	std::map<Id, const PhysicalEmitter*> emitters;
	std::map<Id, unsigned> resolved;

	Validator(const ReconstructedLightingAsset& a, std::string* e) : asset(a), error(e) {}
	bool Fail(const std::string& path, const char* reason)
	{
		if (error) *error = path + ": " + reason;
		return false;
	}
	template<typename T> bool Index(const std::vector<T>& records, std::map<Id, const T*>& map,
		const char* path)
	{
		for (const T& record : records)
			if (!record.id || !map.insert(std::make_pair(record.id, &record)).second)
				return Fail(path, "zero or duplicate ID");
		return true;
	}
	bool AddSource(const Source& source)
	{
		return (source.id && sources.insert(std::make_pair(source.id, &source)).second) ||
			Fail("sources", "zero or duplicate source ID");
	}
	bool Data(Id id, DatasetPurpose purpose, const std::string& path, bool required = false)
	{
		if (!id) return !required || Fail(path, "dataset reference required");
		const auto it = datasets.find(id);
		return (it != datasets.end() && it->second->purpose == purpose) ||
			Fail(path, "missing dataset or wrong purpose");
	}
	bool CalibrationRef(Id id, PhotometricUnit unit, const std::string& path, bool required)
	{
		if (!EnumIn(unit, PhotometricUnit::Lux)) return Fail(path, "invalid photometric unit");
		if (!id) return (!required && unit == PhotometricUnit::Relative) ||
			Fail(path, "calibration reference required");
		const auto it = calibrations.find(id);
		if (it == calibrations.end()) return Fail(path, "missing calibration");
		return ((unit == PhotometricUnit::Relative) ==
			(it->second->kind == CalibrationKind::UncalibratedRelative)) ||
			Fail(path, "relative/photometric unit conflicts with calibration");
	}
	bool Photo(const Photometry& p, PhotometricUnit expected, const std::string& path)
	{
		if (!Nonnegative(p.intensity) || !ValidColor(p.color) ||
			(p.unit != expected && p.unit != PhotometricUnit::Relative))
			return Fail(path, "invalid intensity, chromaticity or unit");
		return CalibrationRef(p.calibration, p.unit, path + ".calibration", p.intensity.known || p.intensity.bounded);
	}
	bool Common(const Source& s, const std::string& path, bool physical)
	{
		if (!provenance.count(s.provenance)) return Fail(path + ".provenance", "missing provenance");
		if (s.hypothesisGroup && !groups.count(s.hypothesisGroup))
			return Fail(path + ".hypothesisGroup", "missing group");
		if (!EnumIn(s.state, HypothesisState::Rejected) || !Confidence(s.hypothesisWeight))
			return Fail(path, "invalid hypothesis state or weight");
		if (s.hypothesisGroup && s.state == HypothesisState::Resolved && ++resolved[s.hypothesisGroup] > 1)
			return Fail(path, "multiple resolved alternatives in one group");
		std::set<Id> seen;
		for (Id id : s.regions)
			if (!regions.count(id) || !seen.insert(id).second) return Fail(path + ".regions", "missing or duplicate region");
		const Evidence& e = s.evidence;
		if (!Confidence(e.physicalEmitterConfidence) || !Confidence(e.transportConfidence) ||
			!EnumIn(e.exactLight, Suitability::Candidate) || !EnumIn(e.approximateIrradiance, Suitability::Candidate))
			return Fail(path + ".evidence", "invalid confidence or suitability");
		if (e.exactLight == Suitability::Candidate && (!physical || s.state == HypothesisState::Rejected))
			return Fail(path + ".evidence.exactLight", "only non-rejected physical emitters can be exact candidates");
		return true;
	}
	bool UncertaintyRecords()
	{
		std::map<Id,const LightingConfiguration*> configs;
		std::map<Id,const SourceUncertainty*> records;
		if(!Index(asset.lightingConfigurations,configs,"lightingConfigurations")||!Index(asset.uncertainty,records,"uncertainty"))return false;
		for(const auto& c:asset.lightingConfigurations){
			if(!provenance.count(c.provenance)||c.key.empty()||c.profile.empty()||c.calibrationHypothesis.empty())return Fail("lightingConfigurations","missing method identity/provenance");
			if(!Positive(c.observationGain))return Fail("lightingConfigurations.observationGain","invalid gain");
			std::set<Id> materials;for(const auto& m:c.materials)if(!m.material||!materials.insert(m.material).second||!Finite(m.reflectance)||m.reflectance.x<0||m.reflectance.y<0||m.reflectance.z<0||m.reflectance.x>1||m.reflectance.y>1||m.reflectance.z>1||!EnumIn(m.status,UncertaintyStatus::Contradicted))return Fail("lightingConfigurations.materials","invalid passive reflectance");
			std::set<Id> tracks;for(const auto& s:c.sources){
				if(!s.track||!tracks.insert(s.track).second||s.amplitudeDomain.empty()||!Finite(s.amplitude)||s.amplitude.x<0||s.amplitude.y<0||s.amplitude.z<0)return Fail("lightingConfigurations.sources","invalid track/amplitude domain");
				if(!EnumIn(s.root,TransportRootKind::Unknown)||!Nonnegative(s.legacySpotDistanceWorld)||!Nonnegative(s.legacySpotRadiusWorld))return Fail("lightingConfigurations.sources","invalid root/spot convention");
				if(s.root==TransportRootKind::Sky||s.root==TransportRootKind::LegacyAmbient||s.root==TransportRootKind::Unknown){if(!Finite(s.position.world)||!ValidFrame(s.frame))return Fail("lightingConfigurations.sources","invalid environment geometry");continue;}
				if(s.root==TransportRootKind::Sun&&s.kind!=EmitterKind::Directional)return Fail("lightingConfigurations.sources","sun must be directional");
				PhysicalEmitter e;e.kind=s.kind;e.position=s.position;e.frame=s.frame;e.geometry=s.dimensions;e.angular=s.angular;
				e.source.id=s.track;e.source.provenance=c.provenance;
				if(!Emitter(e,"lightingConfigurations.sources"))return false;
			}
		}
		auto calibrated=[&](const CalibratedConfidence& c){
			if(!EnumIn(c.meaning,ConfidenceMeaning::IndependentEmpiricalBinRate)||!Confidence(c.probability)||!Finite(c.intervalNominal))return false;
			if(c.meaning==ConfidenceMeaning::Unknown)return !c.probability.known&&!c.probability.bounded;
			return c.probability.known&&c.probability.bounded&&!c.event.empty()&&!c.population.empty()&&!c.protocol.empty()&&!c.calibrationHash.empty()&&c.independentFamilies&&c.intervalNominal>0&&c.intervalNominal<1;
		};
		std::set<Id> assessed;
		for(const auto& item:asset.uncertainty){
			const auto& lo=item.peakColourLower;const auto& hi=item.peakColourUpper;
			if(!Finite(lo)||!Finite(hi)||(item.peakColourBounded&&(lo.x<0||lo.y<0||lo.z<0||hi.x>1||hi.y>1||hi.z>1||lo.x>hi.x||lo.y>hi.y||lo.z>hi.z)))return Fail("uncertainty.peakColour","invalid peak-normalized projection");
			if(!sources.count(item.source)||!item.track||!assessed.insert(item.source).second||!provenance.count(item.provenance)||item.version!=1||item.method.empty()||item.inputHash.empty())return Fail("uncertainty","invalid source/method/provenance");
			if(!calibrated(item.existence)||!calibrated(item.type)||!calibrated(item.exactUse)||!calibrated(item.fieldUse))return Fail("uncertainty.confidence","unsupported probability claim");
			if(!Nonnegative(item.relativePeak)||!ValidEstimate(item.directionSpreadRadians,0,3.14159265359f)||item.minimumCount>item.maximumCount||!EnumIn(item.exactLight,Suitability::Candidate)||!EnumIn(item.approximateField,Suitability::Candidate))return Fail("uncertainty","invalid bounds/suitability");
			std::set<unsigned> causes;for(const auto& f:item.factors)if(!EnumIn(f.cause,UncertaintyCause::Numerical)||!EnumIn(f.status,UncertaintyStatus::Contradicted)||f.reason.empty()||!causes.insert(unsigned(f.cause)).second)return Fail("uncertainty.factors","invalid/missing cause");
			if(causes.size()!=13)return Fail("uncertainty.factors","every cause must be explicitly assessed or unknown");
			std::set<Id> refs;for(Id id:item.configurations){if(!configs.count(id)||!refs.insert(id).second)return Fail("uncertainty.configurations","missing/duplicate configuration");if(std::none_of(configs[id]->sources.begin(),configs[id]->sources.end(),[&](const JointParameterState& s){return s.track==item.track;}))return Fail("uncertainty.configurations","track absent from referenced configuration");}
			auto same=[](const Estimate& a,const Estimate& b){return a.known==b.known&&a.bounded==b.bounded&&a.value==b.value&&a.lower==b.lower&&a.upper==b.upper;};
			const auto& evidence=sources[item.source]->evidence;
			if(!same(item.existence.probability,evidence.physicalEmitterConfidence)||!same(item.fieldUse.probability,evidence.transportConfidence)||item.exactLight!=evidence.exactLight||item.approximateField!=evidence.approximateIrradiance)return Fail("uncertainty.evidence","typed and compatibility evidence disagree");
			const auto& s=item.positionScatter;
			if(!Finite(s.reference)||!Finite(s.xx)||!Finite(s.xy)||!Finite(s.xz)||!Finite(s.yy)||!Finite(s.yz)||!Finite(s.zz))return Fail("uncertainty.positionScatter","nonfinite covariance");
			if(s.known){double scale=std::max(1.,std::max(double(s.xx),std::max(double(s.yy),double(s.zz))));double eps=scale*scale*1e-5;
				if(s.samples<2||s.method.empty()||s.xx<0||s.yy<0||s.zz<0||double(s.xx)*s.yy-double(s.xy)*s.xy < -eps||double(s.xx)*s.zz-double(s.xz)*s.xz < -eps||double(s.yy)*s.zz-double(s.yz)*s.yz < -eps)return Fail("uncertainty.positionScatter","invalid PSD scatter");
				double det=double(s.xx)*(double(s.yy)*s.zz-double(s.yz)*s.yz)-double(s.xy)*(double(s.xy)*s.zz-double(s.xz)*s.yz)+double(s.xz)*(double(s.xy)*s.yz-double(s.xz)*s.yy);
				if(det < -eps*scale)return Fail("uncertainty.positionScatter","non PSD scatter");
			}
			if(item.exactLight==Suitability::Candidate&&(!item.complete||!item.absoluteScaleKnown||item.exactUse.meaning==ConfidenceMeaning::Unknown||sources[item.source]->evidence.exactLight!=Suitability::Candidate))return Fail("uncertainty.exactLight","unsupported exact-light suitability");
		}
		return true;
	}
	bool Emitter(const PhysicalEmitter& e, const std::string& path)
	{
		if (!Common(e.source, path + ".source", true)) return false;
		const uint32_t kind = static_cast<uint32_t>(e.kind);
		const bool extension = kind >= static_cast<uint32_t>(EmitterKind::Extension);
		if ((!extension && (kind < 1 || kind > 5)) || (extension && e.extensionType.empty()))
			return Fail(path + ".kind", "invalid kind or absent extension key");
		if (!Finite(e.position.world) || !Nonnegative(e.position.sigmaWorld) || !ValidFrame(e.frame))
			return Fail(path, "invalid position, positional uncertainty or frame");
		const EmitterGeometry& g = e.geometry;
		if (!Nonnegative(g.halfWidthWorld) || !Nonnegative(g.halfHeightWorld) ||
			!Nonnegative(g.halfLengthWorld) || !Nonnegative(g.radiusWorld) ||
			!Nonnegative(g.effectiveRadiusWorld) || !Nonnegative(g.angularRadiusRadians) ||
			(g.effectiveRadiusWorld.known && g.effectiveRadiusWorld.value <= 0.0f) ||
			(g.effectiveRadiusWorld.bounded && g.effectiveRadiusWorld.upper <= 0.0f) ||
			(g.angularRadiusRadians.known && g.angularRadiusRadians.value >= 1.57079632679f) ||
			(g.angularRadiusRadians.bounded && g.angularRadiusRadians.upper >= 1.57079632679f))
			return Fail(path + ".geometry", "invalid dimension, radius or angular size");
		if (g.known && ((e.kind == EmitterKind::Rectangle &&
			(!Positive(g.halfWidthWorld) || !Positive(g.halfHeightWorld))) ||
			(e.kind == EmitterKind::Tube && (!Positive(g.halfLengthWorld) || !Positive(g.radiusWorld)))))
			return Fail(path + ".geometry", "degenerate rectangle or tube");
		if (!Data(g.extension, DatasetPurpose::EmitterGeometry, path + ".geometry.extension", extension)) return false;
		if (!Nonnegative(e.rangeWorld) || (e.rangeWorld.known && e.rangeWorld.value <= 0.0f) ||
			(e.rangeWorld.bounded && e.rangeWorld.upper <= 0.0f) ||
			(e.kind == EmitterKind::Directional && (e.position.known || e.rangeWorld.known || e.rangeWorld.bounded)))
			return Fail(path + ".rangeWorld", "invalid finite range or finite directional position");
		const PhotometricUnit unit = e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot ?
			PhotometricUnit::Candela : e.kind == EmitterKind::Directional ? PhotometricUnit::Lux : PhotometricUnit::Nits;
		if (extension)
		{
			if (!Nonnegative(e.emission.intensity) || !ValidColor(e.emission.color))
				return Fail(path + ".emission", "invalid extension intensity or chromaticity");
			if (!CalibrationRef(e.emission.calibration, e.emission.unit, path + ".emission.calibration",
				e.emission.intensity.known || e.emission.intensity.bounded)) return false;
		}
		else if (!Photo(e.emission, unit, path + ".emission")) return false;
		const AngularDistribution& a = e.angular;
		if (!EnumIn(a.kind, AngularKind::Tabulated) || !Finite(a.cosInner) || !Finite(a.cosOuter) ||
			a.cosOuter < -1.0f || a.cosInner > 1.0f || a.cosOuter > a.cosInner ||
			(a.kind == AngularKind::SmoothCone && a.cosInner == a.cosOuter))
			return Fail(path + ".angular", "invalid angular profile or cone");
		if (!Data(a.dataset, DatasetPurpose::AngularProfile, path + ".angular.dataset", a.kind == AngularKind::Tabulated)) return false;
		if (a.kind == AngularKind::Tabulated && !e.frame.known)
			return Fail(path + ".frame", "tabulated profile needs a known local frame");
		if (!extension && a.kind != AngularKind::Unknown && a.kind != AngularKind::Tabulated)
		{
			const AngularKind expected = e.kind == EmitterKind::Point ? AngularKind::Isotropic :
				e.kind == EmitterKind::Spot ? AngularKind::SmoothCone :
				e.kind == EmitterKind::Directional ? AngularKind::Parallel : AngularKind::Lambertian;
			if (a.kind != expected) return Fail(path + ".angular.kind", "profile conflicts with emitter kind");
		}
		if (a.twoSided && (e.kind != EmitterKind::Rectangle || a.kind != AngularKind::Lambertian))
			return Fail(path + ".angular.twoSided", "two-sided flag requires a Lambertian rectangle");
		if (e.source.evidence.exactLight == Suitability::Candidate &&
			(extension || !e.emission.intensity.known || !e.emission.color.known ||
			 e.emission.unit == PhotometricUnit::Relative || a.kind == AngularKind::Unknown ||
			 (e.kind != EmitterKind::Directional && !e.position.known) ||
			 (e.kind != EmitterKind::Point && !e.frame.known) ||
			 ((e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot) && !g.effectiveRadiusWorld.known) ||
			 ((e.kind == EmitterKind::Rectangle || e.kind == EmitterKind::Tube) && !g.known)))
			return Fail(path + ".source.evidence.exactLight", "incomplete or unsupported exact-light candidate");
		return true;
	}

	bool Run()
	{
		if (asset.schema != kSchema || asset.version != kSchemaVersion) return Fail("schema", "unsupported schema/version");
		if (!ValidUnits(asset.units)) return Fail("units", "scales must be finite and positive");
		if (!Index(asset.provenanceRecords, provenance, "provenanceRecords") ||
			!Index(asset.calibrations, calibrations, "calibrations") || !Index(asset.datasets, datasets, "datasets") ||
			!Index(asset.regions, regions, "regions") || !Index(asset.hypothesisGroups, groups, "hypothesisGroups")) return false;
		for (const PhysicalEmitter& e : asset.emitters)
		{
			if (!AddSource(e.source)) return false;
			emitters[e.source.id] = &e;
		}
		for (const EnvironmentLighting& e : asset.environments) if (!AddSource(e.source)) return false;
		for (const TransportField& f : asset.transport) if (!AddSource(f.source)) return false;
		if (asset.provenance && !provenance.count(asset.provenance)) return Fail("provenance", "missing root provenance");
		if ((!sources.empty() || !datasets.empty() || !regions.empty() || !calibrations.empty()) &&
			(asset.lineage.empty() || !provenance.count(asset.provenance)))
			return Fail("lineage", "nonempty reconstruction needs lineage and root provenance");

		// Kahn traversal validates provenance DAG without recursive stack growth.
		std::map<Id, size_t> dependencies;
		std::map<Id, std::vector<Id>> children;
		std::vector<Id> ready;
		for (const Provenance& p : asset.provenanceRecords)
		{
			if (!EnumIn(p.kind, ProvenanceKind::External) || p.producer.empty())
				return Fail("provenanceRecords", "invalid kind or missing producer/version");
			if (!Data(p.observationQuality, DatasetPurpose::ObservationQuality, "provenanceRecords.observationQuality")) return false;
			std::set<Id> seen;
			for (Id parent : p.parents)
			{
				if (!provenance.count(parent) || !seen.insert(parent).second) return Fail("provenanceRecords.parents", "missing or duplicate parent");
				children[parent].push_back(p.id);
			}
			dependencies[p.id] = p.parents.size();
			if (p.parents.empty()) ready.push_back(p.id);
		}
		for (size_t i = 0; i < ready.size(); ++i)
			for (Id child : children[ready[i]]) if (--dependencies[child] == 0) ready.push_back(child);
		if (ready.size() != provenance.size()) return Fail("provenanceRecords.parents", "cycle");
		for (const Calibration& c : asset.calibrations)
		{
			if (!EnumIn(c.kind, CalibrationKind::UncalibratedRelative) || !provenance.count(c.provenance))
				return Fail("calibrations", "invalid kind or missing provenance");
			if (c.kind == CalibrationKind::LegacySceneEquivalent && c.method.empty())
				return Fail("calibrations.method", "legacy equivalent needs an explicit calibration method");
			if (c.kind == CalibrationKind::PhysicallyAuthored &&
				provenance[c.provenance]->kind != ProvenanceKind::Authored && provenance[c.provenance]->kind != ProvenanceKind::External)
				return Fail("calibrations.provenance", "physical authoring cannot be inferred from legacy/reconstructed observations");
		}
		for (const Dataset& d : asset.datasets)
			if (!EnumIn(d.purpose, DatasetPurpose::ObservationQuality) || !EnumIn(d.layout, DatasetLayout::Procedural) ||
				d.locator.empty() || d.encoding.empty() || !d.encodingVersion ||
				(d.layout == DatasetLayout::Chunked && !d.chunkElements))
				return Fail("datasets", "invalid descriptor, encoding or chunk size");
		for (const GeometryRegion& r : asset.regions)
			if (!provenance.count(r.provenance) || !ValidBounds(r.bounds)) return Fail("regions", "invalid provenance or bounds");
			else if (!Data(r.membership, DatasetPurpose::RegionMembership, "regions.membership")) return false;
		for (size_t i = 0; i < asset.emitters.size(); ++i)
			if (!Emitter(asset.emitters[i], "emitters[" + std::to_string(i) + "]")) return false;
		std::set<Id> suns;
		for (const SunSource& s : asset.suns)
			if (!emitters.count(s.emitter) || emitters[s.emitter]->kind != EmitterKind::Directional || !suns.insert(s.emitter).second)
				return Fail("suns", "missing, duplicate or non-directional sun emitter");
		for (const EnvironmentLighting& e : asset.environments)
			if (!Common(e.source, "environments.source", false) || !Photo(e.meanLuminance, PhotometricUnit::Nits, "environments.meanLuminance") ||
				!Data(e.radiance, DatasetPurpose::Environment, "environments.radiance") ||
				!CalibrationRef(e.meanLuminance.calibration, e.meanLuminance.unit, "environments.calibration", e.radiance != 0)) return false;
			else if (!EnumIn(e.sunCoverage, SunCoverage::IncludesExplicitSun)) return Fail("environments.sunCoverage", "invalid sun coverage");
		for (const TransportField& f : asset.transport)
		{
			if (!Common(f.source, "transport.source", false)) return false;
			if (!EnumIn(f.component, TransportComponent::UnresolvedIrradiance) || !EnumIn(f.quantity, FieldQuantity::Radiance) ||
				!ValidBounds(f.bounds)) return Fail("transport", "invalid component, quantity or bounds");
			const bool irradiance = f.quantity == FieldQuantity::Irradiance;
			if (((f.component == TransportComponent::IndirectIrradiance || f.component == TransportComponent::UnresolvedDirectIrradiance || f.component == TransportComponent::UnresolvedIrradiance) && !irradiance) ||
				((f.component == TransportComponent::ReflectedRadiance || f.component == TransportComponent::UnattributedIncidentRadiance) && irradiance) ||
				(f.unit != PhotometricUnit::Relative && f.unit != (irradiance ? PhotometricUnit::Lux : PhotometricUnit::Nits)))
				return Fail("transport.unit", "component, quantity and unit conflict");
			const uint32_t* dims = f.sampleDimensions;
			if ((dims[0] || dims[1] || dims[2]) && (!dims[0] || !dims[1] || !dims[2] || !f.bounds.known ||
				!(f.bounds.maxWorld.x > f.bounds.minWorld.x && f.bounds.maxWorld.y > f.bounds.minWorld.y && f.bounds.maxWorld.z > f.bounds.minWorld.z)))
				return Fail("transport.sampleDimensions", "lattice needs three positive dimensions and nondegenerate bounds");
			uint64_t count = 1;
			for (uint32_t d : f.sampleDimensions)
			{
				if (d && count > std::numeric_limits<uint64_t>::max() / d) return Fail("transport.sampleDimensions", "sample count overflow");
				count *= d;
			}
			if (!CalibrationRef(f.calibration, f.unit, "transport.calibration", f.dataset != 0) ||
				!Data(f.dataset, DatasetPurpose::Transport, "transport.dataset")) return false;
			if (f.dataset && dims[0] && datasets[f.dataset]->elementCount != count)
				return Fail("transport.sampleDimensions", "dataset logical count differs from lattice");
			const auto& m=f.membership;
			if(m.version!=1 || m.style>255 || !EnumIn(m.directLocal,FieldMembership::Included) ||
				!EnumIn(m.directSun,FieldMembership::Included) || !EnumIn(m.directSky,FieldMembership::Included) ||
				!EnumIn(m.legacyAmbient,FieldMembership::Included) || !EnumIn(m.reflected,FieldMembership::Included) ||
				!EnumIn(m.unresolved,FieldMembership::Included)) return Fail("transport.membership","unsupported membership");
			if(m.reflectionOrderKnown && (m.minimumReflectionOrder>m.maximumReflectionOrder || m.maximumReflectionOrder>64))
				return Fail("transport.membership","invalid reflection orders");
			if((f.component==TransportComponent::IndirectIrradiance && m.reflected==FieldMembership::Excluded) ||
				(f.component==TransportComponent::UnresolvedIrradiance && m.unresolved==FieldMembership::Excluded) ||
				(m.reflectionOrderKnown && m.maximumReflectionOrder>0 && m.reflected==FieldMembership::Excluded) ||
				(m.reflectionOrderKnown && m.maximumReflectionOrder==0 && m.reflected==FieldMembership::Included))
				return Fail("transport.membership","component/reflection membership conflict");
			std::set<Id> roots;
			for(const auto& r:m.roots) {
				if(!EnumIn(r.kind,TransportRootKind::Unknown) ||
					(r.kind!=TransportRootKind::Unknown && (!r.source || !sources.count(r.source))) ||
					(r.source && (!sources.count(r.source)||!roots.insert(r.source).second)))
					return Fail("transport.membership.roots","invalid root identity");
				if(r.kind==TransportRootKind::LocalEmitter && (!emitters.count(r.source)||emitters[r.source]->kind==EmitterKind::Directional))
					return Fail("transport.membership.roots","local root is not a local emitter");
				if(r.kind==TransportRootKind::Sun && !suns.count(r.source)) return Fail("transport.membership.roots","sun root missing");
				if(r.kind==TransportRootKind::Sky && !IsEnvironment(r.source)) return Fail("transport.membership.roots","sky root missing");
			}
			if((!m.roots.empty() || m.reflectionOrderKnown) && m.model.empty()) return Fail("transport.membership.model","model identity required");
		}
		for (const VisibilityReference& v : asset.visibility)
		{
			if (!Data(v.dataset, DatasetPurpose::Visibility, "visibility.dataset", true)) return false;
			if (!EnumIn(v.kind, VisibilityKind::SkyTransmittance) || (v.source && !sources.count(v.source)) || (v.region && !regions.count(v.region)))
				return Fail("visibility", "invalid visibility kind or dangling source/region");
		}
		for (const ContributionReference& c : asset.contributions)
		{
			if (!Data(c.dataset, DatasetPurpose::Contributions, "contributions.dataset", true) ||
				!Data(c.sampleDomain, DatasetPurpose::SampleDomain, "contributions.sampleDomain", true)) return false;
			if (!EnumIn(c.component, ContributionComponent::Unresolved) || !EnumIn(c.quantity, ContributionQuantity::AttributionFraction) || c.sources.empty())
				return Fail("contributions", "invalid component/quantity or no source IDs");
			std::set<Id> seen;
			for (Id source : c.sources)
				if (!sources.count(source) || !seen.insert(source).second ||
					(c.component == ContributionComponent::Direct && !emitters.count(source) &&
					 !IsEnvironment(source)))
					return Fail("contributions.sources", "missing/duplicate source or transport used as direct emitter");
			if (c.quantity == ContributionQuantity::AttributionFraction)
			{
				if (c.unit != PhotometricUnit::Relative || c.calibration) return Fail("contributions", "fractions are dimensionless");
			}
			else
			{
				const PhotometricUnit unit = c.quantity == ContributionQuantity::IrradianceRGB ? PhotometricUnit::Lux : PhotometricUnit::Nits;
				if (c.unit != unit && c.unit != PhotometricUnit::Relative) return Fail("contributions.unit", "wrong field unit");
				if (!CalibrationRef(c.calibration, c.unit, "contributions.calibration", true)) return false;
			}
		}
		for (const Diagnostic& d : asset.diagnostics)
			if (!EnumIn(d.severity, Severity::Error) || (d.source && !sources.count(d.source)) || (d.region && !regions.count(d.region)) ||
				!ValidEstimate(d.metric, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max()) ||
				((d.metric.known || d.metric.bounded) && d.metricUnit.empty())) return Fail("diagnostics", "invalid reference, metric or unit");
		return UncertaintyRecords();
	}
	bool IsEnvironment(Id source) const
	{
		for (const EnvironmentLighting& e : asset.environments) if (e.source.id == source) return true;
		return false;
	}
};

} // namespace

bool WorldToMeters(float world, const Units& units, float& meters)
{
	return Nonnegative(world) && ValidUnits(units) && StoreFloat(double(world) / units.worldUnitsPerMeter, meters);
}
bool MetersToWorld(float meters, const Units& units, float& world)
{
	return Nonnegative(meters) && ValidUnits(units) && StoreFloat(double(meters) * units.worldUnitsPerMeter, world);
}
bool NormalizeChromaticity(const Vec3& rgb, Chromaticity& result)
{
	if (!Finite(rgb)) return false;
	const float input[3] = { rgb.x, rgb.y, rgb.z };
	float output[3];
	if (!Rend2PBR::NormalizeChromaticity(input, output)) return false;
	Chromaticity normalized;
	normalized.known = true;
	normalized.rgb = Vec3(output[0], output[1], output[2]);
	if (!ValidColor(normalized)) return false;
	result = normalized;
	return true;
}
bool SceneEmissionRGB(const ReconstructedLightingAsset& asset, const Photometry& emission, Vec3& rgb)
{
	if (!ValidUnits(asset.units) || !emission.intensity.known || !emission.color.known ||
		!Nonnegative(emission.intensity) || !ValidColor(emission.color) ||
		emission.unit == PhotometricUnit::Relative || !EnumIn(emission.unit, PhotometricUnit::Lux)) return false;
	const Calibration* calibration = nullptr;
	for (const Calibration& c : asset.calibrations)
		if (c.id == emission.calibration) { if (calibration) return false; calibration = &c; }
	if (!calibration || (calibration->kind != CalibrationKind::PhysicallyAuthored && calibration->kind != CalibrationKind::LegacySceneEquivalent)) return false;
	const double y = double(emission.intensity.value) * asset.units.physicalToScene;
	Vec3 result;
	if (!StoreFloat(y * emission.color.rgb.x, result.x) || !StoreFloat(y * emission.color.rgb.y, result.y) ||
		!StoreFloat(y * emission.color.rgb.z, result.z)) return false;
	rgb = result;
	return true;
}
bool Validate(const ReconstructedLightingAsset& asset, std::string* error)
{
	if (error) error->clear();
	Validator validator(asset, error);
	return validator.Run();
}

} // namespace Rend2LightRecon
