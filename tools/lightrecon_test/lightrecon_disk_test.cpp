// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "lightrecon/disk.h"
#include "lightrecon/cache.h"
#include "tr_lightrecon_manager.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace Rend2LightRecon;
namespace D = Rend2LightRecon::Disk;
namespace
{
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
void Ok(const D::Error &e)
{
	CHECK(bool(e));
	if (!e)
		std::printf("  disk error %d: %s\n", int(e.code), e.message.c_str());
}
Estimate Known(float x)
{
	Estimate e;
	e.known = true;
	e.value = x;
	return e;
}
D::Fingerprint Fingerprint()
{
	D::Fingerprint f;
	f.algorithm = "org.openjk.test/no-solver";
	f.algorithmVersion = 7;
	f.complete.fill(true);
	for (unsigned k = 0; k < unsigned(D::InputKind::Count); ++k)
	{
		D::InputContent i;
		i.kind = static_cast<D::InputKind>(k);
		i.name = "test-content/" + std::to_string(k);
		i.bytes = i.name.size();
		i.sha256 = D::Hash(i.name.data(), i.name.size());
		f.inputs.push_back(i);
	}
	return f;
}
Source SourceRecord(Id id)
{
	Source s;
	s.id = id;
	s.provenance = 1;
	s.regions.push_back(40);
	s.hypothesisWeight = Known(0.6f);
	s.evidence.transportConfidence = Known(0.8f);
	return s;
}
ReconstructedLightingAsset Populated()
{
	ReconstructedLightingAsset a;
	a.lineage = "test/lineage";
	a.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::Authored;
	p.producer = "disk-test/1";
	p.locator = "authored://test";
	p.contentHash = "sha256:test";
	p.method = "synthetic";
	a.provenanceRecords.push_back(p);
	p.id = 2;
	p.kind = ProvenanceKind::LegacyLightmap;
	p.parents.push_back(1);
	p.observationIssues = ObservationClipped | ObservationMissing | ObservationUnsupportedMaterial;
	p.observationQuality = 38;
	a.provenanceRecords.push_back(p);
	for (unsigned k = 0; k < 3; ++k)
	{
		Calibration c;
		c.id = 11 + k;
		c.kind = static_cast<CalibrationKind>(k);
		c.provenance = k == 1 ? 2 : 1;
		c.method = "synthetic reference scale, no measured legacy units";
		a.calibrations.push_back(c);
	}
	GeometryRegion region;
	region.id = 40;
	region.provenance = 1;
	region.key = "room/a";
	region.bounds.known = true;
	region.bounds.maxWorld = Vec3(10, 20, 30);
	region.membership = 36;
	a.regions.push_back(region);
	HypothesisGroup g;
	g.id = 90;
	g.description = "candidate suns";
	a.hypothesisGroups.push_back(g);
	for (unsigned k = 1; k <= 5; ++k)
	{
		PhysicalEmitter e;
		e.source = SourceRecord(100 + k);
		e.kind = static_cast<EmitterKind>(k);
		e.position.known = k != 5;
		e.position.world = Vec3(10, -20, 30);
		e.position.sigmaWorld = Known(2);
		e.source.evidence.physicalEmitterConfidence = Known(0.7f);
		e.source.evidence.observationIssues = ObservationQuantized;
		e.source.evidence.approximateIrradiance = Suitability::Candidate;
		e.frame.known = true;
		e.geometry.known = true;
		e.geometry.halfWidthWorld = 4;
		e.geometry.halfHeightWorld = 2;
		e.geometry.halfLengthWorld = 5;
		e.geometry.radiusWorld = 0.5f;
		e.geometry.effectiveRadiusWorld = Known(0.2f);
		e.emission.calibration = k == 2 ? 12 : 11;
		e.emission.intensity = Known(1000);
		e.emission.intensity.bounded = true;
		e.emission.intensity.lower = 900;
		e.emission.intensity.upper = 1100;
		CHECK(NormalizeChromaticity(Vec3(1, 0.5f, 0.1f), e.emission.color));
		e.emission.unit = k < 3 ? PhotometricUnit::Candela
								: k == 5 ? PhotometricUnit::Lux : PhotometricUnit::Nits;
		e.angular.kind = k == 1 ? AngularKind::Isotropic
								: k == 2 ? AngularKind::SmoothCone
										 : k == 5 ? AngularKind::Parallel : AngularKind::Lambertian;
		if (k != 5)
			e.rangeWorld = Known(150);
		else
		{
			e.source.hypothesisGroup = 90;
			e.geometry.angularRadiusRadians = Known(0.005f);
		}
		a.emitters.push_back(e);
	}
	SunSource sun;
	sun.emitter = 105;
	a.suns.push_back(sun);
	// Unknown relative source remains distinct from a zero-valued physical light.
	PhysicalEmitter unknown;
	unknown.source = SourceRecord(106);
	unknown.emission.calibration = 13;
	unknown.source.evidence.observationIssues = ObservationMissing | ObservationUnreliable;
	unknown.emission.intensity.bounded = true;
	unknown.emission.intensity.upper = 100;
	a.emitters.push_back(unknown);
	const DatasetPurpose purposes[] = {
		DatasetPurpose::Transport,		   DatasetPurpose::Visibility,
		DatasetPurpose::Contributions,	   DatasetPurpose::SampleDomain,
		DatasetPurpose::Environment,	   DatasetPurpose::AngularProfile,
		DatasetPurpose::RegionMembership,  DatasetPurpose::EmitterGeometry,
		DatasetPurpose::ObservationQuality};
	for (unsigned i = 0; i < 9; ++i)
	{
		Dataset d;
		d.id = 30 + i;
		d.purpose = purposes[i];
		d.locator = "chunk://" + std::to_string(d.id);
		d.encoding = "org.openjk.test/opaque";
		d.contentHash = "sha256:external-content";
		d.elementCount = 4;
		d.chunkElements = 2;
		a.datasets.push_back(d);
	}
	a.emitters[1].angular.kind = AngularKind::Tabulated;
	a.emitters[1].angular.dataset = 35;
	PhysicalEmitter future;
	future.source = SourceRecord(107);
	future.kind = static_cast<EmitterKind>(0x10001);
	future.extensionType = "org.example.future-emitter/1";
	future.geometry.extension = 37;
	a.emitters.push_back(future);
	EnvironmentLighting sky;
	sky.source = SourceRecord(300);
	sky.meanLuminance.calibration = 13;
	sky.radiance = 34;
	sky.sunCoverage = SunCoverage::ExcludesExplicitSun;
	a.environments.push_back(sky);
	TransportField field;
	field.source = SourceRecord(200);
	field.source.evidence.exactLight = Suitability::Unsuitable;
	field.calibration = 13;
	field.bounds = region.bounds;
	field.dataset = 30;
	field.sampleDimensions[0] = 1;
	field.sampleDimensions[1] = 2;
	field.sampleDimensions[2] = 2;
	a.transport.push_back(field);
	VisibilityReference v;
	v.dataset = 31;
	v.source = 101;
	v.region = 40;
	v.kind = VisibilityKind::SkyTransmittance;
	a.visibility.push_back(v);
	ContributionReference c;
	c.dataset = 32;
	c.sampleDomain = 33;
	c.sources = {101, 200};
	c.calibration = 13;
	c.component = ContributionComponent::Indirect;
	a.contributions.push_back(c);
	Diagnostic d;
	d.severity = Severity::Warning;
	d.code = "legacy-clipped";
	d.message = "synthetic uncalibrated observation";
	d.source = 106;
	d.region = 40;
	d.metric = Known(-0.3f);
	d.metricUnit = "relative-residual";
	a.diagnostics.push_back(d);
	std::string error;
	CHECK(Validate(a, &error));
	if (!error.empty())
		std::printf("%s\n", error.c_str());
	return a;
}
std::vector<D::Chunk> Chunks()
{
	std::vector<D::Chunk> chunks;
	for (Id id : {30ull, 31ull, 32ull})
		for (unsigned first : {0u, 2u})
		{
			D::Chunk c;
			c.dataset = id;
			c.firstElement = first;
			c.count = 2;
			c.bytes = {0, 13, 10, uint8_t(id), uint8_t(first), 255};
			chunks.push_back(c);
		}
	return chunks;
}
uint64_t Get(const std::vector<uint8_t> &b, size_t at, unsigned n = 8)
{
	uint64_t x = 0;
	for (unsigned i = 0; i < n; ++i)
		x |= uint64_t(b[at + i]) << (8 * i);
	return x;
}
void Put(std::vector<uint8_t> &b, size_t at, uint64_t x, unsigned n = 8)
{
	for (unsigned i = 0; i < n; ++i)
		b[at + i] = uint8_t(x >> (8 * i));
}
void RehashHeader(std::vector<uint8_t> &b)
{
	std::fill(b.begin() + 144, b.begin() + 176, uint8_t(0));
	auto h = D::Hash(b.data(), 192);
	std::copy(h.begin(), h.end(), b.begin() + 144);
}
void RehashDirectory(std::vector<uint8_t> &b)
{
	auto h = D::Hash(b.data() + 192, size_t(Get(b, 28, 4)) * 80);
	std::copy(h.begin(), h.end(), b.begin() + 112);
	RehashHeader(b);
}
size_t Entry(const std::vector<uint8_t> &b, D::Section section)
{
	for (size_t i = 0; i < Get(b, 28, 4); ++i)
		if (Get(b, 192 + 80 * i, 4) == uint32_t(section))
			return 192 + 80 * i;
	return 0;
}
void RehashSection(std::vector<uint8_t> &b, size_t entry)
{
	auto h = D::Hash(b.data() + Get(b, entry + 24), size_t(Get(b, entry + 32)));
	std::copy(h.begin(), h.end(), b.begin() + entry + 48);
	RehashDirectory(b);
}
D::Error Decode(const std::vector<uint8_t> &b, ReconstructedLightingAsset &out, D::Index &index,
				const D::Limits &limits = D::Limits())
{
	D::MemoryReader r(b.data(), b.size());
	return D::Open(r, Fingerprint(), out, index, limits);
}
struct TrackingReader : D::Reader
{
	D::MemoryReader memory;
	mutable size_t calls = 0;
	mutable uint64_t largest = 0, total = 0;
	explicit TrackingReader(const std::vector<uint8_t> &b) : memory(b.data(), b.size()) {}
	uint64_t Size() const override
	{
		return memory.Size();
	}
	D::Error Read(uint64_t offset, void *out, size_t n) const override
	{
		++calls;
		largest = std::max(largest, uint64_t(n));
		total += n;
		return memory.Read(offset, out, n);
	}
};
struct FailedReader : D::Reader
{
	uint64_t Size() const override
	{
		return 1024;
	}
	D::Error Read(uint64_t, void *, size_t) const override
	{
		D::Error e;
		e.code = D::Code::IoError;
		e.message = "synthetic read failure";
		return e;
	}
};
void HashAndKeys()
{
	CHECK(D::Hex(D::Hash(nullptr, 0)) ==
		  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(D::Hex(D::Hash("abc", 3)) ==
		  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	std::string million(1000000, 'a');
	CHECK(D::Hex(D::Hash(million.data(), million.size())) ==
		  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	std::vector<uint8_t> content(million.begin(), million.end());
	TrackingReader stream(content);
	D::Digest streamed{};
	Ok(D::HashContent(stream, streamed, content.size()));
	CHECK(streamed == D::Hash(content.data(), content.size()) && stream.largest <= 65536 &&
		  stream.calls == 16);
	CHECK(D::HashContent(stream, streamed, 100).code == D::Code::LimitExceeded);
	FailedReader failed;
	CHECK(D::HashContent(failed, streamed, 1024).code == D::Code::IoError);
	auto f = Fingerprint();
	D::Digest key{}, other{};
	Ok(D::MakeKey(f, key));
	std::reverse(f.inputs.begin(), f.inputs.end());
	Ok(D::MakeKey(f, other));
	CHECK(key == other);
	for (size_t i = 0; i < f.complete.size(); ++i)
	{
		auto missing = f;
		missing.complete[i] = false;
		CHECK(D::MakeKey(missing, other).code == D::Code::NotCacheable);
		CHECK(other == key);
	}
	for (size_t i = 0; i < f.inputs.size(); ++i)
	{
		auto changed = f;
		changed.inputs[i].sha256[0] ^= 1;
		Ok(D::MakeKey(changed, other));
		CHECK(other != key);
	}
	f = Fingerprint();
	f.algorithmVersion++;
	Ok(D::MakeKey(f, other));
	CHECK(other != key);
	f = Fingerprint();
	f.algorithm += "/other";
	Ok(D::MakeKey(f, other));
	CHECK(other != key);
	f = Fingerprint();
	f.units.worldUnitsPerMeter = 64;
	Ok(D::MakeKey(f, other));
	CHECK(other != key);
	f = Fingerprint();
	f.units.physicalToScene = 0.01f;
	Ok(D::MakeKey(f, other));
	CHECK(other != key);
	f = Fingerprint();
	f.inputs[0].sha256.fill(0);
	CHECK(D::MakeKey(f, other).code == D::Code::NotCacheable);
	f = Fingerprint();
	f.inputs.push_back(f.inputs[0]);
	CHECK(D::MakeKey(f, other).code == D::Code::NotCacheable);
	f = Fingerprint();
	f.inputs.erase(f.inputs.begin());
	CHECK(D::MakeKey(f, other).code == D::Code::NotCacheable);
	f = Fingerprint();
	f.inputs.erase(f.inputs.begin() + 2);
	Ok(D::MakeKey(f, other)); // explicitly absent external set
}
void RoundTrips()
{
	for (const auto &asset : {ReconstructedLightingAsset(), Populated()})
	{
		std::vector<uint8_t> bytes, again;
		auto chunks = asset.emitters.empty() ? std::vector<D::Chunk>() : Chunks();
		Ok(D::Encode(asset, Fingerprint(), chunks, bytes));
		ReconstructedLightingAsset out;
		D::Index index;
		Ok(Decode(bytes, out, index));
		Ok(D::Encode(out, Fingerprint(), chunks, again));
		CHECK(bytes == again);
		CHECK(out.emitters.size() == asset.emitters.size());
		CHECK(out.transport.size() == asset.transport.size());
		CHECK(out.lineage == asset.lineage);
		TrackingReader reader(bytes);
		Ok(D::Open(reader, Fingerprint(), out, index));
		CHECK(reader.calls ==
			  index.sections.size() - chunks.size() + 2); // header + directory + metadata only
		for (const auto &chunk : chunks)
		{
			std::vector<uint8_t> decoded;
			Ok(D::ReadChunk(reader, index, chunk.dataset, chunk.firstElement, decoded));
			CHECK(decoded == chunk.bytes);
		}
		std::vector<uint8_t> sentinel{77};
		CHECK(D::ReadChunk(reader, index, 999, 0, sentinel).code == D::Code::Missing);
		CHECK(sentinel == std::vector<uint8_t>{77});
		if (!asset.emitters.empty())
		{
			CHECK(out.emitters[4].kind == EmitterKind::Directional && out.suns[0].emitter == 105);
			CHECK(!out.emitters[5].emission.intensity.known &&
				  out.emitters[5].emission.intensity.bounded);
			CHECK(out.emitters[6].extensionType == "org.example.future-emitter/1");
			CHECK(out.environments[0].radiance == 34 && out.transport[0].source.id == 200);
			CHECK(out.contributions[0].sources[1] == 200 && out.visibility[0].source == 101);
			CHECK(out.calibrations[1].kind == CalibrationKind::LegacySceneEquivalent &&
				  out.emitters[1].emission.unit == PhotometricUnit::Candela);
			CHECK(out.provenanceRecords[1].parents == std::vector<Id>{1} &&
				  out.provenanceRecords[1].observationQuality == 38);
			CHECK(out.diagnostics[0].metric.value == -0.3f);
			auto reordered = asset;
			std::reverse(reordered.emitters.begin(), reordered.emitters.end());
			std::reverse(reordered.datasets.begin(), reordered.datasets.end());
			std::reverse(chunks.begin(), chunks.end());
			Ok(D::Encode(reordered, Fingerprint(), chunks, again));
			CHECK(bytes == again);
		}
		std::printf("  %zu emitters: %zu encoded bytes, %zu sections\n", asset.emitters.size(),
					bytes.size(), index.sections.size());
		if (asset.emitters.empty())
		{
			auto digest = D::Hex(D::Hash(bytes.data(), bytes.size()));
			// Schema 3 adds joint alternatives and typed uncertainty. Schema-2 empty
			// witness was 935a8078...; both earlier semantic schemas are rejected.
			CHECK(kSchemaVersion == 3);
			CHECK(digest == "bdf7dceba59265891c5df61840f03610d81049d144b7163413d3c8ab487e35ef");
			std::printf("  empty format SHA-256: %s\n", digest.c_str());
		}
	}
}
void CorruptionAndLimits()
{
	std::vector<uint8_t> bytes;
	Ok(D::Encode(Populated(), Fingerprint(), Chunks(), bytes));
	ReconstructedLightingAsset out;
	out.lineage = "sentinel";
	D::Index index;
	index.key.fill(77);
	auto reject = [&](const std::vector<uint8_t> &b, D::Code code) {
		auto e = Decode(b, out, index);
		CHECK(e.code == code);
		CHECK(out.lineage == "sentinel" && index.key[0] == 77);
	};
	for (size_t n = 0; n < 192; ++n)
		reject(std::vector<uint8_t>(bytes.begin(), bytes.begin() + n), D::Code::Corrupt);
	for (size_t n = 192; n < bytes.size(); n += 53)
		reject(std::vector<uint8_t>(bytes.begin(), bytes.begin() + n), D::Code::Corrupt);
	auto b = bytes;
	b[0] ^= 1;
	reject(b, D::Code::Corrupt);
	b = bytes;
	b[48] ^= 1;
	reject(b, D::Code::Corrupt); // header checksum
	b = bytes;
	Put(b, 8, 2, 2);
	RehashHeader(b);
	reject(b, D::Code::Unsupported);
	b = bytes;
	Put(b, 12, 160, 4);
	RehashHeader(b);
	reject(b, D::Code::Corrupt);
	for (size_t at : {16u, 20u})
	{
		b = bytes;
		Put(b, at, 99, 4);
		RehashHeader(b);
		reject(b, D::Code::Unsupported);
	}
	b=bytes;
	Put(b,16,1,4); // valid envelope/hash, obsolete canonical schema
	RehashHeader(b);
	reject(b,D::Code::Unsupported);
	b=bytes;
	Put(b,16,2,4); // old passive-transport schema, valid envelope/hash
	RehashHeader(b);
	reject(b,D::Code::Unsupported);
	b = bytes;
	Put(b, 40, std::numeric_limits<uint64_t>::max());
	RehashHeader(b);
	reject(b, D::Code::Corrupt);
	b = bytes;
	b[192] ^= 1;
	reject(b, D::Code::Corrupt); // directory checksum
	size_t emitter = Entry(bytes, D::Section::Emitters), meta = Entry(bytes, D::Section::Metadata),
		   diagnostic = Entry(bytes, D::Section::Diagnostics);
	for (uint64_t offset : {0ull, 193ull, std::numeric_limits<uint64_t>::max() - 7})
	{
		b = bytes;
		Put(b, emitter + 24, offset);
		RehashDirectory(b);
		reject(b, D::Code::Corrupt);
	}
	b = bytes;
	Put(b, emitter + 32, std::numeric_limits<uint64_t>::max());
	RehashDirectory(b);
	reject(b, D::Code::Corrupt);
	b = bytes;
	Put(b, emitter + 40, std::numeric_limits<uint64_t>::max());
	RehashDirectory(b);
	reject(b, D::Code::LimitExceeded);
	b = bytes;
	b[size_t(Get(b, emitter + 24))] ^= 1;
	reject(b, D::Code::Corrupt);
	b = bytes;
	Put(b, size_t(Get(b, emitter + 24)) + 8, 999);
	RehashSection(b, emitter);
	reject(b, D::Code::Corrupt); // dangling provenance
	b = bytes;
	size_t visibility = Entry(b, D::Section::Visibility);
	Put(b, size_t(Get(b, visibility + 24)) + 12, 999);
	RehashSection(b, visibility);
	reject(b, D::Code::Corrupt); // dangling source, with valid payload/directory checksums
	// Non-finite unit float, including with a valid checksum.
	b = bytes;
	size_t p = size_t(Get(b, meta + 24));
	p += 4 + size_t(Get(b, p, 4)) + 4;
	p += 4 + size_t(Get(b, p, 4));
	Put(b, p, 0x7fc00000, 4);
	RehashSection(b, meta);
	reject(b, D::Code::Corrupt);
	// Unknown optional section skipped; unknown required section rejected.
	b = bytes;
	Put(b, diagnostic, 90, 4);
	RehashDirectory(b);
	ReconstructedLightingAsset accepted;
	D::Index parsed;
	Ok(Decode(b, accepted, parsed));
	CHECK(accepted.diagnostics.empty() && accepted.emitters.size() == 7);
	Put(b, 10, 9, 2);
	RehashHeader(b);
	Ok(Decode(b, accepted, parsed)); // compatible future minor
	Put(b, diagnostic + 4, 1, 4);
	RehashDirectory(b);
	reject(b, D::Code::Unsupported);
	// Deferred chunk integrity: Open does not read or pretend to validate payload.
	b = bytes;
	size_t chunk = Entry(b, D::Section::Chunk);
	b[size_t(Get(b, chunk + 24))] ^= 1;
	Ok(Decode(b, accepted, parsed));
	D::MemoryReader memory(b.data(), b.size());
	std::vector<uint8_t> payload{77};
	CHECK(D::ReadChunk(memory, parsed, 30, 0, payload).code == D::Code::Corrupt);
	CHECK(payload[0] == 77);
	b = bytes;
	Put(b, chunk + 8, 999);
	RehashDirectory(b);
	reject(b, D::Code::Corrupt);
	b = bytes;
	Put(b, chunk + 16, std::numeric_limits<uint64_t>::max());
	RehashDirectory(b);
	reject(b, D::Code::Corrupt);
	for (unsigned limit = 0; limit < 7; ++limit)
	{
		D::Limits l;
		switch (limit)
		{
		case 0:
			l.maxFileBytes = 100;
			break;
		case 1:
			l.maxMetadataBytes = 10;
			break;
		case 2:
			l.maxDecodedBytes = 100;
			break;
		case 3:
			l.maxRecords = 2;
			break;
		case 4:
			l.maxAllocations = 2;
			break;
		case 5:
			l.maxSections = 2;
			break;
		case 6:
			l.maxStringBytes = 5;
			break;
		}
		CHECK(Decode(bytes, out, index, l).code == D::Code::LimitExceeded);
		CHECK(out.lineage == "sentinel");
	}
	D::Limits l;
	l.maxChunkBytes = 1;
	CHECK(Decode(bytes, out, index, l).code == D::Code::LimitExceeded);
	D::MemoryReader r(bytes.data(), bytes.size());
	auto fp = Fingerprint();
	fp.inputs[2].sha256[0] ^= 1;
	CHECK(D::Open(r, fp, out, index).code == D::Code::Mismatch);
	TrackingReader tracking(bytes);
	fp.complete[0] = false;
	CHECK(D::Open(tracking, fp, out, index).code == D::Code::NotCacheable);
	CHECK(tracking.calls == 0);
	FailedReader failed;
	CHECK(D::Open(failed, Fingerprint(), out, index).code == D::Code::IoError);
	auto invalid = Populated();
	invalid.emitters[0].position.world.x = std::numeric_limits<float>::infinity();
	std::vector<uint8_t> sentinel{77};
	CHECK(D::Encode(invalid, Fingerprint(), {}, sentinel).code == D::Code::InvalidAsset);
	CHECK(sentinel[0] == 77);
	invalid = Populated();
	invalid.emitters[2].geometry.halfWidthWorld = 0;
	CHECK(D::Encode(invalid, Fingerprint(), {}, sentinel).code == D::Code::InvalidAsset);
	invalid = Populated();
	invalid.units.worldUnitsPerMeter = 64;
	CHECK(D::Encode(invalid, Fingerprint(), {}, sentinel).code == D::Code::InvalidAsset);
	invalid = Populated();
	invalid.emitters.back().emission.unit = static_cast<PhotometricUnit>(99);
	CHECK(D::Encode(invalid, Fingerprint(), {}, sentinel).code == D::Code::InvalidAsset);
	// Fixed-seed mutations exercise rejection without any manager publication.
	uint32_t random = 0x12345678;
	for (unsigned n = 0; n < 256; ++n)
	{
		random = random * 1664525 + 1013904223;
		b = bytes;
		size_t at = random % 192;
		b[at] ^= uint8_t(1u << (n % 8));
		CHECK(!Decode(b, out, index));
	}
}
void NativeCache(const std::string &root)
{
	CHECK(D::CacheRoot("relative").empty());
	CHECK(D::CacheRoot(root + "/../escape").empty());
	CHECK(!D::CacheRoot(root).empty());
	D::Cache cache(root);
	auto fp = Fingerprint();
	D::Digest key;
	Ok(D::MakeKey(fp, key));
	std::string filename = root + "/" + D::Hex(key) + ".jklr";
	std::remove(filename.c_str());
	ReconstructedLightingAsset out;
	out.lineage = "sentinel";
	D::Index index;
	std::unique_ptr<D::FileReader> file;
	auto a = Populated();
	Manager owner;
	CHECK(owner.Publish(a) == Status::Ok);
	const auto pinned = owner.Acquire();
	const auto before = owner.Acquire().Generation();
	CHECK(cache.Load(fp, out, index, file).code == D::Code::Missing);
	CHECK(out.lineage == "sentinel" && !file);
	Ok(cache.Store(fp, a, Chunks()));
	Ok(cache.Load(fp, out, index, file));
	CHECK(out.lineage == a.lineage);
	// Replace while an old immutable file lease remains open (also on Windows).
	a.lineage = "test/replacement";
	Ok(cache.Store(fp, a, Chunks()));
	std::vector<uint8_t> payload;
	Ok(D::ReadChunk(*file, index, 30, 0, payload));
	CHECK(payload == Chunks()[0].bytes);
	std::FILE *interrupted = std::fopen((filename + ".tmp-crash").c_str(), "wb");
	CHECK(interrupted != nullptr);
	if (interrupted)
	{
		std::fputs("incomplete", interrupted);
		std::fclose(interrupted);
	}
	std::unique_ptr<D::FileReader> next;
	Ok(cache.Load(fp, out, index, next));
	CHECK(out.lineage == "test/replacement");
	std::remove((filename + ".tmp-crash").c_str());
	a.units.worldUnitsPerMeter = -1;
	CHECK(cache.Store(fp, a).code == D::Code::InvalidAsset);
	Ok(cache.Load(fp, out, index, next));
	CHECK(out.lineage == "test/replacement");
	file.reset();
	next.reset();
	std::FILE *corrupt = std::fopen(filename.c_str(), "wb");
	CHECK(corrupt != nullptr);
	if (corrupt)
	{
		std::fputs("truncated", corrupt);
		std::fclose(corrupt);
	}
	out.lineage = "sentinel";
	CHECK(cache.Load(fp, out, index, file).code == D::Code::Corrupt);
	CHECK(out.lineage == "sentinel" && !file);
	fp.complete[1] = false;
	CHECK(cache.Load(fp, out, index, file).code == D::Code::NotCacheable);
	CHECK(cache.Store(fp, ReconstructedLightingAsset()).code == D::Code::NotCacheable);
	CHECK(owner.Acquire().Generation() == before &&
		  owner.Acquire().State() == Availability::Partial);
	CHECK(pinned.Asset() && pinned.Asset().value->lineage == "test/lineage");
	std::remove(filename.c_str());
}
} // namespace
int main(int argc, char **argv)
{
	HashAndKeys();
	RoundTrips();
	CorruptionAndLimits();
	if (argc != 2)
	{
		std::printf("Expected an absolute test-only cache directory argument\n");
		return 2;
	}
	NativeCache(argv[1]);
	std::printf("lightrecon disk: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
