// Explicit report generation. Never schedules reconstruction or field queries.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include <iomanip>
#include <locale>
#include <sstream>
namespace Rend2LightRecon
{
namespace Core
{
namespace Diagnostics
{
namespace
{
std::string Q(const std::string &s)
{
	std::ostringstream o;
	o << '"';
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			o << '\\' << char(c);
		else if (c < 32)
			o << "\\u00"
			  << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
		else
			o << char(c);
	}
	o << '"';
	return o.str();
}
void V(std::ostream &o, Vec3 v)
{
	o << '[' << v.x << ',' << v.y << ',' << v.z << ']';
}
void E(std::ostream &o, const Estimate &e)
{
	if (!e.known)
		o << "null";
	else
	{
		o << "{\"value\":" << e.value << ",\"bounds\":";
		if (e.bounded)
			o << '[' << e.lower << ',' << e.upper << ']';
		else
			o << "null";
		o << '}';
	}
}
void Membership(std::ostream &o, const TransportMembership &m)
{
	o << "{\"version\":" << m.version << ",\"model\":" << Q(m.model)
	  << ",\"configuration\":" << Q(std::to_string(m.configuration))
	  << ",\"alternative_group\":" << Q(std::to_string(m.alternativeGroup))
	  << ",\"colour\":" << unsigned(m.colour) << ",\"profile\":" << Q(m.profile)
	  << ",\"domain\":" << Q(m.domain) << ",\"generation\":" << Q(m.generation)
	  << ",\"geometry\":" << Q(m.geometry) << ",\"uncertainty\":" << m.uncertainty
	  << ",\"roots_complete\":" << (m.rootsComplete ? "true" : "false")
	  << ",\"local_sun_sky_reflected_unresolved_membership\":[" << unsigned(m.directLocal) << ','
	  << unsigned(m.directSun) << ',' << unsigned(m.directSky) << ',' << unsigned(m.reflected)
	  << ',' << unsigned(m.unresolved) << "],\"addition\":" << unsigned(m.addition)
	  << ",\"replacement\":" << unsigned(m.replacement) << ",\"roots\":[";
	bool first = true;
	for (auto &r : m.roots)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"id\":" << Q(std::to_string(r.source)) << ",\"kind\":" << unsigned(r.kind) << '}';
	}
	o << "],\"reflection_orders\":[";
	first = true;
	for (auto n : m.reflectionOrders)
	{
		if (!first)
			o << ',';
		first = false;
		o << n;
	}
	o << "]}";
}
void Extras(std::ostream &o, const ReconstructedLightingAsset &a)
{
	const auto &b = a.assessment;
	const auto &c = b.coverage;
	o << ",\"coverage\":{\"training_requested\":" << c.requestedTraining
	  << ",\"training_used\":" << c.usedTraining
	  << ",\"training_unavailable\":" << c.unavailableTraining
	  << ",\"validation_requested\":" << c.requestedValidation
	  << ",\"validation_used\":" << c.usedValidation
	  << ",\"heldout_requested\":" << c.requestedHeldout << ",\"heldout_used\":" << c.usedHeldout
	  << ",\"supported_area_world_squared\":";
	E(o, c.supportedAreaWorldSquared);
	o << ",\"certified_visibility_fraction\":";
	E(o, c.certifiedVisibilityFraction);
	o << ",\"primaries_known\":" << (c.primariesKnown ? "true" : "false")
	  << ",\"historical_profile_known\":" << (c.historicalProfileKnown ? "true" : "false")
	  << ",\"limitations\":[";
	bool first = true;
	for (auto &s : c.limitations)
	{
		if (!first)
			o << ',';
		first = false;
		o << Q(s);
	}
	o << "]},\"providers\":[";
	first = true;
	for (auto &p : b.providers)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"name\":" << Q(p.name) << ",\"outcome\":" << Q(ProviderOutcomeName(p.outcome))
		  << ",\"required\":" << (p.required ? "true" : "false")
		  << ",\"search_complete\":" << (p.searchComplete ? "true" : "false")
		  << ",\"reason\":" << Q(p.reason) << ",\"rays\":" << p.rays << ",\"work\":" << p.work
		  << ",\"bytes\":" << p.bytes << '}';
	}
	o << "],\"source_admissions\":[";
	first = true;
	for (auto &p : b.admissions)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"source\":" << Q(std::to_string(p.source)) << ",\"use\":" << unsigned(p.use)
		  << ",\"status\":" << unsigned(p.status) << ",\"reason\":" << Q(p.reason) << '}';
	}
	o << "],\"transport\":[";
	first = true;
	for (auto &f : a.transport)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"source\":" << Q(std::to_string(f.source.id))
		  << ",\"dataset\":" << Q(std::to_string(f.dataset))
		  << ",\"quantity\":" << unsigned(f.quantity) << ",\"unit\":" << unsigned(f.unit)
		  << ",\"calibration\":" << Q(std::to_string(f.calibration)) << ",\"membership\":";
		Membership(o, f.membership);
		o << '}';
	}
	o << "],\"contributions\":[";
	first = true;
	for (auto &r : a.contributions)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"dataset\":" << Q(std::to_string(r.dataset))
		  << ",\"quantity\":" << unsigned(r.quantity) << ",\"unit\":" << unsigned(r.unit)
		  << ",\"domain\":" << Q(r.domain)
		  << ",\"configuration\":" << Q(std::to_string(r.configuration))
		  << ",\"replacement_supported_proofs\":" << r.replacement.supported
		  << ",\"replacement_contradictions\":" << r.replacement.contradicted
		  << ",\"replacement_scope\":" << Q(r.replacement.scope) << ",\"membership\":";
		Membership(o, r.membership);
		o << '}';
	}
	o << "],\"configurations\":[";
	first = true;
	for (auto &c : a.lightingConfigurations)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"id\":" << Q(std::to_string(c.id)) << ",\"key\":" << Q(c.key)
		  << ",\"profile\":" << Q(c.profile)
		  << ",\"calibration_hypothesis\":" << Q(c.calibrationHypothesis)
		  << ",\"search_complete\":" << (c.searchComplete ? "true" : "false")
		  << ",\"first_bounce\":" << (c.firstBounce ? "true" : "false") << ",\"sources\":[";
		bool firstSource = true;
		for (auto &s : c.sources)
		{
			if (!firstSource)
				o << ',';
			firstSource = false;
			o << "{\"track\":" << Q(std::to_string(s.track)) << ",\"kind\":" << unsigned(s.kind)
			  << ",\"root\":" << unsigned(s.root) << ",\"position\":";
			if (s.position.known)
				V(o, s.position.world);
			else
				o << "null";
			o << ",\"amplitude\":";
			V(o, s.amplitude);
			o << ",\"amplitude_domain\":" << Q(s.amplitudeDomain) << '}';
		}
		o << "]}";
	}
	o << "],\"uncertainty\":[";
	first = true;
	for (auto &u : a.uncertainty)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"source\":" << Q(std::to_string(u.source)) << ",\"method\":" << Q(u.method)
		  << ",\"absolute_scale_known\":" << (u.absoluteScaleKnown ? "true" : "false")
		  << ",\"complete\":" << (u.complete ? "true" : "false") << ",\"source_count_range\":["
		  << u.minimumCount << ',' << u.maximumCount << "],\"relative_peak\":";
		E(o, u.relativePeak);
		o << ",\"direction_spread_radians\":";
		E(o, u.directionSpreadRadians);
		o << ",\"existence_event\":" << Q(u.existence.event)
		  << ",\"existence_calibration\":" << Q(u.existence.calibrationHash)
		  << ",\"existence_probability\":";
		E(o, u.existence.probability);
		o << ",\"position_scatter\":";
		if (!u.positionScatter.known)
			o << "null";
		else
		{
			o << "{\"method\":" << Q(u.positionScatter.method)
			  << ",\"samples\":" << u.positionScatter.samples << ",\"descriptive_reference\":";
			V(o, u.positionScatter.reference);
			o << ",\"xx_xy_xz_yy_yz_zz\":[" << u.positionScatter.xx << ',' << u.positionScatter.xy
			  << ',' << u.positionScatter.xz << ',' << u.positionScatter.yy << ','
			  << u.positionScatter.yz << ',' << u.positionScatter.zz << "]}";
		}
		o << ",\"factors\":[";
		bool firstFactor = true;
		for (auto &f : u.factors)
		{
			if (!firstFactor)
				o << ',';
			firstFactor = false;
			o << "{\"cause\":" << unsigned(f.cause) << ",\"status\":" << unsigned(f.status)
			  << ",\"reason\":" << Q(f.reason) << '}';
		}
		o << "]}";
	}
	o << "],\"provenance\":[";
	first = true;
	for (auto &p : a.provenanceRecords)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"id\":" << Q(std::to_string(p.id)) << ",\"kind\":" << unsigned(p.kind)
		  << ",\"producer\":" << Q(p.producer) << ",\"hash\":" << Q(p.contentHash) << '}';
	}
	o << ']';
}
} // namespace
Error Json(const Scene &s, std::ostream &o, const Limits &l)
{
	o.imbue(std::locale::classic());
	std::string check;
	auto valid = Encode(s, check, l);
	if (!valid)
		return valid;
	o << std::setprecision(17)
	  << "{\"schema\":\"org.openjk.lighting-debug/1\",\"map\":" << Q(s.mapPath)
	  << ",\"map_sha256\":" << Q(Disk::Hex(s.map))
	  << ",\"fingerprint\":" << Q(Disk::Hex(s.fingerprint)) << ",\"algorithm\":" << Q(s.algorithm)
	  << ",\"settings\":" << Q(s.settings) << ",\"scope\":" << Q(s.scope)
	  << ",\"generation\":" << s.generation
	  << ",\"world_units_per_metre\":" << s.units.worldUnitsPerMeter
	  << ",\"complete\":" << (s.complete ? "true" : "false") << ",\"requested\":" << s.requested
	  << ",\"omitted\":" << s.omitted << ",\"rays\":" << s.rays
	  << ",\"unknown_queries\":" << s.unknownQueries << ",\"invalid_queries\":" << s.invalidQueries
	  << ",\"records\":[";
	bool first = true;
	for (const auto &r : s.records)
	{
		if (!first)
			o << ',';
		first = false;
		o << "{\"id\":" << Q(std::to_string(r.id)) << ",\"source\":" << Q(std::to_string(r.source))
		  << ",\"sample\":" << Q(std::to_string(r.sample))
		  << ",\"region\":" << Q(std::to_string(r.region))
		  << ",\"configuration\":" << Q(std::to_string(r.configuration))
		  << ",\"alternative\":" << Q(std::to_string(r.alternative))
		  << ",\"layer\":" << Q(Name(r.layer)) << ",\"state\":" << Q(Name(r.state))
		  << ",\"primitive\":" << unsigned(r.primitive)
		  << ",\"geometry_known\":" << (r.geometryKnown ? "true" : "false") << ",\"a\":";
		V(o, r.a);
		o << ",\"b\":";
		V(o, r.b);
		o << ",\"c\":";
		V(o, r.c);
		o << ",\"value\":";
		if (r.valueKnown)
			V(o, r.value);
		else
			o << "null";
		o << ",\"lower\":";
		if (r.lowerKnown)
			V(o, r.lower);
		else
			o << "null";
		o << ",\"upper\":";
		if (r.upperKnown)
			V(o, r.upper);
		else
			o << "null";
		o << ",\"unit\":" << unsigned(r.unit) << ",\"issues\":" << r.issues
		  << ",\"quantity\":" << Q(r.quantity) << ",\"domain\":" << Q(r.domain)
		  << ",\"profile\":" << Q(r.profile) << ",\"reason\":" << Q(r.reason)
		  << ",\"provenance\":" << Q(r.provenance) << '}';
	}
	o << "]}\n";
	return o ? Error() : Failure(Code::ProviderError, "Diagnostic JSON write");
}
Error Report(const Snapshot &snapshot, const WorldReport *world, std::ostream &o,
			 std::ostream *human, const Scene *scene, const Limits &l)
{
	o.imbue(std::locale::classic());
	try
	{
		auto result = snapshot.Asset();
		const ReconstructedLightingAsset *a = result ? result.value.get() : nullptr;
		if (a && (a->emitters.size() + a->datasets.size() + a->contributions.size() +
					  a->transport.size() + a->diagnostics.size() + a->provenanceRecords.size() +
					  a->assessment.providers.size() >
				  l.maxRecords))
			return Failure(Code::MemoryLimit, "Report record budget");
		size_t types[6] = {}, states[3] = {}, relative = 0, knownPosition = 0;
		if (a)
			for (auto &e : a->emitters)
			{
				if (unsigned(e.kind) < 6)
					++types[unsigned(e.kind)];
				++states[unsigned(e.source.state)];
				relative += e.emission.unit == PhotometricUnit::Relative;
				knownPosition += e.position.known;
			}
		o << std::setprecision(17)
		  << "{\"schema\":\"org.openjk.reconstruction-report/1\",\"availability\":"
		  << Q(AvailabilityName(snapshot.State()))
		  << ",\"availability_is_physical_certification\":false,\"generation\":"
		  << snapshot.Generation() << ",\"asset_schema\":" << (a ? Q(a->schema) : "null")
		  << ",\"asset_version\":" << (a ? std::to_string(a->version) : "null")
		  << ",\"message\":" << Q(snapshot.Message())
		  << ",\"sources_by_type\":{\"point\":" << types[1] << ",\"spot\":" << types[2]
		  << ",\"rectangle\":" << types[3] << ",\"tube\":" << types[4]
		  << ",\"directional\":" << types[5] << "},\"candidate\":" << states[0]
		  << ",\"resolved\":" << states[1] << ",\"rejected\":" << states[2]
		  << ",\"relative_sources\":" << relative << ",\"known_positions\":" << knownPosition;
		if (world)
			o << ",\"cache\":{\"state\":" << Q(CacheCodeName(world->cacheStatus))
			  << ",\"key_complete\":" << (world->keyComplete ? "true" : "false")
			  << ",\"key\":" << Q(world->cacheKey) << ",\"policy\":" << unsigned(world->policy)
			  << ",\"solver_steps\":" << world->steps
			  << ",\"read_bytes\":" << world->cacheWork.readBytes
			  << ",\"selected_payload_bytes\":" << world->cacheWork.payloadBytes
			  << ",\"prepared_reservation_bytes\":" << world->cacheWork.preparedBudgetBytes
			  << ",\"open_seconds\":" << world->cacheWork.openSeconds
			  << ",\"prepare_seconds\":" << world->cacheWork.prepareSeconds
			  << ",\"publication_seconds\":" << world->secondsToPublication << '}';
		if (a)
		{
			const auto &b = a->assessment;
			o << ",\"assessment\":{\"present\":" << (b.present ? "true" : "false")
			  << ",\"algorithm\":" << Q(b.algorithm) << ",\"settings\":" << Q(b.settingsHash)
			  << ",\"fingerprint\":" << Q(b.fingerprint) << ",\"profile\":" << Q(b.selectedProfile)
			  << ",\"conditional_sources\":" << b.conditionalSources
			  << ",\"strict_evaluated\":" << (b.strictEvaluated ? "true" : "false")
			  << ",\"sun_evidence\":" << unsigned(b.sunEvidence)
			  << ",\"environment_evidence\":" << unsigned(b.environmentEvidence)
			  << ",\"training_interval_mse\":";
			E(o, b.trainingIntervalMse);
			o << ",\"heldout_interval_mse\":";
			E(o, b.heldoutIntervalMse);
			o << "},\"sun_records\":" << a->suns.size()
			  << ",\"environment_records\":" << a->environments.size()
			  << ",\"transport_records\":" << a->transport.size()
			  << ",\"visibility_datasets\":" << a->visibility.size() << ",\"sources\":[";
			bool first = true;
			for (auto &e : a->emitters)
			{
				if (!first)
					o << ',';
				first = false;
				o << "{\"id\":" << Q(std::to_string(e.source.id))
				  << ",\"kind\":" << unsigned(e.kind) << ",\"state\":" << unsigned(e.source.state)
				  << ",\"position\":";
				if (e.position.known)
					V(o, e.position.world);
				else
					o << "null";
				o << ",\"photon_travel_axis\":";
				if (e.frame.known)
					V(o, e.frame.forward);
				else
					o << "null";
				o << ",\"colour_chromaticity\":";
				if (e.emission.color.known)
					V(o, e.emission.color.rgb);
				else
					o << "null";
				o << ",\"range_world\":";
				E(o, e.rangeWorld);
				o << ",\"angular_kind\":" << unsigned(e.angular.kind)
				  << ",\"two_sided\":" << (e.angular.twoSided ? "true" : "false");
				o << ",\"cone_cos_inner_outer\":[" << e.angular.cosInner << ','
				  << e.angular.cosOuter << ']';
				o << ",\"geometry_halfwidth_halfheight_halflength_radius\":";
				if (e.geometry.known)
					o << '[' << e.geometry.halfWidthWorld << ',' << e.geometry.halfHeightWorld
					  << ',' << e.geometry.halfLengthWorld << ',' << e.geometry.radiusWorld << ']';
				else
					o << "null";
				o << ",\"intensity\":";
				E(o, e.emission.intensity);
				o << ",\"unit\":" << unsigned(e.emission.unit)
				  << ",\"calibration\":" << Q(std::to_string(e.emission.calibration))
				  << ",\"provenance\":" << Q(std::to_string(e.source.provenance))
				  << ",\"alternative_group\":" << Q(std::to_string(e.source.hypothesisGroup))
				  << ",\"existence\":";
				E(o, e.source.evidence.physicalEmitterConfidence);
				o << ",\"position_uncertainty_world\":";
				E(o, e.position.sigmaWorld);
				o << ",\"exact_light_suitability\":" << unsigned(e.source.evidence.exactLight)
				  << ",\"field_suitability\":" << unsigned(e.source.evidence.approximateIrradiance)
				  << '}';
			}
			o << "],\"calibrations\":[";
			first = true;
			for (const auto &c : a->calibrations)
			{
				if (!first)
					o << ',';
				first = false;
				o << "{\"id\":" << Q(std::to_string(c.id)) << ",\"kind\":" << unsigned(c.kind)
				  << ",\"method\":" << Q(c.method)
				  << ",\"provenance\":" << Q(std::to_string(c.provenance)) << '}';
			}
			o << "],\"datasets\":[";
			first = true;
			for (auto &d : a->datasets)
			{
				if (!first)
					o << ',';
				first = false;
				o << "{\"id\":" << Q(std::to_string(d.id)) << ",\"purpose\":" << unsigned(d.purpose)
				  << ",\"encoding\":" << Q(d.encoding) << ",\"version\":" << d.encodingVersion
				  << ",\"elements\":" << d.elementCount
				  << ",\"resident_bytes\":null,\"content_hash\":" << Q(d.contentHash) << '}';
			}
			o << "],\"warnings\":[";
			first = true;
			for (auto &d : a->diagnostics)
			{
				if (!first)
					o << ',';
				first = false;
				o << "{\"code\":" << Q(d.code) << ",\"message\":" << Q(d.message) << '}';
			}
			o << ']';
		}
		if (a)
			Extras(o, *a);
		o << ",\"capabilities\":[";
		for (unsigned j = 0; j < unsigned(Capability::Count); ++j)
		{
			if (j)
				o << ',';
			auto cap = snapshot.GetCapability(Capability(j));
			o << "{\"capability\":" << j << ",\"status\":" << unsigned(cap.status)
			  << ",\"records\":" << cap.records << '}';
		}
		o << ']';
		if (scene)
		{
			o << ",\"spatial\":";
			auto e = Json(*scene, o, l);
			if (!e)
				return e;
		}
		o << "}\n";
		if (human)
		{
			*human << "Reconstructed lighting: " << AvailabilityName(snapshot.State())
				   << " (not physical certification)\n"
				   << "sources=" << (a ? a->emitters.size() : 0) << " candidate=" << states[0]
				   << " resolved=" << states[1] << " rejected=" << states[2]
				   << " relative=" << relative << "\n";
			if (a)
			{
				const auto &coverage = a->assessment.coverage;
				*human << "schema=" << a->schema << " version=" << a->version
					   << " fingerprint=" << a->assessment.fingerprint << "\n"
					   << "algorithm=" << a->assessment.algorithm
					   << " settings=" << a->assessment.settingsHash
					   << " profile=" << a->assessment.selectedProfile << "\n"
					   << "type-counts point/spot/rectangle/tube/directional=" << types[1] << '/'
					   << types[2] << '/' << types[3] << '/' << types[4] << '/' << types[5] << "\n"
					   << "sun/environment/transport/visibility-datasets=" << a->suns.size() << '/'
					   << a->environments.size() << '/' << a->transport.size() << '/'
					   << a->visibility.size() << "; missing records are unknown, not zero\n"
					   << "training=" << coverage.usedTraining << '/' << coverage.requestedTraining
					   << " validation=" << coverage.usedValidation << '/'
					   << coverage.requestedValidation << " heldout=" << coverage.usedHeldout << '/'
					   << coverage.requestedHeldout << "\n";
				size_t count = 0;
				for (auto &warning : a->diagnostics)
				{
					if (count++ >= 16)
						break;
					*human << "warning " << warning.code << ": " << warning.message << '\n';
				}
			}
			if (world)
				*human << "cache=" << CacheCodeName(world->cacheStatus) << " steps=" << world->steps
					   << " prepared-reservation=" << world->cacheWork.preparedBudgetBytes
					   << " bytes\n";
			if (scene)
				*human << "diagnostic records=" << scene->records.size()
					   << " omitted=" << scene->omitted
					   << " unknown-queries=" << scene->unknownQueries << " scope=" << scene->scope
					   << '\n';
		}
		return o ? Error() : Failure(Code::ProviderError, "Report write failure");
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Report allocation");
	}
}
} // namespace Diagnostics
} // namespace Core
} // namespace Rend2LightRecon
