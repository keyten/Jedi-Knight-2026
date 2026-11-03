// SPDX-License-Identifier: GPL-2.0-or-later
#include "persistent.h"
#include "contributions.h"
#include "field_builder.h"
#include "geometry_key.h"
#include "shadow_cache.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
namespace Rend2LightRecon
{
namespace Disk
{
namespace Persistent
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
void Check(Core::Error e)
{
	if (!e)
		throw Fail(e.code == Core::Code::MemoryLimit
					   ? Code::LimitExceeded
					   : e.code == Core::Code::UnsupportedSchema ? Code::Unsupported
																 : Code::Corrupt,
				   e.message);
}
class Counting final : public Reader
{
	const Reader &reader_;
	Work &work_;

  public:
	Counting(const Reader &r, Work &w) : reader_(r), work_(w)
	{
	}
	uint64_t Size() const override
	{
		return reader_.Size();
	}
	Error Read(uint64_t p, void *b, size_t n) const override
	{
		++work_.reads;
		work_.readBytes += n;
		return reader_.Read(p, b, n);
	}
};
class Prepared final : public CpuEvaluator
{
  public:
	Digest binding{};
	std::map<Id, std::shared_ptr<const CpuEvaluator>> fields, shadows;
	std::shared_ptr<const CpuEvaluator> attribution;
	Status AcceptsAsset(const ReconstructedLightingAsset &a) const override
	{
		Digest d;
		auto e = MetadataDigest(a, d);
		return e && d == binding ? Status::Ok : Status::Corrupt;
	}
	Status Supports(Capability c) const override
	{
		for (auto &p : fields)
			if (p.second->Supports(c) == Status::Ok)
				return Status::Ok;
		for (auto &p : shadows)
			if (p.second->Supports(c) == Status::Ok)
				return Status::Ok;
		return attribution ? attribution->Supports(c) : Status::Unavailable;
	}
	const CpuEvaluator *Field(Id id) const
	{
		if (!id)
			return fields.size() == 1 ? fields.begin()->second.get() : nullptr;
		auto i = fields.find(id);
		return i == fields.end() ? nullptr : i->second.get();
	}
	Status Indirect(const ReconstructedLightingAsset &a, const IrradianceQuery &q,
					LightingSample &o) const override
	{
		auto p = Field(q.field.id);
		return p ? p->Indirect(a, q, o)
				 : (!q.field.id && fields.size() > 1 ? Status::Ambiguous : Status::Unavailable);
	}
	Status IncomingRadiance(const ReconstructedLightingAsset &a, const RadianceQuery &q,
							LightingSample &o) const override
	{
		auto p = Field(q.field.id);
		return p ? p->IncomingRadiance(a, q, o)
				 : (!q.field.id && fields.size() > 1 ? Status::Ambiguous : Status::Unavailable);
	}
	Status ConditionalField(const ReconstructedLightingAsset &a, const ConditionalFieldQuery &q,
							ConditionalFieldSample &o) const override
	{
		auto p = Field(q.field.id);
		return p ? p->ConditionalField(a, q, o) : Status::Unavailable;
	}
	Status ShadowSupport(const ReconstructedLightingAsset &a, const ShadowSupportQuery &q,
						 ShadowSupportSample &o) const override
	{
		if (!q.dataset)
		{
			if (shadows.size() != 1)
				return shadows.empty() ? Status::Unavailable : Status::Ambiguous;
			return shadows.begin()->second->ShadowSupport(a, q, o);
		}
		auto p = shadows.find(q.dataset);
		return p == shadows.end() ? Status::Unavailable : p->second->ShadowSupport(a, q, o);
	}
	Status Visibility(const ReconstructedLightingAsset &a, const VisibilityQuery &q,
					  VisibilitySample &o) const override
	{
		return shadows.size() == 1 ? shadows.begin()->second->Visibility(a, q, o)
								   : shadows.empty() ? Status::Unavailable : Status::Ambiguous;
	}
	Status SkyVisibility(const ReconstructedLightingAsset &a, const SkyVisibilityQuery &q,
						 VisibilitySample &o) const override
	{
		return shadows.size() == 1 ? shadows.begin()->second->SkyVisibility(a, q, o)
								   : shadows.empty() ? Status::Unavailable : Status::Ambiguous;
	}
	Status Contribution(const ReconstructedLightingAsset &a, const ContributionQuery &q,
						ContributionSample &o) const override
	{
		return attribution ? attribution->Contribution(a, q, o) : Status::Unavailable;
	}
};
bool Derived(const Dataset &d)
{
	return d.encoding == "org.openjk.spatial-field" || d.encoding == "org.openjk.probe-geometry" ||
		   d.encoding == "org.openjk.probe-topology" ||
		   d.encoding == "org.openjk.static-shadow-support";
}
std::set<Id> Closure(const ReconstructedLightingAsset &a, const Selection &s)
{
	Need(s.fields.size() <= 64 && s.visibility.size() <= 64 && s.maxPreparedBytes,
		 Code::LimitExceeded, "Prepared selection budget");
	std::set<Id> ids, fields, shadow;
	for (auto i : s.fields)
	{
		Need(i.id && i.geometry != Digest{} && fields.insert(i.id).second, Code::InvalidArgument,
			 "Missing/duplicate field selection identity");
		auto f = std::find_if(a.transport.begin(), a.transport.end(),
							  [&](const TransportField &x) { return x.source.id == i.id; });
		Need(f != a.transport.end(), Code::Unsupported, "Selected field is absent");
		ids.insert(f->dataset);
		unsigned support = 0;
		for (auto &d : a.datasets)
			if (d.locator == "field-geometry:" + std::to_string(i.id) ||
				d.locator == "field-topology:" + std::to_string(i.id))
			{
				ids.insert(d.id);
				++support;
			}
		Need(support == 2, Code::Corrupt, "Missing field support descriptors");
	}
	for (auto i : s.visibility)
	{
		Need(i.id && i.geometry != Digest{} && shadow.insert(i.id).second, Code::InvalidArgument,
			 "Missing/duplicate visibility selection identity");
		ids.insert(i.id);
	}
	if (s.contributions)
		for (auto &d : a.datasets)
			if (!Derived(d))
				ids.insert(d.id);
	for (auto id : ids)
		Need(std::find_if(a.datasets.begin(), a.datasets.end(),
						  [&](const Dataset &d) { return d.id == id; }) != a.datasets.end(),
			 Code::Corrupt, "Missing selected dataset");
	return ids;
}
Error Prepare(const Fingerprint &f, const ReconstructedLightingAsset &a,
			  const std::vector<Chunk> &chunks, const Selection &s, BuildProduct &out, Work &w,
			  const Limits &limits)
{
	try
	{
		auto ids = Closure(a, s);
		uint64_t payload = 0;
		for (auto &c : chunks)
		{
			Need(ids.count(c.dataset), Code::Corrupt, "Unexpected selected chunk");
			Need(c.bytes.size() <= limits.maxDecodedBytes - payload, Code::LimitExceeded,
				 "Selected payload limit");
			payload += c.bytes.size();
		}
		// Decoder validation and immutable copies coexist. Reserve pessimistically;
		// individual codecs enforce their own record/decoded allocation limits too.
		std::vector<uint8_t> metadata;
		auto encoded = Encode(a, f, {}, metadata, limits);
		if (!encoded)
			return encoded;
		uint64_t copies =
			8 + 4 * (s.fields.size() + s.visibility.size() + unsigned(s.contributions));
		Need(metadata.size() <= s.maxPreparedBytes / copies, Code::LimitExceeded,
			 "Prepared metadata copy reservation exceeded");
		uint64_t reserved = metadata.size() * copies;
		Need(payload <= (s.maxPreparedBytes - reserved) / 8, Code::LimitExceeded,
			 "Prepared resident/scratch reservation exceeded");
		w.preparedBudgetBytes = reserved + payload * 8;
		Core::ReadContext read;
		read.maxBytes = s.maxPreparedBytes;
		auto p = std::make_shared<Prepared>();
		auto hashed = MetadataDigest(a, p->binding, limits);
		if (!hashed)
			return hashed;
		for (auto i : s.fields)
		{
			Core::Fields::Artifact artifact;
			Check(Core::Fields::Decode(a, chunks, i.id, i.geometry, read, artifact));
			std::shared_ptr<const Core::Fields::Prepared> field;
			Check(Core::Fields::Prepare(artifact, read, field));
			p->fields[i.id] = field;
		}
		Digest map{};
		for (auto &i : f.inputs)
			if (i.kind == InputKind::Bsp)
				map = i.sha256;
		for (auto i : s.visibility)
		{
			Core::ShadowCache::Artifact artifact;
			Check(Core::ShadowCache::Decode(a, chunks, i.id, map, i.geometry, read, artifact));
			std::shared_ptr<const Core::ShadowCache::Prepared> shadow;
			Check(Core::ShadowCache::Prepare(artifact, read, shadow));
			p->shadows[i.id] = shadow;
		}
		if (s.contributions)
		{
			Core::Producer::Artifact artifact;
			artifact.asset = std::make_shared<const ReconstructedLightingAsset>(a);
			artifact.fingerprint = f;
			auto e = MakeKey(f, artifact.key, limits);
			if (!e)
				return e;
			artifact.chunks = chunks;
			std::shared_ptr<const Core::Attribution::Prepared> attribution;
			Check(Core::Attribution::Prepare(artifact, read, attribution));
			p->attribution = attribution;
		}
		BuildProduct result;
		result.asset = std::make_shared<const ReconstructedLightingAsset>(a);
		result.completeness = Completeness::Partial;
		if (!s.fields.empty() || !s.visibility.empty() || s.contributions)
			result.evaluator = p;
		result.message =
			"Validated resident CPU cache selection; no inverse fitting or geometry tracing";
		out = std::move(result);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Prepared cache allocation");
	}
}
} // namespace
Error Load(const Cache &cache, const Fingerprint &f, const Selection &s, BuildProduct &out, Work &w,
		   const Limits &limits)
{
	out = {};
	w = {};
	auto start = std::chrono::steady_clock::now();
	try
	{
		ReconstructedLightingAsset a;
		Index index;
		std::unique_ptr<FileReader> file;
		auto bounded = limits;
		bounded.maxMetadataBytes = std::min(bounded.maxMetadataBytes, s.maxPreparedBytes);
		bounded.maxDecodedBytes = std::min(bounded.maxDecodedBytes, s.maxPreparedBytes);
		auto e = cache.Load(f, a, index, file, bounded);
		if (!e)
			return e;
		w.reads = file->ReadCalls();
		w.readBytes = file->ReadBytes();
		Counting reader(*file, w);
		w.fileBytes = reader.Size();
		w.openSeconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		auto ids = Closure(a, s);
		std::vector<Chunk> chunks;
		for (auto &section : index.sections)
			if (section.type == unsigned(Section::Chunk) && ids.count(section.dataset))
			{
				Need(section.bytes <= limits.maxDecodedBytes - w.payloadBytes &&
						 section.bytes <= s.maxPreparedBytes / 8 - w.payloadBytes,
					 Code::LimitExceeded, "Selected chunk memory budget");
				Chunk c;
				c.dataset = section.dataset;
				c.firstElement = section.firstElement;
				c.count = section.count;
				e = ReadChunk(reader, index, c.dataset, c.firstElement, c.bytes, limits);
				if (!e)
					return e;
				w.payloadBytes += c.bytes.size();
				++w.chunks;
				chunks.push_back(std::move(c));
			}
		start = std::chrono::steady_clock::now();
		e = Prepare(f, a, chunks, s, out, w, limits);
		w.prepareSeconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		return e;
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Selective cache allocation");
	}
}
Error Store(const Cache &cache, const Fingerprint &f, const ReconstructedLightingAsset &a,
			const std::vector<Chunk> &chunks, const Selection &s, const Limits &limits)
{
	try
	{
		auto ids = Closure(a, s);
		std::vector<Chunk> selected;
		for (auto &c : chunks)
			if (ids.count(c.dataset))
				selected.push_back(c);
		BuildProduct p;
		Work w;
		auto e = Prepare(f, a, selected, s, p, w, limits);
		if (!e)
			return e;
		return cache.Store(f, a, chunks, limits);
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Cache validation allocation");
	}
}
Error LoadProbes(const Cache &cache, const Fingerprint &f, Id geo, Id top, const Digest &geometry,
				 std::shared_ptr<const Core::Probes::Topology> &out, Work &w, const Limits &limits)
{
	out.reset();
	w = {};
	auto start = std::chrono::steady_clock::now();
	try
	{
		Need(geo && top && geo != top && geometry != Digest{}, Code::InvalidArgument,
			 "Missing probe dependency identity");
		ReconstructedLightingAsset a;
		Index index;
		std::unique_ptr<FileReader> file;
		auto e = cache.Load(f, a, index, file, limits);
		if (!e)
			return e;
		w.fileBytes = file->Size();
		w.readBytes = file->ReadBytes();
		w.reads = file->ReadCalls();
		w.openSeconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		Counting reader(*file, w);
		std::vector<Chunk> chunks;
		for (auto &section : index.sections)
			if (section.type == unsigned(Section::Chunk) &&
				(section.dataset == geo || section.dataset == top))
			{
				Need(section.bytes <= limits.maxDecodedBytes / 8 - w.payloadBytes,
					 Code::LimitExceeded, "Probe payload reservation exceeded");
				Chunk c;
				c.dataset = section.dataset;
				c.firstElement = section.firstElement;
				c.count = section.count;
				e = ReadChunk(reader, index, c.dataset, c.firstElement, c.bytes, limits);
				if (!e)
					return e;
				w.payloadBytes += c.bytes.size();
				++w.chunks;
				chunks.push_back(std::move(c));
			}
		std::sort(chunks.begin(), chunks.end(), [](const Chunk &a, const Chunk &b) {
			return a.dataset == b.dataset ? a.firstElement < b.firstElement : a.dataset < b.dataset;
		});
		for (auto id : {geo, top})
		{
			auto d = std::find_if(a.datasets.begin(), a.datasets.end(),
								  [&](const Dataset &v) { return v.id == id; });
			Need(d != a.datasets.end(), Code::Corrupt, "Missing probe dataset descriptor");
			Need(d->encoding ==
						 (id == geo ? "org.openjk.probe-geometry" : "org.openjk.probe-topology") &&
					 d->encodingVersion == Core::Probes::kVersion,
				 Code::Unsupported, "Unsupported probe encoding/version");
			Core::GeometryKey hash;
			uint64_t count = 0;
			for (auto &c : chunks)
				if (c.dataset == id)
				{
					hash.U(c.firstElement);
					hash.U(c.count);
					hash.Digest(Hash(c.bytes.data(), c.bytes.size()));
					count += c.count;
				}
			Need(count == d->elementCount && d->contentHash == Hex(hash.Finish()), Code::Corrupt,
				 "Probe descriptor/content mismatch");
		}
		start = std::chrono::steady_clock::now();
		Core::ReadContext read;
		read.maxBytes = limits.maxDecodedBytes;
		std::shared_ptr<const Core::Probes::Topology> prepared;
		Check(Core::Probes::Decode(chunks, geometry, read, prepared));
		w.preparedBudgetBytes = 8 * w.payloadBytes;
		w.prepareSeconds =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		out = std::move(prepared);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "Probe cache allocation");
	}
}
} // namespace Persistent
} // namespace Disk
} // namespace Rend2LightRecon
