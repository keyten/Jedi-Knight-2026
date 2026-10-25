// Deliberately small forward reference fixture baker, NOT a reconstruction solver.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "offline_fixtures.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace OfflineFixtures
{
namespace
{
constexpr float pi = 3.14159265358979323846f, units = 32;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
Vec3 Add(Vec3 a, Vec3 b)
{
	return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}
Vec3 Sub(Vec3 a, Vec3 b)
{
	return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}
Vec3 Mul(Vec3 a, float b)
{
	return Vec3(a.x * b, a.y * b, a.z * b);
}
float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 Cross(Vec3 a, Vec3 b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
float Len(Vec3 a)
{
	return std::sqrt(Dot(a, a));
}
Vec3 Unit(Vec3 a)
{
	float n = Len(a);
	return n > 1e-8f ? Mul(a, 1 / n) : Vec3();
}
Vec3 Product(Vec3 a, Vec3 b)
{
	return Vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}
void Vec(std::ostream &out, Vec3 p)
{
	out << '[' << p.x << ',' << p.y << ',' << p.z << ']';
}
struct Light
{
	std::string type;
	Vec3 position, colour = Vec3(1, 1, 1), axis = Vec3(0, 0, -1);
	float intensity = 120, cone = 25;
};
struct Scene
{
	std::string name;
	std::vector<BakedSurface> surfaces;
	BspSpaceInput space;
	std::vector<Light> lights;
	bool sun = false, sky = false, bounce = false;
	Vec3 sunDirection = Unit(Vec3(.4f, .1f, 1));
};
MaterialFacts Material(std::string name)
{
	MaterialFacts m;
	m.available = true;
	m.cull = SurfaceCull::None;
	m.baseTexture = "self-authored/" + name;
	m.signals = MaterialLightmapped;
	m.meaning = BakedMeaning::DiffuseModulationBakeUnknown;
	m.uncertainty |= BakeReflectanceUnknown | TextureIsNotBakeAlbedo;
	return m;
}
void Quad(Scene &s, Vec3 a, Vec3 b, Vec3 c, Vec3 d, std::string material = "white",
		  bool floor = false, bool sky = false)
{
	BakedSurface v;
	v.ordinal = uint32_t(s.surfaces.size());
	v.model = 0;
	v.motion = SurfaceMotion::StaticWorld;
	v.kind = BakedSurfaceKind::Face;
	v.material = "textures/recon/" + material;
	v.materialIndex =
		uint32_t(material == "white" ? 0 : material == "red" ? 1 : material == "glow" ? 2 : 3);
	v.materialFacts = Material(material);
	if (sky)
		v.materialFacts.signals = MaterialSky;
	if (material == "glow")
	{
		v.materialFacts.signals |= MaterialGlow;
		v.materialFacts.emissionTexture = "self-authored/panel-mask";
		v.materialFacts.emissionProvenance = EmissionProvenance::GlowTexture;
		v.materialFacts.emissionColourKnown = true;
		v.materialFacts.emissionColour = Vec3(.8f, 1, .7f);
	}
	Vec3 normal = Unit(Cross(Sub(b, a), Sub(c, a)));
	unsigned k = 0;
	for (auto p : {a, b, c, d})
	{
		BakedVertex vert;
		vert.position = p;
		vert.shadingNormal = normal;
		vert.sourceVertex = v.ordinal * 4 + k;
		vert.colourPresent = true;
		for (unsigned slot = 0; slot < 4; ++slot)
		{
			vert.colour[slot][0] = 128;
			vert.colour[slot][1] = 121;
			vert.colour[slot][2] = 110;
			vert.colour[slot][3] = 255;
		}
		unsigned x = k == 1 || k == 2, y = k >= 2;
		vert.textureUv = LightmapUv(x, y);
		vert.lightmap[0] = LightmapUv(x, y);
		v.vertices.push_back(vert);
		++k;
	}
	v.indices = {0, 1, 2, 0, 2, 3};
	v.vertexStyles[0] = 0;
	if (floor)
	{
		v.lightmap[0] = 0;
		v.styles[0] = 0;
		v.chartWidth = v.chartHeight = 16;
		v.lightmapOrigin = a;
		v.lightmapBasis[0] = Mul(Sub(b, a), 1.0f / 16);
		v.lightmapBasis[1] = Mul(Sub(d, a), 1.0f / 16);
		v.lightmapBasis[2] = normal;
	}
	else
		v.materialFacts.signals |= MaterialVertexLit;
	s.surfaces.push_back(std::move(v));
}
void BoxBrush(Scene &s, Vec3 lo, Vec3 hi)
{
	SpatialBrush b;
	b.firstSide = uint32_t(s.space.sides.size());
	b.sideCount = 6;
	b.solid = true;
	for (unsigned axis = 0; axis < 3; ++axis)
		for (unsigned high = 0; high < 2; ++high)
		{
			SpatialPlane p;
			if (axis == 0)
				p.normal.x = high ? 1.f : -1.f;
			if (axis == 1)
				p.normal.y = high ? 1.f : -1.f;
			if (axis == 2)
				p.normal.z = high ? 1.f : -1.f;
			float value = axis == 0 ? (high ? hi.x : lo.x)
									: axis == 1 ? (high ? hi.y : lo.y) : (high ? hi.z : lo.z);
			p.distance = high ? value : -value;
			s.space.sides.push_back(uint32_t(s.space.planes.size()));
			s.space.planes.push_back(p);
		}
	s.space.leafBrushes.push_back(uint32_t(s.space.brushes.size()));
	s.space.brushes.push_back(b);
}
void Partition(Scene &s, float bottom, float top, float ymin, float ymax)
{
	Quad(s, Vec3(0, ymin, bottom), Vec3(0, ymax, bottom), Vec3(0, ymax, top), Vec3(0, ymin, top));
	BoxBrush(s, Vec3(-2, ymin, bottom), Vec3(2, ymax, top));
}
Scene Build(std::string name)
{
	Scene s;
	s.name = name;
	s.space.worldBounds.known = true;
	s.space.worldBounds.minWorld = Vec3(-208, -144, -16);
	s.space.worldBounds.maxWorld = Vec3(208, 144, 288);
	Quad(s, Vec3(-192, -128, 0), Vec3(192, -128, 0), Vec3(192, 128, 0), Vec3(-192, 128, 0), "white",
		 true);
	Quad(s, Vec3(-192, -128, 0), Vec3(-192, 128, 0), Vec3(-192, 128, 256), Vec3(-192, -128, 256));
	Quad(s, Vec3(192, 128, 0), Vec3(192, -128, 0), Vec3(192, -128, 256), Vec3(192, 128, 256),
		 name == "bounce_colour" ? "red" : "white");
	Quad(s, Vec3(192, -128, 0), Vec3(-192, -128, 0), Vec3(-192, -128, 256), Vec3(192, -128, 256));
	Quad(s, Vec3(-192, 128, 0), Vec3(192, 128, 0), Vec3(192, 128, 256), Vec3(-192, 128, 256));
	BoxBrush(s, Vec3(-208, -144, -16), Vec3(208, 144, 0));
	BoxBrush(s, Vec3(-208, -144, 0), Vec3(-192, 144, 272));
	BoxBrush(s, Vec3(192, -144, 0), Vec3(208, 144, 272));
	BoxBrush(s, Vec3(-192, -144, 0), Vec3(192, -128, 272));
	BoxBrush(s, Vec3(-192, 128, 0), Vec3(192, 144, 272));
	if (name == "sun_sky" || name == "skylight")
	{
		s.sun = s.sky = true;
		Quad(s, Vec3(-208, -144, 280), Vec3(208, -144, 280), Vec3(208, 144, 280),
			 Vec3(-208, 144, 280), "sky", false, true);
		if (name == "skylight")
		{
			Quad(s, Vec3(-192, -128, 256), Vec3(-32, -128, 256), Vec3(-32, 128, 256),
				 Vec3(-192, 128, 256));
			Quad(s, Vec3(32, -128, 256), Vec3(192, -128, 256), Vec3(192, 128, 256),
				 Vec3(32, 128, 256));
			Quad(s, Vec3(-32, -128, 256), Vec3(32, -128, 256), Vec3(32, -32, 256),
				 Vec3(-32, -32, 256));
			Quad(s, Vec3(-32, 32, 256), Vec3(32, 32, 256), Vec3(32, 128, 256), Vec3(-32, 128, 256));
		}
	}
	else
	{
		Quad(s, Vec3(-192, -128, 256), Vec3(192, -128, 256), Vec3(192, 128, 256),
			 Vec3(-192, 128, 256));
		BoxBrush(s, Vec3(-208, -144, 256), Vec3(208, 144, 272));
	}
	Light l;
	l.type = "point";
	l.position = Vec3(-96, 32, 180);
	l.intensity = 120;
	s.lights.push_back(l);
	if (name == "two_colour" || name == "overlap_whites")
	{
		s.lights[0].colour = name == "two_colour" ? Vec3(1, .12f, .03f) : Vec3(1, 1, 1);
		l.position = Vec3(96, -32, 160);
		l.colour = name == "two_colour" ? Vec3(.04f, .2f, 1) : Vec3(1, 1, 1);
		s.lights.push_back(l);
	}
	if (name == "spot")
	{
		s.lights[0].type = "spot";
		s.lights[0].position = Vec3(64, -80, 200);
		s.lights[0].axis = Unit(Sub(Vec3(-32, 64, 0), s.lights[0].position));
	}
	if (name == "panel" || name == "tube")
	{
		s.lights[0].type = name;
		s.lights[0].position = Vec3(0, 0, 240);
		s.lights[0].intensity = name == "panel" ? 50.f : 100.f;
		float width = name == "panel" ? 72.f : 80.f, height = name == "panel" ? 32.f : 4.f;
		Quad(s, Vec3(-width, height, 240), Vec3(width, height, 240), Vec3(width, -height, 240),
			 Vec3(-width, -height, 240), "glow");
	}
	if (name == "sun_sky" || name == "skylight")
		s.lights.clear();
	if (name == "door_occlusion")
	{
		Partition(s, 0, 256, -128, -40);
		Partition(s, 0, 256, 40, 128);
		Partition(s, 160, 256, -40, 40);
	}
	if (name == "wall_leak")
	{
		Partition(s, 0, 256, -128, 128);
		s.lights[0].position = Vec3(-96, 0, 180);
	}
	if (name == "bounce_colour")
	{
		s.bounce = true;
		s.lights[0].position = Vec3(100, 0, 160);
		s.surfaces[0].materialFacts.baseColourKnown = true;
		s.surfaces[0].materialFacts.baseColourLinear = Vec3(.45f, .5f, .6f);
		s.surfaces[0].materialFacts.uncertainty |= TextureIsNotBakeAlbedo;
	}
	SpatialLeaf leaf;
	leaf.cluster = 0;
	leaf.area = 0;
	leaf.firstBrush = 0;
	leaf.brushCount = uint32_t(s.space.brushes.size());
	s.space.leaves.push_back(leaf);
	return s;
}
// Independent Moller-Trumbore reference: no BVH, tiny fixture scenes only.
bool Clear(const Scene &s, Vec3 start, Vec3 end, bool sky = false)
{
	Vec3 ray = Sub(end, start);
	double best = 1;
	bool hitSky = false;
	for (const auto &surf : s.surfaces)
		for (size_t n = 0; n + 2 < surf.indices.size(); n += 3)
		{
			Vec3 a = surf.vertices[surf.indices[n]].position,
				 b = surf.vertices[surf.indices[n + 1]].position,
				 c = surf.vertices[surf.indices[n + 2]].position, e1 = Sub(b, a), e2 = Sub(c, a),
				 p = Cross(ray, e2);
			double det = Dot(e1, p);
			if (std::abs(det) < 1e-8)
				continue;
			double inv = 1 / det;
			Vec3 t = Sub(start, a);
			double u = Dot(t, p) * inv;
			if (u < 0 || u > 1)
				continue;
			Vec3 q = Cross(t, e1);
			double v = Dot(ray, q) * inv;
			if (v < 0 || u + v > 1)
				continue;
			double distance = Dot(e2, q) * inv;
			if (distance <= 1e-5 || distance >= best - 1e-7)
				continue;
			best = distance;
			hitSky = (surf.materialFacts.signals & MaterialSky) != 0;
		}
	return sky ? hitSky : best >= 1 || hitSky;
}
Vec3 Direct(const Scene &s, Vec3 position, Vec3 normal, Vec3 &dominant)
{
	Vec3 result;
	dominant = {};
	Vec3 start = Add(position, Mul(normal, .001f));
	auto sample = [&](Vec3 source, Vec3 colour, float candela) {
		Vec3 d = Sub(source, position);
		float distance = Len(d);
		if (distance < 1e-4f || !Clear(s, start, source))
			return;
		d = Mul(d, 1 / distance);
		float e = candela * std::max(0.f, Dot(normal, d)) / (distance * distance / (units * units));
		Vec3 rgb = Mul(colour, e);
		result = Add(result, rgb);
		dominant = Add(dominant, Mul(d, .2126f * rgb.x + .7152f * rgb.y + .0722f * rgb.z));
	};
	for (const auto &l : s.lights)
	{
		if (l.type == "point" || l.type == "spot")
		{
			if (l.type == "spot" &&
				Dot(Unit(Sub(position, l.position)), l.axis) < std::cos(l.cone * pi / 180))
				continue;
			sample(l.position, l.colour, l.intensity);
		}
		else if (l.type == "panel")
		{
			for (unsigned y = 0; y < 4; ++y)
				for (unsigned x = 0; x < 4; ++x)
				{
					Vec3 p(-72 + (x + .5f) * 36, -32 + (y + .5f) * 16, 240);
					float cosLight = std::max(0.f, Unit(Sub(position, p)).z * -1);
					sample(p, l.colour, l.intensity * cosLight * (36 * 16 / (units * units)));
				}
		}
		else if (l.type == "tube")
		{
			for (unsigned i = 0; i < 12; ++i)
			{
				Vec3 p(-80 + (i + .5f) * (160.f / 12), 0, 240);
				Vec3 d = Unit(Sub(position, p));
				float projected = std::sqrt(std::max(0.f, 1 - d.x * d.x));
				sample(p, l.colour, l.intensity * projected * (8 * 160.f / 12 / (units * units)));
			}
		}
	}
	if (s.sun && Clear(s, start, Add(position, Mul(s.sunDirection, 4096)), true))
	{
		result =
			Add(result, Mul(Vec3(1, .98f, .9f), 1.4f * std::max(0.f, Dot(normal, s.sunDirection))));
		dominant = Add(dominant, s.sunDirection);
	}
	return result;
}
Vec3 Shade(const Scene &s, Vec3 p, Vec3 normal, Vec3 &direction)
{
	Vec3 direct = Direct(s, p, normal, direction);
	Vec3 ambient(.025f, .025f, .025f);
	if (s.sky)
	{
		unsigned visible = 0;
		for (Vec3 d : {Vec3(0, 0, 1), Unit(Vec3(.5f, .2f, 1)), Unit(Vec3(-.5f, .2f, 1)),
					   Unit(Vec3(.1f, -.4f, 1))})
			visible += Clear(s, Add(p, Mul(normal, .001f)), Add(p, Mul(d, 4096)), true);
		ambient = Add(ambient, Mul(Vec3(.1f, .16f, .24f), visible * .25f));
	}
	if (s.bounce)
	{
		for (unsigned y = 0; y < 4; ++y)
			for (unsigned z = 0; z < 4; ++z)
			{
				Vec3 wall(192, -96 + (y + .5f) * 48, 32 + (z + .5f) * 48), ignored;
				Vec3 incoming = Direct(s, wall, Vec3(-1, 0, 0), ignored);
				Vec3 d = Sub(wall, p);
				float distance = Len(d);
				d = Mul(d, 1 / distance);
				if (Clear(s, Add(p, Mul(normal, .001f)), Add(wall, Vec3(-.002f, 0, 0))))
				{
					float e = std::max(0.f, Dot(normal, d)) * std::max(0.f, -d.x) *
							  (48 * 48 / (units * units)) /
							  (distance * distance / (units * units)) / pi;
					ambient = Add(ambient, Mul(Product(incoming, Vec3(.85f, .08f, .03f)), e));
				}
			}
	}
	return Add(direct, ambient);
}
uint8_t Encode(float value)
{
	value = std::max(0.f, std::min(1.f, value * .35f));
	float v = value <= .0031308f ? 12.92f * value : 1.055f * std::pow(value, 1 / 2.4f) - .055f;
	return uint8_t(std::floor(v * 255 + .5f));
}
std::string Answer(const Scene &s)
{
	std::ostringstream o;
	o.imbue(std::locale::classic());
	o << std::setprecision(9)
	  << "{\n\"schema\":\"org.openjk.synthetic-lighting-answer\",\"version\":1,\"scene\":\""
	  << s.name
	  << "\",\n\"backend\":\"cpu-forward-reference-v1, not "
		 "q3map2\",\"compiler_executed\":false,\n\"units_per_metre\":32,\"observation_gain\":0.35,"
		 "\"transfer\":\"sRGB quantized "
		 "RGB8\",\"source_secret\":\"ANSWER_ONLY_DO_NOT_EXPORT\",\n\"lights\":[";
	for (size_t i = 0; i < s.lights.size(); ++i)
	{
		const auto &l = s.lights[i];
		if (i)
			o << ',';
		o << "{\"type\":\"" << l.type << "\",\"position\":";
		Vec(o, l.position);
		o << ",\"colour\":";
		Vec(o, l.colour);
		o << ",\"axis_propagation\":";
		Vec(o, l.axis);
		o << ",\"intensity\":" << l.intensity << ",\"unit\":\""
		  << (l.type == "point" || l.type == "spot" ? "candela" : "luminance_cd_per_m2")
		  << "\",\"cone_degrees\":" << l.cone;
		if (l.type == "panel")
			o << ",\"half_width\":72,\"half_height\":32";
		if (l.type == "tube")
			o << ",\"half_length\":80,\"radius\":4,\"reference_quadrature_segments\":12";
		o << '}';
	}
	o << "],\n\"sun\":{\"present\":" << (s.sun ? "true" : "false") << ",\"direction_to_source\":";
	Vec(o, s.sunDirection);
	o << ",\"illuminance_lux\":1.4},\"sky\":" << (s.sky ? "true" : "false")
	  << ",\"bounce_model\":\"" << (s.bounce ? "one coloured wall, 4x4 diffuse quadrature" : "none")
	  << "\",\"styles\":[0,3],\"missing_grid_last_cell\":true,\"wall_cell_heuristics_forbidden\":"
		 "true,\"rgb8_overrides\":[{\"xy\":[0,0],\"rgb\":[0,0,0]},"
		 "{\"xy\":[1,0],\"rgb\":[255,32,18]},{\"xy\":[2,0],\"rgb\":[255,255,255]},"
		 "{\"xy\":[3,0],\"rgb\":[254,254,254]}],\"invalid_grid_index_cell\":4,"
		 "\"grid_missing_index_cell\":29,\"unknown_material_bake_albedo\":true,"
		 "\"overbright_bits\":0,\"pre_exposure_stops\":0\n}\n";
	return o.str();
}
class Masks : public EmissionMaskProvider
{
	Error Read(const EmissionMaskRequest &, const ReadContext &, Emission::Mask &out) const override
	{
		out.width = 16;
		out.height = 8;
		out.rgb.assign(16 * 8 * 3, 0);
		for (unsigned y = 1; y < 7; ++y)
			for (unsigned x = 1; x < 15; ++x)
				for (unsigned c = 0; c < 3; ++c)
					out.rgb[(y * 16 + x) * 3 + c] = 1;
		for (float &x : out.average)
			x = .65625f;
		return {};
	}
};
} // namespace
std::vector<std::string> Names()
{
	return {"point_room", "two_colour", "overlap_whites", "spot",		   "panel",	   "tube",
			"sun_sky",	  "skylight",	"door_occlusion", "bounce_colour", "wall_leak"};
}
Fixture Make(const std::string &name)
{
	auto names = Names();
	if (std::find(names.begin(), names.end(), name) == names.end())
		throw std::runtime_error("Unknown synthetic scene");
	Scene scene = Build(name);
	Fixture result;
	result.answer = Answer(scene);
	ReadContext ctx;
	ctx.maxBytes = 256ull * 1024 * 1024;
	// Synthetic source byte stream with private answer header. Only hashes and selected
	// observation chunks leave this builder; the header is NEVER exported as raw BSP.
	std::vector<uint8_t> source(65536, 0);
	std::memcpy(source.data(), result.answer.data(), std::min<size_t>(result.answer.size(), 4096));
	constexpr size_t pixelOffset = 4096;
	std::vector<uint8_t> deluxe(16 * 16 * 4);
	for (unsigned y = 0; y < 16; ++y)
		for (unsigned x = 0; x < 16; ++x)
		{
			Vec3 direction;
			Vec3 colour = Shade(scene, Vec3(-192 + (x + .5f) * 24, -128 + (y + .5f) * 16, 0),
								Vec3(0, 0, 1), direction);
			uint8_t rgb[3] = {Encode(colour.x), Encode(colour.y), Encode(colour.z)};
			// Realistic sensor/bake imperfections, explicitly documented fixture injection.
			if (x == 0 && y == 0)
				rgb[0] = rgb[1] = rgb[2] = 0;
			if (x == 1 && y == 0)
			{
				rgb[0] = 255;
				rgb[1] = 32;
				rgb[2] = 18;
			}
			if (x == 2 && y == 0)
				rgb[0] = rgb[1] = rgb[2] = 255;
			if (x == 3 && y == 0)
				rgb[0] = rgb[1] = rgb[2] = 254;
			for (unsigned c = 0; c < 3; ++c)
				source[pixelOffset + (y * 16 + x) * 3 + c] = rgb[c];
			direction = Unit(direction);
			for (unsigned c = 0; c < 3; ++c)
			{
				float d = c == 0 ? direction.x : c == 1 ? direction.y : direction.z;
				deluxe[(y * 16 + x) * 4 + c] =
					Len(direction) > 0 ? uint8_t(std::floor((d * .5f + .5f) * 255 + .5f)) : 0;
			}
			deluxe[(y * 16 + x) * 4 + 3] = 255;
		}
	std::vector<uint8_t> records, indices;
	GridLayout grid;
	grid.originWorld = Vec3(-128, -64, 64);
	grid.spacingWorld = Vec3(64, 64, 128);
	grid.bounds[0] = 5;
	grid.bounds[1] = 3;
	grid.bounds[2] = 2;
	for (unsigned z = 0; z < 2; ++z)
		for (unsigned y = 0; y < 3; ++y)
			for (unsigned x = 0; x < 5; ++x)
			{
				unsigned ordinal = (z * 3 + y) * 5 + x;
				Vec3 p(-128 + 64.f * x, -64 + 64.f * y, 64 + 128.f * z), dir;
				Vec3 rgb = Shade(scene, p, Vec3(0, 0, 1), dir);
				uint8_t r[30] = {};
				for (unsigned c = 0; c < 3; ++c)
				{
					r[c] = Encode(.05f);
					r[12 + c] = Encode(c == 0 ? rgb.x : c == 1 ? rgb.y : rgb.z);
					if (x == 2 && name == "wall_leak")
						r[12 + c] = 70;
				}
				r[24] = 0;
				r[25] = r[26] = r[27] = 255;
				if (ordinal % 7 == 0)
				{
					r[25] = 3;
					r[15] = 20;
					r[16] = 12;
					r[17] = 30;
				}
				if (ordinal == 2)
					r[24] = r[25] = 255;
				dir = Unit(dir);
				float polar = std::acos(std::max(-1.f, std::min(1.f, dir.z))),
					  azimuth = std::atan2(dir.y, dir.x);
				r[28] = uint8_t(int(std::floor(polar * 256 / (2 * pi) + .5f)) & 255);
				r[29] = uint8_t(int(std::floor(azimuth * 256 / (2 * pi) + .5f)) & 255);
				records.insert(records.end(), r, r + 30);
				uint16_t record = uint16_t(ordinal == 4 ? 65535 : ordinal);
				indices.push_back(uint8_t(record));
				indices.push_back(uint8_t(record >> 8));
			}
	indices.resize(indices.size() - 2);
	std::memcpy(source.data() + 8192, records.data(), records.size());
	std::memcpy(source.data() + 16384, indices.data(), indices.size());
	LightmapCapture images;
	images.Begin(BuildPolicy::ForceRebuild);
	Check(images.SetMap("synthetic://" + name, source.data(), source.size()));
	auto map = images.SourceIdentity();
	Check(images.Embedded(source.data(), source.size(), pixelOffset, 16 * 16 * 3, 16, 16, false));
	Check(images.Rgba8(0, LightmapRole::DeluxeDirection, "self-authored/dm_0000.tga", 16, 16,
					   deluxe.data(), deluxe.size(), 0));
	if (name == "bounce_colour")
	{
		float hdr[4 * 4 * 3];
		for (unsigned i = 0; i < 4 * 4 * 3; ++i)
			hdr[i] = i % 3 == 0 ? 2.5f : .5f;
		Check(images.Rgb32F(1, "self-authored/unused-hdr.hdr", 4, 4, hdr, 4 * 4 * 3));
	}
	Check(images.Freeze());
	result.sources.images = images.Acquire();
	SurfaceCapture geometry;
	geometry.Begin(BuildPolicy::ForceRebuild);
	Check(geometry.SetMap(map));
	for (auto s : scene.surfaces)
		Check(geometry.Add(std::move(s)));
	Check(geometry.Freeze());
	result.sources.geometry = geometry.Acquire();
	LightgridCapture cells;
	cells.Begin(BuildPolicy::ForceRebuild);
	Check(cells.SetSource(map, records.data(), records.size(), 8192, indices.data(), indices.size(),
						  16384));
	Check(cells.SetLayout(grid));
	if (name == "bounce_colour")
	{
		std::vector<float> hdr(30 * 6, 2.5f);
		hdr[0] = std::numeric_limits<float>::quiet_NaN();
		Check(cells.SetHdr(GridHdrState::Present, "self-authored/lightgrid.raw", hdr.data(),
						   hdr.size() * 4));
	}
	else
		Check(cells.SetHdr(GridHdrState::Missing, "self-authored/lightgrid.raw", nullptr, 0));
	Check(cells.Freeze());
	result.sources.grid = cells.Acquire();
	scene.space.identity = map;
	Check(BspRegionDomain::Create(scene.space, ctx, result.sources.domain));
	VisibilityBuildOptions vis;
	vis.patchToleranceWorld = 8;
	std::shared_ptr<const StaticVisibility> rays;
	Check(StaticVisibility::Build(result.sources.geometry, vis, ctx, rays, result.sources.domain));
	RegionBuildInput region;
	region.identity = map;
	region.domain = result.sources.domain;
	region.geometry = rays;
	RegionOptions options;
	options.bounds = scene.space.worldBounds;
	options.minimumCellWorld = 16;
	options.coarseCellWorld = 64;
	options.constrictionWorld = 32;
	options.skySamplesPerRegion = 4;
	Check(LightingRegionGraph::Build(region, options, ctx, result.sources.regions));
	EmitterEvidenceInput evidence;
	evidence.map = map;
	evidence.geometry = result.sources.geometry;
	evidence.masks = std::make_shared<Masks>();
	std::ostringstream retained;
	retained.imbue(std::locale::classic());
	for (const auto &l : scene.lights)
		if (l.type == "point" || l.type == "spot")
			retained << "{\"classname\" \"light\" \"origin\" \"" << l.position.x << ' '
					 << l.position.y << ' ' << l.position.z << "\" \"light\" \"" << l.intensity
					 << "\"}";
	evidence.entityText = retained.str();
	ShaderEvidenceText shader;
	shader.material = "textures/recon/sky";
	shader.text = "{\nq3map_sun 1 1 1 120 14 68\nskyparms self-authored/sky - -\n}";
	if (scene.sky)
		evidence.shaders.push_back(shader);
	Check(ExtractEmitterEvidence(evidence, EvidenceOptions(), ctx, result.sources.evidence));
	auto &m = result.sources.metadata;
	m.map = map;
	m.producer = "Self-authored CPU forward fixtures (not a BSP compiler)";
	m.producerVersion = "org.openjk.fixture-forward.v1";
	m.compiler = "Self-authored CPU reference baker; q3map2 not run";
#ifdef _MSC_VER
	m.compilerVersion = "reference-v1; MSVC " + std::to_string(_MSC_FULL_VER);
#else
	m.compilerVersion = std::string("reference-v1; GNU/Clang ") + __VERSION__;
#endif
	m.compilerKnown = true;
	m.synthetic = true;
	m.hints = Offline::HintPolicy::BlindBenchmark;
	m.units.worldUnitsPerMeter = 32;
	m.recipe = "RGB8: sRGB(clamp(E_reference*0.35)); 4x4 panel,12 tube,4x4 first bounce; explicit "
			   "corrupt/sparse grid cases";
	m.recipeSha256 = Disk::Hex(Disk::Hash(m.recipe.data(), m.recipe.size()));
	m.decoding.identity = "synthetic-quantized-relative-v1";
	m.decoding.relativeDomain = "fixture-reference";
	m.decoding.source.documentedTransfer = Decode::Transfer::SRgb;
	m.decoding.source.primaries = Decode::Primaries::Rec709D65;
	m.decoding.source.linearGain = 1 / .35f;
	m.decoding.source.transferProvenance =
		"Own forward fixture encoding, not inferred historical JA bake";
	m.decoding.source.quantization = Decode::Quantization::RoundNearest;
	m.decoding.source.magnitudeHistoryKnown = false;
	m.decoding.renderer.known = true;
	m.decoding.renderer.hdr = true;
	return result;
}
} // namespace OfflineFixtures
