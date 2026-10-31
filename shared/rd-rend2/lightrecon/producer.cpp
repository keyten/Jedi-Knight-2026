// S13: orchestrates existing CPU stages and validates complete output payloads.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "producer.h"
#include "../tr_pbr_contract.h"
#include "bake_math.h"
#include "geometry_key.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
namespace Rend2LightRecon
{
namespace Core
{
namespace Producer
{
namespace
{
using namespace Bake::Detail;
void Need(bool b, Code c, const std::string &s)
{
	if (!b)
		throw Failure(c, s);
}
void Check(Error e)
{
	if (!e)
		throw e;
}
void Cancel(const ReadContext &read)
{
	Need(!read.cancellation || !read.cancellation->IsCancelled(), Code::Cancelled,
		 "Producer cancelled; no partial artifact published");
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
double Peak(Vec3 v) { return std::max(double(v.x), std::max(double(v.y), double(v.z))); }
double Y(Vec3 v)
{
	return double(v.x) * Rend2PBR::kLumaR + double(v.y) * Rend2PBR::kLumaG + double(v.z) * Rend2PBR::kLumaB;
}
Estimate Known(double x)
{
	Estimate e;
	e.known = true;
	e.value = float(x);
	return e;
}
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
std::string Escape(const std::string &in)
{
	std::ostringstream s;
	s << '"';
	for (unsigned char c : in)
	{
		if (c == '"' || c == '\\')
			s << '\\' << c;
		else if (c < 32)
			s << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
		else
			s << c;
	}
	s << '"';
	return s.str();
}
const uint32_t SampleMagic = 0x53443331, ContributionMagic = 0x43443331, ObservationMagic = 0x4f443331;
const char *SampleEncoding = "org.openjk.producer-sample-domain";
const char *ContributionEncoding = "org.openjk.producer-source-contributions";
const char *ObservationEncoding = "org.openjk.producer-observation-diagnostics";
struct Bytes
{
	std::vector<uint8_t> bytes;
	void U(uint64_t v, unsigned n)
	{
		for (unsigned k = 0; k < n; ++k)
			bytes.push_back(uint8_t(v >> (8 * k)));
	}
	void F(float f)
	{
		Need(std::isfinite(f), Code::InvalidInput, "Nonfinite payload value");
		uint32_t v;
		std::memcpy(&v, &f, 4);
		U(v, 4);
	}
	void D(double f)
	{
		Need(std::isfinite(f), Code::InvalidInput, "Nonfinite payload area");
		uint64_t v;
		std::memcpy(&v, &f, 8);
		U(v, 8);
	}
	void V(Vec3 v)
	{
		F(v.x);
		F(v.y);
		F(v.z);
	}
};
struct Reader
{
	const std::vector<uint8_t> &bytes;
	size_t at = 0;
	explicit Reader(const std::vector<uint8_t> &v) : bytes(v) {}
	uint64_t U(unsigned n)
	{
		Need(n <= 8 && at <= bytes.size() && n <= bytes.size() - at, Code::ProviderError,
			 "Truncated producer payload");
		uint64_t v = 0;
		for (unsigned k = 0; k < n; ++k)
			v |= uint64_t(bytes[at++]) << (8 * k);
		return v;
	}
	float F()
	{
		uint32_t v = uint32_t(U(4));
		float f;
		std::memcpy(&f, &v, 4);
		Need(std::isfinite(f), Code::ProviderError, "Nonfinite producer payload");
		return f;
	}
	double D()
	{
		uint64_t v = U(8);
		double f;
		std::memcpy(&f, &v, 8);
		Need(std::isfinite(f), Code::ProviderError, "Nonfinite producer area");
		return f;
	}
	Vec3 V()
	{
		Vec3 v;
		v.x = F();
		v.y = F();
		v.z = F();
		return v;
	}
	bool B()
	{
		auto v = U(1);
		Need(v <= 1, Code::ProviderError, "Invalid payload boolean");
		return v != 0;
	}
	void End() { Need(at == bytes.size(), Code::ProviderError, "Trailing producer payload"); }
};
void Write(Bytes &b, const SampleRecord &v)
{
	for (Id id : {v.id, v.patch, v.region, v.surface, v.material, v.block})
		b.U(id, 8);
	b.V(v.world);
	b.V(v.normal);
	b.D(v.area);
	b.U(v.style, 4);
}
void Write(Bytes &b, const ContributionRecord &v)
{
	b.U(v.sample, 8);
	b.U(v.source, 8);
	b.U(unsigned(v.root), 4);
	b.U(v.reflectionOrder, 4);
	b.U(v.issues, 4);
	b.U(v.available, 1);
	b.V(v.value);
	b.V(v.lower);
	b.V(v.upper);
}
void Write(Bytes &b, const ObservationRecord &v)
{
	b.U(v.sample, 8);
	b.U(v.patch, 8);
	b.U(unsigned(v.role), 4);
	b.U(unsigned(v.channel), 4);
	b.U(v.available, 1);
	b.U(v.discrepancyKnown, 1);
	b.U(v.issues, 4);
	b.U(v.directionKnown, 1);
	for (Vec3 x : {v.prediction, v.direct, v.sun, v.sky, v.ambient, v.indirect, v.unresolved,
				   v.signedIntervalDiscrepancy, v.directionToSource})
		b.V(x);
}
SampleRecord ReadSample(Reader &b)
{
	SampleRecord v;
	v.id = b.U(8);
	v.patch = b.U(8);
	v.region = b.U(8);
	v.surface = b.U(8);
	v.material = b.U(8);
	v.block = b.U(8);
	v.world = b.V();
	v.normal = b.V();
	v.area = b.D();
	v.style = uint32_t(b.U(4));
	return v;
}
ContributionRecord ReadContribution(Reader &b)
{
	ContributionRecord v;
	v.sample = b.U(8);
	v.source = b.U(8);
	v.root = TransportRootKind(b.U(4));
	v.reflectionOrder = uint32_t(b.U(4));
	v.issues = uint32_t(b.U(4));
	v.available = b.B();
	v.value = b.V();
	v.lower = b.V();
	v.upper = b.V();
	return v;
}
ObservationRecord ReadObservation(Reader &b)
{
	ObservationRecord v;
	v.sample = b.U(8);
	v.patch = b.U(8);
	v.role = Joint::Role(b.U(4));
	v.channel = Joint::Channel(b.U(4));
	v.available = b.B();
	v.discrepancyKnown = b.B();
	v.issues = uint32_t(b.U(4));
	v.directionKnown = b.B();
	v.prediction = b.V();
	v.direct = b.V();
	v.sun = b.V();
	v.sky = b.V();
	v.ambient = b.V();
	v.indirect = b.V();
	v.unresolved = b.V();
	v.signedIntervalDiscrepancy = b.V();
	v.directionToSource = b.V();
	return v;
}
const Dataset &Descriptor(const Artifact &a, Id id)
{
	Need(a.asset != nullptr, Code::InvalidInput, "No artifact");
	auto d = std::find_if(a.asset->datasets.begin(), a.asset->datasets.end(),
						  [&](const Dataset &d) { return d.id == id; });
	Need(d != a.asset->datasets.end(), Code::ProviderError, "Dataset descriptor missing");
	return *d;
}
std::vector<const Disk::Chunk *> Chunks(const Artifact &a, Id id)
{
	std::vector<const Disk::Chunk *> c;
	for (const auto &v : a.chunks)
		if (v.dataset == id)
			c.push_back(&v);
	std::sort(c.begin(), c.end(),
			  [](const Disk::Chunk *a, const Disk::Chunk *b) { return a->firstElement < b->firstElement; });
	return c;
}
template <class T, class Read>
Error DecodeRows(const Artifact &a, Id id, uint32_t magic, const char *encoding, const ReadContext &read,
				 std::vector<T> &out, Read decode)
{
	out.clear();
	try
	{
		const auto &d = Descriptor(a, id);
		Need(d.encoding == encoding && d.encodingVersion == 1 && d.layout == DatasetLayout::Chunked,
			 Code::UnsupportedSchema, "Unsupported producer dataset encoding");
		Need(d.elementCount <= (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024) / sizeof(T),
			 Code::MemoryLimit, "Producer decode span limit");
		uint64_t next = 0;
		GeometryKey hash;
		for (auto c : Chunks(a, id))
		{
			Cancel(read);
			Need(c->firstElement == next && c->count && c->count <= d.chunkElements &&
					 c->count <= d.elementCount - next,
				 Code::ProviderError, "Producer chunk gaps/overlaps/range");
			Reader b{c->bytes};
			Need(b.U(4) == magic && b.U(4) == 1 && b.U(4) == c->count, Code::ProviderError,
				 "Producer chunk header mismatch");
			for (uint64_t i = 0; i < c->count; ++i)
				out.push_back(decode(b));
			b.End();
			hash.Digest(Disk::Hash(c->bytes.data(), c->bytes.size()));
			next += c->count;
		}
		Need(next == d.elementCount && d.contentHash == "sha256-chain:" + Disk::Hex(hash.Finish()),
			 Code::ProviderError, "Producer missing/corrupt payload");
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
		return Failure(Code::MemoryLimit, "Producer decode allocation");
	}
}
struct Ids
{
	std::set<Id> used;
	const Disk::Digest map;
	Ids(const ReconstructedLightingAsset &a, Disk::Digest m) : map(m)
	{
		for (const auto &p : a.provenanceRecords)
			used.insert(p.id);
		for (const auto &c : a.calibrations)
			used.insert(c.id);
		for (const auto &r : a.regions)
			used.insert(r.id);
		for (const auto &d : a.datasets)
			used.insert(d.id);
		for (const auto &h : a.hypothesisGroups)
			used.insert(h.id);
	}
	Id Span(const std::string &kind, size_t count = 1)
	{
		for (unsigned attempt = 0; attempt < 1024; ++attempt)
		{
			GeometryKey k;
			k.Digest(map);
			Text(k, kind);
			k.U(kVersion);
			k.U(attempt);
			auto key = k.Finish();
			Id id = 0;
			for (unsigned j = 0; j < 7; ++j)
				id |= Id(key[j]) << (8 * j);
			id |= Id(1) << 56;
			bool ok = true;
			for (size_t j = 0; j < count; ++j)
				ok &= !used.count(id + j);
			if (ok)
			{
				for (size_t j = 0; j < count; ++j)
					used.insert(id + j);
				return id;
			}
		}
		throw Failure(Code::InvalidInput, "Producer ID allocation collision");
	}
};
std::string Domain(const Indirect::Contract &c)
{
	switch (c.incidentDomain)
	{
	case Indirect::Root::Domain::CompilerRelative255:
		return "compiler-relative255/pre-encoding";
	case Indirect::Root::Domain::PhysicalLux:
		return "physical-irradiance-lux";
	case Indirect::Root::Domain::SceneEquivalentLux:
		return "scene-equivalent-lux/unknown-absolute-scale";
	default:
		return "specified-source-linear-relative";
	}
}
void HashEstimate(GeometryKey &k, const Estimate &e)
{
	k.U(e.known);
	k.U(e.bounded);
	k.F(e.value);
	k.F(e.lower);
	k.F(e.upper);
}
void HashGeometry(GeometryKey &k, const PhysicalEmitter &e)
{
	k.U(unsigned(e.kind));
	k.U(e.position.known);
	k.V(e.position.world);
	HashEstimate(k, e.position.sigmaWorld);
	k.U(e.frame.known);
	k.V(e.frame.right);
	k.V(e.frame.up);
	k.V(e.frame.forward);
	const auto &g = e.geometry;
	k.U(g.known);
	k.F(g.halfWidthWorld);
	k.F(g.halfHeightWorld);
	k.F(g.halfLengthWorld);
	k.F(g.radiusWorld);
	HashEstimate(k, g.effectiveRadiusWorld);
	HashEstimate(k, g.angularRadiusRadians);
	k.U(g.extension);
	k.U(unsigned(e.angular.kind));
	k.F(e.angular.cosInner);
	k.F(e.angular.cosOuter);
	k.U(e.angular.twoSided);
	k.U(e.angular.dataset);
	HashEstimate(k, e.rangeWorld);
}
Disk::Fingerprint Fingerprint(const Request &r)
{
	auto f = r.fingerprint;
	f.algorithm = "org.openjk.inverse-lighting-producer";
	f.algorithmVersion = kVersion;
	f.units = r.metadata.units;
	GeometryKey k;
	k.U(kVersion);
	k.U(kSchemaVersion);
	k.U(Joint::kVersion);
	k.U(Uncertainty::kVersion);
	k.U(Indirect::kVersion);
	k.Digest(r.map.sha256);
	k.Digest(r.fitting.trainingKey);
	k.Digest(r.fitting.policyKey);
	k.Digest(r.validationKey);
	k.Digest(r.heldoutKey);
	k.Digest(r.fitting.geometry.geometryKey);
	k.Digest(r.fitting.geometry.materialKey);
	k.Digest(r.fitting.geometry.observationPolicyKey);
	k.U(r.fitting.count);
	k.U(r.fitting.geometry.samplingSeedKnown);
	k.U(r.fitting.geometry.samplingSeed);
	k.U(r.fitting.geometry.orderedGridReduction);
	k.F(r.fitting.shadowWeight);
	if (r.fitting.shadow)
		k.Digest(r.fitting.shadow->Key());
	k.U(r.fitting.placement.maxWorkPerCheck);
	k.U(r.fitting.placement.maxSegments);
	k.F(r.fitting.placement.minimumSegmentWorld);
	for (const auto &d : r.fitting.placement.dynamic)
	{
		k.U(d.id);
		k.B(d.bounds);
	}
	if (r.fitting.placement.regions)
		k.Digest(r.fitting.placement.regions->CertificationKey());
	if (r.fitting.geometry.compilerTrace || r.fitting.geometry.integralScene ||
		r.fitting.geometry.triangles || r.fitting.geometry.materials)
		f.complete[size_t(Disk::InputKind::Settings)] = false;
	if (r.fitting.transfer)
		k.Digest(r.fitting.transfer->Key());
	Text(k, r.settingsIdentity);
	Text(k, r.decoder);
	const auto &o = r.fitting.options;
	k.U(o.seed);
	k.U(o.beamWidth);
	k.U(o.rounds);
	k.U(o.maxModels);
	k.U(o.maxSources);
	k.U(o.maxCandidates);
	k.U(o.amplitudeSweeps);
	k.U(o.amplitudeLineSteps);
	k.U(o.geometrySweeps);
	k.U(o.jointSweeps);
	k.U(o.restarts);
	k.U(o.batchRows);
	k.U(o.maxObservations);
	k.U(o.maxRetained);
	k.U(o.residualCandidates);
	k.U(o.residualGridDivisions);
	k.U(o.maxEvaluations);
	k.U(o.maxRays);
	k.U(o.maxWork);
	k.U(o.maxBytes);
	k.U(o.cacheBytes);
	k.U(o.timeLimitMilliseconds);
	k.F(o.huber);
	k.F(o.objectiveTolerance);
	k.F(o.parameterTolerance);
	k.F(o.positionStepWorld);
	k.F(o.angularStepRadians);
	k.F(o.logDimensionStep);
	k.F(o.complexityPerSource);
	k.F(o.complexityPerParameter);
	k.F(o.equivalentValidationSlack);
	k.F(o.worstBlockWeight);
	k.F(o.maximumRequiredBlockRmse);
	k.F(o.minimumValidationImprovement);
	for (double w : o.channelWeight)
		k.F(w);
	k.U(o.geometry);
	k.U(o.materials);
	k.U(o.residualSearch);
	k.U(o.mergeSplit);
	k.U(o.conditionalUnknownMaterials);
	k.U(o.diagnosticSources);
	k.U(o.diagnosticRows);
	k.U(o.coarseRows);
	const auto &q = r.options.uncertainty;
	k.U(q.maxAttempts);
	k.U(q.maxWitnesses);
	k.U(q.maxRefits);
	k.U(q.maxObservations);
	k.U(q.positionLevels);
	k.U(q.colourProfiles);
	k.U(q.deleteBlocks);
	k.U(q.subsampleReplicates);
	k.U(q.restarts);
	k.U(q.maxEvaluations);
	k.U(q.maxRays);
	k.U(q.maxWork);
	k.U(q.maxBytes);
	k.U(q.seed);
	k.F(q.trainingRegret);
	k.F(q.validationRegret);
	k.F(q.positionStepWorld);
	k.F(q.minimumExactRateLower);
	k.F(q.minimumFieldRateLower);
	k.U(q.refitGeometry);
	k.U(q.materialProfiles);
	Text(k, q.population);
	Text(k, q.evaluationFamily);
	k.U(r.options.maxBytes);
	k.U(r.options.maxPayloadBytes);
	k.U(r.options.maxPatches);
	k.U(r.options.maxSources);
	k.U(r.options.maxChunks);
	k.U(r.options.chunkRows);
	k.U(r.options.requireIndependentValidation);
	k.U(r.options.requireFinalEvaluation);
	k.U(r.options.retainUnsupportedDiagnostic);
	for (const auto &p : r.fitting.profiles)
	{
		k.Digest(p.model.profile.contentKey);
		k.Digest(p.contract.profile.contentKey);
		k.U(unsigned(p.model.law));
		k.U(unsigned(p.model.output));
		k.F(p.model.units.worldUnitsPerMeter);
		k.U(p.model.allowAssumed);
		k.U(p.model.allowFacetExpansion);
		k.U(p.model.linearRec709PrimariesAttested);
		k.U(p.model.rectangleDivisions);
		k.U(p.model.tubeSides);
		k.U(p.model.tubeSegments);
		k.F(p.model.historicalEmitterPushWorld);
		k.U(unsigned(p.contract.operation));
		k.U(unsigned(p.contract.incidentDomain));
		k.U(unsigned(p.contract.unit));
		k.U(p.contract.calibration.id);
		k.U(unsigned(p.contract.calibration.kind));
		Text(k, p.contract.calibration.method);
		k.U(p.contract.transferAttested);
		k.U(p.contract.primariesAttested);
		k.U(p.contract.physicalTransportAssumed);
		k.F(p.fixedGain);
		Text(k, p.calibrationHypothesis);
	}
	auto candidates = r.fitting.candidates;
	std::sort(candidates.begin(), candidates.end(),
			  [](const Joint::Candidate &a, const Joint::Candidate &b) { return a.id < b.id; });
	for (const auto &c : candidates)
	{
		k.U(c.id);
		k.U(c.family);
		k.U(c.region);
		k.U(unsigned(c.root));
		k.U(c.initialActive);
		k.U(c.mayRemove);
		k.U(c.fitAmplitude);
		k.U(c.fitEnvironmentDirection);
		k.F(c.maximumEnvironmentRotationRadians);
		k.F(c.clearanceWorld);
		const auto &s = c.shape;
		k.U(unsigned(s.initial.geometry.kind));
		k.V(s.initial.geometry.position.world);
		k.V(s.initial.geometry.frame.right);
		k.V(s.initial.geometry.frame.up);
		k.V(s.initial.geometry.frame.forward);
		k.V(s.initial.rgbAmplitude);
		k.V(s.lowerRgb);
		k.V(s.upperRgb);
		k.B(s.centreBounds);
		k.U(s.fitPosition);
		k.U(s.fitFrame);
		k.U(s.fitCone);
		k.U(s.fitDimensions);
		k.F(s.maximumTranslationWorld);
		k.F(s.maximumRotationRadians);
		k.F(s.minimumDimensionWorld);
		k.F(s.maximumDimensionWorld);
		k.F(s.geometryPriorWeight);
		k.F(s.positionSigmaWorld);
		k.F(s.angleSigmaRadians);
		k.F(s.logSizeSigma);
		k.V(c.environment.orientation.forward);
		Text(k, c.provenance);
	}
	for (const auto &p : r.providers)
	{
		k.Digest(p.generation);
		k.Digest(p.geometryKey);
		k.Digest(p.regionKey);
		Text(k, p.name);
	}
	for (const auto &m : r.fitting.materials)
	{
		k.U(m.material);
		k.U(unsigned(m.knowledge));
		k.V(m.lower);
		k.V(m.upper);
		k.V(m.initial);
		k.F(m.priorWeight);
		Text(k, m.provenance);
	}
	for (const auto &root : r.fitting.boundaryRoots)
	{
		k.U(root.source);
		k.Digest(root.generation);
		k.U(unsigned(root.kind));
		k.U(unsigned(root.domain));
		k.U(root.calibration);
		k.U(unsigned(root.sunCoverage));
		k.U(root.incident.size());
		for (auto v : root.incident)
			k.V(v);
		k.U(root.available.size());
		for (auto v : root.available)
			k.U(v);
		Text(k, root.provenance);
	}
	for (const auto &s : {r.fitting.coarseSampling, r.fitting.strictSampling})
	{
		k.U(s.areaDivisions);
		k.U(s.tubeSides);
		k.U(s.tubeSegments);
		k.U(s.receiverSubdivisions);
		k.U(s.compareRefined);
		k.F(s.receiverOffsetWorld);
		k.F(s.endpointToleranceWorld);
		k.F(s.minimumDistanceWorld);
		k.U(s.maxBytes);
		k.U(s.maxRays);
		k.U(s.maxWork);
	}
	k.U(r.coverage.rawPages);
	k.U(r.coverage.rawPixels);
	k.U(r.coverage.derivedPixels);
	k.U(r.coverage.mappedSamples);
	k.U(r.coverage.rawComplete);
	k.U(r.coverage.derivedComplete);
	k.U(r.coverage.sourceTransferKnown);
	k.U(r.coverage.primariesKnown);
	k.U(r.coverage.historicalProfileKnown);
	if (r.calibration)
		k.Digest(r.calibration->key);
	// Optional providers/search policies are explicit dependencies. Never infer
	// complete fingerprints for caller-supplied unversioned generation settings.
	k.U(bool(r.hidden));
	k.U(bool(r.solar));
	// Provider request policies are not yet canonically serialized here. Refuse
	// cache reuse for these builds rather than trusting an incomplete settings key.
	if (r.hidden || r.solar || !r.providers.empty())
		f.complete[size_t(Disk::InputKind::Settings)] = false;
	Text(k, r.metadata.revision);

	k.B(r.fitting.sourceSearchBounds);
	k.Digest(r.fitting.placement.domainKey);
	k.Digest(r.fitting.placement.map.sha256);
	k.F(r.metadata.units.physicalToScene);
	Text(k, r.metadata.lineage);
	for (const auto &c : candidates)
	{
		HashGeometry(k, c.shape.initial.geometry);
		k.F(c.shape.initial.spotTargetDistanceWorld);
		k.F(c.shape.initial.spotRadiusWorld);
		k.U(c.shape.initial.emittingSurface);
		k.F(c.shape.minimumOuterRadians);
		k.F(c.shape.maximumOuterRadians);
		k.U(c.shape.directionalEvidence);
		k.U(unsigned(c.shape.evidence));
		HashEstimate(k, c.environment.compilerParametersPerCanonicalUnit);
		k.U(unsigned(c.environment.kind));
		k.U(c.environment.orientation.known);
		k.V(c.environment.orientation.right);
		k.V(c.environment.orientation.up);
		k.U(c.environment.amplitudeKnown);
		k.V(c.environment.compilerRgbAmplitude);
		Text(k, c.provider);
		for (Id id : c.parents)
			k.U(id);
	}
	for (const auto &c : r.metadata.calibrations)
	{
		k.U(c.id);
		k.U(unsigned(c.kind));
		k.U(c.provenance);
		Text(k, c.method);
	}
	for (const auto &p : r.fitting.profiles)
	{
		k.U(p.model.calibration.id);
		k.U(unsigned(p.model.calibration.kind));
		Text(k, p.model.calibration.method);
		for (const auto &rule : p.model.profile.rules)
		{
			k.U(unsigned(rule.knowledge));
			Text(k, rule.selector);
			k.U(rule.formulaVersion);
			Text(k, rule.verificationScope);
			for (const auto &param : rule.parameters)
			{
				Text(k, param.name);
				Text(k, param.unit);
				HashEstimate(k, param.value);
			}
		}
	}
	auto key = k.Finish();
	std::string content = "S13/settings/1/" + Disk::Hex(key);
	Disk::InputContent d;
	d.kind = Disk::InputKind::Settings;
	d.name = "producer/structured-settings";
	d.bytes = content.size();
	d.sha256 = Disk::Hash(content.data(), content.size());
	f.inputs.push_back(d);
	return f;
}
struct Assembly
{
	const Request &request;
	const ReadContext &read;
	ReconstructedLightingAsset asset;
	std::vector<Disk::Chunk> chunks;
	Ids ids;
	Id provenance = 0, relativeCalibration = 0;
	std::map<Id, Id> trackSources;
	Assembly(const Request &r, const ReadContext &c)
		: request(r), read(c), asset(AssetMetadata(r.metadata)), ids(asset, r.map.sha256)
	{
		Need(Validate(asset), Code::InvalidInput, "Producer input metadata invalid");
		Need(r.fitting.profiles.empty() || r.metadata.units.worldUnitsPerMeter ==
											   r.fitting.profiles.front().contract.units.worldUnitsPerMeter,
			 Code::InvalidInput, "Input/operator world scale differs");
		provenance = ids.Span("producer-provenance");
		Provenance p;
		p.id = provenance;
		p.kind = ProvenanceKind::Reconstruction;
		p.producer = "org.openjk.inverse-lighting-producer/1";
		p.locator = r.map.path;
		p.contentHash = "sha256:" + Disk::Hex(r.map.sha256);
		p.method =
			"Strict production CPU fitting; conditional hypotheses and independent readiness dimensions";
		if (asset.provenance)
			p.parents.push_back(asset.provenance);
		asset.provenanceRecords.push_back(p);
		asset.provenance = provenance;
		relativeCalibration = ids.Span("relative-calibration");
		Calibration calibration;
		calibration.id = relativeCalibration;
		calibration.kind = CalibrationKind::UncalibratedRelative;
		calibration.provenance = provenance;
		calibration.method =
			"Compiler-relative or specified scene-linear gauge; no absolute photometric measurement";
		asset.calibrations.push_back(calibration);
		auto &a = asset.assessment;
		a.present = false;
		a.producer = p.producer;
		a.algorithm = "Joint/" + std::to_string(Joint::kVersion) + " + Uncertainty/1 + Producer/1";
		a.map = r.map.path;
		a.bspSha256 = Disk::Hex(r.map.sha256);
		a.geometryKey = Disk::Hex(r.fitting.geometry.geometryKey);
		a.decoder = r.decoder;
		a.settingsHash = Disk::Hex(Disk::Hash(r.settingsIdentity.data(), r.settingsIdentity.size()));
		a.seed = r.fitting.options.seed;
		a.inputsLoaded = true;
		a.coverage = r.coverage;
		for (const auto &profile : r.fitting.profiles)
			a.profileHypotheses.push_back(profile.model.law == Shape::Law::HistoricalBake
											  ? profile.model.profile.id + "/" +
													Disk::Hex(profile.model.profile.contentKey)
											  : "specified-physical-integral-relative");
		a.coverage.requestedTraining = r.fitting.count;
		a.trainingHash = Disk::Hex(r.fitting.trainingKey);
		for (const auto &d : r.fingerprint.inputs)
		{
			InputDependency dep;
			dep.kind = std::to_string(unsigned(d.kind));
			dep.name = d.name;
			dep.sha256 = Disk::Hex(d.sha256);
			dep.bytes = d.bytes;
			a.dependencies.push_back(dep);
		}
	}
	template <class T>
	Id DatasetRows(const std::string &name, DatasetPurpose purpose, const char *encoding, uint32_t magic,
				   const std::vector<T> &rows)
	{
		Cancel(read);
		Id id = ids.Span(name);
		Dataset d;
		d.id = id;
		d.purpose = purpose;
		d.layout = DatasetLayout::Chunked;
		d.encoding = encoding;
		d.encodingVersion = 1;
		d.locator = "embedded://producer/" + std::to_string(id);
		d.elementCount = rows.size();
		d.chunkElements = request.options.chunkRows;
		GeometryKey hash;
		for (size_t first = 0; first < rows.size(); first += request.options.chunkRows)
		{
			Cancel(read);
			Disk::Chunk c;
			c.dataset = id;
			c.firstElement = first;
			c.count = std::min(request.options.chunkRows, rows.size() - first);
			Bytes b;
			b.U(magic, 4);
			b.U(1, 4);
			b.U(c.count, 4);
			for (size_t j = first; j < first + c.count; ++j)
				Write(b, rows[j]);
			c.bytes = std::move(b.bytes);
			hash.Digest(Disk::Hash(c.bytes.data(), c.bytes.size()));
			chunks.push_back(std::move(c));
			Need(chunks.size() <= request.options.maxChunks, Code::WorkLimit, "Producer chunk count limit");
		}
		d.contentHash = "sha256-chain:" + Disk::Hex(hash.Finish());
		asset.datasets.push_back(d);
		return id;
	}
	void SourceRecords(const Joint::Model &selected, const Uncertainty::Result &uncertainty)
	{
		std::set<Id> validRegions;
		for (const auto &r : asset.regions)
			validRegions.insert(r.id);
		for (const auto &track : uncertainty.tracks)
		{
			const Joint::Source *primary = nullptr;
			size_t primaryProfile = selected.profile;
			bool physical = false;
			for (const auto &w : uncertainty.witnesses)
			{
				for (size_t i = 0; i < w.tracks.size(); ++i)
					if (w.tracks[i] == track.id)
					{
						if (!primary || w.model.key == selected.key)
						{
							primary = &w.model.sources[i];
							primaryProfile = w.model.profile;
							physical = w.model.absoluteScaleKnown;
						}
					}
				if (primary && w.model.key == selected.key)
					break;
			}
			Need(primary != nullptr, Code::AlgorithmError, "Producer track has no configuration");
			const auto &s = *primary;
			const Id canonicalSource = ids.Span("source-track/" + std::to_string(track.id));
			trackSources[track.id] = canonicalSource;
			Provenance p;
			p.id = ids.Span("source-provenance/" + std::to_string(track.id));
			p.kind = ProvenanceKind::Reconstruction;
			p.producer = "producer/1/source-evidence";
			p.parents = {provenance};
			p.locator = s.candidate.provider;
			p.method = s.candidate.provenance;
			p.contentHash = Disk::Hex(selected.key);
			asset.provenanceRecords.push_back(p);
			Source source;
			source.id = canonicalSource;
			source.provenance = p.id;
			source.state = HypothesisState::Candidate;
			if (validRegions.count(s.candidate.region))
				source.regions.push_back(s.candidate.region);
			const auto &model = request.fitting.profiles[primaryProfile].model;
			bool colourKnown = request.coverage.primariesKnown && model.linearRec709PrimariesAttested;
			Photometry photometry;
			photometry.calibration = relativeCalibration;
			photometry.unit = PhotometricUnit::Relative;
			if (colourKnown && Peak(s.value.rgbAmplitude) > 0)
			{
				Check(NormalizeChromaticity(s.value.rgbAmplitude, photometry.color)
						  ? Error{}
						  : Failure(Code::InvalidInput, "Emitter chromaticity invalid"));
				photometry.intensity = Known(Y(s.value.rgbAmplitude));
			}
			if (physical)
			{
				Need(colourKnown, Code::InvalidInput,
					 "Physical source photometry needs independently attested linear primaries");
				auto calibration = std::find_if(
					asset.calibrations.begin(), asset.calibrations.end(), [&](const Calibration &c) {
						return c.id == model.calibration.id && c.kind == CalibrationKind::PhysicallyAuthored;
					});
				Need(calibration != asset.calibrations.end(), Code::InvalidInput,
					 "Physical source calibration absent from input metadata");
				photometry.calibration = calibration->id;
				photometry.unit =
					s.value.geometry.kind == EmitterKind::Point || s.value.geometry.kind == EmitterKind::Spot
						? PhotometricUnit::Candela
						: s.value.geometry.kind == EmitterKind::Directional ? PhotometricUnit::Lux
																			: PhotometricUnit::Nits;
			}
			if (s.candidate.root == TransportRootKind::Sky)
			{
				EnvironmentLighting environment;
				environment.source = source;
				environment.meanLuminance = photometry;
				environment.sunCoverage = SunCoverage::ExcludesExplicitSun;
				asset.environments.push_back(environment);
			}
			else if (s.candidate.root == TransportRootKind::LocalEmitter ||
					 s.candidate.root == TransportRootKind::Sun)
			{
				PhysicalEmitter emitter = s.value.geometry;
				emitter.source = source;
				emitter.emission = photometry;
				if (s.candidate.root == TransportRootKind::Sun)
				{
					emitter.kind = EmitterKind::Directional;
					emitter.position = {};
					emitter.frame = s.environment.orientation;
					emitter.angular.kind = AngularKind::Parallel;
					SunSource sun;
					sun.emitter = source.id;
					asset.suns.push_back(sun);
				}
				asset.emitters.push_back(emitter);
			}
			else
				Need(false, Code::Unavailable,
					 "Unsupported optimized nonphysical root; use an explicit boundary field");
		}
		std::vector<Uncertainty::SourceBinding> bindings;
		for (const auto &t : uncertainty.tracks)
		{
			Uncertainty::SourceBinding b;
			b.track = t.id;
			b.source = trackSources.at(t.id);
			bindings.push_back(b);
		}
		Id first = ids.Span("uncertainty-configuration-records",
							uncertainty.witnesses.size() + uncertainty.tracks.size() + 1);
		Check(Uncertainty::Attach(uncertainty, request.fitting, bindings, provenance, first, asset));
		for (const auto &c : asset.lightingConfigurations)
			if (c.key == Disk::Hex(selected.key))
				asset.assessment.selectedConfiguration = c.id;
		Need(asset.assessment.selectedConfiguration != 0, Code::AlgorithmError,
			 "Preferred strict model missing from alternative configurations");
	}
};
Frame DirectionFrame(Vec3 travel)
{
	Frame f;
	f.known = true;
	V z = Unit(V(travel)), x = Unit(Cross(std::abs(z.z) > .9 ? V(0, 1, 0) : V(0, 0, 1), z));
	f.forward = z.Float();
	f.right = x.Float();
	f.up = Cross(z, x).Float();
	return f;
}
bool Convert(const Candidates::Catalog &catalog, const Candidates::Model &model, const Joint::Request &r,
			 Joint::Candidate &c)
{
	if (!model.geometryKnown || model.nature == Candidates::Nature::TransportEvidence ||
		model.fragmentUnion && model.parts.size() > 1)
		return false;
	const auto &provider = catalog.providers.at(model.primary.provider);
	const auto &original = provider.candidates.data[model.primary.index];
	const auto &e = provider.evidence.data[original.primary];
	if (e.issues & (MovingEvidence | AnimatedEvidence | DynamicAuthoredMode))
		return false;
	c.id = model.id;
	c.family = model.bundle;
	c.provider = provider.name;
	c.provenance = e.source + "; " + e.name + "; " + EvidenceOriginName(e.origin) +
				   "; preserved source evidence " + std::to_string(e.id);
	c.parents.push_back(original.id);
	// Keep all originating evidence in auditable provenance, including records
	// that did not supply the preferred geometry. These are origins, not proofs.
	for (const auto &ref : model.evidence)
	{
		const auto &origin = Candidates::Evidence(catalog, ref);
		c.parents.push_back(origin.id);
		c.provenance += "; origin=" + std::to_string(origin.id) + ":" + EvidenceOriginName(origin.origin) +
						":" + origin.source + ":" + origin.material;
	}
	c.mayRemove = !model.protectedAuthored;
	c.initialActive = model.protectedAuthored;
	auto &s = c.shape;
	auto &g = s.initial.geometry;
	g.source.id = c.id;
	g.position.known = e.positionKnown;
	g.position.world = e.position;
	s.initial.rgbAmplitude = Vec3(64, 64, 64);
	s.upperRgb = Vec3(2000, 2000, 2000);
	s.fitPosition = model.tier > Candidates::Tier::Geometry;
	s.fitFrame = s.fitCone = false;
	s.fitDimensions = false;
	s.evidence =
		model.protectedAuthored ? Shape::ShapeEvidence::Authored : Shape::ShapeEvidence::VisibleFixture;
	s.geometryPriorWeight = model.protectedAuthored ? 1 : .1;
	s.maximumTranslationWorld = 16;
	s.minimumDimensionWorld = .0001;
	s.maximumDimensionWorld = 65536;
	s.centreBounds = r.sourceSearchBounds;
	if (!s.centreBounds.known && e.positionKnown)
	{
		s.centreBounds.known = true;
		s.centreBounds.minWorld = (V(e.position) - V(16, 16, 16)).Float();
		s.centreBounds.maxWorld = (V(e.position) + V(16, 16, 16)).Float();
	}
	if (!model.regions.empty())
		c.region = model.regions.front();
	if (model.sourceClass == Candidates::Class::Sky)
	{
		c.root = TransportRootKind::Sky;
		c.environment.id = c.id;
		c.environment.kind = Bake::SourceKind::Sky;
		s.fitPosition = false;
		return true;
	}
	if (model.sourceClass == Candidates::Class::Sun)
	{
		c.root = TransportRootKind::Sun;
		c.environment.id = c.id;
		c.environment.kind = Bake::SourceKind::Directional;
		c.environment.orientation = DirectionFrame((V(e.toSource) * -1).Float());
		g.kind = EmitterKind::Directional;
		g.position = {};
		g.frame = c.environment.orientation;
		g.angular.kind = AngularKind::Parallel;
		s.fitPosition = false;
		return true;
	}
	g.kind =
		model.sourceClass == Candidates::Class::Spot
			? EmitterKind::Spot
			: model.sourceClass == Candidates::Class::Area
				  ? EmitterKind::Rectangle
				  : model.sourceClass == Candidates::Class::Tube ? EmitterKind::Tube : EmitterKind::Point;
	g.angular.kind = g.kind == EmitterKind::Point
						 ? AngularKind::Isotropic
						 : g.kind == EmitterKind::Spot ? AngularKind::SmoothCone : AngularKind::Lambertian;
	if (g.kind != EmitterKind::Point)
	{
		g.frame.known = e.orientationKnown;
		g.frame.right = e.right;
		g.frame.up = e.up;
		g.frame.forward = e.outward;
		if (g.kind == EmitterKind::Spot)
		{
			g.frame = DirectionFrame(e.outward);
			double outer = e.coneDegrees * Pi / 180;
			g.angular.cosOuter = float(std::cos(outer));
			g.angular.cosInner = float(std::cos(outer * .8));
			s.initial.spotTargetDistanceWorld = 64;
			s.initial.spotRadiusWorld = 64 * std::tan(outer);
		}
		else
		{
			g.geometry.known = true;
			g.geometry.halfWidthWorld = e.halfWidth;
			g.geometry.halfHeightWorld = e.halfHeight;
			g.geometry.halfLengthWorld = e.halfWidth;
			g.geometry.radiusWorld = e.radius;
			g.angular.twoSided = g.kind == EmitterKind::Rectangle && e.twoSided;
		}
	}
	return true;
}
Joint::Request Prepare(const Request &request, const ReadContext &read, Result &result)
{
	auto q = request.fitting;
	auto providers = request.providers;
	Disk::Digest regionKey{};
	if (request.hidden && request.hidden->regions)
		regionKey = request.hidden->regions->CertificationKey();
	if (request.hidden)
	{
		Need(request.hidden->map.sha256 == request.map.sha256 &&
				 request.hidden->geometry.geometryKey == q.geometry.geometryKey,
			 Code::InvalidInput, "Hidden provider generation mismatch");
		auto generated = std::make_shared<Hidden::Result>();
		Check(Hidden::Generate(*request.hidden, read, *generated));
		result.work.hidden = generated->report;
		Candidates::Provider provider;
		Check(Candidates::FromHidden("surface-response", request.map.sha256, q.geometry.geometryKey,
									 regionKey, generated, provider));
		providers.push_back(provider);
		result.stages.push_back("hidden-candidate search executed; proposals are not physical lights");
	}
	if (request.solar)
	{
		Need(request.solar->mapKey == request.map.sha256 &&
				 request.solar->geometry.geometryKey == q.geometry.geometryKey,
			 Code::InvalidInput, "Solar provider generation mismatch");
		Solar::FitResult fit;
		Check(Solar::Fit(*request.solar, read, fit));
		result.work.solar = fit.work;
		if (request.solarValidation)
		{
			auto accepted = std::make_shared<Solar::Result>();
			Check(request.solarValidation->Assess(fit, read, *accepted));
			std::vector<Candidates::Provider> solar;
			Check(Candidates::FromSolar("sun-sky", request.map.sha256, q.geometry.geometryKey, accepted,
										solar));
			providers.insert(providers.end(), solar.begin(), solar.end());
			result.stages.push_back(std::string("independent solar assessment: ") +
									Solar::StateName(accepted->state));
		}
		else
			result.stages.push_back("solar fit unvalidated; no directional promotion");
	}
	if (providers.empty())
	{
		result.work.candidates = q.candidates.size();
		result.stages.push_back("supplied candidate geometry priors; no claim of source discovery");
		return q;
	}
	Candidates::Request input;
	input.map = request.map.sha256;
	input.geometryKey = q.geometry.geometryKey;
	input.regionKey = regionKey;
	input.providers = providers;
	input.visibility = q.geometry.visibility;
	input.limits = request.options.candidates;
	Candidates::Catalog catalog;
	Check(Candidates::Consolidate(input, read, catalog));
	result.work.candidates = catalog.models.size() + q.candidates.size();
	std::set<std::pair<std::string, Disk::Digest>> contexts;
	std::set<Disk::Digest> exclusiveScenarios;
	for (const auto &m : catalog.models)
	{
		contexts.emplace(m.profile, m.scenario);
		if (m.scenario != Disk::Digest{})
			exclusiveScenarios.insert(m.scenario);
		if (!m.profile.empty())
			Need(q.profiles.size() == 1 && q.profiles[0].model.profile.id == m.profile, Code::Unavailable,
				 "Provider-conditioned profile requires a separately scoped fitting request");
	}
	Need(exclusiveScenarios.size() <= 1, Code::Unavailable,
		 "Mutually exclusive provider scenarios require separate joint requests; never sum solar "
		 "alternatives");
	std::set<Id> selected;
	for (const auto &context : contexts)
	{
		Candidates::Cursor cursor;
		auto policy = request.options.selection;
		policy.profile = context.first;
		policy.scenario = context.second;
		for (unsigned wave = 0; wave < 64; ++wave)
		{
			Cancel(read);
			size_t remaining = q.options.maxCandidates > q.candidates.size()
								   ? q.options.maxCandidates - q.candidates.size()
								   : 0;
			if (!remaining)
				break;
			policy.maxTickets = std::min(policy.maxTickets, remaining);
			Candidates::Selection selection;
			Check(Candidates::Select(catalog, policy, read, cursor, selection));
			for (const auto &ticket : selection.tickets)
			{
				if (!ticket.completesModel || !selected.insert(ticket.model).second)
					continue;
				auto m = std::find_if(catalog.models.begin(), catalog.models.end(),
									  [&](const Candidates::Model &m) { return m.id == ticket.model; });
				Need(m != catalog.models.end(), Code::AlgorithmError, "Consolidated model ticket absent");
				Joint::Candidate c;
				if (Convert(catalog, *m, q, c))
					q.candidates.push_back(c);
				else
					++result.work.deferred;
			}
			if (selection.exhausted || selection.tickets.empty())
				break;
		}
	}
	result.work.deferred += catalog.models.size() - selected.size();
	result.stages.push_back(
		"S05 provider consolidation and local/class work admission; deferred models retained in diagnostics");
	return q;
}
void AssessReadiness(Assembly &build, const Result &result, bool fitted)
{
	auto &a = build.asset.assessment;
	a.present = true;
	a.solverExecuted = result.fitted.state != Joint::State::NotStarted;
	a.proposedSources = result.work.candidates;
	a.strictEvaluated = fitted;
	a.coverage.usedTraining = result.training.used;
	a.coverage.unavailableTraining = fitted ? result.training.unavailable : a.coverage.requestedTraining;
	a.coverage.requestedValidation = result.validation.requested;
	a.coverage.usedValidation = result.validation.used;
	a.coverage.requestedHeldout = result.final.score.requested;
	a.coverage.usedHeldout = result.final.score.used;
	a.validationHash = Disk::Hex(result.validation.partitionKey);
	a.heldoutHash = Disk::Hex(result.final.score.partitionKey);
	if (result.training.available)
		a.trainingIntervalMse = Known(result.training.intervalMse);
	if (result.validation.available)
		a.validationIntervalMse = Known(result.validation.intervalMse);
	if (result.final.score.available)
	{
		a.heldoutIntervalMse = Known(result.final.score.intervalMse);
		a.heldoutBlackMse = Known(result.final.score.blackMse);
	}
	a.coverage.limitations.push_back(
		"No stock-map source XYZ ground truth or original Raven compiler identification");
	a.coverage.limitations.push_back(
		"Unresolved incident transport not invented from signed observation discrepancy");
	auto add = [&](ReadinessKind kind, AssessmentStatus status, const std::string &scope,
				   const std::string &evidence, const std::string &blocker) {
		ReadinessAssessment r;
		r.kind = kind;
		r.status = status;
		r.scope = scope;
		r.evidence = evidence;
		r.blocker = blocker;
		a.readiness.push_back(r);
	};
	add(ReadinessKind::Data,
		a.coverage.derivedComplete ? AssessmentStatus::Conditional : AssessmentStatus::Unavailable,
		"Reported full-map raw/derived coverage; fitting subset is separately counted", a.bspSha256,
		"Geometric/transfer/material support remains per observation");
	add(ReadinessKind::ForwardModel, fitted ? AssessmentStatus::Conditional : AssessmentStatus::Unavailable,
		"Selected feature-scoped strict historical/physical operator", a.selectedProfile,
		"Compiler parity and shadow rules only in tested scopes; UnknownProfile has no numeric default");
	add(ReadinessKind::OfflineSolver, fitted ? AssessmentStatus::Validated : AssessmentStatus::Unavailable,
		"Bounded offline software execution and structural/payload validation", a.solverKey,
		fitted ? "Not source-existence or map-completeness certification" : result.reason);
	add(ReadinessKind::KnownSourcePhysics, AssessmentStatus::Unknown, "External ground-truth evaluation only",
		result.final.evidence,
		"No hidden source-answer reader is available to the producer; external benchmark measures physical "
		"recovery");
	add(ReadinessKind::StockConditional,
		!a.coverage.historicalProfileKnown && fitted ? AssessmentStatus::Conditional
													 : AssessmentStatus::Unknown,
		"Current map and explicit profile/material hypotheses", a.solverKey,
		"Stock truth/absolute scale unavailable; no generic JA compiler claim");
	add(ReadinessKind::ProductionCache, AssessmentStatus::Unavailable,
		"Actual game map-load preflight/warm reuse", a.fingerprint,
		"Only explicit offline fingerprint/cache is supported; runtime collector remains NotCacheable");
	add(ReadinessKind::RuntimeConsumers, AssessmentStatus::Unavailable,
		"Fog/entity/Forward+/LTC/shadow/PBR adapters", "data-only",
		"No rendering consumer or lazy reconstruction is installed");
	auto source = [&](const Source &s) {
		if (s.state == HypothesisState::Candidate)
			++a.conditionalSources;
		else if (s.state == HypothesisState::Resolved)
			++a.resolvedSources;
		else
			++a.rejectedSources;
	};
	for (const auto &s : build.asset.emitters)
		source(s.source);
	for (const auto &s : build.asset.environments)
		source(s.source);
	auto admission = [&](const Source &s) {
		for (unsigned use = 0; use <= unsigned(SourceUse::Specular); ++use)
		{
			SourceAdmission x;
			x.source = s.id;
			x.use = SourceUse(use);
			bool exact = use == unsigned(SourceUse::ExactEmitter) || use == unsigned(SourceUse::Shadow) ||
						 use == unsigned(SourceUse::Specular);
			x.status = exact ? AssessmentStatus::Unavailable
							 : s.evidence.approximateIrradiance == Suitability::Candidate
								   ? AssessmentStatus::Conditional
								   : AssessmentStatus::Unknown;
			x.reason = exact ? "No independently calibrated exact/shadow/specular admission; canonical "
							   "parameters remain hypotheses"
							 : "Approximate field requires its own independent usefulness calibration and a "
							   "consumer implementation";
			a.admissions.push_back(x);
		}
	};
	for (const auto &s : build.asset.emitters)
		admission(s.source);
	for (const auto &s : build.asset.environments)
		admission(s.source);
	for (const auto &s : build.asset.transport)
		admission(s.source);
	a.structuralValidated = a.payloadsValidated = true;
}
void MakePayloads(Assembly &b, const Joint::Request &q, const Joint::Model &selected, Result &out)
{
	auto &a = b.asset.assessment;
	Indirect::Result decomposition;
	Check(Joint::DecomposeFrozen(q, selected, b.read, decomposition, out.work.decomposition));
	std::map<Id, Id> sourceIds;
	for (const auto &witness : out.uncertainty.witnesses)
		if (witness.model.key == selected.key)
			for (size_t i = 0; i < witness.tracks.size(); ++i)
				sourceIds[witness.model.sources[i].candidate.id] = b.trackSources.at(witness.tracks[i]);
	for (const auto &root : q.boundaryRoots)
	{
		Need(root.kind == TransportRootKind::LegacyAmbient, Code::Unavailable,
			 "A fixed directional/environment root requires canonical source geometry, not an anonymous "
			 "irradiance array");
		Need(!b.ids.used.count(root.source), Code::InvalidInput, "Boundary root identity collision");
		b.ids.used.insert(root.source);
		TransportField field;
		field.source.id = root.source;
		field.source.provenance = b.provenance;
		field.component = TransportComponent::LegacyAmbientIrradiance;
		field.unit = q.profiles[selected.profile].contract.unit;
		field.calibration = field.unit == PhotometricUnit::Relative
								? b.relativeCalibration
								: q.profiles[selected.profile].contract.calibration.id;
		field.membership.model = "identified fixed ambient boundary root";
		field.membership.directLocal = field.membership.directSun = field.membership.directSky =
			field.membership.reflected = field.membership.unresolved = FieldMembership::Excluded;
		field.membership.legacyAmbient = FieldMembership::Included;
		field.membership.reflectionOrderKnown = true;
		b.asset.transport.push_back(field);
		sourceIds[root.source] = root.source;
	}
	for (auto &root : decomposition.fixedRoots)
	{
		Need(sourceIds.count(root.source), Code::AlgorithmError, "Frozen root has no canonical source");
		root.source = sourceIds[root.source];
	}
	for (auto &root : decomposition.byRoot)
		root.source = sourceIds.at(root.source);
	decomposition.contract.calibration.id = decomposition.contract.unit == PhotometricUnit::Relative
												? b.relativeCalibration
												: decomposition.contract.calibration.id;
	Id first = b.ids.Span("passive-fields", 2 * (decomposition.byRoot.size() + 1) + 1);
	if(decomposition.fitted)
        Check(Indirect::AppendFields(*q.transfer, decomposition, first, b.provenance, b.asset, b.chunks, b.read));
    else
        b.asset.assessment.coverage.limitations.push_back("No comparable frozen surface transport samples; unavailable fields were not published");
	for (auto &field : b.asset.transport)
		field.source.evidence.approximateIrradiance = Suitability::Unknown;
	std::vector<SampleRecord> samples;
	for (const auto &patch : q.transfer->Patches())
	{
		SampleRecord s;
		s.id = s.patch = patch.id;
		s.region = patch.region;
		s.surface = patch.receiver.surface;
		s.material = patch.material;
		s.world = patch.receiver.world;
		s.normal = patch.receiver.geometricNormal;
		s.style = patch.receiver.style;
		s.area = patch.areaWorldSquared;
		samples.push_back(s);
	}
	std::sort(samples.begin(), samples.end(),
			  [](const SampleRecord &a, const SampleRecord &b) { return a.id < b.id; });
	a.sampleDomain =
		b.DatasetRows("sample-domain", DatasetPurpose::SampleDomain, SampleEncoding, SampleMagic, samples);
	const auto &contract = q.profiles[selected.profile].contract;
	Id calibration =
		contract.unit == PhotometricUnit::Relative ? b.relativeCalibration : contract.calibration.id;
	for (const auto &root : decomposition.fixedRoots)
	{
		for (unsigned order = 0; order <= (contract.physicalTransportAssumed ? 1u : 0u); ++order)
		{
			std::vector<ContributionRecord> rows;
			auto reflected =
				std::find_if(decomposition.byRoot.begin(), decomposition.byRoot.end(),
							 [&](const Indirect::RootBounce &x) { return x.source == root.source; });
			Need(order == 0 || reflected != decomposition.byRoot.end(), Code::AlgorithmError,
				 "Reflected root attribution missing");
			for (size_t i = 0; i < q.transfer->Patches().size(); ++i)
			{
				ContributionRecord r;
				r.sample = q.transfer->Patches()[i].id;
				r.source = root.source;
				r.root = root.kind;
				r.reflectionOrder = order;
				// Certified direct rays do not depend on unrelated GI links.
				r.available = root.available[i] && (!order || decomposition.values[i].comparable);
				r.issues = decomposition.values[i].issues;
				r.value = order ? reflected->incident[i] : root.incident[i];
				r.lower = order ? reflected->lower[i] : r.value;
				r.upper = order ? reflected->upper[i] : r.value;
				rows.push_back(r);
			}
			std::sort(rows.begin(), rows.end(), [](const ContributionRecord &a, const ContributionRecord &b) {
				return a.sample < b.sample;
			});
			Id id = b.DatasetRows(
				"source-contribution/" + std::to_string(root.source) + "/" + std::to_string(order),
				DatasetPurpose::Contributions, ContributionEncoding, ContributionMagic, rows);
			ContributionReference ref;
			ref.dataset = id;
			ref.sampleDomain = a.sampleDomain;
			ref.sources = {root.source};
			ref.component = order ? ContributionComponent::Indirect
								  : root.kind == TransportRootKind::LegacyAmbient
										? ContributionComponent::Unresolved
										: ContributionComponent::Direct;
			ref.quantity = contract.incidentDomain == Indirect::Root::Domain::CompilerRelative255
							   ? ContributionQuantity::LegacyRelativeChannels
							   : ContributionQuantity::IrradianceRGB;
			ref.unit = contract.unit;
			ref.calibration = calibration;
			ref.domain = Domain(contract);
			ref.configuration = a.selectedConfiguration;
			auto &m = ref.membership;
			m.model = "strict frozen source-linked transport/1";
			TransportRoot membership;
			membership.source = root.source;
			membership.kind = root.kind;
			m.roots = {membership};
			m.directLocal = m.directSun = m.directSky = m.legacyAmbient = m.reflected = m.unresolved =
				FieldMembership::Excluded;
			if (order)
				m.reflected = FieldMembership::Included;
			else if (root.kind == TransportRootKind::Sun)
				m.directSun = FieldMembership::Included;
			else if (root.kind == TransportRootKind::Sky)
				m.directSky = FieldMembership::Included;
			else if (root.kind == TransportRootKind::LegacyAmbient)
				m.legacyAmbient = FieldMembership::Included;
			else
				m.directLocal = FieldMembership::Included;
			m.reflectionOrderKnown = true;
			m.minimumReflectionOrder = m.maximumReflectionOrder = order;
			b.asset.contributions.push_back(ref);
		}
	}
	Joint::Model checked;
	std::vector<Joint::Prediction> predictions;
	Check(Joint::AssessHypothesis(q, selected, b.read, checked, out.training, out.work.strict, &predictions));
	Need(checked.key == selected.key, Code::InvalidState,
		 "Producer strict reevaluation changed frozen model identity");
	std::vector<ObservationRecord> rows;
	size_t ordinal = 0;
	for (uint64_t firstRow = 0; firstRow < q.count; firstRow += 128)
	{
		Chunk<Joint::Observation> chunk;
		Check(q.training->Read(firstRow, size_t(std::min(uint64_t(128), q.count - firstRow)), b.read, chunk));
		for (size_t i = 0; i < chunk.count; ++i, ++ordinal)
		{
			Need(ordinal < predictions.size(), Code::AlgorithmError, "Prediction/observation mismatch");
			const auto &o = chunk.data[i];
			const auto &p = predictions[ordinal];
			Need(o.id == p.id, Code::InvalidState, "Observation stream changed before publication");
			ObservationRecord v;
			v.sample = o.id;
			v.patch = o.patch;
			v.role = Joint::Role::Training;
			v.channel = o.channel;
			v.available = p.available;
			v.issues = p.issues;
			v.prediction = p.rgb;
			v.direct = p.direct;
			v.sun = p.sun;
			v.sky = p.sky;
			v.ambient = p.ambient;
			v.indirect = p.indirect;
			v.unresolved = p.unresolved;
			v.directionKnown = p.directionKnown;
			v.directionToSource = p.directionToSource;
			v.discrepancyKnown = o.comparable && p.available && o.channel == Joint::Channel::Surface;
			V residual;
			for (unsigned c = 0; c < 3; ++c)
				if (o.rgb[c].present)
					residual[c] = V(p.rgb)[c] < o.rgb[c].lower
									  ? o.rgb[c].lower - V(p.rgb)[c]
									  : V(p.rgb)[c] > o.rgb[c].upper ? o.rgb[c].upper - V(p.rgb)[c] : 0;
			v.signedIntervalDiscrepancy = residual.Float();
			rows.push_back(v);
		}
	}
	a.diagnostics = b.DatasetRows("observation-diagnostics", DatasetPurpose::ObservationQuality,
								  ObservationEncoding, ObservationMagic, rows);
}
} // namespace

Error ReadSamples(const Artifact &a, Id id, const ReadContext &c, std::vector<SampleRecord> &out)
{
	return DecodeRows(a, id, SampleMagic, SampleEncoding, c, out, ReadSample);
}
Error ReadContributions(const Artifact &a, Id id, const ReadContext &c, std::vector<ContributionRecord> &out)
{
	return DecodeRows(a, id, ContributionMagic, ContributionEncoding, c, out, ReadContribution);
}
Error ReadObservations(const Artifact &a, Id id, const ReadContext &c, std::vector<ObservationRecord> &out)
{
	return DecodeRows(a, id, ObservationMagic, ObservationEncoding, c, out, ReadObservation);
}

Error Run(const Request &original, const ReadContext &read, Result &out, const Joint::Validation *validation,
		  const FinalEvaluation *final)
{
	out = {};
	try
	{
		Cancel(read);
		Need(original.options.maxSources && original.options.maxPatches && original.options.chunkRows &&
				 original.options.chunkRows <= 4096 && original.options.maxBytes &&
				 original.map.sha256 == original.fitting.mapKey && !original.map.path.empty() &&
				 !original.decoder.empty() && !original.settingsIdentity.empty(),
			 Code::InvalidInput, "Producer input/map/budget/decoder identity invalid");
		Need(original.fitting.count <= original.fitting.options.maxObservations, Code::WorkLimit,
			 "Producer training observation limit");
		bool bspMatched = false;
		for (const auto &input : original.fingerprint.inputs)
			if (input.kind == Disk::InputKind::Bsp && input.sha256 == original.map.sha256 &&
				input.bytes == original.map.bytes)
				bspMatched = true;
		Need(bspMatched, Code::InvalidInput, "Producer BSP manifest does not match immutable map");
		Need((!validation || original.validationKey == validation->Key()) &&
				 (!final || original.heldoutKey == final->Key()),
			 Code::InvalidInput, "Independent evaluation partition identity mismatch");
		Request request = original;
		request.fitting = Prepare(original, read, out);
		auto &q = request.fitting;
		Need(q.candidates.size() <= request.options.maxSources, Code::WorkLimit,
			 "Producer fitting-wave source budget exceeded");
		Assembly build(request, read);
		out.stages.push_back("immutable input/provenance validated");
		bool admissible = !q.profiles.empty() && q.count && q.training && q.transfer;
		if (admissible)
		{
			Need(q.transfer->MapKey() == q.mapKey &&
					 q.transfer->Patches().size() <= request.options.maxPatches,
				 Code::WorkLimit, "Producer transfer generation/patch limit");
			for (const auto &p : q.profiles)
				if (p.model.law == Shape::Law::HistoricalBake && p.model.profile.id == "UnknownProfile")
					admissible = false;
		}
		if (!admissible)
		{
			Need(request.options.retainUnsupportedDiagnostic, Code::Unavailable,
				 "No supported numerical profile or training geometry");
			out.state = State::DiagnosticOnly;
			out.reason = "No admissible numerical reconstruction; UnknownProfile/unknown transfer/missing "
						 "observations are not black predictions";
			out.stages.push_back(out.reason);
			AssessReadiness(build, out, false);
		}
		else
		{
			Need(validation || !request.options.requireIndependentValidation, Code::Unavailable,
				 "Producer requires independent validation before publication");
			Check(Joint::Solve(q, read, out.fitted, validation));
			out.work.solver = out.fitted.work;
			out.stages.push_back("joint intensity/geometry/type/profile/visibility/passive-transport model "
								 "selection executed");
			if (out.fitted.alternatives.empty() || out.fitted.preferred == SIZE_MAX)
			{
				Need(request.options.retainUnsupportedDiagnostic, Code::Unavailable,
					 "No independently supported positive model");
				out.state = State::DiagnosticOnly;
				out.reason = out.fitted.reason;
				AssessReadiness(build, out, false);
			}
			else
			{
				const auto selected = out.fitted.alternatives[out.fitted.preferred];
				Check(Uncertainty::Analyze(q, out.fitted, request.options.uncertainty, read, out.uncertainty,
										   validation, request.calibration));
				out.work.uncertainty = out.uncertainty.work;
				Need(out.uncertainty.complete, Code::WorkLimit,
					 "Uncertainty schedule incomplete; no half-built artifact publication");
				out.stages.push_back("joint alternative/configuration and uncertainty schedule complete");
				Need(final || !request.options.requireFinalEvaluation, Code::Unavailable,
					 "Final held-out evaluation controller missing");
				if (final)
				{
					Check(final->Assess(selected, read, out.final));
					Need(out.final.score.partitionKey == final->Key() && out.final.score.available &&
							 out.final.score.used && out.final.score.requiredBlocksPass,
						 Code::Unavailable, "Final independent supported coverage failed");
					Need(out.final.score.positive && out.final.score.blackMse > out.final.score.intervalMse,
						 Code::Unavailable, "Final positive-light prediction does not improve over black");
				}
				out.stages.push_back(
					"final model frozen; independent held-out assessment performed without refitting");
				out.validation = selected.validation;
				build.SourceRecords(selected, out.uncertainty);
				build.asset.assessment.solverKey = Disk::Hex(selected.key);
				build.asset.assessment.uncertaintyKey = Disk::Hex(out.uncertainty.key);
				build.asset.assessment.selectedProfile =
					q.profiles[selected.profile].model.law == Shape::Law::HistoricalBake
						? q.profiles[selected.profile].model.profile.id
						: "specified-diffuse-integral";
				MakePayloads(build, q, selected, out);
				AssessReadiness(build, out, true);
				out.state = State::Completed;
				out.reason = "Operational conditional offline producer; structural publication is not "
							 "physical-source certification";
			}
		}
		Cancel(read);
		out.artifact.fingerprint = Fingerprint(original);
		Disk::Digest cacheKey;
		auto key = Disk::MakeKey(out.artifact.fingerprint, cacheKey);
		if (key)
		{
			build.asset.assessment.fingerprint = Disk::Hex(cacheKey);
			out.artifact.key = cacheKey;
		}
		else
			build.asset.assessment.coverage.limitations.push_back("Offline fingerprint incomplete: " +
																  key.message);
		out.artifact.asset = std::make_shared<const ReconstructedLightingAsset>(std::move(build.asset));
		out.artifact.chunks = std::move(build.chunks);
		out.artifact.completeness = Completeness::Partial;
		for (const auto &c : out.artifact.chunks)
		{
			Need(c.bytes.size() <= original.options.maxPayloadBytes - out.work.payloadBytes,
				 Code::MemoryLimit, "Producer payload byte limit");
			out.work.payloadBytes += c.bytes.size();
		}
		Check(ValidateArtifact(out.artifact, read));
		out.stages.push_back(
			"canonical structural and every referenced payload validated; data-only artifact sealed");
		return {};
	}
	catch (const Error &error)
	{
		out.artifact = {};
		out.state = error.code == Code::Cancelled
						? State::Cancelled
						: error.code == Code::WorkLimit || error.code == Code::MemoryLimit ? State::Budget
																						   : State::Failed;
		out.reason = error.message;
		return error;
	}
	catch (const std::bad_alloc &)
	{
		out.artifact = {};
		out.state = State::Budget;
		out.reason = "Producer allocation limit";
		return Failure(Code::MemoryLimit, out.reason);
	}
}

Error ValidateArtifact(const Artifact &artifact, const ReadContext &read)
{
	try
	{
		Cancel(read);
		Need(artifact.asset != nullptr, Code::InvalidInput, "No sealed producer asset");
		const auto &a = *artifact.asset;
		std::string problem;
		Need(Validate(a, &problem), Code::InvalidInput, "Canonical asset invalid: " + problem);
		Need(a.assessment.present && a.assessment.structuralValidated && a.assessment.payloadsValidated,
			 Code::InvalidInput, "Producer validation manifest absent/incomplete");
		uint64_t bytes = 0;
		std::set<Id> descriptors;
		for (const auto &d : a.datasets)
			descriptors.insert(d.id);
		for (const auto &c : artifact.chunks)
		{
			Need(descriptors.count(c.dataset), Code::ProviderError, "Orphan producer chunk");
			Need(c.bytes.size() <= (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024) - bytes,
				 Code::MemoryLimit, "Producer payload memory limit");
			bytes += c.bytes.size();
		}
		std::map<Id, SampleRecord> samples;
		std::vector<SampleRecord> sampleRows;
		if (a.assessment.sampleDomain)
		{
			Check(ReadSamples(artifact, a.assessment.sampleDomain, read, sampleRows));
			Need(sampleRows.size() <= (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024) / 512,
				 Code::MemoryLimit, "Producer validation sample scratch");
			Id last = 0;
			for (const auto &v : sampleRows)
			{
				Need(v.id > last && v.patch && std::isfinite(v.area) && v.area > 0 && Normal(V(v.normal)) &&
						 v.style <= 255,
					 Code::ProviderError, "Invalid sample identity/geometry/area/style");
				last = v.id;
				samples.emplace(v.id, v);
			}
		}
		struct Sum
		{
			V direct, sun, sky, ambient, indirect;
			bool available = true;
		};
		std::map<Id, Sum> sum;
		std::set<Id> handled;
		if (a.assessment.sampleDomain)
			handled.insert(a.assessment.sampleDomain);
		std::map<Id, const Source *> sources;
		for (const auto &e : a.emitters)
			sources[e.source.id] = &e.source;
		for (const auto &e : a.environments)
			sources[e.source.id] = &e.source;
		for (const auto &e : a.transport)
			sources[e.source.id] = &e.source;
		std::set<std::tuple<Id, Id, unsigned>> identities;
		for (const auto &ref : a.contributions)
		{
			Cancel(read);
			Need(ref.sampleDomain == a.assessment.sampleDomain &&
					 ref.configuration == a.assessment.selectedConfiguration && !ref.domain.empty() &&
					 ref.membership.reflectionOrderKnown,
				 Code::ProviderError, "Contribution domain/configuration/order missing");
			std::vector<ContributionRecord> rows;
			Check(ReadContributions(artifact, ref.dataset, read, rows));
			handled.insert(ref.dataset);
			Need(rows.size() == samples.size(), Code::ProviderError, "Incomplete per-root surface coverage");
			Id last = 0;
			for (const auto &v : rows)
			{
				Need(v.sample > last && samples.count(v.sample) && sources.count(v.source) &&
						 std::find(ref.sources.begin(), ref.sources.end(), v.source) != ref.sources.end() &&
						 v.reflectionOrder <= 1 && unsigned(v.root) < unsigned(TransportRootKind::Unknown) &&
						 Positive(V(v.value)) && Positive(V(v.lower)) && Positive(V(v.upper)),
					 Code::ProviderError, "Invalid root contribution record");
				last = v.sample;
				Need(identities.emplace(v.sample, v.source, v.reflectionOrder).second, Code::ProviderError,
					 "Duplicate root/order contribution");
				Need(ref.membership.minimumReflectionOrder == v.reflectionOrder &&
						 ref.membership.maximumReflectionOrder == v.reflectionOrder &&
						 ref.membership.roots.size() == 1 && ref.membership.roots[0].source == v.source &&
						 ref.membership.roots[0].kind == v.root,
					 Code::ProviderError, "Contribution root/order membership mismatch");
				for (unsigned c = 0; c < 3; ++c)
					Need(V(v.lower)[c] <= V(v.value)[c] + 1e-5 && V(v.value)[c] <= V(v.upper)[c] + 1e-5,
						 Code::ProviderError, "Contribution uncertainty bounds invalid");
				auto &s = sum[v.sample];
				s.available &= v.available;
				if (v.reflectionOrder)
					s.indirect = s.indirect + V(v.value);
				else if (v.root == TransportRootKind::Sun)
					s.sun = s.sun + V(v.value);
				else if (v.root == TransportRootKind::Sky)
					s.sky = s.sky + V(v.value);
				else if (v.root == TransportRootKind::LegacyAmbient)
					s.ambient = s.ambient + V(v.value);
				else
					s.direct = s.direct + V(v.value);
			}
		}
		if (a.assessment.diagnostics)
		{
			std::vector<ObservationRecord> rows;
			Check(ReadObservations(artifact, a.assessment.diagnostics, read, rows));
			handled.insert(a.assessment.diagnostics);
			Id last = 0;
			for (const auto &v : rows)
			{
				Need(v.sample > last && v.role == Joint::Role::Training &&
						 unsigned(v.channel) < unsigned(Joint::Channel::Count) && Positive(V(v.prediction)),
					 Code::ProviderError, "Invalid observation diagnostic identity/domain");
				last = v.sample;
				if (v.directionKnown)
					Need(Normal(V(v.directionToSource)), Code::ProviderError, "Invalid predicted direction");
				for (Vec3 rgb : {v.direct, v.sun, v.sky, v.ambient, v.indirect, v.unresolved})
					Need(Positive(V(rgb)), Code::ProviderError, "Negative/nonfinite pre-encoding component");
				if (v.channel != Joint::Channel::Surface)
					continue;
				Need(samples.count(v.patch), Code::ProviderError,
					 "Surface diagnostic has no original sample patch");
				const auto &s = sum[v.patch];
				if (!v.available)
					continue;
				Need(s.available, Code::ProviderError, "Known diagnostic with unknown root contribution");
				const V stored[] = {V(v.direct), V(v.sun), V(v.sky), V(v.ambient), V(v.indirect)};
				const V accumulated[] = {s.direct, s.sun, s.sky, s.ambient, s.indirect};
				for (unsigned c = 0; c < 5; ++c)
					Need(Length(stored[c] - accumulated[c]) <= 2e-5 * std::max(1., Length(stored[c])),
						 Code::ProviderError, "Source/component contribution sum mismatch");
			}
		}
		// S10 field decoder: validate every byte/checksum, not just descriptors.
		for (const auto &d : a.datasets)
		{
			if (handled.count(d.id))
				continue;
			Need(d.purpose == DatasetPurpose::Transport &&
					 d.encoding.find("org.openjk.passive-transport-samples/") == 0 && d.encodingVersion == 1,
				 Code::UnsupportedSchema, "Referenced dataset has no validated producer decoder");
			auto field = std::find_if(a.transport.begin(), a.transport.end(),
									  [&](const TransportField &f) { return f.dataset == d.id; });
			Need(field != a.transport.end(), Code::ProviderError, "Unreferenced passive field payload");
			uint64_t next = 0;
			GeometryKey hash;
			std::set<Id> seen;
			for (auto c : Chunks(artifact, d.id))
			{
				Cancel(read);
				Need(c->firstElement == next && c->count <= d.chunkElements && next <= d.elementCount &&
						 c->count <= d.elementCount - next,
					 Code::ProviderError, "Passive field chunk gap/range");
				Reader r(c->bytes);
				Need(r.U(4) == 0x3154474a && r.U(4) == 1 && r.U(4) == c->count &&
						 r.U(4) == unsigned(field->component),
					 Code::ProviderError, "Passive field header");
				for (uint64_t i = 0; i < c->count; ++i)
				{
					Id id = r.U(8);
					r.U(4);
					r.B();
					r.B();
					auto style = r.U(1);
					Need(r.U(1) == 0 && samples.count(id) && seen.insert(id).second && style <= 255,
						 Code::ProviderError, "Passive field sample identity/style");
					Vec3 world = r.V(), normal = r.V(), value = r.V(), lower = r.V(), upper = r.V();
					Need(Length(V(world) - V(samples.at(id).world)) < 1e-4 && Normal(V(normal)) &&
							 Positive(V(value)) && Positive(V(lower)) && Positive(V(upper)),
						 Code::ProviderError, "Invalid passive field geometry/value");
				}
				r.End();
				hash.Digest(Disk::Hash(c->bytes.data(), c->bytes.size()));
				next += c->count;
			}
			Need(next == d.elementCount && seen.size() == samples.size() &&
					 d.contentHash == "sha256-chain:" + Disk::Hex(hash.Finish()),
				 Code::ProviderError, "Missing/corrupt passive field chunks");
		}
		Disk::Digest key;
		auto keyed = Disk::MakeKey(artifact.fingerprint, key);
		Need(!keyed || artifact.key == key, Code::InvalidState, "Artifact fingerprint changed");
		if (artifact.key != Disk::Digest{})
			Need(keyed && a.assessment.fingerprint == Disk::Hex(key), Code::InvalidState,
				 "Assessment/cache key mismatch");
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Producer payload validation allocation");
	}
}
Status Publish(const Artifact &artifact, Manager &owner, uint64_t generation, const ReadContext &read)
{
	if (read.cancellation && read.cancellation->IsCancelled())
		return Status::Unavailable;
	if (owner.Acquire().Generation() != generation)
		return Status::StaleHandle;
	auto valid = ValidateArtifact(artifact, read);
	if (!valid)
		return Status::Corrupt;
	if (owner.Acquire().Generation() != generation)
		return Status::StaleHandle;
	return owner.Publish(*artifact.asset, artifact.completeness);
}
Error Load(const Disk::Cache &cache, const Disk::Fingerprint &fingerprint, const ReadContext &read,
		   Artifact &out)
{
	out = {};
	try
	{
		Cancel(read);
		ReconstructedLightingAsset asset;
		Disk::Index index;
		std::unique_ptr<Disk::FileReader> reader;
		auto error = cache.Load(fingerprint, asset, index, reader);
		Need(bool(error), error.code == Disk::Code::Missing || error.code == Disk::Code::NotCacheable
							  ? Code::Unavailable : Code::ProviderError,
			 error.message);
		Artifact candidate;
		candidate.asset = std::make_shared<const ReconstructedLightingAsset>(std::move(asset));
		candidate.fingerprint = fingerprint;
		candidate.key = index.key;
		uint64_t bytes = 0;
		for (const auto &entry : index.sections)
			if (entry.type == unsigned(Disk::Section::Chunk))
			{
				Need(entry.bytes <= (read.maxBytes ? read.maxBytes : 128ull * 1024 * 1024) - bytes,
					 Code::MemoryLimit, "Cached producer payload budget");
				Cancel(read);
				Disk::Chunk chunk;
				chunk.dataset = entry.dataset;
				chunk.firstElement = entry.firstElement;
				chunk.count = entry.count;
				auto e = Disk::ReadChunk(*reader, index, chunk.dataset, chunk.firstElement, chunk.bytes);
				Need(bool(e), Code::ProviderError, e.message);
				bytes += chunk.bytes.size();
				candidate.chunks.push_back(std::move(chunk));
			}
		Check(ValidateArtifact(candidate, read));
		out = std::move(candidate);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Producer cache decode allocation");
	}
}
Disk::Error Store(const Artifact &artifact, const Disk::Cache &cache, const ReadContext &read)
{
	auto valid = ValidateArtifact(artifact, read);
	if (!valid)
	{
		Disk::Error e;
		e.code = Disk::Code::InvalidAsset;
		e.message = valid.message;
		return e;
	}
	return cache.Store(artifact.fingerprint, *artifact.asset, artifact.chunks);
}
Backend::Backend(Request r, const ReadContext &read, const Joint::Validation *validation,
				 const FinalEvaluation *final, std::shared_ptr<Disk::Cache> cache)
	: request_(std::move(r)), read_(read), validation_(validation), final_(final), cache_(std::move(cache))
{
}
Status Backend::LoadCache(BuildProduct &product)
{
	product = {};
	if (!cache_)
		return Status::Unavailable;
	Artifact a;
	auto error = Load(*cache_, Fingerprint(request_), read_, a);
	if (!error)
		return error.code == Code::Unavailable ? Status::Missing : Status::Corrupt;
	last = {};
	last.artifact = std::move(a);
	last.state = last.artifact.asset->assessment.strictEvaluated ? State::Completed : State::DiagnosticOnly;
	last.reason = "Validated offline cached artifact; no solver or rendering consumer invoked";
	product.asset = last.artifact.asset;
	product.completeness = Completeness::Partial;
	product.message = last.reason;
	return Status::Ok;
}
Status Backend::Build(BuildProduct &product)
{
	product = {};
	auto error = Run(request_, read_, last, validation_, final_);
	if (!error)
		return error.code == Code::Unavailable ? Status::Unavailable : Status::Corrupt;
	product.asset = last.artifact.asset;
	product.completeness = Completeness::Partial;
	product.message = last.reason;
	if (cache_)
	{
		cacheWrite = Store(last.artifact, *cache_, read_).code;
		if (cacheWrite != Disk::Code::Ok)
			product.message += "; offline cache write failed, valid memory artifact retained";
	}
	return Status::Ok;
}
std::string ReportJson(const Result &result)
{
	std::ostringstream s;
	s << std::setprecision(12)
	  << "{\"schema\":\"org.openjk.producer-report.v1\",\"state\":" << Escape(StateName(result.state))
	  << ",\"reason\":" << Escape(result.reason)
	  << ",\"asset_available\":" << (result.artifact.asset ? "true" : "false")
	  << ",\"candidates\":" << result.work.candidates << ",\"deferred\":" << result.work.deferred
	  << ",\"evaluations\":"
	  << result.work.solver.evaluations + result.work.strict.evaluations +
			 result.work.decomposition.evaluations + result.work.uncertainty.evaluations
	  << ",\"rays\":"
	  << result.work.solver.rays + result.work.strict.rays + result.work.decomposition.rays +
			 result.work.uncertainty.rays
	  << ",\"payload_bytes\":" << result.work.payloadBytes;
	if (result.artifact.asset)
	{
		const auto &a = result.artifact.asset->assessment;
		s << ",\"raw_pages\":" << a.coverage.rawPages << ",\"raw_pixels\":" << a.coverage.rawPixels
		  << ",\"derived_pixels\":" << a.coverage.derivedPixels
		  << ",\"mapped_samples\":" << a.coverage.mappedSamples
		  << ",\"training_requested\":" << a.coverage.requestedTraining
		  << ",\"training_used\":" << a.coverage.usedTraining
		  << ",\"validation_used\":" << a.coverage.usedValidation
		  << ",\"heldout_used\":" << a.coverage.usedHeldout
		  << ",\"conditional_sources\":" << a.conditionalSources
		  << ",\"resolved_sources\":" << a.resolvedSources << ",\"profile\":" << Escape(a.selectedProfile)
		  << ",\"cache_key\":" << Escape(a.fingerprint)
		  << ",\"source_transfer_known\":" << (a.coverage.sourceTransferKnown ? "true" : "false")
		  << ",\"primaries_known\":" << (a.coverage.primariesKnown ? "true" : "false")
		  << ",\"derived_complete\":" << (a.coverage.derivedComplete ? "true" : "false")
		  << ",\"production_cache_ready\":false,\"runtime_consumers_ready\":false";
	}
	auto metric = [&](const char *name, bool available, double value, const Estimate *cached) {
		s << ",\"" << name << "\":";
		if (available)
			s << value;
		else if (cached && cached->known)
			s << cached->value;
		else
			s << "null";
	};
	const auto *assessment = result.artifact.asset ? &result.artifact.asset->assessment : nullptr;
	metric("training_interval_mse", result.training.available, result.training.intervalMse,
		   assessment ? &assessment->trainingIntervalMse : nullptr);
	metric("validation_interval_mse", result.validation.available, result.validation.intervalMse,
		   assessment ? &assessment->validationIntervalMse : nullptr);
	metric("heldout_interval_mse", result.final.score.available, result.final.score.intervalMse,
		   assessment ? &assessment->heldoutIntervalMse : nullptr);
	metric("heldout_black_mse", result.final.score.available, result.final.score.blackMse,
		   assessment ? &assessment->heldoutBlackMse : nullptr);
	s << ",\"stages\":[";
	for (size_t i = 0; i < result.stages.size(); ++i)
		s << (i ? "," : "") << Escape(result.stages[i]);
	s << "]}";
	return s.str();
}
Error DebugObj(const Artifact &artifact, size_t maxBytes, std::string &output)
{
	output.clear();
	try
	{
		Need(artifact.asset != nullptr, Code::Unavailable, "No debug artifact");
		std::ostringstream s;
		s << std::setprecision(9) << "# S13 DATA ONLY: candidate markers, no runtime illumination\n";
		size_t index = 0;
		for (const auto &e : artifact.asset->emitters)
		{
			s << "o source_" << e.source.id << "_kind_" << unsigned(e.kind) << "_state_"
			  << unsigned(e.source.state) << "\n";
			V centre = e.position.world;
			double size = 4;
			if (e.kind == EmitterKind::Rectangle)
				size = std::max(double(e.geometry.halfWidthWorld), double(e.geometry.halfHeightWorld));
			if (e.kind == EmitterKind::Tube)
				size = e.geometry.halfLengthWorld;
			for (V axis : {V(e.frame.right), V(e.frame.up), V(e.frame.forward)})
			{
				auto a = centre - axis * size, b = centre + axis * size;
				s << "v " << a.x << ' ' << a.y << ' ' << a.z << "\nv " << b.x << ' ' << b.y << ' ' << b.z
				  << "\nl " << index + 1 << ' ' << index + 2 << "\n";
				index += 2;
			}
			Need(size_t(s.tellp()) <= maxBytes, Code::MemoryLimit, "Debug OBJ limit");
		}
		output = s.str();
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
}

Error DebugPreview(const Artifact &artifact, size_t maximumBytes, std::string &out)
{
	out.clear();
	if (!artifact.asset || artifact.asset->emitters.size() > 256)
		return Failure(Code::Unavailable, "Preview emitter limit/no artifact");
	std::ostringstream s;
	s << std::setprecision(9) << "JKRECONPREVIEW1 " << artifact.asset->assessment.bspSha256 << ' '
	  << artifact.asset->emitters.size() << '\n';
	for (const auto &e : artifact.asset->emitters)
	{
		if (unsigned(e.kind) < 1 || unsigned(e.kind) > 5)
			return Failure(Code::Unavailable, "Unsupported preview source kind");
		s << e.source.id << ' ' << unsigned(e.kind) - 1 << " 0";
		for (auto v : {e.position.world, e.frame.right, e.frame.up, e.frame.forward})
			s << ' ' << v.x << ' ' << v.y << ' ' << v.z;
		s << ' ' << e.geometry.halfWidthWorld << ' ' << e.geometry.halfHeightWorld << ' '
		  << e.geometry.halfLengthWorld << ' ' << e.geometry.radiusWorld << ' '
		  << e.source.evidence.physicalEmitterConfidence.known << ' '
		  << e.source.evidence.physicalEmitterConfidence.value << '\n';
	}
	out = s.str();
	if (out.size() > maximumBytes)
	{
		out.clear();
		return Failure(Code::MemoryLimit, "Preview byte limit");
	}
	return {};
}

const char *StateName(State s)
{
	static const char *n[] = {"NotStarted", "DiagnosticOnly", "Completed", "Cancelled", "Budget", "Failed"};
	return unsigned(s) < 6 ? n[unsigned(s)] : "Invalid";
}
} // namespace Producer
} // namespace Core
} // namespace Rend2LightRecon
