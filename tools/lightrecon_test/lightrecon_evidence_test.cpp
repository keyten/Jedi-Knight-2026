// Synthetic source evidence; no renderer, engine files or OpenGL.
#include "lightrecon/evidence.h"
#include "lightrecon/session.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <type_traits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
bool Near(float a, float b, float eps = 1e-4f)
{
	return std::abs(a - b) <= eps;
}
ReadContext Context()
{
	ReadContext c;
	c.maxBytes = 64ull * 1024 * 1024;
	return c;
}
LightmapMapIdentity Map()
{
	LightmapMapIdentity m;
	m.path = "maps/synthetic.bsp";
	m.bytes = 3;
	m.sha256 = Disk::Hash("map", 3);
	return m;
}
BakedSurface Panel(unsigned ordinal = 0, float x = 0, float width = 16, float height = 8)
{
	BakedSurface s;
	s.ordinal = ordinal;
	s.model = 0;
	s.motion = SurfaceMotion::StaticWorld;
	s.kind = BakedSurfaceKind::Face;
	s.material = "panel";
	s.materialFacts.available = true;
	s.materialFacts.signals = MaterialGlow;
	s.materialFacts.emissionTexture = "mask";
	for (unsigned y = 0; y < 2; ++y)
		for (unsigned i = 0; i < 2; ++i)
		{
			BakedVertex v;
			v.position = Vec3(x + i * width, y * height, 0);
			v.textureUv = LightmapUv(i, y);
			s.vertices.push_back(v);
		}
	s.indices = {0, 1, 3, 0, 3, 2};
	return s;
}
std::shared_ptr<const SurfaceGeometry> Geometry(std::vector<BakedSurface> s)
{
	SurfaceCapture c;
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetMap(Map()));
	for (auto &v : s)
		CHECK(c.Add(std::move(v)));
	CHECK(c.Freeze());
	return c.Acquire();
}
struct MaskProvider : EmissionMaskProvider
{
	Emission::Mask mask;
	Code failure = Code::Ok;
	mutable int reads = 0;
	MaskProvider()
	{
		mask.width = 16;
		mask.height = 8;
		mask.rgb.assign(16 * 8 * 3, 1);
		for (float &v : mask.average)
			v = 1;
	}
	Error Read(const EmissionMaskRequest &, const ReadContext &, Emission::Mask &out) const override
	{
		++reads;
		if (failure != Code::Ok)
			return Failure(failure, "test provider");
		out = mask;
		return {};
	}
};
std::shared_ptr<const EmitterEvidenceSet> Run(EmitterEvidenceInput in)
{
	std::shared_ptr<const EmitterEvidenceSet> s;
	auto err = ExtractEmitterEvidence(in, EvidenceOptions(), Context(), s);
	if (!err)
		std::printf("extraction error: %s\n", err.message.c_str());
	CHECK(err);
	CHECK(s);
	return s;
}
const EmitterEvidence *Origin(const EmitterEvidenceSet &s, EvidenceOrigin o)
{
	for (const auto &e : s.Evidence())
		if (e.origin == o)
			return &e;
	return nullptr;
}
void Empty()
{
	EmitterEvidenceInput in;
	in.map = Map();
	auto s = Run(in);
	CHECK(s->Candidates().empty());
	CHECK(s->Evidence().empty());
	CHECK(s->Report().bytes > 0);
	InputDataset d;
	d.emitterEvidence = s;
	InputReader reader(d);
	CHECK(reader.EmitterEvidence() == s);
	static_assert(
		std::is_same<decltype(s->Evidence()), const std::vector<EmitterEvidence> &>::value,
		"Read-only publication");
	// An evidence provider, including a nonempty entity candidate set, does
	// not install an algorithm or become canonical reconstructed output.
	in.entityText = "{ \"classname\" \"light\" \"origin\" \"1 2 3\" \"light\" \"300\" }";
	s = Run(in);
	CHECK(!s->Candidates().empty());
	d.emitterEvidence = s;
	Session session(d);
	CHECK(session.Begin().code == Code::Unavailable);
	CHECK(session.State() == SessionState::Unavailable);
	CHECK(!session.Output());
}
void GeometryTests()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	auto masks = std::make_shared<MaskProvider>();
	in.masks = masks;
	auto s = Run(in);
	CHECK(s->Candidates().size() == 1);
	const auto &e = s->Evidence()[0];
	CHECK(e.maskSampled);
	CHECK(e.shapes & CandidateRectangle);
	CHECK(e.staticEligible);
	CHECK(e.issues & ShapeHypothesis);
	CHECK(!e.intensityKnown);
	CHECK(e.unit == EvidenceUnit::Unknown);
	CHECK(Near(e.position.x, 8));
	CHECK(Near(e.position.y, 4));
	CHECK(Near(e.position.z, 0));
	CHECK(Near(e.areaWorld, 128));
	CHECK(Near(e.outward.z, 1));
	CHECK(e.colourKnown && Near(e.colour.x, 1));
	CHECK(e.chromaticityKnown);
	CHECK(
		Near(.2126f * e.chromaticity.x + .7152f * e.chromaticity.y + .0722f * e.chromaticity.z, 1));
	CHECK(s->Report().triangles == 2);
	// Adjacent disconnected fixtures are not merged by colour, including less than legacy
	// quantization.
	in.geometry = Geometry({Panel(0), Panel(1, 16.01f)});
	s = Run(in);
	CHECK(s->Candidates().size() == 2);
	// Coplanar connected fragments of one panel share evidence and geometry.
	in.geometry = Geometry({Panel(0, 0, 8), Panel(1, 8, 8)});
	s = Run(in);
	CHECK(s->Candidates().size() == 1);
	CHECK(s->Evidence()[0].surfaces.size() == 2);
	// Two luminous strips inside one texture are two blobs, not an encompassing lamp.
	for (unsigned y = 0; y < 8; ++y)
		for (unsigned x = 0; x < 16; ++x)
			for (unsigned c = 0; c < 3; ++c)
				masks->mask.rgb[(y * 16 + x) * 3 + c] = (x < 3 || x > 12) ? 1.f : 0.f;
	in.geometry = Geometry({Panel()});
	s = Run(in);
	CHECK(s->Candidates().size() == 2);
	for (const auto &v : s->Evidence())
		CHECK(v.maskSampled);
	in.geometry = Geometry({Panel(0, 0, 128, 1)});
	masks = std::make_shared<MaskProvider>();
	in.masks = masks;
	s = Run(in);
	CHECK(s->Evidence()[0].shapes & CandidateTube);
	CHECK(s->Evidence()[0].radius > 0);
	in.geometry = Geometry({Panel(0, 0, 1, 1)});
	s = Run(in);
	CHECK(s->Evidence()[0].shapes & CandidatePoint);
	auto p = Panel();
	p.materialFacts.signals |= MaterialAnimated;
	s = Run([&] {
		auto t = in;
		t.geometry = Geometry({p});
		return t;
	}());
	CHECK(!s->Candidates()[0].staticEligible);
	CHECK(s->Evidence()[0].issues & AnimatedEvidence);
	p.motion = SurfaceMotion::BrushModel;
	in.geometry = Geometry({p});
	s = Run(in);
	CHECK(s->Evidence()[0].issues & MovingEvidence);
	p = Panel();
	p.materialFacts.signals = MaterialSky;
	in.geometry = Geometry({p});
	s = Run(in);
	CHECK(s->Evidence().empty());
	p = Panel();
	p.kind = BakedSurfaceKind::Unsupported;
	in.geometry = Geometry({p});
	s = Run(in);
	CHECK(!s->Evidence()[0].positionKnown);
	CHECK(s->Evidence()[0].issues & EvidenceUnsupportedGeometry);
	p = Panel();
	p.materialFacts.emissionCoordinatesKnown = true;
	p.materialFacts.emissionStaticCoordinates = false;
	in.geometry = Geometry({p});
	s = Run(in);
	CHECK(!s->Evidence()[0].maskSampled);
	CHECK(s->Evidence()[0].issues & MaskCoordinatesUnsupported);
	in.geometry = Geometry({Panel()});
	in.masks.reset();
	s = Run(in);
	CHECK(s->Evidence()[0].issues & MaskUnavailable);
	CHECK(!s->Evidence()[0].colourKnown);
	p = Panel();
	p.materialFacts.legacySurfaceLight = 300;
	p.materialFacts.legacySourceColour = Vec3(1, .5f, .2f);
	in.geometry = Geometry({p});
	s = Run(in);
	CHECK(s->Candidates().size() == 1);
	CHECK(s->Evidence().size() == 2);
	const auto *hint = Origin(*s, EvidenceOrigin::SurfaceLight);
	CHECK(hint && hint->unit == EvidenceUnit::LegacyBakeTool && hint->intensityKnown &&
		  Near(hint->intensity, 300));
	p.materialFacts.emissionProvenance = EmissionProvenance::AuthoredLuminance;
	p.materialFacts.authoredLuminance = 420;
	in.geometry = Geometry({p});
	s = Run(in);
	hint = Origin(*s, EvidenceOrigin::GlowSurface);
	CHECK(hint && hint->unit == EvidenceUnit::LuminanceNits && Near(hint->intensity, 420));
}
void EmptyLuminousMask()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	auto masks = std::make_shared<MaskProvider>();
	for (float &v : masks->mask.rgb)
		v = 0;
	for (float &v : masks->mask.average)
		v = 0;
	in.masks = masks;
	auto s = Run(in);
	CHECK(s->Candidates().size() == 1);
	CHECK(s->Evidence()[0].issues & NoLuminousMaskSamples);
	CHECK(s->Evidence()[0].colourKnown);
	CHECK(s->Evidence()[0].colour.x == 0);
	CHECK(!s->Evidence()[0].chromaticityKnown);
	CHECK(!s->Evidence()[0].intensityKnown);
}

