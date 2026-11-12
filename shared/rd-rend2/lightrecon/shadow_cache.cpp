// Sparse, source-aware static rays and separately scoped bake occupancy.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shadow_cache_internal.h"
#include <chrono>
#include <map>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace ShadowCache
{
Disk::Digest SourceKey(const PhysicalEmitter &e,
					   const Rend2LightRecon::Core::SourceSupport::Contract &s)
{
	GeometryKey k;
	k.U(kVersion);
	k.U(e.source.id);
	k.U(unsigned(e.kind));
	k.U(unsigned(e.source.state));
	k.U(e.source.hypothesisGroup);
	k.U(e.position.known);
	k.V(e.position.world);
	k.U(e.position.sigmaWorld.known);
	k.U(e.position.sigmaWorld.bounded);
	k.F(e.position.sigmaWorld.value);
	k.F(e.position.sigmaWorld.upper);
	k.U(e.frame.known);
	k.V(e.frame.right);
	k.V(e.frame.up);
	k.V(e.frame.forward);
	k.U(e.geometry.known);
	k.F(e.geometry.halfWidthWorld);
	k.F(e.geometry.halfHeightWorld);
	k.F(e.geometry.halfLengthWorld);
	k.F(e.geometry.radiusWorld);
	k.U(e.geometry.effectiveRadiusWorld.known);
	k.F(e.geometry.effectiveRadiusWorld.value);
	k.U(e.geometry.angularRadiusRadians.known);
	k.F(e.geometry.angularRadiusRadians.value);
	k.U(unsigned(e.angular.kind));
	k.F(e.angular.cosInner);
	k.F(e.angular.cosOuter);
	k.U(e.angular.twoSided);
	k.U(e.angular.dataset);
	k.Digest(Rend2LightRecon::Core::SourceSupport::Key(s));
	return k.Finish();
}
namespace
{
struct Acc
{
	double total = 0, visible = 0, unknown = 0;
	void Add(const RayResult &r, bool sky, double weight, bool assumed)
	{
		if (weight <= 0)
			return;
		total += weight;
		bool known = r.reliability == RayReliability::Geometric ||
					 (assumed && r.reliability == RayReliability::Assumed);
		if (!known || r.state == VisibilityState::Unknown || (sky && r.sky == SkyReach::Unknown))
		{
			unknown += weight;
			return;
		}
		bool open = sky ? r.sky == SkyReach::Reached : r.state == VisibilityState::Visible;
		if (open)
			visible += weight;
	}
	VisibilityEstimate Value() const
	{
		VisibilityEstimate e;
		if (total <= 0)
			return e;
		e.lower = Clamp(visible / total, 0, 1);
		e.upper = Clamp((visible + unknown) / total, 0, 1);
		e.estimateKnown = unknown == 0;
		e.value = e.estimateKnown ? e.lower : 0;
		e.state = !e.estimateKnown ? ShadowState::Unknown
								   : e.value == 0 ? ShadowState::Blocked
												  : e.value == 1 ? ShadowState::Visible
																 : ShadowState::Fractional;
		return e;
	}
};
Bounds SegmentBox(Vec3 a, Vec3 b)
{
	Bounds v;
	v.known = true;
	v.minWorld = Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
	v.maxWorld = Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
	return v;
}
bool Overlap(Bounds a, Bounds b)
{
	return a.known && b.known && a.minWorld.x <= b.maxWorld.x && a.maxWorld.x >= b.minWorld.x &&
		   a.minWorld.y <= b.maxWorld.y && a.maxWorld.y >= b.minWorld.y &&
		   a.minWorld.z <= b.maxWorld.z && a.maxWorld.z >= b.minWorld.z;
}
struct SourceData
{
	const PhysicalEmitter *emitter = nullptr;
	Rend2LightRecon::Core::SourceSupport::Contract support;
	std::vector<Shape::EmissionNode> coarse, fine;
	Disk::Digest sourceKey{}, supportKey{};
	bool supported = true;
};
struct Engine
{
	const Request &r;
	const ReadContext &read;
	Artifact &out;
	std::shared_ptr<const PointRefine::PlacementProvider> placement;
	std::vector<PointRefine::OccupancyEvidence> occupancy;
	std::map<Id, SourceData> sources;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	Engine(const Request &a, const ReadContext &b, Artifact &c) : r(a), read(b), out(c)
	{
	}
	void Tick(uint64_t n = 1)
	{
		Need(!Cancelled(read), Code::Cancelled, "Shadow build cancelled");
		Need(n <= r.options.maxWork - out.work.traversal, Code::WorkLimit, "Shadow work budget");
		out.work.traversal += n;
		Need(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <=
				 r.options.maximumWallSeconds,
			 Code::WorkLimit, "Shadow wall safety budget");
	}
	RayResult Trace(const Item &i, Vec3 target, bool sky, Id surface, ShadowSupportSample &value,
					RayResult &scenario)
	{
		Tick();
		Need(out.work.rays < r.options.maxRays, Code::WorkLimit, "Shadow ray budget");
		RayQuery q;
		q.startWorld = i.receiver.positionWorld;
		q.endWorld = target;
		q.purpose = sky ? RayPurpose::Sky : RayPurpose::Shadow;
		q.policy = r.options.optical;
		q.bias = r.options.bias;
		q.bias.normalKnown = i.receiver.normalKnown;
		q.bias.geometricNormal = i.receiver.geometricNormal;
		q.bias.footprintWorld = i.receiver.footprintWorld;
		q.originSurface = i.receiver.surface;
		q.targetSurface = surface;
		q.targetToleranceWorld =
			surface ? 8 * std::numeric_limits<float>::epsilon() * (1 + Length(V(target))) : 0;
		Need(out.work.traversal < r.options.maxWork, Code::WorkLimit, "Shadow ray remaining work");
		q.maxWork = r.options.maxWork - out.work.traversal;
		RayResult raw;
		Check(r.scene.geometry->Query(q, read, raw));
		++out.work.rays;
		scenario = raw;
		Check(placement->ConstrainVisibility(q, read, scenario));
		Tick(scenario.nodeTests + scenario.primitiveTests);
		value.rayIssues |= raw.issues | scenario.issues;
		if (raw.issues & OpticalUnknown)
			value.issues |= OpticalUncertain;
		if (raw.reliability != RayReliability::Geometric || raw.state == VisibilityState::Unknown)
			value.issues |= GeometryUncertain;
		if (raw.issues & SkyBoundaryMissing)
			value.issues |= MissingSky;
		if (raw.issues & PolicyAssumption)
		{
			value.conditional = true;
			value.issues |= OpticalUncertain;
		}
		if ((scenario.reliability == RayReliability::Assumed &&
			 raw.reliability != RayReliability::Assumed) ||
			((scenario.issues & PolicyAssumption) && !(raw.issues & PolicyAssumption)))
		{
			value.conditional = true;
			value.issues |= BakeStateConditional;
		}
		if (raw.state == VisibilityState::Blocked && raw.reliability == RayReliability::Geometric &&
			raw.terminalHit.positionKnown)
		{
			double d = raw.terminalHit.distanceWorld;
			if (!value.occluderDistanceKnown || d < value.nearestStaticOccluderWorld)
			{
				value.occluderDistanceKnown = true;
				value.nearestStaticOccluderWorld = d;
			}
		}
		auto box = SegmentBox(q.startWorld, q.endWorld);
		for (auto &e : occupancy)
		{
			Tick();
			if (!e.runtimeSweptKnown || !e.runtimeSweptBounds.known)
			{
				value.runtimeUpdateRequired = true;
				value.issues |= RuntimeMotionUnknown;
			}
			else if (Overlap(box, e.runtimeSweptBounds))
			{
				value.runtimeUpdateRequired = true;
				value.issues |= RuntimeMotionRelevant;
			}
		}
		return raw;
	}
	SourceData &Source(Id id)
	{
		auto found = sources.find(id);
		if (found != sources.end())
			return found->second;
		Need(sources.size() < 4096, Code::MemoryLimit, "Shadow source batch budget");
		SourceData s;
		s.emitter = Emitter(*r.asset, id);
		Need(s.emitter, Code::InvalidInput, "Shadow source ID not an emitter");
		for (auto &support : r.support)
			if (support.source == id)
				s.support = support.geometry;
		if (s.support.kind != Rend2LightRecon::Core::SourceSupport::Kind::LegacyVacuum)
			Need(s.support.map == out.descriptor.map &&
					 s.support.geometry == out.descriptor.geometry,
				 Code::InvalidState, "Stale source mounting geometry");
		s.sourceKey = SourceKey(*s.emitter, {});
		s.supportKey = Rend2LightRecon::Core::SourceSupport::Key(s.support);
		auto kind = s.emitter->kind;
		if (kind != EmitterKind::Directional)
			s.supported =
				s.emitter->position.known &&
				(kind == EmitterKind::Point || s.emitter->frame.known) &&
				(kind == EmitterKind::Point || kind == EmitterKind::Spot ||
				 s.emitter->geometry.known) &&
				(kind == EmitterKind::Point || kind == EmitterKind::Spot ||
				 kind == EmitterKind::Rectangle || kind == EmitterKind::Tube) &&
				(kind != EmitterKind::Spot || s.emitter->angular.kind == AngularKind::SmoothCone);
		if (s.emitter->kind == EmitterKind::Directional)
		{
			s.supported = s.emitter->frame.known &&
						  s.emitter->angular.kind == AngularKind::Parallel &&
						  (!s.emitter->geometry.angularRadiusRadians.known ||
						   s.emitter->geometry.angularRadiusRadians.value == 0);
		}
		else if (s.supported)
		{
			if (s.support.kind != Rend2LightRecon::Core::SourceSupport::Kind::LegacyVacuum)
			{
				auto staticScene = r.scene;
				staticScene.dynamic.clear();
				staticScene.occupancy.clear();
				staticScene.occupancyPolicy = PointRefine::OccupancyPolicy::RuntimeConservative;
				staticScene.occupancyProvenance.clear();
				std::shared_ptr<const PointRefine::PlacementProvider> staticPlacement;
				Check(PointRefine::MakePlacement(staticScene, read, staticPlacement));
				Rend2LightRecon::Core::SourceSupport::Result supportResult;
				Check(Rend2LightRecon::Core::SourceSupport::Check(staticScene, *staticPlacement,
																  *s.emitter, *s.emitter, s.support,
																  read, supportResult));
				Tick(supportResult.work);
				out.work.rays += supportResult.rays;
				s.supported = supportResult.sourceSideCertified &&
							  PointRefine::Admissible(supportResult.placement);
			}
			auto error =
				Shape::EmissionNodes(*s.emitter, s.support, r.options.sampling, read, s.coarse);
			if (!error)
			{
				if (error.code == Code::Unavailable)
					s.supported = false;
				else
					Check(error);
			}
			if (s.supported &&
				(s.emitter->kind == EmitterKind::Rectangle || s.emitter->kind == EmitterKind::Tube))
			{
				auto fine = r.options.sampling;
				fine.areaDivisions = std::min(32u, fine.areaDivisions * 2);
				fine.tubeSides = std::min(64u, fine.tubeSides * 2);
				fine.tubeSegments = std::min(32u, fine.tubeSegments * 2);
				bool different = s.emitter->kind == EmitterKind::Rectangle
									 ? fine.areaDivisions != r.options.sampling.areaDivisions
									 : (fine.tubeSides != r.options.sampling.tubeSides ||
										fine.tubeSegments != r.options.sampling.tubeSegments);
				if (different)
					Check(Shape::EmissionNodes(*s.emitter, s.support, fine, read, s.fine));
			}
			if (s.supported && s.support.mountKnown && s.support.surfaces.size() == 1)
			{
				auto endpoint = [&](Shape::EmissionNode &n) {
					double error = 8 * std::numeric_limits<float>::epsilon() *
								   (1 + Length(V(n.positionWorld)));
					if (!n.surface &&
						std::abs(Dot(V(s.support.outward),
									 V(n.positionWorld) - V(s.support.anchorWorld))) <= error)
						n.surface = s.support.surfaces[0];
				};
				for (auto &n : s.coarse)
					endpoint(n);
				for (auto &n : s.fine)
					endpoint(n);
			}
		}
		uint64_t bytes = (s.coarse.size() + s.fine.size()) * sizeof(Shape::EmissionNode);
		for (auto &x : sources)
			bytes += (x.second.coarse.size() + x.second.fine.size()) * sizeof(Shape::EmissionNode);
		Need(bytes <= (read.maxBytes ? read.maxBytes : r.options.maxBytes) / 4, Code::MemoryLimit,
			 "Shadow source quadrature budget");
		return sources.emplace(id, std::move(s)).first->second;
	}
	ShadowSupportSample Evaluate(const Item &i, SourceData *s,
								 const std::vector<Shape::EmissionNode> *nodes)
	{
		ShadowSupportSample v;
		v.dataset = r.dataset;
		v.receiver = i.receiver.id;
		v.source = i.source;
		v.region = i.receiver.region;
		v.scenario = r.scenario;
		Acc stat, scen, response, responseScen;
		auto ray = [&](Vec3 target, bool sky, Id surface, double area, double weight) {
			RayResult conditional;
			auto raw = Trace(i, target, sky, surface, v, conditional);
			stat.Add(raw, sky, area, false);
			scen.Add(conditional, sky, area, true);
			response.Add(raw, sky, weight, false);
			responseScen.Add(conditional, sky, weight, true);
			++v.coarseSamples;
		};
		if (i.kind == ShadowQueryKind::Segment)
			ray(i.targetOrDirection, false, 0, 1, 1);
		else if (i.kind == ShadowQueryKind::SkyDirection)
			ray((V(i.receiver.positionWorld) + V(i.targetOrDirection) * r.options.skyDistanceWorld)
					.Float(),
				true, 0, 1, 1);
		else
		{
			auto &e = *s->emitter;
			if ((e.position.sigmaWorld.known && e.position.sigmaWorld.value > 0) ||
				(e.position.sigmaWorld.bounded && e.position.sigmaWorld.upper > 0) ||
				e.source.state != HypothesisState::Resolved || e.source.hypothesisGroup)
				v.issues |= SourceUncertain;
			if (!s->supported)
			{
				v.issues |= UnsupportedSource;
				return v;
			}
			if (e.kind == EmitterKind::Directional)
			{
				V w = V(e.frame.forward) * -1;
				double weight = i.receiver.normalKnown
									? std::max(0., Dot(V(i.receiver.geometricNormal), w))
									: 1;
				ray((V(i.receiver.positionWorld) + w * r.options.skyDistanceWorld).Float(), true, 0,
					1, weight);
			}
			else
				for (auto &n : *nodes)
				{
					Tick();
					V delta = V(n.positionWorld) - V(i.receiver.positionWorld);
					double distance = Length(delta);
					Need(distance > 1e-8, Code::Unavailable, "Singular receiver/source support");
					V w = delta * (1 / distance);
					double angular = 1, cosine = n.finiteSurface ? -Dot(V(n.normalWorld), w) : 1;
					if (n.finiteSurface && e.angular.twoSided)
						cosine = std::abs(cosine);
					if (n.finiteSurface && cosine <= 0)
						continue;
					if (!n.finiteSurface && s->support.mountKnown &&
						Dot(V(s->support.outward),
							V(n.positionWorld) - V(s->support.anchorWorld)) == 0 &&
						Dot(V(s->support.outward), w) >= 0)
						angular = 0;
					if (e.kind == EmitterKind::Spot)
					{
						double t = Clamp((-Dot(V(e.frame.forward), w) - e.angular.cosOuter) /
											 (e.angular.cosInner - e.angular.cosOuter),
										 0, 1);
						angular = t * t * (3 - 2 * t);
					}
					double weight = cosine * angular * n.areaWorldSquared / (distance * distance);
					if (i.receiver.normalKnown)
						weight *= std::max(0., Dot(V(i.receiver.geometricNormal), w));
					ray(n.positionWorld, false, n.surface, n.areaWorldSquared, weight);
				}
		}
		v.staticGeometry = stat.Value();
		v.scenarioGeometry = scen.Value();
		v.staticResponse = response.Value();
		v.scenarioResponse = responseScen.Value();
		v.responseApplicable = response.total > 0;
		if (!v.responseApplicable)
			v.issues |= OutsideEmission;
		return v;
	}
};
} // namespace
Error Build(const Request &r, const ReadContext &read, Artifact &out)
{
	out = {};
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Shadow build cancelled");
		Need(r.asset && r.scene.geometry && r.scene.domain && r.dataset && r.provenance &&
				 !r.scope.empty(),
			 Code::InvalidInput, "Shadow input leases/identity missing");
		uint64_t cap =
			read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
		Metadata(*r.asset, cap);
		std::string why;
		Need(Rend2LightRecon::Validate(*r.asset, &why), Code::InvalidInput, why.c_str());
		Need(std::isfinite(r.options.skyDistanceWorld) && r.options.skyDistanceWorld > 0 &&
				 std::isfinite(r.options.maximumSamplingDifference) &&
				 r.options.maximumSamplingDifference >= 0 &&
				 std::isfinite(r.options.maximumWallSeconds) && r.options.maximumWallSeconds > 0 &&
				 unsigned(r.options.storage) <= unsigned(Storage::UNorm16),
			 Code::InvalidInput, "Shadow numerical settings");
		Need(r.items.size() <= r.options.maxEntries && r.items.size() * sizeof(Record) <= cap / 2 &&
				 r.support.size() <= 4096,
			 Code::MemoryLimit, "Shadow sparse entry budget");
		Need(std::none_of(r.asset->datasets.begin(), r.asset->datasets.end(),
						  [&](const Dataset &d) { return d.id == r.dataset; }),
			 Code::InvalidInput, "Shadow dataset ID already present");
		ReadContext effective = read;
		effective.maxBytes = cap;
		Engine e{r, effective, out};
		Check(PointRefine::MakePlacement(r.scene, effective, e.placement));
		e.occupancy = PointRefine::CapturedOccupancy(r.scene);
		Need(r.scenario || r.scene.occupancyPolicy != PointRefine::OccupancyPolicy::ConditionalBake,
			 Code::InvalidInput, "Conditional bake cache needs explicit scenario ID");
		auto &d = out.descriptor;
		d.dataset = r.dataset;
		d.provenance = r.provenance;
		d.scenario = r.scenario;
		d.scope = r.scope;
		d.occupancyProvenance = r.scene.occupancyProvenance;
		for (const auto &record : e.occupancy)
		{
			d.occupancyProvenance += "; model=" + std::to_string(record.model) +
									 " selected-state=" + std::to_string(record.selectedState) +
									 " swept-known=" + std::to_string(record.runtimeSweptKnown) +
									 " " + record.provenance;
			for (const auto &state : record.states)
				if (state.id == record.selectedState)
					d.occupancyProvenance += "; " + state.provenance +
											 " verified=" + std::to_string(state.verified) +
											 " state-generation=" + Disk::Hex(state.generation);
		}
		Need(d.occupancyProvenance.size() <= 65536 && d.scope.size() <= 65536, Code::MemoryLimit,
			 "Shadow scenario provenance budget");
		d.map = r.scene.geometry->GeometryIdentity().sha256;
		d.geometry = r.scene.geometry->CertificationKey();
		d.storage = r.options.storage;
		d.areaDivisions = r.options.sampling.areaDivisions;
		d.tubeSides = r.options.sampling.tubeSides;
		d.tubeSegments = r.options.sampling.tubeSegments;
		d.optical = r.options.optical;
		d.bias = r.options.bias;
		d.skyDistanceWorld = r.options.skyDistanceWorld;
		d.maximumSamplingDifference = r.options.maximumSamplingDifference;
		GeometryKey policy;
		policy.U(kVersion);
		policy.Digest(e.placement->PolicyKey());
		policy.U(unsigned(r.options.optical.facing));
		policy.U(unsigned(r.options.optical.alphaTested));
		policy.U(unsigned(r.options.optical.translucent));
		policy.U(r.options.optical.acceptPatchApproximation);
		policy.U(r.options.optical.requireKnownFreeOrigin);
		policy.F(r.options.bias.absoluteWorld);
		policy.F(r.options.bias.relative);
		policy.F(r.options.bias.maximumWorld);
		policy.F(r.options.skyDistanceWorld);
		policy.F(r.options.maximumSamplingDifference);
		policy.U(r.options.sampling.areaDivisions);
		policy.U(r.options.sampling.tubeSides);
		policy.U(r.options.sampling.tubeSegments);
		policy.U(unsigned(r.options.storage));
		policy.U(r.scenario);
		d.policy = policy.Finish();
		std::set<Id> supports;
		for (auto &s : r.support)
		{
			Need(s.source && supports.insert(s.source).second && Emitter(*r.asset, s.source),
				 Code::InvalidInput, "Duplicate/unknown mounting support");
			Check(Rend2LightRecon::Core::SourceSupport::Validate(s.geometry));
			Need(!s.geometry.mountedPointPlane, Code::Unavailable,
				 "Named point plane is not a shadow cache support");
		}
		if (r.previous)
			Check(Validate(*r.previous, read));
		std::map<Disk::Digest, const Record *> reuse;
		if (r.previous)
			for (auto &v : r.previous->records)
				reuse[v.key] = &v;
		auto items = r.items;
		std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
			return a.priority != b.priority
					   ? a.priority > b.priority
					   : std::make_tuple(unsigned(a.kind), a.source, a.receiver.id) <
							 std::make_tuple(unsigned(b.kind), b.source, b.receiver.id);
		});
		std::set<std::tuple<unsigned, Id, Id>> ids;
		std::map<Id, Disk::Digest> receiverKeys;
		out.work.requested = items.size();
		for (auto &i : items)
		{
			e.Tick();
			auto receiverOnly = i;
			receiverOnly.kind = ShadowQueryKind::Segment;
			receiverOnly.source = 0;
			receiverOnly.targetOrDirection = Vec3();
			auto receiverKey = ItemKey(receiverOnly, {}, {}, {});
			auto receiver = receiverKeys.emplace(i.receiver.id, receiverKey);
			Need(receiver.second || receiver.first->second == receiverKey, Code::InvalidInput,
				 "One stable receiver ID has different geometry");
			Need(i.receiver.id && Finite(i.receiver.positionWorld) && Finite(i.targetOrDirection) &&
					 unsigned(i.kind) <= unsigned(ShadowQueryKind::SkyDirection) &&
					 Finite(i.receiver.geometricNormal) &&
					 (!i.receiver.normalKnown || Normal(i.receiver.geometricNormal)) &&
					 std::isfinite(i.receiver.footprintWorld) && i.receiver.footprintWorld >= 0,
				 Code::InvalidInput, "Invalid receiver/target support");
			Need(ids.emplace(unsigned(i.kind), i.source, i.receiver.id).second, Code::InvalidInput,
				 "Duplicate shadow sample identity");
			Need(i.kind != ShadowQueryKind::SkyDirection || Normal(i.targetOrDirection),
				 Code::InvalidInput, "Sky direction not unit");
			Need(i.kind != ShadowQueryKind::Segment ||
					 !Same(i.receiver.positionWorld, i.targetOrDirection),
				 Code::InvalidInput, "Zero visibility segment");
			Record row;
			row.item = i;
			SourceData *source = i.kind == ShadowQueryKind::Source ? &e.Source(i.source) : nullptr;
			if (source)
			{
				row.sourceKey = source->sourceKey;
				row.supportKey = source->supportKey;
			}
			else
				Need(!i.source || Emitter(*r.asset, i.source) ||
						 std::any_of(
							 r.asset->environments.begin(), r.asset->environments.end(),
							 [&](const EnvironmentLighting &x) { return x.source.id == i.source; }),
					 Code::InvalidInput, "Unknown segment/sky source ID");
			row.key = ItemKey(i, d.policy, row.sourceKey, row.supportKey);
			auto cached = reuse.find(row.key);
			if (cached != reuse.end())
			{
				row.result = cached->second->result;
				row.result.dataset = r.dataset;
				++out.work.reused;
			}
			else
			{
				try
				{
					row.result = e.Evaluate(i, source, source ? &source->coarse : nullptr);
					if (source && (source->emitter->kind == EmitterKind::Rectangle ||
								   source->emitter->kind == EmitterKind::Tube))
					{
						row.result.issues |= FiniteSampling;
						row.result.conditional = true;
						if (source->fine.empty())
							row.result.issues |= ResolutionLimited;
					}
					if (source && !source->fine.empty())
					{
						auto fine = e.Evaluate(i, source, &source->fine);
						fine.referenceSamples = fine.coarseSamples;
						fine.coarseSamples = row.result.coarseSamples;
						auto difference = [](const VisibilityEstimate &a,
											 const VisibilityEstimate &b) {
							return std::max(std::abs(a.lower - b.lower),
											std::abs(a.upper - b.upper));
						};
						fine.samplingDifference =
							std::max(difference(row.result.staticGeometry, fine.staticGeometry),
									 difference(row.result.staticResponse, fine.staticResponse));
						fine.issues |= FiniteSampling;
						fine.conditional = true;
						if (fine.samplingDifference > r.options.maximumSamplingDifference)
							fine.issues |= SamplingDisagreement;
						row.result = fine;
					}
				}
				catch (const Error &error)
				{
					if (error.code != Code::Unavailable)
						throw;
					row.result.dataset = r.dataset;
					row.result.receiver = i.receiver.id;
					row.result.source = i.source;
					row.result.region = i.receiver.region;
					row.result.scenario = r.scenario;
					row.result.issues |= GeometryUncertain | UnsupportedSource;
				}
				++out.work.built;
			}
			auto &v = row.result;
			out.work.staticKnown += v.staticGeometry.estimateKnown;
			out.work.staticUnknown += !v.staticGeometry.estimateKnown;
			out.work.scenarioConditional += v.conditional;
			out.work.unsupported += (v.issues & UnsupportedSource) != 0;
			out.records.push_back(std::move(row));
		}
		std::sort(out.records.begin(), out.records.end(), RecordLess);
		d.key = Content(d, out.records);
		out.work.bytes = out.records.size() * sizeof(Record);
		out.work.seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - e.start).count();
		d.coverage = out.work;
		out.asset = *r.asset;
		if (out.asset.assessment.present)
		{
			out.asset.assessment.payloadsValidated = false;
			out.asset.assessment.coverage.limitations.push_back(
				"P05 static shadow support is separately prepared; source-solver assessment is not "
				"blanket validation of added visibility payloads");
		}
		Dataset data;
		data.id = r.dataset;
		data.purpose = DatasetPurpose::Visibility;
		data.layout = DatasetLayout::Chunked;
		data.chunkElements = 64;
		data.elementCount = out.records.size() + 1;
		data.encoding = "org.openjk.static-shadow-support";
		data.encodingVersion = kVersion;
		data.locator = "static-shadow:" + std::to_string(r.dataset);
		out.asset.datasets.push_back(data);
		for (auto kind : {VisibilityKind::SegmentTransmittance, VisibilityKind::SkyTransmittance})
		{
			VisibilityReference ref;
			ref.dataset = r.dataset;
			ref.kind = kind;
			out.asset.visibility.push_back(ref);
		}
		Check(Encode(out, read));
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Shadow cache allocation");
	}
}
Error ProbeItems(const Probes::Topology &t, const std::vector<Id> &sources, size_t maximum,
				 std::vector<Item> &out)
{
	out.clear();
	try
	{
		Need(sources.empty() || t.Probes().size() <= maximum / sources.size(), Code::MemoryLimit,
			 "Source/probe requested support budget");
		for (auto source : sources)
			for (auto &p : t.Probes())
			{
				if (p.classification != Probes::Classification::Valid &&
					p.classification != Probes::Classification::Relocated)
					continue;
				Item i;
				i.source = source;
				i.receiver.id = p.id;
				i.receiver.region = p.region;
				i.receiver.positionWorld = p.positionWorld;
				out.push_back(i);
			}
		return {};
	}
	catch (const Error &e)
	{
		out.clear();
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out.clear();
		return Failure(Code::MemoryLimit, "Source/probe request allocation");
	}
}
} // namespace ShadowCache
} // namespace Core
} // namespace Rend2LightRecon
