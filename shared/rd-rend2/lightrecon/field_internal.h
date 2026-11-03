// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_math.h"
#include "field_builder.h"
#include "geometry_key.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Fields
{
using namespace Bake::Detail;
inline Disk::Digest SceneKey(const ReconstructedLightingAsset &a)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.F(a.units.worldUnitsPerMeter);
	auto text = [&](const std::string &s) {
		k.U(s.size());
		for (auto c : s)
			k.U(uint8_t(c));
	};
	auto estimate = [&](const Estimate &e) {
		k.U(e.known);
		k.U(e.bounded);
		k.F(e.value);
		k.F(e.lower);
		k.F(e.upper);
	};
	auto photo = [&](const Photometry &p) {
		k.U(unsigned(p.unit));
		k.U(p.calibration);
		estimate(p.intensity);
		k.U(p.color.known);
		k.V(p.color.rgb);
	};
	std::vector<const PhysicalEmitter *> emitters;
	for (const auto &e : a.emitters)
		emitters.push_back(&e);
	std::sort(emitters.begin(), emitters.end(),
			  [](const PhysicalEmitter *x, const PhysicalEmitter *y) {
				  return x->source.id < y->source.id;
			  });
	for (auto e : emitters)
	{
		k.U(e->source.id);
		k.U(unsigned(e->source.state));
		k.U(e->source.hypothesisGroup);
		k.U(unsigned(e->source.evidence.exactLight));
		if (e->source.evidence.observationIssues)
		{
			text("source-observation-issues");
			k.U(e->source.evidence.observationIssues);
		}
		k.U(unsigned(e->kind));
		k.U(e->position.known);
		k.V(e->position.world);
		estimate(e->position.sigmaWorld);
		k.U(e->frame.known);
		k.V(e->frame.right);
		k.V(e->frame.up);
		k.V(e->frame.forward);
		k.U(e->geometry.known);
		k.F(e->geometry.halfWidthWorld);
		k.F(e->geometry.halfHeightWorld);
		k.F(e->geometry.halfLengthWorld);
		k.F(e->geometry.radiusWorld);
		estimate(e->geometry.effectiveRadiusWorld);
		estimate(e->geometry.angularRadiusRadians);
		k.U(unsigned(e->angular.kind));
		k.F(e->angular.cosInner);
		k.F(e->angular.cosOuter);
		k.U(e->angular.twoSided);
		k.U(e->angular.dataset);
		photo(e->emission);
	}
	std::vector<const EnvironmentLighting *> env;
	for (const auto &e : a.environments)
		env.push_back(&e);
	std::sort(env.begin(), env.end(),
			  [](const EnvironmentLighting *x, const EnvironmentLighting *y) {
				  return x->source.id < y->source.id;
			  });
	for (auto e : env)
	{
		k.U(e->source.id);
		k.U(unsigned(e->source.state));
		k.U(e->source.hypothesisGroup);
		k.U(unsigned(e->sunCoverage));
		k.U(e->radiance);
		photo(e->meanLuminance);
		for (auto &d : a.datasets)
			if (d.id == e->radiance)
			{
				text(d.contentHash);
				text(d.encoding);
				k.U(d.encodingVersion);
			}
	}
	std::vector<const Calibration *> cal;
	for (const auto &c : a.calibrations)
		cal.push_back(&c);
	std::sort(cal.begin(), cal.end(),
			  [](const Calibration *x, const Calibration *y) { return x->id < y->id; });
	for (auto c : cal)
	{
		k.U(c->id);
		k.U(unsigned(c->kind));
		text(c->method);
		estimate(c->relativeMapping.canonicalUnitsPerRelative);
		k.U(c->relativeMapping.targetCalibration);
		k.U(c->relativeMapping.configuration);
	}
	std::vector<const LightingConfiguration *> config;
	for (const auto &c : a.lightingConfigurations)
		config.push_back(&c);
	std::sort(config.begin(), config.end(),
			  [](const LightingConfiguration *x, const LightingConfiguration *y) {
				  return x->id < y->id;
			  });
	for (auto c : config)
	{
		k.U(c->id);
		text(c->key);
		text(c->profile);
		text(c->calibrationHypothesis);
		k.F(c->observationGain);
		k.U(c->firstBounce);
		for (auto &s : c->sources)
		{
			k.U(s.track);
			k.U(unsigned(s.kind));
			k.V(s.position.world);
			k.V(s.frame.right);
			k.V(s.frame.up);
			k.V(s.frame.forward);
			k.F(s.dimensions.halfWidthWorld);
			k.F(s.dimensions.halfHeightWorld);
			k.F(s.dimensions.halfLengthWorld);
			k.F(s.dimensions.radiusWorld);
			k.F(s.angular.cosInner);
			k.F(s.angular.cosOuter);
			k.V(s.amplitude);
			text(s.amplitudeDomain);
		}
		for (auto &m : c->materials)
		{
			k.U(m.material);
			k.V(m.reflectance);
			k.U(unsigned(m.status));
		}
	}
	std::vector<const SourceUncertainty *> uncertainty;
	for (const auto &u : a.uncertainty)
		uncertainty.push_back(&u);
	std::sort(uncertainty.begin(), uncertainty.end(),
			  [](const SourceUncertainty *x, const SourceUncertainty *y) { return x->id < y->id; });
	for (auto u : uncertainty)
	{
		text("source-uncertainty");
		k.U(u->id);
		k.U(u->source);
		text(u->inputHash);
	}
	return k.Finish();
}
inline void Need(bool ok, Code c, const char *s)
{
	if (!ok)
		throw Failure(c, s);
}
inline void Check(Error e)
{
	if (!e)
		throw e;
}
inline Disk::Digest MetadataBinding(const ReconstructedLightingAsset &a, uint64_t maximum)
{
	Disk::Limits limits;
	limits.maxFileBytes = limits.maxMetadataBytes = std::min<uint64_t>(32ull << 20, maximum / 4);
	Disk::Digest key;
	Need(bool(Disk::MetadataDigest(a, key, limits)), Code::MemoryLimit,
		 "Field metadata binding/copy budget");
	return key;
}
inline Angular::V AV(V v)
{
	return Angular::V{{v.x, v.y, v.z}};
}
inline V VV(Angular::V v)
{
	return V(v[0], v[1], v[2]);
}
inline bool Energy(V v)
{
	return Finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0;
}
inline const TransportField *Field(const ReconstructedLightingAsset &a, Id id)
{
	for (const auto &f : a.transport)
		if (f.source.id == id)
			return &f;
	return nullptr;
}
inline const Calibration *CalibrationAt(const ReconstructedLightingAsset &a, Id id)
{
	for (const auto &c : a.calibrations)
		if (c.id == id)
			return &c;
	return nullptr;
}
inline const Probes::Value *ValueAt(const Probes::Values &v, Id id)
{
	auto i = std::lower_bound(v.records.begin(), v.records.end(), id,
							  [](const Probes::Value &a, Id b) { return a.probe < b; });
	return i != v.records.end() && i->probe == id ? &*i : nullptr;
}
inline Disk::Digest MembershipKey(const TransportMembership &m)
{
	Core::GeometryKey k;
	k.U(m.version);
	for (const auto &s : {m.model, m.generation, m.geometry, m.profile, m.domain})
	{
		k.U(s.size());
		for (auto c : s)
			k.U(uint8_t(c));
	}
	for (const auto &r : m.roots)
	{
		k.U(r.source);
		k.U(unsigned(r.kind));
	}
	for (auto x :
		 {m.directLocal, m.directSun, m.directSky, m.legacyAmbient, m.reflected, m.unresolved})
		k.U(unsigned(x));
	k.U(m.reflectionOrderKnown);
	k.U(m.minimumReflectionOrder);
	k.U(m.maximumReflectionOrder);
	for (auto x : m.reflectionOrders)
		k.U(x);
	k.U(m.style);
	k.U(unsigned(m.colour));
	k.U(m.rootsComplete);
	k.U(m.configuration);
	k.U(m.alternativeGroup);
	k.U(m.uncertainty);
	k.U(m.numericalError.known);
	k.U(m.numericalError.bounded);
	k.F(m.numericalError.value);
	k.F(m.numericalError.lower);
	k.F(m.numericalError.upper);
	k.U(unsigned(m.addition));
	k.U(unsigned(m.replacement));
	return k.Finish();
}
} // namespace Fields
} // namespace Core
} // namespace Rend2LightRecon