void Authored()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.areaPath = "maps/synthetic.arealights.json";
	in.areaJson = R"({"lights":[
 {"name":"authored panel","center":[8,4,0],"right":[2,0,0],"up":[0,3,0],"halfWidth":8,"halfHeight":4,"color":[0.1,0.4,0.2],"luminanceNits":123,"surface":0,"custom":"preserved"},
 {"type":"line","name":"tube","start":[0,0,0],"end":[0,0,100],"radius":0.25,"intensity":2},
 {"center":[100,0,0],"right":[1,0,0],"up":[0,1,0],"halfWidth":1,"halfHeight":2,"mode":"dynamic"},
 {"center":[200,0,0],"right":[1,0,0],"up":[0,1,0],"halfWidth":1,"halfHeight":2,"automatic":true}]})";
	in.geometry = Geometry({Panel()});
	auto s = Run(in);
	CHECK(s->Candidates().size() == 4);
	CHECK(s->Evidence().size() == 5);
	bool merged = false, foundTube = false, dynamic = false, automatic = false;
	for (const auto &c : s->Candidates())
	{
		const auto &e = s->Evidence()[c.primary];
		if (e.name == "authored panel")
		{
			merged = true;
			CHECK(c.evidence.size() == 2);
			CHECK(Near(e.right.x, 2) && Near(e.up.y, 3));
			CHECK(e.raw.find("custom") != std::string::npos);
			CHECK(e.intensityKnown && Near(e.intensity, 123));
			CHECK(e.unit == EvidenceUnit::LuminanceNits);
			CHECK(e.colour.x == .1f);
		}
		if (e.name == "tube")
		{
			foundTube = true;
			CHECK(e.shapes == CandidateTube);
			CHECK(Near(e.halfWidth, 50) && Near(e.radius, .25f));
			CHECK(e.unit == EvidenceUnit::RelativeShader);
		}
		if (e.mode == "dynamic")
		{
			dynamic = true;
			CHECK(!c.staticEligible);
		}
		if (e.origin == EvidenceOrigin::AutomaticArea)
		{
			automatic = true;
			CHECK(!(e.issues & AuthoredPrior));
		}
	}
	CHECK(merged && foundTube && dynamic && automatic);
	// Ambiguous links to several blobs never collapse all lamps into one authored prior.
	auto masks = std::make_shared<MaskProvider>();
	for (unsigned y = 0; y < 8; ++y)
		for (unsigned x = 0; x < 16; ++x)
			for (unsigned c = 0; c < 3; ++c)
				masks->mask.rgb[(y * 16 + x) * 3 + c] = (x < 3 || x > 12) ? 1.f : 0.f;
	in.masks = masks;
	s = Run(in);
	CHECK(s->Candidates().size() == 6);
	bool ambiguous = false;
	for (const auto &c : s->Candidates())
		if (c.issues & AmbiguousCorrespondence)
			ambiguous = true;
	CHECK(ambiguous);
}
void ImplicitGeometryLink()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	in.masks = std::make_shared<MaskProvider>();
	in.areaJson =
		R"({"lights":[{"center":[8,4,0.25],"right":[1,0,0],"up":[0,1,0],"halfWidth":8,"halfHeight":4,"luminanceNits":50}]})";
	auto s = Run(in);
	CHECK(s->Candidates().size() == 1);
	CHECK(s->Candidates()[0].evidence.size() == 2);
	CHECK(s->Evidence()[s->Candidates()[0].primary].position.z == .25f);
}

