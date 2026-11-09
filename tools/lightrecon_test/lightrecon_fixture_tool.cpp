// ASR03 owned geometry/material demonstration and adversarial fixture checks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fixture_hypothesis.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace FixtureDiscovery;
namespace
{
unsigned checks = 0;
void Check(bool ok, const char *message)
{
	++checks;
	if (!ok) throw std::runtime_error(message);
}
void Must(Error e) { if (!e) throw std::runtime_error(e.message); }
ReadContext Context() { ReadContext c; c.maxBytes = 64ull*1024*1024; return c; }
Vec3 Plus(Vec3 a, Vec3 b) { return Vec3(a.x+b.x, a.y+b.y, a.z+b.z); }
Vec3 Scale(Vec3 a, float s) { return Vec3(a.x*s, a.y*s, a.z*s); }
Membership Member(const char *key, std::vector<uint32_t> surfaces)
{
	Membership m; m.sourceKey = key; m.surfaces = std::move(surfaces); return m;
}
BakedSurface Quad(uint32_t ordinal, Vec3 origin, Vec3 right, Vec3 up,
				  float width, float height, const char *texture = "solid")
{
	BakedSurface s;
	s.ordinal = ordinal;
	s.motion = SurfaceMotion::StaticWorld;
	s.kind = BakedSurfaceKind::Triangles;
	s.material = std::string("owned/") + texture;
	s.materialFacts.available = true;
	s.materialFacts.signals = MaterialGlow;
	s.materialFacts.emissionTexture = texture;
	s.materialFacts.emissionCoordinatesKnown = true;
	s.materialFacts.emissionStaticCoordinates = true;
	s.materialFacts.emissionLayerCount = 1;
	s.materialFacts.cull = SurfaceCull::Front;
	for (unsigned y = 0; y < 2; ++y)
		for (unsigned x = 0; x < 2; ++x)
		{
			BakedVertex v;
			v.position = Plus(origin, Plus(Scale(right, width*x), Scale(up, height*y)));
			v.textureUv = LightmapUv(x, y);
			s.vertices.push_back(v);
		}
	s.indices = {0,1,3,0,3,2};
	return s;
}
struct Masks : EmissionMaskProvider
{
	Error Read(const EmissionMaskRequest &q, const ReadContext &, Emission::Mask &out) const override
	{
		if (q.texture == "missing") return Failure(Code::Unavailable, "Owned missing mask");
		out = {};
		out.width = 32; out.height = 8;
		out.rgb.resize(32*8*3);
		for (unsigned y = 0; y < 8; ++y)
			for (unsigned x = 0; x < 32; ++x)
				for (unsigned c = 0; c < 3; ++c)
				{
					// Authored source-linear relative colour, never encoded lightmap RGB8.
					float v = c == 0 ? 1.f : c == 1 ? .5f : .25f;
					if (q.texture == "slots" && x >= 8 && x < 24) v = 0;
					out.rgb[(y*32+x)*3+c] = v;
					out.average[c] += v/(32*8);
				}
		return {};
	}
};
std::shared_ptr<const EmitterEvidenceSet> Extract(const std::vector<BakedSurface> &surfaces,
												 const char *entities = "")
{
	// Content identity of these owned source objects; no commercial BSP is fabricated.
	GeometryKey key;
	for (const auto &s : surfaces)
	{
		key.U(s.ordinal); key.U(s.materialFacts.signals); key.U(uint32_t(s.kind));
		for (const auto &v : s.vertices) key.V(v.position);
	}
	LightmapMapIdentity map;
	const auto geometryManifest = key.Finish();
	map.path = "owned/asr03-fixtures.geometry-digest";
	map.bytes = geometryManifest.size();
	map.sha256 = Disk::Hash(geometryManifest.data(), geometryManifest.size());
	SurfaceCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	Must(capture.SetMap(map));
	for (const auto &s : surfaces) Must(capture.Add(s));
	Must(capture.Freeze());
	EmitterEvidenceInput in;
	in.map = map; in.geometry = capture.Acquire(); in.masks = std::make_shared<Masks>();
	in.entityText = entities;
	std::shared_ptr<const EmitterEvidenceSet> evidence;
	Must(ExtractEmitterEvidence(in, {}, Context(), evidence));
	return evidence;
}
const Fixture &Find(const Views &v, const char *key)
{
	for (const auto &f : v.fixtures) if (f.sourceKey == key) return f;
	throw std::runtime_error("Fixture missing");
}
void XYZ(std::ostream &out, Vec3 v) { out << v.x << ',' << v.y << ',' << v.z; }
std::string Shapes(uint32_t bits)
{
	std::string names;
	if (bits & CandidatePoint) names += "point,";
	if (bits & CandidateRectangle) names += "rectangle,";
	if (bits & CandidateTube) names += "tube,";
	if (bits & CandidateSpot) names += "spot,";
	if (bits & CandidateSurface) names += "surface,";
	return names;
}
std::string Report(const Views &v)
{
	std::ostringstream out;
	out << std::setprecision(9);
	out << "# ASR03 diagnostic hypotheses; all placement/bake participation unknown; no fitted intensity\n";
	out << "# original_candidates=" << v.evidence->Candidates().size()
		<< " fixtures=" << v.fixtures.size() << " unassigned=" << v.unassigned
		<< " non_surface=" << v.nonSurface << '\n';
	out << "fixture_id\tobject\tcentroid_known\tsummary_centroid\tcommon_normal_known\tcommon_normal"
		<< "\tregion_id\talias_ids\tsurfaces\tregion_position_known\tregion_position\tregion_normal_known"
		<< "\tregion_normal\tright\tup\thalf_width\thalf_height\tarea_estimate\tshape_alternatives_bits"
		<< "\tshape_alternatives\ttwo_sided\tcolour_prior_known\trelative_rgb\tmask_sampled\tissues_bits\tstatic_eligible\n";
	for (const auto &f : v.fixtures)
		for (const auto &r : f.regions)
		{
			const auto &e = v.evidence->Evidence()[r.evidence];
			const auto &c = v.evidence->Candidates()[r.candidate];
			out << f.id << '\t' << f.sourceKey << '\t' << f.positionKnown << '\t'; XYZ(out, f.position);
			out << '\t' << f.orientationKnown << '\t'; XYZ(out, f.outward);
			out << '\t' << e.id << '\t';
			for (auto id : c.sourceIds) out << id << ',';
			out << '\t'; for (auto s : e.surfaces) out << s << ',';
			out << '\t' << e.positionKnown << '\t'; XYZ(out, e.position);
			out << '\t' << e.orientationKnown << '\t'; XYZ(out, e.outward);
			out << '\t'; XYZ(out, e.right); out << '\t'; XYZ(out, e.up);
			out << '\t' << e.halfWidth << '\t' << e.halfHeight << '\t' << e.areaWorld << '\t' << e.shapes
				<< '\t' << Shapes(e.shapes) << '\t' << e.twoSided
				<< '\t' << e.colourKnown << '\t'; XYZ(out, e.colour);
			out << '\t' << e.maskSampled << '\t' << c.issues << '\t' << c.staticEligible << '\n';
		}
	return out.str();
}
struct Cancelled : Cancellation { bool IsCancelled() const noexcept override { return true; } };
void NegativeChecks()
{
	const Vec3 x(1,0,0), y(0,1,0);
	auto a = Quad(0, {}, x, y, 4, 4), b = Quad(1, Vec3(4.01f,0,0), x, y, 4, 4);
	auto evidence = Extract({a,b});
	Views v;
	Must(Build(evidence, {Member("A", {0}), Member("B", {1})}, Context(), v));
	Check(v.fixtures.size() == 2 && evidence->Candidates().size() == 2,
		  "Nearby identical lamps must remain separate");
	Check(Find(v,"A").id != Find(v,"B").id, "Distinct object identities");
	Check(Build(evidence, {Member("A", {0}), Member("B", {0})}, Context(), v).code == Code::InvalidInput
		  && !v.evidence, "Overlapping membership must clear output");
	// Connected original emission can straddle declared object identities. Do not duplicate it.
	b = Quad(1, Vec3(4,0,0), x, y, 4, 4);
	evidence = Extract({a,b});
	Check(evidence->Candidates().size() == 1, "Connected baseline control");
	Check(Build(evidence, {Member("A", {0}), Member("B", {1})}, Context(), v).code == Code::InvalidInput,
		  "Cross-object region must require re-extraction per object");
	Check(Build(evidence, {Member("A", {0})}, Context(), v).code == Code::InvalidInput,
		  "Partial membership must remain ambiguous");
	Must(Build(evidence, {}, Context(), v));
	Check(v.unassigned == 1 && v.fixtures.empty(), "Unassigned emission must be reported");
	a.materialFacts.emissionTexture = "missing";
	evidence = Extract({a});
	Must(Build(evidence, {Member("missing-mask", {0})}, Context(), v));
	Check((v.fixtures[0].issues & MaskUnavailable) != 0, "Missing texture cannot become certain");
	a.kind = BakedSurfaceKind::Unsupported; a.vertices.clear(); a.indices.clear();
	evidence = Extract({a});
	Must(Build(evidence, {Member("unknown-geometry", {0})}, Context(), v));
	Check(!v.fixtures[0].positionKnown && !v.fixtures[0].orientationKnown &&
		  !v.fixtures[0].staticEligible && (v.fixtures[0].issues & EvidenceUnsupportedGeometry),
		  "Unknown geometry is not empty or placed");
	Must(Build(evidence, {Member("no-emission", {55})}, Context(), v));
	Check(!v.fixtures[0].positionKnown && !v.fixtures[0].staticEligible,
		  "Object membership alone does not prove a luminous source");
	ReadContext tiny = Context(); tiny.maxBytes = 1;
	Check(Build(evidence, {Member("A", {0})}, tiny, v).code == Code::MemoryLimit && !v.evidence,
		  "Bounded diagnostic allocation");
	Cancelled cancelled; auto ctx = Context(); ctx.cancellation = &cancelled;
	Check(Build(evidence, {}, ctx, v).code == Code::Cancelled && !v.evidence, "Cancellation");
	a = Quad(0, {}, x, y, 4, 4); a.motion = SurfaceMotion::Unknown;
	evidence = Extract({a}, "{ \"classname\" \"light\" \"origin\" \"0 0 9\" }");
	Must(Build(evidence, {Member("unknown-state", {0})}, Context(), v));
	Check(!v.fixtures[0].staticEligible && v.nonSurface == 1, "Unknown state and separate entity prior");
}
} // namespace
int main(int argc, char **argv)
{
	try
	{
		const Vec3 x(1,0,0), y(0,1,0), z(0,0,1);
		auto slots = Quad(0, Vec3(0,0,64), x, y, 32, 8, "slots");
		// Dual compiler/glow hints must be retained as aliases, not extra emitting area.
		slots.materialFacts.legacySurfaceLight = 100;
		slots.materialFacts.legacySourceColour = Vec3(.1f,1,.1f);
		auto tube = Quad(1, Vec3(48,0,16), y, z, 32, 2);
		auto front = Quad(2, Vec3(96,0,16), x, y, 8, 8);
		auto side = Quad(3, Vec3(96,0,16), y, z, 8, 8);
		auto housing = Quad(4, Vec3(-1,-1,63), x, y, 34, 10);
		housing.materialFacts.signals = 0;
		auto evidence = Extract({slots,tube,front,side,housing});
		std::vector<Membership> members = {Member("owned/dual-slot", {0,4}),
			Member("owned/wall-strip", {1}), Member("owned/corner-lamp", {2,3})};
		Views v;
		Must(Build(evidence, members, Context(), v));
		Check(evidence->Candidates().size() == 5 && evidence->Evidence().size() == 7,
			  "Baseline: five luminous regions, seven evidence records");
		Check(v.fixtures.size() == 3 && !v.unassigned, "Three attested fixtures");
		const auto &dual = Find(v, "owned/dual-slot");
		Check(dual.regions.size() == 2 && std::abs(dual.sampledArea-128) < .001,
			  "Dark gap and duplicate compiler hints must not add area");
		Check(dual.colourKnown && dual.colour.x == 1 && dual.colour.y == .5f && dual.colour.z == .25f,
			  "Compiler colour aliases must not overwrite the source texture colour prior");
		Check(dual.positionKnown && std::abs(dual.position.x-16) < .001 &&
			  std::abs(dual.position.y-4) < .001 && std::abs(dual.position.z-64) < .001,
			  "Symmetric region centroid in world coordinates");
		const auto &strip = Find(v, "owned/wall-strip");
		const auto &stripEvidence = evidence->Evidence()[strip.regions[0].evidence];
		Check((stripEvidence.shapes & CandidateTube) && strip.orientationKnown && strip.outward.x > .999,
			  "Rotated strip keeps tube alternative and world orientation");
		const auto &corner = Find(v, "owned/corner-lamp");
		Check(corner.regions.size() == 2 && !corner.orientationKnown && corner.positionKnown,
			  "Multi-face lamp retains per-region normals, no invented common direction");
		for (const auto &e : evidence->Evidence())
			Check(e.unit != EvidenceUnit::LuminanceNits && (e.issues & UncalibratedEmission),
				  "Relative colour and legacy compiler hints cannot calibrate radiance");
		const auto report = Report(v);
		std::reverse(members.begin(), members.end());
		for (auto &m : members) std::reverse(m.surfaces.begin(), m.surfaces.end());
		Views repeat; Must(Build(evidence, members, Context(), repeat));
		Check(report == Report(repeat), "Membership order must not change the diagnostic result");
		NegativeChecks();
		if (argc > 2) throw std::runtime_error("Usage: lightrecon_fixture_tool [report.tsv]");
		if (argc == 2)
		{
			std::ofstream out(argv[1], std::ios::binary); out << report;
			if (!out) throw std::runtime_error("Cannot write report");
		}
		else std::cout << report;
		std::cout << "ASR03: " << checks << " checks passed; 5 regions -> 3 fixture views.\n";
		return 0;
	}
	catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
