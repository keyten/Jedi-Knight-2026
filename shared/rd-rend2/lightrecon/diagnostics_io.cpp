// Bounded diagnostic interchange; not a lighting-data decoder.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_math.h"
#include "diagnostics.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Diagnostics
{
namespace
{
template <class T> bool Unsigned(std::istream &in, T &out)
{
	std::string token;
	if (!(in >> token) || token.empty())
		return false;
	uint64_t value = 0, maximum = std::numeric_limits<T>::max();
	for (unsigned char c : token)
	{
		if (c < '0' || c > '9' || value > (maximum - (c - '0')) / 10)
			return false;
		value = value * 10 + c - '0';
	}
	out = T(value);
	return true;
}
Error Bad(const char *m)
{
	return Failure(Code::InvalidInput, m);
}
std::string HexText(const std::string &s)
{
	if (s.empty())
		return "-";
	std::string out;
	out.reserve(2 * s.size());
	for (unsigned char c : s)
	{
		out += "0123456789abcdef"[c >> 4];
		out += "0123456789abcdef"[c & 15];
	}
	return out;
}
int Digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
bool Unhex(const std::string &s, std::string &out, size_t bound)
{
	out.clear();
	if (s == "-")
		return true;
	if (s.size() % 2 || s.size() / 2 > bound)
		return false;
	for (size_t j = 0; j < s.size(); j += 2)
	{
		int a = Digit(s[j]), b = Digit(s[j + 1]);
		if (a < 0 || b < 0)
			return false;
		out += char(a * 16 + b);
	}
	return true;
}
bool Digest(const std::string &s, Disk::Digest &d)
{
	std::string bytes;
	if (s.size() != 64 || !Unhex(s, bytes, 32))
		return false;
	std::copy(bytes.begin(), bytes.end(), d.begin());
	return true;
}
bool Finite(const Record &r)
{
	using Bake::Detail::Finite;
	return Finite(r.a) && Finite(r.b) && Finite(r.c) && Finite(r.value) && Finite(r.lower) &&
		   Finite(r.upper);
}
bool Valid(const Scene &s, const Limits &l)
{
	if (s.records.size() > l.maxRecords || !std::isfinite(s.units.worldUnitsPerMeter) ||
		s.units.worldUnitsPerMeter <= 0 || !std::isfinite(s.units.physicalToScene) ||
		s.units.physicalToScene <= 0)
		return false;
	uint64_t bytes = sizeof(s);
	for (const auto &str : {s.mapPath, s.algorithm, s.settings, s.scope})
	{
		if (str.size() > l.maxString)
			return false;
		bytes += str.size();
	}
	for (const auto &r : s.records)
	{
		if (!r.id || !Finite(r) || unsigned(r.layer) >= unsigned(Layer::Count) ||
			unsigned(r.state) >= unsigned(State::Count) || unsigned(r.primitive) > 2 ||
			unsigned(r.unit) > unsigned(PhotometricUnit::Lux))
			return false;
		bytes += sizeof(r);
		for (const auto &str : {r.quantity, r.domain, r.profile, r.reason, r.provenance})
		{
			if (str.size() > l.maxString)
				return false;
			bytes += str.size();
		}
		if (bytes > l.maxBytes)
			return false;
	}
	return bytes <= l.maxBytes;
}
std::vector<const Record *> Ordered(const Scene &s)
{
	std::vector<const Record *> r;
	for (auto &x : s.records)
		r.push_back(&x);
	std::stable_sort(r.begin(), r.end(), [](const Record *a, const Record *b) {
		return std::tie(a->layer, a->id, a->source, a->sample, a->region, a->configuration,
						a->alternative, a->primitive, a->geometryKnown, a->a.x, a->a.y, a->a.z,
						a->b.x, a->b.y, a->b.z, a->c.x, a->c.y, a->c.z, a->state, a->valueKnown,
						a->value.x, a->value.y, a->value.z, a->lowerKnown, a->lower.x, a->lower.y,
						a->lower.z, a->upperKnown, a->upper.x, a->upper.y, a->upper.z, a->unit,
						a->issues, a->quantity, a->domain, a->profile, a->reason, a->provenance) <
			   std::tie(b->layer, b->id, b->source, b->sample, b->region, b->configuration,
						b->alternative, b->primitive, b->geometryKnown, b->a.x, b->a.y, b->a.z,
						b->b.x, b->b.y, b->b.z, b->c.x, b->c.y, b->c.z, b->state, b->valueKnown,
						b->value.x, b->value.y, b->value.z, b->lowerKnown, b->lower.x, b->lower.y,
						b->lower.z, b->upperKnown, b->upper.x, b->upper.y, b->upper.z, b->unit,
						b->issues, b->quantity, b->domain, b->profile, b->reason, b->provenance);
	});
	return r;
}
void CsvText(std::ostream &o, const std::string &s)
{
	o << '"';
	for (char c : s)
	{
		if (c == '"')
			o << '"';
		o << c;
	}
	o << '"';
}
} // namespace
Error Encode(const Scene &s, std::string &out, const Limits &l)
{
	try
	{
		if (!Valid(s, l))
			return Bad("Diagnostic scene/size invalid");
		std::ostringstream o;
		o.imbue(std::locale::classic());
		o << std::setprecision(17) << "JKLIGHTDEBUG1\n"
		  << Disk::Hex(s.map) << ' ' << Disk::Hex(s.fingerprint) << ' ' << s.generation << ' '
		  << s.units.worldUnitsPerMeter << ' ' << s.units.physicalToScene << ' ' << s.requested
		  << ' ' << s.omitted << ' ' << s.invalidQueries << ' ' << s.unknownQueries << ' ' << s.rays
		  << ' ' << s.complete << ' ' << s.records.size();
		for (const auto &str : {s.mapPath, s.algorithm, s.settings, s.scope})
			o << ' ' << HexText(str);
		o << '\n';
		for (auto r : Ordered(s))
		{
			unsigned flags =
				r->geometryKnown + 2 * r->valueKnown + 4 * r->lowerKnown + 8 * r->upperKnown;
			o << r->id << ' ' << r->source << ' ' << r->sample << ' ' << r->region << ' '
			  << r->configuration << ' ' << r->alternative << ' ' << unsigned(r->layer) << ' '
			  << unsigned(r->state) << ' ' << unsigned(r->primitive) << ' ' << flags << ' '
			  << unsigned(r->unit) << ' ' << r->issues;
			for (auto v : {r->a, r->b, r->c, r->value, r->lower, r->upper})
				o << ' ' << v.x << ' ' << v.y << ' ' << v.z;
			for (const auto &str : {r->quantity, r->domain, r->profile, r->reason, r->provenance})
				o << ' ' << HexText(str);
			o << '\n';
			if (uint64_t(o.tellp()) > l.maxBytes)
				return Failure(Code::MemoryLimit, "Diagnostic serialization budget");
		}
		auto text = o.str();
		text += "SHA256 " + Disk::Hex(Disk::Hash(text.data(), text.size())) + "\n";
		if (text.size() > l.maxBytes)
			return Failure(Code::MemoryLimit, "Diagnostic bytes budget");
		out = std::move(text);
		return {};
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Diagnostic encoding allocation");
	}
}
Error Decode(const std::string &text, const Disk::Digest &expected, Scene &out, const Limits &l)
{
	out = {};
	try
	{
		if (text.size() > l.maxBytes || expected == Disk::Digest{})
			return Bad("Diagnostic import size/map identity missing");
		auto seal = text.rfind("SHA256 ");
		if (seal == std::string::npos || text.size() - seal != 72 || text.back() != '\n' ||
			text.substr(seal + 7, 64) != Disk::Hex(Disk::Hash(text.data(), seal)))
			return Bad("Diagnostic checksum/truncation");
		std::istringstream in(text.substr(0, seal));
		in.imbue(std::locale::classic());
		Scene s;
		std::string magic, map, key;
		unsigned complete;
		size_t count;
		bool header = bool(in >> magic >> map >> key) && Unsigned(in, s.generation) &&
					  bool(in >> s.units.worldUnitsPerMeter >> s.units.physicalToScene) &&
					  Unsigned(in, s.requested) && Unsigned(in, s.omitted) &&
					  Unsigned(in, s.invalidQueries) && Unsigned(in, s.unknownQueries) &&
					  Unsigned(in, s.rays) && Unsigned(in, complete) && Unsigned(in, count);
		if (!header || magic != "JKLIGHTDEBUG1" || !Digest(map, s.map) || s.map != expected ||
			!Digest(key, s.fingerprint) || complete > 1 || count > l.maxRecords)
			return Bad("Diagnostic schema/map/header mismatch");
		s.complete = complete;
		for (auto p : {&s.mapPath, &s.algorithm, &s.settings, &s.scope})
		{
			std::string token;
			in >> token;
			if (!Unhex(token, *p, l.maxString))
				return Bad("Diagnostic header string");
		}
		s.records.reserve(count);
		for (size_t j = 0; j < count; ++j)
		{
			Record r;
			unsigned layer, state, primitive, flags, unit;
			bool fields = Unsigned(in, r.id) && Unsigned(in, r.source) && Unsigned(in, r.sample) &&
						  Unsigned(in, r.region) && Unsigned(in, r.configuration) &&
						  Unsigned(in, r.alternative) && Unsigned(in, layer) &&
						  Unsigned(in, state) && Unsigned(in, primitive) && Unsigned(in, flags) &&
						  Unsigned(in, unit) && Unsigned(in, r.issues);
			if (!fields || layer >= unsigned(Layer::Count) || state >= unsigned(State::Count) ||
				primitive > 2 || flags > 15 || unit > unsigned(PhotometricUnit::Lux))
				return Bad("Diagnostic enum/flags invalid");
			r.layer = Layer(layer);
			r.state = State(state);
			r.primitive = Primitive(primitive);
			r.geometryKnown = flags & 1;
			r.valueKnown = flags & 2;
			r.lowerKnown = flags & 4;
			r.upperKnown = flags & 8;
			r.unit = PhotometricUnit(unit);
			for (auto p : {&r.a, &r.b, &r.c, &r.value, &r.lower, &r.upper})
				in >> p->x >> p->y >> p->z;
			for (auto p : {&r.quantity, &r.domain, &r.profile, &r.reason, &r.provenance})
			{
				std::string token;
				in >> token;
				if (!Unhex(token, *p, l.maxString))
					return Bad("Diagnostic record string");
			}
			if (!in)
				return Bad("Truncated diagnostic record");
			s.records.push_back(std::move(r));
		}
		std::string extra;
		if (in >> extra || !Valid(s, l))
			return Bad("Trailing/invalid diagnostic records");
		out = std::move(s);
		return {};
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Diagnostic import allocation");
	}
}
Error Csv(const Scene &s, std::ostream &o, const Limits &l)
{
	o.imbue(std::locale::classic());
	if (!Valid(s, l))
		return Bad("Invalid diagnostic CSV");
	o << "id,source,sample,region,configuration,layer,state,x,y,z,value_known,r,g,b,unit,quantity,"
		 "domain,reason\n"
	  << std::setprecision(17);
	for (auto r : Ordered(s))
	{
		o << r->id << ',' << r->source << ',' << r->sample << ',' << r->region << ','
		  << r->configuration << ',' << Name(r->layer) << ',' << Name(r->state) << ',' << r->a.x
		  << ',' << r->a.y << ',' << r->a.z << ',' << r->valueKnown << ',';
		if (r->valueKnown)
			o << r->value.x << ',' << r->value.y << ',' << r->value.z;
		else
			o << ",,";
		o << ',' << unsigned(r->unit) << ',';
		CsvText(o, r->quantity);
		o << ',';
		CsvText(o, r->domain);
		o << ',';
		CsvText(o, r->reason);
		o << '\n';
	}
	return o ? Error() : Failure(Code::ProviderError, "Diagnostic CSV write");
}
Error Obj(const Scene &s, std::ostream &o, const Selection &selection, const Limits &l)
{
	o.imbue(std::locale::classic());
	if (!Valid(s, l))
		return Bad("Invalid diagnostic OBJ");
	o << "# Data-only world-space geometry; JA +Z up; no inferred visible fixture mesh\n"
	  << std::setprecision(17);
	size_t vertex = 1;
	for (auto r : Ordered(s))
		if (r->geometryKnown && (selection.layers ? selection.Matches(*r) : true))
		{
			o << "g " << Name(r->layer) << '_' << Name(r->state) << '_' << r->id << "\nv " << r->a.x
			  << ' ' << r->a.y << ' ' << r->a.z << '\n';
			if (r->primitive == Primitive::Point)
				o << "p " << vertex++ << '\n';
			else
			{
				o << "v " << r->b.x << ' ' << r->b.y << ' ' << r->b.z << '\n';
				if (r->primitive == Primitive::Triangle)
				{
					o << "v " << r->c.x << ' ' << r->c.y << ' ' << r->c.z << "\nf " << vertex << ' '
					  << vertex + 1 << ' ' << vertex + 2 << '\n';
					vertex += 3;
				}
				else
				{
					o << "l " << vertex << ' ' << vertex + 1 << '\n';
					vertex += 2;
				}
			}
		}
	return o ? Error() : Failure(Code::ProviderError, "Diagnostic OBJ write");
}
Error HeatmapPpm(const std::vector<Record> &rows, Layer layer, unsigned width, const Heatmap &scale,
				 std::string &out, const Limits &limits)
{
	if (!width || width > 4096 || !std::isfinite(scale.minimum) || !std::isfinite(scale.maximum) ||
		scale.maximum <= scale.minimum || rows.size() > limits.maxRecords)
		return Bad("Invalid heatmap range/size");
	std::vector<const Record *> selected;
	std::string domain;
	PhotometricUnit unit = PhotometricUnit::Relative;
	for (auto &r : rows)
		if (r.layer == layer)
		{
			if (r.valueKnown)
			{
				if (domain.empty())
				{
					domain = r.domain;
					unit = r.unit;
				}
				if (domain.empty() || r.domain != domain || r.unit != unit || !Finite(r))
					return Bad("Heatmap requires one declared comparable numerical domain");
			}
			selected.push_back(&r);
		}
	if (selected.empty())
		return Failure(Code::Unavailable, "No heatmap records");
	unsigned height = unsigned((selected.size() + width - 1) / width);
	if (uint64_t(width) * height * 3 + 256 > limits.maxBytes)
		return Failure(Code::MemoryLimit, "Heatmap budget");
	std::ostringstream header;
	header << "P6\n# metric=max-absolute-channel; scale=" << scale.minimum << ":" << scale.maximum
		   << " unknown=magenta\n"
		   << width << ' ' << height << "\n255\n";
	std::string pixels = header.str();
	for (size_t j = 0; j < size_t(width) * height; ++j)
	{
		uint8_t c[3] = {32, 32, 32};
		if (j < selected.size())
		{
			auto r = selected[j];
			if (!r->valueKnown)
			{
				c[0] = 255;
				c[1] = 0;
				c[2] = 255;
			}
			else
			{
				double v = r->value.x;
				for (auto x : {r->value.y, r->value.z})
					if (std::abs(x) > std::abs(v))
						v = x;
				double t = std::max(0., std::min(1., (std::abs(v) - scale.minimum) /
														 (scale.maximum - scale.minimum)));
				c[0] = uint8_t(255 * t);
				c[1] = uint8_t(255 * (1 - t));
				c[2] = scale.signedValues && v < 0 ? uint8_t(255 * t) : 0;
			}
		}
		pixels.append(reinterpret_cast<const char *>(c), 3);
	}
	out = std::move(pixels);
	return {};
}
} // namespace Diagnostics
} // namespace Core
} // namespace Rend2LightRecon