void SeparateAuthoredRecords()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	in.areaJson =
		R"({"lights":[{"name":"first","surface":0,"center":[8,4,0],"right":[1,0,0],"up":[0,1,0],"halfWidth":8,"halfHeight":4,"luminanceNits":100},
 {"name":"second","surface":0,"center":[9,4,0],"right":[1,0,0],"up":[0,1,0],"halfWidth":8,"halfHeight":4,"luminanceNits":200}]})";
	auto s = Run(in);
	CHECK(s->Candidates().size() == 2);
	unsigned authors = 0;
	for (const auto &c : s->Candidates())
	{
		const auto &e = s->Evidence()[c.primary];
		CHECK(e.origin == EvidenceOrigin::AuthoredArea);
		authors += e.origin == EvidenceOrigin::AuthoredArea;
	}
	CHECK(authors == 2);
}

void EntitiesSun()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.entityText = R"({"classname" "worldspawn"}
 {"classname" "light" "origin" "1 2 3" "_color" "0.2 0.5 1" "light" "300" "style" "2" "target" "aim" "radius" "24"}
 {"classname" "info_null" "targetname" "aim" "origin" "1 2 -7"}
 {"classname" "light" "origin" "4 5 6" "target" "missing"})";
	ShaderEvidenceText shader;
	shader.material = "sky";
	shader.text = "{\nq3map_sun 1 0.5 0.2 300 90 30\nq3map_sunExt 1 1 1 100 0 90 2 16\nq3gl2_sun 1 "
				  "1 1 80 0 0 0.5 0.8\nsun 1 1 1 20 180 0\nq3map_skylight 40 4\nskyparms env/test "
				  "- -\n{ map normalmap }\n}";
	in.shaders.push_back(shader);
	auto s = Run(in);
	CHECK(s->Report().entities == 2);
	CHECK(s->Report().suns == 4);
	CHECK(s->Report().skies == 2);
	const auto *e = Origin(*s, EvidenceOrigin::BspEntity);
	CHECK(e);
	bool spot = false, missing = false;
	for (const auto &v : s->Evidence())
	{
		if (v.origin == EvidenceOrigin::BspEntity && v.orientationKnown)
		{
			spot = true;
			CHECK(v.shapes & CandidateSpot);
			CHECK(Near(v.outward.z, -1));
			CHECK(v.style == 2);
			CHECK(!v.staticEligible);
			CHECK(v.unit == EvidenceUnit::LegacyBakeTool);
			CHECK(v.entities[0] == 1);
			CHECK(v.sourceRadiusHint == 24);
			CHECK(v.radius == 0);
		}
		if (v.issues & MissingTarget)
			missing = true;
		if (v.origin == EvidenceOrigin::SunShader && v.ordinal == 0)
		{
			CHECK(Near(v.toSource.x, 0));
			CHECK(Near(v.toSource.y, .8660254f));
			CHECK(Near(v.toSource.z, .5f));
			CHECK(Near(v.outward.z, -.5f));
			CHECK(v.unit == EvidenceUnit::LegacyBakeTool);
			CHECK(v.raw.find("300") != std::string::npos);
		}
	}
	CHECK(spot && missing);
	// No shader declaration means no default sun evidence, regardless of sky presence.
	in.shaders[0].text = "{\nskyparms - - -\n}";
	s = Run(in);
	CHECK(!Origin(*s, EvidenceOrigin::SunShader));
	in.entityText = "";
	s = Run(in);
	CHECK(s->Report().entities == 0);
}
void CheckError(EmitterEvidenceInput bad, Code expected, EvidenceOptions o = EvidenceOptions(),
				ReadContext ctx = Context())
{
	std::shared_ptr<const EmitterEvidenceSet> s;
	auto e = ExtractEmitterEvidence(bad, o, ctx, s);
	CHECK(e.code == expected);
	CHECK(!s);
}
void ErrorsAndIds()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	auto base = Run(in);
	auto id = base->Evidence()[0].id;
	for (int n = 0; n < 100; ++n)
	{
		auto s = Run(in);
		CHECK(s->Evidence()[0].id == id);
		CHECK(s->Candidates()[0].sourceIds[0] == id);
		CHECK(s->Report().bytes == base->Report().bytes);
	}
	auto lease = base;
	in.geometry.reset();
	base.reset();
	CHECK(lease->Evidence()[0].id == id);

	in.areaJson = "{";
	CheckError(in, Code::InvalidInput);
	in.areaJson = "{\"lights\":[{\"intensity\":NaN}]}";
	CheckError(in, Code::InvalidInput);
	in.areaJson = "{\"lights\":[],\"lights\":[]}";
	CheckError(in, Code::InvalidInput);
	in.areaJson = "{\"lights\":[{\"center\":[0,0,0],\"right\":[0,0,0],\"up\":[0,1,0],\"halfWidth\":"
				  "2,\"halfHeight\":3}]}";
	CheckError(in, Code::InvalidInput);
	in.areaJson = "";
	in.entityText = "{\"classname\" \"light\" \"origin\" \"NaN 0 0\"}";
	CheckError(in, Code::InvalidInput);
	in.entityText = "";
	in.geometry = Geometry({Panel()});
	in.map.sha256 = Disk::Hash("other", 5);
	CheckError(in, Code::InvalidInput);
	in.map = Map();
	auto masks = std::make_shared<MaskProvider>();
	in.masks = masks;
	masks->failure = Code::ProviderError;
	CheckError(in, Code::ProviderError);
	masks->failure = Code::Unavailable;
	auto s = Run(in);
	CHECK(s->Evidence()[0].issues & MaskUnavailable);
	masks->failure = Code::Ok;
	masks->mask.rgb[0] = std::numeric_limits<float>::infinity();
	CheckError(in, Code::InvalidInput);
	EvidenceOptions options;
	options.maxEvidence = 0;
	CheckError(in, Code::InvalidInput, options);
	options = EvidenceOptions();
	options.maxTriangles = 1;
	CheckError(in, Code::MemoryLimit, options);
	options = EvidenceOptions();
	options.maxWork = 1;
	in.masks.reset();
	CheckError(in, Code::WorkLimit, options);
	ReadContext ctx = Context();
	ctx.maxBytes = 4;
	CheckError(in, Code::MemoryLimit, EvidenceOptions(), ctx);
	struct Cancel : Cancellation
	{
		bool IsCancelled() const noexcept override
		{
			return true;
		}
	} cancel;
	ctx = Context();
	ctx.cancellation = &cancel;
	CheckError(in, Code::Cancelled, EvidenceOptions(), ctx);
	// Map lineage controls identity, not memory addresses, master cvars or extraction calls.
	in.geometry.reset();
	in.entityText = "{\"classname\" \"light\" \"origin\" \"1 2 3\"}";
	auto a = Run(in);
	in.map.sha256 = Disk::Hash("changed", 7);
	auto b = Run(in);
	CHECK(a->Evidence()[0].id != b->Evidence()[0].id);
}
void ProviderLifetime()
{
	EmitterEvidenceInput in;
	in.map = Map();
	in.geometry = Geometry({Panel()});
	auto masks = std::make_shared<MaskProvider>();
	in.masks = masks;
	auto s = Run(in);
	int reads = masks->reads;
	for (int i = 0; i < 100; ++i)
	{
		CHECK(s->Candidates().size() == 1);
		CHECK(s->Evidence()[0].id != 0);
		CHECK(s->Report().bytes > 0);
	}
	CHECK(masks->reads == reads);
	std::weak_ptr<MaskProvider> weak = masks;
	in.masks.reset();
	masks.reset();
	in.geometry.reset();
	CHECK(weak.expired());
	CHECK(s->Evidence()[0].surfaces[0] == 0);
}

