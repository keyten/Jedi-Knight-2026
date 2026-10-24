// Initial emitter evidence, deliberately separate from ReconstructedLightingAsset.
#include "evidence.h"
#include "../tr_pbr_contract.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <tuple>
#include <sstream>
#include <new>
#include <exception>
namespace Rend2LightRecon
{
namespace Core
{
namespace
{
bool Fin(float x)
{
	return std::isfinite(x);
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
float Length(Vec3 a)
{
	return std::sqrt(Dot(a, a));
}
bool Unit(Vec3 &v)
{
	float n = Length(v);
	if (!Fin(n) || n < 1e-8f)
		return false;
	v = Mul(v, 1 / n);
	return true;
}
bool Colour(Vec3 a)
{
	return Fin(a.x) && Fin(a.y) && Fin(a.z) && a.x >= 0 && a.y >= 0 && a.z >= 0;
}
Vec3 V(const float *p)
{
	return Vec3(p[0], p[1], p[2]);
}
std::string Lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c += 32;
	return s;
}
void Bad(const std::string &m)
{
	throw Failure(Code::InvalidInput, m);
}
struct Token
{
	std::string s;
	size_t begin = 0, end = 0, line = 0;
	bool quoted = false;
};
unsigned HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	Bad("Invalid unicode escape");
	return 0;
}
void Unicode(const std::string &s, size_t &i, std::string &out)
{
	auto code = [&]() {
		if (s.size() - i < 4)
			Bad("Truncated unicode escape");
		unsigned cp = 0;
		for (int n = 0; n < 4; ++n)
			cp = (cp << 4) | HexDigit(s[i++]);
		return cp;
	};
	unsigned cp = code();
	if (cp >= 0xd800 && cp <= 0xdbff)
	{
		if (s.compare(i, 2, "\\u"))
			Bad("Missing low surrogate");
		i += 2;
		unsigned low = code();
		if (low < 0xdc00 || low > 0xdfff)
			Bad("Invalid low surrogate");
		cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
	}
	else if (cp >= 0xdc00 && cp <= 0xdfff)
		Bad("Unpaired low surrogate");
	if (cp <= 0x7f)
		out += char(cp);
	else if (cp <= 0x7ff)
	{
		out += char(0xc0 | (cp >> 6));
		out += char(0x80 | (cp & 63));
	}
	else if (cp <= 0xffff)
	{
		out += char(0xe0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 63));
		out += char(0x80 | (cp & 63));
	}
	else
	{
		out += char(0xf0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 63));
		out += char(0x80 | ((cp >> 6) & 63));
		out += char(0x80 | (cp & 63));
	}
}
std::vector<Token> Tokens(const std::string &s, size_t maxTokens)
{
	std::vector<Token> r;
	size_t i = 0, line = 0;
	while (i < s.size())
	{
		if (s[i] == '\n')
		{
			++line;
			++i;
			continue;
		}
		if ((unsigned char)s[i] <= 32)
		{
			++i;
			continue;
		}
		if (s.compare(i, 2, "//") == 0)
		{
			while (i < s.size() && s[i] != '\n')
				++i;
			continue;
		}
		if (s.compare(i, 2, "/*") == 0)
		{
			i += 2;
			while (i + 1 < s.size() && s.compare(i, 2, "*/"))
			{
				if (s[i] == '\n')
					++line;
				++i;
			}
			if (i + 1 >= s.size())
				Bad("Unterminated comment");
			i += 2;
			continue;
		}
		Token t;
		t.begin = i;
		t.line = line;
		if (s[i] == '"')
		{
			t.quoted = true;
			++i;
			while (i < s.size() && s[i] != '"')
			{
				if (s[i] == '\\')
				{
					if (++i >= s.size())
						Bad("Invalid escape");
					char c = s[i++];
					if (c == 'u')
						Unicode(s, i, t.s);
					else if (c == 'b')
						t.s += '\b';
					else if (c == 'f')
						t.s += '\f';
					else if (c == 'n')
						t.s += '\n';
					else if (c == 'r')
						t.s += '\r';
					else if (c == 't')
						t.s += '\t';
					else if (c == '"' || c == '\\' || c == '/')
						t.s += c;
					else
						Bad("Unsupported string escape");
				}
				else
					t.s += s[i++];
			}
			if (i == s.size())
				Bad("Unterminated string");
			++i;
		}
		else if (std::strchr("{}[]:,", s[i]))
			t.s += s[i++];
		else
		{
			size_t b = i;
			while (i < s.size() && (unsigned char)s[i] > 32 && !std::strchr("{}[]:,\"", s[i]))
				++i;
			t.s = s.substr(b, i - b);
		}
		t.end = i;
		r.push_back(std::move(t));
		if (r.size() > maxTokens)
			throw Failure(Code::MemoryLimit, "Emitter token allocation limit");
	}
	return r;
}
float Number(const std::string &s)
{
	char *e = nullptr;
	double n = std::strtod(s.c_str(), &e);
	if (e != s.c_str() + s.size() || s.empty() || !std::isfinite(n) || std::abs(n) > 3.4e38)
		Bad("Non-finite or invalid numeric evidence");
	return float(n);
}
std::vector<float> Numbers(const std::string &s)
{
	std::istringstream in(s);
	std::vector<float> r;
	std::string t;
	while (in >> t)
		r.push_back(Number(t));
	return r;
}
Vec3 Vector(const std::string &s)
{
	auto n = Numbers(s);
	if (n.size() != 3)
		Bad("Expected three vector components");
	return Vec3(n[0], n[1], n[2]);
}
Id Identity(const LightmapMapIdentity &m, const std::string &key)
{
	auto d = Disk::Hash(key.data(), key.size());
	std::string bytes =
		Disk::Hex(m.sha256) + ":" + std::to_string(kEmitterEvidenceVersion) + ":" + Disk::Hex(d);
	d = Disk::Hash(bytes.data(), bytes.size());
	Id id = 0;
	for (unsigned i = 0; i < 8; ++i)
		id |= Id(d[i]) << (i * 8);
	return id ? id : 1;
}
struct Json
{
	std::string atom;
	bool quoted = false;
	size_t begin = 0, end = 0;
	std::map<std::string, Json> object;
	std::vector<Json> array;
};
Json ParseJson(const std::vector<Token> &t, size_t &i, unsigned depth = 0)
{
	if (i == t.size() || depth > 16)
		Bad("Truncated or nested JSON");
	Json n;
	n.begin = t[i].begin;
	std::string v = t[i].s;
	n.quoted = t[i].quoted;
	if (!n.quoted && (v == "{" || v == "["))
	{
		bool obj = v == "{";
		++i;
		while (i < t.size() && t[i].s != (obj ? "}" : "]"))
		{
			if (obj)
			{
				if (!t[i].quoted)
					Bad("JSON object key required");
				std::string k = t[i++].s;
				if (i == t.size() || t[i++].s != ":")
					Bad("JSON colon required");
				auto c = ParseJson(t, i, depth + 1);
				if (!n.object.emplace(k, std::move(c)).second)
					Bad("Duplicate JSON key");
			}
			else
				n.array.push_back(ParseJson(t, i, depth + 1));
			if (i < t.size() && t[i].s == ",")
			{
				++i;
				if (i == t.size() || t[i].s == "}" || t[i].s == "]")
					Bad("Trailing JSON comma");
			}
			else
				break;
		}
		if (i == t.size() || t[i].s != (obj ? "}" : "]"))
			Bad("JSON closing delimiter required");
		n.atom = obj ? "object" : "array";
		n.end = t[i++].end;
	}
	else
	{
		if (!n.quoted && v != "true" && v != "false" && v != "null")
			Number(v);
		n.atom = v;
		n.end = t[i++].end;
	}
	return n;
}
const Json *Field(const Json &j, const char *k)
{
	auto i = j.object.find(k);
	return i == j.object.end() ? nullptr : &i->second;
}
float Float(const Json &j, const char *k, float fallback)
{
	auto n = Field(j, k);
	if (!n)
		return fallback;
	if (n->quoted)
		Bad("JSON number required");
	return Number(n->atom);
}
std::string String(const Json &j, const char *k, std::string fallback = "")
{
	auto n = Field(j, k);
	if (!n)
		return fallback;
	if (!n->quoted)
		Bad("JSON string required");
	return n->atom;
}
bool Boolean(const Json &j, const char *k)
{
	auto n = Field(j, k);
	if (!n)
		return false;
	if (n->quoted)
		Bad("JSON boolean required");
	if (n->atom == "true")
		return true;
	if (n->atom == "false")
		return false;
	return Number(n->atom) != 0;
}
float Scalar(const Json &j)
{
	if (j.quoted)
		Bad("JSON scalar number required");
	return Number(j.atom);
}
Vec3 JsonVector(const Json &j, const char *k, Vec3 fallback = Vec3())
{
	auto n = Field(j, k);
	if (!n)
		return fallback;
	if (n->quoted || n->atom != "array" || n->array.size() != 3)
		Bad("JSON vector requires three numbers");
	return Vec3(Scalar(n->array[0]), Scalar(n->array[1]), Scalar(n->array[2]));
}
void JsonEvidence(const EmitterEvidenceInput &in, std::vector<EmitterEvidence> &out,
				  size_t maxTokens)
{
	if (in.areaJson.empty())
		return;
	auto t = Tokens(in.areaJson, maxTokens);
	size_t i = 0;
	auto root = ParseJson(t, i);
	if (i != t.size() || root.quoted || root.atom != "object")
		Bad("Area JSON object required");
	auto lights = Field(root, "lights");
	if (!lights || lights->quoted || lights->atom != "array")
		Bad("Area JSON lights array required");
	uint32_t ordinal = 0;
	for (const auto &j : lights->array)
	{
		EmitterEvidence e;
		e.origin = EvidenceOrigin::AuthoredArea;
		e.issues = AuthoredPrior;
		e.staticEligible = true;
		e.ordinal = ordinal++;
		e.source = in.areaPath;
		e.raw = in.areaJson.substr(j.begin, j.end - j.begin);
		e.name = String(j, "name");
		e.mode = String(j, "mode", "static_specular");
		e.id = Identity(in.map, "json:" + in.areaPath + ":" + std::to_string(e.ordinal));
		e.colour = JsonVector(j, "color", Vec3(1, 1, 1));
		e.colourKnown = true;
		e.positionKnown = e.orientationKnown = true;
		auto type = Lower(String(j, "type", "rect"));
		if (type == "rect")
		{
			if (!Field(j, "center") || !Field(j, "right") || !Field(j, "up"))
				Bad("Authored rectangle needs axes and centre");
			e.shapes = CandidateRectangle;
			e.position = JsonVector(j, "center");
			e.right = JsonVector(j, "right");
			e.up = JsonVector(j, "up");
			e.halfWidth = Float(j, "halfWidth", 0);
			e.halfHeight = Float(j, "halfHeight", 0);
			Vec3 r = e.right, u = e.up;
			if (!Unit(r) || !Unit(u) || Length(Cross(r, u)) < 1e-4f || e.halfWidth <= 0 ||
				e.halfHeight <= 0)
				Bad("Degenerate authored rectangle");
			e.outward = Cross(r, u);
			Unit(e.outward);
			e.areaWorld = 4 * e.halfWidth * e.halfHeight;
			e.twoSided = Boolean(j, "twoSided");
		}
		else if (type == "line")
		{
			if (!Field(j, "start") || !Field(j, "end"))
				Bad("Authored tube needs endpoints");
			Vec3 a = JsonVector(j, "start"), b = JsonVector(j, "end");
			e.position = Mul(Add(a, b), .5f);
			e.right = Sub(b, a);
			e.halfWidth = Length(e.right) * .5f;
			e.radius = Float(j, "radius", 1);
			if (!Unit(e.right) || e.radius <= 0)
				Bad("Degenerate authored tube");
			e.halfHeight = e.radius;
			e.shapes = CandidateTube;
			e.twoSided = true;
			e.areaWorld = 4 * 3.14159265358979323846f * e.halfWidth * e.radius;
		}
		else
			Bad("Unsupported authored area type");
		e.range = Float(j, "range", 0);
		auto lum = Field(j, "luminanceNits");
		if (!lum)
			lum = Field(j, "luminance");
		auto intensity = Field(j, "intensity");
		if (lum)
		{
			e.intensityKnown = true;
			e.intensity = Scalar(*lum);
			e.unit = EvidenceUnit::LuminanceNits;
		}
		else if (intensity)
		{
			e.intensityKnown = true;
			e.intensity = Scalar(*intensity);
			e.unit = EvidenceUnit::RelativeShader;
			e.issues |= UncalibratedEmission;
		}
		else
			e.issues |= UncalibratedEmission;
		if (e.mode == "dynamic")
		{
			e.issues |= DynamicAuthoredMode;
			e.staticEligible = false;
		}
		if (Boolean(j, "automatic"))
		{
			e.origin = EvidenceOrigin::AutomaticArea;
			e.issues |= ShapeHypothesis | UncalibratedEmission;
			e.issues &= ~AuthoredPrior;
		}
		const std::pair<const char *, std::vector<uint32_t> *> references[2] = {
			{"surface", &e.surfaces}, {"entity", &e.entities}};
		for (auto pair : references)
			if (auto n = Field(j, pair.first))
			{
				float v = Scalar(*n);
				if (v < 0 || v > 4294967040.0f || v != std::floor(v))
					Bad("Invalid explicit source reference");
				pair.second->push_back(uint32_t(v));
			}
		if (!Colour(e.colour) || e.intensity < 0 || e.range < 0)
			Bad("Invalid authored colour/intensity/range");
		out.push_back(std::move(e));
	}
}
void EntityEvidence(const EmitterEvidenceInput &in, std::vector<EmitterEvidence> &out,
					size_t maxTokens)
{
	auto t = Tokens(in.entityText, maxTokens);
	size_t i = 0;
	struct Entity
	{
		std::map<std::string, std::string> kv;
		size_t begin, end;
	};
	std::vector<Entity> all;
	while (i < t.size())
	{
		if (t[i].s != "{")
			Bad("Entity opening brace required");
		Entity e;
		e.begin = t[i++].begin;
		while (i < t.size() && t[i].s != "}")
		{
			std::string k = Lower(t[i++].s);
			if (i == t.size() || t[i].s == "}")
				Bad("Incomplete entity field");
			e.kv[k] = t[i++].s;
		}
		if (i == t.size())
			Bad("Truncated entity");
		e.end = t[i++].end;
		all.push_back(std::move(e));
	}
	for (size_t n = 0; n < all.size(); ++n)
	{
		const auto &j = all[n];
		auto get = [&](const char *k) -> std::string {
			auto f = j.kv.find(k);
			return f == j.kv.end() ? "" : f->second;
		};
		std::string cls = Lower(get("classname"));
		if (cls != "light" && cls != "light_spot")
			continue;
		EmitterEvidence e;
		e.origin = EvidenceOrigin::BspEntity;
		e.shapes = CandidatePoint;
		e.issues = UncalibratedEmission | ColourTransferUnknown | ShapeHypothesis;
		e.source = in.map.path + "#entities";
		e.ordinal = uint32_t(n);
		e.entities.push_back(uint32_t(n));
		e.id = Identity(in.map, "entity:" + std::to_string(n));
		e.raw = in.entityText.substr(j.begin, j.end - j.begin);
		e.name = get("targetname");
		e.staticEligible = true;
		if (!get("origin").empty())
		{
			e.position = Vector(get("origin"));
			e.positionKnown = true;
		}
		else
			e.staticEligible = false;
		auto color = get("_color");
		if (color.empty())
			color = get("color");
		if (!color.empty())
		{
			e.colour = Vector(color);
			e.colourKnown = true;
		}
		auto light = get("light");
		if (!light.empty())
		{
			e.intensity = Number(light);
			e.intensityKnown = true;
			e.unit = EvidenceUnit::LegacyBakeTool;
		}
		if (!get("_light").empty())
		{
			auto v = Numbers(get("_light"));
			if (v.size() != 4)
				Bad("_light needs RGB intensity");
			e.colour = Vec3(v[0], v[1], v[2]);
			e.colourKnown = true;
			e.intensity = v[3];
			e.intensityKnown = true;
			e.unit = EvidenceUnit::LegacyBakeTool;
		}
		if (!get("style").empty())
		{
			float v = Number(get("style"));
			if (v < 0 || v > 255 || v != std::floor(v))
				Bad("Invalid light style");
			e.style = uint8_t(v);
			if (e.style)
			{
				e.issues |= AnimatedEvidence;
				e.staticEligible = false;
			}
		}
		if (!get("radius").empty())
			e.sourceRadiusHint = Number(get("radius"));

		auto cone = get("_cone");
		if (cone.empty())
			cone = get("cone");
		if (!cone.empty())
		{
			e.coneDegrees = Number(cone);
			e.coneKnown = true;
			if (e.coneDegrees < 0 || e.coneDegrees > 180)
				Bad("Invalid cone hint");
		}
		auto target = get("target");
		if (!target.empty() || cls == "light_spot")
		{
			e.shapes |= CandidateSpot;
			bool found = false;
			Vec3 dir;
			for (const auto &dest : all)
			{
				auto name = dest.kv.find("targetname"), pos = dest.kv.find("origin");
				if (!target.empty() && name != dest.kv.end() && name->second == target &&
					pos != dest.kv.end() && e.positionKnown)
				{
					Vec3 d = Sub(Vector(pos->second), e.position);
					if (!Unit(d))
						continue;
					if (found)
					{
						e.issues |= AmbiguousCorrespondence;
						found = false;
						break;
					}
					dir = d;
					found = true;
				}
			}
			if (found)
			{
				e.outward = dir;
				e.orientationKnown = true;
			}
			else
				e.issues |= MissingTarget;
		}
		if (!get("model").empty() || !get("parent").empty())
		{
			e.issues |= MovingEvidence;
			e.staticEligible = false;
		}
		if (!Colour(e.colour) || e.intensity < 0 || e.sourceRadiusHint < 0)
			Bad("Invalid light entity measurement");
		out.push_back(std::move(e));
	}
}
void ShaderEvidence(const EmitterEvidenceInput &in, std::vector<EmitterEvidence> &out,
					size_t maxTokens)
{
	std::map<std::string, std::string> unique;
	for (const auto &s : in.shaders)
	{
		auto result = unique.emplace(s.material, s.text);
		if (!result.second && result.first->second != s.text)
			Bad("Conflicting selected shader definitions");
	}
	for (const auto &s : unique)
	{
		auto t = Tokens(s.second, maxTokens);
		int depth = 0;
		uint32_t ordinal = 0;
		for (size_t i = 0; i < t.size(); ++i)
		{
			if (t[i].s == "{")
			{
				++depth;
				continue;
			}
			if (t[i].s == "}")
			{
				--depth;
				continue;
			}
			if (depth != 1)
				continue;
			auto key = Lower(t[i].s);
			bool sun = key == "sun" || key == "q3map_sun" || key == "q3map_sunext" ||
					   key == "q3gl2_sun",
				 sky = key == "q3map_skylight" || key == "skyparms";
			if (!sun && !sky)
				continue;
			EmitterEvidence e;
			e.origin = sun ? EvidenceOrigin::SunShader : EvidenceOrigin::SkyShader;
			e.shapes = sun ? CandidateSun : CandidateSky;
			e.issues = UncalibratedEmission | ColourTransferUnknown;
			e.material = s.first;
			e.source = s.first + "#shader";
			e.ordinal = ordinal++;
			e.id = Identity(in.map, "shader:" + s.first + ":" + std::to_string(e.ordinal));
			e.staticEligible = true;
			size_t begin = t[i].begin, end = t[i].end, line = t[i].line;
			if (sun)
			{
				if (i + 6 >= t.size())
					Bad("Incomplete sun declaration");
				float v[6];
				for (int n = 0; n < 6; ++n)
				{
					if (t[i + 1].line != line)
						Bad("Incomplete sun line");
					v[n] = Number(t[++i].s);
				}
				e.colour = Vec3(v[0], v[1], v[2]);
				e.colourKnown = true;
				e.intensity = v[3];
				e.intensityKnown = true;
				e.unit = EvidenceUnit::LegacyBakeTool;
				float az = v[4] * 3.14159265358979323846f / 180,
					  el = v[5] * 3.14159265358979323846f / 180;
				e.toSource =
					Vec3(std::cos(az) * std::cos(el), std::sin(az) * std::cos(el), std::sin(el));
				e.outward = Mul(e.toSource, -1);
				e.orientationKnown = true;
				if (!Colour(e.colour) || e.intensity < 0)
					Bad("Invalid sun parameters");
			}
			else if (key == "q3map_skylight")
			{
				if (i + 1 >= t.size() || t[i + 1].line != line)
					Bad("Incomplete skylight");
				e.intensity = Number(t[++i].s);
				e.intensityKnown = true;
				e.unit = EvidenceUnit::LegacyBakeTool;
				if (e.intensity < 0)
					Bad("Invalid skylight");
			}
			while (i + 1 < t.size() && t[i + 1].line == line && t[i + 1].s != "}" &&
				   t[i + 1].s != "{")
				end = t[++i].end;
			end = std::max(end, t[i].end);
			e.raw = s.second.substr(begin, end - begin);
			if (in.geometry)
				for (const auto &surface : in.geometry->Surfaces())
					if (surface.material == s.first)
						e.surfaces.push_back(surface.ordinal);
			out.push_back(std::move(e));
		}
	}
}

