// X15 source-independent receiver capability and fair common-domain manifests.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "receiver_support.h"
#include "bake_math.h"
#include "bake_trace.h"
#include "geometry_key.h"
#include <algorithm>
#include <iomanip>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace ReceiverSupport
{
using namespace Bake::Detail;
namespace
{
void Need(bool b, Code c, const char *s)
{
	if (!b)
		throw Failure(c, s);
}
void Text(Core::GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (auto c : s)
		k.U(uint8_t(c));
}
Reason Convert(Bake::CapabilityReason c)
{
	using C = Bake::CapabilityReason;
	switch (c)
	{
	case C::Supported:
		return Reason::Supported;
	case C::InvalidReceiver:
		return Reason::InvalidGeometry;
	case C::UnknownProfile:
		return Reason::UnknownProfile;
	case C::UnsupportedSource:
	case C::GridSource:
	case C::DeluxeSource:
		return Reason::UnsupportedSource;
	case C::UnsupportedDomain:
		return Reason::UnsupportedDomain;
	case C::UnsupportedStyle:
		return Reason::UnsupportedStyle;
	case C::NonPlanar:
		return Reason::NonPlanar;
	case C::UncertifiedPlacement:
		return Reason::UncertifiedPlacement;
	case C::GeometryError:
		return Reason::GeometryError;
	case C::MappingError:
		return Reason::MappingError;
	case C::DistinctNormals:
		return Reason::DistinctNormals;
	case C::OrderedGrid:
		return Reason::OrderedGrid;
	case C::UnknownMode:
	case C::UnknownTrace:
	case C::UnimplementedStage:
		return Reason::UnsupportedSampling;
	default:
		return Reason::ProfileRule;
	}
}
bool Valid(const Geometry &g)
{
	return g.receiver.sample && g.block && unsigned(g.role) <= 3 && unsigned(g.channel) <= 4 &&
		   std::isfinite(g.areaWorldSquared) && g.areaWorldSquared >= 0;
}
void Quote(std::ostream &o, const std::string &s)
{
	o << '"';
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			o << '\\';
		if (c >= 32)
			o << char(c);
		else
			o << ' ';
	}
	o << '"';
}
} // namespace
const char *ReasonName(Reason r)
{
	static const char *names[] = {"Supported",
								  "InvalidGeometry",
								  "UnknownProfile",
								  "ProfileRule",
								  "UnsupportedSource",
								  "UnsupportedDomain",
								  "UnsupportedStyle",
								  "NonPlanar",
								  "UncertifiedPlacement",
								  "GeometryError",
								  "MappingError",
								  "DistinctNormals",
								  "OrderedGrid",
								  "MaterialRequired",
								  "UnknownTransfer",
								  "UnknownPrimaries",
								  "InvalidCalibration",
								  "MissingVisibility",
								  "UnsupportedSampling",
								  "GeometryUnavailable",
								  "DisabledChannel",
								  "PartitionGap",
								  "ReceiverOriginSolid",
								  "ReceiverOriginBoundary",
								  "ReceiverOriginUnknown"};
	return unsigned(r) < unsigned(Reason::Count) ? names[unsigned(r)] : "InvalidReason";
}
Disk::Digest OperatorKey(const Operator &o)
{
	Core::GeometryKey k;
	k.U(kVersion);
	k.U(unsigned(o.model.law));
	k.U(unsigned(o.model.output));
	k.Digest(o.model.profile.contentKey);
	Text(k, o.model.profile.id);
	Text(k, o.model.profile.family);
	k.U(o.model.profile.schemaVersion);
	k.U(o.model.profile.formulaVersion);
	Text(k, o.model.profile.compilerSourceRevision);
	Text(k, o.model.profile.compilerBinarySha256);
	k.Digest(o.model.profile.sourceLockKey);
	k.U(unsigned(o.model.profile.applicability));
	for (const auto &r : o.model.profile.rules)
	{
		k.U(unsigned(r.knowledge));
		Text(k, r.selector);
		Text(k, r.provenance);
		Text(k, r.verificationScope);
		k.U(r.formulaVersion);
		for (const auto &p : r.parameters)
		{
			Text(k, p.name);
			k.F(p.value.value);
			k.U(p.value.known);
		}
	}
	k.F(o.model.units.worldUnitsPerMeter);
	k.U(o.model.calibration.id);
	k.U(unsigned(o.model.calibration.kind));
	Text(k, o.model.calibration.method);
	k.U(o.model.linearRec709PrimariesAttested);
	k.U(o.model.allowAssumed);
	k.U(o.model.allowFacetExpansion);
	k.U(unsigned(o.bake.output));
	k.U(unsigned(o.bake.mode));
	k.U(unsigned(o.bake.trace));
	k.U(unsigned(o.bake.requestedUnit));
	k.U(o.bake.allowAssumed);
	k.U(o.bake.unpackedGridOnly);
	k.U(o.bake.externallyCertifiedVisibility);
	k.U(o.bake.requireScopedCompilerReplay);
	for (auto f : o.bake.required)
		k.U(unsigned(f));
	auto kinds = o.sources;
	std::sort(kinds.begin(), kinds.end());
	k.U(kinds.size());
	for (auto c : kinds)
		k.U(unsigned(c));
	k.U(o.sampling.areaDivisions);
	k.U(o.sampling.tubeSides);
	k.U(o.sampling.tubeSegments);
	k.U(o.sampling.receiverSubdivisions);
	k.U(o.sampling.compareRefined);
	k.F(o.sampling.receiverOffsetWorld);
	k.F(o.sampling.endpointToleranceWorld);
	k.F(o.sampling.minimumDistanceWorld);
	k.U(o.transferAttested);
	k.U(o.primariesAttested);
	k.U(o.requiresMaterial);
	k.U(o.physicalEnvironmentImplemented);
	Text(k, o.scope);
	return k.Finish();
}
Disk::Digest GeometryKey(const Geometry &g)
{
	Core::GeometryKey k;
	const auto &r = g.receiver;
	k.U(kVersion);
	k.U(r.sample);
	k.U(r.surface);
	k.U(r.material);
	k.V(r.world);
	k.V(r.geometricNormal);
	k.V(r.shadingNormal);
	k.U(r.style);
	k.U(unsigned(r.domain));
	k.U(r.mappedPlanar);
	k.U(r.compilerPlacementCertified);
	k.U(r.twoSided);
	k.F(r.geometryErrorWorld);
	k.F(r.mappingErrorWorld);
	k.U(r.footprint.count);
	if (r.footprint.owner && r.footprint.data && r.footprint.count <= 64)
		for (size_t i = 0; i < r.footprint.count; ++i)
			for (unsigned j = 0; j < 3; ++j)
			{
				k.V(r.footprint.data[i].world[j]);
				k.V(r.footprint.data[i].normal[j]);
			}
	k.U(g.region);
	k.U(g.block);
	k.U(unsigned(g.role));
	k.U(unsigned(g.channel));
	k.F(g.areaWorldSquared);
	k.U(g.enabled);
	k.U(g.geometryKnown);
	k.U(g.materialKnown);
	k.U(g.materialAvailable);
	k.U(g.opticsKnown);
	k.U(g.required);
	k.U(g.channelMask);
	return k.Finish();
}
Decision Check(const Operator &o, const Geometry &g, const Bake::EvaluationContext &ctx)
{
	Decision d;
	auto reject = [&](Reason r) {
		d.reason = r;
		return d;
	};
	const auto &r = g.receiver;
	if (unsigned(o.model.law) > unsigned(Shape::Law::DiffuseIntegral) ||
		unsigned(o.model.output) > unsigned(Shape::Output::PhysicalLux))
		return reject(Reason::UnsupportedDomain);
	if (!std::isfinite(o.model.units.worldUnitsPerMeter) || o.model.units.worldUnitsPerMeter <= 0)
		return reject(Reason::InvalidCalibration);
	if (o.model.law == Shape::Law::DiffuseIntegral)
	{
		const auto &s = o.sampling;
		if (s.areaDivisions < 1 || s.areaDivisions > 32 || s.tubeSides < 4 || s.tubeSides > 64 ||
			s.tubeSegments < 1 || s.tubeSegments > 32 || s.receiverSubdivisions > 3 ||
			!std::isfinite(s.receiverOffsetWorld) || s.receiverOffsetWorld < 0 ||
			!std::isfinite(s.endpointToleranceWorld) || s.endpointToleranceWorld < 0 ||
			!std::isfinite(s.minimumDistanceWorld) || s.minimumDistanceWorld <= 0)
			return reject(Reason::UnsupportedSampling);
	}
	if (!g.enabled)
		return reject(Reason::DisabledChannel);
	if (g.role == Role::Gap)
		return reject(Reason::PartitionGap);
	if (!Valid(g) || !Finite(r.world) || !Normal(r.geometricNormal) || !Normal(r.shadingNormal) ||
		!std::isfinite(r.geometryErrorWorld) || r.geometryErrorWorld < 0 ||
		!std::isfinite(r.mappingErrorWorld) || r.mappingErrorWorld < 0)
		return reject(Reason::InvalidGeometry);
	if (!g.geometryKnown)
		return reject(Reason::GeometryUnavailable);
	if (!o.transferAttested)
		return reject(Reason::UnknownTransfer);
	if (!o.primariesAttested)
		return reject(Reason::UnknownPrimaries);
	if (o.requiresMaterial && !g.materialKnown && !g.materialAvailable)
		return reject(Reason::MaterialRequired);
	if (o.sources.empty() || o.sources.size() > 8)
		return reject(Reason::UnsupportedSource);
	if (!ctx.visibility && !o.bake.externallyCertifiedVisibility &&
		!(o.bake.trace == Bake::TraceMode::CompilerTree && ctx.compilerTrace))
		return reject(Reason::MissingVisibility);
	if (o.model.law == Shape::Law::HistoricalBake)
	{
		auto options = o.bake;
		options.output = g.channel == Channel::Surface ? o.bake.output
													   : g.channel == Channel::DeluxeDirection
															 ? Bake::Domain::DeluxeWorldDirection
															 : Bake::Domain::GridAmbientDirected;
		for (auto kind : o.sources)
		{
			auto support = Bake::Capability(o.model.profile, kind, r, options);
			if (!support.implemented)
			{
				d.capability = support.code;
				return reject(Convert(support.code));
			}
		}
	}
	else
	{
		if (g.channel != Channel::Surface || o.model.output == Shape::Output::EncodedRgb8)
			return reject(Reason::UnsupportedDomain);
		if (o.model.output == Shape::Output::PhysicalLux &&
			(!o.model.calibration.id ||
			 o.model.calibration.kind != CalibrationKind::PhysicallyAuthored ||
			 !o.model.linearRec709PrimariesAttested))
			return reject(Reason::InvalidCalibration);
		if (r.style != 0)
			return reject(Reason::UnsupportedStyle);
		if (r.geometryErrorWorld > 0)
			return reject(Reason::GeometryError);
		if (!r.mappedPlanar && !r.footprint.count)
			return reject(Reason::NonPlanar);
		if (r.footprint.count &&
			(!r.footprint.owner || !r.footprint.data || r.footprint.count > 64))
			return reject(Reason::UnsupportedSampling);
		for (size_t i = 0; i < r.footprint.count; ++i)
		{
			const auto &t = r.footprint.data[i];
			for (unsigned j = 0; j < 3; ++j)
				if (!Finite(t.world[j]) || !Normal(t.normal[j]))
					return reject(Reason::InvalidGeometry);
			if (Length(Cross(V(t.world[1]) - V(t.world[0]), V(t.world[2]) - V(t.world[0]))) <=
				2e-14)
				return reject(Reason::InvalidGeometry);
		}
		for (auto kind : o.sources)
			if (kind != Bake::SourceKind::Point && kind != Bake::SourceKind::Spot &&
				kind != Bake::SourceKind::Polygon && kind != Bake::SourceKind::Tube &&
				!o.physicalEnvironmentImplemented)
				return reject(Reason::UnsupportedSource);
	}
	return d;
}
Error CheckOrigin(const Operator &o, const Geometry &g, const Bake::EvaluationContext &ctx,
				  const PointClassifier *domain, Decision &decision, uint64_t &probes)
{
	if (decision.reason != Reason::Supported || o.model.law != Shape::Law::HistoricalBake ||
		o.bake.externallyCertifiedVisibility)
		return {};
	if (!domain && !(o.bake.trace == Bake::TraceMode::CompilerTree && ctx.compilerTrace))
		return {}; // path remains explicitly conditional
	Bake::Recipe recipe;
	auto error = Bake::DescribeRecipe(o.model.profile, recipe);
	if (!error)
		return error;
	V start(g.receiver.world);
	if (g.channel == Channel::Surface || g.channel == Channel::DeluxeDirection)
		start = start + V(g.receiver.geometricNormal) * recipe.receiverPushWorld;
	PointDomain where = PointDomain::Unknown;
	++probes;
	if (o.bake.trace == Bake::TraceMode::CompilerTree && ctx.compilerTrace)
		where = ctx.compilerTrace->ClassifyOrigin(start.Float());
	else
	{
		try
		{
			error = domain->Classify(start.Float(), 0, where);
		}
		catch (...)
		{
			return Failure(Code::ProviderError, "Receiver origin classifier threw");
		}
		if (!error)
			return error;
	}
	if (unsigned(where) > unsigned(PointDomain::Boundary))
		return Failure(Code::ProviderError,
					   "Receiver origin classifier returned an invalid domain");
	if (where == PointDomain::Solid)
		decision.reason = Reason::ReceiverOriginSolid;
	else if (where == PointDomain::Boundary)
		decision.reason = Reason::ReceiverOriginBoundary;
	else if (where == PointDomain::Unknown)
		decision.reason = Reason::ReceiverOriginUnknown;
	return {};
}
const Entry *Manifest::Find(Id id) const
{
	auto i = std::lower_bound(entries_.begin(), entries_.end(), id,
							  [](const Entry &e, Id x) { return e.sample < x; });
	return i != entries_.end() && i->sample == id ? &*i : nullptr;
}
bool Manifest::ContainsOperator(const Operator &o) const
{
	return std::find(operators_.begin(), operators_.end(), OperatorKey(o)) != operators_.end();
}
Error Manifest::Match(const Geometry &g, const Entry *&e) const
{
	e = Find(g.receiver.sample);
	if (!e || e->geometry != GeometryKey(g))
		return Failure(Code::InvalidState, "Receiver differs from frozen support manifest");
	return {};
}
Error Build(const Request &r, std::shared_ptr<const ChunkSource<Geometry>> source, uint64_t count,
			const ReadContext &read, std::shared_ptr<const Manifest> &out)
{
	out.reset();
	try
	{
		Need(!Cancelled(read), Code::Cancelled, "Receiver support cancelled");
		Need(source && count && count <= r.maxRows && r.batchRows && r.batchRows <= 4096 &&
				 !r.operators.empty() && r.operators.size() <= 16 && r.map != Disk::Digest{} &&
				 r.geometry != Disk::Digest{} && r.partition != Disk::Digest{},
			 Code::InvalidInput, "Receiver support request identity/bounds");
		Need(!r.domain || r.domainKey != Disk::Digest{}, Code::InvalidInput,
			 "Receiver point-domain generation required");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, r.maxBytes) : r.maxBytes,
				 bytes =
					 65536 + count * (sizeof(Entry) + r.operators.size() * sizeof(Decision) + 128);
		Need(bytes <= cap, Code::MemoryLimit, "Receiver manifest bounded memory");
		std::shared_ptr<Manifest> m(new Manifest);
		m->request_ = r;
		Need(r.context.geometryKey==Disk::Digest{}||r.context.geometryKey==r.geometry,Code::InvalidState,"Receiver context geometry generation mismatch");
		auto traceKey=r.context.compilerTrace?r.context.compilerTrace->Key():Disk::Digest{};
		Need(r.traceKey==Disk::Digest{}||r.traceKey==traceKey,Code::InvalidState,"Receiver trace generation mismatch");
		m->request_.traceKey=traceKey;
		m->request_.context = {};
		m->request_.domain.reset();
		for (auto &op : m->request_.operators)
		{
			op.bake.factors.reset();
			op.bake.contributions = nullptr;
		}
		m->summary_.bytes = bytes;
		auto &sum = m->summary_;
		sum.profileAdmitted.resize(r.operators.size());
		sum.profileArea.resize(r.operators.size());
		Core::GeometryKey key;
		key.U(kVersion);
		key.Digest(r.map);
		key.Digest(r.geometry);
		key.Digest(r.partition);
		key.Digest(r.domainKey);key.Digest(m->request_.traceKey);
		key.Digest(r.policy);
		key.Digest(r.context.materialKey);
		key.Digest(r.context.observationPolicyKey);
		for (const auto &o : r.operators)
		{
			auto k = OperatorKey(o);
			m->operators_.push_back(k);
			key.Digest(k);
		}
		Id last = 0;
		std::set<std::tuple<unsigned, unsigned, Id, Id>> groups;
		for (uint64_t a = 0; a < count; a += r.batchRows)
		{
			Need(!Cancelled(read), Code::Cancelled, "Receiver support cancelled");
			Chunk<Geometry> chunk;
			auto error =
				source->Read(a, size_t(std::min<uint64_t>(r.batchRows, count - a)), read, chunk);
			if (!error)
				return error;
			Need(chunk.owner && chunk.data &&
					 chunk.count == std::min<uint64_t>(r.batchRows, count - a),
				 Code::ProviderError, "Truncated receiver geometry stream");
			for (size_t i = 0; i < chunk.count; ++i)
			{
				const auto &g = chunk.data[i];
				Need(Valid(g) && g.receiver.sample > last, Code::InvalidInput,
					 "Receiver support stable geometry IDs/roles/areas");
				last = g.receiver.sample;
				Entry e;
				e.sample = last;
				e.surface = g.receiver.surface;
				e.material = g.receiver.material;
				e.region = g.region;
				e.block = g.block;
				e.role = g.role;
				e.channel = g.channel;
				e.channelMask = g.channelMask;
				e.required = g.required;
				e.areaWorldSquared = g.areaWorldSquared;
				e.geometry = GeometryKey(g);
				e.common = true;
				e.uncertainty =
					(!g.materialKnown ? MaterialUnknown : 0) |
					(!g.opticsKnown ? OpticalUnknown : 0) |
					(!g.geometryKnown || g.receiver.geometryErrorWorld > 0 ? GeometryApproximate
																		   : 0) |
					(!g.areaWorldSquared ? UnknownArea : 0);
				for (size_t j = 0; j < r.operators.size(); ++j)
				{
					Need(++sum.checks <= r.maxChecks, Code::WorkLimit,
						 "Receiver support capability work limit");
					auto d = Check(r.operators[j], g, r.context);
					auto origin = CheckOrigin(r.operators[j], g, r.context, r.domain.get(), d,
											  sum.originProbes);
					if (!origin)
						return origin;
					e.profiles.push_back(d);
					bool admitted = d.reason == Reason::Supported;
					e.common &= admitted;
					if (admitted)
					{
						++sum.profileAdmitted[j];
						sum.profileArea[j] += g.areaWorldSquared;
					}
					if (r.operators[j].model.law == Shape::Law::HistoricalBake &&
						r.operators[j].model.profile.applicability !=
							Bake::Applicability::VerifiedFeatureSubset)
						e.uncertainty |= AssumedProfile;
					key.U(unsigned(d.reason));
					key.U(unsigned(d.capability));
				}
				++sum.requested;
				sum.requestedArea += g.areaWorldSquared;
				++sum.roleRequested[unsigned(g.role)];
				sum.roleArea[unsigned(g.role)] += g.areaWorldSquared;
				sum.unknownGeometry += !g.geometryKnown;
				sum.unknownOptics += !g.opticsKnown;
				sum.unknownMaterial += !g.materialKnown;
				sum.unknownArea += !g.areaWorldSquared;
				if (e.common)
				{
					++sum.admitted;
					sum.commonArea += g.areaWorldSquared;
					++sum.roleCommon[unsigned(g.role)];
					sum.roleCommonArea[unsigned(g.role)] += g.areaWorldSquared;
					groups.emplace(unsigned(g.role), unsigned(g.channel), g.region, g.block);
				}
				key.Digest(e.geometry);
				key.U(e.common);
				key.U(e.uncertainty);
				m->entries_.push_back(std::move(e));
			}
		}
		sum.requiredGroups = groups.size();
		m->key_ = key.Finish();
		out = m;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Receiver support allocation");
	}
}
Error WriteJson(const Manifest &m, std::ostream &o, const ReadContext &read)
{
	o << std::setprecision(17) << "{\"schema\":\"org.openjk.receiver-support.v2\",\"key\":\""
	  << Disk::Hex(m.Key()) << "\",\"map\":\"" << Disk::Hex(m.Policy().map) << "\",\"geometry\":\""
	  << Disk::Hex(m.Policy().geometry) << "\",\"partition\":\"" << Disk::Hex(m.Policy().partition)
	  << "\",\"operators\":[";
	for (size_t i = 0; i < m.Policy().operators.size(); ++i)
	{
		auto &op = m.Policy().operators[i];
		o << (i ? "," : "") << "{\"key\":\"" << Disk::Hex(OperatorKey(op)) << "\",\"profile\":";
		Quote(o, op.model.profile.id);
		o << ",\"law\":" << unsigned(op.model.law) << ",\"output\":" << unsigned(op.bake.output)
		  << ",\"trace\":" << unsigned(op.bake.trace) << ",\"mode\":" << unsigned(op.bake.mode)
		  << ",\"receiver_subdivisions\":" << op.sampling.receiverSubdivisions
		  << ",\"unpacked_grid_only\":" << (op.bake.unpackedGridOnly ? "true" : "false");
		o << ",\"scope\":";
		Quote(o, op.scope);
		o << ",\"supported\":" << m.Report().profileAdmitted[i]
		  << ",\"area\":" << m.Report().profileArea[i] << '}';
	}
	o << "],\"requested\":" << m.Report().requested << ",\"common\":" << m.Report().admitted
	  << ",\"requested_area\":" << m.Report().requestedArea
	  << ",\"common_area\":" << m.Report().commonArea
	  << ",\"unknown_geometry\":" << m.Report().unknownGeometry
	  << ",\"unknown_optics\":" << m.Report().unknownOptics
	  << ",\"unknown_material\":" << m.Report().unknownMaterial
	  << ",\"required_groups\":" << m.Report().requiredGroups
	  << ",\"origin_probes\":" << m.Report().originProbes << ",\"domain_key\":\""
	  << Disk::Hex(m.Policy().domainKey) << "\",\"entries\":[";
	bool first = true;
	for (const auto &e : m.Entries())
	{
		if (Cancelled(read))
			return Failure(Code::Cancelled, "Manifest export cancelled");
		o << (first ? "" : ",") << "{\"sample\":\"" << e.sample << "\",\"surface\":" << e.surface
		  << ",\"material\":" << e.material << ",\"region\":" << e.region << ",\"block\":\""
		  << e.block << "\",\"role\":" << unsigned(e.role) << ",\"channel\":" << unsigned(e.channel)
		  << ",\"required\":" << (e.required ? "true" : "false")
		  << ",\"channel_mask\":" << unsigned(e.channelMask) << ",\"area\":" << e.areaWorldSquared
		  << ",\"geometry_key\":\"" << Disk::Hex(e.geometry)
		  << "\",\"common\":" << (e.common ? "true" : "false")
		  << ",\"uncertainty\":" << e.uncertainty << ",\"reasons\":[";
		first = false;
		for (size_t i = 0; i < e.profiles.size(); ++i)
		{
			o << (i ? "," : "");
			Quote(o, ReasonName(e.profiles[i].reason));
		}
		o << "],\"capabilities\":[";
		for (size_t i = 0; i < e.profiles.size(); ++i)
		{
			o << (i ? "," : "");
			Quote(o, Bake::CapabilityReasonName(e.profiles[i].capability));
		}
		o << "]}";
	}
	o << "]}";
	return o ? Error{} : Failure(Code::ProviderError, "Receiver support output failed");
}
Error AuditCoverage(const Manifest &m, const std::vector<Id> &ids,
					const std::vector<RuntimePrediction> &predictions, const ReadContext &read,
					Coverage &out)
{
	out = {};
	try
	{
		Need(ids.size() <= m.Entries().size() && predictions.size() <= ids.size(),
			 Code::InvalidInput, "Coverage span bounds");
		std::map<Id, const RuntimePrediction *> indexed;
		std::set<Id> wanted(ids.begin(), ids.end());
		Need(wanted.size() == ids.size(), Code::InvalidInput, "Duplicate denominator ID");
		for (const auto &p : predictions)
			Need(wanted.count(p.sample) && indexed.emplace(p.sample, &p).second, Code::InvalidInput,
				 "Duplicate/unrequested prediction");
		Core::GeometryKey key;
		key.Digest(m.Key());
		for (auto id : wanted)
		{
			Need(!Cancelled(read), Code::Cancelled, "Coverage audit cancelled");
			const auto *e = m.Find(id);
			Need(e, Code::InvalidInput, "Prediction outside frozen manifest");
			++out.requested;
			if (!e->common)
			{
				++out.excluded;
				continue;
			}
			++out.admitted;
			key.U(id);
			key.Digest(e->geometry);
			auto f = indexed.find(id);
			if (f == indexed.end())
			{
				++out.missing;
				continue;
			}
			const auto &p = *f->second;
			switch (p.state)
			{
			case RuntimeState::Available:
			case RuntimeState::KnownBlocked:
				if (!Finite(p.value) || p.value.x < 0 || p.value.y < 0 || p.value.z < 0)
				{
					++out.nonFinite;
					break;
				}
				if (p.state == RuntimeState::KnownBlocked && (p.value.x || p.value.y || p.value.z))
				{
					++out.nonFinite;
					break;
				}
				++out.used;
				out.blocked += p.state == RuntimeState::KnownBlocked;
				break;
			case RuntimeState::UnknownVisibility:
				++out.unknownVisibility;
				break;
			case RuntimeState::WorkLimit:
				++out.workLimited;
				break;
			case RuntimeState::NonFinite:
				++out.nonFinite;
				break;
			case RuntimeState::UnsupportedSource:
				++out.unsupported;
				break;
			default:
				++out.missing;
				break;
			}
		}
		out.denominator = key.Finish();
		out.complete = out.admitted > 0 && out.used == out.admitted;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Coverage audit allocation");
	}
}
} // namespace ReceiverSupport
} // namespace Core
} // namespace Rend2LightRecon
