// Portable sparse CPU cache; no renderer layouts or implicit file access.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shadow_cache_internal.h"
#include <cstring>
#include <map>
namespace Rend2LightRecon
{
namespace Core
{
namespace ShadowCache
{
namespace
{
struct Writer
{
	std::vector<uint8_t> b;
	void U(uint64_t u)
	{
		for (unsigned j = 0; j < 8; ++j)
			b.push_back(uint8_t(u >> (8 * j)));
	}
	void F(double x)
	{
		uint64_t u;
		std::memcpy(&u, &x, 8);
		U(u);
	}
	void V3(Vec3 p)
	{
		F(p.x);
		F(p.y);
		F(p.z);
	}
	void D(const Disk::Digest &d)
	{
		b.insert(b.end(), d.begin(), d.end());
	}
	void S(const std::string &s)
	{
		U(s.size());
		b.insert(b.end(), s.begin(), s.end());
	}
	void Seal()
	{
		D(Disk::Hash(b.data(), b.size()));
	}
};
struct Reader
{
	const std::vector<uint8_t> &b;
	size_t at = 0;
	explicit Reader(const std::vector<uint8_t> &v) : b(v)
	{
	}
	uint64_t U()
	{
		Need(at <= b.size() && b.size() - at >= 8, Code::InvalidInput, "Truncated shadow chunk");
		uint64_t u = 0;
		for (unsigned j = 0; j < 8; ++j)
			u |= uint64_t(b[at++]) << (8 * j);
		return u;
	}
	double F()
	{
		auto u = U();
		double v;
		std::memcpy(&v, &u, 8);
		Need(std::isfinite(v), Code::InvalidInput, "Nonfinite shadow value");
		return v;
	}
	bool B()
	{
		auto u = U();
		Need(u <= 1, Code::InvalidInput, "Invalid shadow flag");
		return u != 0;
	}
	Vec3 V3()
	{
		float x = float(F()), y = float(F()), z = float(F());
		Vec3 p(x, y, z);
		Need(Finite(p), Code::InvalidInput, "Shadow coordinate overflow");
		return p;
	}
	Disk::Digest D()
	{
		Need(at <= b.size() && b.size() - at >= 32, Code::InvalidInput, "Truncated shadow digest");
		Disk::Digest d;
		std::copy(b.begin() + at, b.begin() + at + 32, d.begin());
		at += 32;
		return d;
	}
	std::string S()
	{
		auto n = U();
		Need(n <= 65536 && at <= b.size() && n <= b.size() - at, Code::InvalidInput,
			 "Shadow string budget");
		std::string s(b.begin() + at, b.begin() + at + size_t(n));
		at += size_t(n);
		return s;
	}
	void End()
	{
		Need(at == b.size() - 32, Code::InvalidInput, "Trailing shadow data");
	}
};
void SealCheck(const Disk::Chunk &c)
{
	Need(c.bytes.size() >= 48 && c.bytes.size() <= 1ull << 20, Code::InvalidInput,
		 "Shadow chunk size");
	auto key = Disk::Hash(c.bytes.data(), c.bytes.size() - 32);
	Need(std::equal(key.begin(), key.end(), c.bytes.end() - 32), Code::InvalidInput,
		 "Shadow checksum mismatch");
}
Disk::Digest DatasetKey(const std::vector<Disk::Chunk> &chunks, Id dataset)
{
	GeometryKey k;
	for (auto &c : chunks)
		if (c.dataset == dataset)
		{
			k.U(c.firstElement);
			k.U(c.count);
			k.Digest(Disk::Hash(c.bytes.data(), c.bytes.size()));
		}
	return k.Finish();
}
void EstimateOut(Writer &w, const VisibilityEstimate &e, Storage storage)
{
	w.U(unsigned(e.state));
	w.U(e.estimateKnown);
	for (double v : {e.value, e.lower, e.upper})
	{
		if (storage == Storage::Float64)
			w.F(v);
		else
		{
			auto n = uint16_t(std::llround(v * 65535));
			w.b.push_back(uint8_t(n));
			w.b.push_back(uint8_t(n >> 8));
		}
	}
}
VisibilityEstimate EstimateIn(Reader &r, Storage storage)
{
	VisibilityEstimate e;
	auto s = r.U();
	Need(s <= unsigned(ShadowState::Unknown), Code::InvalidInput, "Shadow state enum");
	e.state = ShadowState(s);
	e.estimateKnown = r.B();
	double *v[3] = {&e.value, &e.lower, &e.upper};
	for (auto p : v)
	{
		if (storage == Storage::Float64)
			*p = r.F();
		else
		{
			Need(r.at <= r.b.size() && r.b.size() - r.at >= 2, Code::InvalidInput,
				 "Truncated compact fraction");
			*p = double(uint16_t(r.b[r.at]) | uint16_t(r.b[r.at + 1]) << 8) / 65535;
			r.at += 2;
		}
	}
	Need(Valid(e), Code::InvalidInput, "Invalid visibility interval");
	return e;
}
void Quantize(VisibilityEstimate &e)
{
	e.value = std::round(e.value * 65535) / 65535;
	e.lower = std::floor(e.lower * 65535) / 65535;
	e.upper = std::ceil(e.upper * 65535) / 65535;
}
void Row(Writer &w, const Record &r, Storage storage)
{
	const auto &i = r.item;
	const auto &v = r.result;
	w.U(unsigned(i.kind));
	w.U(i.source);
	w.U(i.receiver.id);
	w.U(i.receiver.region);
	w.U(i.receiver.surface);
	w.V3(i.receiver.positionWorld);
	w.V3(i.receiver.geometricNormal);
	w.U(i.receiver.normalKnown);
	w.F(i.receiver.footprintWorld);
	w.V3(i.targetOrDirection);
	w.D(r.key);
	w.D(r.sourceKey);
	w.D(r.supportKey);
	for (auto e : {v.staticGeometry, v.scenarioGeometry, v.staticResponse, v.scenarioResponse})
		EstimateOut(w, e, storage);
	w.U(v.responseApplicable);
	w.U(v.conditional);
	w.U(v.runtimeUpdateRequired);
	w.U(v.occluderDistanceKnown);
	w.F(v.nearestStaticOccluderWorld);
	w.F(v.samplingDifference);
	w.F(v.quantizationBound);
	w.U(v.issues);
	w.U(v.rayIssues);
	w.U(v.coarseSamples);
	w.U(v.referenceSamples);
}
Record Row(Reader &r, Storage storage, const Descriptor &d)
{
	Record row;
	auto &i = row.item;
	auto &v = row.result;
	auto kind = r.U();
	Need(kind <= unsigned(ShadowQueryKind::SkyDirection), Code::InvalidInput, "Shadow query kind");
	i.kind = ShadowQueryKind(kind);
	i.source = r.U();
	i.receiver.id = r.U();
	i.receiver.region = r.U();
	i.receiver.surface = r.U();
	i.receiver.positionWorld = r.V3();
	i.receiver.geometricNormal = r.V3();
	i.receiver.normalKnown = r.B();
	i.receiver.footprintWorld = r.F();
	i.targetOrDirection = r.V3();
	row.key = r.D();
	row.sourceKey = r.D();
	row.supportKey = r.D();
	v.dataset = d.dataset;
	v.scenario = d.scenario;
	v.receiver = i.receiver.id;
	v.source = i.source;
	v.region = i.receiver.region;
	v.staticGeometry = EstimateIn(r, storage);
	v.scenarioGeometry = EstimateIn(r, storage);
	v.staticResponse = EstimateIn(r, storage);
	v.scenarioResponse = EstimateIn(r, storage);
	v.responseApplicable = r.B();
	v.conditional = r.B();
	v.runtimeUpdateRequired = r.B();
	v.occluderDistanceKnown = r.B();
	v.nearestStaticOccluderWorld = r.F();
	v.samplingDifference = r.F();
	v.quantizationBound = r.F();
	auto issues = r.U(), rayIssues = r.U(), coarse = r.U(), refined = r.U();
	Need(issues <= UINT32_MAX && rayIssues <= UINT32_MAX && coarse <= UINT32_MAX &&
			 refined <= UINT32_MAX,
		 Code::InvalidInput, "Shadow diagnostic overflow");
	v.issues = uint32_t(issues);
	v.rayIssues = uint32_t(rayIssues);
	v.coarseSamples = uint32_t(coarse);
	v.referenceSamples = uint32_t(refined);
	return row;
}
} // namespace
Error Validate(const Artifact &a, const ReadContext &read)
{
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Shadow validation cancelled");
		Metadata(a.asset, read.maxBytes ? read.maxBytes : 128ull << 20);
		std::string why;
		Need(Rend2LightRecon::Validate(a.asset, &why), Code::InvalidInput, why.c_str());
		auto &d = a.descriptor;
		Need(d.provenance &&
				 std::any_of(a.asset.provenanceRecords.begin(), a.asset.provenanceRecords.end(),
							 [&](const Provenance &p) { return p.id == d.provenance; }),
			 Code::InvalidInput, "Dangling shadow provenance");
		Need(d.areaDivisions && d.areaDivisions <= 32 && d.tubeSides >= 4 && d.tubeSides <= 64 &&
				 d.tubeSegments && d.tubeSegments <= 32 && std::isfinite(d.bias.absoluteWorld) &&
				 d.bias.absoluteWorld >= 0 && std::isfinite(d.bias.relative) &&
				 d.bias.relative >= 0 && std::isfinite(d.bias.maximumWorld) &&
				 d.bias.maximumWorld >= 0 && std::isfinite(d.skyDistanceWorld) &&
				 d.skyDistanceWorld > 0 && std::isfinite(d.maximumSamplingDifference) &&
				 d.maximumSamplingDifference >= 0 &&
				 unsigned(d.optical.facing) <= unsigned(FacingPolicy::TwoSided) &&
				 unsigned(d.optical.alphaTested) <= unsigned(OpticalPolicy::Ignore) &&
				 unsigned(d.optical.translucent) <= unsigned(OpticalPolicy::Ignore),
			 Code::InvalidInput, "Invalid shadow sampling/optical recipe");
		auto dataset = std::find_if(a.asset.datasets.begin(), a.asset.datasets.end(),
									[&](const Dataset &x) { return x.id == d.dataset; });
		Need(dataset != a.asset.datasets.end() && dataset->purpose == DatasetPurpose::Visibility &&
				 dataset->encoding == "org.openjk.static-shadow-support" &&
				 dataset->encodingVersion == kVersion &&
				 dataset->elementCount == a.records.size() + 1 && d.map != Disk::Digest{} &&
				 d.geometry != Disk::Digest{} && d.policy != Disk::Digest{} &&
				 unsigned(d.storage) <= unsigned(Storage::UNorm16),
			 Code::InvalidInput, "Shadow dataset identity/coverage");
		Need(std::any_of(a.asset.visibility.begin(), a.asset.visibility.end(),
						 [&](const VisibilityReference &v) {
							 return v.dataset == d.dataset &&
									v.kind == VisibilityKind::SegmentTransmittance;
						 }) &&
				 std::any_of(a.asset.visibility.begin(), a.asset.visibility.end(),
							 [&](const VisibilityReference &v) {
								 return v.dataset == d.dataset &&
										v.kind == VisibilityKind::SkyTransmittance;
							 }),
			 Code::InvalidInput, "Missing canonical visibility references");
		Need(d.key == Content(d, a.records) && d.coverage.requested == a.records.size(),
			 Code::InvalidState, "Shadow content generation mismatch");
		Need(a.records.size() <= 100000 && a.records.size() * sizeof(Record) <=
											   (read.maxBytes ? read.maxBytes : 128ull << 20),
			 Code::MemoryLimit, "Prepared shadow records budget");
		std::map<Id, Disk::Digest> sourceKeys;
		uint64_t known = 0, unknown = 0, conditional = 0, unsupported = 0;
		for (size_t j = 0; j < a.records.size(); ++j)
		{
			Need(!Cancelled(read), Code::Cancelled, "Shadow record validation cancelled");
			const auto &r = a.records[j];
			const auto &i = r.item;
			const auto &v = r.result;
			Need(j == 0 || RecordLess(a.records[j - 1], r), Code::InvalidInput,
				 "Duplicate/unordered shadow samples");
			Need(i.receiver.id && Finite(i.receiver.positionWorld) && Finite(i.targetOrDirection) &&
					 Finite(i.receiver.geometricNormal) &&
					 (!i.receiver.normalKnown || Normal(i.receiver.geometricNormal)) &&
					 std::isfinite(i.receiver.footprintWorld) && i.receiver.footprintWorld >= 0 &&
					 unsigned(i.kind) <= unsigned(ShadowQueryKind::SkyDirection),
				 Code::InvalidInput, "Invalid shadow receiver geometry");
			Need(i.kind != ShadowQueryKind::SkyDirection || Normal(i.targetOrDirection),
				 Code::InvalidInput, "Invalid shadow sky direction");
			Need(i.kind != ShadowQueryKind::Segment ||
					 !Same(i.receiver.positionWorld, i.targetOrDirection),
				 Code::InvalidInput, "Degenerate cached segment");
			Need(r.key == ItemKey(i, d.policy, r.sourceKey, r.supportKey), Code::InvalidState,
				 "Shadow dependency key mismatch");
			if (i.kind == ShadowQueryKind::Source)
			{
				auto key = sourceKeys.find(i.source);
				if (key == sourceKeys.end())
				{
					auto e = Emitter(a.asset, i.source);
					Need(e, Code::InvalidInput, "Missing cached source ID");
					key = sourceKeys.emplace(i.source, SourceKey(*e, {})).first;
				}
				Need(key->second == r.sourceKey, Code::InvalidState,
					 "Stale cached emitter geometry");
			}
			Need(v.dataset == d.dataset && v.receiver == i.receiver.id && v.source == i.source &&
					 v.region == i.receiver.region && v.scenario == d.scenario &&
					 Valid(v.staticGeometry) && Valid(v.scenarioGeometry) &&
					 Valid(v.staticResponse) && Valid(v.scenarioResponse) &&
					 std::isfinite(v.nearestStaticOccluderWorld) &&
					 v.nearestStaticOccluderWorld >= 0 && std::isfinite(v.samplingDifference) &&
					 v.samplingDifference >= 0 && std::isfinite(v.quantizationBound) &&
					 v.quantizationBound >= 0,
				 Code::InvalidInput, "Invalid shadow result");
			known += v.staticGeometry.estimateKnown;
			unknown += !v.staticGeometry.estimateKnown;
			conditional += v.conditional;
			unsupported += (v.issues & UnsupportedSource) != 0;
		}
		Need(d.coverage.built + d.coverage.reused == d.coverage.requested &&
				 known == d.coverage.staticKnown && unknown == d.coverage.staticUnknown &&
				 conditional == d.coverage.scenarioConditional &&
				 unsupported == d.coverage.unsupported,
			 Code::InvalidInput, "Shadow diagnostic coverage mismatch");
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow validation allocation");
	}
}
Error Encode(Artifact &a, const ReadContext &read)
{
	try
	{
		Check(Validate(a, read));
		Artifact temp = a;
		temp.chunks.clear();
		auto &d = temp.descriptor;
		if (d.storage == Storage::UNorm16)
		{
			for (auto &r : temp.records)
			{
				auto &v = r.result;
				if (v.issues & Quantized)
					continue;
				Quantize(v.staticGeometry);
				Quantize(v.scenarioGeometry);
				Quantize(v.staticResponse);
				Quantize(v.scenarioResponse);
				v.issues |= Quantized;
				v.quantizationBound = 1. / 65535;
			}
		}
		d.key = Content(d, temp.records);
		Writer w;
		w.U(0x315748534b4aull);
		w.U(kVersion);
		w.U(d.dataset);
		w.U(d.provenance);
		w.U(d.scenario);
		w.U(unsigned(d.storage));
		w.D(d.map);
		w.D(d.geometry);
		w.D(d.policy);
		w.D(d.key);
		w.S(d.scope);
		w.S(d.occupancyProvenance);
		w.U(d.areaDivisions);
		w.U(d.tubeSides);
		w.U(d.tubeSegments);
		w.U(unsigned(d.optical.facing));
		w.U(unsigned(d.optical.alphaTested));
		w.U(unsigned(d.optical.translucent));
		w.U(d.optical.acceptPatchApproximation);
		w.U(d.optical.requireKnownFreeOrigin);
		w.F(d.bias.absoluteWorld);
		w.F(d.bias.relative);
		w.F(d.bias.maximumWorld);
		w.F(d.skyDistanceWorld);
		w.F(d.maximumSamplingDifference);
		w.U(d.coverage.requested);
		w.U(d.coverage.built);
		w.U(d.coverage.reused);
		w.U(d.coverage.rays);
		w.U(d.coverage.traversal);
		w.U(d.coverage.staticKnown);
		w.U(d.coverage.staticUnknown);
		w.U(d.coverage.scenarioConditional);
		w.U(d.coverage.unsupported);
		w.Seal();
		Disk::Chunk header;
		header.dataset = d.dataset;
		header.count = 1;
		header.bytes = std::move(w.b);
		temp.chunks.push_back(std::move(header));
		for (size_t first = 0; first < temp.records.size(); first += 64)
		{
			Need(!Cancelled(read), Code::Cancelled, "Shadow encoding cancelled");
			Writer row;
			row.U(0x315257534b4aull);
			row.U(kVersion);
			auto end = std::min(temp.records.size(), first + 64);
			for (size_t j = first; j < end; ++j)
				Row(row, temp.records[j], d.storage);
			row.Seal();
			Disk::Chunk c;
			c.dataset = d.dataset;
			c.firstElement = first + 1;
			c.count = end - first;
			c.bytes = std::move(row.b);
			temp.chunks.push_back(std::move(c));
		}
		uint64_t bytes = 0;
		for (auto &c : temp.chunks)
			bytes += c.bytes.size();
		Need(bytes <= (read.maxBytes ? read.maxBytes : 128ull << 20), Code::MemoryLimit,
			 "Shadow chunk encoding budget");
		for (auto &x : temp.asset.datasets)
			if (x.id == d.dataset)
				x.contentHash = Disk::Hex(DatasetKey(temp.chunks, d.dataset));
		Check(Validate(temp, read));
		a = std::move(temp);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow encoding allocation");
	}
}
Error Decode(const ReconstructedLightingAsset &a, const std::vector<Disk::Chunk> &chunks,
			 Id dataset, const Disk::Digest &map, const Disk::Digest &geometry,
			 const ReadContext &read, Artifact &out)
{
	out = {};
	try
	{
		Metadata(a, read.maxBytes ? read.maxBytes : 128ull << 20);
		std::vector<const Disk::Chunk *> rows;
		uint64_t bytes = 0;
		for (auto &c : chunks)
			if (c.dataset == dataset)
			{
				rows.push_back(&c);
				bytes += c.bytes.size();
			}
		Need(bytes <= (read.maxBytes ? read.maxBytes : 128ull << 20), Code::MemoryLimit,
			 "Shadow decode byte budget");
		std::sort(rows.begin(), rows.end(), [](const Disk::Chunk *a, const Disk::Chunk *b) {
			return a->firstElement < b->firstElement;
		});
		Need(!rows.empty() && rows[0]->firstElement == 0 && rows[0]->count == 1, Code::InvalidInput,
			 "Missing shadow header");
		SealCheck(*rows[0]);
		Reader r(rows[0]->bytes);
		Need(r.U() == 0x315748534b4aull && r.U() == kVersion, Code::UnsupportedSchema,
			 "Shadow header version");
		auto &d = out.descriptor;
		d.dataset = r.U();
		d.provenance = r.U();
		d.scenario = r.U();
		auto storage = r.U();
		Need(storage <= unsigned(Storage::UNorm16), Code::UnsupportedSchema,
			 "Unknown shadow storage");
		d.storage = Storage(storage);
		d.map = r.D();
		d.geometry = r.D();
		d.policy = r.D();
		d.key = r.D();
		d.scope = r.S();
		d.occupancyProvenance = r.S();
		auto area = r.U(), sides = r.U(), segments = r.U(), facing = r.U(), alpha = r.U(),
			 translucent = r.U();
		Need(area && area <= 32 && sides >= 4 && sides <= 64 && segments && segments <= 32 &&
				 facing <= unsigned(FacingPolicy::TwoSided) &&
				 alpha <= unsigned(OpticalPolicy::Ignore) &&
				 translucent <= unsigned(OpticalPolicy::Ignore),
			 Code::UnsupportedSchema, "Unknown shadow sampling/optical recipe");
		d.areaDivisions = unsigned(area);
		d.tubeSides = unsigned(sides);
		d.tubeSegments = unsigned(segments);
		d.optical.facing = FacingPolicy(facing);
		d.optical.alphaTested = OpticalPolicy(alpha);
		d.optical.translucent = OpticalPolicy(translucent);
		d.optical.acceptPatchApproximation = r.B();
		d.optical.requireKnownFreeOrigin = r.B();
		d.bias.absoluteWorld = r.F();
		d.bias.relative = r.F();
		d.bias.maximumWorld = r.F();
		d.skyDistanceWorld = r.F();
		d.maximumSamplingDifference = r.F();
		d.coverage.requested = r.U();
		d.coverage.built = r.U();
		d.coverage.reused = r.U();
		d.coverage.rays = r.U();
		d.coverage.traversal = r.U();
		d.coverage.staticKnown = r.U();
		d.coverage.staticUnknown = r.U();
		d.coverage.scenarioConditional = r.U();
		d.coverage.unsupported = r.U();
		r.End();
		Need(d.dataset == dataset && d.map == map && d.geometry == geometry, Code::InvalidState,
			 "Wrong map/geometry visibility cache");
		Need(d.coverage.requested <= 100000, Code::MemoryLimit, "Shadow decode record budget");
		auto descriptor = std::find_if(a.datasets.begin(), a.datasets.end(),
									   [&](const Dataset &x) { return x.id == dataset; });
		Need(descriptor != a.datasets.end() &&
				 descriptor->contentHash == Disk::Hex(DatasetKey(chunks, dataset)),
			 Code::InvalidInput, "Shadow dataset content checksum");
		uint64_t first = 1;
		for (size_t j = 1; j < rows.size(); ++j)
		{
			Need(!Cancelled(read), Code::Cancelled, "Shadow decode cancelled");
			auto &c = *rows[j];
			Need(c.firstElement == first && c.count && c.count <= 64, Code::InvalidInput,
				 "Missing/duplicate shadow rows");
			SealCheck(c);
			Reader reader(c.bytes);
			Need(reader.U() == 0x315257534b4aull && reader.U() == kVersion, Code::UnsupportedSchema,
				 "Shadow row version");
			for (uint64_t k = 0; k < c.count; ++k)
			{
				Need(out.records.size() < d.coverage.requested, Code::InvalidInput,
					 "Unexpected shadow row count");
				out.records.push_back(Row(reader, d.storage, d));
			}
			reader.End();
			first += c.count;
		}
		Need(out.records.size() == d.coverage.requested, Code::InvalidInput,
			 "Incomplete shadow dataset");
		out.asset = a;
		out.work = d.coverage;
		for (auto c : rows)
			out.chunks.push_back(*c);
		Check(Validate(out, read));
		return {};
	}
	catch (const Error &e)
	{
		out = {};
		return e;
	}
	catch (const std::bad_alloc &)
	{
		out = {};
		return Failure(Code::MemoryLimit, "Shadow decoding allocation");
	}
}
} // namespace ShadowCache
} // namespace Core
} // namespace Rend2LightRecon
