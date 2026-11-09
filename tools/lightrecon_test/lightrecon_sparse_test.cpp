// Owned ASR21 witnesses. Answers generate observations, never candidate geometry/evidence.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "aesthetic_fit.h"
#include "joint_test_scene.h"
#include "lightrecon/shape_selection.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace A = Rend2LightRecon::Aesthetic;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
#include "aesthetic_test_scene.h"

A::SparsePolicy Policy(const AestheticRig &rig)
{
	A::SparsePolicy p;
	p.maxSources = 4;
	p.maxModels = 128;
	p.trainingPenaltyPerFixture = 2e-5;
	p.provenance = "Owned predeclared sparse relative-loss policy v1";
	p.evidence.resize(rig.request.fixtures.size());
	for (auto &e : p.evidence)
		e.provenance = "Owned supplied hypothesis, no independent visible fixture claim";
	return p;
}
A::Experiment Select(const AestheticRig &rig, const A::SparsePolicy &p)
{
	A::Experiment e;
	Must(A::SelectSparse(rig.request, p, rig.folds[0], rig.folds[1], Read(), e));
	return e;
}
std::vector<Id> IDs(const A::Experiment &e)
{
	std::vector<Id> ids;
	if (e.Selected() != SIZE_MAX)
		for (const auto &s : e.Fits()[e.Selected()].sources)
			ids.push_back(s.fixture);
	return ids;
}
bool Action(const A::Experiment &e, Id id, const char *action)
{
	for (const auto &d : e.Sparse().decisions)
		if (d.fixture == id && d.action == action)
			return true;
	return false;
}
void Verify(std::ostream &out, const char *name, const AestheticRig &rig, const A::Experiment &e,
			double ratio = .05)
{
	std::vector<A::Score> held;
	A::Score baseline;
	Must(A::AuditHeldout(e, rig.folds[2], Read(), held, baseline));
	CHECK(e.Selected() != SIZE_MAX);
	CHECK(baseline.available);
	if (e.Selected() == SIZE_MAX)
		return;
	const auto &fit = e.Fits()[e.Selected()];
	CHECK(!fit.sources.empty());
	CHECK(held[e.Selected()].available);
	CHECK(held[e.Selected()].robust < ratio * baseline.robust);
	for (const auto &s : fit.sources)
	{
		CHECK(Positive(s.relativePower));
		CHECK(s.relativePower.x <= 12 && s.relativePower.y <= 12 && s.relativePower.z <= 12);
		CHECK(s.emitter.geometry.source.state == HypothesisState::Candidate);
	}
	out << "{\"name\":\"" << name << "\",\"inputs\":" << e.Sparse().inputCandidates
		<< ",\"unique\":" << e.Sparse().uniqueCandidates << ",\"duplicates_removed\":"
		<< e.Sparse().duplicateInputs << ",\"models\":" << e.Fits().size()
		<< ",\"work\":" << e.Work() << ",\"heldout_baseline_robust\":" << baseline.robust
		<< ",\"heldout_selected_robust\":" << held[e.Selected()].robust << ",\"selected_ids\":[";
	auto ids = IDs(e);
	for (size_t j = 0; j < ids.size(); ++j)
		out << (j ? "," : "") << ids[j];
	out << "],\"validation_equivalent\":" << e.ValidationAlternatives().size() << ",\"decisions\":[";
	for (size_t j = 0; j < e.Sparse().decisions.size(); ++j)
	{
		const auto &d = e.Sparse().decisions[j];
		out << (j ? "," : "") << "{\"fixture\":" << d.fixture << ",\"action\":\"" << d.action
			<< "\",\"rationale\":\"" << d.rationale << "\"}";
	}
	out << "]}";
}
void PinGeometry(AestheticRig &rig)
{
	Rebuild(rig.scene);
	SurfaceCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	Must(capture.SetMap(rig.scene.map));
	for (auto s : rig.scene.surfaces)
		Must(capture.Add(s));
	Must(capture.Freeze());
	Must(StaticVisibility::Build(capture.Acquire(), {}, Read(), rig.scene.visibility, rig.scene.domain));
	rig.request.scene.originalSurfaces = capture.Acquire();
	rig.request.scene.geometry = rig.scene.visibility;
	rig.request.scene.map = rig.scene.map;
	rig.request.geometry.visibility = rig.scene.visibility;
	rig.request.geometry.geometryKey = rig.scene.visibility->CertificationKey();
}
void Panel(AestheticRig &rig, Id id, unsigned ordinal)
{
	ShapeSelection::Request q;
	q.scene = rig.request.scene;
	q.fixture = id;
	q.emittingSurfaces = {ordinal};
	q.fullSurfaceEmissionAttested = true;
	q.emissionProvenance = "Owned uniform emitting geometry assumption; candidate existence separate";
	ShapeSelection::Result ranked;
	Must(ShapeSelection::Rank(q, Read(), ranked));
	for (const auto &v : ranked.ranked)
		if (v.kind == EmitterKind::Rectangle && v.status == ShapeSelection::Status::Supported)
		{
			A::Fixture f;
			f.variants = {v.emitter};
			f.upperPower = Vec3(12, 12, 12);
			f.provenance = "Owned fixed panel geometry";
			rig.request.fixtures.push_back(f);
			return;
		}
	throw std::runtime_error("Panel admission unavailable");
}
void ManySurfacesData(AestheticRig &rig)
{
	for (unsigned fold = 0; fold < 3; ++fold)
	{
		auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
		for (unsigned face = 0; face < 3; ++face)
			for (unsigned i = 0; i < 8; ++i)
			{
				Bake::Receiver r;
				r.sample = 1 + fold * 1000 + face * 100 + i;
				r.surface = face == 0 ? 1 : face + 2;
				r.material = 2;
				double y = -1.8 + .45 * i + .08 * fold;
				r.world = face == 0 ? Vec3(float(-1 + .3 * i), float(y), .01f) :
					Vec3(face == 1 ? -2.49f : 2.49f, float(y), float(.35 + .08 * i));
				r.geometricNormal = r.shadingNormal = face == 0 ? Vec3(0, 0, 1) :
					Vec3(face == 1 ? 1.f : -1.f, 0, 0);
				r.mappedPlanar = r.compilerPlacementCertified = true;
				receivers->push_back(r);
			}
		std::shared_ptr<const S::Block> block;
		Must(S::Prepare(rig.request.model, {rig.request.fixtures[0].variants[0]}, Lease(receivers),
			rig.request.geometry, rig.request.sampling, Read(), block));
		std::vector<S::Prediction> prediction;
		S::Work work;
		Must(block->Evaluate({Vec3(3, 1, .5)}, Read(), prediction, work));
		for (size_t i = 0; i < prediction.size(); ++i)
		{
			CHECK(prediction[i].available);
			S::Observation row;
			row.receiver = (*receivers)[i];
			row.id = row.receiver.sample;
			row.spatialBlock = 1 + fold * 100 + i / 4;
			row.role = S::Role(fold);
			row.comparable = true;
			for (unsigned c = 0; c < 3; ++c)
				row.channel[c].lower = row.channel[c].upper = V(prediction[i].value)[c] + .02;
			rig.folds[fold].push_back(row);
		}
	}
}
int main(int argc, char **argv)
{
	try
	{
		std::ostringstream out;
		out << std::setprecision(12) << "{\"schema\":\"org.openjk.sparse-owned.v1\",\"cases\":[";
		AestheticRig multiple;
		multiple.Fixture(10, V(-1.3, -.6, 1.1));
		multiple.Fixture(20, V(1.3, .6, 1.1));
		multiple.Data({Vec3(3, .2, .1), Vec3(.1, .2, 4)});
		for (unsigned i = 0; i < 6; ++i)
			multiple.Fixture(30 + i, V(-1.5 + .6 * i, -1.4, 1.4));
		// Distinct near-equivalent ID must survive the terminal replacement catalog.
		multiple.Fixture(40, V(-1.3001, -.6, 1.1));
		auto p = Policy(multiple);
		p.evidence[0].independentlyVisible = p.evidence[1].independentlyVisible = true;
		auto fitted = Select(multiple, p);
		Verify(out, "multiple-lights-one-surface", multiple, fitted);
		CHECK(IDs(fitted) == std::vector<Id>({10, 20}));
		CHECK(fitted.ValidationAlternatives().size() == 2);
		CHECK(Action(fitted, 40, "alternative-retained"));
		// Candidate permutation does not alter catalog order, powers or arithmetic count.
		auto reordered = multiple;
		std::reverse(reordered.request.fixtures.begin(), reordered.request.fixtures.end());
		auto reversePolicy = p;
		std::reverse(reversePolicy.evidence.begin(), reversePolicy.evidence.end());
		auto repeated = Select(reordered, reversePolicy);
		CHECK(IDs(repeated) == IDs(fitted));
		CHECK(repeated.Work() == fitted.Work());
		CHECK(repeated.Fits().size() == fitted.Fits().size());
		CHECK(repeated.Fits()[repeated.Selected()].training.robust == fitted.Fits()[fitted.Selected()].training.robust);
		auto perturbed = multiple;
		for (auto &fold : perturbed.folds)
			for (auto &row : fold)
				for (auto &v : row.channel)
				{
					v.lower += (row.id % 2 ? 1 : -1) * .0001;
					v.upper = v.lower;
				}
		CHECK(IDs(Select(perturbed, p)) == IDs(fitted));

		// Duplicate provider records cannot allocate two amplitudes to one persistent ID.
		auto duplicate = multiple;
		duplicate.request.fixtures.push_back(duplicate.request.fixtures[0]);
		auto dupPolicy = p;
		dupPolicy.evidence.push_back(dupPolicy.evidence[0]);
		auto dedup = Select(duplicate, dupPolicy);
		CHECK(dedup.Sparse().duplicateInputs == 1);
		CHECK(Action(dedup, 10, "duplicate-input-removed"));
		CHECK(IDs(dedup) == IDs(fitted));
		CHECK(dedup.Work() == fitted.Work());
		out << ',';
		Verify(out, "duplicate-geometry-derived-source-id", duplicate, dedup);

		AestheticRig equivalent;
		equivalent.Fixture(1, V(-1, 0, 1.2));
		equivalent.Fixture(2, V(-1, 0, 1.2));
		equivalent.Data({Vec3(3, 1, .2)});
		auto eqp = Policy(equivalent);
		eqp.evidence[1].independentlyVisible = true;
		eqp.evidence[1].provenance = "Owned separately captured visible fixture";
		auto eq = Select(equivalent, eqp);
		CHECK(IDs(eq) == std::vector<Id>({2}));
		CHECK(eq.ValidationAlternatives().size() == 2);
		CHECK(Action(eq, 1, "alternative-retained"));
		out << ',';
		Verify(out, "visible-preference-equivalent-hidden", equivalent, eq);

		auto hidden = equivalent;
		hidden.request.fixtures[1].variants[0].geometry.position.world = Vec3(1.5, 0, 1.2);
		auto stronger = Select(hidden, eqp);
		CHECK(IDs(stronger) == std::vector<Id>({1}));
		out << ',';
		Verify(out, "hidden-wins-stronger-validation", hidden, stronger);

		AestheticRig many;
		many.scene.Quad(V(-2.5, -3, 0), V(-2.5, 3, 0), V(-2.5, 3, 2), V(-2.5, -3, 2));
		many.scene.Quad(V(2.5, 3, 0), V(2.5, -3, 0), V(2.5, -3, 2), V(2.5, 3, 2));
		PinGeometry(many);
		many.Fixture(10, V(-.5, .3, 1.1));
		many.Fixture(20, V(1, -.7, 1.3));
		many.Fixture(30, V(-1.3, -1, 1.4));
		ManySurfacesData(many);
		auto one = Select(many, Policy(many));
		CHECK(IDs(one) == std::vector<Id>({10}));
		CHECK(one.Fits()[one.Selected()].validation.normalBins == 3);
		out << ',';
		Verify(out, "one-light-three-surfaces", many, one);

		// Adjacent mounted panels and a larger hypothetical uniform ceiling emitter.
		AestheticRig panels;
		for (auto box : {std::vector<double>{-1.6, -.15, -.5, .5, 1.2},
						 std::vector<double>{.15, 1.6, -.5, .5, 1.2},
						 std::vector<double>{-2, 2, -1, 1, 1.3}})
		{
			panels.scene.Quad(V(box[0], box[2], box[4]), V(box[0], box[3], box[4]),
				V(box[1], box[3], box[4]), V(box[1], box[2], box[4]), MaterialGlow);
			panels.scene.surfaces.back().materialFacts.cull = SurfaceCull::Back;
		}
		PinGeometry(panels);
		Panel(panels, 10, 2);
		Panel(panels, 20, 3);
		Panel(panels, 30, 4);
		panels.request.sampling.areaDivisions = 8;
		panels.Data({Vec3(3, .2, .1), Vec3(.1, .2, 3)});
		auto pp = Policy(panels);
		pp.evidence[0].independentlyVisible = pp.evidence[1].independentlyVisible = true;
		auto pair = Select(panels, pp);
		CHECK(IDs(pair) == std::vector<Id>({10, 20}));
		CHECK(Action(pair, 30, "excluded"));
		out << ',';
		Verify(out, "adjacent-panels-versus-fictitious-large-emitter", panels, pair);

		// A central proxy can enter first, then become redundant after two separated roots.
		AestheticRig prune;
		prune.Fixture(2, V(-1, 0, 1.1));
		prune.Fixture(3, V(1, 0, 1.1));
		prune.Data({Vec3(2, 1, .5), Vec3(2, 1, .5)});
		prune.Fixture(1, V(0, 0, 1.5));
		auto pruned = Select(prune, Policy(prune));
		CHECK(IDs(pruned) == std::vector<Id>({2, 3}));
		CHECK(Action(pruned, 1, "prune-refit"));
		out << ',';
		Verify(out, "redundant-central-proxy-pruned", prune, pruned);

		// Training-only bounce/residual ghost: perfect training fit is insufficient.
		auto ghost = equivalent;
		for (unsigned fold : {1u, 2u})
			for (auto &row : ghost.folds[fold])
				for (auto &v : row.channel)
					v.lower = v.upper = .02;
		auto negative = Select(ghost, eqp);
		CHECK(negative.Selected() == SIZE_MAX);
		CHECK(negative.ValidationAlternatives().empty());
		CHECK(negative.Fits()[0].training.robust < negative.Baseline().training.robust);
		CHECK(Action(negative, 1, "excluded"));
		std::vector<A::Score> audit;
		A::Score base;
		Must(A::AuditHeldout(negative, ghost.folds[2], Read(), audit, base));
		CHECK(base.available);
		out << ",{\"name\":\"training-only-GI-ghost\",\"selected_count\":0,\"models\":"
			<< negative.Fits().size() << ",\"training_baseline\":" << negative.Baseline().training.robust
			<< ",\"training_ghost\":" << negative.Fits()[0].training.robust << '}';

		AestheticRig dark;
		for (unsigned i = 0; i < 48; ++i)
			dark.Fixture(100 + i, V(-1.7 + (i % 8) * .45, -1.5 + (i / 8) * .5, 1.2));
		dark.Data({Vec3()}, Vec3(.02, .02, .02));
		auto dp = Policy(dark);
		auto noLights = Select(dark, dp);
		CHECK(noLights.Selected() == SIZE_MAX);
		CHECK(noLights.Sparse().uniqueCandidates == 48);
		CHECK(noLights.Fits().size() == 48);
		out << ",{\"name\":\"constant-negative-48-candidates\",\"selected_count\":0,\"models\":48}";
		auto empty = dark;
		empty.request.fixtures.clear();
		auto emptyResult = Select(empty, Policy(empty));
		CHECK(emptyResult.Selected() == SIZE_MAX);
		CHECK(emptyResult.Fits().empty());
		CHECK(emptyResult.Sparse().uniqueCandidates == 0);

		// Fail closed on conflicting duplicates, missing evidence, leakage and exhausted budgets.
		A::Experiment rejected;
		auto bad = duplicate;
		bad.request.fixtures.back().upperPower.x = 11;
		CHECK(A::SelectSparse(bad.request, dupPolicy, bad.folds[0], bad.folds[1], Read(), rejected).code == Code::InvalidInput);
		CHECK(!rejected.Sparse().enabled);
		auto bp = p;
		bp.evidence[0].provenance.clear();
		CHECK(!A::SelectSparse(multiple.request, bp, multiple.folds[0], multiple.folds[1], Read(), rejected));
		bp = p;
		bp.maxModels = 1;
		CHECK(A::SelectSparse(multiple.request, bp, multiple.folds[0], multiple.folds[1], Read(), rejected).code == Code::WorkLimit);
		bad = multiple;
		bad.request.options.maxWork = 1;
		CHECK(A::SelectSparse(bad.request, p, bad.folds[0], bad.folds[1], Read(), rejected).code == Code::WorkLimit);
		bad = multiple;
		bad.request.options.maxBytes = 1024;
		CHECK(A::SelectSparse(bad.request, p, bad.folds[0], bad.folds[1], Read(), rejected).code == Code::MemoryLimit);
		auto leaked = multiple.folds[1];
		leaked[0].spatialBlock = multiple.folds[0][0].spatialBlock;
		CHECK(A::SelectSparse(multiple.request, p, multiple.folds[0], leaked, Read(), rejected).code == Code::InvalidInput);
		bad = equivalent;
		bad.scene = Scene(true, false, false, false, true);
		bad.request.scene.geometry = bad.scene.visibility;
		bad.request.scene.map = bad.scene.map;
		bad.request.geometry.visibility = bad.scene.visibility;
		bad.request.geometry.geometryKey = bad.scene.visibility->CertificationKey();
		for (auto &f : bad.request.fixtures)
		{
			f.variants[0].support.map = bad.scene.map.sha256;
			f.variants[0].support.geometry = bad.request.geometry.geometryKey;
		}
		auto unknown = Select(bad, Policy(bad));
		CHECK(unknown.Selected() == SIZE_MAX);
		CHECK(!unknown.Fits()[0].available);
		CHECK(Action(unknown, 1, "unavailable"));
		struct Cancel : Cancellation { bool IsCancelled() const noexcept override { return true; } } cancel;
		auto read = Read();
		read.cancellation = &cancel;
		CHECK(A::SelectSparse(multiple.request, p, multiple.folds[0], multiple.folds[1], read, rejected).code == Code::Cancelled);
		// A changed audit cannot alter the already chosen candidate count or RGB.
		auto frozenIDs = IDs(fitted);
		auto before = fitted.Fits()[fitted.Selected()].sources[0].relativePower;
		for (auto &row : multiple.folds[2])
			for (auto &v : row.channel)
				v.lower = v.upper = 1000;
		Must(A::AuditHeldout(fitted, multiple.folds[2], Read(), audit, base));
		CHECK(IDs(fitted) == frozenIDs);
		CHECK(fitted.Fits()[fitted.Selected()].sources[0].relativePower.x == before.x);
		out << "],\"checks\":" << checks << ",\"failures\":" << failures
			<< ",\"historical_source_truth\":\"Unavailable\",\"absolute_scale_known\":false}";
		if (argc == 2)
		{
			std::ofstream file(argv[1]);
			file << out.str() << '\n';
			if (!file)
				throw std::runtime_error("Sparse report write failed");
		}
		std::printf("ASR21: %d checks, %d failures\n", checks, failures);
		return failures ? 1 : 0;
	}
	catch (const std::exception &e)
	{
		std::fprintf(stderr, "ASR21 exception: %s\n", e.what());
		return 2;
	}
}
