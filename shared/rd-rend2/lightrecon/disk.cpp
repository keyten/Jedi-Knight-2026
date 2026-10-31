// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "disk.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace Rend2LightRecon
{
namespace Disk
{
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
			  "IEEE-754 binary32 required");
namespace
{
Error Fail(Code c, const std::string &text)
{
	Error e;
	e.code = c;
	e.message = text;
	return e;
}
struct Failure
{
	Error error;
};
void Require(bool ok, Code c, const char *text)
{
	if (!ok)
		throw Failure{Fail(c, text)};
}
constexpr uint8_t magic[8] = {'J', 'K', 'R', 'L', 'A', 13, 10, 26};
uint32_t Rot(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}
// SHA-256, FIPS 180-4. Explicit byte operations, independent of host endianness.
struct Sha
{
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
					 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	uint8_t block[64]{};
	size_t used = 0;
	uint64_t length = 0;
	void Transform()
	{
		static const uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
			0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
			0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
			0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
			0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
			0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
			0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
			0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
			0xc67178f2};
		uint32_t w[64];
		for (unsigned i = 0; i < 16; ++i)
			w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) |
				   (uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
		for (unsigned i = 16; i < 64; ++i)
			w[i] = w[i - 16] + (Rot(w[i - 15], 7) ^ Rot(w[i - 15], 18) ^ (w[i - 15] >> 3)) +
				   w[i - 7] + (Rot(w[i - 2], 17) ^ Rot(w[i - 2], 19) ^ (w[i - 2] >> 10));
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], v = h[7];
		for (unsigned i = 0; i < 64; ++i)
		{
			uint32_t t =
				v + (Rot(e, 6) ^ Rot(e, 11) ^ Rot(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			uint32_t u = (Rot(a, 2) ^ Rot(a, 13) ^ Rot(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			v = g;
			g = f;
			f = e;
			e = d + t;
			d = c;
			c = b;
			b = a;
			a = t + u;
		}
		const uint32_t v8[8] = {a, b, c, d, e, f, g, v};
		for (unsigned i = 0; i < 8; ++i)
			h[i] += v8[i];
	}
	void Add(const void *p, size_t n)
	{
		const auto *bytes = static_cast<const uint8_t *>(p);
		length += n;
		while (n)
		{
			size_t take = std::min(n, 64 - used);
			std::memcpy(block + used, bytes, take);
			used += take;
			bytes += take;
			n -= take;
			if (used == 64)
			{
				Transform();
				used = 0;
			}
		}
	}
	Digest Finish()
	{
		uint64_t bits = length * 8;
		uint8_t one = 128, zero = 0;
		Add(&one, 1);
		while (used != 56)
			Add(&zero, 1);
		uint8_t tail[8];
		for (unsigned i = 0; i < 8; ++i)
			tail[7 - i] = uint8_t(bits >> (8 * i));
		Add(tail, 8);
		Digest out{};
		for (unsigned i = 0; i < 32; ++i)
			out[i] = uint8_t(h[i / 4] >> (24 - 8 * (i % 4)));
		return out;
	}
};
struct Budget
{
	const Limits &limits;
	uint64_t bytes = 0, allocations = 0, records = 0;
	explicit Budget(const Limits &l) : limits(l) {}
	void Take(uint64_t n, uint64_t allocationsNeeded = 1)
	{
		Require(n <= limits.maxDecodedBytes - bytes &&
					allocationsNeeded <= limits.maxAllocations - allocations,
				Code::LimitExceeded, "decoded allocation budget");
		bytes += n;
		allocations += allocationsNeeded;
	}
	void Records(uint64_t n)
	{
		Require(n <= limits.maxRecords - records, Code::LimitExceeded, "record count budget");
		records += n;
	}
};

// One field order for both directions; there is no native struct dumping.
#define FIELDS(T, ...)                                                                             \
	template <class A> void Fields(A &a, T &v)                                                     \
	{                                                                                              \
		a(__VA_ARGS__);                                                                            \
	}
FIELDS(Vec3, v.x, v.y, v.z)
FIELDS(Estimate, v.known, v.value, v.bounded, v.lower, v.upper)
FIELDS(Position, v.known, v.world, v.sigmaWorld)
FIELDS(Frame, v.known, v.right, v.up, v.forward)
FIELDS(Units, v.worldUnitsPerMeter, v.physicalToScene)
FIELDS(Provenance, v.id, v.kind, v.locator, v.contentHash, v.producer, v.method, v.parents,
	   v.observationIssues, v.observationQuality)
FIELDS(Calibration, v.id, v.kind, v.provenance, v.method)
FIELDS(Chromaticity, v.known, v.rgb)
FIELDS(Photometry, v.unit, v.intensity, v.color, v.calibration)
FIELDS(HypothesisGroup, v.id, v.description)
FIELDS(Evidence, v.physicalEmitterConfidence, v.transportConfidence, v.observationIssues,
	   v.exactLight, v.approximateIrradiance)
FIELDS(Source, v.id, v.provenance, v.hypothesisGroup, v.state, v.hypothesisWeight, v.regions,
	   v.evidence)
FIELDS(EmitterGeometry, v.known, v.halfWidthWorld, v.halfHeightWorld, v.halfLengthWorld,
	   v.radiusWorld, v.effectiveRadiusWorld, v.angularRadiusRadians, v.extension)
FIELDS(AngularDistribution, v.kind, v.cosInner, v.cosOuter, v.twoSided, v.dataset)
FIELDS(PhysicalEmitter, v.source, v.kind, v.extensionType, v.position, v.frame, v.geometry,
	   v.emission, v.angular, v.rangeWorld)
FIELDS(SunSource, v.emitter)
FIELDS(Bounds, v.known, v.minWorld, v.maxWorld)
FIELDS(GeometryRegion, v.id, v.provenance, v.key, v.bounds, v.membership)
FIELDS(Dataset, v.id, v.purpose, v.layout, v.locator, v.contentHash, v.encoding, v.encodingVersion,
	   v.elementCount, v.chunkElements)
FIELDS(EnvironmentLighting, v.source, v.meanLuminance, v.radiance, v.sunCoverage)
FIELDS(TransportRoot, v.source, v.kind)
FIELDS(TransportMembership, v.version, v.model, v.roots, v.directLocal, v.directSun, v.directSky,
	   v.legacyAmbient, v.reflected, v.unresolved, v.reflectionOrderKnown,
	   v.minimumReflectionOrder, v.maximumReflectionOrder, v.style)
FIELDS(TransportField, v.source, v.component, v.quantity, v.unit, v.calibration, v.bounds,
	   v.sampleDimensions[0], v.sampleDimensions[1], v.sampleDimensions[2], v.dataset, v.membership)
FIELDS(VisibilityReference, v.dataset, v.kind, v.source, v.region)
FIELDS(ContributionReference, v.dataset, v.sampleDomain, v.sources, v.component, v.quantity, v.unit,
	   v.calibration)
FIELDS(Diagnostic, v.severity, v.code, v.message, v.source, v.region, v.metric, v.metricUnit)
FIELDS(UncertaintyFactor,v.cause,v.status,v.reason)
FIELDS(CalibratedConfidence,v.meaning,v.probability,v.event,v.population,v.protocol,v.calibrationHash,v.independentFamilies,v.intervalNominal)
FIELDS(JointParameterState,v.track,v.root,v.kind,v.position,v.frame,v.dimensions,v.angular,v.amplitude,v.amplitudeDomain,v.legacySpotDistanceWorld,v.legacySpotRadiusWorld)
FIELDS(MaterialParameterState,v.material,v.reflectance,v.status)
FIELDS(LightingConfiguration,v.id,v.provenance,v.key,v.profile,v.calibrationHypothesis,v.sources,v.materials,v.observationGain,v.firstBounce,v.searchComplete)
FIELDS(CorrelatedPositionScatter,v.known,v.reference,v.xx,v.xy,v.xz,v.yy,v.yz,v.zz,v.samples,v.method)
FIELDS(SourceUncertainty,v.id,v.source,v.track,v.provenance,v.version,v.method,v.inputHash,v.configurations,v.factors,v.existence,v.type,v.exactUse,v.fieldUse,v.positionScatter,v.relativePeak,v.directionSpreadRadians,v.peakColourBounded,v.peakColourLower,v.peakColourUpper,v.absoluteScaleKnown,v.complete,v.minimumCount,v.maximumCount,v.exactLight,v.approximateField)
FIELDS(InputContent, v.kind, v.name, v.bytes, v.sha256)
#undef FIELDS
struct Writer
{
	std::vector<uint8_t> bytes;
	const Limits &limits;
	explicit Writer(const Limits &l) : limits(l) {}
	void Raw(const void *p, size_t n)
	{
		Require(n <= limits.maxFileBytes - bytes.size(), Code::LimitExceeded,
				"encoded file budget");
		if (n)
		{
			const auto *b = static_cast<const uint8_t *>(p);
			bytes.insert(bytes.end(), b, b + n);
		}
	}
	void Value(uint64_t v)
	{
		uint8_t b[8];
		for (unsigned i = 0; i < 8; ++i)
			b[i] = uint8_t(v >> (8 * i));
		Raw(b, 8);
	}
	void Value(uint32_t v)
	{
		uint8_t b[4];
		for (unsigned i = 0; i < 4; ++i)
			b[i] = uint8_t(v >> (8 * i));
		Raw(b, 4);
	}
	void Value(uint16_t v)
	{
		uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
		Raw(b, 2);
	}
	void Value(bool v)
	{
		uint8_t b = v ? 1 : 0;
		Raw(&b, 1);
	}
	void Value(float v)
	{
		Require(std::isfinite(v), Code::InvalidAsset, "non-finite float");
		uint32_t b;
		std::memcpy(&b, &v, 4);
		Value(b);
	}
	void Value(const Digest &v)
	{
		Raw(v.data(), v.size());
	}
	void Value(const std::string &v)
	{
		Require(v.size() <= limits.maxStringBytes, Code::LimitExceeded, "string budget");
		Value(uint32_t(v.size()));
		Raw(v.data(), v.size());
	}
	template <class T> typename std::enable_if<std::is_enum<T>::value>::type Value(T v)
	{
		Value(static_cast<uint32_t>(v));
	}
	template <class T> typename std::enable_if<!std::is_enum<T>::value>::type Value(const T &v)
	{
		Fields(*this, const_cast<T &>(v));
	}
	template <class T> void Value(const std::vector<T> &v)
	{
		Require(v.size() <= limits.maxRecords, Code::LimitExceeded, "vector count");
		Value(uint64_t(v.size()));
		for (const T &x : v)
			Value(x);
	}
	void operator()() {}
	template <class T, class... Rest> void operator()(const T &v, const Rest &... rest)
	{
		Value(v);
		(*this)(rest...);
	}
};
template <class T> bool ValidEnum(uint32_t value);
#define ENUM_RANGE(T, last)                                                                        \
	template <> bool ValidEnum<T>(uint32_t v)                                                      \
	{                                                                                              \
		return v <= uint32_t(T::last);                                                             \
	}
ENUM_RANGE(ProvenanceKind, External)
ENUM_RANGE(UncertaintyStatus,Contradicted)
ENUM_RANGE(UncertaintyCause,Numerical)
ENUM_RANGE(ConfidenceMeaning,IndependentEmpiricalBinRate)
ENUM_RANGE(FieldMembership, Included)
ENUM_RANGE(TransportRootKind, Unknown)
ENUM_RANGE(CalibrationKind, UncalibratedRelative)
ENUM_RANGE(PhotometricUnit, Lux) ENUM_RANGE(HypothesisState, Rejected)
	ENUM_RANGE(Suitability, Candidate) ENUM_RANGE(AngularKind, Tabulated)
		ENUM_RANGE(DatasetPurpose, ObservationQuality) ENUM_RANGE(DatasetLayout, Procedural)
			ENUM_RANGE(SunCoverage, IncludesExplicitSun)
				ENUM_RANGE(TransportComponent, UnresolvedIrradiance)
					ENUM_RANGE(FieldQuantity, Radiance) ENUM_RANGE(VisibilityKind, SkyTransmittance)
						ENUM_RANGE(ContributionComponent, Unresolved)
							ENUM_RANGE(ContributionQuantity, AttributionFraction)
								ENUM_RANGE(Severity, Error)
#undef ENUM_RANGE
									template <>
									bool ValidEnum<EmitterKind>(uint32_t v)
{
	return (v >= 1 && v <= 5) || v >= uint32_t(EmitterKind::Extension);
}
template <> bool ValidEnum<InputKind>(uint32_t v)
{
	return v < uint32_t(InputKind::Count);
}
struct Decoder
{
	const uint8_t *data;
	size_t size, pos = 0;
	Budget &budget;
	Decoder(const std::vector<uint8_t> &b, Budget &q) : data(b.data()), size(b.size()), budget(q) {}
	void Raw(void *p, size_t n)
	{
		Require(n <= size - pos, Code::Corrupt, "truncated record");
		if (n)
			std::memcpy(p, data + pos, n);
		pos += n;
	}
	void Value(uint64_t &v)
	{
		uint8_t b[8];
		Raw(b, 8);
		v = 0;
		for (unsigned i = 0; i < 8; ++i)
			v |= uint64_t(b[i]) << (8 * i);
	}
	void Value(uint32_t &v)
	{
		uint8_t b[4];
		Raw(b, 4);
		v = 0;
		for (unsigned i = 0; i < 4; ++i)
			v |= uint32_t(b[i]) << (8 * i);
	}
	void Value(uint16_t &v)
	{
		uint8_t b[2];
		Raw(b, 2);
		v = uint16_t(b[0] | (uint16_t(b[1]) << 8));
	}
	void Value(bool &v)
	{
		uint8_t b;
		Raw(&b, 1);
		Require(b <= 1, Code::Corrupt, "invalid bool");
		v = b != 0;
	}
	void Value(float &v)
	{
		uint32_t b;
		Value(b);
		std::memcpy(&v, &b, 4);
		Require(std::isfinite(v), Code::Corrupt, "non-finite float");
	}
	void Value(Digest &v)
	{
		Raw(v.data(), v.size());
	}
	void Value(std::string &v)
	{
		uint32_t n;
		Value(n);
		Require(n <= budget.limits.maxStringBytes && n <= size - pos, Code::LimitExceeded,
				"string size");
		if (n)
			budget.Take(uint64_t(n) + 1);
		v.assign(reinterpret_cast<const char *>(data + pos), n);
		pos += n;
	}
	template <class T> typename std::enable_if<std::is_enum<T>::value>::type Value(T &v)
	{
		uint32_t n;
		Value(n);
		Require(ValidEnum<T>(n), Code::Corrupt, "invalid enum");
		v = static_cast<T>(n);
	}
	template <class T> typename std::enable_if<!std::is_enum<T>::value>::type Value(T &v)
	{
		Fields(*this, v);
	}
	template <class T> void Value(std::vector<T> &v)
	{
		uint64_t n;
		Value(n);
		budget.Records(n);
		Require(n <= size - pos && n <= std::numeric_limits<size_t>::max() / sizeof(T),
				Code::LimitExceeded, "vector size");
		if (n)
			budget.Take(n * sizeof(T));
		v.resize(size_t(n));
		for (T &x : v)
			Value(x);
	}
	void operator()() {}
	template <class T, class... Rest> void operator()(T &v, Rest &... rest)
	{
		Value(v);
		(*this)(rest...);
	}
	void End()
	{
		Require(pos == size, Code::Corrupt, "trailing record data");
	}
};
void FingerprintFields(Writer &w, const Fingerprint &f)
{
	w(f.algorithm, f.algorithmVersion, f.units);
	for (bool b : f.complete)
		w.Value(b);
	std::vector<const InputContent *> sorted;
	for (const auto &i : f.inputs)
		sorted.push_back(&i);
	std::sort(sorted.begin(), sorted.end(), [](const InputContent *a, const InputContent *b) {
		return std::tie(a->kind, a->name) < std::tie(b->kind, b->name);
	});
	w.Value(uint64_t(sorted.size()));
	for (const auto *i : sorted)
		w.Value(*i);
}
void FingerprintFields(Decoder &d, Fingerprint &f)
{
	d(f.algorithm, f.algorithmVersion, f.units);
	for (auto &b : f.complete)
		d.Value(b);
	d.Value(f.inputs);
}
bool Zero(const Digest &d)
{
	for (uint8_t b : d)
		if (b)
			return false;
	return true;
}
void CheckFingerprint(const Fingerprint &f, const Limits &l)
{
	Require(!f.algorithm.empty() && f.algorithmVersion, Code::NotCacheable,
			"algorithm identity/version missing");
	Require(f.algorithm.size() <= l.maxStringBytes, Code::LimitExceeded, "algorithm string budget");
	Require(std::isfinite(f.units.worldUnitsPerMeter) && f.units.worldUnitsPerMeter > 0 &&
				std::isfinite(f.units.physicalToScene) && f.units.physicalToScene > 0,
			Code::NotCacheable, "unit/calibration scales missing or invalid");
	for (bool b : f.complete)
		Require(b, Code::NotCacheable, "incomplete input fingerprint coverage");
	Require(f.inputs.size() <= l.maxRecords, Code::LimitExceeded, "fingerprint count");
	std::vector<const InputContent *> seen;
	size_t counts[static_cast<size_t>(InputKind::Count)]{};
	for (const auto &i : f.inputs)
	{
		Require(static_cast<size_t>(i.kind) < static_cast<size_t>(InputKind::Count) &&
					!i.name.empty() && !Zero(i.sha256),
				Code::NotCacheable, "invalid/unhashed fingerprint input");
		Require(i.name.size() <= l.maxStringBytes, Code::LimitExceeded, "fingerprint name");
		++counts[static_cast<size_t>(i.kind)];
		seen.push_back(&i);
	}
	auto less = [](const InputContent *a, const InputContent *b) {
		return std::tie(a->kind, a->name) < std::tie(b->kind, b->name);
	};
	std::sort(seen.begin(), seen.end(), less);
	Require(std::adjacent_find(seen.begin(), seen.end(),
							   [&](const InputContent *a, const InputContent *b) {
								   return !less(a, b) && !less(b, a);
							   }) == seen.end(),
			Code::NotCacheable, "duplicate fingerprint input");
	Require(counts[0] == 1 && counts[static_cast<size_t>(InputKind::Settings)] &&
				counts[static_cast<size_t>(InputKind::UnitCalibration)],
			Code::NotCacheable, "missing BSP/settings/unit policy content");
}
template <class T> Id RecordId(const T &)
{
	return 0;
}
Id RecordId(const Provenance &v)
{
	return v.id;
}
Id RecordId(const Calibration &v)
{
	return v.id;
}
Id RecordId(const HypothesisGroup &v)
{
	return v.id;
}
Id RecordId(const PhysicalEmitter &v)
{
	return v.source.id;
}
Id RecordId(const EnvironmentLighting &v)
{
	return v.source.id;
}
Id RecordId(const TransportField &v)
{
	return v.source.id;
}
Id RecordId(const GeometryRegion &v)
{
	return v.id;
}
Id RecordId(const Dataset &v)
{
	return v.id;
}
Id RecordId(const SunSource &v)
{
	return v.emitter;
}
struct Encoded
{
	SectionInfo info;
	std::vector<uint8_t> bytes;
	const std::vector<uint8_t> *external = nullptr; // borrowed only during Encode
	const std::vector<uint8_t> &Data() const
	{
		return external ? *external : bytes;
	}
};
template <class T>
void Add(std::vector<Encoded> &sections, Section kind, const std::vector<T> &records,
		 const Limits &limits)
{
	if (records.empty())
		return;
	Require(records.size() <= limits.maxRecords, Code::LimitExceeded, "section records");
	std::vector<const T *> sorted;
	for (const auto &v : records)
		sorted.push_back(&v);
	std::stable_sort(sorted.begin(), sorted.end(),
					 [](const T *a, const T *b) { return RecordId(*a) < RecordId(*b); });
	Writer w(limits);
	for (const auto *v : sorted)
		w.Value(*v);
	uint64_t total = w.bytes.size();
	for (const auto &s : sections)
	{
		Require(s.bytes.size() <= limits.maxMetadataBytes - total, Code::LimitExceeded,
				"metadata budget");
		total += s.bytes.size();
	}
	Encoded e;
	e.info.type = uint32_t(kind);
	e.info.count = records.size();
	e.bytes.swap(w.bytes);
	sections.push_back(std::move(e));
}
template <class T> void DecodeRecords(Decoder &d, const SectionInfo &s, std::vector<T> &records)
{
	d.budget.Records(s.count);
	Require(s.count <= d.size && s.count <= std::numeric_limits<size_t>::max() / sizeof(T),
			Code::LimitExceeded, "section record count");
	if (s.count)
		d.budget.Take(s.count * sizeof(T));
	records.resize(size_t(s.count));
	for (T &v : records)
		d.Value(v);
}
auto Order(const SectionInfo &s) -> std::tuple<uint32_t, Id, uint64_t>
{
	return std::make_tuple(s.type, s.dataset, s.firstElement);
}
void Directory(Writer &w, const SectionInfo &s)
{
	w(s.type, s.flags, s.dataset, s.firstElement, s.offset, s.bytes, s.count, s.checksum);
}
void Directory(Decoder &d, SectionInfo &s)
{
	d(s.type, s.flags, s.dataset, s.firstElement, s.offset, s.bytes, s.count, s.checksum);
}
std::vector<uint8_t> ReadBytes(const Reader &r, uint64_t offset, uint64_t n, uint64_t max)
{
	Require(n <= max && n <= std::numeric_limits<size_t>::max(), Code::LimitExceeded,
			"section byte budget");
	Require(offset <= r.Size() && n <= r.Size() - offset, Code::Corrupt, "section outside file");
	std::vector<uint8_t> b(static_cast<size_t>(n));
	Error e = r.Read(offset, b.data(), b.size());
	if (!e)
		throw Failure{e};
	return b;
}
void CheckHash(const std::vector<uint8_t> &b, const Digest &expected)
{
	Require(Hash(b.data(), b.size()) == expected, Code::Corrupt, "SHA-256 mismatch");
}
void ValidationBudget(Budget &budget)
{
	// Conservative reservation for canonical validator's indexes, DAG, sets and
	// per-record field-path strings. Counts include nested reference lists.
	Require(budget.records <= std::numeric_limits<uint64_t>::max() / 1024, Code::LimitExceeded,
			"validation size overflow");
	budget.Take(budget.records * 1024, budget.records * 16);
}
template <class F> Error Guard(F f)
{
	try
	{
		f();
		return Error();
	}
	catch (const Failure &e)
	{
		return e.error;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "allocation failed");
	}
	catch (const std::length_error &)
	{
		return Fail(Code::LimitExceeded, "container size exceeded");
	}
}
} // namespace

Digest Hash(const void *data, size_t size)
{
	Sha s;
	s.Add(data, size);
	return s.Finish();
}
std::string Hex(const Digest &d)
{
	static const char h[] = "0123456789abcdef";
	std::string s;
	for (uint8_t b : d)
	{
		s += h[b >> 4];
		s += h[b & 15];
	}
	return s;
}
Error MemoryReader::Read(uint64_t offset, void *out, size_t n) const
{
	if (offset > size_ || n > size_ - offset || (n && (!data_ || !out)))
		return Fail(Code::Corrupt, "invalid memory read");
	if (n)
		std::memcpy(out, data_ + size_t(offset), n);
	return Error();
}
Error MakeKey(const Fingerprint &f, Digest &out, const Limits &limits)
{
	return Guard([&] {
		CheckFingerprint(f, limits);
		Limits l = limits;
		l.maxFileBytes = std::min(l.maxFileBytes, l.maxMetadataBytes);
		Writer w(l);
		w(std::string("org.openjk.lightrecon-key/1"), kFormatMajor, kSchemaVersion,
		  kConventionVersion);
		FingerprintFields(w, f);
		out = Hash(w.bytes.data(), w.bytes.size());
	});
}
Error HashContent(const Reader &reader, Digest &out, uint64_t maxInputBytes)
{
	return Guard([&] {
		const uint64_t size = reader.Size();
		Require(size <= maxInputBytes && size <= std::numeric_limits<uint64_t>::max() / 8,
				Code::LimitExceeded, "content hash input budget");
		Sha hash;
		uint8_t scratch[65536];
		for (uint64_t pos = 0; pos < size;)
		{
			size_t count = size_t(std::min(uint64_t(sizeof(scratch)), size - pos));
			Error e = reader.Read(pos, scratch, count);
			if (!e)
				throw Failure{e};
			hash.Add(scratch, count);
			pos += count;
		}
		out = hash.Finish();
	});
}

Error Encode(const ReconstructedLightingAsset &asset, const Fingerprint &fingerprint,
			 const std::vector<Chunk> &chunks, std::vector<uint8_t> &out, const Limits &limits)
{
	return Guard([&] {
		Digest key;
		Error k = MakeKey(fingerprint, key, limits);
		if (!k)
			throw Failure{k};
		Require(asset.units.worldUnitsPerMeter == fingerprint.units.worldUnitsPerMeter &&
					asset.units.physicalToScene == fingerprint.units.physicalToScene,
				Code::InvalidAsset, "asset units differ from fingerprint policy");
		Limits metadataLimits = limits;
		metadataLimits.maxFileBytes = std::min(limits.maxFileBytes, limits.maxMetadataBytes);
		std::vector<Encoded> sections;
		Writer meta(metadataLimits);
		meta(asset.schema, asset.version, asset.lineage, asset.units, asset.provenance);
		Encoded m;
		m.info.type = uint32_t(Section::Metadata);
		m.info.flags = 1;
		m.info.count = 1;
		m.bytes.swap(meta.bytes);
		sections.push_back(std::move(m));
		Writer fw(metadataLimits);
		FingerprintFields(fw, fingerprint);
		Encoded f;
		f.info.type = uint32_t(Section::Fingerprint);
		f.info.flags = 1;
		f.info.count = 1;
		f.bytes.swap(fw.bytes);
		sections.push_back(std::move(f));
#define ADD(kind, member) Add(sections, Section::kind, asset.member, metadataLimits)
		ADD(Provenance, provenanceRecords);
		ADD(Calibration, calibrations);
		ADD(Hypotheses, hypothesisGroups);
		ADD(Emitters, emitters);
		ADD(Sun, suns);
		ADD(Environment, environments);
		ADD(Transport, transport);
		ADD(Regions, regions);
		ADD(Datasets, datasets);
		ADD(Visibility, visibility);
		ADD(Contributions, contributions);
		ADD(Diagnostics, diagnostics);
		ADD(LightingConfigurations,lightingConfigurations);
		ADD(Uncertainty,uncertainty);
#undef ADD
		uint64_t metadata = 0;
		for (const auto &s : sections)
		{
			Require(s.bytes.size() <= limits.maxMetadataBytes - metadata, Code::LimitExceeded,
					"metadata budget");
			metadata += s.bytes.size();
		}
		Require(chunks.size() <= limits.maxSections &&
					sections.size() <= limits.maxSections - chunks.size(),
				Code::LimitExceeded, "section count");
		uint64_t overhead =
			kHeaderBytes + uint64_t(sections.size() + chunks.size()) * (kDirectoryEntryBytes + 7);
		Require(metadata <= std::numeric_limits<uint64_t>::max() - overhead, Code::LimitExceeded,
				"encoded size overflow");
		uint64_t total = metadata + overhead;
		Require(total <= limits.maxFileBytes, Code::LimitExceeded, "file budget");
		for (const auto &c : chunks)
		{
			Require(c.bytes.size() <= limits.maxFileBytes - total, Code::LimitExceeded,
					"chunk file budget");
			total += c.bytes.size();
		}
		for (const auto &c : chunks)
		{
			Require(!c.bytes.empty() && c.bytes.size() <= limits.maxChunkBytes, Code::LimitExceeded,
					"chunk bytes");
			Encoded s;
			s.info.type = uint32_t(Section::Chunk);
			s.info.dataset = c.dataset;
			s.info.firstElement = c.firstElement;
			s.info.count = c.count;
			s.external = &c.bytes;
			sections.push_back(std::move(s));
		}
		std::sort(sections.begin(), sections.end(),
				  [](const Encoded &a, const Encoded &b) { return Order(a.info) < Order(b.info); });
		Writer directory(limits);
		uint64_t offset = kHeaderBytes + uint64_t(sections.size()) * kDirectoryEntryBytes;
		for (auto &s : sections)
		{
			s.info.offset = offset;
			s.info.bytes = s.Data().size();
			s.info.checksum = Hash(s.Data().data(), s.Data().size());
			Directory(directory, s.info);
			Require(s.info.bytes <= limits.maxFileBytes &&
						offset <= limits.maxFileBytes - s.info.bytes,
					Code::LimitExceeded, "file size");
			offset += s.info.bytes;
			Require(offset <= std::numeric_limits<uint64_t>::max() - 7, Code::LimitExceeded,
					"alignment overflow");
			offset = (offset + 7) & ~uint64_t(7);
		}
		Digest map{};
		for (const auto &i : fingerprint.inputs)
			if (i.kind == InputKind::Bsp)
				map = i.sha256;
		Writer file(limits);
		file.Raw(magic, 8);
		file(kFormatMajor, kFormatMinor, kHeaderBytes, kSchemaVersion, kConventionVersion,
			 fingerprint.algorithmVersion, uint32_t(sections.size()), offset,
			 uint64_t(kHeaderBytes), map, key, Hash(directory.bytes.data(), directory.bytes.size()),
			 Digest{});
		uint8_t reserved[16]{};
		file.Raw(reserved, 16);
		Digest head = Hash(file.bytes.data(), file.bytes.size());
		std::copy(head.begin(), head.end(), file.bytes.begin() + 144);
		file.Raw(directory.bytes.data(), directory.bytes.size());
		for (const auto &s : sections)
		{
			file.Raw(s.Data().data(), s.Data().size());
			while (file.bytes.size() % 8)
			{
				uint8_t z = 0;
				file.Raw(&z, 1);
			}
		}
		// Reuse the untrusted decoder so encoding cannot bypass budgets, references,
		// geometry, schema or chunk-directory validation. Chunks themselves are opaque.
		MemoryReader reader(file.bytes.data(), file.bytes.size());
		ReconstructedLightingAsset checked;
		Index index;
		Error result = Open(reader, fingerprint, checked, index, limits);
		if (!result)
			throw Failure{Fail(result.code == Code::Corrupt ? Code::InvalidAsset : result.code,
							   result.message)};
		out.swap(file.bytes);
	});
}

Error Open(const Reader &reader, const Fingerprint &expected, ReconstructedLightingAsset &out,
		   Index &outIndex, const Limits &limits)
{
	return Guard([&] {
		Digest expectedKey;
		Error keyResult = MakeKey(expected, expectedKey, limits);
		if (!keyResult)
			throw Failure{keyResult};
		Require(reader.Size() >= kHeaderBytes, Code::Corrupt, "truncated header");
		Require(reader.Size() <= limits.maxFileBytes, Code::LimitExceeded, "file budget");
		Budget budget(limits);
		budget.Take(kHeaderBytes);
		auto header = ReadBytes(reader, 0, kHeaderBytes, kHeaderBytes);
		Require(std::equal(magic, magic + 8, header.begin()), Code::Corrupt, "invalid magic");
		Digest head;
		std::copy(header.begin() + 144, header.begin() + 176, head.begin());
		std::fill(header.begin() + 144, header.begin() + 176, 0);
		CheckHash(header, head);
		Decoder h(header, budget);
		uint8_t ignored[8];
		h.Raw(ignored, 8);
		uint16_t major, minor;
		uint32_t headerBytes, schema, convention, algorithm, count;
		uint64_t fileBytes, dirOffset;
		Index index;
		Digest directoryHash;
		h(major, minor, headerBytes, schema, convention, algorithm, count, fileBytes, dirOffset,
		  index.mapHash, index.key, directoryHash);
		(void)minor; // same-major extensions must use optional, independently framed sections
		Require(major == kFormatMajor && schema == kSchemaVersion &&
					convention == kConventionVersion,
				Code::Unsupported, "format/schema/convention version");
		Require(headerBytes == kHeaderBytes && dirOffset == kHeaderBytes &&
					fileBytes == reader.Size(),
				Code::Corrupt, "header sizes/offsets");
		for (size_t i = 176; i < 192; ++i)
			Require(header[i] == 0, Code::Unsupported, "reserved header flags");
		Require(count >= 2 && count <= limits.maxSections, Code::LimitExceeded, "directory count");
		Require(index.key == expectedKey && algorithm == expected.algorithmVersion, Code::Mismatch,
				"input/algorithm fingerprint mismatch");
		Digest map{};
		for (const auto &i : expected.inputs)
			if (i.kind == InputKind::Bsp)
				map = i.sha256;
		Require(map == index.mapHash, Code::Mismatch, "map content mismatch");
		uint64_t dirBytes = uint64_t(count) * kDirectoryEntryBytes;
		budget.Take(dirBytes);
		budget.Take(uint64_t(count) * sizeof(SectionInfo));
		auto directory = ReadBytes(reader, dirOffset, dirBytes, limits.maxMetadataBytes);
		CheckHash(directory, directoryHash);
		Decoder d(directory, budget);
		index.sections.resize(count);
		uint64_t end = dirOffset + dirBytes, metadata = 0;
		uint32_t previous = 0;
		for (auto &s : index.sections)
		{
			Directory(d, s);
			Require(!(s.flags & ~1u), Code::Unsupported, "unknown section flags");
			Require(s.offset % 8 == 0 && s.offset >= end && s.offset <= fileBytes && s.bytes &&
						s.bytes <= fileBytes - s.offset,
					Code::Corrupt, "overlapping/invalid section range");
			end = s.offset + s.bytes;
			if (previous)
				Require(Order(index.sections[previous - 1]) < Order(s), Code::Corrupt,
						"duplicate/unordered sections");
			++previous;
			bool known = s.type >= 1 && s.type <= 16;
			Require(known || s.type == uint32_t(Section::Chunk) || !(s.flags & 1),
					Code::Unsupported, "unknown required section");
			if (known)
			{
				Require(!s.dataset && !s.firstElement, Code::Corrupt, "metadata chunk identity");
				Require(s.bytes <= limits.maxMetadataBytes - metadata, Code::LimitExceeded,
						"metadata bytes");
				metadata += s.bytes;
			}
		}
		Require(index.sections[0].type == 1 && index.sections[1].type == 2 &&
					index.sections[0].count == 1 && index.sections[1].count == 1,
				Code::Corrupt, "missing mandatory metadata/fingerprint");
		ReconstructedLightingAsset asset;
		for (const auto &s : index.sections)
		{
			if (s.type > 16)
				continue;
			budget.Take(s.bytes);
			auto bytes = ReadBytes(reader, s.offset, s.bytes, limits.maxMetadataBytes);
			CheckHash(bytes, s.checksum);
			Decoder r(bytes, budget);
			switch (static_cast<Section>(s.type))
			{
			case Section::Metadata:
				r(asset.schema, asset.version, asset.lineage, asset.units, asset.provenance);
				break;
			case Section::Fingerprint:
			{
				Fingerprint f;
				FingerprintFields(r, f);
				Require(s.bytes <= std::numeric_limits<uint64_t>::max() / 3, Code::LimitExceeded,
						"fingerprint scratch overflow");
				budget.Take(s.bytes * 3, uint64_t(f.inputs.size()) * 4 + 128);
				Digest actual;
				Error result = MakeKey(f, actual, limits);
				Require(bool(result) && actual == index.key, Code::Corrupt,
						"stored fingerprint mismatch");
				break;
			}
#define READ(kind, member)                                                                         \
	case Section::kind:                                                                            \
		DecodeRecords(r, s, asset.member);                                                         \
		break
				READ(Provenance, provenanceRecords);
				READ(Calibration, calibrations);
				READ(Hypotheses, hypothesisGroups);
				READ(Emitters, emitters);
				READ(Sun, suns);
				READ(Environment, environments);
				READ(Transport, transport);
				READ(Regions, regions);
				READ(Datasets, datasets);
				READ(Visibility, visibility);
				READ(Contributions, contributions);
				READ(Diagnostics, diagnostics);
				READ(LightingConfigurations,lightingConfigurations);
				READ(Uncertainty,uncertainty);
#undef READ
			default:
				break;
			}
			r.End();
		}
		ValidationBudget(budget);
		std::string reason;
		if (!Validate(asset, &reason))
			throw Failure{Fail(Code::Corrupt, reason)};
		Require(asset.units.worldUnitsPerMeter == expected.units.worldUnitsPerMeter &&
					asset.units.physicalToScene == expected.units.physicalToScene,
				Code::Corrupt, "asset units differ from fingerprint policy");
		Id priorDataset = 0;
		uint64_t priorEnd = 0;
		for (const auto &s : index.sections)
			if (s.type == uint32_t(Section::Chunk))
			{
				auto found = std::find_if(asset.datasets.begin(), asset.datasets.end(),
										  [&](const Dataset &ds) { return ds.id == s.dataset; });
				Require(found != asset.datasets.end() &&
							found->layout != DatasetLayout::Procedural && s.count &&
							s.firstElement <= found->elementCount &&
							s.count <= found->elementCount - s.firstElement,
						Code::Corrupt, "invalid chunk dataset/range");
				Require(s.bytes <= limits.maxChunkBytes &&
							(found->layout != DatasetLayout::Chunked ||
							 s.count <= found->chunkElements),
						Code::LimitExceeded, "chunk budget");
				Require(priorDataset != s.dataset || s.firstElement >= priorEnd, Code::Corrupt,
						"overlapping logical chunks");
				priorDataset = s.dataset;
				priorEnd = s.firstElement + s.count;
			}
		out = std::move(asset);
		outIndex = std::move(index);
	});
}

Error ReadChunk(const Reader &reader, const Index &index, Id dataset, uint64_t first,
				std::vector<uint8_t> &out, const Limits &limits)
{
	return Guard([&] {
		Require(reader.Size() <= limits.maxFileBytes, Code::LimitExceeded, "file budget");
		auto found =
			std::find_if(index.sections.begin(), index.sections.end(), [&](const SectionInfo &s) {
				return s.type == uint32_t(Section::Chunk) && s.dataset == dataset &&
					   s.firstElement == first;
			});
		Require(found != index.sections.end(), Code::Missing, "chunk unavailable");
		Require(found->bytes <= limits.maxDecodedBytes && limits.maxAllocations >= 1,
				Code::LimitExceeded, "chunk allocation budget");
		auto b = ReadBytes(reader, found->offset, found->bytes, limits.maxChunkBytes);
		CheckHash(b, found->checksum);
		out.swap(b);
	});
}
} // namespace Disk
} // namespace Rend2LightRecon
