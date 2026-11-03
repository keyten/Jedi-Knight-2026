// Trusted stock controller: geometry-only source-independent support census.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace RS = Rend2LightRecon::Core::ReceiverSupport;
static RS::Operator StockReceiverOperator(bool linear)
{
	RS::Operator op;
	Bake::Recipe r;
	r.falloff = linear ? Bake::Falloff::Linear : Bake::Falloff::InverseSquare;
	op.model.profile = Bake::NetRadiantProfile(r);
	op.model.law = Shape::Law::HistoricalBake;
	op.model.output = Shape::Output::RelativeIncident;
	op.model.allowAssumed = op.bake.allowAssumed = true;
	op.bake.output = Bake::Domain::EncodedRgb8BeforeQuantization;
	op.scope = "Assumed NetRadiant surface super1, static style 0, original equal-normal planar "
			   "receivers; Raven profile/transfer/scale unknown";
	return op;
}
static void ReceiverAudit(const O::Dataset &data, const O::Sources &s,
						  const std::shared_ptr<const StaticVisibility> &rays,
						  const std::array<std::vector<Selected>, 3> &folds,
						  const ReadContext &read, const std::string &output)
{
	auto started = std::chrono::steady_clock::now();
	auto op = StockReceiverOperator(false);
	Bake::EvaluationContext ctx;
	ctx.visibility = rays;
	ctx.geometryKey = rays->CertificationKey();
	std::map<uint32_t, size_t> images;
	for (size_t i = 0; i < s.images->Images().size(); ++i)
	{
		const auto &im = s.images->Images()[i];
		if (im.role == LightmapRole::Lighting && !im.missing)
			images.emplace(im.logicalIndex, i);
	}
	struct Count
	{
		uint64_t rows = 0;
		double area = 0;
	};
	std::map<RS::Reason, Count> categories;
	std::map<uint32_t, Count> issues;
	std::map<uint64_t, RS::Decision> cache;
	uint64_t raw = 0, mapped = 0, unknownArea = 0, checks = 0, cacheHits = 0;
	double mappedArea = 0;
	uint64_t originProbes = 0;
	std::map<unsigned, Count> styles;
	std::string witness = "null";
	GeometryKey censusKey;
	censusKey.U(RS::kVersion);
	censusKey.Digest(s.metadata.map.sha256);
	censusKey.Digest(RS::OperatorKey(op));
	for (size_t entry = 0; entry < data.Directory().size(); ++entry)
		if (data.Directory()[entry].kind == 15)
		{
			std::vector<O::Luxel> tile;
			Check(data.ReadLuxels(entry, tile));
			for (const auto &l : tile)
			{
				const auto &g = l.surface;
				if (!images.count(g.logicalLightmap) || images.at(g.logicalLightmap) != g.image)
					continue;
				++raw;
				if (!g.positionKnown || g.surface >= s.geometry->Surfaces().size())
				{
					++unknownArea;
					continue;
				}
				const auto &surface = s.geometry->Surfaces()[g.surface];
				if (g.coverage == LuxelCoverage::Mapped)
				{
					++mapped;
					mappedArea += g.areaWorldSquared;
				}
				RS::Geometry geometry;
				auto &r = geometry.receiver;
				r.sample = Number(O::SampleIdentity(s.metadata.map, s.images->Images()[g.image], g,
													l.classification.style.id));
				r.surface = g.surface + 1;
				r.material = g.materialIndex + 1;
				r.world = g.positionWorld;
				r.geometricNormal = g.geometricNormal;
				r.shadingNormal = g.shadingNormal;
				r.style = l.classification.style.id;
				r.mappedPlanar = surface.kind == BakedSurfaceKind::Face ||
								 surface.kind == BakedSurfaceKind::Triangles;
				r.compilerPlacementCertified = true;
				r.mappingErrorWorld = g.mappingErrorBoundWorld;
				geometry.areaWorldSquared = std::max(0., g.areaWorldSquared);
				geometry.block = r.surface;
				geometry.region = l.positiveRegion;
				// The stock adapter assumes independently mapped UV positions, not the raw BSP
				// basis. Keep LuxelBasisMismatch in its own census column; it is not a
				// numerical normal-response capability failure. No normal is replaced.
				uint32_t excluded = LuxelOverlap | LuxelDuplicate | LuxelDegenerate |
									LuxelShadingMissing | LuxelMultipleCharts | LuxelUnsupported |
									LuxelNormalSeam | LuxelMixedFootprint |
									LuxelOrientationUnknown | LuxelPrecisionExceeded;
				geometry.geometryKnown = g.coverage == LuxelCoverage::Mapped &&
										 g.normalOrientationKnown && g.shadingNormalKnown &&
										 surface.motion == SurfaceMotion::StaticWorld &&
										 !(g.issues & excluded) && !surface.excludedMaterial;
				geometry.opticsKnown =
					surface.materialFacts.available &&
					!(surface.materialFacts.signals & (MaterialCutout | MaterialTranslucent));
				// These are EXACT predicate classes of the current historical receiver capability,
				// not spatial bins. No RGB, source XYZ or brightness enters this cache.
				uint64_t cls = r.style;
				unsigned bit = 8;
				auto flag = [&](bool b) { cls |= uint64_t(b) << bit++; };
				flag(geometry.geometryKnown);
				flag(Finite(r.world) && Normal(r.geometricNormal) && Normal(r.shadingNormal));
				flag(r.mappedPlanar);
				flag(r.mappingErrorWorld > .002);
				flag(std::isfinite(r.mappingErrorWorld) && r.mappingErrorWorld >= 0);
				flag(Length(V(r.geometricNormal) - V(r.shadingNormal)) > 1e-5);
				auto found = cache.find(cls);
				RS::Decision d;
				if (found == cache.end())
				{
					Need(cache.size() < 4096, "Receiver capability class budget");
					d = RS::Check(op, geometry, ctx);
					cache.emplace(cls, d);
					++checks;
				}
				else
				{
					d = found->second;
					++cacheHits;
				}
				uint64_t probes = 0;
				Check(RS::CheckOrigin(op, geometry, ctx, s.domain.get(), d, probes));
				originProbes += probes;
				auto &c = categories[d.reason];
				++c.rows;
				c.area += geometry.areaWorldSquared;
				auto &style = styles[r.style];
				++style.rows;
				style.area += geometry.areaWorldSquared;
				for (unsigned b = 0; b < 14; ++b)
					if (g.issues & (1u << b))
					{
						auto &v = issues[1u << b];
						++v.rows;
						v.area += geometry.areaWorldSquared;
					}
				censusKey.U(r.sample);
				censusKey.U(unsigned(d.reason));
				censusKey.F(geometry.areaWorldSquared);
				if (r.sample == 7804426648716081ull)
				{
					std::ostringstream w;
					w << std::setprecision(12) << "{\"sample\":\"" << r.sample
					  << "\",\"surface\":" << r.surface
					  << ",\"reason\":" << Q(RS::ReasonName(d.reason))
					  << ",\"capability\":" << Q(Bake::CapabilityReasonName(d.capability))
					  << ",\"normal_difference\":"
					  << Length(V(r.geometricNormal) - V(r.shadingNormal))
					  << ",\"normal_replaced\":false,\"area\":" << geometry.areaWorldSquared << '}';
					witness = w.str();
				}
			}
		}
	auto rows = std::make_shared<Rows<RS::Geometry>>();
	for (unsigned role = 0; role < 3; ++role)
		for (const auto &v : folds[role])
		{
			RS::Geometry g;
			g.receiver = v.receiver;
			g.region = v.luxel.positiveRegion;
			g.block = v.block;
			g.role = RS::Role(role);
			g.areaWorldSquared = v.area;
			rows->v->push_back(g);
		}
	std::sort(rows->v->begin(), rows->v->end(), [](const RS::Geometry &a, const RS::Geometry &b) {
		return a.receiver.sample < b.receiver.sample;
	});
	RS::Request request;
	request.map = s.metadata.map.sha256;
	request.geometry = rays->CertificationKey();
	request.partition = SamplingPartitionKey();
	request.policy = Key("X15 predeclared common isq/linear stock receiver support v1");
	request.context = ctx;
	request.domain = s.domain;
	request.domainKey = s.metadata.map.sha256;
	request.operators = {op, StockReceiverOperator(true)};
	std::shared_ptr<const RS::Manifest> manifest;
	Check(RS::Build(request, rows, rows->v->size(), read, manifest));
	std::ofstream full(output + "/receiver-support-manifest.json");
	Check(RS::WriteJson(*manifest, full, read));
	full.close();
	uint64_t trainingPositive = 0, commonPositive = 0;
	for (const auto &row : folds[0])
	{
		bool positive = false;
		for (auto value : row.luxel.colour.raw.bytes)
			positive |= value > 0 && value < 255;
		trainingPositive += positive;
		const auto *e = manifest->Find(row.id);
		commonPositive += positive && e && e->common;
	}
	std::ofstream out(output + "/receiver-support.json");
	out << std::setprecision(15)
		<< "{\"schema\":\"org.openjk.receiver-support-stock.v1\",\"bsp_sha256\":"
		<< Q(Disk::Hex(s.metadata.map.sha256))
		<< ",\"full_census_key\":" << Q(Disk::Hex(censusKey.Finish()))
		<< ",\"training_positive_original\":" << trainingPositive
		<< ",\"training_positive_common\":" << commonPositive
		<< ",\"signal_checked_after_support_freeze\":true"
		<< ",\"raw_texels\":" << raw << ",\"mapped\":" << mapped
		<< ",\"mapped_area\":" << mappedArea << ",\"unknown_position_area_records\":" << unknownArea
		<< ",\"origin_probes\":" << originProbes << ",\"operator_checks\":" << checks
		<< ",\"receiver_class_cache_hits\":" << cacheHits << ",\"categories\":{";
	bool first = true;
	for (auto x : categories)
	{
		out << (first ? "" : ",") << Q(RS::ReasonName(x.first)) << ":{\"rows\":" << x.second.rows
			<< ",\"area\":" << x.second.area << '}';
		first = false;
	}
	out << "},\"mapping_issue_bits\":{";
	first = true;
	for (auto x : issues)
	{
		out << (first ? "" : ",") << Q(std::to_string(x.first)) << ":{\"rows\":" << x.second.rows
			<< ",\"area\":" << x.second.area << '}';
		first = false;
	}
	out << "},\"styles\":{";
	first = true;
	for (auto x : styles)
	{
		out << (first ? "" : ",") << Q(std::to_string(x.first)) << ":{\"rows\":" << x.second.rows
			<< ",\"area\":" << x.second.area << '}';
		first = false;
	}
	out << "},\"selected_requested\":" << manifest->Report().requested
		<< ",\"selected_common\":" << manifest->Report().admitted
		<< ",\"selected_area\":" << manifest->Report().requestedArea
		<< ",\"selected_common_area\":" << manifest->Report().commonArea
		<< ",\"selected_manifest_key\":" << Q(Disk::Hex(manifest->Key()))
		<< ",\"selected_roles\":[";
	for (unsigned role = 0; role < 3; ++role)
		out << (role ? "," : "") << "{\"requested\":" << manifest->Report().roleRequested[role]
			<< ",\"common\":" << manifest->Report().roleCommon[role]
			<< ",\"area\":" << manifest->Report().roleArea[role]
			<< ",\"common_area\":" << manifest->Report().roleCommonArea[role] << '}';
	out << "],\"academy_witness\":" << witness << ",\"seconds\":"
		<< std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
		<< ",\"census_uses_target_rgb\":false,\"candidate_xyz_used\":false,\"stock_profile\":"
		   "\"UnknownProfile\",\"conditional_profiles\":[\"NetRadiant inverse "
		   "square\",\"NetRadiant linear\"],\"scope\":\"Full captured mapped area is "
		   "observation-chart area, not a disjoint transport mesh or full-map free-space "
		   "certificate; no source-dependent paths certified\"}";
}
