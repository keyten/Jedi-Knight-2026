// X13 controlled bake occupancy, not recovered historical mover state.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "joint_test_scene.h"
#include "lightrecon/uncertainty.h"
#include <cstdio>
#include <fstream>
using namespace JointTest;
namespace P = Rend2LightRecon::Core::PointRefine;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                             \
		}                                                                                          \
	} while (0)
struct Solid : RegionDomain
{
	Bounds b;
	bool Contains(Vec3 p) const
	{
		return p.x >= b.minWorld.x && p.y >= b.minWorld.y && p.z >= b.minWorld.z &&
			   p.x <= b.maxWorld.x && p.y <= b.maxWorld.y && p.z <= b.maxWorld.z;
	}
	Error Locate(Vec3 p, SpatialLocation &o, double = 1e-5) const override
	{
		o = {};
		o.space = Contains(p) ? SpaceClass::Solid : SpaceClass::Free;
		o.work = 1;
		return {};
	}
	Error ClassifyBox(const Bounds &q, SpaceClass &o, uint64_t &w) const override
	{
		w = 1;
		bool disjoint = q.maxWorld.x < b.minWorld.x || q.minWorld.x > b.maxWorld.x ||
						q.maxWorld.y < b.minWorld.y || q.minWorld.y > b.maxWorld.y ||
						q.maxWorld.z < b.minWorld.z || q.minWorld.z > b.maxWorld.z;
		o = disjoint ? SpaceClass::Free
					 : Contains(q.minWorld) && Contains(q.maxWorld) ? SpaceClass::Solid
																	: SpaceClass::Unknown;
		return {};
	}
};
std::shared_ptr<const StaticVisibility> Door(const LightmapMapIdentity &map)
{
	Scene scene;
	scene.surfaces.clear();
	scene.Quad(V(0, -.5, .4), V(0, .5, .4), V(0, .5, 1.4), V(0, -.5, 1.4));
	SurfaceCapture cap;
	cap.Begin(BuildPolicy::ForceRebuild);
	Must(cap.SetMap(map));
	for (auto s : scene.surfaces)
		Must(cap.Add(s));
	Must(cap.Freeze());
	std::shared_ptr<const StaticVisibility> out;
	Must(StaticVisibility::Build(cap.Acquire(), {}, Read(), out));
	return out;
}
std::shared_ptr<const P::PlacementProvider> Make(const P::SceneInput &i)
{
	std::shared_ptr<const P::PlacementProvider> p;
	Must(P::MakePlacement(i, Read(), p));
	return p;
}
P::PlacementResult At(const P::SceneInput &i, Vec3 p = Vec3(0, 0, 1), double radius = .01)
{
	P::PlacementResult out;
	Must(Make(i)->Check(p, p, radius, Read(), out));
	return out;
}
class Oracle : public J::Validation
{
	Rig &rig;

