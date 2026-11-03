// Versioned CPU field datasets; decoding is explicit before publication.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "field_internal.h"
#include <cstring>
namespace Rend2LightRecon
{
namespace Core
{
namespace Fields
{
namespace
{
struct Writer
{
	std::vector<uint8_t> b;
	void U(uint64_t x)
	{
		for (unsigned j = 0; j < 8; ++j)
			b.push_back(uint8_t(x >> (8 * j)));
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
	void S(const std::string &s)
	{
		U(s.size());
		b.insert(b.end(), s.begin(), s.end());
	}
	void D(const Disk::Digest &d)
	{
		b.insert(b.end(), d.begin(), d.end());
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
		Need(at <= b.size() && b.size() - at >= 8, Code::InvalidInput, "Truncated field chunk");
		uint64_t u = 0;
		for (unsigned j = 0; j < 8; ++j)
			u |= uint64_t(b[at++]) << (8 * j);
		return u;
	}
	double F()
	{
		auto u = U();
		double x;
		std::memcpy(&x, &u, 8);
		Need(std::isfinite(x), Code::InvalidInput, "Nonfinite field number");
		return x;
	}
	Vec3 V3()
	{
		float x = float(F()), y = float(F()), z = float(F());
		Vec3 v(x, y, z);
		Need(Finite(v), Code::InvalidInput, "Field coordinate overflow");
		return v;
	}
	std::string S()
	{
		auto n = U();
		Need(n <= 65536 && at <= b.size() && n <= b.size() - at, Code::InvalidInput,
			 "Field metadata string bound");
		std::string s(b.begin() + at, b.begin() + at + size_t(n));
		at += size_t(n);
		return s;
	}
	Disk::Digest D()
	{
		Need(at <= b.size() && b.size() - at >= 32, Code::InvalidInput,
			 "Truncated field source dependency");
		Disk::Digest d;
		std::copy_n(b.data() + at, 32, d.begin());
		at += 32;
		return d;
	}
	void Seal()
	{
		Need(at == b.size() - 32, Code::InvalidInput, "Trailing field records");
	}
};
Disk::Digest DatasetKey(const std::vector<Disk::Chunk> &chunks, Id id)
{
	Core::GeometryKey k;
	for (auto &c : chunks)
		if (c.dataset == id)
		{
			k.U(c.firstElement);
			k.U(c.count);
			k.Digest(Disk::Hash(c.bytes.data(), c.bytes.size()));
		}
	return k.Finish();
}
void Descriptor(Writer &w, const ConditionalFieldDescriptor &d)
{
	w.U(d.field);
	w.U(d.dataset);
	w.U(d.calibration);
	w.U(d.configuration);
	w.U(d.alternativeGroup);
	w.U(unsigned(d.quantity));
	w.U(unsigned(d.domain));
	w.U(unsigned(d.unit));
	w.U(unsigned(d.colour));
	w.U(d.primariesKnown);
	w.U(d.linearTransferKnown);
	w.U(d.uncertainty);
	w.S(d.profile);
	w.S(d.encoding);
	w.S(d.provenance);
	w.S(d.topology);
	w.S(d.geometry);
	w.U(d.representationError.known);
	w.F(d.representationError.value);
	w.F(d.geometryVolumeWorldCubed);
	w.F(d.lightingVolumeWorldCubed);
	w.U(d.requestedProbes);
	w.U(d.availableProbes);
}
void Descriptor(Reader &r, ConditionalFieldDescriptor &d)
{
	d.field = r.U();
	d.dataset = r.U();
	d.calibration = r.U();
	d.configuration = r.U();
	d.alternativeGroup = r.U();
	auto quantity = r.U(), domain = r.U(), unit = r.U(), colour = r.U(), primaries = r.U(),
		 linear = r.U(), issues = r.U();
	Need(quantity <= unsigned(ConditionalQuantity::LegacyAppearance) &&
			 domain <= unsigned(FieldDomain::LegacyAppearance) &&
			 unit <= unsigned(PhotometricUnit::Lux) &&
			 colour <= unsigned(ContributionColour::EncodedSourceRGB) && primaries <= 1 &&
			 linear <= 1 && issues <= ((AttributionAlternative << 1) - 1),
		 Code::InvalidInput, "Field descriptor enums");
	d.quantity = ConditionalQuantity(quantity);
	d.domain = FieldDomain(domain);
	d.unit = PhotometricUnit(unit);
	d.colour = ContributionColour(colour);
	d.primariesKnown = primaries;
	d.linearTransferKnown = linear;
	d.uncertainty = uint32_t(issues);
	d.profile = r.S();
	d.encoding = r.S();
	d.provenance = r.S();
	d.topology = r.S();
	d.geometry = r.S();
	auto known = r.U();
	Need(known <= 1, Code::InvalidInput, "Field error flag");
	d.representationError.known = known;
	d.representationError.value = float(r.F());
	Need(d.representationError.value >= 0 && std::isfinite(d.representationError.value),
		 Code::InvalidInput, "Field error estimate");
	d.geometryVolumeWorldCubed = r.F();
	d.lightingVolumeWorldCubed = r.F();
	d.requestedProbes = r.U();
	d.availableProbes = r.U();
	Need(d.lightingVolumeWorldCubed >= 0 &&
			 d.geometryVolumeWorldCubed >= d.lightingVolumeWorldCubed &&
			 d.availableProbes <= d.requestedProbes,
		 Code::InvalidInput, "Invalid field spatial coverage");
}
} // namespace
Error Encode(Artifact &a, const ReadContext &read)
{
	try
	{
		Need(a.topology && a.descriptor, Code::InvalidInput, "Missing field payload");
		Need(a.sourceScene != Disk::Digest{} && a.sourceScene == SourceIdentity(a.asset),
			 Code::InvalidState, "Cannot encode stale source-conditioned field");
		MetadataBinding(a.asset, read.maxBytes ? read.maxBytes : 128ull << 20);
		auto asset = a.asset;
		auto descriptor = *a.descriptor;
		auto field = Field(asset, descriptor.field);
		Need(field && field->dataset == descriptor.dataset, Code::InvalidInput,
			 "Field canonical identity");
		Id geo = 0, top = 0;
		for (auto &d : asset.datasets)
			if (d.locator == "field-geometry:" + std::to_string(descriptor.field))
				geo = d.id;
			else if (d.locator == "field-topology:" + std::to_string(descriptor.field))
				top = d.id;
		Need(geo && top, Code::InvalidInput, "Probe geometry dataset allocation missing");
		std::vector<Disk::Chunk> chunks;
		Check(Probes::Encode(*a.topology, geo, top, read, chunks));
		Writer meta;
		meta.U(0x31444c4549464b4aull);
		meta.U(kVersion);
		meta.U(geo);
		meta.U(top);
		meta.D(a.sourceScene);
		Descriptor(meta, descriptor);
		meta.Seal();
		Disk::Chunk header;
		header.dataset = descriptor.dataset;
		header.count = 1;
		header.bytes = std::move(meta.b);
		chunks.push_back(std::move(header));
		std::vector<const ProbeDiagnostic *> qualityIndex;
		for (const auto &d : a.diagnostics)
			qualityIndex.push_back(&d);
		std::sort(
			qualityIndex.begin(), qualityIndex.end(),
			[](const ProbeDiagnostic *x, const ProbeDiagnostic *y) { return x->probe < y->probe; });
		for (size_t offset = 0; offset < a.values.records.size(); offset += 64)
		{
			Need(!Cancelled(read), Code::Cancelled, "Field encoding cancelled");
			Writer w;
			w.U(0x31564c4549464b4aull);
			w.U(kVersion);
			size_t end = std::min(a.values.records.size(), offset + 64);
			for (size_t i = offset; i < end; ++i)
			{
				auto &v = a.values.records[i];
				std::vector<uint8_t> angular;
				Need(Angular::Encode(v.angular, Angular::Storage::Float32, angular) ==
						 Angular::Code::Ok,
					 Code::InvalidInput, "Field angular encoding unsupported");
				w.U(v.probe);
				w.V3(v.evaluatedPositionWorld);
				w.U(angular.size());
				w.b.insert(w.b.end(), angular.begin(), angular.end());
				auto found =
					std::lower_bound(qualityIndex.begin(), qualityIndex.end(), v.probe,
									 [](const ProbeDiagnostic *d, Id id) { return d->probe < id; });
				const ProbeDiagnostic *diagnostic =
					found != qualityIndex.end() && (*found)->probe == v.probe ? *found : nullptr;
				bool quality = diagnostic && diagnostic->referenceKnown &&
							   descriptor.quantity != ConditionalQuantity::LegacyAppearance;
				w.U(quality);
				w.F(quality ? diagnostic->relativeRMSE : 0);
				w.F(quality ? diagnostic->maximumError : 0);
				w.F(quality ? diagnostic->energyBias : 0);
				w.F(quality ? diagnostic->luminanceRMSE : 0);
				w.U(diagnostic ? diagnostic->issues : 0);
			}
			w.Seal();
			Disk::Chunk c;
			c.dataset = descriptor.dataset;
			c.firstElement = offset + 1;
			c.count = end - offset;
			c.bytes = std::move(w.b);
			chunks.push_back(std::move(c));
		}
		uint64_t bytes = 0;
		for (auto &c : chunks)
			bytes += c.bytes.size();
		Need(bytes <= (read.maxBytes ? read.maxBytes : 128ull << 20), Code::MemoryLimit,
			 "Field payload budget");
		for (auto &d : asset.datasets)
			if (d.id == geo || d.id == top || d.id == descriptor.dataset)
			{
				d.contentHash = Disk::Hex(DatasetKey(chunks, d.id));
				d.layout = DatasetLayout::Chunked;
				d.chunkElements = d.id == descriptor.dataset ? 64 : 128;
				d.elementCount = d.id == geo ? a.topology->Probes().size()
											 : d.id == top ? a.topology->Cells().size()
														   : a.values.records.size() + 1;
			}
		std::string why;
		auto assetValid = Validate(asset, &why);
		Need(assetValid, Code::InvalidInput, why.c_str());
		a.asset = std::move(asset);
		a.chunks = std::move(chunks);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Field codec allocation");
	}
}
Error Decode(const ReconstructedLightingAsset &asset, const std::vector<Disk::Chunk> &chunks,
			 Id field, const Disk::Digest &geometryKey, const ReadContext &read, Artifact &out)
{
	out = {};
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Field decode cancelled");
		MetadataBinding(asset, read.maxBytes ? read.maxBytes : 128ull << 20);
		std::string why;
		auto assetValid = Validate(asset, &why);
		Need(assetValid, Code::InvalidInput, why.c_str());
		const auto *f = Field(asset, field);
		Need(f, Code::InvalidInput, "Missing canonical field");
		auto dataset = std::find_if(asset.datasets.begin(), asset.datasets.end(),
									[&](const Dataset &d) { return d.id == f->dataset; });
		Need(dataset != asset.datasets.end() && dataset->encoding == "org.openjk.spatial-field" &&
				 dataset->encodingVersion == kVersion,
			 Code::UnsupportedSchema, "Field encoding/version mismatch");
		uint64_t bytes = 0;
		std::vector<const Disk::Chunk *> values;
		for (auto &c : chunks)
		{
			bytes += c.bytes.size();
			if (c.dataset == f->dataset)
				values.push_back(&c);
		}
		Need(bytes <= (read.maxBytes ? read.maxBytes : 128ull << 20), Code::MemoryLimit,
			 "Field decode budget");
		std::sort(values.begin(), values.end(), [](const Disk::Chunk *a, const Disk::Chunk *b) {
			return a->firstElement < b->firstElement;
		});
		Need(!values.empty() && values[0]->firstElement == 0 && values[0]->count == 1,
			 Code::InvalidInput, "Missing field descriptor chunk");
		auto check = [](const Disk::Chunk &c) {
			Need(c.bytes.size() >= 48 && c.bytes.size() <= (4ull << 20), Code::InvalidInput,
				 "Field chunk bounds");
			auto key = Disk::Hash(c.bytes.data(), c.bytes.size() - 32);
			Need(std::equal(key.begin(), key.end(), c.bytes.end() - 32), Code::InvalidInput,
				 "Corrupt field checksum");
		};
		check(*values[0]);
		Reader meta(values[0]->bytes);
		Need(meta.U() == 0x31444c4549464b4aull && meta.U() == kVersion, Code::UnsupportedSchema,
			 "Field header mismatch");
		Id geo = meta.U(), top = meta.U();
		out.sourceScene = meta.D();
		Need(out.sourceScene == SourceIdentity(asset), Code::InvalidState,
			 "Cached field source geometry/photometry/configuration changed");
		auto descriptor = std::make_shared<ConditionalFieldDescriptor>();
		Descriptor(meta, *descriptor);
		meta.Seal();
		Need(descriptor->field == field && descriptor->dataset == f->dataset &&
				 descriptor->geometry == Disk::Hex(geometryKey),
			 Code::InvalidState, "Field geometry mismatch");
		descriptor->membership = f->membership;
		std::vector<Disk::Chunk> support;
		for (auto &c : chunks)
			if (c.dataset == geo || c.dataset == top)
				support.push_back(c);
		Check(Probes::Decode(support, geometryKey, read, out.topology));
		Need(descriptor->topology == Disk::Hex(out.topology->Key()), Code::InvalidState,
			 "Field topology generation mismatch");
		for (Id id : {geo, top, f->dataset})
		{
			auto d = std::find_if(asset.datasets.begin(), asset.datasets.end(),
								  [&](const Dataset &x) { return x.id == id; });
			Need(d != asset.datasets.end() && d->contentHash == Disk::Hex(DatasetKey(chunks, id)),
				 Code::InvalidInput, "Field dataset content identity mismatch");
		}
		uint64_t first = 1;
		Id previous = 0;
		out.values.field = field;
		out.values.topology = out.topology->Key();
		for (size_t k = 1; k < values.size(); ++k)
		{
			Need(!Cancelled(read), Code::Cancelled, "Field decode cancelled");
			auto &c = *values[k];
			Need(c.firstElement == first && c.count && c.count <= 64, Code::InvalidInput,
				 "Missing/duplicate field value chunk");
			check(c);
			Reader r(c.bytes);
			Need(r.U() == 0x31564c4549464b4aull && r.U() == kVersion, Code::UnsupportedSchema,
				 "Field value version");
			for (uint64_t j = 0; j < c.count; ++j)
			{
				Probes::Value v;
				v.probe = r.U();
				Need(v.probe > previous, Code::InvalidInput, "Duplicate/unordered probe values");
				previous = v.probe;
				v.evaluatedPositionWorld = r.V3();
				auto n = r.U();
				Need(n <= 8192 && r.at <= c.bytes.size() && n <= c.bytes.size() - r.at,
					 Code::InvalidInput, "Angular block bounds");
				std::vector<uint8_t> angular(c.bytes.begin() + r.at,
											 c.bytes.begin() + r.at + size_t(n));
				r.at += size_t(n);
				Need(Angular::Decode(angular, v.angular) == Angular::Code::Ok, Code::InvalidInput,
					 "Corrupt angular values");
				auto quality = r.U();
				Need(quality <= 1, Code::InvalidInput, "Field quality flag");
				ProbeDiagnostic diagnostic;
				diagnostic.probe = v.probe;
				diagnostic.state = ProbeState::Available;
				diagnostic.referenceKnown = quality != 0;
				diagnostic.relativeRMSE = r.F();
				diagnostic.maximumError = r.F();
				diagnostic.energyBias = r.F();
				diagnostic.luminanceRMSE = r.F();
				auto issues = r.U();
				Need(diagnostic.relativeRMSE >= 0 && diagnostic.maximumError >= 0 &&
						 diagnostic.luminanceRMSE >= 0 && issues <= UINT32_MAX,
					 Code::InvalidInput, "Invalid field quality values");
				diagnostic.issues = uint32_t(issues);
				out.diagnostics.push_back(diagnostic);
				out.values.records.push_back(std::move(v));
				Need(out.values.records.size() <= out.topology->Probes().size(), Code::MemoryLimit,
					 "Field record budget");
			}
			r.Seal();
			first += c.count;
		}
		Need(first == dataset->elementCount, Code::InvalidInput, "Incomplete field predictions");
		out.asset = asset;
		out.descriptor = descriptor;
		out.chunks = chunks;
		out.partial = out.values.records.size() != out.topology->Probes().size() ||
					  descriptor->domain != FieldDomain::PhysicallyAuthored ||
					  descriptor->uncertainty;
		std::shared_ptr<const Prepared> prepared;
		Check(Prepare(out, read, prepared));
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
		return Failure(Code::MemoryLimit, "Field decode allocation");
	}
}
} // namespace Fields
} // namespace Core
} // namespace Rend2LightRecon
