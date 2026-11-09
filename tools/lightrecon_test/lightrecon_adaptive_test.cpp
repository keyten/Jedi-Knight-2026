// Owned geometric and code-domain controls. No authored source answer in search requests.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adaptive_hidden.h"
#include "lightrecon/bake_math.h"
#include <cstdio>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace H = Hidden;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d %s\n", __LINE__, #x); } } while (0)
Bounds Box(V a, V b) { Bounds r; r.known = true; r.minWorld = a.Float(); r.maxWorld = b.Float(); return r; }
class Domain : public PointClassifier
{
  public:
	bool unknown = false;
	Error Classify(Vec3 p, double, PointDomain &out) const override
	{
		out = unknown ? PointDomain::Unknown : std::abs(p.x) < 128 && std::abs(p.y) < 128 && p.z > 0 && p.z < 256 ? PointDomain::Free : PointDomain::Solid;
		return {};
	}
};
class Cancel : public Cancellation
{
	bool IsCancelled() const noexcept override { return true; }
};
int main()
{
	ReadContext read; read.maxBytes = 128ull << 20;
	H::Request r; r.map.path = "maps/owned-asr16.bsp"; r.map.sha256[0] = 116; r.map.bytes = 1;
	r.analysisBounds = Box(V(-127, -127, -1), V(127, 127, 255));
	H::SearchArea a; a.id = 1; a.positions = Box(V(-120, -120, 8), V(120, 120, 248));
	a.receivers = Box(V(-128, -128, -1), V(128, 128, 257)); a.reason = "Owned unanchored room; bounds from geometry"; r.areas = {a};
	auto domain = std::make_shared<Domain>(); r.domain = domain; r.domainKey[0] = 1;
	SurfaceCapture capture; capture.Begin(BuildPolicy::ForceRebuild); CHECK(capture.SetMap(r.map));
	unsigned ordinal = 0;
	auto quad = [&](V a0, V b, V c, V d) {
		BakedSurface s; s.ordinal = ordinal++; s.kind = BakedSurfaceKind::Face; s.motion = SurfaceMotion::StaticWorld;
		s.material = "owned/white"; s.materialFacts.available = true; s.materialFacts.cull = SurfaceCull::None;
		for (V p : {a0, b, c, d}) { BakedVertex v; v.position = p.Float(); s.vertices.push_back(v); }
		s.indices = {0, 1, 2, 0, 2, 3}; CHECK(capture.Add(s));
	};
	quad(V(-128,-128,0), V(128,-128,0), V(128,128,0), V(-128,128,0));
	quad(V(-128,-128,256), V(128,-128,256), V(128,128,256), V(-128,128,256));
	quad(V(-128,-128,0), V(-128,128,0), V(-128,128,256), V(-128,-128,256));
	quad(V(128,-128,0), V(128,128,0), V(128,128,256), V(128,-128,256));
	quad(V(-128,-128,0), V(128,-128,0), V(128,-128,256), V(-128,-128,256));
	quad(V(-128,128,0), V(128,128,0), V(128,128,256), V(-128,128,256));
	CHECK(capture.Freeze());
	std::shared_ptr<const StaticVisibility> rays;
	CHECK(StaticVisibility::Build(capture.Acquire(), VisibilityBuildOptions(), read, rays, domain));
	r.geometry.visibility = rays; r.geometry.geometryKey = rays->CertificationKey();
	auto rows = std::make_shared<std::vector<H::Sample>>();
	for (int y = -112; y <= 112; y += 32) for (int x = -112; x <= 112; x += 32)
	{
		H::Sample s; s.receiver.sample = rows->size()+1; s.receiver.surface = 1; s.receiver.material = 1;
		s.receiver.world = Vec3(float(x), float(y), 0); s.receiver.geometricNormal = s.receiver.shadingNormal = Vec3(0,0,1);
		s.receiver.mappedPlanar = s.receiver.compilerPlacementCertified = true; s.areaWorldSquared = 1024;
		s.eligible = s.encodingAttested = true;
		double distance = std::sqrt((x-24.)*(x-24.) + (y+16.)*(y+16.) + 160.*160.);
		for (int c = 0; c < 3; ++c) s.rgb[c] = uint8_t(8 + 100*std::pow(160./distance, 3));
		if (y == -112) s.rgb[0] = s.rgb[1] = s.rgb[2] = 0; // dark counterevidence retained
		rows->push_back(s);
	}
	r.training = VectorChunk<H::Sample>(rows, 0, rows->size());
	r.options.samplesPerArea = 64; r.options.maxNodesPerArea = 64; r.options.refinementRounds = 2;
	r.options.allowAssumedProfiles = true; r.options.geometrySpotSeeds = true;
	Bake::Recipe recipe; recipe.fast = false; r.profiles = {Bake::NetRadiantProfile(recipe)};
	H::Result strictBefore, adaptive, repeated, strictAfter; AdaptiveHidden::Audit audit, repeatAudit;
	CHECK(H::Generate(r, read, strictBefore));
	CHECK(AdaptiveHidden::Generate(r, AdaptiveHidden::Options(), read, adaptive, audit));
	CHECK(!adaptive.candidates.empty()); CHECK(!adaptive.report.complete); CHECK(audit.batches.size() > 1);
	CHECK(adaptive.report.rays <= r.options.maxRays); CHECK(adaptive.report.forwardCalls <= r.options.maxForwardCalls);
	for (const auto &h : adaptive.hypotheses)
	{
		CHECK(h.feasibility == H::Feasibility::ProvenFree); CHECK(!h.existenceConfidence.known);
		CHECK(h.kind != H::Kind::PhysicalEmitterCandidate); CHECK(!h.reasons.empty());
	}
	CHECK(std::any_of(audit.batches.begin(), audit.batches.end(), [](const AdaptiveHidden::Batch &b) { return b.darkReceivers > 0; }));
	std::reverse(rows->begin(), rows->end());
	CHECK(AdaptiveHidden::Generate(r, AdaptiveHidden::Options(), read, repeated, repeatAudit));
	CHECK(adaptive.key == repeated.key); CHECK(adaptive.hypotheses.size() == repeated.hypotheses.size());
	for (size_t i = 0; i < adaptive.hypotheses.size() && i < repeated.hypotheses.size(); ++i)
		CHECK(adaptive.hypotheses[i].candidate == repeated.hypotheses[i].candidate);
	CHECK(H::Generate(r, read, strictAfter)); CHECK(strictBefore.key == strictAfter.key);
	CHECK(strictBefore.report.rays == strictAfter.report.rays); CHECK(strictBefore.hypotheses.size() == strictAfter.hypotheses.size());
	AdaptiveHidden::Options explained; explained.explainedAreas = {1};
	CHECK(AdaptiveHidden::Generate(r, explained, read, repeated, repeatAudit));
	CHECK(repeated.candidates.empty()); CHECK(repeatAudit.explainedSkipped == 1); CHECK(repeated.report.forwardCalls == 0);
	auto q = r; q.areas.clear(); CHECK(!AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit)); CHECK(repeated.candidates.empty());
	q = r; q.options.maxBytes = 1; CHECK(!AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit)); CHECK(repeated.candidates.empty());
	q = r; q.options.maxForwardCalls = 8; CHECK(AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit));
	CHECK(repeatAudit.budgetLimited); CHECK(repeated.report.forwardCalls == 0); CHECK(repeated.candidates.empty());
	q = r; auto unknown = std::make_shared<Domain>(); unknown->unknown = true; q.domain = unknown; q.domainKey[0] = 2;
	CHECK(AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit)); CHECK(repeated.candidates.empty());
	H::DirectionConstraint d; d.id = 4; d.originWorld = Vec3(0,0,0); d.toSource = Vec3(0,0,1);
	d.usable = true; d.halfAngleRadians = .2; d.provenance = "Owned aggregate direction alternative";
	q = r; q.directions = {d}; CHECK(AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit));
	CHECK(std::any_of(repeatAudit.batches.begin(), repeatAudit.batches.end(), [](const AdaptiveHidden::Batch &b) { return b.reason.find("Source-direction ray") == 0; }));
	q.directions[0].toSource.x = 10; CHECK(!AdaptiveHidden::Generate(q, {}, read, repeated, repeatAudit));
	Cancel cancel; read.cancellation = &cancel; CHECK(!AdaptiveHidden::Generate(r, {}, read, repeated, repeatAudit)); CHECK(repeated.candidates.empty());
	std::printf("ASR16 adaptive: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