void GeometryNumerics()
{
	// Deterministic arbitrary planar rotated fixtures and raw centroid (no LTC .25 bias).
	for (unsigned n = 0; n < 300; ++n)
	{
		float angle = float(n) * .013f, c = std::cos(angle), s = std::sin(angle);
		float normal[3] = {0, 0, 1};
		std::vector<Emission::Point> pts(4);
		float corners[4][2] = {{-8, -2}, {8, -2}, {8, 2}, {-8, 2}};
		std::vector<const float *> refs;
		for (unsigned k = 0; k < 4; ++k)
		{
			pts[k].p[0] = corners[k][0] * c - corners[k][1] * s + 11;
			pts[k].p[1] = corners[k][0] * s + corners[k][1] * c - 13;
			pts[k].p[2] = 7;
			refs.push_back(pts[k].p);
		}
		Emission::Rectangle r;
		Emission::FitRectangle(refs, normal, 0, r);
		CHECK(Near(r.center[0], 11));
		CHECK(Near(r.center[1], -13));
		CHECK(Near(r.center[2], 7));
		CHECK(Near(r.halfWidth, 8, 2e-5f));
		CHECK(Near(r.halfHeight, 2, 2e-5f));
	}
}
} // namespace
int main()
{
	Empty();
	GeometryTests();
	EmptyLuminousMask();
	Authored();
	ImplicitGeometryLink();
	SeparateAuthoredRecords();
	EntitiesSun();
	ErrorsAndIds();
	ProviderLifetime();
	GeometryNumerics();
	std::printf("emitter evidence: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
