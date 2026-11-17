// Dependency-free CPU tests and TSV probe for licensed-asset audits. No game/GL.
#include "tr_surface_material.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace Rend2Material;
static unsigned checks;
static void Check(bool ok, const char *message) {
	++checks;
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static void Expect(const char *name, Type t, const char *diffuse = nullptr) {
	Input in; in.shaderName = name; in.diffuseName = diffuse;
	Check(Classify(in).type == t, name);
}
static void SelfTest() {
	Expect("textures/tatooine/sand01", Sand);
	Expect("textures/desert/stucco", Plaster);
	Expect("textures/factory/outside_rock1b", Rock);
	Expect("textures/yavin/grass_ground", Grass);
	Expect("textures/yavin/fern3b", Foliage);
	Expect("textures/rooftop/office_floor_carpet_trim", Carpet);
	Expect("textures/imperial/metalsec1", Metal);
	Expect("textures/imperial/basicwall", Unknown);
	Expect("textures/desert/sandstone", Rock);
	Expect("textures/common/dirtymetal", Unknown);
	Expect("textures/common/sandro", Unknown);
	Expect("textures/office/floor", Unknown);
	Expect("textures/common/grasshopper", Unknown);
	Expect("models/players/icebear/body", Unknown);
	Expect("textures/hoth/wall", Unknown);
	Expect("textures/common/BrickWall", Masonry);
	Expect("textures/yavin/stonewall2_vertex", Masonry);
	Expect("TEXTURES\\COMMON\\METAL_03.TGA", Metal);
	Expect("textures/unknown/body", Glass, "textures/common/glass01");
	Expect("textures/metal/wood_floor", Wood);
	Expect("textures/common/base", Metal, "textures/metal/wall");
	Expect("textures/yavin/mud", Mud);
	Expect("textures/test/gravel", Gravel);
	Expect("textures/test/concrete", Concrete);
	Expect("textures/test/tile", Tile);
	Expect("models/test/bark", Bark);
	Expect("textures/test/snow", Snow);
	Expect("textures/test/ice", Ice);
	Expect("models/test/robe", Fabric);
	Expect("models/test/leather", Leather);
	Expect("models/test/rubber", Rubber);
	Expect("models/test/armor", Plastic);
	Expect("models/test/skin", Skin);
	Expect("models/test/hair", Hair);
	Expect("textures/test/computer", Electronics);
	Expect("textures/test/soil", Soil);
	Input in;
	in.shaderName = "textures/common/sand";
	in.pbr = {0.95f, 0.4f, 1.0f, 0.0f, 100};
	Check(Classify(in).type == Metal && Classify(in).source == PBR, "PBR outranks names");
	in.material = Sand;
	Check(Classify(in).type == Sand && (Classify(in).flags & ConflictingPBR), "JA metadata wins, conflict retained");
	in.keyword = Water;
	Check(Classify(in).type == Water, "water semantics outrank substrate");
	in.keyword = Sky;
	Check(Classify(in).type == Sky, "sky semantics outrank substrate");
	in.overrideType = Concrete; in.hasOverride = true;
	Check(Classify(in).type == Concrete && Classify(in).confidence == 100, "override wins");
	in.overrideType = Unknown;
	Check(Classify(in).type == Unknown && Classify(in).source == Override, "explicit unknown suppresses guessing");
	in = Input(); in.material = Metal; in.pbr = {0.01f,0.5f,0.0f,1.0f,100};
	Check(Classify(in).type == PaintedMetal, "dielectric coat on tagged metal");
	in.material = Rock;
	Check(Classify(in).type == Rock, "rough dielectric does not imply concrete");
	in.material = Unknown; in.pbr = {0.5f,0.5f,0.5f,0.5f,100};
	Check(Classify(in).type == Unknown && (Classify(in).flags & MixedPBR), "mixed atlas stays unknown");
	in.pbr = {}; in.model = Skin;
	Check(Classify(in).type == Skin && Classify(in).confidence == 40, "model fallback");
	in.shaderName = "models/players/sand_creature/worm";
	Check(Classify(in).type == Skin, "sand creature is organic, not sand terrain");
	in.shaderName = "models/players/sand_creature/hair";
	Check(Classify(in).type == Hair, "explicit material basename outranks character");
	in.keyword = Fabric;
	Check(Classify(in).type == Fabric && Classify(in).source == Keyword, "cloth keyword");
	for (int i = 0; i < Count; ++i) {
		Type parsed = Unknown;
		Check(ParseType(TypeName((Type)i), parsed) && parsed == i, "type name roundtrip");
		for (int j = 0; j < i; ++j) {
			const float *a = DebugColor((Type)i), *b = DebugColor((Type)j);
			Check(a[0] != b[0] || a[1] != b[1] || a[2] != b[2], "unique debug colors");
		}
	}
	Type parsed;
	Check(!ParseType("nonsense", parsed), "reject invalid override");
	Check(SamplePacked(nullptr, 4, ORM).samples == 0, "null data");
	const uint8_t orm[] = {255,102,255,0, 255,204,0,255};
	const uint8_t rmo[] = {102,255,255,0, 204,0,255,255};
	const uint8_t moxr[] = {255,255,0,102, 0,255,255,204};
	for (int layout = ORM; layout <= MOXR; ++layout) {
		const PBRStats s = SamplePacked(layout == ORM ? orm : layout == RMO ? rmo : moxr, 2, (Layout)layout);
		Check(s.samples == 2 && std::abs(s.metalMean - 0.5f) < 0.001f &&
			std::abs(s.roughnessMean - 0.6f) < 0.001f && s.metalCoverage == 0.5f &&
			s.dielectricCoverage == 0.5f, "ORM/RMO/MOXR channel decoding");
	}
	std::vector<uint8_t> pixels(32768 * 4, 255);
	for (size_t i = 0; i < 32768; ++i) pixels[i * 4 + 2] = (i & 1) ? 255 : 0;
	const PBRStats s = SamplePacked(pixels.data(), 32768, ORM);
	Check(s.samples == 4096 && std::abs(s.metalMean - 0.5f) < 0.04f, "bounded jittered sampling avoids stripe aliasing");
	Check(SamplePacked(pixels.data(), 0, ORM).samples == 0, "zero pixels");
	std::cout << "Passed " << checks << " material classification checks\n";
}
int main(int argc, char **) {
	if (argc == 1) { SelfTest(); return 0; }
	// TSV: shader, diffuse, semantic type, keyword type, mean metal, rough,
	// metal coverage, dielectric coverage, samples. Empty types = unknown.
	std::string line;
	while (std::getline(std::cin, line)) {
		std::istringstream stream(line); std::vector<std::string> cols; std::string col;
		while (std::getline(stream, col, '\t')) cols.push_back(col);
		if (cols.size() != 9) { std::cerr << "Expected nine TSV columns\n"; return 2; }
		Input in; in.shaderName = cols[0].c_str(); in.diffuseName = cols[1].c_str();
		if (!cols[2].empty() && !ParseType(cols[2].c_str(), in.material)) return 3;
		if (!cols[3].empty() && !ParseType(cols[3].c_str(), in.keyword)) return 3;
		in.pbr = {std::stof(cols[4]), std::stof(cols[5]), std::stof(cols[6]), std::stof(cols[7]), (uint32_t)std::stoul(cols[8])};
		const Result r = Classify(in);
		std::cout << cols[0] << '\t' << TypeName(r.type) << '\t' << SourceName(r.source) << '\t'
			<< (int)r.confidence << '\t' << (int)r.flags << '\t' << r.reason << '\n';
	}
}
