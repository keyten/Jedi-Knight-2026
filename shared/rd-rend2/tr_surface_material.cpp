#include "tr_surface_material.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Rend2Material {
struct Definition { const char *name; float color[3]; };
static const Definition definitions[] = {
	{"unknown", {0.32f,0.32f,0.32f}}, {"sand", {1.00f,0.82f,0.36f}},
	{"soil", {0.38f,0.20f,0.07f}}, {"mud", {0.25f,0.25f,0.06f}},
	{"gravel", {0.65f,0.50f,0.35f}}, {"rock", {0.48f,0.40f,0.65f}},
	{"concrete", {0.72f,0.75f,0.80f}}, {"plaster", {1.00f,0.90f,0.80f}},
	{"masonry", {0.72f,0.20f,0.10f}}, {"tile", {0.10f,0.72f,0.70f}},
	{"metal", {1.00f,0.95f,0.05f}}, {"painted_metal", {1.00f,0.48f,0.05f}},
	{"wood", {0.62f,0.32f,0.08f}}, {"bark", {0.35f,0.13f,0.12f}},
	{"grass", {0.30f,1.00f,0.10f}}, {"foliage", {0.02f,0.40f,0.08f}},
	{"glass", {0.40f,0.90f,1.00f}}, {"water", {0.02f,0.20f,1.00f}},
	{"snow", {1.00f,1.00f,1.00f}}, {"ice", {0.20f,0.55f,0.80f}},
	{"fabric", {0.85f,0.10f,0.40f}}, {"carpet", {0.55f,0.05f,0.22f}},
	{"leather", {0.75f,0.48f,0.32f}}, {"rubber", {0.18f,0.20f,0.30f}},
	{"plastic", {0.80f,0.25f,1.00f}}, {"skin", {1.00f,0.55f,0.58f}},
	{"hair", {0.40f,0.10f,0.75f}}, {"electronics", {0.05f,1.00f,0.70f}},
	{"sky", {0.45f,0.65f,1.00f}}, {"effect", {1.00f,0.00f,0.90f}}
};
static_assert(sizeof(definitions) / sizeof(definitions[0]) == Count, "material table");
const char *TypeName(Type t) { return definitions[t < Count ? t : Unknown].name; }
const float *DebugColor(Type t) { return definitions[t < Count ? t : Unknown].color; }
const char *SourceName(Source s) {
	static const char *names[] = {"unresolved", "override", "JA-material", "keyword", "PBR", "name", "model"};
	return s <= Model ? names[s] : "invalid";
}
static std::string Lower(const char *s) {
	std::string out;
	for (const char *p = s ? s : ""; *p; ++p) {
		const unsigned char c = (unsigned char)*p;
		if (p != s && std::islower((unsigned char)p[-1]) && std::isupper(c)) out += '_';
		out += c == '\\' ? '/' : (char)std::tolower(c);
	}
	return out;
}
bool ParseType(const char *s, Type &out) {
	const std::string name = Lower(s);
	for (int i = 0; i < Count; ++i) if (name == definitions[i].name) { out = (Type)i; return true; }
	return false;
}
PBRStats SamplePacked(const uint8_t *rgba, size_t pixels, Layout layout) {
	PBRStats out = {};
	if (!rgba || !pixels) return out;
	out.samples = (uint32_t)std::min(pixels, size_t(4096));
	const int m = layout == RMO ? 1 : layout == MOXR ? 0 : 2;
	const int r = layout == RMO ? 0 : layout == MOXR ? 3 : 1;
	for (uint32_t i = 0; i < out.samples; ++i) {
		// Jittered strata avoid a regular stride aliasing repeated texture rows.
		const size_t begin = (size_t)((uint64_t(i) * pixels) / out.samples);
		const size_t end = (size_t)((uint64_t(i + 1) * pixels) / out.samples);
		uint32_t hash = i + 0x9e3779b9u;
		hash = (hash ^ (hash >> 16)) * 0x85ebca6bu;
		hash = (hash ^ (hash >> 13)) * 0xc2b2ae35u;
		hash ^= hash >> 16;
		const size_t p = begin + hash % (end - begin);
		const float metal = rgba[p * 4 + m] / 255.0f;
		out.metalMean += metal;
		out.roughnessMean += rgba[p * 4 + r] / 255.0f;
		out.metalCoverage += metal >= 0.8f ? 1.0f : 0.0f;
		out.dielectricCoverage += metal <= 0.2f ? 1.0f : 0.0f;
	}
	out.metalMean /= out.samples; out.roughnessMean /= out.samples;
	out.metalCoverage /= out.samples; out.dielectricCoverage /= out.samples;
	return out;
}

