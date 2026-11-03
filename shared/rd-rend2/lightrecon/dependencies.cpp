// SPDX-License-Identifier: GPL-2.0-or-later
#include "dependencies.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>
#include <stdexcept>
namespace Rend2LightRecon
{
namespace Disk
{
namespace Dependencies
{
namespace
{
Error Fail(Code c, const std::string &m)
{
	Error e;
	e.code = c;
	e.message = m;
	return e;
}
void Need(bool b, Code c, const std::string &m)
{
	if (!b)
		throw Fail(c, m);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
InputContent Text(InputKind kind, const std::string &name, const std::string &s)
{
	InputContent c;
	c.kind = kind;
	c.name = name;
	c.bytes = s.size();
	c.sha256 = Hash(s.data(), s.size());
	return c;
}
bool Safe(const std::string &s)
{
	return !s.empty() && s.size() < 1024 && s[0] != '/' && s.find('\\') == std::string::npos &&
		   s.find(':') == std::string::npos && s.find("..") == std::string::npos &&
		   s.find('\0') == std::string::npos;
}
std::string Lower(std::string s)
{
	for (auto &c : s)
		if (c >= 'A' && c <= 'Z')
			c += 32;
	return s;
}
// Shader definitions are already selected by the host. Tokenizing all resource
// paths conservatively covers colour/alpha/emission masks without registering
// renderer shaders or decoding images. No unrelated shader files are hashed.
std::vector<std::string> Tokens(const std::string &s)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size())
	{
		if (s[i] == '\n')
		{
			out.push_back("\n");
			++i;
			continue;
		}
		if (s[i] <= ' ' || s[i] == '{' || s[i] == '}')
		{
			++i;
			continue;
		}
		if (s.compare(i, 2, "//") == 0)
		{
			i = s.find('\n', i);
			if (i == std::string::npos)
				break;
			continue;
		}
		if (s.compare(i, 2, "/*") == 0)
		{
			i = s.find("*/", i + 2);
			Need(i != std::string::npos, Code::NotCacheable, "Unclosed material comment");
			i += 2;
			continue;
		}
		bool quote = s[i] == '"';
		if (quote)
			++i;
		size_t b = i;
		while (i < s.size() && (quote ? s[i] != '"' : (s[i] > ' ' && s[i] != '{' && s[i] != '}')))
			++i;
		Need(!quote || i < s.size(), Code::NotCacheable, "Unclosed material string");
		out.push_back(s.substr(b, i - b));
		if (quote)
			++i;
	}
	return out;
}
} // namespace
Error Collect(const std::string &map, const Reader &bsp, const Sources &source,
			  const Recipe &recipe, Fingerprint &out, Work &work, const Budget &budget)
{
	work = {};
	auto start = std::chrono::steady_clock::now();
	try
	{
		Need(Safe(map) && map.size() > 9 && map.compare(0, 5, "maps/") == 0 &&
				 map.substr(map.size() - 4) == ".bsp",
			 Code::InvalidArgument, "Invalid BSP VFS identity");
		for (auto s : {recipe.materialContext, recipe.decoder, recipe.geometry, recipe.profiles,
					   recipe.solver, recipe.settings, recipe.calibration})
			Need(!s.empty() && s.size() <= 65536, Code::NotCacheable,
				 "Incomplete reconstruction recipe");
		Need(bsp.Size() >= 152 && bsp.Size() <= budget.maxBspBytes &&
				 bsp.Size() <= budget.maxContentBytes,
			 Code::LimitExceeded, "BSP fingerprint budget");
		uint8_t header[152];
		Check(bsp.Read(0, header, sizeof(header)));
		auto u = [](const uint8_t *p) {
			return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
				   uint32_t(p[3]) << 24;
		};
		Need(!std::memcmp(header, "RBSP", 4) && u(header + 4) == 1, Code::Unsupported,
			 "Unsupported source BSP");
		struct Lump
		{
			uint32_t at, bytes;
		};
		Lump lumps[18];
		for (unsigned j = 0; j < 18; ++j)
		{
			lumps[j] = {u(header + 8 + j * 8), u(header + 12 + j * 8)};
			Need(lumps[j].at <= bsp.Size() && lumps[j].bytes <= bsp.Size() - lumps[j].at,
				 Code::Corrupt, "Invalid source lump bounds");
		}
		// Runtime shader remapping changes the selected optical/source context.
		// Until a pinned remap resolver is supplied, do not claim complete coverage.
		Need(lumps[0].bytes <= (4u << 20), Code::LimitExceeded, "Entity dependency bound");
		std::string entities(lumps[0].bytes, '\0');
		if (!entities.empty())
			Check(bsp.Read(lumps[0].at, &entities[0], entities.size()));
		Need(Lower(entities).find("remapshader") == std::string::npos, Code::NotCacheable,
			 "World material remapping has no pinned dependency resolver");
		Fingerprint f;
		f.algorithm = "org.openjk.renderer-reconstruction-input";
		f.algorithmVersion = kVersion;
		f.units = recipe.units;
		InputContent root;
		root.kind = InputKind::Bsp;
		root.name = map;
		root.bytes = bsp.Size();
		Check(HashContent(bsp, root.sha256, budget.maxBspBytes));
		f.inputs.push_back(root);
		work.hashedBytes = bsp.Size();
		auto addText = [&](InputKind k, const std::string &n, const std::string &s) {
			f.inputs.push_back(Text(k, n, s));
		};
		addText(InputKind::Settings, "material-context", recipe.materialContext);
		addText(InputKind::Settings, "decoder", recipe.decoder);
		addText(InputKind::Settings, "geometry", recipe.geometry);
		addText(InputKind::Settings, "profiles", recipe.profiles);
		addText(InputKind::Settings, "solver", recipe.solver);
		addText(InputKind::Settings, "settings", recipe.settings);
		addText(InputKind::UnitCalibration, "calibration", recipe.calibration);
		std::set<std::pair<InputKind, std::string>> seen;
		auto file = [&](InputKind kind, const std::string &path) {
			Need(Safe(path), Code::NotCacheable, "Unsupported source resource path");
			if (!seen.emplace(kind, path).second)
				return;
			Need(++work.files <= budget.maxFiles, Code::LimitExceeded,
				 "Dependency file-count budget");
			std::unique_ptr<Reader> r;
			auto e = source.Open(path, r);
			if (e.code == Code::Missing)
			{
				++work.missing;
				addText(kind, "absent:" + path, "absent/v1");
				return;
			}
			Check(e);
			Need(bool(r), Code::IoError, "Source reader absent");
			Need(r->Size() <= budget.maxFileBytes &&
					 r->Size() <= budget.maxContentBytes - work.hashedBytes,
				 Code::LimitExceeded, "Dependency byte budget");
			InputContent c;
			c.kind = kind;
			c.name = path;
			c.bytes = r->Size();
			Check(HashContent(*r, c.sha256, budget.maxFileBytes));
			work.hashedBytes += c.bytes;
			f.inputs.push_back(c);
		};
		auto image = [&](std::string path) {
			if (path.empty() || path[0] == '$' || path[0] == '*')
				return;
			Need(Safe(path), Code::NotCacheable, "Unsupported material image path");
			auto dot = path.find_last_of('.'), slash = path.find_last_of('/');
			if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
				path.resize(dot);
			for (auto ext : {".jpg", ".png", ".tga"})
				file(InputKind::Materials, path + ext);
		};
		Need(lumps[1].bytes % 72 == 0 && lumps[1].bytes / 72 <= budget.maxMaterials, Code::Corrupt,
			 "Invalid BSP shader inventory");
		std::set<std::string> materials;
		for (uint32_t j = 0; j < lumps[1].bytes / 72; ++j)
		{
			char name[64];
			Check(bsp.Read(lumps[1].at + j * 72, name, 64));
			auto end = std::find(name, name + 64, '\0');
			Need(end != name + 64, Code::Corrupt, "Unterminated BSP material");
			materials.insert(Lower(std::string(name, end)));
		}
		for (const auto &name : materials)
		{
			Need(Safe(name), Code::Corrupt, "Invalid BSP material path");
			std::string text;
			auto e = source.Material(name, text);
			Need(e.code == Code::Ok || e.code == Code::Missing, e.code, e.message);
			Need(text.size() <= 65536, Code::NotCacheable, "Material definition bound");
			++work.materials;
			Need(text.size() <= budget.maxContentBytes - work.hashedBytes, Code::LimitExceeded,
				 "Material content budget");
			work.hashedBytes += text.size();
			addText(InputKind::Materials, "definition:" + name,
					(e.code == Code::Missing ? "implicit/v1\n" : "explicit/v1\n") + text);
			image(name);
			auto tokens = Tokens(text);
			for (size_t j = 0; j < tokens.size(); ++j)
			{
				auto token = Lower(tokens[j]);
				Need(token != "videomap", Code::NotCacheable,
					 "Video material has no immutable source image");
				if ((token == "animmap" || token == "clampanimmap" || token == "oneshotanimmap") &&
					j + 2 < tokens.size())
					for (size_t k = j + 2; k < tokens.size() && tokens[k] != "\n"; ++k)
						image(tokens[k]);
				Need(token != "q3map_backshader" && token != "q3map_cloneshader" &&
						 token != "q3map_baseshader",
					 Code::NotCacheable,
					 "Dependent compiler material requires an explicit resolver");
				if (token == "skyparms" && j + 1 < tokens.size() && tokens[j + 1] != "-" &&
					tokens[j + 1] != "_")
					for (auto suffix : {"_rt", "_lf", "_bk", "_ft", "_up", "_dn"})
						image(tokens[j + 1] + suffix);
				// Source colour/alpha/emission inputs only. A normal/specular/RMO
				// texture's content cannot change this source-definition recipe.
				// Its declaration still belongs to the hashed effective shader text.
				if ((token == "map" || token == "clampmap" || token == "q3map_lightimage" ||
						  token == "emissivemap") &&
						 j + 1 < tokens.size())
					image(tokens[j + 1]);
			}
		}
		constexpr unsigned surfaces = 13, lightmaps = 14; // JA RBSP1 (fog is lump 12)
		Need(lumps[lightmaps].bytes % (128 * 128 * 3) == 0 && lumps[surfaces].bytes % 148 == 0,
			 Code::Corrupt, "Invalid BSP lightmap/surface inventory");
		uint32_t pages = lumps[lightmaps].bytes / (128 * 128 * 3);
		bool oddReference = false;
		for (uint32_t j = 0; j < lumps[surfaces].bytes / 148; ++j)
		{
			uint8_t nums[16];
			Check(bsp.Read(lumps[surfaces].at + j * 148 + 36, nums, 16));
			for (unsigned k = 0; k < 4; ++k)
			{
				auto n = u(nums + k * 4);
				if (n < 0x80000000u)
				{
					oddReference = oddReference || ((n & 1) != 0);
					Need(n < budget.maxLightmaps, Code::LimitExceeded, "Lightmap index limit");
					pages = std::max(pages, n + 1);
				}
			}
		}
		if (!lumps[lightmaps].bytes && pages > 1 && !oddReference)
			++pages;
		Need(pages <= budget.maxLightmaps, Code::LimitExceeded, "Lightmap dependency budget");
		// Matches the engine's COM_SkipPath(baseName), including MP maps.
		std::string base = "maps/" + map.substr(map.find_last_of('/') + 1);
		base.resize(base.size() - 4);
		for (uint32_t j = 0; j < pages; ++j)
		{
			auto n = std::to_string(j);
			n = std::string(n.size() < 4 ? 4 - n.size() : 0, '0') + n;
			file(InputKind::ExternalLightmaps, base + "/lm_" + n + ".hdr");
			file(InputKind::ExternalLightmaps, base + "/lm_" + n + ".tga");
			file(InputKind::ExternalLightmaps, base + "/lm_" + n + ".jpg");
			file(InputKind::ExternalLightmaps, base + "/lm_" + n + ".png");
			file(InputKind::DeluxeMaps, base + "/dm_" + n + ".tga");
			file(InputKind::DeluxeMaps, base + "/dm_" + n + ".jpg");
			file(InputKind::DeluxeMaps, base + "/dm_" + n + ".png");
		}
		file(InputKind::Lightgrid, base + "/lightgrid.raw");
		file(InputKind::ExternalLightmaps, base + "/vertlight.raw");
		file(InputKind::AuthoredLighting, base + ".arealights.json");
		f.complete.fill(true);
		Digest key;
		Check(MakeKey(f, key));
		out = std::move(f);
		work.seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		return {};
	}
	catch (const Error &e)
	{
		work.seconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Dependency manifest allocation");
	}
}
Error Derive(const Fingerprint &source, const LayerRecipe &r, Fingerprint &out)
{
	try
	{
		Digest parent;
		Check(MakeKey(source, parent));
		Need(r.version && !r.algorithm.empty() && !r.settings.empty(), Code::NotCacheable,
			 "Incomplete layer recipe");
		const char *names[] = {"probes", "field", "visibility", "contributions", "compression"};
		Need(unsigned(r.layer) < 5, Code::InvalidArgument, "Unknown cache layer");
		std::set<std::string> required;
		switch (r.layer)
		{
		case Layer::Probes:
			required = {"geometry", "optics"};
			break;
		case Layer::Visibility:
			required = {"geometry", "optics", "source-geometry", "scenario"};
			break;
		case Layer::Field:
			required = {"canonical-result", "probes"};
			break;
		case Layer::Contributions:
			required = {"canonical-result", "observations"};
			break;
		case Layer::Compression:
			required = {"uncompressed"};
			break;
		}
		Fingerprint f;
		f.algorithm =
			"org.openjk.derived/" + std::string(names[unsigned(r.layer)]) + "/" + r.algorithm;
		f.algorithmVersion = r.version;
		f.units = source.units;
		f.complete.fill(true);
		for (const auto &c : source.inputs)
			if (c.kind == InputKind::Bsp || c.kind == InputKind::Materials || c.kind == InputKind::UnitCalibration ||
				c.name == "material-context")
				f.inputs.push_back(c);
		f.inputs.push_back(Text(InputKind::Settings, "layer-settings", r.settings));
		std::set<std::string> seen;
		for (auto c : r.dependencies)
		{
			Need(!c.name.empty() && seen.insert(c.name).second && c.sha256 != Digest{},
				 Code::NotCacheable, "Missing/duplicate layer dependency hash");
			required.erase(c.name);
			c.kind = InputKind::Settings;
			c.name = "dependency:" + c.name;
			f.inputs.push_back(c);
		}
		Need(required.empty(), Code::NotCacheable, "Missing mandatory layer dependency");
		Digest key;
		Check(MakeKey(f, key));
		out = std::move(f);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Layer key allocation");
	}
}
} // namespace Dependencies
} // namespace Disk
} // namespace Rend2LightRecon
