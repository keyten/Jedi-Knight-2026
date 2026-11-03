// Bounded source-conditioned angular integration and explicit appearance approximation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "field_internal.h"
#include "photometry.h"
#include <chrono>
#include <iomanip>
#include <set>
#include <sstream>
namespace Rend2LightRecon
{
namespace Core
{
namespace Fields
{
namespace
{
const double pi = 3.14159265358979323846;
struct Surface
{
	Shape::EmissionNode node;
	V radiance;
	Id root = 0;
};
struct Direct
{
	PhysicalEmitter emitter;
	V amplitude;
	std::vector<Shape::EmissionNode> nodes;
	SourceSupport::Contract support;
};
struct Measure
{
	V direction, energy;
};
struct Engine
{
	const Request &r;
	const ReadContext &read;
	Artifact &out;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	std::vector<Direct> direct;
	std::vector<Surface> bounce;
	std::vector<const Environment *> skies;
	std::vector<Measure> measure;
	std::shared_ptr<ConditionalFieldDescriptor> descriptor =
		std::make_shared<ConditionalFieldDescriptor>();
	Engine(const Request &q, const ReadContext &c, Artifact &a) : r(q), read(c), out(a)
	{
	}
	void Tick(uint64_t count = 1)
	{
		Need(!Cancelled(read), Code::Cancelled, "Field build cancelled");
		Need(out.work.angularEvaluations <= r.options.maxWork &&
				 out.work.traversal <= r.options.maxWork - out.work.angularEvaluations &&
				 count <= r.options.maxWork - out.work.angularEvaluations - out.work.traversal,
			 Code::WorkLimit, "Field integration work limit");
		out.work.angularEvaluations += count;
		Need(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <=
				 r.options.maximumWallSeconds,
			 Code::WorkLimit, "Field wall-time safety limit");
	}
	void Trace(Vec3 from, Vec3 to, bool sky, Id surface, bool &visible, bool &known)
	{
		Tick();
		Need(out.work.rays < r.options.maxRays, Code::WorkLimit, "Field visibility budget");
		RayQuery ray;
		ray.startWorld = from;
		ray.endWorld = to;
		ray.purpose = sky ? RayPurpose::Sky : RayPurpose::IrradianceProbe;
		ray.policy.facing = FacingPolicy::TwoSided;
		ray.targetSurface = surface;
		ray.targetToleranceWorld =
			surface ? 8 * std::numeric_limits<float>::epsilon() * (1 + Length(V(to))) : 0;
		ray.bias.absoluteWorld = ray.bias.relative = 0;
		Need(out.work.angularEvaluations + out.work.traversal < r.options.maxWork, Code::WorkLimit,
			 "Field ray traversal work limit");
		ray.maxWork = r.options.maxWork - out.work.traversal - out.work.angularEvaluations;
		RayResult hit;
		Check(r.visibility->Query(ray, read, hit));
		++out.work.rays;
		out.work.traversal += hit.nodeTests + hit.primitiveTests;
		known =
			hit.state != VisibilityState::Unknown && hit.reliability == RayReliability::Geometric;
		visible = sky ? hit.sky == SkyReach::Reached : hit.state == VisibilityState::Visible;
		if (sky && hit.sky == SkyReach::Unknown)
			known = false;
		// An unattested contact at the emitting endpoint is not proof of a dark
		// source. Do not suppress unrelated opaque blockers or manufacture a bias.
		if (!sky && !surface && hit.state == VisibilityState::Blocked &&
			hit.terminalHit.positionKnown &&
			std::abs(hit.terminalHit.distanceWorld - Length(V(to) - V(from))) <=
				8 * std::numeric_limits<float>::epsilon() * (1 + Length(V(to))))
			known = false;
	}
	bool Segment(Vec3 x, Vec3 y, Id surface = 0)
	{
		bool visible = false, known = false;
		Trace(x, y, false, surface, visible, known);
		Need(known, Code::Unavailable, "Unknown field source visibility");
		return visible;
	}
	bool Sky(Vec3 x, V w)
	{
		bool visible = false, known = false;
		Trace(x, (V(x) + w * r.options.skyRayLengthWorld).Float(), true, 0, visible, known);
		Need(known, Code::Unavailable, "Sky boundary/optics not supported");
		return visible;
	}
	void Select()
	{
		const auto &s = r.selection;
		const auto &a = *r.asset;
		Need(unsigned(s.route) <= unsigned(Route::ObservationAppearance) &&
				 unsigned(s.domain) <= unsigned(FieldDomain::LegacyAppearance) &&
				 unsigned(s.quantity) <= unsigned(ConditionalQuantity::LegacyAppearance),
			 Code::InvalidInput, "Invalid field selection enum");
		Need(!s.alternativeGroup || s.configuration, Code::InvalidInput,
			 "Exclusive fields require an explicit configuration");
		Need(r.topology && r.visibility &&
				 r.topology->GeometryKey() == r.visibility->CertificationKey() &&
				 r.topology->MapKey() == r.visibility->GeometryIdentity().sha256,
			 Code::InvalidState, "Field stale map/topology/geometry");
		Need(s.field && s.dataset && s.provenance && s.calibration && s.geometryDataset &&
				 s.topologyDataset && s.geometryDataset != s.topologyDataset &&
				 s.geometryDataset != s.dataset && s.topologyDataset != s.dataset,
			 Code::InvalidInput, "Field IDs");
		Need(!Field(a, s.field), Code::InvalidInput, "Field source ID already exists");
		Need(r.options.degree == 2 || r.options.degree == 4, Code::UnsupportedSchema,
			 "Field supports SH L2/L4 only");
		Need(r.options.angularOrder >= 4 && r.options.angularOrder <= 64 &&
				 r.options.referenceOrder > r.options.angularOrder &&
				 r.options.referenceOrder <= 96 && r.options.maxBytes >= 65536 &&
				 std::isfinite(r.options.maximumRelativeAngularRMSE) &&
				 r.options.maximumRelativeAngularRMSE >= 0 &&
				 std::isfinite(r.options.maximumWallSeconds) && r.options.maximumWallSeconds > 0 &&
				 std::isfinite(r.options.skyRayLengthWorld) && r.options.skyRayLengthWorld > 0 &&
				 std::isfinite(r.options.absoluteErrorFloor) && r.options.absoluteErrorFloor > 0 &&
				 std::isfinite(r.options.appearanceRadiusWorld) &&
				 r.options.appearanceRadiusWorld > 0,
			 Code::InvalidInput, "Field numerical/work options");
		Need(r.topology->Probes().size() <= r.options.maxProbes, Code::WorkLimit,
			 "Field probe budget");
		Need(s.sources.size() <= 256 && s.bounceRoots.size() <= 256 &&
				 r.appearance.size() <= 65536 && r.appearanceValidation.size() <= 65536 &&
				 r.environments.size() <= 256 && r.emitterSupport.size() <= 256,
			 Code::MemoryLimit, "Field source/evidence limits");
		std::set<Id> supportIds;
		for (const auto &support : r.emitterSupport)
			Need(support.source && supportIds.insert(support.source).second &&
					 std::find(s.sources.begin(), s.sources.end(), support.source) !=
						 s.sources.end(),
				 Code::InvalidInput, "Duplicate or unselected emitting support");
		if (s.firstBounce && !s.sources.empty())
		{
			auto roots = s.bounceRoots, selected = s.sources;
			std::sort(roots.begin(), roots.end());
			std::sort(selected.begin(), selected.end());
			Need(roots == selected, Code::Unavailable,
				 "Different root/path sets require separate fields");
		}
		auto c = CalibrationAt(a, s.calibration);
		Need(c, Code::InvalidInput, "Missing field calibration");
		if (s.domain == FieldDomain::PhysicallyAuthored)
			Need(c->kind == CalibrationKind::PhysicallyAuthored && s.primariesKnown &&
					 s.linearTransferKnown && s.colour == ContributionColour::LinearRec709D65,
				 Code::Unavailable, "Physical field requires authored calibration/linear Rec709");
		if (s.domain == FieldDomain::SceneEquivalent)
			Need(c->kind == CalibrationKind::LegacySceneEquivalent, Code::InvalidInput,
				 "Scene-equivalent calibration mismatch");
		if (s.domain == FieldDomain::SourceLinearRelative ||
			s.domain == FieldDomain::LegacyAppearance)
			Need(c->kind == CalibrationKind::UncalibratedRelative, Code::InvalidInput,
				 "Relative calibration mismatch");
		if (s.route == Route::ObservationAppearance)
		{
			Need(s.quantity == ConditionalQuantity::LegacyAppearance &&
					 s.domain == FieldDomain::LegacyAppearance && !r.appearance.empty() &&
					 !r.appearanceValidation.empty() && !s.appearanceDomain.empty() &&
					 !s.directLocal && !s.sun && !s.sky && !s.firstBounce && s.sources.empty() &&
					 s.bounceRoots.empty(),
				 Code::InvalidInput, "Appearance is not a physical component decomposition");
			return;
		}
		Need(s.quantity == ConditionalQuantity::NormalIrradiance ||
				 s.quantity == ConditionalQuantity::IrradianceOverPi ||
				 s.quantity == ConditionalQuantity::IncomingRadiance,
			 Code::Unavailable, "Unsupported physical field quantity");
		Need(s.profile == "physical-emission/1" &&
				 s.colour == ContributionColour::LinearRec709D65 && s.primariesKnown &&
				 s.linearTransferKnown,
			 Code::Unavailable,
			 "Historical Q/unknown transfer cannot become a physical emission law");
		descriptor->uncertainty |= AttributionRepresentationError;
		Need(!(s.directLocal || s.sun) || s.allowDirectProjection, Code::Unavailable,
			 "Direct high-frequency projection must be explicitly requested");
		if (s.configuration)
		{
			auto cfg = std::find_if(
				a.lightingConfigurations.begin(), a.lightingConfigurations.end(),
				[&](const LightingConfiguration &x) { return x.id == s.configuration; });
			Need(cfg != a.lightingConfigurations.end() && cfg->profile == s.profile,
				 Code::InvalidInput, "Selected configuration missing");
		}
		std::set<Id> selected;
		for (Id id : s.sources)
		{
			Need(selected.insert(id).second, Code::InvalidInput, "Duplicate field root");
			auto e = std::find_if(a.emitters.begin(), a.emitters.end(),
								  [&](const PhysicalEmitter &x) { return x.source.id == id; });
			if (e != a.emitters.end())
			{
				bool sun = e->kind == EmitterKind::Directional;
				if (sun)
					Need(!e->geometry.angularRadiusRadians.known ||
							 e->geometry.angularRadiusRadians.value == 0,
						 Code::Unavailable,
						 "Finite solar extent requires a separately validated disk integration "
						 "model");
				Need(sun ? s.sun : s.directLocal, Code::InvalidInput, "Root class not selected");
				Need(e->source.state != HypothesisState::Rejected, Code::Unavailable,
					 "Rejected emitter");
				if ((e->position.sigmaWorld.known && e->position.sigmaWorld.value > 0) ||
					(e->position.sigmaWorld.bounded && e->position.sigmaWorld.upper > 0) ||
					(e->emission.intensity.bounded &&
					 e->emission.intensity.lower != e->emission.intensity.upper) ||
					e->source.evidence.observationIssues ||
					std::any_of(a.uncertainty.begin(), a.uncertainty.end(),
								[&](const SourceUncertainty &u) { return u.source == id; }))
				{
					descriptor->uncertainty |= AttributionParametersUnknown;
					descriptor->provenance +=
						"; source=" + std::to_string(id) +
						" parameter/observation uncertainty retained; no propagated upper bound";
				}
				Need((e->source.state == HypothesisState::Resolved && !e->source.hypothesisGroup) ||
						 (s.allowConditionalSources && s.configuration &&
						  s.domain != FieldDomain::PhysicallyAuthored),
					 Code::Unavailable, "Unresolved/exclusive source needs conditional selection");
				if (e->source.state != HypothesisState::Resolved || e->source.hypothesisGroup)
				{
					auto cfg = std::find_if(
						a.lightingConfigurations.begin(), a.lightingConfigurations.end(),
						[&](const LightingConfiguration &x) { return x.id == s.configuration; });
					auto p = std::find_if(
						cfg->sources.begin(), cfg->sources.end(),
						[&](const JointParameterState &x) { return x.track == e->source.id; });
					Need(p != cfg->sources.end() && p->kind == e->kind &&
							 p->position.world.x == e->position.world.x &&
							 p->position.world.y == e->position.world.y &&
							 p->position.world.z == e->position.world.z &&
							 p->amplitudeDomain == "source-linear-physical-model/1",
						 Code::Unavailable,
						 "Selected configuration does not attest these source parameters");
				}
				auto emission = Photometric::Emission(a, *e);
				Need(bool(emission) && emission.value.calibration == s.calibration,
					 Code::Unavailable, "Unavailable/incomparable source photometry");
				if (e->source.state != HypothesisState::Resolved || e->source.hypothesisGroup)
				{
					auto cfg = std::find_if(
						a.lightingConfigurations.begin(), a.lightingConfigurations.end(),
						[&](const LightingConfiguration &x) { return x.id == s.configuration; });
					auto state = std::find_if(
						cfg->sources.begin(), cfg->sources.end(),
						[&](const JointParameterState &x) { return x.track == e->source.id; });
					Need(state != cfg->sources.end() &&
							 Length(V(state->amplitude) - V(emission.value.rgb)) <
								 1e-5 * (1 + Length(V(emission.value.rgb))) &&
							 Length(V(state->frame.right) - V(e->frame.right)) < 1e-6 &&
							 Length(V(state->frame.up) - V(e->frame.up)) < 1e-6 &&
							 Length(V(state->frame.forward) - V(e->frame.forward)) < 1e-6 &&
							 state->angular.kind == e->angular.kind &&
							 state->angular.cosInner == e->angular.cosInner &&
							 state->angular.cosOuter == e->angular.cosOuter &&
							 state->angular.twoSided == e->angular.twoSided &&
							 state->dimensions.halfWidthWorld == e->geometry.halfWidthWorld &&
							 state->dimensions.halfHeightWorld == e->geometry.halfHeightWorld &&
							 state->dimensions.halfLengthWorld == e->geometry.halfLengthWorld &&
							 state->dimensions.radiusWorld == e->geometry.radiusWorld,
						 Code::Unavailable,
						 "Configuration parameters/amplitudes differ from selected emitter");
				}
				if (s.domain == FieldDomain::PhysicallyAuthored)
					Need(bool(Photometric::RequirePhysical(a, emission.value)), Code::Unavailable,
						 "Source is not physically authored");
				if (s.quantity == ConditionalQuantity::IncomingRadiance &&
					(e->kind == EmitterKind::Point || e->kind == EmitterKind::Spot || sun))
					throw Failure(Code::Unavailable, "Delta/solar radiance retained as explicit "
													 "source; no finite radiance encoding");
				Direct d;
				d.emitter = *e;
				d.amplitude = V(emission.value.rgb);
				if (!sun)
				{
					auto support =
						std::find_if(r.emitterSupport.begin(), r.emitterSupport.end(),
									 [&](const EmitterSupport &x) { return x.source == id; });
					if (support != r.emitterSupport.end())
					{
						d.support = support->geometry;
						Need(d.support.map == r.topology->MapKey() &&
								 d.support.geometry == r.topology->GeometryKey(),
							 Code::InvalidState, "Stale emitting surface support");
						descriptor->provenance +=
							"; support=" + Disk::Hex(SourceSupport::Key(d.support));
						if (d.support.kind == SourceSupport::Kind::OriginalSurface)
							descriptor->uncertainty |= AttributionParametersUnknown;
					}
					Check(Shape::EmissionNodes(*e, d.support, r.options.sourceSampling, read,
											   d.nodes));
				}
				direct.push_back(d);
				size_t nodes = 0;
				for (const auto &x : direct)
					nodes += x.nodes.size();
				Need(nodes <= 65536 && nodes * sizeof(Shape::EmissionNode) < r.options.maxBytes / 2,
					 Code::MemoryLimit, "Field source quadrature storage budget");
			}
			else
			{
				auto env =
					std::find_if(a.environments.begin(), a.environments.end(),
								 [&](const EnvironmentLighting &x) { return x.source.id == id; });
				Need(env != a.environments.end() && s.sky &&
						 env->source.state == HypothesisState::Resolved &&
						 !env->source.hypothesisGroup &&
						 env->sunCoverage == SunCoverage::ExcludesExplicitSun,
					 Code::Unavailable, "Missing/ambiguous sky or unknown sun membership");
				if (env->source.evidence.observationIssues ||
					(env->meanLuminance.intensity.bounded &&
					 env->meanLuminance.intensity.lower != env->meanLuminance.intensity.upper) ||
					std::any_of(a.uncertainty.begin(), a.uncertainty.end(),
								[&](const SourceUncertainty &u) { return u.source == id; }))
					descriptor->uncertainty |= AttributionParametersUnknown;
				auto data = std::find_if(r.environments.begin(), r.environments.end(),
										 [&](const Environment &x) {
											 return x.source == id && x.dataset == env->radiance;
										 });
				Need(data != r.environments.end() && env->radiance &&
						 data->content != Disk::Digest{} &&
						 Angular::Validate(data->incoming) == Angular::Code::Ok &&
						 data->incoming.quantity == Angular::Quantity::IncomingRadiance &&
						 env->meanLuminance.calibration == s.calibration,
					 Code::Unavailable, "Decoded directional sky data missing");
				auto dataset =
					std::find_if(a.datasets.begin(), a.datasets.end(),
								 [&](const Dataset &x) { return x.id == env->radiance; });
				std::vector<uint8_t> encoded;
				Need(Angular::Encode(data->incoming, Angular::Storage::Float32, encoded) ==
						 Angular::Code::Ok,
					 Code::Unavailable, "Sky angular codec unsupported");
				auto content = Disk::Hash(encoded.data(), encoded.size());
				Need(dataset != a.datasets.end() && dataset->encoding == "org.openjk.angular" &&
						 dataset->encodingVersion == Angular::kVersion &&
						 data->content == content && dataset->contentHash == Disk::Hex(content),
					 Code::InvalidInput, "Sky payload does not match canonical content identity");
				skies.push_back(&*data);
			}
		}
		if (s.firstBounce)
			PassiveSources();
		Need(!direct.empty() || !skies.empty() || !bounce.empty(), Code::Unavailable,
			 "No selected supported lighting roots");
	}
	void PassiveSources()
	{
		const auto &p = r.passive;
		const auto &s = r.selection;
		Need(p.transfer && p.solution && p.solution->fitted &&
				 p.sourceScene == SceneKey(*r.asset) && p.solution->key != Disk::Digest{} &&
				 p.solution->fixedRootsKey != Disk::Digest{} &&
				 p.solution->transferKey == p.transfer->Key() &&
				 p.transfer->GeometryKey() == r.visibility->CertificationKey() &&
				 p.solution->contract.physicalTransportAssumed &&
				 p.solution->contract.calibration.id == s.calibration,
			 Code::Unavailable, "Passive source-linked transport unavailable/stale");
		descriptor->provenance += "; passive=" + Disk::Hex(p.solution->key) +
								  "; fixed-roots=" + Disk::Hex(p.solution->fixedRootsKey);
		Need(s.quantity != ConditionalQuantity::IncomingRadiance, Code::Unavailable,
			 "Passive radiance requires separately validated directional patch reference; "
			 "irradiance lift is supported");
		if (!p.transfer->Report().completePatchSet || !p.transfer->Report().allPairsRepresented)
			descriptor->uncertainty |= AttributionSupportMissing;
		const auto &patches = p.transfer->Patches();
		for (Id root : s.bounceRoots)
		{
			const Source *record = nullptr;
			for (const auto &e : r.asset->emitters)
				if (e.source.id == root)
				{
					record = &e.source;
					if ((e.position.sigmaWorld.known && e.position.sigmaWorld.value > 0) ||
						(e.position.sigmaWorld.bounded && e.position.sigmaWorld.upper > 0) ||
						(e.emission.intensity.bounded &&
						 e.emission.intensity.lower != e.emission.intensity.upper))
						descriptor->uncertainty |= AttributionParametersUnknown;
				}
			for (const auto &e : r.asset->environments)
				if (e.source.id == root)
					record = &e.source;
			Need(record && record->state == HypothesisState::Resolved && !record->hypothesisGroup,
				 Code::Unavailable, "Passive field requires an admitted nonexclusive root");
			if (record->evidence.observationIssues ||
				std::any_of(r.asset->uncertainty.begin(), r.asset->uncertainty.end(),
							[&](const SourceUncertainty &u) { return u.source == root; }))
				descriptor->uncertainty |= AttributionParametersUnknown;
			auto source = std::find_if(p.solution->fixedRoots.begin(), p.solution->fixedRoots.end(),
									   [&](const Indirect::Root &x) { return x.source == root; });
			Need(source != p.solution->fixedRoots.end() && source->calibration == s.calibration &&
					 source->incident.size() == patches.size() &&
					 source->available.size() == patches.size(),
				 Code::Unavailable, "Passive root missing");
			Need((s.domain == FieldDomain::PhysicallyAuthored &&
				  source->domain == Indirect::Root::Domain::PhysicalLux) ||
					 (s.domain != FieldDomain::PhysicallyAuthored &&
					  source->domain != Indirect::Root::Domain::CompilerRelative255),
				 Code::Unavailable, "Passive compiler channels cannot become physical radiance");
			for (size_t j = 0; j < patches.size(); ++j)
			{
				const auto &patch = patches[j];
				auto m = std::find_if(
					p.solution->materials.begin(), p.solution->materials.end(),
					[&](const Indirect::Reflectance &x) { return x.material == patch.material; });
				Need(m != p.solution->materials.end() &&
						 m->knowledge != Indirect::MaterialKnowledge::Unknown &&
						 Energy(V(m->initial)) && m->initial.x <= 1 && m->initial.y <= 1 &&
						 m->initial.z <= 1 && source->available[j] &&
						 Energy(V(source->incident[j])) && patch.receiver.footprint.owner &&
						 patch.receiver.footprint.count,
					 Code::Unavailable, "Unknown passive material/root/patch footprint");
				if (m->knowledge != Indirect::MaterialKnowledge::Known)
					descriptor->uncertainty |= AttributionMaterialUnknown;
				V rgb(source->incident[j]);
				rgb =
					V(rgb.x * m->initial.x, rgb.y * m->initial.y, rgb.z * m->initial.z) * (1 / pi);
				for (size_t k = 0; k < patch.receiver.footprint.count; ++k)
				{
					const auto &t = patch.receiver.footprint.data[k];
					V a(t.world[0]), b(t.world[1]), c(t.world[2]);
					double area = Length(Cross(b - a, c - a)) * .5;
					Need(area > 0, Code::Unavailable, "Degenerate passive patch");
					unsigned n = r.options.sourceSampling.areaDivisions;
					Need(n && n <= 32, Code::InvalidInput, "Passive quadrature divisions");
					for (unsigned x = 0; x < n; ++x)
						for (unsigned y = 0; y < n - x; ++y)
						{
							auto append = [&](double u, double v) {
								Surface z;
								z.node.positionWorld = (a + (b - a) * u + (c - a) * v).Float();
								z.node.normalWorld = Unit(Cross(b - a, c - a)).Float();
								z.node.areaWorldSquared = area / (n * n);
								z.node.finiteSurface = true;
								z.node.surface = patch.receiver.surface;
								z.radiance = rgb;
								z.root = root;
								bounce.push_back(z);
								Need(bounce.size() < 200000, Code::MemoryLimit,
									 "Passive node bound");
							};
							append((x + 1. / 3) / n, (y + 1. / 3) / n);
							if (x + y + 1 < n)
								append((x + 2. / 3) / n, (y + 2. / 3) / n);
						}
				}
			}
		}
		Need(!s.bounceRoots.empty(), Code::Unavailable, "Explicit passive roots required");
		descriptor->uncertainty |= AttributionRepresentationError;
	}
	void Add(Vec3 position, V target, V rgb, Vec3 normal, double area, bool surface,
			 Id sourceSurface)
	{
		Tick();
		V delta = target - V(position);
		double distance = Length(delta);
		Need(distance > 1e-8, Code::Unavailable, "Singular physical field source distance");
		V w = delta * (1 / distance);
		double factor = surface ? std::max(0., -Dot(V(normal), w)) * area / (distance * distance)
								: double(r.asset->units.worldUnitsPerMeter) *
									  r.asset->units.worldUnitsPerMeter / (distance * distance);
		if (factor <= 0)
			return;
		if (!Segment(position, target.Float(), sourceSurface))
			return;
		measure.push_back(Measure{w, rgb * factor});
	}
	void Measures(Vec3 p, unsigned order, bool refined)
	{
		measure.clear();
		for (auto &d : direct)
		{
			const auto &e = d.emitter;
			if (e.kind == EmitterKind::Directional)
			{
				V w = V(e.frame.forward) * -1;
				if (Sky(p, w))
					measure.push_back(Measure{w, d.amplitude});
				continue;
			}
			auto nodes = d.nodes;
			if (refined && (e.kind == EmitterKind::Rectangle || e.kind == EmitterKind::Tube))
			{
				auto sampling = r.options.sourceSampling;
				sampling.areaDivisions = std::min(32u, sampling.areaDivisions * 2);
				sampling.tubeSegments = std::min(32u, sampling.tubeSegments * 2);
				sampling.tubeSides = std::min(64u, sampling.tubeSides * 2);
				Check(Shape::EmissionNodes(e, d.support, sampling, read, nodes));
			}
			for (auto &n : nodes)
			{
				V rgb = d.amplitude;
				V wi = Unit(V(n.positionWorld) - V(p));
				if (!n.finiteSurface && d.support.mountKnown &&
					d.support.kind != SourceSupport::Kind::LegacyVacuum &&
					Dot(V(d.support.outward), V(n.positionWorld) - V(d.support.anchorWorld)) == 0 &&
					Dot(V(d.support.outward), wi) >= 0)
					continue;
				if (e.kind == EmitterKind::Spot)
				{
					double t = Clamp((-Dot(V(e.frame.forward), wi) - e.angular.cosOuter) /
										 (e.angular.cosInner - e.angular.cosOuter),
									 0, 1);
					rgb = rgb * (t * t * (3 - 2 * t));
				}
				if (!Energy(rgb))
					throw Failure(Code::InvalidInput, "Invalid physical source RGB");
				if (e.angular.twoSided && n.finiteSurface)
				{
					auto normal = V(n.normalWorld);
					if (Dot(normal, wi) > 0)
						normal = normal * -1;
					Add(p, V(n.positionWorld), rgb, normal.Float(), n.areaWorldSquared, true,
						n.surface);
				}
				else
					Add(p, V(n.positionWorld), rgb, n.normalWorld, n.areaWorldSquared,
						n.finiteSurface, n.surface);
			}
		}
		for (auto &z : bounce)
			Add(p, V(z.node.positionWorld), z.radiance, z.node.normalWorld, z.node.areaWorldSquared,
				true, z.node.surface);
		if (!skies.empty())
			for (auto node : Angular::Sphere(order))
			{
				Tick();
				V rgb;
				for (auto e : skies)
				{
					auto value = Angular::Evaluate(e->incoming, node.direction,
												   Angular::Quantity::IncomingRadiance);
					Need(value.code == Angular::Code::Ok && !value.negative, Code::Unavailable,
						 "Sky representation unavailable/negative");
					rgb = rgb + VV(value.rgb);
				}
				if (Length(rgb) == 0)
					continue;
				if (Sky(p, VV(node.direction)))
					measure.push_back(Measure{VV(node.direction), rgb * node.weight});
			}
	}
	Angular::Field Project()
	{
		Angular::Field f;
		f.present = true;
		f.degree = r.options.degree;
		f.quantity = r.selection.quantity == ConditionalQuantity::IncomingRadiance
						 ? Angular::Quantity::IncomingRadiance
						 : Angular::Quantity::DiffuseIrradiance;
		f.sh.assign((f.degree + 1) * (f.degree + 1), Angular::V{{0, 0, 0}});
		double basis[81];
		for (auto &m : measure)
		{
			Tick(f.sh.size());
			CheckBasis(m.direction, f.degree, basis);
			for (size_t k = 0; k < f.sh.size(); ++k)
				for (unsigned c = 0; c < 3; ++c)
					f.sh[k][c] +=
						basis[k] * (c == 0 ? m.energy.x : c == 1 ? m.energy.y : m.energy.z);
		}
		if (f.quantity == Angular::Quantity::DiffuseIrradiance)
		{
			auto factors = Angular::CosineFactors(f.degree);
			for (unsigned l = 0; l <= f.degree; ++l)
				for (unsigned k = l * l; k < (l + 1) * (l + 1); ++k)
					for (unsigned c = 0; c < 3; ++c)
						f.sh[k][c] *=
							factors[l] /
							(r.selection.quantity == ConditionalQuantity::IrradianceOverPi ? pi
																						   : 1);
		}
		return f;
	}
	void CheckBasis(V w, unsigned degree, double *b)
	{
		Need(Angular::BasisInto(Angular::Unit(AV(w)), degree, b, 81) == Angular::Code::Ok,
			 Code::AlgorithmError, "Field angular basis");
	}
	V ReferenceIrradiance(V n)
	{
		V rgb;
		Tick(measure.size());
		for (auto &m : measure)
			rgb = rgb + m.energy * std::max(0., Dot(n, m.direction));
		if (r.selection.quantity == ConditionalQuantity::IrradianceOverPi)
			rgb = rgb * (1 / pi);
		return rgb;
	}
	V ReferenceRadiance(Vec3 p, V w)
	{
		V rgb;
		// Exact parametric emitting surfaces, independent of angular projection nodes.
		for (auto &d : direct)
		{
			const auto &e = d.emitter;
			V origin = V(p) - V(e.position.world);
			double t = 0;
			V normal;
			if (e.kind == EmitterKind::Rectangle)
			{
				double den = Dot(w, V(e.frame.forward));
				if (std::abs(den) < 1e-12)
					continue;
				t = -Dot(origin, V(e.frame.forward)) / den;
				if (t <= 0)
					continue;
				V hit = origin + w * t;
				if (std::abs(Dot(hit, V(e.frame.right))) > e.geometry.halfWidthWorld ||
					std::abs(Dot(hit, V(e.frame.up))) > e.geometry.halfHeightWorld)
					continue;
				normal = V(e.frame.forward);
				if (!e.angular.twoSided && Dot(normal, w) >= 0)
					continue;
			}
			else if (e.kind == EmitterKind::Tube)
			{
				V axis(e.frame.right), o = origin - axis * Dot(origin, axis),
									   u = w - axis * Dot(w, axis);
				double A = Dot(u, u), B = 2 * Dot(o, u),
					   C = Dot(o, o) - e.geometry.radiusWorld * e.geometry.radiusWorld,
					   disc = B * B - 4 * A * C;
				if (A < 1e-12 || disc < 0)
					continue;
				t = (-B - std::sqrt(disc)) / (2 * A);
				if (t <= 0)
					continue;
				V hit = origin + w * t;
				if (std::abs(Dot(hit, axis)) > e.geometry.halfLengthWorld)
					continue;
				normal = Unit(hit - axis * Dot(hit, axis));
				if (Dot(normal, w) >= 0)
					continue;
			}
			else
				throw Failure(Code::Unavailable, "Nonfinite directional radiance reference");
			if (Segment(p, (V(p) + w * t).Float()))
				rgb = rgb + d.amplitude;
		}
		Need(bounce.empty(), Code::Unavailable,
			 "Passive radiance reference requires independent patch angular validation; irradiance "
			 "supported");
		if (!skies.empty())
		{
			V environment;
			for (auto e : skies)
			{
				auto v = Angular::Evaluate(e->incoming, AV(w), Angular::Quantity::IncomingRadiance);
				Need(v.code == Angular::Code::Ok && !v.negative, Code::Unavailable,
					 "Unavailable sky radiance reference");
				environment = environment + VV(v.rgb);
			}
			if (Length(environment) > 0 && Sky(p, w))
				rgb = rgb + environment;
		}
		return rgb;
	}
	void EvaluateProbe(const Probes::Probe &p)
	{
		ProbeDiagnostic diagnostic;
		diagnostic.probe = p.id;
		++out.work.probes;
		Tick();
		if ((p.issues & Probes::DynamicConditional) && !r.options.allowConditionalStatic)
		{
			diagnostic.state = ProbeState::GeometryUnknown;
			diagnostic.issues = p.issues;
			out.diagnostics.push_back(diagnostic);
			return;
		}
		try
		{
			for (const auto &source : direct)
			{
				auto &e = source.emitter;
				V delta = V(p.positionWorld) - V(e.position.world);
				bool inside = false;
				if (e.kind == EmitterKind::Rectangle)
					inside = std::abs(Dot(delta, V(e.frame.forward))) <=
								 8 * std::numeric_limits<float>::epsilon() *
									 (1 + Length(V(e.position.world))) &&
							 std::abs(Dot(delta, V(e.frame.right))) <= e.geometry.halfWidthWorld &&
							 std::abs(Dot(delta, V(e.frame.up))) <= e.geometry.halfHeightWorld;
				if (e.kind == EmitterKind::Tube)
				{
					double axial = Dot(delta, V(e.frame.right));
					inside = std::abs(axial) <= e.geometry.halfLengthWorld &&
							 Length(delta - V(e.frame.right) * axial) <= e.geometry.radiusWorld;
				}
				if (inside)
				{
					diagnostic.state = ProbeState::GeometryUnknown;
					diagnostic.issues = AttributionSupportMissing;
					out.diagnostics.push_back(diagnostic);
					return;
				}
			}
			Angular::Field field;
			if (r.selection.route == Route::ObservationAppearance)
			{
				V rgb;
				double weight = 0;
				for (auto &x : r.appearance)
				{
					Tick();
					Need(x.id && Energy(V(x.value)) && std::isfinite(x.weight) && x.weight > 0 &&
							 Finite(x.positionWorld),
						 Code::InvalidInput, "Invalid appearance evidence");
					double d = Length(V(x.positionWorld) - V(p.positionWorld));
					if (d > r.options.appearanceRadiusWorld)
						continue;
					bool visible = d < 1e-8 || Segment(p.positionWorld, x.positionWorld);
					if (visible)
					{
						double w = x.weight / (1 + d * d);
						rgb = rgb + V(x.value) * w;
						weight += w;
					}
				}
				Need(weight > 0, Code::Unavailable, "No admissible appearance observations");
				field.present = true;
				field.quantity = Angular::Quantity::DiffuseIrradiance;
				field.degree = 0;
				field.sh.push_back(AV(rgb * (std::sqrt(4 * pi) / weight)));
				diagnostic.issues |= AttributionRootsUnknown | AttributionProfileUnknown |
									 AttributionRepresentationError;
			}
			else
			{
				Measures(p.positionWorld, r.options.angularOrder, false);
				field = Project();
				Measures(p.positionWorld, r.options.referenceOrder, true);
				double se = 0, ref = 0, bias = 0, refsum = 0, lumaSE = 0;
				unsigned count = 0;
				for (auto node : Angular::Sphere(r.options.referenceOrder))
				{
					Tick();
					V truth = field.quantity == Angular::Quantity::IncomingRadiance
								  ? ReferenceRadiance(p.positionWorld, VV(node.direction))
								  : ReferenceIrradiance(VV(node.direction));
					auto value = Angular::Evaluate(field, node.direction, field.quantity);
					Need(value.code == Angular::Code::Ok, Code::AlgorithmError,
						 "Field projection nonfinite");
					V error = VV(value.rgb) - truth;
					se += Dot(error, error) * node.weight;
					ref += Dot(truth, truth) * node.weight;
					bias += (error.x + error.y + error.z) * node.weight;
					refsum += (truth.x + truth.y + truth.z) * node.weight;
					double l = .2126 * error.x + .7152 * error.y + .0722 * error.z;
					lumaSE += l * l * node.weight;
					diagnostic.maximumError = std::max(
						diagnostic.maximumError,
						std::max({std::abs(error.x), std::abs(error.y), std::abs(error.z)}));
					++count;
				}
				diagnostic.referenceKnown = true;
				diagnostic.relativeRMSE =
					std::sqrt(se / std::max(ref, r.options.absoluteErrorFloor *
													 r.options.absoluteErrorFloor));
				diagnostic.energyBias = refsum > 0 ? bias / refsum : 0;
				diagnostic.luminanceRMSE = std::sqrt(lumaSE / (4 * pi));
				if (diagnostic.relativeRMSE > r.options.maximumRelativeAngularRMSE)
				{
					diagnostic.state = ProbeState::AngularError;
					out.diagnostics.push_back(diagnostic);
					return;
				}
			}
			Probes::Value v;
			v.probe = p.id;
			v.evaluatedPositionWorld = p.positionWorld;
			v.angular = std::move(field);
			out.work.coefficients += v.angular.sh.size();
			out.values.records.push_back(std::move(v));
			diagnostic.state = ProbeState::Available;
			++out.work.available;
		}
		catch (const Error &e)
		{
			if (e.code != Code::Unavailable)
				throw;
			diagnostic.state = ProbeState::UnknownVisibility;
			diagnostic.issues |= AttributionVisibilityUnknown;
		}
		out.diagnostics.push_back(diagnostic);
	}
	void Metadata()
	{
		const auto &s = r.selection;
		out.asset = *r.asset;
		if (out.asset.assessment.present)
		{
			out.asset.assessment.payloadsValidated = false;
			out.asset.assessment.coverage.limitations.push_back(
				"P01 fields are separately prepared; source-solver assessment is not blanket field "
				"payload certification");
		}
		out.topology = r.topology;
		out.values.field = s.field;
		out.values.topology = r.topology->Key();
		descriptor->field = s.field;
		descriptor->dataset = s.dataset;
		descriptor->calibration = s.calibration;
		descriptor->configuration = s.configuration;
		descriptor->alternativeGroup = s.alternativeGroup;
		descriptor->quantity = s.quantity;
		descriptor->domain = s.domain;
		descriptor->colour = s.colour;
		descriptor->primariesKnown = s.primariesKnown;
		descriptor->linearTransferKnown = s.linearTransferKnown;
		descriptor->profile = s.profile;
		descriptor->encoding = "org.openjk.spatial-field/1";
		std::ostringstream recipe;
		recipe << std::setprecision(17) << "; SH degree=" << r.options.degree
			   << "; angular order=" << r.options.angularOrder
			   << "; reference order=" << r.options.referenceOrder
			   << "; relative angular budget=" << r.options.maximumRelativeAngularRMSE
			   << "; area divisions=" << r.options.sourceSampling.areaDivisions
			   << "; tube sides=" << r.options.sourceSampling.tubeSides
			   << "; tube segments=" << r.options.sourceSampling.tubeSegments;
		descriptor->provenance = s.provenanceText + descriptor->provenance + recipe.str();
		descriptor->topology = Disk::Hex(r.topology->Key());
		descriptor->geometry = Disk::Hex(r.topology->GeometryKey());
		descriptor->unit = s.domain == FieldDomain::SourceLinearRelative ||
								   s.domain == FieldDomain::LegacyAppearance
							   ? PhotometricUnit::Relative
							   : s.quantity == ConditionalQuantity::IncomingRadiance
									 ? PhotometricUnit::Nits
									 : PhotometricUnit::Lux;
		auto &m = descriptor->membership;
		m.version = 2;
		m.model = "source-conditioned-field/1";
		m.domain = s.route == Route::ObservationAppearance
					   ? s.appearanceDomain
					   : (s.quantity == ConditionalQuantity::IrradianceOverPi
							  ? "generated-probes/irradiance-over-pi/1"
							  : s.quantity == ConditionalQuantity::IncomingRadiance
									? "generated-probes/radiance/1"
									: "generated-probes/irradiance/1");
		m.profile = s.profile;
		m.geometry = descriptor->geometry;
		m.generation = Disk::Hex(SceneKey(*r.asset));
		m.configuration = s.configuration;
		m.alternativeGroup = s.alternativeGroup;
		m.colour = s.colour;
		m.rootsComplete = s.route != Route::ObservationAppearance;
		m.reflectionOrderKnown = s.route != Route::ObservationAppearance;
		m.directLocal = s.directLocal ? FieldMembership::Included : FieldMembership::Excluded;
		m.directSun = s.sun ? FieldMembership::Included : FieldMembership::Excluded;
		m.directSky = s.sky ? FieldMembership::Included : FieldMembership::Excluded;
		m.reflected = s.firstBounce ? FieldMembership::Included : FieldMembership::Excluded;
		m.legacyAmbient = FieldMembership::Excluded;
		m.unresolved = s.route == Route::ObservationAppearance ? FieldMembership::Included
															   : FieldMembership::Excluded;
		if (s.route == Route::ObservationAppearance)
		{
			m.model = "observation-appearance/1";
			m.directLocal = m.directSun = m.directSky = m.reflected = m.legacyAmbient =
				FieldMembership::Unknown;
			m.uncertainty |= AttributionRootsUnknown | AttributionProfileUnknown;
			m.addition = m.replacement = CompositionRule::Forbidden;
		}
		else
		{
			if (s.directLocal || s.sun || s.sky)
				m.reflectionOrders.push_back(0);
			if (s.firstBounce)
				m.reflectionOrders.push_back(1);
			Need(!m.reflectionOrders.empty(), Code::InvalidInput,
				 "Empty field component selection");
			m.minimumReflectionOrder = m.reflectionOrders.front();
			m.maximumReflectionOrder = m.reflectionOrders.back();
			m.addition = CompositionRule::Permitted;
			m.replacement = CompositionRule::Forbidden;
			auto root = [&](Id id) {
				auto e = std::find_if(r.asset->emitters.begin(), r.asset->emitters.end(),
									  [&](const PhysicalEmitter &x) { return x.source.id == id; });
				TransportRoot root;
				root.source = id;
				root.kind = e == r.asset->emitters.end() ? TransportRootKind::Sky
														 : e->kind == EmitterKind::Directional
															   ? TransportRootKind::Sun
															   : TransportRootKind::LocalEmitter;
				for (auto &x : m.roots)
					if (x.source == id)
						return;
				m.roots.push_back(root);
			};
			for (Id id : s.sources)
				root(id);
			for (Id id : s.bounceRoots)
				root(id);
		}
		if (r.options.allowConditionalStatic && r.topology->Diagnostics().conditional)
			descriptor->uncertainty |= AttributionVisibilityUnknown;
		if (s.domain != FieldDomain::PhysicallyAuthored)
			descriptor->uncertainty |= AttributionProfileUnknown | AttributionParametersUnknown;
		m.uncertainty |= descriptor->uncertainty;
		descriptor->uncertainty = m.uncertainty;
		if (m.uncertainty & AttributionParametersUnknown)
			m.addition = CompositionRule::Forbidden;
		TransportField f;
		f.source.id = s.field;
		f.source.provenance = s.provenance;
		f.source.state =
			s.domain == FieldDomain::PhysicallyAuthored &&
					!(descriptor->uncertainty & ~uint32_t(AttributionRepresentationError)) &&
					!s.alternativeGroup
				? HypothesisState::Resolved
				: HypothesisState::Candidate;
		f.source.hypothesisGroup = s.alternativeGroup;
		f.quantity = s.quantity == ConditionalQuantity::LegacyAppearance
						 ? FieldQuantity::ApproximateAppearance
						 : s.quantity == ConditionalQuantity::IncomingRadiance
							   ? FieldQuantity::Radiance
							   : FieldQuantity::Irradiance;
		f.component = f.quantity == FieldQuantity::ApproximateAppearance
						  ? TransportComponent::ApproximateAppearance
						  : f.quantity == FieldQuantity::Radiance
								? TransportComponent::UnattributedIncidentRadiance
								: s.firstBounce && !s.directLocal && !s.sun && !s.sky
									  ? TransportComponent::IndirectIrradiance
									  : TransportComponent::UnresolvedDirectIrradiance;
		f.unit = descriptor->unit;
		f.calibration = s.calibration;
		f.dataset = s.dataset;
		f.membership = m;
		f.bounds.known = true;
		f.bounds.minWorld = r.topology->Cells().front().bounds.minWorld;
		f.bounds.maxWorld = r.topology->Cells().front().bounds.maxWorld;
		for (const auto &c : r.topology->Cells())
		{
			f.bounds.minWorld.x = std::min(f.bounds.minWorld.x, c.bounds.minWorld.x);
			f.bounds.minWorld.y = std::min(f.bounds.minWorld.y, c.bounds.minWorld.y);
			f.bounds.minWorld.z = std::min(f.bounds.minWorld.z, c.bounds.minWorld.z);
			f.bounds.maxWorld.x = std::max(f.bounds.maxWorld.x, c.bounds.maxWorld.x);
			f.bounds.maxWorld.y = std::max(f.bounds.maxWorld.y, c.bounds.maxWorld.y);
			f.bounds.maxWorld.z = std::max(f.bounds.maxWorld.z, c.bounds.maxWorld.z);
		}
		out.asset.transport.push_back(f);
		out.descriptor = descriptor;
		for (Id id : {s.geometryDataset, s.topologyDataset, s.dataset})
		{
			Need(std::none_of(out.asset.datasets.begin(), out.asset.datasets.end(),
							  [&](const Dataset &d) { return d.id == id; }),
				 Code::InvalidInput, "Field dataset ID collision");
			Dataset d;
			d.id = id;
			d.encodingVersion = id == s.dataset ? kVersion : Probes::kVersion;
			d.layout = DatasetLayout::Chunked;
			d.chunkElements = id == s.dataset ? 64 : 128;
			d.purpose = id == s.dataset ? DatasetPurpose::Transport
										: id == s.geometryDataset ? DatasetPurpose::RegionMembership
																  : DatasetPurpose::Visibility;
			d.encoding = id == s.dataset ? "org.openjk.spatial-field"
										 : id == s.geometryDataset ? "org.openjk.probe-geometry"
																   : "org.openjk.probe-topology";
			d.locator = id == s.dataset
							? "prepared-field:" + std::to_string(s.field)
							: id == s.geometryDataset ? "field-geometry:" + std::to_string(s.field)
													  : "field-topology:" + std::to_string(s.field);
			out.asset.datasets.push_back(d);
		}
	}
};
} // namespace
Disk::Digest SourceIdentity(const ReconstructedLightingAsset &a)
{
	return SceneKey(a);
}
Error Build(const Request &r, const ReadContext &read, Artifact &out)
{
	out = {};
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Field build cancelled");
		Need(r.asset && r.topology && r.visibility, Code::InvalidInput,
			 "Missing field source leases");
		MetadataBinding(*r.asset, read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes)
												: r.options.maxBytes);
		std::string why;
		auto valid = Validate(*r.asset, &why);
		Need(valid, Code::InvalidInput, why.c_str());
		out.sourceScene = SourceIdentity(*r.asset);
		ReadContext effective = read;
		effective.maxBytes =
			read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes) : r.options.maxBytes;
		Engine e(r, effective, out);
		e.Select();
		e.Metadata();
		for (const auto &p : r.topology->Probes())
		{
			e.EvaluateProbe(p);
			out.work.bytes =
				sizeof(out) +
				out.values.records.size() *
					(sizeof(Probes::Value) +
					 (r.options.degree + 1) * (r.options.degree + 1) * sizeof(Angular::V)) +
				out.diagnostics.size() * sizeof(ProbeDiagnostic);
			Need(out.work.bytes <= (read.maxBytes ? std::min(read.maxBytes, r.options.maxBytes)
												  : r.options.maxBytes),
				 Code::MemoryLimit, "Field resident value budget");
		}
		std::sort(out.values.records.begin(), out.values.records.end(),
				  [](const Probes::Value &a, const Probes::Value &b) { return a.probe < b.probe; });
		out.partial = out.values.records.empty() || out.work.available < out.work.probes ||
					  r.selection.domain != FieldDomain::PhysicallyAuthored ||
					  out.descriptor->uncertainty;
		e.descriptor->geometryVolumeWorldCubed = r.topology->Diagnostics().supportedStaticVolume;
		e.descriptor->requestedProbes = out.work.probes;
		e.descriptor->availableProbes = out.work.available;
		for (const auto &cell : r.topology->Cells())
		{
			e.Tick();
			if (cell.probe == UINT32_MAX ||
				((cell.issues & Probes::DynamicConditional) && !r.options.allowConditionalStatic))
				continue;
			bool available = cell.linkCount > 0;
			for (unsigned j = 0; j < cell.linkCount; ++j)
				available &=
					ValueAt(
						out.values,
						r.topology->Probes()[r.topology->Links()[cell.firstLink + j].probe].id) !=
					nullptr;
			if (available)
				e.descriptor->lightingVolumeWorldCubed +=
					(double(cell.support.maxWorld.x) - cell.support.minWorld.x) *
					(double(cell.support.maxWorld.y) - cell.support.minWorld.y) *
					(double(cell.support.maxWorld.z) - cell.support.minWorld.z);
		}
		double maximum = 0;
		if (r.selection.route == Route::ObservationAppearance)
		{
			double se = 0, reference = 0;
			std::set<Id> ids;
			for (const auto &x : r.appearance)
			{
				Need(ids.insert(x.id).second, Code::InvalidInput,
					 "Duplicate appearance training identity");
			}
			Probes::QueryPolicy policy;
			policy.allowConditionalStatic = r.options.allowConditionalStatic;
			for (const auto &x : r.appearanceValidation)
			{
				e.Tick();
				Need(x.id && ids.insert(x.id).second && Energy(V(x.value)), Code::InvalidInput,
					 "Appearance validation overlaps training IDs");
				for (const auto &t : r.appearance)
				{
					e.Tick();
					Need(Length(V(t.positionWorld) - V(x.positionWorld)) > 1e-6, Code::InvalidInput,
						 "Appearance validation duplicates training position");
				}
				auto support = r.topology->Lookup(x.positionWorld, policy);
				Need(support.status == Status::Ok, Code::Unavailable,
					 "Independent appearance validation lacks geometry support");
				V prediction;
				for (unsigned j = 0; j < support.count; ++j)
				{
					auto value = ValueAt(out.values, support.probes[j]);
					Need(value, Code::Unavailable,
						 "Independent appearance validation missing values");
					prediction = prediction + VV(value->angular.sh[0]) *
												  (support.weights[j] / std::sqrt(4 * pi));
				}
				V error = prediction - V(x.value);
				se += Dot(error, error);
				reference += Dot(V(x.value), V(x.value));
				maximum = std::max(
					maximum, std::max({std::abs(error.x), std::abs(error.y), std::abs(error.z)}));
			}
			Need(std::sqrt(se / std::max(reference, 1e-12)) <= r.options.maximumRelativeAngularRMSE,
				 Code::Unavailable,
				 "Appearance independent-validation error exceeds declared budget");
		}
		for (const auto &d : out.diagnostics)
			if (d.state == ProbeState::Available)
				maximum = std::max(maximum, d.maximumError);
		e.descriptor->representationError.known = out.work.available > 0;
		e.descriptor->representationError.value = float(maximum);
		e.descriptor->membership.numericalError = e.descriptor->representationError;
		out.asset.transport.back().membership = e.descriptor->membership;
		if (out.values.records.empty())
			out.asset.transport.back().source.state = HypothesisState::Candidate;
		out.work.seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - e.start).count();
		Check(Encode(out, effective));
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
		return Failure(Code::MemoryLimit, "Field build allocation");
	}
}
} // namespace Fields
} // namespace Core
} // namespace Rend2LightRecon