struct Word { const char *word; Type type; };
static const Word words[] = {
	{"sandstone",Rock}, {"sand",Sand}, {"dune",Sand}, {"dunes",Sand},
	{"soil",Soil}, {"dirt",Soil}, {"earth",Soil}, {"mud",Mud}, {"swamp",Mud},
	{"gravel",Gravel}, {"pebbles",Gravel}, {"rubble",Gravel},
	{"rock",Rock}, {"rocks",Rock}, {"stone",Rock}, {"marble",Rock}, {"granite",Rock}, {"cliff",Rock},
	{"concrete",Concrete}, {"cement",Concrete}, {"plaster",Plaster}, {"stucco",Plaster},
	{"brick",Masonry}, {"bricks",Masonry}, {"masonry",Masonry}, {"tile",Tile}, {"tiles",Tile}, {"ceramic",Tile},
	{"metal",Metal}, {"metl",Metal}, {"steel",Metal}, {"iron",Metal}, {"chrome",Metal},
	{"grate",Metal}, {"grating",Metal}, {"pipe",Metal}, {"pipes",Metal}, {"copper",Metal}, {"brass",Metal},
	{"bronze",Metal}, {"gold",Metal}, {"silver",Metal}, {"rust",Metal}, {"rusty",Metal},
	{"wood",Wood}, {"wooden",Wood}, {"timber",Wood}, {"plank",Wood}, {"planks",Wood},
	{"bark",Bark}, {"trunk",Bark}, {"grass",Grass}, {"grassy",Grass}, {"lawn",Grass},
	{"leaf",Foliage}, {"leaves",Foliage}, {"foliage",Foliage}, {"fern",Foliage},
	{"plant",Foliage}, {"plants",Foliage}, {"vine",Foliage}, {"vines",Foliage}, {"bush",Foliage},
	{"glass",Glass}, {"water",Water}, {"snow",Snow}, {"ice",Ice}, {"icicle",Ice},
	{"cloth",Fabric}, {"fabric",Fabric}, {"robe",Fabric}, {"robes",Fabric}, {"cape",Fabric},
	{"cloak",Fabric}, {"tunic",Fabric}, {"banner",Fabric}, {"flag",Fabric}, {"curtain",Fabric},
	{"carpet",Carpet}, {"rug",Carpet}, {"leather",Leather}, {"holster",Leather},
	{"rubber",Rubber}, {"tire",Rubber}, {"hose",Rubber}, {"plastic",Plastic},
	{"armor",Plastic}, {"armour",Plastic}, {"helmet",Plastic},
	{"skin",Skin}, {"flesh",Skin}, {"hair",Hair}, {"hairs",Hair}, {"fur",Hair}, {"beard",Hair},
	{"computer",Electronics}, {"circuit",Electronics}, {"monitor",Electronics}
};
// Stock JA concatenations. Deliberately bounded: no substring matches like
// "dirt" in "dirtymetal", "sand" in "sandro", or "ice" in "office".
static const Word compounds[] = {
	{"sandfloor",Sand}, {"sandground",Sand}, {"sanddune",Sand},
	{"rockwall",Rock}, {"rockfloor",Rock}, {"rockground",Rock}, {"stonewall",Masonry},
	{"grassground",Grass}, {"grassfloor",Grass},
	{"metalsec",Metal}, {"metalpanel",Metal}, {"metalfloor",Metal}, {"metalwall",Metal},
	{"basemetal",Metal}, {"rustmetal",Metal}, {"rustyplate",Metal},
	{"woodfloor",Wood}, {"woodpanel",Wood}, {"snowground",Snow}, {"icewall",Ice}
};
static Type Match(const std::string &s, char *reason) {
	std::vector<std::string> tokens;
	std::string token;
	for (size_t i = 0; i <= s.size(); ++i) {
		const unsigned char c = i < s.size() ? (unsigned char)s[i] : 0;
		if (!std::isalpha(c)) { if (!token.empty()) tokens.push_back(token); token.clear(); }
		else token += (char)c;
	}
	for (const std::string &t : tokens) {
		for (const Word &w : words) if (t == w.word) {
			std::snprintf(reason, 32, "%s", w.word); return w.type;
		}
		for (const Word &w : compounds) if (t == w.word) {
			std::snprintf(reason, 32, "%s", w.word); return w.type;
		}
	}
	return Unknown;
}
static Type MatchNames(const Input &in, char *reason, bool directories) {
	const std::string names[] = {Lower(in.shaderName), Lower(in.diffuseName)};
	// Basenames first, then directories. Generic map families (hoth/yavin/
	// imperial), floor/wall, and colour words carry no semantic evidence.
	for (const std::string &s : names) {
		const size_t slash = s.find_last_of('/');
		if (directories && slash == std::string::npos) continue;
		const Type t = Match(directories ? s.substr(0, slash) :
			s.substr(slash == std::string::npos ? 0 : slash + 1), reason);
		if (t != Unknown) return t;
	}
	return Unknown;
}
Result Classify(const Input &in) {
	Result out = {}; out.pbr = in.pbr;
	const bool metal = in.pbr.samples && in.pbr.metalMean >= 0.65f && in.pbr.metalCoverage >= 0.65f;
	const bool dielectric = in.pbr.samples && in.pbr.dielectricCoverage >= 0.85f && in.pbr.metalMean <= 0.15f;
	if (in.pbr.samples && in.pbr.metalCoverage >= 0.2f && in.pbr.dielectricCoverage >= 0.2f)
		out.flags |= MixedPBR;
	if (in.hasOverride) { out.type = in.overrideType; out.source = Override; out.confidence = 100; }
	else if (in.keyword == Sky || in.keyword == Water || in.keyword == Effect) {
		out.type = in.keyword; out.source = Keyword; out.confidence = 100;
	}
	else if (in.material != Unknown) { out.type = in.material; out.source = ShaderMaterial; out.confidence = 95; }
	else if (in.keyword != Unknown) { out.type = in.keyword; out.source = Keyword; out.confidence = 90; }
	else if (metal) { out.type = Metal; out.source = PBR; out.confidence = 85; }
	else {
		out.type = MatchNames(in, out.reason, false);
		if (out.type != Unknown) { out.source = Name; out.confidence = 60; }
		else if (in.model != Unknown) { out.type = in.model; out.source = Model; out.confidence = 40; }
		else {
			out.type = MatchNames(in, out.reason, true);
			if (out.type != Unknown) { out.source = Name; out.confidence = 50; }
		}
	}
	// JA's Metal tag describes a substrate. A mostly dielectric PBR coating
	// is useful for future weathering, but is not evidence of bare metal.
	if (out.type == Metal && dielectric && out.source != Override) {
		out.type = PaintedMetal; out.confidence = std::min<uint8_t>(out.confidence, 75);
		std::snprintf(out.reason, sizeof(out.reason), "%s+dielectric", SourceName(out.source));
	}
	if (metal && out.type != Metal && out.type != Unknown && out.type != Electronics && out.type != Effect && out.type != Sky)
		out.flags |= ConflictingPBR;
	if (!out.reason[0]) std::snprintf(out.reason, sizeof(out.reason), "%s", SourceName(out.source));
	return out;
}
}
