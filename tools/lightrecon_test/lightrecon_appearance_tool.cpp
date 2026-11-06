// X18: original encoded surface appearance, no emitter inference or SI
// conversion. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/field_builder.h"
#include "lightrecon/geometry_key.h"
#include "lightrecon/offline.h"
#include "lightrecon/persistent.h"
#include "source_footprint.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace F = Fields;
namespace P = Probes;
static void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
static uint64_t Number(const Disk::Digest &d)
{
	uint64_t r = 0;
	for (unsigned k = 0; k < 8; ++k)
		r |= uint64_t(d[k]) << (8 * k);
	return r ? r : 1;
}
static bool Inside(Bounds b, Vec3 p)
{
	return p.x >= b.minWorld.x && p.y >= b.minWorld.y && p.z >= b.minWorld.z &&
		   p.x <= b.maxWorld.x && p.y <= b.maxWorld.y && p.z <= b.maxWorld.z;
}
static std::string Quote(const std::string &s)
{
	std::string r = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\')
			r += '\\';
		if (c >= 32)
			r += c;
	}
	return r + '"';
}
struct Row
{
	Id id = 0, surface = 0;
	Vec3 position, query, normal, rgb;
	double area = 0;
	unsigned role = 0, normalBin = 0;
	bool saturated = false;
};
static unsigned Bin(Vec3 n)
{
	double v[3] = {n.x, n.y, n.z};
	unsigned k = 0;
	for (unsigned j = 1; j < 3; ++j)
		if (std::abs(v[j]) > std::abs(v[k]))
			k = j;
	return 2 * k + (v[k] >= 0);
}
// Frozen spatial blocks and a >=8 WU plus footprint guard. No target values.
static int Role(Vec3 p, double footprint)
{
	GeometryKey k;
	k.U(20261107);
	double a[3] = {p.x, p.y, p.z};
	for (double x : a)
	{
		double cell = std::floor(x / 128), local = x - cell * 128;
		if (std::min(local, 128 - local) < 8 + footprint)
			return -1;
		k.U(int64_t(cell));
	}
	unsigned t = Number(k.Finish()) % 5;
	return t < 3 ? 0 : t == 3 ? 1 : 2;
}
struct Metrics
{
	uint64_t requested = 0, available = 0, positive = 0;
	double se = 0, ref = 0, maximum = 0, area = 0, supportedArea = 0;
	void Add(const Row &r, bool ok, Vec3 prediction = {})
	{
		++requested;
		area += r.area;
		if (!ok)
			return;
		++available;
		supportedArea += r.area;
		V t(r.rgb), e = V(prediction) - t;
		se += Dot(e, e);
		ref += Dot(t, t);
		positive += std::max({t.x, t.y, t.z}) > 0;
		maximum = std::max(maximum, std::max({std::abs(e.x), std::abs(e.y), std::abs(e.z)}));
	}
	void Json(std::ostream &o) const
	{
		o << "{\"requested\":" << requested << ",\"available\":" << available
		  << ",\"positive_targets_used\":" << positive << ",\"selected_receiver_area\":" << area
		  << ",\"supported_selected_area\":" << supportedArea << ",\"rmse_rgb8\":";
		if (available)
			o << std::sqrt(se / (3 * available));
		else
			o << "null";
		o << ",\"relative_rmse\":";
		if (available && ref > 0)
			o << std::sqrt(se / ref);
		else
			o << "null";
		o << ",\"max_channel_error\":";
		if (available)
			o << maximum;
		else
			o << "null";
		o << '}';
	}
};
// Original historical entity proxy, static style-0 only; no physical
// interpretation. Follows tr_light.cpp trilerp, missing style rejection and
// /1023 direction table.
static bool Grid(const LightgridObservations *g, Vec3 p, Vec3 n, Vec3 &out)
{
	if (!g)
		return false;
	const auto &l = g->Layout();
	double v[3] = {p.x, p.y, p.z}, a[3] = {l.originWorld.x, l.originWorld.y, l.originWorld.z},
		   s[3] = {l.spacingWorld.x, l.spacingWorld.y, l.spacingWorld.z};
	int pos[3];
	double f[3];
	for (unsigned k = 0; k < 3; ++k)
	{
		if (!l.bounds[k] || s[k] <= 0)
			return false;
		double t = (v[k] - a[k]) / s[k];
		pos[k] = int(std::floor(t));
		f[k] = t - std::floor(t);
		pos[k] = std::max(0, std::min(pos[k], int(l.bounds[k]) - 1));
	}
	V amb, dir, axis;
	double total = 0;
	for (unsigned j = 0; j < 8; ++j)
	{
		uint64_t index = 0, step = 1;
		double w = 1;
		for (unsigned k = 0; k < 3; ++k)
		{
			index += uint64_t(pos[k] + ((j >> k) & 1)) * step;
			step *= l.bounds[k];
			w *= ((j >> k) & 1) ? f[k] : 1 - f[k];
		}
		if (w == 0)
			continue;
		GridCell c;
		if (!g->Cell(index, c) || c.presence != GridPresence::Present || c.styles[0] != 0 ||
			c.styles[1] != 255)
			continue;
		total += w;
		amb = amb + V(Vec3(c.ambient[0][0], c.ambient[0][1], c.ambient[0][2])) * w;
		dir = dir + V(Vec3(c.directed[0][0], c.directed[0][1], c.directed[0][2])) * w;
		axis = axis + V(c.direction.entityToSource) * w;
	}
	if (total <= 0)
		return false;
	if (total < .99)
	{
		amb = amb * (1 / total);
		dir = dir * (1 / total);
	}
	double cosine = Length(axis) > 0 ? std::max(0., Dot(axis, V(n)) / Length(axis)) : 0;
	out = (amb + dir * cosine).Float();
	return true;
}
static F::Request Request(const Offline::Sources &s, std::shared_ptr<const StaticVisibility> vis,
						  std::shared_ptr<const P::Topology> topology, const std::vector<Row> &rows)
{
	F::Request q;
	q.visibility = vis;
	q.topology = topology;
	auto a = std::make_shared<ReconstructedLightingAsset>();
	a->lineage = "X18 original-BSP appearance only";
	a->provenance = 1;
	a->units = s.metadata.units;
	Provenance prov;
	prov.id = 1;
	prov.kind = ProvenanceKind::LegacyLightmap;
	prov.producer = "org.openjk.legacy-appearance/1";
	prov.contentHash = Disk::Hex(s.metadata.map.sha256);
	a->provenanceRecords.push_back(prov);
	Calibration c;
	c.id = 1;
	c.provenance = 1;
	c.kind = CalibrationKind::UncalibratedRelative;
	c.method = "Original RGB8 code appearance; unknown transfer/primaries/calibration";
	a->calibrations.push_back(c);
	q.asset = a;
	auto &x = q.selection;
	x.route = F::Route::ObservationAppearance;
	x.field = 100;
	x.dataset = 101;
	x.geometryDataset = 102;
	x.topologyDataset = 103;
	x.provenance = 1;
	x.calibration = 1;
	x.quantity = ConditionalQuantity::LegacyAppearance;
	x.domain = FieldDomain::LegacyAppearance;
	x.colour = ContributionColour::EncodedSourceRGB;
	x.primariesKnown = x.linearTransferKnown = false;
	x.profile = "UnknownProfile/appearance-no-inversion";
	x.appearanceDomain = "original-static-style0-surface-rgb8-code/1";
	x.provenanceText = "X18 L0 surface observation proxy; immutable sample XYZ; generated "
					   "probes; unknown roots/path orders. SP captured material context. Query "
					   "16 WU from original receiver; not a physical entity reference.";
	q.options.appearanceRadiusWorld = 192;
	q.options.appearanceNeighbours = 32;
	q.options.allowConditionalStatic = true;
	q.options.appearanceMinimumValidationFraction = .25;
	q.options.maximumRelativeAngularRMSE = .6;
	q.options.maxRays = 200000;
	q.options.maxWork = 100000000;
	q.options.maxProbes = 10000;
	q.options.maximumWallSeconds = 180;
	for (const auto &r : rows)
		if (r.role < 2)
		{
			F::AppearanceSample sample(r.id, r.role ? r.query : r.position, r.rgb, 1);
			sample.surface = r.role ? 0 : r.surface;
			(r.role ? q.appearanceValidation : q.appearance).push_back(sample);
		}
	return q;
}
static bool Sample(const F::Artifact &a, const std::shared_ptr<const F::Prepared> &p, const Row &r,
				   Vec3 &out)
{
	if (!p)
		return false;
	ConditionalFieldQuery q;
	q.optIn = true;
	q.field.id = 100;
	q.quantity = ConditionalQuantity::LegacyAppearance;
	q.allowConditionalStaticGeometry = true;
	q.positionWorld = r.query;
	q.directionOrNormalWorld = r.normal;
	ConditionalFieldSample v;
	auto code = p->ConditionalField(a.asset, q, v);
	if (code != Status::Ok)
		return false;
	out = v.value;
	return true;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 3)
			throw std::runtime_error("appearance_tool original.jkobs report.json");
		auto begin = std::chrono::steady_clock::now();
		std::shared_ptr<const Offline::Dataset> d;
		Check(Offline::OpenFile(argv[1], Offline::Options(), d));
		Offline::Sources s;
		Offline::RegionSnapshot snapshot;
		Check(d->Load(s, snapshot));
		if (!s.domain || !s.geometry || !s.images)
			throw std::runtime_error("Missing original sources");
		// Complete scoped OFFLINE input: this immutable observation archive contains
		// every captured value/material/geometry used here. It does not attest the
		// unavailable original Raven shader/compiler production environment.
		std::unique_ptr<Disk::FileReader> inputReader;
		auto opened = Disk::FileReader::OpenFile(argv[1], inputReader);
		if (!opened)
			throw std::runtime_error(opened.message);
		Disk::Digest archiveKey;
		auto hashed = Disk::HashContent(*inputReader, archiveKey, 1024ull << 20);
		if (!hashed)
			throw std::runtime_error(hashed.message);
		ReadContext read;
		read.maxBytes = 256ull << 20;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Check(capture.SetMap(s.metadata.map));
		std::vector<Vec3> anchors;
		for (auto surf : s.geometry->Surfaces())
		{
			if (surf.motion == SurfaceMotion::StaticWorld && surf.kind == BakedSurfaceKind::Face &&
				!surf.vertices.empty())
				anchors.push_back(surf.vertices[surf.vertices.size() / 2].position);
			for (auto &v : surf.vertices)
				v.colourPresent = false;
			Check(capture.Add(std::move(surf)));
		}
		Check(capture.Freeze());
		VisibilityBuildOptions vo;
		vo.maxBytes = 256ull << 20;
		vo.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> vis;
		Check(StaticVisibility::Build(capture.Acquire(), vo, read, vis, s.domain));
		if (anchors.size() < 4)
			throw std::runtime_error("Too few geometry anchors");
		Bounds boxes[3];
		for (unsigned j = 0; j < 3; ++j)
		{
			auto p = anchors[(j + 1) * anchors.size() / 4];
			boxes[j].known = true;
			boxes[j].minWorld = (V(p) - V(Vec3(256, 256, 256))).Float();
			boxes[j].maxWorld = (V(p) + V(Vec3(256, 256, 256))).Float();
		}
		std::map<Id, Row> pools[3][3];
		uint64_t mapped = 0, eligible = 0, gaps = 0;
		double mappedArea = 0, eligibleArea = 0;
		std::map<std::string, uint64_t> exclusions;
		for (size_t e = 0; e < d->Directory().size(); ++e)
			if (d->Directory()[e].kind == 15)
			{
				std::vector<Offline::Luxel> tile;
				Check(d->ReadLuxels(e, tile));
				for (const auto &l : tile)
				{
					const auto &g = l.surface;
					if (g.coverage != LuxelCoverage::Mapped || !g.positionKnown)
						continue;
					++mapped;
					mappedArea += g.areaWorldSquared;
					if (g.surface >= s.geometry->Surfaces().size())
					{
						++exclusions["surface"];
						continue;
					}
					const auto &surf = s.geometry->Surfaces()[g.surface];
					if (surf.motion != SurfaceMotion::StaticWorld ||
						surf.kind != BakedSurfaceKind::Face)
					{
						++exclusions["nonplanar_or_moving"];
						continue;
					}
					if (!g.normalOrientationKnown || !g.shadingNormalKnown ||
						g.areaWorldSquared <= 0 ||
						(g.issues & (LuxelOverlap | LuxelDuplicate | LuxelMultipleCharts |
									 LuxelMixedFootprint | LuxelPrecisionExceeded)))
					{
						++exclusions["mapping_or_side"];
						continue;
					}
					if (!l.classification.diffuseFitCandidate || l.classification.style.id != 0 ||
						l.colour.raw.encoding != Decode::Encoding::Rgb8)
					{
						++exclusions["material_style_encoding"];
						continue;
					}
					++eligible;
					eligibleArea += g.areaWorldSquared;
					bool inROI = false;
					for (auto b : boxes)
						inROI |= Inside(b, g.positionWorld);
					if (!inROI)
						continue;
					const auto &image = s.images->Images()[g.image];
					if (image.width != 128 || image.height != 128 ||
						image.role != LightmapRole::Lighting)
					{
						++exclusions["nonoriginal_image_scope"];
						continue;
					}
					double radius = 0, footprintArea = 0;
					try
					{
						auto footprint = SourceFootprint::Footprint(
							surf, g.styleSlot, g.x, g.y, g.geometricNormal, footprintArea);
						for (size_t j = 0; j < footprint.count; ++j)
							for (auto vertex : footprint.data[j].world)
								radius = std::max(radius, Length(V(vertex) - V(g.positionWorld)));
					}
					catch (const std::exception &)
					{
						++exclusions["unsupported_footprint"];
						continue;
					}
					int role = Role(g.positionWorld, radius);
					if (role < 0)
					{
						++gaps;
						continue;
					}
					Row r;
					r.surface = Id(g.surface) + 1;
					r.position = g.positionWorld;
					r.query = (V(g.positionWorld) + V(g.geometricNormal) * 16).Float();
					r.normal = g.shadingNormal;
					r.normalBin = Bin(r.normal);
					r.area = g.areaWorldSquared;
					r.role = unsigned(role);
					r.id = Number(
						Offline::SampleIdentity(s.metadata.map, s.images->Images()[g.image], g, 0));
					GeometryKey key;
					key.U(int64_t(std::floor(g.positionWorld.x / 32)));
					key.U(int64_t(std::floor(g.positionWorld.y / 32)));
					key.U(int64_t(std::floor(g.positionWorld.z / 32)));
					key.U(r.normalBin);
					Id correlation = Number(key.Finish());
					for (unsigned j = 0; j < 3; ++j)
						if (Inside(boxes[j], g.positionWorld))
						{
							auto &pool = pools[j][role];
							auto it = pool.find(correlation);
							if (it != pool.end() && it->second.id <= r.id)
								continue;
							// Geometry-only admission above; targets copied only for selected
							// records.
							r.rgb = Vec3(l.colour.raw.bytes[0], l.colour.raw.bytes[1],
										 l.colour.raw.bytes[2]);
							r.saturated = std::max({r.rgb.x, r.rgb.y, r.rgb.z}) == 255;
							pool[correlation] = r;
							if (pool.size() > 256)
								pool.erase(std::prev(pool.end()));
						}
				}
			}
		double extraction =
			std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
		std::ofstream out(argv[2]);
		out << std::setprecision(17) << "{\"schema\":\"org.openjk.appearance-stock/1\",\"map\":"
			<< Quote(s.metadata.map.path)
			<< ",\"bsp_sha256\":" << Quote(Disk::Hex(s.metadata.map.sha256))
			<< ",\"mapped_rows\":" << mapped << ",\"mapped_chart_area\":" << mappedArea
			<< ",\"eligible_rows\":" << eligible << ",\"eligible_chart_area\":" << eligibleArea
			<< ",\"gap_rows\":" << gaps << ",\"extraction_seconds\":" << extraction
			<< ",\"exclusions\":{";
		bool comma = false;
		for (const auto &x : exclusions)
		{
			out << (comma ? "," : "") << Quote(x.first) << ':' << x.second;
			comma = true;
		}
		out << "},\"rois\":[";
		for (unsigned roi = 0; roi < 3; ++roi)
		{
			auto start = std::chrono::steady_clock::now();
			P::Options po;
			po.bounds = boxes[roi];
			po.coarseSpacingWorld = 64;
			po.minimumSpacingWorld = 16;
			po.clearanceWorld = 1;
			po.maxNodes = 50000;
			po.maxProbes = 10000;
			po.maxLinks = 80000;
			po.maxWork = 64000000;
			P::Input in;
			in.map = s.metadata.map.sha256;
			in.geometry = vis->CertificationKey();
			in.domain = s.domain;
			in.visibility = vis;
			in.dynamic = s.dynamic;
			GeometryKey dk;
			dk.Digest(in.map);
			dk.U(P::kVersion);
			dk.U(s.domain->Source().planes.size());
			in.domainKey = dk.Finish();
			std::shared_ptr<const P::Topology> topology;
			auto te = P::Build(in, po, read, topology);
			std::vector<Row> rows;
			unsigned counts[3] = {};
			GeometryKey manifest;
			for (unsigned k = 0; k < 3; ++k)
				for (auto &x : pools[roi][k])
				{
					const auto &r = x.second;
					rows.push_back(r);
					++counts[k];
					manifest.U(r.id);
					manifest.U(r.role);
					manifest.V(r.position);
					manifest.V(r.query);
					manifest.V(r.normal);
					manifest.F(r.area);
				}
			auto manifestKey = Disk::Hex(manifest.Finish());
			F::Artifact art;
			std::shared_ptr<const F::Prepared> prep;
			Error fe = Failure(Code::Unavailable, te ? "Insufficient independent folds"
													 : ("Topology unavailable: " + te.message));
			if (te && counts[0] >= 8 && counts[1] >= 6 && counts[2] >= 6)
			{
				auto q = Request(s, vis, topology, rows);
				fe = F::Build(q, read, art);
				if (fe)
					Check(F::Prepare(art, read, prep));
			}
			// Freeze all coefficients and admission before touching final held-out
			// values.
			std::map<std::string, Metrics> metrics, common, groups;
			V average;
			for (const auto &r : rows)
				if (!r.role)
					average = average + V(r.rgb);
			if (counts[0])
				average = average * (1. / counts[0]);
			uint64_t agreement = 0, blocked = 0, unknown = 0, clear = 0, held = 0;
			std::vector<double> times;
			for (const auto &r : rows)
				if (r.role == 2)
				{
					++held;
					Vec3 predictions[4];
					bool ok[4] = {counts[0] > 0, false, false, false};
					predictions[0] = average.Float();
					double nearest = std::numeric_limits<double>::infinity();
					for (const auto &train : rows)
						if (!train.role)
						{
							double distance = Length(V(train.position) - V(r.query));
							if (distance < nearest)
							{
								nearest = distance;
								predictions[1] = train.rgb;
								ok[1] = true;
							}
						}
					ok[2] = Grid(s.grid.get(), r.query, r.normal, predictions[2]);
					auto tick = std::chrono::steady_clock::now();
					ok[3] = Sample(art, prep, r, predictions[3]);
					times.push_back(std::chrono::duration<double, std::nano>(
										std::chrono::steady_clock::now() - tick)
										.count());
					const char *names[] = {"constant", "nearest_surface",
										   "legacy_grid_entity_proxy", "geometry_L0"};
					bool all = ok[0] && ok[1] && ok[2] && ok[3];
					for (unsigned k = 0; k < 4; ++k)
					{
						metrics[names[k]].Add(r, ok[k], predictions[k]);
						if (all)
							common[names[k]].Add(r, true, predictions[k]);
					}
					groups["normal_" + std::to_string(r.normalBin)].Add(r, ok[3], predictions[3]);
					groups[r.saturated ? "saturated" : "not_saturated"].Add(r, ok[3],
																			predictions[3]);
					groups[std::max({r.rgb.x, r.rgb.y, r.rgb.z}) <= 16
							   ? "dark"
							   : std::max({r.rgb.x, r.rgb.y, r.rgb.z}) >= 64 ? "bright" : "mid"]
						.Add(r, ok[3], predictions[3]);
					SpatialLocation loc;
					Check(s.domain->Locate(r.query, loc));
					groups[loc.space == SpaceClass::Exterior
							   ? "exterior"
							   : loc.space == SpaceClass::Free ? "interior" : "unknown_space"]
						.Add(r, ok[3], predictions[3]);
					if (ok[3])
					{
						P::QueryPolicy policy;
						policy.allowConditionalStatic = true;
						auto support = topology->Lookup(r.query, policy);
						for (unsigned j = 0; j < support.count; ++j)
						{
							const auto &probes = topology->Probes();
							auto it =
								std::find_if(probes.begin(), probes.end(), [&](const P::Probe &p) {
									return p.id == support.probes[j];
								});
							if (it == probes.end())
								throw std::runtime_error("Bad support ID");
							if (Length(V(it->positionWorld) - V(r.query)) < 1e-6)
								continue;
							RayQuery ray;
							ray.startWorld = r.query;
							ray.endWorld = it->positionWorld;
							ray.policy.facing = FacingPolicy::TwoSided;
							RayResult hit;
							Check(vis->Query(ray, read, hit));
							blocked += hit.state == VisibilityState::Blocked;
							unknown += hit.state == VisibilityState::Unknown;
							clear += hit.state == VisibilityState::Visible;
						}
					}
				}
			uint64_t chunks = 0;
			bool roundtrip = false, corruptRejected = false;
			bool cacheRoundtrip = false;
			Disk::Persistent::Work cacheWork;
			double storeSeconds = 0;
			Disk::Digest bytesKey{};
			if (fe)
			{
				F::Artifact decoded;
				Check(
					F::Decode(art.asset, art.chunks, 100, vis->CertificationKey(), read, decoded));
				std::shared_ptr<const F::Prepared> loaded;
				Check(F::Prepare(decoded, read, loaded));
				GeometryKey ck;
				for (const auto &c : art.chunks)
				{
					chunks += c.bytes.size();
					ck.Digest(Disk::Hash(c.bytes.data(), c.bytes.size()));
				}
				bytesKey = ck.Finish();
				roundtrip = true;
				for (const auto &r : rows)
					if (r.role == 2)
					{
						Vec3 a, b;
						bool x = Sample(art, prep, r, a), y = Sample(decoded, loaded, r, b);
						if (x != y || (x && Length(V(a) - V(b)) > 1e-4))
							roundtrip = false;
					}
				auto broken = art.chunks;
				if (!broken.empty() && !broken.back().bytes.empty())
					broken.back().bytes.back() ^= 1;
				F::Artifact rejected;
				corruptRejected =
					!F::Decode(art.asset, broken, 100, vis->CertificationKey(), read, rejected);
				agreement =
					(prep->Supports(Capability::IndirectSampling) == Status::Unavailable &&
					 prep->Supports(Capability::IncomingRadianceSampling) == Status::Unavailable);
				Disk::Fingerprint fingerprint;
				fingerprint.algorithm = "org.openjk.offline-original-appearance";
				fingerprint.algorithmVersion = F::kVersion;
				fingerprint.units = art.asset.units;
				fingerprint.complete.fill(true);
				for (unsigned k = 0; k < unsigned(Disk::InputKind::Count); ++k)
				{
					Disk::InputContent content;
					content.kind = Disk::InputKind(k);
					content.name = "complete-frozen-SP-capture-context/" + std::to_string(k);
					content.bytes = inputReader->Size();
					content.sha256 =
						k == unsigned(Disk::InputKind::Bsp) ? s.metadata.map.sha256 : archiveKey;
					fingerprint.inputs.push_back(content);
				}
				Disk::InputContent settings;
				settings.kind = Disk::InputKind::Settings;
				settings.name = "X18/frozen-geometry-fold-and-appearance-policy-v2";
				std::string policy = "L0;128block;8gap+exact-footprint;32correlation;16query;"
									 "192radius;32nearest;64/16probe;validation=.25/.6;" +
									 manifestKey;
				settings.bytes = policy.size();
				settings.sha256 = Disk::Hash(policy.data(), policy.size());
				fingerprint.inputs.push_back(settings);
				Disk::Cache cache(std::string(argv[2]) + "-cache/roi-" + std::to_string(roi));
				Disk::Persistent::Selection selection;
				Disk::Persistent::Item item;
				item.id = 100;
				item.geometry = vis->CertificationKey();
				selection.fields.push_back(item);
				auto storeStart = std::chrono::steady_clock::now();
				auto stored =
					Disk::Persistent::Store(cache, fingerprint, art.asset, art.chunks, selection);
				if (!stored)
					throw std::runtime_error(stored.message);
				storeSeconds =
					std::chrono::duration<double>(std::chrono::steady_clock::now() - storeStart)
						.count();
				BuildProduct cached;
				auto hit = Disk::Persistent::Load(cache, fingerprint, selection, cached, cacheWork);
				if (!hit)
					throw std::runtime_error(hit.message);
				cacheRoundtrip = cached.asset && cached.asset->emitters.empty() &&
								 cached.evaluator &&
								 cached.evaluator->Supports(Capability::IndirectSampling) ==
									 Status::Unavailable &&
								 cached.evaluator->Supports(Capability::IncomingRadianceSampling) ==
									 Status::Unavailable;
				for (const auto &row : rows)
					if (row.role == 2)
					{
						ConditionalFieldQuery query;
						query.optIn = true;
						query.field.id = 100;
						query.quantity = ConditionalQuantity::LegacyAppearance;
						query.positionWorld = row.query;
						query.directionOrNormalWorld = row.normal;
						query.allowConditionalStaticGeometry = true;
						ConditionalFieldSample value;
						Vec3 expected;
						bool a = Sample(art, prep, row, expected);
						bool b = cached.evaluator->ConditionalField(*cached.asset, query, value) ==
								 Status::Ok;
						cacheRoundtrip &=
							a == b && (!a || Length(V(expected) - V(value.value)) <= 1e-4);
					}
			}
			std::sort(times.begin(), times.end());
			double ns = times.empty() ? 0 : times[times.size() / 2],
				   p95 = times.empty()
							 ? 0
							 : times[std::min(times.size() - 1, size_t(times.size() * .95))];
			out << (roi ? "," : "") << "{\"sample_manifest\":" << Quote(manifestKey)
				<< ",\"cache_roundtrip\":" << (cacheRoundtrip ? "true" : "false")
				<< ",\"cache_file_bytes\":" << cacheWork.fileBytes
				<< ",\"cache_read_bytes\":" << cacheWork.readBytes
				<< ",\"cache_store_seconds\":" << storeSeconds
				<< ",\"cache_load_seconds\":" << cacheWork.openSeconds + cacheWork.prepareSeconds
				<< ",\"cache_prepared_budget_bytes\":" << cacheWork.preparedBudgetBytes
				<< ",\"lighting_supported_volume\":"
				<< (fe ? art.descriptor->lightingVolumeWorldCubed : 0)
				<< ",\"topology_reason\":" << Quote(te.message) << ',';
			out << "\"ordinal\":" << roi << ",\"fold_counts\":[" << counts[0] << ',' << counts[1]
				<< ',' << counts[2] << "],\"topology_code\":" << unsigned(te.code)
				<< ",\"field_code\":" << unsigned(fe.code)
				<< ",\"field_reason\":" << Quote(fe.message)
				<< ",\"probes\":" << (topology ? topology->Probes().size() : 0)
				<< ",\"supported_static_volume\":"
				<< (topology ? topology->Diagnostics().supportedStaticVolume : 0)
				<< ",\"analysis_volume\":134217728,\"dynamic_conditional_probes\":"
				<< (topology ? topology->Diagnostics().conditional : 0)
				<< ",\"available_values\":" << art.values.records.size()
				<< ",\"validation_available\":"
				<< (art.work.appearanceValidationRequested ? std::to_string(art.work.appearanceValidationAvailable) : "null")
				<< ",\"validation_requested\":" << counts[1] << ",\"validation_relative_rmse\":"
				<< (art.work.appearanceValidationAvailable ? std::to_string(art.work.appearanceValidationRelativeRMSE) : "null")
				<< ",\"field_rays\":" << art.work.rays
				<< ",\"evidence_visible\":" << art.work.appearanceVisible
				<< ",\"evidence_blocked\":" << art.work.appearanceBlocked
				<< ",\"evidence_unknown\":" << art.work.appearanceUnknown
				<< ",\"chunk_bytes\":" << chunks
				<< ",\"chunk_content_key\":" << Quote(Disk::Hex(bytesKey))
				<< ",\"roundtrip\":" << (roundtrip ? "true" : "false")
				<< ",\"corrupt_rejected\":" << (corruptRejected ? "true" : "false")
				<< ",\"physical_capabilities_unavailable\":" << (agreement ? "true" : "false")
				<< ",\"support_check_blocked\":" << blocked
				<< ",\"support_check_unknown\":" << unknown << ",\"support_check_clear\":" << clear
				<< ",\"query_p50_ns\":" << ns << ",\"query_p95_ns\":" << p95 << ",\"seconds\":"
				<< std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
				<< ",\"metrics\":{";
			comma = false;
			for (const auto &m : metrics)
			{
				out << (comma ? "," : "") << Quote(m.first) << ':';
				m.second.Json(out);
				comma = true;
			}
			out << "},\"common_domain\":{";
			comma = false;
			for (const auto &m : common)
			{
				out << (comma ? "," : "") << Quote(m.first) << ':';
				m.second.Json(out);
				comma = true;
			}
			out << "},\"groups\":{";
			comma = false;
			for (const auto &m : groups)
			{
				out << (comma ? "," : "") << Quote(m.first) << ':';
				m.second.Json(out);
				comma = true;
			}
			out << "}}";
		}
		out << "]}";
		return out ? 0 : 2;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 2;
	}
}