void SurfaceEvidence(const EmitterEvidenceInput &in, const EvidenceOptions &o,
					 const ReadContext &ctx, std::vector<EmitterEvidence> &out,
					 EvidenceReport &report)
{
	if (!in.geometry)
		return;
	std::map<uint32_t, const BakedSurface *> byOrdinal;
	std::vector<Emission::Triangle> tris;
	std::map<std::string, uint32_t> materials;
	uint64_t work = 0;
	for (const auto &s : in.geometry->Surfaces())
	{
		if (!byOrdinal.emplace(s.ordinal, &s).second)
			Bad("Duplicate source surface ordinal");
		const auto &f = s.materialFacts;
		if (!(f.signals & MaterialGlow) && !(f.legacySurfaceLight > 0))
			continue;
		if (f.signals & (MaterialSky | MaterialNoDraw))
			continue;
		if (ctx.cancellation && ctx.cancellation->IsCancelled())
			throw Failure(Code::Cancelled, "Emission extraction cancelled");
		bool supported =
			(s.kind == BakedSurfaceKind::Face || s.kind == BakedSurfaceKind::Triangles) &&
			!s.vertices.empty() && !s.indices.empty();
		if (!supported)
		{
			EmitterEvidence e;
			e.id = Identity(in.map, "unsupported-surface:" + std::to_string(s.ordinal));
			e.surfaces.push_back(s.ordinal);
			e.source = in.map.path;
			e.material = s.material;
			e.shapes = CandidateSurface;
			e.issues = UnsupportedGeometry | UncalibratedEmission;
			out.push_back(e);
			++report.unsupported;
			continue;
		}
		std::string materialKey = s.material + ":" + std::to_string(uint32_t(s.motion)) + ":" +
								  std::to_string(f.signals) + ":" + f.emissionTexture;
		auto inserted = materials.emplace(materialKey, uint32_t(materials.size()));
		if (s.indices.size() % 3)
			Bad("Incomplete emissive triangles");
		for (size_t n = 0; n < s.indices.size(); n += 3)
		{
			if (tris.size() >= o.maxTriangles ||
				(tris.size() + 1) * sizeof(Emission::Triangle) > o.maxBytes / 4)
				throw Failure(Code::MemoryLimit, "Emission triangle limit");
			Emission::Triangle t;
			t.material = inserted.first->second;
			t.surface = s.ordinal;
			for (unsigned k = 0; k < 3; ++k)
			{
				if (s.indices[n + k] >= s.vertices.size())
					Bad("Invalid emissive vertex reference");
				const auto &v = s.vertices[s.indices[n + k]];
				t.v[k][0] = v.position.x;
				t.v[k][1] = v.position.y;
				t.v[k][2] = v.position.z;
				t.st[k][0] = float(v.textureUv.u);
				t.st[k][1] = float(v.textureUv.v);
				for (float c : t.v[k])
					if (!Fin(c) || std::abs(c) > 1e8f)
						Bad("Invalid emissive position");
				for (float c : t.st[k])
					if (!Fin(c) || std::abs(c) > 1e6f)
						Bad("Invalid emissive UV");
			}
			Vec3 normal = Cross(Sub(V(t.v[1]), V(t.v[0])), Sub(V(t.v[2]), V(t.v[0])));
			t.area = .5f * Length(normal);
			if (!Unit(normal) || t.area <= .000001f)
			{
				++report.unsupported;
				continue;
			}
			t.normal[0] = normal.x;
			t.normal[1] = normal.y;
			t.normal[2] = normal.z;
			tris.push_back(t);
		}
	}
	report.triangles = tris.size();
	auto groups = Emission::Groups(tris, true);
	report.groups = groups.size();
	for (const auto &g : groups)
	{
		const auto &s = *byOrdinal.at(tris[g[0]].surface);
		const auto &f = s.materialFacts;
		float normal[3] = {};
		std::set<uint32_t> refs;
		for (int m : g)
		{
			for (int c = 0; c < 3; ++c)
				normal[c] += tris[m].area * tris[m].normal[c];
			refs.insert(tris[m].surface);
		}
		Vec3 nv = V(normal);
		if (!Unit(nv))
			continue;
		normal[0] = nv.x;
		normal[1] = nv.y;
		normal[2] = nv.z;
		Emission::Mask mask;
		const Emission::Mask *mp = nullptr;
		uint32_t issues = UncalibratedEmission | ShapeHypothesis | ColourTransferUnknown;
		bool coords = f.emissionCoordinatesKnown ? f.emissionStaticCoordinates
												 : !(f.uncertainty & TextureTransformPresent);
		if (in.masks)
		{
			EmissionMaskRequest request;
			request.material = s.material;
			request.texture = f.emissionTexture.empty() ? f.baseTexture : f.emissionTexture;
			Error err = in.masks->Read(request, ctx, mask);
			if (err)
			{
				if (uint64_t(mask.rgb.size()) * sizeof(float) > o.maxBytes / 4)
					throw Failure(Code::MemoryLimit, "Emission mask limit");
				for (float c : mask.rgb)
					if (!Fin(c) || c < 0)
						Bad("Invalid source emission mask");
				mask.clamp = f.emissionClamp;
				mp = &mask;
			}
			else if (err.code != Code::Unavailable)
				throw err;
		}
		if (!mp)
			issues |= MaskUnavailable;
		if (!coords)
			issues |= MaskCoordinatesUnsupported;
		if (f.signals & (MaterialAnimated | MaterialDeformed | MaterialEntityDependent))
			issues |= AnimatedEvidence;
		if (s.motion != SurfaceMotion::StaticWorld)
			issues |= MovingEvidence;
		if (f.emissionLayerCount > 1)
			issues |= MultipleEmissionLayers;
		if (f.signals & (MaterialCutout | MaterialTranslucent))
			issues |= AlphaEmissionUnknown;
		uint64_t samples = uint64_t(o.maskAxis) * o.maskAxis;
		if (g.size() > o.maxWork / samples || work > o.maxWork - samples * g.size())
			throw Failure(Code::WorkLimit, "Emission mask triangle work limit");
		work += samples * g.size();
		std::vector<Emission::Blob> blobs;
		auto err = Emission::Sample(tris, g, mp, coords, o.maskThreshold, o.maskAxis, ctx, blobs);
		if (!err)
			throw err;

		bool emptyMask = blobs.empty();
		if (emptyMask)
		{
			issues |= NoLuminousMaskSamples;
			err =
				Emission::Sample(tris, g, nullptr, false, o.maskThreshold, o.maskAxis, ctx, blobs);
			if (!err)
				throw err;
		}
		size_t blobOrdinal = 0;
		for (const auto &b : blobs)
		{
			if (b.points.empty())
				continue;
			std::vector<const float *> points;
			for (const auto &p : b.points)
				points.push_back(p.p);
			Emission::Rectangle r;
			Emission::FitRectangle(points, normal,
								   b.sampled ? .5f * std::sqrt(b.area / points.size()) : 0, r);
			EmitterEvidence e;
			e.origin = (f.signals & MaterialGlow) ? EvidenceOrigin::GlowSurface
												  : EvidenceOrigin::SurfaceLight;
			e.issues = issues;
			e.source = in.map.path;
			e.material = s.material;
			e.emissionTexture = f.emissionTexture;
			e.materialSignals = f.signals;
			e.surfaces.assign(refs.begin(), refs.end());
			e.positionKnown = e.orientationKnown = true;
			e.position = V(r.center);
			e.right = V(r.right);
			e.up = V(r.up);
			e.outward = nv;
			e.halfWidth = r.halfWidth;
			e.halfHeight = r.halfHeight;
			e.areaWorld = b.area;
			e.maskSampled = b.sampled;
			e.twoSided = f.cull == SurfaceCull::None;
			e.geometricCoverage =
				std::min(1.0f, b.area / std::max(1e-20f, 4 * r.halfWidth * r.halfHeight));
			e.shapes = CandidateRectangle;
			float major = 2 * std::max(r.halfWidth, r.halfHeight),
				  minor = 2 * std::min(r.halfWidth, r.halfHeight);
			if (major <= o.pointExtent)
				e.shapes |= CandidatePoint;
			if (major >= o.tubeAspect * std::max(minor, 1e-8f))
			{
				e.shapes |= CandidateTube;
				e.radius = minor * .5f;
			}
			if (e.geometricCoverage < .7f || major > 256)
				e.shapes |= CandidateSurface;
			e.colourKnown = mp || f.emissionColourKnown;
			if (mp)
			{
				e.colour =
					emptyMask ? V(mask.average)
							  : Vec3(b.power[0] / b.area, b.power[1] / b.area, b.power[2] / b.area);
				if (f.emissionColourKnown)
				{
					e.colour.x *= f.emissionColour.x;
					e.colour.y *= f.emissionColour.y;
					e.colour.z *= f.emissionColour.z;
				}
			}
			else
				e.colour = f.emissionColour;
			e.staticEligible = !(issues & (AnimatedEvidence | MovingEvidence));
			e.id = Identity(in.map, "surface:" + std::to_string(*refs.begin()) + ":" +
										std::to_string(g[0]) + ":" + std::to_string(blobOrdinal++));
			if (f.emissionProvenance == EmissionProvenance::AuthoredLuminance)
			{
				e.intensityKnown = true;
				e.intensity = f.authoredLuminance;
				e.unit = EvidenceUnit::LuminanceNits;
				e.issues |= AuthoredPrior;
				e.issues &= ~UncalibratedEmission;
			}
			if (!Colour(e.colour) || !Fin(e.intensity) || e.intensity < 0)
				Bad("Invalid material emission metadata");
			out.push_back(e);
			if (f.legacySurfaceLight > 0)
			{
				auto hint = e;
				hint.id = Identity(in.map, "surfacelight:" + std::to_string(e.id));
				hint.origin = EvidenceOrigin::SurfaceLight;
				hint.intensityKnown = true;
				hint.intensity = f.legacySurfaceLight;
				hint.unit = EvidenceUnit::LegacyBakeTool;
				hint.issues |= UncalibratedEmission;
				if (Colour(f.legacySourceColour) && Length(f.legacySourceColour) > 0)
				{
					hint.colour = f.legacySourceColour;
					hint.colourKnown = true;
					hint.issues |= ColourTransferUnknown;
				}
				out.push_back(std::move(hint));
			}
			if (b.sampled)
				++report.maskBlobs;
			if (out.size() > o.maxEvidence)
				throw Failure(Code::MemoryLimit, "Emission evidence count limit");
		}
	}
}
bool Intersects(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
	for (auto v : a)
		if (std::find(b.begin(), b.end(), v) != b.end())
			return true;
	return false;
}
bool Same(const EmitterEvidence &a, const EmitterEvidence &b)
{
	if (a.origin == EvidenceOrigin::AuthoredArea && b.origin == EvidenceOrigin::AuthoredArea)
		return false;
	if ((a.shapes | b.shapes) & (CandidateSun | CandidateSky))
		return false;
	if (a.origin == EvidenceOrigin::BspEntity && b.origin == EvidenceOrigin::BspEntity)
		return a.entities == b.entities;
	bool authored =
		a.origin == EvidenceOrigin::AuthoredArea || b.origin == EvidenceOrigin::AuthoredArea;
	if (authored && (Intersects(a.entities, b.entities) || Intersects(a.surfaces, b.surfaces)))
		return true;
	if (authored && a.origin != b.origin && a.positionKnown && b.positionKnown &&
		a.orientationKnown && b.orientationKnown && (a.shapes & CandidateRectangle) &&
		(b.shapes & CandidateRectangle))
	{
		Vec3 ar = a.right, au = a.up, br = b.right, bu = b.up;
		Unit(ar);
		Unit(au);
		Unit(br);
		Unit(bu);
		Vec3 d = Sub(a.position, b.position);
		bool aligned = std::abs(Dot(ar, br)) > .99999f && std::abs(Dot(au, bu)) > .99999f &&
					   std::abs(a.halfWidth - b.halfWidth) < .001f &&
					   std::abs(a.halfHeight - b.halfHeight) < .001f;
		bool swapped = std::abs(Dot(ar, bu)) > .99999f && std::abs(Dot(au, br)) > .99999f &&
					   std::abs(a.halfWidth - b.halfHeight) < .001f &&
					   std::abs(a.halfHeight - b.halfWidth) < .001f;
		if ((aligned || swapped) && std::abs(Dot(d, ar)) < .001f && std::abs(Dot(d, au)) < .001f &&
			std::abs(Dot(d, a.outward)) <= .251f)
			return true;
	}

	// Never proximity-only merge. Same support + same fitted blob (two lamps may share a surface).
	if (a.surfaces != b.surfaces || a.surfaces.empty() || !a.positionKnown || !b.positionKnown)
		return false;
	return Length(Sub(a.position, b.position)) < .001f &&
		   std::abs(a.halfWidth - b.halfWidth) < .001f &&
		   std::abs(a.halfHeight - b.halfHeight) < .001f &&
		   std::abs(Dot(a.outward, b.outward)) > .9999f;
}
} // namespace
const char *EvidenceOriginName(EvidenceOrigin o)
{
	switch (o)
	{
	case EvidenceOrigin::GlowSurface:
		return "glow";
	case EvidenceOrigin::SurfaceLight:
		return "surfacelight";
	case EvidenceOrigin::AuthoredArea:
		return "authored-area";
	case EvidenceOrigin::AutomaticArea:
		return "automatic-area";
	case EvidenceOrigin::BspEntity:
		return "bsp-light";
	case EvidenceOrigin::SunShader:
		return "sun-declaration";
	case EvidenceOrigin::SkyShader:
		return "sky-hint";
	}
	return "unknown";
}
Error ExtractEmitterEvidence(const EmitterEvidenceInput &in, const EvidenceOptions &o,
							 const ReadContext &ctx, std::shared_ptr<const EmitterEvidenceSet> &out)
{
	out.reset();
	try
	{
		if (ctx.cancellation && ctx.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Emitter extraction cancelled");
		uint64_t limit = std::min(o.maxBytes, ctx.maxBytes),
				 text = in.entityText.size() + in.areaJson.size();
		for (const auto &s : in.shaders)
			text += s.text.size() + s.material.size();
		if (!o.maxEvidence || !o.maxTriangles || o.maskAxis < 4 || o.maskAxis > 1024 ||
			!Fin(o.maskThreshold) || o.maskThreshold < 0 || !Fin(o.pointExtent) ||
			o.pointExtent < 0 || !Fin(o.tubeAspect) || o.tubeAspect < 1)
			return Failure(Code::InvalidInput, "Invalid evidence options");
		if (limit < sizeof(EmitterEvidenceSet))
			return Failure(Code::MemoryLimit, "Emitter object budget");
		if (text > o.maxTextBytes || text > limit / 32)
			return Failure(Code::MemoryLimit, "Emitter text/allocation limit");
		if (in.geometry &&
			(in.map.sha256 != in.geometry->Map().sha256 || in.map.path != in.geometry->Map().path))
			return Failure(Code::InvalidInput, "Geometry/map identity mismatch");
		std::unique_ptr<EmitterEvidenceSet> set(new EmitterEvidenceSet);
		set->map_ = in.map;
		JsonEvidence(in, set->evidence_, size_t(limit / 1024));
		EntityEvidence(in, set->evidence_, size_t(limit / 1024));
		ShaderEvidence(in, set->evidence_, size_t(limit / 1024));
		EvidenceOptions bounded = o;
		bounded.maxBytes = limit;
		SurfaceEvidence(in, bounded, ctx, set->evidence_, set->report_);
		std::sort(set->evidence_.begin(), set->evidence_.end(),
				  [](const EmitterEvidence &a, const EmitterEvidence &b) { return a.id < b.id; });
		uint64_t bytes = sizeof(EmitterEvidenceSet);
		std::set<Id> ids;
		for (auto &e : set->evidence_)
		{
			float raw[3] = {e.colour.x, e.colour.y, e.colour.z}, chroma[3];
			if (e.colourKnown && Rend2PBR::NormalizeChromaticity(raw, chroma))
			{
				e.chromaticityKnown = true;
				e.chromaticity = Vec3(chroma[0], chroma[1], chroma[2]);
			}

			for (const Vec3 *v : {&e.position, &e.right, &e.up, &e.outward, &e.toSource, &e.colour})
				if (!Fin(v->x) || !Fin(v->y) || !Fin(v->z))
					Bad("Non-finite emitter geometry/colour");
			for (float v : {e.halfWidth, e.halfHeight, e.radius, e.areaWorld, e.intensity, e.range,
							e.geometricCoverage})
				if (!Fin(v) || v < 0)
					Bad("Non-finite or negative emitter dimension/value");
			if (!ids.insert(e.id).second)
				Bad("Evidence ID collision");
			bytes += sizeof(e) + e.source.size() + e.material.size() + e.emissionTexture.size() +
					 e.name.size() + e.mode.size() + e.raw.size() +
					 4 * (e.surfaces.size() + e.entities.size());
			if (e.issues & AnimatedEvidence)
				++set->report_.animated;
			if (e.issues & MovingEvidence)
				++set->report_.moving;
			if (e.origin == EvidenceOrigin::AuthoredArea)
				++set->report_.authored;
			if (e.origin == EvidenceOrigin::BspEntity)
				++set->report_.entities;
			if (e.origin == EvidenceOrigin::SunShader)
				++set->report_.suns;
			if (e.origin == EvidenceOrigin::SkyShader)
				++set->report_.skies;
		}
		if (set->evidence_.size() > o.maxEvidence || bytes > limit / 2)
			return Failure(Code::MemoryLimit, "Emitter publication limit");
		// Index cross references; avoid quadratic scans over every unrelated candidate.
		std::map<uint32_t, std::vector<size_t>> surfaces, entities;

		std::vector<size_t> order;
		for (size_t i = 0; i < set->evidence_.size(); ++i)
			order.push_back(i);
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			return (set->evidence_[a].origin == EvidenceOrigin::AuthoredArea) <
				   (set->evidence_[b].origin == EvidenceOrigin::AuthoredArea);
		});
		using PositionKey = std::tuple<double, double, double>;
		std::map<PositionKey, std::vector<size_t>> positions;
		uint64_t dedupWork = 0;
		for (size_t n : order)
		{
			const auto &e = set->evidence_[n];
			std::set<size_t> possible;
			for (uint32_t id : e.surfaces)
				for (size_t c : surfaces[id])
					possible.insert(c);
			for (uint32_t id : e.entities)
				for (size_t c : entities[id])
					possible.insert(c);

			if (e.origin == EvidenceOrigin::AuthoredArea && e.positionKnown)
			{
				double x = std::floor(double(e.position.x)), y = std::floor(double(e.position.y)),
					   z = std::floor(double(e.position.z));
				for (int dz = -1; dz <= 1; ++dz)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							auto p = positions.find(PositionKey(x + dx, y + dy, z + dz));
							if (p != positions.end())
								for (size_t c : p->second)
									possible.insert(c);
						}
			}
			std::vector<size_t> matches;
			for (size_t c : possible)
			{
				if (e.origin == EvidenceOrigin::AuthoredArea &&
					set->evidence_[set->candidates_[c].primary].origin ==
						EvidenceOrigin::AuthoredArea)
					continue;
				for (size_t v : set->candidates_[c].evidence)
				{
					if (++dedupWork > o.maxWork)
						throw Failure(Code::WorkLimit, "Evidence deduplication work limit");
					if (Same(e, set->evidence_[v]))
					{
						matches.push_back(c);
						break;
					}
				}
			}
			// Ambiguous explicit references do not collapse several lamps into one.
			size_t c;
			if (matches.size() == 1)
				c = matches[0];
			else
			{
				c = set->candidates_.size();
				EmitterCandidate candidate;
				candidate.id = e.id;
				candidate.primary = n;
				set->candidates_.push_back(candidate);
			}
			auto &candidate = set->candidates_[c];
			candidate.shapes |= e.shapes;
			candidate.issues |= e.issues;
			if (matches.size() > 1)
				candidate.issues |= AmbiguousCorrespondence;
			candidate.staticEligible |= e.staticEligible;
			candidate.evidence.push_back(n);
			candidate.sourceIds.push_back(e.id);
			if (e.origin == EvidenceOrigin::AuthoredArea)
			{
				candidate.primary = n;
				candidate.id = e.id;
				candidate.staticEligible = e.staticEligible;
			}

			if (e.positionKnown)
				positions[PositionKey(std::floor(double(e.position.x)),
									  std::floor(double(e.position.y)),
									  std::floor(double(e.position.z)))]
					.push_back(c);
			for (uint32_t id : e.surfaces)
				surfaces[id].push_back(c);
			for (uint32_t id : e.entities)
				entities[id].push_back(c);
		}
		std::sort(set->candidates_.begin(), set->candidates_.end(),
				  [](const EmitterCandidate &a, const EmitterCandidate &b) { return a.id < b.id; });
		for (auto &c : set->candidates_)
		{
			if (set->evidence_[c.primary].origin == EvidenceOrigin::AuthoredArea)
				c.staticEligible = set->evidence_[c.primary].staticEligible;
			c.staticEligible =
				c.staticEligible &&
				!(c.issues & (MovingEvidence | AnimatedEvidence | DynamicAuthoredMode));
		}
		for (const auto &c : set->candidates_)
			bytes +=
				sizeof(c) + c.evidence.size() * sizeof(size_t) + c.sourceIds.size() * sizeof(Id);
		if (bytes > limit)
			return Failure(Code::MemoryLimit, "Candidate memory limit");
		set->report_.bytes = bytes;
		out = std::shared_ptr<const EmitterEvidenceSet>(set.release());
		return Error();
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Emitter allocation failed");
	}
	catch (const std::exception &e)
	{
		return Failure(Code::ProviderError, e.what());
	}
	catch (...)
	{
		return Failure(Code::ProviderError, "Unknown evidence provider exception");
	}
}
} // namespace Core
} // namespace Rend2LightRecon