  public:
	explicit Oracle(Rig &r) : rig(r)
	{
	}
	Disk::Digest Key() const override
	{
		return JointFixture::Key("X13 independent validation");
	}
	Error Evaluate(const J::Model &m, const ReadContext &r, J::Score &s) const override
	{
		return J::EvaluateFrozen(rig.request, m, rig.validation, rig.validation->data->size(),
								 J::Role::Validation, Key(), r, s);
	}
};
struct Cancel : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
int main(int argc, char **argv)
{
	uint64_t conditionalForward = 0, conditionalQueries = 0;
	double heldout = -1;
	try
	{
		Rig rig;
		auto input = rig.request.placement;
		DynamicBoundary raw;
		raw.id = 717;
		raw.model = 7;
		raw.bounds = Box(V(100, 100, 100), V(101, 101, 101));
		input.dynamic = {raw};
		auto before = At(input);
		CHECK(before.state == P::Constraint::Dynamic);
		CHECK(P::Classify(before) == P::Feasibility::Unknown);
		input.occupancyPolicy = P::OccupancyPolicy::ConditionalBake;
		input.occupancyProvenance =
			"Owned counterfactual static bake scene; unknown runtime motion retained";
		auto after = At(input);
		CHECK(after.state == P::Constraint::Conditional && P::Admissible(after));
		CHECK(after.occupancy.size() == 1 && after.occupancy[0].boundary == 717);
		CHECK(after.dynamicExtentUnknown);
		CHECK(!after.regionKnown);
		input.dynamic[0].sweptExtentKnown = true;
		CHECK(At(input).state ==
			  P::Constraint::Conditional); // runtime envelope is not bake participation
		input.dynamic[0].sweptExtentKnown = false;
		auto surface = At(input, Vec3(0, 0, 0));
		CHECK(P::Classify(surface) == P::Feasibility::Collision); // real floor still wins
		auto current = input;
		current.dynamic[0].bounds = Box(V(-.1, -.1, .8), V(.1, .1, 1.2));
		CHECK(P::Classify(At(current)) ==
			  P::Feasibility::Conditional); // bounds are not filled solids; explicit local bake
											// assumption
		auto e = P::CapturedOccupancy(current)[0];
		auto solid = std::make_shared<Solid>();
		solid->b = current.dynamic[0].bounds;
		P::BakeState closed;
		closed.id = 901;
		closed.alternativeGroup = 900;
		closed.participation = P::BakeParticipation::Included;
		closed.verified = true;
		closed.supportComplete = true;
		closed.support = solid->b;
		closed.domain = solid;
		closed.geometry = Door(rig.scene.map);
		closed.generation = Key("owned closed bake scene");
		closed.provenance = "Independent controlled closed door geometry, not a stock claim";
		P::BakeState open;
		open.id = 902;
		open.alternativeGroup = 900;
		open.participation = P::BakeParticipation::Excluded;
		open.verified = true;
		open.generation = Key("owned open bake scene");
		open.provenance = "Independent controlled absent/open model state";
		e.states = {closed, open};
		e.selectedState = 901;
		current.occupancy = {e};
		CHECK(At(current).state == P::Constraint::Solid);
		current.occupancy[0].selectedState = 902;
		auto openProvider = Make(current);
		CHECK(At(current).state == P::Constraint::Free);
		RayQuery ray;
		ray.startWorld = Vec3(-1, 0, 1);
		ray.endWorld = Vec3(1, 0, 1);
		ray.originKnownFree = true;
		ray.bias.absoluteWorld = ray.bias.relative = 0;
		RayResult result;
		Must(openProvider->Query(ray, Read(), result));
		CHECK(result.state == VisibilityState::Visible);
		current.occupancy[0].selectedState = 901;
		Must(Make(current)->Query(ray, Read(), result));
		CHECK(result.state == VisibilityState::Blocked &&
			  result.reliability == RayReliability::Geometric);
		current.occupancy[0].selectedState = 902;
		current.occupancy[0].states[1].verified = false;
		CHECK(At(current).state == P::Constraint::Conditional);
		Must(Make(current)->Query(ray, Read(), result));
		CHECK(result.state == VisibilityState::Visible && (result.issues & PolicyAssumption) &&
			  result.reliability == RayReliability::Assumed);
		current.occupancy[0].selectedState = 0;
		Must(Make(current)->Query(ray, Read(), result));
		CHECK(result.state == VisibilityState::Unknown); // conditional placement != ray certificate
		auto unbounded = input;
		unbounded.dynamic[0].bounds = {};
		CHECK(P::Classify(At(unbounded)) == P::Feasibility::Unknown);
		auto local = P::CapturedOccupancy(unbounded)[0];
		local.conditionalClearRegion = Box(V(-.2, -.2, .5), V(.2, .2, 1.3));
		local.provenance = "Explicit local source-support exclusion assumption only";
		unbounded.occupancy = {local};
		CHECK(At(unbounded).state == P::Constraint::Conditional);
		Must(Make(unbounded)->Query(ray, Read(), result));
		CHECK(result.state == VisibilityState::Unknown);
		std::shared_ptr<const P::PlacementProvider> p;
		auto stale = current;
		stale.occupancy[0].geometryGeneration = Key("changed geometry");
		CHECK(P::MakePlacement(stale, Read(), p).code == Code::InvalidInput);
		auto changed = current;
		changed.occupancy[0].states[1].generation = Key("new state evidence");
		CHECK(Make(changed)->PolicyKey() != Make(current)->PolicyKey());
		auto permuted = current;
		std::reverse(permuted.occupancy[0].states.begin(), permuted.occupancy[0].states.end());
		CHECK(Make(permuted)->PolicyKey() == Make(current)->PolicyKey());
		auto corrupt = current;
		corrupt.occupancy[0].selectedState = 999;
		CHECK(P::MakePlacement(corrupt, Read(), p).code == Code::InvalidInput);
		auto noProvenance = input;
		noProvenance.occupancyProvenance.clear();
		CHECK(P::MakePlacement(noProvenance, Read(), p).code == Code::InvalidInput);
		auto limits = input;
		limits.maxWorkPerCheck = 1;
		auto provider = Make(limits);
		P::PlacementResult value;
		CHECK(provider->Check(Vec3(0, 0, 1), Vec3(0, 0, 1), .01, Read(), value).code ==
			  Code::WorkLimit);
		Cancel cancel;
		auto read = Read();
		read.cancellation = &cancel;
		CHECK(P::MakePlacement(input, read, p).code == Code::Cancelled);
		CHECK(Make(input)->Check(Vec3(0, 0, 1), Vec3(0, 0, 1), .01, read, value).code ==
			  Code::Cancelled);
		read = Read();
		read.maxBytes = 1;
		CHECK(P::MakePlacement(input, read, p).code == Code::MemoryLimit);
		// Actual supported historical operator: conditional occupancy is not a physics certificate.
		Rig fit;
		Bake::Recipe recipe;
		recipe.fast = false;
		Historical(fit, recipe);
		auto source = CompilerPoint(10, V(0, 0, 60));
		fit.request.candidates = {source};
		fit.Observations(fit.Truth({Answer(source, V(35, 12, 21))}), true);
		DynamicBoundary far = raw;
		far.bounds = Box(V(1000, 1000, 1000), V(1001, 1001, 1001));
		fit.request.placement.dynamic = {far};
		fit.request.placement.occupancyPolicy = P::OccupancyPolicy::ConditionalBake;
		fit.request.placement.occupancyProvenance = input.occupancyProvenance;
		Oracle oracle(fit);
		J::Result fitted;
		Must(J::Solve(fit.request, Read(), fitted, &oracle));
		CHECK(fitted.preferred != SIZE_MAX);
		conditionalForward = fitted.work.forwardCalls;
		conditionalQueries = fitted.work.conditionalPlacements;
		CHECK(conditionalForward > 0 && conditionalQueries > 0);
		if (fitted.preferred != SIZE_MAX)
		{
			const auto &m = fitted.alternatives[fitted.preferred];
			CHECK(m.occupancyConditional && !m.physicalExistenceCertified && m.strictReevaluated);
			CHECK(!m.placements.empty() && m.placements[0].occupancy.size() == 1);
			CHECK(m.sources[0].value.geometry.source.state == HypothesisState::Candidate);
			J::Score score;
			std::vector<J::Prediction> predictions;
			Must(J::EvaluateFrozen(fit.request, m, fit.held, fit.held->data->size(),
								   J::Role::Heldout, Key("X13 final heldout"), Read(), score,
								   &predictions));
			heldout = score.intervalMse;
			CHECK(score.available && score.intervalMse < score.blackMse);
			for (auto v : predictions)
				CHECK(v.issues & I::MovingPath);
		}
		fit.request.candidates[0].attachedModel = 7;
		J::Result attached;
		Must(J::Solve(fit.request, Read(), attached, &oracle));
		CHECK(attached.preferred == SIZE_MAX);
	}
	catch (const std::exception &e)
	{
		++failures;
		std::printf("EXCEPTION %s\n", e.what());
	}
	if (argc > 1)
	{
		std::ofstream f(argv[1]);
		f << "{\"schema\":\"org.openjk.occupancy-tests.v1\",\"checks\":" << checks
		  << ",\"failures\":" << failures
		  << ",\"distant_unknown_conditional\":true,\"known_wall_rejected\":true,\"conditional_"
			 "forward_calls\":"
		  << conditionalForward << ",\"conditional_placement_queries\":" << conditionalQueries
		  << ",\"heldout_mse\":" << heldout << ",\"original_stock_state_recovered\":false}";
	}
	std::printf("X13: %d checks %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
