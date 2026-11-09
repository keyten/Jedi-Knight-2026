// Controlled original-grid records. Assertions remain active in Release.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_grid_evidence.h"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace OriginalGridEvidence;
static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)
static const double pi = 3.14159265358979323846;
static double Dot(Vec3 a, Vec3 b) { return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z; }
static Vec3 Unit(Vec3 v) { double n = std::sqrt(Dot(v,v)); return Vec3(float(v.x/n),float(v.y/n),float(v.z/n)); }
static double Angle(Vec3 a, Vec3 b) { return std::acos(std::max(-1.0,std::min(1.0,Dot(Unit(a),Unit(b))))); }
static bool Near(double a, double b) { return std::abs(a-b)<1e-6; }
// Independently transcribed pinned q3map2 NormalToLatLong; truncation precedes mask.
static void Encode(Vec3 n, uint8_t &p, uint8_t &a)
{
	if (n.x == 0 && n.y == 0) { p = n.z > 0 ? 0 : 128; a = 0; return; }
	p = uint8_t(int(std::acos(n.z)*255/(2*pi)) & 255);
	a = uint8_t(int(std::atan2(n.y,n.x)*255/(2*pi)) & 255);
}
class Domain final : public RegionDomain
{
public:
	bool unknown = false, solid = false, boundary = false, spanUnknown = false;
	Error Locate(Vec3, SpatialLocation &l, double = 1e-5) const override
	{ l = {}; l.space = unknown ? SpaceClass::Unknown : solid ? SpaceClass::Solid : SpaceClass::Free; l.boundary = boundary; return {}; }
	Error ClassifyBox(const Bounds &b, SpaceClass &s, uint64_t &work) const override
	{
		if (b.minWorld.x >= b.maxWorld.x || b.minWorld.y >= b.maxWorld.y || b.minWorld.z >= b.maxWorld.z)
			return Failure(Code::InvalidInput,"Degenerate fixture box");
		s = unknown || spanUnknown ? SpaceClass::Unknown : solid ? SpaceClass::Solid : SpaceClass::Free; work = 1; return {};
	}
};
class Visibility final : public VisibilityProvider
{
public:
	VisibilityState state = VisibilityState::Visible;
	uint32_t issues = 0; bool wall = false;
	Error Query(const RayQuery &q, const ReadContext &, RayResult &r) const override
	{
		r = {}; r.state = wall && q.endWorld.x > q.startWorld.x ? VisibilityState::Blocked : state;
		r.reliability = RayReliability::Geometric; r.issues = issues; return {};
	}
};
struct Rig
{
	Input input; Options options; ReadContext read;
	std::shared_ptr<Domain> domain = std::make_shared<Domain>();
	std::shared_ptr<Visibility> visibility = std::make_shared<Visibility>();
	std::vector<uint8_t> records, indices;
	std::vector<float> hdr;
	Rig()
	{
		input.domain = domain; input.visibility = visibility;
		records.resize(27*30); indices.resize(54);
		for (unsigned i = 0; i < 27; ++i)
		{
			indices[2*i] = uint8_t(i);
			auto *p = records.data()+30*i;
			p[24] = 0; p[25] = 7; p[26] = p[27] = 255; p[28] = 64;
			unsigned x = i%3, y = (i/3)%3, z = i/9;
			for (unsigned c = 0; c < 3; ++c)
			{
				p[c] = uint8_t(30+2*x+3*y+4*z);
				p[12+c] = uint8_t(60+4*x+6*y+8*z);
				p[3+c] = 180; p[15+c] = 120;
			}
		}
		Capture();
	}
	void Capture()
	{
		LightgridCapture capture; capture.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity map; map.path = "maps/asr12-owned.bsp"; map.bytes = 4096; map.sha256[0] = 12;
		CHECK(capture.SetSource(map,records.data(),records.size(),0,indices.data(),indices.size(),1024));
		GridLayout l; l.originWorld = Vec3(-2,-4,-8); l.spacingWorld = Vec3(2,4,8); l.bounds[0] = l.bounds[1] = l.bounds[2] = 3;
		CHECK(capture.SetLayout(l));
		if (!hdr.empty()) CHECK(capture.SetHdr(GridHdrState::Present,"maps/asr12-owned/lightgrid.raw",hdr.data(),hdr.size()*sizeof(float)));
		CHECK(capture.Freeze()); input.grid = capture.Acquire();
	}
	Observation Get(uint64_t i = 13) { Observation o; CHECK(Extract(input,options,i,read,o)); return o; }
};
static void Directions()
{
	const Vec3 axes[] = {Vec3(1,0,0),Vec3(0,1,0),Vec3(0,0,1),Vec3(-1,0,0),Vec3(0,-1,0),Vec3(0,0,-1)};
	for (auto v : axes)
	{
		uint8_t p,a; Encode(v,p,a); auto d = InterpretDirection(p,a,Compression::Unknown);
		CHECK(Angle(v,d.writer255) <= d.writerEncodingBoundRadians);
		CHECK(Dot(v,d.consumer256) > .998); CHECK(!d.compressionBoundKnown);
	}
	CHECK(Angle(InterpretDirection(64,255,Compression::Unknown).writer255,
		Vec3(float(std::cos(-2*pi/255)*std::sin(128*pi/255)),float(std::sin(-2*pi/255)*std::sin(128*pi/255)),float(std::cos(128*pi/255)))) < .0005);
	CHECK(InterpretDirection(128,0,Compression::Unknown).writer255.z == -1);
	CHECK(InterpretDirection(128,0,Compression::Unknown).writer255CodePossible);
	CHECK(!InterpretDirection(64,128,Compression::Unknown).writer255CodePossible);
	CHECK(!InterpretDirection(129,0,Compression::Unknown).writer255CodePossible);
	double maxWriter = 0, maxConsumer = 0;
	for (int lat = 1; lat < 180; lat += 2) for (int az = -180; az < 180; az += 2)
	{
		double p = lat*pi/180, a = az*pi/180;
		Vec3 v(float(std::sin(p)*std::cos(a)),float(std::sin(p)*std::sin(a)),float(std::cos(p)));
		uint8_t pb,ab; Encode(v,pb,ab); auto d = InterpretDirection(pb,ab,Compression::Q3map2Merge4);
		double ew = Angle(v,d.writer255), ec = Angle(v,d.consumer256);
		maxWriter = std::max(maxWriter,ew); maxConsumer = std::max(maxConsumer,ec);
		CHECK(ew <= d.writerEncodingBoundRadians); CHECK(ec <= d.consumerEncodingBoundRadians);
	}
	// Merge threshold >=251 accepts five circular bins, including wraparound.
	auto d = InterpretDirection(64,0,Compression::Q3map2Merge4);
	CHECK(Angle(d.writer255,InterpretDirection(64,251,Compression::Unknown).writer255) <= d.mergeBoundRadians);
	CHECK(d.mergeBoundRadians > 9*2*pi/255);
	std::printf("angular maxima: writer255 %.9g rad, consumer256 %.9g rad\n",maxWriter,maxConsumer);
	// Two equal orthogonal lights produce a diagonal aggregate, neither emitter direction.
	Vec3 multiple = Unit(Vec3(1,1,0)); uint8_t p,a; Encode(multiple,p,a);
	auto mixed = InterpretDirection(p,a,Compression::Unknown);
	CHECK(Angle(mixed.writer255,multiple) < mixed.writerEncodingBoundRadians);
	CHECK(Angle(mixed.writer255,axes[0]) > .7 && Angle(mixed.writer255,axes[1]) > .7);
	// Sun + local: independently weighted vector at two receivers; no source identity recovered.
	Vec3 first = Unit(Vec3(2,0,5)), second = Unit(Vec3(-1,0,5));
	Encode(first,p,a); auto one = InterpretDirection(p,a,Compression::Unknown);
	Encode(second,p,a); auto two = InterpretDirection(p,a,Compression::Unknown);
	CHECK(one.writer255.x > 0 && two.writer255.x < 0); CHECK(one.writer255.z > .9 && two.writer255.z > .9);
}
static void Gradients()
{
	Rig r; auto o = r.Get();
	CHECK(o.raw.centreWorld.x == 0 && o.raw.centreWorld.y == 0 && o.raw.centreWorld.z == 0);
	CHECK(o.location.space == SpaceClass::Free && o.raw.geometry == GridGeometry::Unknown);
	for (unsigned a = 0; a < 3; ++a)
	{
		auto g = o.styles[0].directedGradient.intensity[a];
		double expected = (a == 0 ? 2 : a == 1 ? 1.5 : 1)/255;
		CHECK(g.available && g.bounded && Near(g.value,expected)); CHECK(g.lower <= expected && g.upper >= expected);
		CHECK(o.styles[1].directedGradient.intensity[a].value == 0);
	}
	CHECK(!o.direction.compressionBoundKnown && !o.interpolationErrorKnown);
	r.options.compression = Compression::Q3map2Merge4;
	auto merged = r.Get().styles[0].directedGradient.intensity[0];
	CHECK(merged.upper-merged.lower > 4*(o.styles[0].directedGradient.intensity[0].upper-o.styles[0].directedGradient.intensity[0].lower));
	// Match STYLE ID across neighbours, even when packed slots change.
	for (unsigned i : {12u,14u})
	{
		auto *p = r.records.data()+30*i; std::swap(p[24],p[25]);
		for (unsigned c = 0; c < 3; ++c) { std::swap(p[c],p[3+c]); std::swap(p[12+c],p[15+c]); }
	}
	r.Capture(); CHECK(Near(r.Get().styles[0].directedGradient.intensity[0].value,2.0/255));
	r.visibility->wall = true; o = r.Get(); CHECK(o.edges[1] == Edge::Blocked); CHECK(!o.styles[0].directedGradient.intensity[0].available);
	r.visibility->wall = false; r.visibility->issues = OpticalUnknown;
	CHECK(!r.Get().styles[0].directedGradient.intensity[0].available);
	r.visibility->issues = 0; r.domain->spanUnknown = true;
	CHECK(!r.Get().styles[0].directedGradient.intensity[0].available);
	r.domain->spanUnknown = false; r.domain->unknown = true;
	CHECK(r.Get().location.space == SpaceClass::Unknown); CHECK(!r.Get().styles[0].directedGradient.intensity[0].available);
	r.domain->unknown = false; r.domain->solid = true;
	CHECK(r.Get().location.space == SpaceClass::Solid); CHECK(!r.Get().styles[0].directedGradient.intensity[0].available);
	r.domain->solid = false; r.domain->boundary = true;
	CHECK(!r.Get().styles[0].directedGradient.intensity[0].available); r.domain->boundary = false;
	r.input.visibility.reset(); CHECK(!r.Get().styles[0].directedGradient.intensity[0].available); r.input.visibility = r.visibility;
	r.input.domain.reset(); CHECK(r.Get().location.space == SpaceClass::Unknown); r.input.domain = r.domain;
	DynamicBoundary moving; r.input.dynamic.push_back(moving);
	CHECK(r.Get().dynamicPossible); CHECK(!r.Get().styles[0].directedGradient.intensity[0].available); r.input.dynamic.clear();
	r.options.colourSpace = ColourSpace::ProfileSourceRelative;
	CHECK(!r.Get().styles[0].directed.intensity.available); // unknown transfer is never linear
	r.input.profile.identity = "owned-linear"; r.input.profile.source.assumedTransfer = Decode::Transfer::Linear;
	r.input.profile.source.assumptionRationale = "controlled numeric fixture";
	CHECK(r.Get().styles[0].directed.intensity.available);
	CHECK(r.Get().styles[0].directed.decoderFlags & Decode::TransferAssumed);
}
static void InvalidAndWeak()
{
	Rig r;
	for (unsigned c = 0; c < 3; ++c) r.records[13*30+12+c] = 1;
	r.Capture(); CHECK(r.Get().styles[0].weakDirected);
	r.indices[2*13] = 255; r.Capture(); auto invalid = r.Get();
	CHECK(invalid.raw.presence == GridPresence::InvalidIndex); CHECK(!invalid.styles[0].active);
	r.indices[2*13] = 13; r.indices[2*14] = 255; r.Capture();
	CHECK(!r.Get().styles[0].directedGradient.intensity[0].available);
	r.indices[2*14] = 12; r.Capture();
	CHECK(r.Get().styles[0].directedGradient.intensity[0].value == 0); // duplicate records: not independent spatial evidence
	r.records[13*30+24] = 255; r.records[13*30+25] = 7; r.Capture();
	CHECK(!r.Get().styles[1].active && (r.Get().raw.issues & GridStyleGap));
	Rig saturated; saturated.records[14*30+12] = 255; saturated.Capture();
	CHECK(saturated.Get().styles[0].directedGradient.intensity[0].available);
	CHECK(!saturated.Get().styles[0].directedGradient.intensity[0].bounded);
	Observation out;
	CHECK(!Extract(r.input,r.options,27,r.read,out));
	CHECK(out.raw.presence == GridPresence::Absent);
	CHECK(!Extract(Input(),Options(),0,r.read,out));
	CHECK(!r.Get(0).styles[0].directedGradient.intensity[0].available);
	// Dense HDR is unstyled and does not follow compressed record indices or replace LDR.
	Rig h; h.hdr.resize(27*6,3); h.hdr[13*6+3] = 17;
	h.indices[2*13] = 255; h.Capture(); auto hr = h.Get();
	CHECK(hr.raw.presence == GridPresence::InvalidIndex && hr.raw.hdrPresent);
	CHECK(hr.hdrAmbient.intensity.available && hr.hdrAmbient.rgb[0].value == 3);
	CHECK(hr.hdrDirected.rgb[0].value == 17 && !hr.hdrDirected.rgb[0].bounded);
	h.hdr[13*6+3] = std::numeric_limits<float>::infinity(); h.Capture();
	CHECK(!h.Get().hdrDirected.intensity.available); CHECK(h.Get().raw.issues & GridHdrNonFinite);
	h.hdr[13*6+3] = -1; h.Capture(); CHECK(!h.Get().hdrDirected.intensity.available);
	// Duplicate active IDs never create a gradient by arbitrary slot selection.
	Rig duplicate; duplicate.records[13*30+25] = 0; duplicate.Capture();
	CHECK(!duplicate.Get().styles[0].directedGradient.intensity[0].available);
}
static void Mixtures()
{
	// Controlled q3map2 grid reduction, no surface cosine. Norm-RGB weights choose ONE
	// direction before per-style ambient/directed reduction. Not a production bake emulator.
	Rig r;
	auto pack = [&](Vec3 first, Vec3 second, Vec3 rgbA, Vec3 rgbB)
	{
		const double wA = std::sqrt(Dot(rgbA,rgbA)), wB = std::sqrt(Dot(rgbB,rgbB));
		Vec3 sum(float(first.x*wA+second.x*wB),float(first.y*wA+second.y*wB),float(first.z*wA+second.z*wB));
		Vec3 m = Dot(sum,sum) > 0 ? Unit(sum) : Vec3();
		uint8_t p,a; Encode(m,p,a);
		auto *record = r.records.data()+13*30;
		record[28] = p; record[29] = a;
		for (unsigned s = 0; s < 2; ++s)
		{
			Vec3 v = s ? second : first, rgb = s ? rgbB : rgbA;
			double q = std::max(0.,Dot(v,m));
			double channels[3] = {rgb.x,rgb.y,rgb.z};
			for (unsigned c = 0; c < 3; ++c)
			{
				record[12+3*s+c] = uint8_t(q*channels[c]);
				record[3*s+c] = uint8_t(.25*(1-q)*channels[c]);
			}
		}
		r.Capture(); return r.Get();
	};
	auto orthogonal = pack(Vec3(1,0,0),Vec3(0,1,0),Vec3(80,0,0),Vec3(0,0,80));
	CHECK(orthogonal.raw.directed[0][0] == 56 && orthogonal.raw.directed[1][2] == 56);
	CHECK(orthogonal.raw.ambient[0][0] == 5 && orthogonal.raw.ambient[1][2] == 5);
	CHECK(orthogonal.styles[0].id == 0 && orthogonal.styles[1].id == 7);
	CHECK(Angle(orthogonal.direction.writer255,Unit(Vec3(1,1,0))) < orthogonal.direction.writerEncodingBoundRadians);
	auto cancelled = pack(Vec3(1,0,0),Vec3(-1,0,0),Vec3(80,80,80),Vec3(80,80,80));
	CHECK(cancelled.styles[0].weakDirected && cancelled.styles[1].weakDirected);
	CHECK(cancelled.raw.ambient[0][0] == 20 && cancelled.raw.directed[0][0] == 0);
	CHECK(!cancelled.raw.direction.confidenceKnown); // fallback encoded vector is not evidence of a lamp
	auto sunLocal = pack(Vec3(0,0,1),Vec3(1,0,0),Vec3(120,120,120),Vec3(40,40,40));
	CHECK(sunLocal.direction.writer255.z > .9 && sunLocal.direction.writer255.x > .25);
}
int main()
{
	Directions(); Gradients(); InvalidAndWeak(); Mixtures();
	std::printf("original grid evidence: %d checks, %d failures\n",checks,failures);
	return failures ? 1 : 0;
}
