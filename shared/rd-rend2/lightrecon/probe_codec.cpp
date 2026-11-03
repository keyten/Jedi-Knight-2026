// Bounded chunked probe geometry/topology records, separate from angular values.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "probe_internal.h"
#include <cstring>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace Probes
{
namespace
{
struct Writer
{
	std::vector<uint8_t> b;
	void U(uint64_t x)
	{
		for (unsigned k = 0; k < 8; ++k)
			b.push_back(uint8_t(x >> (k * 8)));
	}
	void F(double x)
	{
		uint64_t u;
		std::memcpy(&u, &x, 8);
		U(u);
	}
	void V3(Vec3 v)
	{
		F(v.x);
		F(v.y);
		F(v.z);
	}
	void B(Bounds b)
	{
		U(b.known);
		V3(b.minWorld);
		V3(b.maxWorld);
	}
	void D(const Disk::Digest &d)
	{
		b.insert(b.end(), d.begin(), d.end());
	}
};
struct Reader
{
	const std::vector<uint8_t> &b;
	size_t at = 0;
	explicit Reader(const std::vector<uint8_t> &bytes) : b(bytes)
	{
	}
	uint64_t U()
	{
		if (at > b.size() || b.size() - at < 8)
			throw Failure(Code::InvalidInput, "Truncated probe chunk");
		uint64_t x = 0;
		for (unsigned k = 0; k < 8; ++k)
			x |= uint64_t(b[at++]) << (8 * k);
		return x;
	}
	uint32_t U32()
	{
		auto n = U();
		if (n > UINT32_MAX)
			throw Failure(Code::InvalidInput, "Probe integer overflow");
		return uint32_t(n);
	}
	double F()
	{
		auto u = U();
		double x;
		std::memcpy(&x, &u, 8);
		if (!std::isfinite(x))
			throw Failure(Code::InvalidInput, "Nonfinite probe payload");
		return x;
	}
	Vec3 V3()
	{
		double x = F(), y = F(), z = F();
		Vec3 v{float(x), float(y), float(z)};
		if (!Finite(v))
			throw Failure(Code::InvalidInput, "Probe coordinate overflow");
		return v;
	}
	Bounds B()
	{
		Bounds b;
		auto k = U();
		if (k > 1)
			throw Failure(Code::InvalidInput, "Probe bounds flag");
		b.known = k != 0;
		b.minWorld = V3();
		b.maxWorld = V3();
		return b;
	}
	Disk::Digest D()
	{
		Disk::Digest d{};
		if (at > b.size() || b.size() - at < 32)
			throw Failure(Code::InvalidInput, "Truncated probe digest");
		std::copy(b.begin() + at, b.begin() + at + 32, d.begin());
		at += 32;
		return d;
	}
};
void Header(Writer &w, const Topology::Data &d, unsigned kind)
{
	w.U(kind);
	w.U(kVersion);
	w.D(d.map);
	w.D(d.geometry);
	w.D(d.domain);
	w.D(d.policy);
	w.D(d.key);
	w.B(d.options.bounds);
	w.F(d.options.coarseSpacingWorld);
	w.F(d.options.minimumSpacingWorld);
	w.F(d.options.clearanceWorld);
}
} // namespace
Error Encode(const Topology &t, Id geometryDataset, Id topologyDataset, const ReadContext &read,
			 std::vector<Disk::Chunk> &out)
{
	out.clear();
	try
	{
		auto e = ValidateData(*t.Storage(), read);
		if (!e)
			return e;
		if (!geometryDataset || !topologyDataset || geometryDataset == topologyDataset)
			return Failure(Code::InvalidInput, "Distinct probe dataset IDs required");
		const auto &d = *t.Storage();
		uint64_t total = 0;
		for (unsigned kind = 1; kind <= 2; ++kind)
		{
			size_t count = kind == 1 ? d.probes.size() : d.cells.size();
			for (size_t a = 0; a < count; a += 128)
			{
				if (Cancelled(read))
					throw Failure(Code::Cancelled, "Probe encoding cancelled");
				Writer w;
				Header(w, d, kind);
				size_t end = std::min(count, a + 128);
				for (size_t j = a; j < end; ++j)
					if (kind == 1)
					{
						const auto &p = d.probes[j];
						w.U(p.id);
						w.U(p.source);
						w.U(p.cell);
						w.U(p.region);
						w.V3(p.originalWorld);
						w.V3(p.positionWorld);
						w.B(p.support);
						w.U(unsigned(p.classification));
						w.U(p.issues);
						w.U(p.level);
						w.U(uint32_t(p.leaf));
						w.U(uint32_t(p.area));
						w.F(p.displacementWorld);
						w.F(p.clearanceWorld);
						w.U(p.moment.present);
						w.V3(p.moment.directionToQuery);
						w.F(p.moment.minimumCosine);
						w.F(p.moment.mean);
						w.F(p.moment.second);
						w.F(p.moment.maximumDistance);
						w.U(p.moment.samples);
					}
					else
					{
						const auto &c = d.cells[j];
						w.U(c.id);
						w.U(c.parent);
						w.B(c.bounds);
						w.B(c.support);
						w.U(c.level);
						w.U(c.issues);
						w.U(unsigned(c.space));
						w.U(c.probe);
						w.U(c.linkCount);
						for (unsigned k = 0; k < c.linkCount; ++k)
						{
							auto &l = d.links[c.firstLink + k];
							w.U(l.probe);
							w.B(l.certificate);
							w.U(l.issues);
						}
					}
				auto digest = Disk::Hash(w.b.data(), w.b.size());
				w.D(digest);
				total += w.b.size();
				if (total > (read.maxBytes ? read.maxBytes : 128ull << 20))
					throw Failure(Code::MemoryLimit, "Probe encoded byte budget");
				Disk::Chunk c;
				c.dataset = kind == 1 ? geometryDataset : topologyDataset;
				c.firstElement = a;
				c.count = end - a;
				c.bytes = std::move(w.b);
				out.push_back(std::move(c));
			}
		}
		return {};
	}
	catch (const Error &e)
	{
		out.clear();
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out.clear();
		return Failure(Code::MemoryLimit, "Probe codec allocation");
	}
}
Error Decode(const std::vector<Disk::Chunk> &chunks, const Disk::Digest &expected,
			 const ReadContext &read, std::shared_ptr<const Topology> &out)
{
	out.reset();
	try
	{
		auto d = std::make_shared<Topology::Data>();
		uint64_t total = 0;
		Id datasets[3] = {};
		bool first = true;
		uint64_t counts[3] = {};
		for (const auto &c : chunks)
		{
			if (Cancelled(read))
				throw Failure(Code::Cancelled, "Probe decode cancelled");
			total += c.bytes.size();
			if (c.bytes.size() < 300 || c.bytes.size() > (1ull << 20) ||
				total > (read.maxBytes ? read.maxBytes : 128ull << 20) || !c.count || c.count > 128)
				throw Failure(Code::InvalidInput, "Probe chunk limits");
			auto hash = Disk::Hash(c.bytes.data(), c.bytes.size() - 32);
			if (!std::equal(hash.begin(), hash.end(), c.bytes.end() - 32))
				throw Failure(Code::InvalidInput, "Probe chunk checksum");
			Reader r{c.bytes};
			unsigned kind = r.U32();
			if (kind < 1 || kind > 2 || r.U() != kVersion)
				throw Failure(Code::UnsupportedSchema, "Probe chunk kind/version");
			auto map = r.D(), geometry = r.D(), domain = r.D(), policy = r.D(), key = r.D();
			auto bounds = r.B();
			float coarse = float(r.F()), minimum = float(r.F()), clearance = float(r.F());
			if (first)
			{
				d->map = map;
				d->geometry = geometry;
				d->domain = domain;
				d->policy = policy;
				d->key = key;
				d->options.bounds = bounds;
				d->options.coarseSpacingWorld = coarse;
				d->options.minimumSpacingWorld = minimum;
				d->options.clearanceWorld = clearance;
				first = false;
			}
			if (geometry != expected || map != d->map || geometry != d->geometry ||
				domain != d->domain || policy != d->policy || key != d->key ||
				bounds.known != d->options.bounds.known ||
				bounds.minWorld.x != d->options.bounds.minWorld.x ||
				bounds.minWorld.y != d->options.bounds.minWorld.y ||
				bounds.minWorld.z != d->options.bounds.minWorld.z ||
				bounds.maxWorld.x != d->options.bounds.maxWorld.x ||
				bounds.maxWorld.y != d->options.bounds.maxWorld.y ||
				bounds.maxWorld.z != d->options.bounds.maxWorld.z ||
				coarse != d->options.coarseSpacingWorld ||
				minimum != d->options.minimumSpacingWorld ||
				clearance != d->options.clearanceWorld || c.firstElement != counts[kind] ||
				!c.dataset || (datasets[kind] && datasets[kind] != c.dataset))
				throw Failure(Code::InvalidState, "Probe chunk generation/order mismatch");
			datasets[kind] = c.dataset;
			counts[kind] += c.count;
			if (counts[1] > 200000 || counts[2] > 1000000)
				throw Failure(Code::MemoryLimit, "Probe record bound");
			for (uint64_t j = 0; j < c.count; ++j)
				if (kind == 1)
				{
					Probe p;
					p.id = r.U();
					p.source = r.U();
					p.cell = r.U();
					p.region = r.U();
					p.originalWorld = r.V3();
					p.positionWorld = r.V3();
					p.support = r.B();
					p.classification = Classification(r.U32());
					p.issues = r.U32();
					p.level = r.U32();
					p.leaf = int32_t(r.U32());
					p.area = int32_t(r.U32());
					p.displacementWorld = r.F();
					p.clearanceWorld = r.F();
					auto present = r.U();
					if (present > 1)
						throw Failure(Code::InvalidInput, "Probe moment flag");
					p.moment.present = present != 0;
					p.moment.directionToQuery = r.V3();
					p.moment.minimumCosine = r.F();
					p.moment.mean = r.F();
					p.moment.second = r.F();
					p.moment.maximumDistance = r.F();
					p.moment.samples = r.U32();
					d->probes.push_back(p);
				}
				else
				{
					Cell cell;
					cell.id = r.U();
					cell.parent = r.U();
					cell.bounds = r.B();
					cell.support = r.B();
					cell.level = r.U32();
					cell.issues = r.U32();
					cell.space = SpaceClass(r.U32());
					cell.probe = r.U32();
					cell.firstLink = cell.probe == UINT32_MAX ? 0 : uint32_t(d->links.size());
					cell.linkCount = r.U32();
					if (cell.linkCount > kNeighbours || d->links.size() + cell.linkCount > 3200000)
						throw Failure(Code::InvalidInput, "Probe stencil limits");
					for (unsigned k = 0; k < cell.linkCount; ++k)
					{
						Link l;
						l.probe = r.U32();
						l.certificate = r.B();
						l.issues = r.U32();
						d->links.push_back(l);
					}
					d->cells.push_back(cell);
				}
			if (r.at != c.bytes.size() - 32)
				throw Failure(Code::InvalidInput, "Trailing probe chunk records");
		}
		if (first || !datasets[2] || (datasets[1] && datasets[1] == datasets[2]))
			return Failure(Code::InvalidInput, "Missing probe topology");
		d->report.analysisVolume = Volume(d->options.bounds);
		d->report.nodes = d->cells.size();
		d->report.valid = d->probes.size();
		d->report.links = d->links.size();
		for (const auto &p : d->probes)
		{
			d->report.relocated += p.displacementWorld > 0;
			d->report.conditional += (p.issues & DynamicConditional) != 0;
			d->report.unassignedRegion += !p.region;
			d->report.maximumDisplacement =
				std::max(d->report.maximumDisplacement, p.displacementWorld);
		}
		for (const auto &c : d->cells)
		{
			if (c.probe != UINT32_MAX)
			{
				d->report.supportedStaticVolume += Volume(c.support);
				d->report.unknownVolume += Volume(c.bounds) - Volume(c.support);
			}
			else if (c.space == SpaceClass::Solid)
			{
				++d->report.solid;
				d->report.solidVolume += Volume(c.bounds);
			}
			else
			{
				++d->report.unknown;
				d->report.unknownVolume += Volume(c.bounds);
			}
		}
		auto e = ValidateData(*d, read);
		if (!e)
			return e;
		Index(*d);
		d->report.bytes = sizeof(*d) + d->probes.size() * sizeof(Probe) +
						  d->cells.size() * sizeof(Cell) + d->links.size() * sizeof(Link) +
						  d->index.size() * sizeof(Node);
		out = std::make_shared<Topology>(d);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Probe decode allocation");
	}
}
} // namespace Probes
} // namespace Core
} // namespace Rend2LightRecon
