// Full-map input census + bounded production producer driver. CPU only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/geometry_key.h"
#include "lightrecon/offline.h"
#include "lightrecon/producer.h"
#include "lightrecon/shadow_evidence.h"
#include "lightrecon/transport_mesh.h"
#include "lightrecon/sampling.h"
#include "source_footprint.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
using namespace Bake::Detail;
namespace O = Offline;
namespace J = Joint;
namespace P = Producer;
namespace T = Indirect::Topology;
static void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
static void Need(bool v, const char *m)
{
	if (!v)
		throw std::runtime_error(m);
}
static Disk::Digest Key(const std::string &s)
{
	return Disk::Hash(s.data(), s.size());
}
static std::string Q(const std::string &v)
{
	std::string r = "\"";
	for (unsigned char c : v)
	{
		if (c == '"' || c == '\\')
			r += '\\';
		if (c >= 32)
			r += char(c);
		else
			r += ' ';
	}
	return r + '"';
}
static uint64_t Number(const Disk::Digest &d)
{
	uint64_t n = 0;
	for (int i = 0; i < 8; ++i)
		n |= uint64_t(d[i]) << (i * 8);
	return n ? n : 1;
}
static void Vec(std::ostream &s, Vec3 v)
{
	s << '[' << v.x << ',' << v.y << ',' << v.z << ']';
}
template <class T> static Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
template <class T> class Rows final : public ChunkSource<T>
{
  public:
	std::shared_ptr<std::vector<T>> v = std::make_shared<std::vector<T>>();
	Error Read(uint64_t a, size_t n, const ReadContext &, Chunk<T> &o) const override
	{
		if (a > v->size() || n > v->size() - a)
			return Failure(Code::ProviderError, "Short immutable fold");
		o = VectorChunk<T>(v, size_t(a), n);
		return {};
	}
};
struct Selected
{
	O::Luxel luxel;
	Bake::Receiver receiver;
	Id id = 0, block = 0;
	unsigned role = 0;
	double area = 0, radius = 0, weight = 0;
};
using Cell = std::tuple<int64_t, int64_t, int64_t, int>;
struct Census
{
	uint64_t texels = 0, coverage[5] = {}, eligible = 0, unsupported = 0, gap = 0, invalid = 0,
			 quantized = 0, saturated = 0, channels = 0, deluxe = 0, region = 0, moving = 0,
			 unknownTransfer = 0, unknownPrimaries = 0, transportNormalsUnsupported = 0;
	double mappedArea = 0, eligibleArea = 0;
	std::set<Cell> cells;
	std::map<uint32_t, std::array<uint64_t, 5>> pages;
	std::map<unsigned, uint64_t> kinds, styles;
	std::map<Id, double> materialArea;
};
static std::shared_ptr<const SurfaceGeometry> Sanitized(const O::Sources &s)
{
	SurfaceLimits limits;
	limits.maxBytes = 128ull << 20;
	SurfaceCapture c(limits);
	c.Begin(BuildPolicy::ForceRebuild);
	Check(c.SetMap(s.metadata.map));
	for (auto v : s.geometry->Surfaces())
	{
		for (auto &p : v.vertices)
		{
			p.colourPresent = false;
			for (auto &b : p.colour)
				for (auto &x : b)
					x = 0;
		}
		Check(c.Add(std::move(v)));
	}
	Check(c.Freeze());
	return c.Acquire();
}
static unsigned Split(Vec3 p, double radius, Id &block)
{
	int64_t c[3];
	double xyz[3] = {p.x - 17, p.y - 31, p.z - 47};
	GeometryKey k;
	k.U(20261031);
	for (unsigned i = 0; i < 3; ++i)
	{
		c[i] = int64_t(std::floor(xyz[i] / 256));
		if (std::min(xyz[i] - c[i] * 256, (c[i] + 1) * 256 - xyz[i]) <= 3 * radius + 1)
			return 3;
		k.U(uint64_t(c[i]));
	}
	block = Number(k.Finish());
	unsigned v = unsigned(block % 10);
	return v < 6 ? 0 : v < 8 ? 1 : 2;
}
static bool InSelection(const Bounds &b, Vec3 p)
{
	return p.x >= b.minWorld.x && p.y >= b.minWorld.y && p.z >= b.minWorld.z &&
		   p.x < b.maxWorld.x && p.y < b.maxWorld.y && p.z < b.maxWorld.z;
}
static std::array<std::vector<Selected>, 3> Scan(const O::Dataset &data, const O::Sources &s,
												 Census &census, const std::string &folder,
												 Bounds *chooseFocus = nullptr,
												 const Bounds *selection = nullptr,
												 double cellWorld = 512)
{
	std::map<Cell, Selected> bins;
	std::map<Cell, double> weights;
	std::map<uint32_t, size_t> selectedImage;
	for (size_t i = 0; i < s.images->Images().size(); ++i)
	{
		const auto &im = s.images->Images()[i];
		if (im.role == LightmapRole::Lighting && !im.missing)
			selectedImage.emplace(im.logicalIndex, i);
	}
	for (size_t e = 0; e < data.Directory().size(); ++e)
		if (data.Directory()[e].kind == 15)
		{
			std::vector<O::Luxel> tile;
			Check(data.ReadLuxels(e, tile));
			for (const auto &l : tile)
			{
				const auto &g = l.surface;
				if (selectedImage[g.logicalLightmap] != g.image)
					continue;
				++census.texels;
				++census.coverage[unsigned(g.coverage)];
				++census.pages[g.logicalLightmap][unsigned(g.coverage)];
				++census.styles[l.classification.style.id];
				census.quantized += (l.colour.channels[0] & Decode::Quantized) != 0;
				census.unknownTransfer += l.colour.resolvedTransfer == Decode::Transfer::Unknown;
				census.unknownPrimaries += l.colour.resolvedPrimaries == Decode::Primaries::Unknown;
				for (unsigned k = 0; k < 3; ++k)
				{
					++census.channels;
					census.saturated += (l.colour.channels[k] & Decode::Saturated) != 0;
				}
				if (!g.positionKnown || g.surface >= s.geometry->Surfaces().size())
					continue;
				const auto &surface = s.geometry->Surfaces()[g.surface];
				++census.kinds[unsigned(surface.kind)];
				if (g.coverage == LuxelCoverage::Mapped)
					census.mappedArea += g.areaWorldSquared;
				census.deluxe += l.deluxe.state == DeluxeState::Available;
				census.region += l.positiveRegion != 0;
				if (surface.motion != SurfaceMotion::StaticWorld)
				{
					++census.moving;
					continue;
				}
				if (g.coverage != LuxelCoverage::Mapped || !l.classification.diffuseFitCandidate ||
					l.classification.style.id != 0 || !g.normalOrientationKnown ||
					!g.shadingNormalKnown || g.areaWorldSquared <= 0)
					continue;
				if (surface.kind != BakedSurfaceKind::Face ||
					l.colour.raw.encoding != Decode::Encoding::Rgb8 ||
					(g.issues & (LuxelOverlap | LuxelDuplicate | LuxelMultipleCharts |
								 LuxelMixedFootprint | LuxelPrecisionExceeded)))
				{
					++census.unsupported;
					continue;
				}
				if (Length(V(g.shadingNormal) - V(g.geometricNormal)) >= 1e-4)
				{
					++census.transportNormalsUnsupported;
					continue;
				}
				++census.eligible;
				census.eligibleArea += g.areaWorldSquared;
				census.materialArea[g.materialIndex + 1] += g.areaWorldSquared;
				double n[3] = {g.geometricNormal.x, g.geometricNormal.y, g.geometricNormal.z};
				int axis = int(
					std::max_element(n, n + 3,
									 [](double a, double b) { return std::abs(a) < std::abs(b); }) -
					n);
				if (selection && !InSelection(*selection, g.positionWorld))
					continue;
				Cell cell(int64_t(std::floor(g.positionWorld.x / cellWorld)),
						  int64_t(std::floor(g.positionWorld.y / cellWorld)),
						  int64_t(std::floor(g.positionWorld.z / cellWorld)),
						  axis * 2 + (n[axis] >= 0));
				census.cells.insert(cell);
				weights[cell] += g.areaWorldSquared;
				Id id = Number(O::SampleIdentity(s.metadata.map, s.images->Images()[g.image], g,
												 l.classification.style.id));
				auto found = bins.find(cell);
				if (found != bins.end() && found->second.id < id)
					continue;
				Selected value;
				value.id = id;
				value.luxel = l;
				value.receiver.sample = id;
				value.receiver.surface = g.surface + 1;
				value.receiver.material = g.materialIndex + 1;
				value.receiver.style = 0;
				value.receiver.world = g.positionWorld;
				value.receiver.geometricNormal = g.geometricNormal;
				value.receiver.shadingNormal = g.shadingNormal;
				value.receiver.mappedPlanar = true;
				value.receiver.compilerPlacementCertified =
					true; // EXPLICIT assumed recipe, never stock verified placement
				value.receiver.mappingErrorWorld = g.mappingErrorBoundWorld;
				try
				{
					value.receiver.footprint = SourceFootprint::Footprint(
						surface, g.styleSlot, g.x, g.y, g.geometricNormal, value.area);
				}
				catch (const std::exception &)
				{
					++census.invalid;
					continue;
				}
				if (std::abs(value.area - g.areaWorldSquared) >
					1e-4 * std::max(1., g.areaWorldSquared))
				{
					++census.invalid;
					continue;
				}
				for (size_t i = 0; i < value.receiver.footprint.count; ++i)
					for (const auto &v : value.receiver.footprint.data[i].world)
						value.radius = std::max(value.radius, Length(V(v) - V(g.positionWorld)));
				value.radius += g.mappingErrorBoundWorld + 1e-4;
				value.role = Split(g.positionWorld, value.radius, value.block);
				if (value.role == 3)
				{
					++census.gap;
					continue;
				}
				bins[cell] = value;
				Need(bins.size() <= 65536, "Geometry stratum budget exceeded");
			}
		}
	if (chooseFocus && !weights.empty())
	{
		// Predeclared geometry-only local follow-up. Prefer normal diversity,
		// then mapped receiver area, then lexicographic grid coordinates. No RGB.
		using XYZ = std::tuple<int64_t, int64_t, int64_t>;
		std::map<XYZ, std::pair<unsigned, double>> choices;
		for (const auto &w : weights)
		{
			auto &c =
				choices[XYZ(std::get<0>(w.first), std::get<1>(w.first), std::get<2>(w.first))];
			++c.first;
			c.second += w.second;
		}
		auto best = choices.begin();
		for (auto i = choices.begin(); i != choices.end(); ++i)
			if (i->second > best->second)
				best = i;
		chooseFocus->known = true;
		chooseFocus->minWorld = Vec3(float(std::get<0>(best->first) * cellWorld),
									 float(std::get<1>(best->first) * cellWorld),
									 float(std::get<2>(best->first) * cellWorld));
		chooseFocus->maxWorld = Vec3(chooseFocus->minWorld.x + float(cellWorld),
									 chooseFocus->minWorld.y + float(cellWorld),
									 chooseFocus->minWorld.z + float(cellWorld));
	}
	std::array<std::vector<Selected>, 3> fold;
	for (auto &v : bins)
	{
		v.second.weight = weights[v.first];
		fold[v.second.role].push_back(v.second);
	}
	const size_t cap[3] = {512, 256, 256};
	for (unsigned role = 0; role < 3; ++role)
	{
		auto &rows = fold[role];
		std::sort(rows.begin(), rows.end(),
				  [](const Selected &a, const Selected &b) { return a.id < b.id; });
		if (rows.size() > cap[role])
			rows.resize(cap[role]);
	}
	std::ofstream t(folder + "/selected-geometry.tsv");
	t << "id\trole\tpage\tx\ty\tsurface\tworld_x\tworld_y\tworld_"
		 "z\tarea\tweight\tradius\tregion\n";
	for (unsigned role = 0; role < 3; ++role)
		for (auto &v : fold[role])
		{
			const auto &g = v.luxel.surface;
			t << v.id << '\t' << role << '\t' << g.logicalLightmap << '\t' << g.x << '\t' << g.y
			  << '\t' << g.surface << '\t' << g.positionWorld.x << '\t' << g.positionWorld.y << '\t'
			  << g.positionWorld.z << '\t' << v.area << '\t' << v.weight << '\t' << v.radius << '\t'
			  << v.luxel.positiveRegion << '\n';
		}
	return fold;
}
#include "sampling_audit.h"
struct Validator final : J::Validation
{
	const J::Request &q;
	std::shared_ptr<Rows<J::Observation>> rows;
	Disk::Digest key;
	Validator(const J::Request &a, std::shared_ptr<Rows<J::Observation>> b, Disk::Digest k)
		: q(a), rows(b), key(k)
	{
	}
	Disk::Digest Key() const override
	{
		return key;
	}
	Error Evaluate(const J::Model &m, const ReadContext &r, J::Score &s) const override
	{
		return J::EvaluateFrozen(q, m, rows, rows->v->size(), J::Role::Validation, key, r, s);
	}
};
struct Final final : P::FinalEvaluation
{
	const J::Request &q;
	std::shared_ptr<Rows<J::Observation>> rows;
	Disk::Digest key;
	Final(const J::Request &a, std::shared_ptr<Rows<J::Observation>> b, Disk::Digest k)
		: q(a), rows(b), key(k)
	{
	}
	Disk::Digest Key() const override
	{
		return key;
	}
	Error Assess(const J::Model &m, const ReadContext &r, P::FinalReceipt &s) const override
	{
		s.evidence = "Independent untouched stock spatial held-out; no "
					 "emitter/compiler truth";
		return J::EvaluateFrozen(q, m, rows, rows->v->size(), J::Role::Heldout, key, r, s.score);
	}
};
struct SolarValidator final : P::SolarValidation
{
	Chunk<Solar::Sample> rows;
	Disk::Digest map;
	Bake::EvaluationContext geometry;
	mutable Solar::Result result;
	Error Assess(const Solar::FitResult &f, const ReadContext &r, Solar::Result &out) const override
	{
		auto e = Solar::ValidateAndPublish(f, map, rows, geometry, r, result);
		out = result;
		return e;
	}
};
static void Diagnostics(const O::Sources &s, const O::RegionSnapshot &graph,
						const std::shared_ptr<const StaticVisibility> &rays,
						const std::array<std::vector<Selected>, 3> &folds, const ReadContext &read,
						const std::string &output)
{
	const auto &g = rays->Report();
	std::ofstream f(output + "/geometry.json");
	f << "{\"scene_complete\":" << (g.complete ? "true" : "false")
	  << ",\"spatial_coverage_complete\":" << (g.spatialCoverageComplete ? "true" : "false")
	  << ",\"triangles\":" << g.triangles << ",\"patch_triangles\":" << g.patchTriangles
	  << ",\"unsupported_bounded\":" << g.unsupportedBounds
	  << ",\"unsupported_unbounded\":" << g.unboundedUnsupported
	  << ",\"excluded_flares\":" << g.excludedFlares
	  << ",\"patch_error_world\":" << g.maximumPatchErrorWorld
	  << ",\"peak_build_bytes_accounted\":" << g.peakBuildBytes
	  << ",\"dynamic_bounds\":" << s.dynamic.size()
	  << ",\"region_scope\":\"captured bounded ROI, not full "
		 "map\",\"region_free_cells\":"
	  << graph.report.freeCells << ",\"region_solid_cells\":" << graph.report.solidCells
	  << ",\"region_unknown_cells\":" << graph.report.unknownCells
	  << ",\"regions\":" << graph.report.regions
	  << ",\"components\":" << graph.report.staticComponents
	  << ",\"apertures\":" << graph.report.apertures << '}';
	std::ofstream signals(output + "/folds.json");
	signals << "{\"scope\":\"trusted evaluation controller; never solver source "
			   "discovery\",\"folds\":[";
	for (unsigned role = 0; role < 3; ++role)
	{
		double sum = 0, mse = 0, area = 0;
		size_t positive = 0, saturated = 0;
		std::set<Id> surfaces, blocks;
		std::set<unsigned> normals;
		for (const auto &v : folds[role])
		{
			sum += v.weight;
			area += v.area;
			surfaces.insert(v.receiver.surface);
			blocks.insert(v.block);
			unsigned peak = 0;
			const auto &n = v.receiver.geometricNormal;
			double a[3] = {n.x, n.y, n.z};
			unsigned axis = unsigned(
				std::max_element(a, a + 3,
								 [](double x, double y) { return std::abs(x) < std::abs(y); }) -
				a);
			normals.insert(axis * 2 + (a[axis] >= 0));
			for (unsigned c = 0; c < 3; ++c)
			{
				unsigned b = v.luxel.colour.raw.bytes[c];
				peak = std::max(peak, b);
				mse += v.weight * double(b) * b / 3;
				saturated += b == 255;
			}
			positive += peak >= 8;
		}
		signals << (role ? "," : "") << "{\"role\":" << role << ",\"rows\":" << folds[role].size()
				<< ",\"positive_rows_ge_8_codes\":" << positive
				<< ",\"censored_channels\":" << saturated
				<< ",\"source_surfaces\":" << surfaces.size()
				<< ",\"independent_blocks\":" << blocks.size()
				<< ",\"normal_major_axis_bins\":" << normals.size()
				<< ",\"selected_actual_area\":" << area << ",\"represented_stratum_area\":" << sum
				<< ",\"black_interval_mse\":" << (sum ? mse / sum : 0) << '}';
	}
	signals << "]}";
	// Geometry-only witnesses: retained fixture evidence -> independent receiver
	// positions. These are not fitted source visibility or proof of true lamps.
	std::ofstream t(output + "/fixture-rays.tsv");
	t << "source_evidence_id\treceiver_id\tstate\tissues\thit_surface\tprimitive_"
		 "tests\n";
	uint64_t counts[3] = {}, queries = 0;
	std::vector<const EmitterEvidence *> seeds;
	if (s.evidence)
	{
		for (const auto &e : s.evidence->Evidence())
			if (e.positionKnown && e.staticEligible)
				seeds.push_back(&e);
	}
	const size_t sources = std::min(size_t(8), seeds.size());
	for (size_t k = 0; k < sources; ++k)
	{
		const auto &e = *seeds[k * seeds.size() / sources];
		const auto &rows = folds[0];
		size_t stride = std::max(size_t(1), rows.size() / 64);
		for (size_t i = 0; i < rows.size() && i / stride < 64; i += stride)
		{
			const auto &v = rows[i];
			RayQuery q;
			q.startWorld = v.receiver.world;
			q.endWorld = e.position;
			q.originSurface = v.receiver.surface;
			q.bias.normalKnown = true;
			q.bias.geometricNormal = v.receiver.geometricNormal;
			RayResult hit;
			auto err = rays->Query(q, read, hit);
			if (!err)
				continue;
			++queries;
			++counts[unsigned(hit.state)];
			t << e.id << '\t' << v.id << '\t' << unsigned(hit.state) << '\t' << hit.issues << '\t'
			  << hit.firstHit.surface << '\t' << hit.primitiveTests << '\n';
		}
	}
	std::ofstream ray(output + "/ray-summary.json");
	ray << "{\"scope\":\"bounded geometry-only fixture-centre witnesses; "
		   "unverified physical XYZ\",\"queries\":"
		<< queries << ",\"visible\":" << counts[0] << ",\"blocked\":" << counts[1]
		<< ",\"unknown\":" << counts[2] << '}';
}
static std::string WorkJson(const J::Work &w)
{
	std::ostringstream s;
	s << "{\"evaluations\":" << w.evaluations << ",\"rays\":" << w.rays
	  << ",\"forward_calls\":" << w.forwardCalls << ",\"models\":" << w.attemptedModels
	  << ",\"rejected_models\":" << w.rejectedModels << ",\"bytes\":" << w.accountedBytes << '}';
	return s.str();
}
struct TransportPreparation
{
	std::shared_ptr<const T::Mesh> mesh;
	std::shared_ptr<const Indirect::Transfer> transfer;
};
static TransportPreparation PrepareTransport(const O::Sources &s,
											 const std::shared_ptr<const SurfaceGeometry> &geometry,
											 const std::shared_ptr<const StaticVisibility> &rays,
											 const std::array<std::vector<Selected>, 3> &folds,
											 const Bounds &scope, const std::string &folder,
											 const ReadContext &read)
{
	auto start = std::chrono::steady_clock::now();
	std::vector<const Selected *> observations;
	for (const auto &fold : folds)
		for (const auto &v : fold)
			observations.push_back(&v);
	std::sort(observations.begin(), observations.end(),
			  [](const Selected *a, const Selected *b) { return a->id < b->id; });
	std::ofstream witness(folder + "/transport-overlap-witnesses.tsv");
	witness << std::setprecision(17)
			<< "a\tb\trole_a\trole_b\tsurface_a\tsurface_b\tpage_a\tx_a\ty_a\tpage_b\tx_b\ty_"
			   "b\ttriangle_a\ttriangle_b\trelation\tshared_area\tsame_material\tshared_original_"
			   "triangle\tvertices_a\tvertices_b\n";
	uint64_t overlapPairs = 0, oppositePairs = 0, crossingPairs = 0, crossFold = 0, sameSurface = 0,
			 sameMaterial = 0, duplicateOriginal = 0, nodePairs = 0;
	std::vector<std::pair<const Selected *, const Selected *>> failures;
	double sharedArea = 0;
	// Diagnostic replay of the old level-0/1 centroid rule, not a repaired topology.
	auto nodes = [](const Bake::Receiver &r) {
		std::vector<V> result;
		for (size_t i = 0; i < r.footprint.count; ++i)
		{
			const auto &t = r.footprint.data[i];
			V a(t.world[0]), b(t.world[1]), c(t.world[2]);
			result.push_back((a + b + c) / 3);
			V ab = (a + b) * .5, bc = (b + c) * .5, ca = (c + a) * .5;
			for (auto v :
				 {(a + ab + ca) / 3, (ab + b + bc) / 3, (ca + bc + c) / 3, (ab + bc + ca) / 3})
				result.push_back(v);
		}
		return result;
	};
	std::vector<std::vector<V>> sampleNodes;
	for (auto v : observations)
		sampleNodes.push_back(nodes(v->receiver));
	auto rawTriangleKeys = [&](Id surface) {
		std::set<std::vector<float>> keys;
		const auto &g = s.geometry->Surfaces().at(size_t(surface - 1));
		for (size_t i = 0; i + 2 < g.indices.size(); i += 3)
		{
			std::vector<std::array<float, 3>> p;
			for (unsigned c = 0; c < 3; ++c)
			{
				auto v = g.vertices.at(g.indices[i + c]).position;
				p.push_back({{v.x, v.y, v.z}});
			}
			std::sort(p.begin(), p.end());
			std::vector<float> key;
			for (auto v : p)
				key.insert(key.end(), v.begin(), v.end());
			keys.insert(key);
		}
		return keys;
	};
	for (size_t i = 0; i < observations.size(); ++i)
		for (size_t j = 0; j < i; ++j)
		{
			const auto &a = *observations[j], &b = *observations[i];
			if (Length(V(a.receiver.world) - V(b.receiver.world)) > 256)
				continue;
			bool coincident = false;
			for (auto x : sampleNodes[j])
				for (auto y : sampleNodes[i])
					coincident |= Length(x - y) < 1e-8;
			nodePairs += coincident;
			if (coincident && failures.size() < 16)
				failures.emplace_back(&a, &b);
			bool has = false, opposite = false, crossing = false, rawSame = false;
			auto ak = rawTriangleKeys(a.receiver.surface), bk = rawTriangleKeys(b.receiver.surface);
			for (auto k : ak)
				rawSame |= bk.count(k) != 0;
			for (size_t x = 0; x < a.receiver.footprint.count; ++x)
				for (size_t y = 0; y < b.receiver.footprint.count; ++y)
				{
					auto contact =
						T::Inspect(a.receiver.footprint.data[x], a.receiver.geometricNormal,
								   b.receiver.footprint.data[y], b.receiver.geometricNormal);
					if (contact.relation != T::Relation::SameSideOverlap &&
						contact.relation != T::Relation::OppositeSideOverlap &&
						contact.relation != T::Relation::Crossing)
						continue;
					has |= contact.relation == T::Relation::SameSideOverlap;
					opposite |= contact.relation == T::Relation::OppositeSideOverlap;
					crossing |= contact.relation == T::Relation::Crossing;
					sharedArea += contact.sharedArea;
					witness << a.id << '\t' << b.id << '\t' << a.role << '\t' << b.role << '\t'
							<< a.receiver.surface - 1 << '\t' << b.receiver.surface - 1 << '\t'
							<< a.luxel.surface.logicalLightmap << '\t' << a.luxel.surface.x << '\t'
							<< a.luxel.surface.y << '\t' << b.luxel.surface.logicalLightmap << '\t'
							<< b.luxel.surface.x << '\t' << b.luxel.surface.y << '\t' << x << '\t'
							<< y << '\t' << unsigned(contact.relation) << '\t' << contact.sharedArea
							<< '\t' << (a.receiver.material == b.receiver.material) << '\t'
							<< rawSame;
					for (const auto *t :
						 {&a.receiver.footprint.data[x], &b.receiver.footprint.data[y]})
					{
						witness << '\t';
						for (auto v : t->world)
							witness << v.x << ',' << v.y << ',' << v.z << ';';
					}
					witness << '\n';
				}
			overlapPairs += has;
			oppositePairs += opposite;
			crossingPairs += crossing;
			if (has)
			{
				crossFold += a.role != b.role;
				sameSurface += a.receiver.surface == b.receiver.surface;
				sameMaterial += a.receiver.material == b.receiver.material;
				duplicateOriginal += rawSame;
			}
		}
	TransportPreparation out;
	T::Options options;
	options.bounds = scope;
	options.maximumEdgeWorld = 128;
	options.maxPieces = 8192;
	auto meshStart = std::chrono::steady_clock::now();
	Check(T::Mesh::FromSurfaces(*geometry, rays->CertificationKey(), options, read, out.mesh));
	double meshSeconds =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - meshStart).count();
	// Test the actual old singular contacts, even when they lie outside the
	// independently selected normal-diverse GI ROI. No favourable-ROI substitution.
	std::ofstream contactGeometry(folder + "/transport-singular-geometry.tsv");
	contactGeometry << std::setprecision(17)
					<< "pair\tkind\tsample\tsurface\ttriangle\tmaterial\tnx\tny\tnz\tvertices\n";
	for (size_t i = 0; i < failures.size(); ++i)
		for (const auto *v : {failures[i].first, failures[i].second})
		{
			for (size_t t = 0; t < v->receiver.footprint.count; ++t)
			{
				contactGeometry << i << "\tmapped\t" << v->id << '\t' << v->receiver.surface - 1
								<< '\t' << t << '\t' << v->receiver.material << '\t'
								<< v->receiver.geometricNormal.x << '\t'
								<< v->receiver.geometricNormal.y << '\t'
								<< v->receiver.geometricNormal.z << '\t';
				for (auto p : v->receiver.footprint.data[t].world)
					contactGeometry << p.x << ',' << p.y << ',' << p.z << ';';
				contactGeometry << '\n';
			}
			const auto &surface = s.geometry->Surfaces().at(size_t(v->receiver.surface - 1));
			for (size_t t = 0; t + 2 < surface.indices.size(); t += 3)
			{
				contactGeometry << i << "\tsource\t" << v->id << '\t' << v->receiver.surface - 1
								<< '\t' << t / 3 << '\t' << v->receiver.material << '\t'
								<< v->receiver.geometricNormal.x << '\t'
								<< v->receiver.geometricNormal.y << '\t'
								<< v->receiver.geometricNormal.z << '\t';
				for (unsigned c = 0; c < 3; ++c)
				{
					auto p = surface.vertices.at(surface.indices[t + c]).position;
					contactGeometry << p.x << ',' << p.y << ',' << p.z << ';';
				}
				contactGeometry << '\n';
			}
		}
	contactGeometry.close();
	std::ofstream cases(folder + "/transport-contact-regressions.json");
	cases << std::setprecision(17) << "[";
	for (size_t i = 0; i < failures.size(); ++i)
	{
		const auto &a = *failures[i].first, &b = *failures[i].second;
		auto ak = rawTriangleKeys(a.receiver.surface), bk = rawTriangleKeys(b.receiver.surface);
		bool rawSame = false;
		for (const auto &key : ak)
			rawSame |= bk.count(key) != 0;
		T::Options local;
		V centre = (V(a.receiver.world) + V(b.receiver.world)) * .5;
		local.bounds.known = true;
		for (unsigned c = 0; c < 3; ++c)
		{
			V lo(local.bounds.minWorld), hi(local.bounds.maxWorld);
			lo[c] = std::floor(centre[c] / 128) * 128 - 128;
			hi[c] = lo[c] + 256;
			local.bounds.minWorld = lo.Float();
			local.bounds.maxWorld = hi.Float();
		}
		std::shared_ptr<const T::Mesh> m;
		auto status = T::Mesh::FromSurfaces(*geometry, rays->CertificationKey(), local, read, m);
		std::shared_ptr<const Indirect::Transfer> t;
		std::shared_ptr<const T::Mapping> ma, mb;
		if (status)
		{
			Indirect::BuildInput input;
			input.mapKey = s.metadata.map.sha256;
			input.geometry = rays;
			input.moving = s.dynamic;
			input.patchInterpretationKey = m->Key();
			input.allowEmptySupport = true;
			for (auto &e : m->Elements())
				input.patches.push_back(e.patch);
			Indirect::BuildOptions o;
			o.subdivisions = 0;
			o.maxRays = 250000;
			o.maxWork = 16000000;
			o.allowPartialWork = true;
			status = Indirect::Transfer::Build(input, o, read, t);
			Check(m->Map(a.receiver, read, ma));
			Check(m->Map(b.receiver, read, mb));
		}
		cases << (i ? "," : "") << "{\"a\":" << a.id << ",\"b\":" << b.id
			  << ",\"surface_a\":" << a.receiver.surface - 1
			  << ",\"surface_b\":" << b.receiver.surface - 1
			  << ",\"page_a\":" << a.luxel.surface.logicalLightmap
			  << ",\"x_a\":" << a.luxel.surface.x << ",\"y_a\":" << a.luxel.surface.y
			  << ",\"page_b\":" << b.luxel.surface.logicalLightmap
			  << ",\"x_b\":" << b.luxel.surface.x << ",\"y_b\":" << b.luxel.surface.y
			  << ",\"same_fold\":" << (a.role == b.role ? "true" : "false") << ",\"normal_dot\":"
			  << Dot(V(a.receiver.geometricNormal), V(b.receiver.geometricNormal))
			  << ",\"shared_original_triangle\":" << (rawSame ? "true" : "false")
			  << ",\"code\":" << unsigned(status.code) << ",\"reason\":" << Q(status.message);
		if (m)
			cases << ",\"patches\":" << m->Elements().size()
				  << ",\"unsupported_area\":" << m->Diagnostics().unsupportedArea
				  << ",\"precision_excluded_pieces\":" << m->Diagnostics().precisionExcludedPieces
				  << ",\"crossing_pairs\":" << m->Diagnostics().crossingPairs
				  << ",\"a_mapping\":" << unsigned(ma->State())
				  << ",\"b_mapping\":" << unsigned(mb->State());
		if (t)
			cases << ",\"coplanar_pairs\":" << t->Report().coplanarPairs
				  << ",\"unresolved_pairs\":" << t->Report().unresolvedPairs
				  << ",\"maximum_outgoing_fraction\":" << t->Report().maximumOutgoingFraction
				  << ",\"local_budget_exceeded\":"
				  << (t->Report().localBudgetExceeded ? "true" : "false");
		cases << '}';
	}
	cases << ']';
	cases.close();
	Indirect::BuildInput input;
	input.mapKey = s.metadata.map.sha256;
	input.geometry = rays;
	input.moving = s.dynamic;
	input.patchInterpretationKey = out.mesh->Key();
	input.completePatchSet = out.mesh->Diagnostics().complete;
	input.allowEmptySupport = true;
	for (const auto &e : out.mesh->Elements())
		input.patches.push_back(e.patch);
	Indirect::BuildOptions build;
	build.maximumLinkDistanceWorld = 256;
	build.subdivisions = 0;
	build.maxBytes = 128ull << 20;
	build.maxRays = 4000000;
	build.allowPartialWork = true;
	auto transferStart = std::chrono::steady_clock::now();
	auto error = Indirect::Transfer::Build(std::move(input), build, read, out.transfer);
	double transferSeconds =
		std::chrono::duration<double>(std::chrono::steady_clock::now() - transferStart).count();
	uint64_t mapped = 0, partial = 0, unavailable = 0;
	double observationArea = 0, coveredArea = 0;
	std::ofstream mapping(folder + "/observation-transport.tsv");
	mapping << "observation\trole\tstate\tarea\tcovered_area\tpatch\tintersection_area\n"
			<< std::setprecision(17);
	for (auto observation : observations)
	{
		std::shared_ptr<const T::Mapping> m;
		Check(out.mesh->Map(observation->receiver, read, m));
		mapped += m->State() == T::Availability::Available;
		partial += m->State() == T::Availability::Partial;
		unavailable += m->State() == T::Availability::Unavailable;
		observationArea += m->Area();
		coveredArea += m->CoveredArea();
		if (m->Weights().empty())
			mapping << observation->id << '\t' << observation->role << '\t' << unsigned(m->State())
					<< '\t' << m->Area() << '\t' << m->CoveredArea() << "\t0\t0\n";
		for (auto w : m->Weights())
			mapping << observation->id << '\t' << observation->role << '\t' << unsigned(m->State())
					<< '\t' << m->Area() << '\t' << m->CoveredArea() << '\t' << w.patch << '\t'
					<< w.areaWorldSquared << '\n';
	}
	std::ofstream obj(folder + "/transport.obj"), patches(folder + "/transport-patches.tsv");
	obj << std::setprecision(17);
	patches << std::setprecision(17) << "id\tmaterial\tarea\tprovenance_count\tnx\tny\tnz\n";
	size_t vertex = 1;
	for (const auto &e : out.mesh->Elements())
	{
		const auto &p = e.patch;
		patches << p.id << '\t' << p.material << '\t' << p.areaWorldSquared << '\t'
				<< e.sources.size() << '\t' << p.receiver.geometricNormal.x << '\t'
				<< p.receiver.geometricNormal.y << '\t' << p.receiver.geometricNormal.z << '\n';
		obj << "g patch_" << p.id << '\n';
		for (size_t i = 0; i < p.receiver.footprint.count; ++i)
		{
			for (auto v : p.receiver.footprint.data[i].world)
				obj << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
			obj << "f " << vertex << ' ' << vertex + 1 << ' ' << vertex + 2 << '\n';
			vertex += 3;
		}
	}
	const auto &r = out.mesh->Diagnostics();
	std::ofstream json(folder + "/transport.json");
	json << std::setprecision(17)
		 << "{\"schema\":\"org.openjk.transport-topology-x09.v1\",\"map_sha256\":"
		 << Q(Disk::Hex(s.metadata.map.sha256)) << ",\"mesh_key\":" << Q(Disk::Hex(out.mesh->Key()))
		 << ",\"observations\":" << observations.size()
		 << ",\"legacy_coincident_node_pairs\":" << nodePairs
		 << ",\"legacy_same_side_overlap_pairs\":" << overlapPairs
		 << ",\"legacy_opposite_side_pairs\":" << oppositePairs
		 << ",\"legacy_crossing_pairs\":" << crossingPairs
		 << ",\"legacy_cross_fold_overlap_pairs\":" << crossFold
		 << ",\"legacy_same_surface_overlap_pairs\":" << sameSurface
		 << ",\"legacy_same_material_overlap_pairs\":" << sameMaterial
		 << ",\"legacy_duplicate_original_triangle_pairs\":" << duplicateOriginal
		 << ",\"legacy_pairwise_shared_area\":" << sharedArea
		 << ",\"topology_version\":" << T::kVersion
		 << ",\"transfer_version\":" << Indirect::kVersion
		 << ",\"scope\":\"geometry-selected 512-WU ROI, independent of folds and RGB; outside GI "
			"unavailable\",\"input_triangles\":"
		 << r.inputTriangles << ",\"scoped_triangles\":" << r.clippedTriangles
		 << ",\"transport_patches\":" << out.mesh->Elements().size()
		 << ",\"scoped_area\":" << r.scopedArea << ",\"output_area\":" << r.outputArea
		 << ",\"deduplicated_area\":" << r.deduplicatedArea
		 << ",\"unsupported_area\":" << r.unsupportedArea
		 << ",\"unsupported_records\":" << r.unsupportedRecords
		 << ",\"duplicate_overlaps\":" << r.duplicateOverlaps
		 << ",\"conflicting_overlaps\":" << r.conflictingOverlaps
		 << ",\"crossing_pairs\":" << r.crossingPairs << ",\"topology_work\":" << r.work
		 << ",\"topology_accounted_peak_bytes\":" << r.bytes
		 << ",\"mapped_observations\":" << mapped << ",\"partial_observations\":" << partial
		 << ",\"unavailable_observations\":" << unavailable
		 << ",\"observation_area\":" << observationArea
		 << ",\"mapped_observation_area\":" << coveredArea
		 << ",\"transfer_code\":" << unsigned(error.code)
		 << ",\"transfer_reason\":" << Q(error.message);
	if (out.transfer)
	{
		const auto &t = out.transfer->Report();
		uint64_t positiveLinks=0,uncertainLinks=0;
		for(const auto &l:out.transfer->Links()){positiveLinks+=l.conductance>0;uncertainLinks+=l.possibleConductance>l.conductance||(l.issues&Indirect::UnresolvedQuadrature)!=0;}
		json << ",\"links\":" << out.transfer->Links().size() << ",\"rays\":" << t.rays
			 << ",\"known_positive_links\":"<<positiveLinks<<",\"uncertain_links\":"<<uncertainLinks
			 << ",\"unknown_rays\":"<<t.unknownRays<<",\"blocked_rays\":"<<t.blockedRays
			 << ",\"complete_patch_set\":"<<(t.completePatchSet?"true":"false")
			 << ",\"all_pairs_represented\":"<<(t.allPairsRepresented?"true":"false")
			 << ",\"transfer_work\":" << t.work << ",\"transfer_accounted_bytes\":" << t.bytes
			 << ",\"maximum_outgoing_fraction\":" << t.maximumOutgoingFraction
			 << ",\"maximum_rule_difference\":" << t.maximumRuleDifference
			 << ",\"refined_pairs\":" << t.refinedPairs
			 << ",\"unresolved_pairs\":" << t.unresolvedPairs
			 << ",\"energy_limited_pairs\":" << t.energyLimitedPairs;
	}
	json << ",\"precision_excluded_pieces\":" << r.precisionExcludedPieces
		 << ",\"area_accounting_residual\":" << r.areaAccountingResidual;
	if (out.transfer)
		json << ",\"pair_search_complete\":"
			 << (out.transfer->Report().pairSearchComplete ? "true" : "false")
			 << ",\"local_budget_exceeded\":"
			 << (out.transfer->Report().localBudgetExceeded ? "true" : "false");
	json << ",\"mesh_seconds\":" << meshSeconds << ",\"transfer_seconds\":" << transferSeconds;
	json << ",\"build_seconds\":"
		 << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << '}';
	json.close();
	Check(error);
	return out;
}
static void RunProfile(const O::Sources &s, const O::Dataset &data, const O::RegionSnapshot &graph,
					   const std::shared_ptr<const SurfaceGeometry> &geometry,
					   const std::shared_ptr<const StaticVisibility> &rays,
					   const std::array<std::vector<Selected>, 3> &folds, const Census &census,
					   const std::string &output, bool linear, Disk::Digest archiveHash,
					   const TransportPreparation &transport, const Bounds *localScope = nullptr,
					   const SamplingSelection *sampling = nullptr)
{
	ReadContext read;
	read.maxBytes = 512ull << 20;
	J::Request q;
	q.mapKey = s.metadata.map.sha256;
	q.policyKey = Key("All stock baseline 27119e46e: full-page census + global geometry strata "
					  "+ X09 disjoint source geometry support, conditional gamma1/Rec709, v2");
	q.transfer = transport.transfer;
	q.transportMesh = transport.mesh;
	q.directOnlyConditional = true;
	if(sampling){GeometryKey key;key.Digest(q.policyKey);key.Digest(sampling->plan.key);q.policyKey=key.Finish();}
	Bake::Recipe recipe;
	recipe.falloff = linear ? Bake::Falloff::Linear : Bake::Falloff::InverseSquare;
	J::Profile profile;
	profile.model.law = Shape::Law::HistoricalBake;
	profile.model.output = Shape::Output::RelativeIncident;
	profile.model.profile = Bake::NetRadiantProfile(recipe);
	profile.model.units = s.metadata.units;
	profile.model.allowAssumed = true;
	profile.model.provenance = "Explicit conditional NetRadiant hypothesis; original stock Raven "
							   "profile, transfer and primaries UNKNOWN";
	profile.contract.profile = profile.model.profile;
	profile.contract.operation = Indirect::ObservationOperator::NetRadiantEncoded;
	profile.contract.incidentDomain = Indirect::Root::Domain::CompilerRelative255;
	profile.contract.units = s.metadata.units;
	profile.contract.transferAttested = profile.contract.primariesAttested =
		true; // conditional recipe only, manifest explicitly UNKNOWN
	profile.contract.physicalTransportAssumed = false;
	profile.contract.provenance =
		"X09 separately identified conditional direct-only model; indirect energy is unavailable, "
		"not zero; conditional gamma1 stored RGB comparison";
	profile.calibrationHypothesis = "Unknown physical scale; compiler Q gauge=1";
	q.profiles = {profile};
	q.geometry.visibility = rays;
	q.geometry.geometryKey = rays->CertificationKey();
	q.geometry.materialKey = archiveHash;
	q.geometry.observationPolicyKey = q.policyKey;
	q.placement.map = s.metadata.map;
	q.placement.domain = s.domain;
	q.placement.domainKey = archiveHash;
	q.placement.geometry = rays;
	q.placement.dynamic = s.dynamic;
	q.sourceSearchBounds = s.domain->Source().worldBounds;
	if (localScope)
	{
		const auto &world = q.sourceSearchBounds;
		Bounds b = *localScope;
		b.minWorld = Vec3(std::max(world.minWorld.x, b.minWorld.x - 256),
						  std::max(world.minWorld.y, b.minWorld.y - 256),
						  std::max(world.minWorld.z, b.minWorld.z - 256));
		b.maxWorld = Vec3(std::min(world.maxWorld.x, b.maxWorld.x + 256),
						  std::min(world.maxWorld.y, b.maxWorld.y + 256),
						  std::min(world.maxWorld.z, b.maxWorld.z + 256));
		q.sourceSearchBounds = b;
		GeometryKey key;
		key.Digest(q.policyKey);
		key.V(b.minWorld);
		key.V(b.maxWorld);
		key.U(64);
		q.policyKey = key.Finish();
		q.geometry.observationPolicyKey = q.policyKey;
	}
	auto hidden = std::make_shared<Hidden::Request>();
	hidden->map = s.metadata.map;
	hidden->units = s.metadata.units;
	hidden->geometry = q.geometry;
	hidden->domain = s.domain;
	hidden->domainKey = archiveHash;
	hidden->analysisBounds = q.sourceSearchBounds;
	hidden->profiles = {profile.model.profile};
	hidden->options.allowAssumedProfiles = hidden->options.allowAssumedEncoding = true;
	std::set<Id> coarseIds;
	if(sampling){
		hidden->options.maxSamples=4096;hidden->options.localCellWorld=sampling->plan.report.localCellWorld;
		for(const auto &job:sampling->plan.jobs){Hidden::SearchArea a;a.id=job.id;a.receivers=job.receivers;a.positions=job.sourceContext;for(unsigned c=0;c<3;++c){V lo(a.positions.minWorld),hi(a.positions.maxWorld);lo[c]=std::max(lo[c],V(q.sourceSearchBounds.minWorld)[c]);hi[c]=std::min(hi[c],V(q.sourceSearchBounds.maxWorld)[c]);a.positions.minWorld=lo.Float();a.positions.maxWorld=hi.Float();}a.reason="X10 geometry-defined local discovery, independent neighbouring evaluation blocks";hidden->areas.push_back(a);}
		for(auto v:sampling->plan.global[size_t(SP::Purpose::CoarseCoverage)])coarseIds.insert(v.id);
	}
	auto hs = std::make_shared<std::vector<Hidden::Sample>>();
	auto solar = std::make_shared<Solar::Request>();
	solar->mapKey = q.mapKey;
	solar->lineage = "all-stock/" + Disk::Hex(q.mapKey);
	solar->units = s.metadata.units;
	solar->geometry = q.geometry;
	solar->profiles = hidden->profiles;
	solar->options.allowAssumedProfiles = solar->options.allowAssumedObservationEncoding = true;
	solar->options.maxSamples = 512;
	std::array<std::shared_ptr<std::vector<Solar::Sample>>, 3> ss;
	std::array<std::shared_ptr<Rows<J::Observation>>, 3> jr;
	std::array<Disk::Digest, 3> keys;
	std::set<Id> ids, materials;
	for (const auto &element : q.transportMesh->Elements())
		materials.insert(element.patch.material);
	for (unsigned role = 0; role < 3; ++role)
	{
		ss[role] = std::make_shared<std::vector<Solar::Sample>>();
		jr[role] = std::make_shared<Rows<J::Observation>>();
		GeometryKey k;
		k.Digest(q.mapKey);
		k.U(role);
		k.Digest(q.policyKey);
		for (const auto &v : folds[role])
		{
			Need(ids.insert(v.id).second, "Duplicate source/partition identity");
			const auto &g = v.luxel.surface;
			J::Observation o;
			o.id = v.id;
			o.region = v.luxel.positiveRegion;
			o.block = v.block;
			o.receiver = v.receiver;
			Check(q.transportMesh->Map(v.receiver, read, o.transport));
			materials.insert(v.receiver.material);
			o.role = J::Role(role);
			o.measure = v.weight;
			if(sampling)o.correlationGroup=sampling->geometry.at(v.id).correlation;
			o.comparable = true;
			for (unsigned c = 0; c < 3; ++c)
				o.rgb[c] = Amplitude::Rgb8(v.luxel.colour.raw.bytes[c]);
			jr[role]->v->push_back(o);
			Solar::Sample sky;
			sky.receiver = v.receiver;
			sky.areaWorldSquared = v.area;
			sky.footprintRadiusWorld = v.radius;
			sky.eligible = true;
			sky.encodingVerifiedForProfile = false;
			for (unsigned c = 0; c < 3; ++c)
				sky.rgb[c] = v.luxel.colour.raw.bytes[c];
			ss[role]->push_back(sky);
			k.U(v.id);
			k.F(v.weight);
			for (unsigned c = 0; c < 3; ++c)
				k.U(v.luxel.colour.raw.bytes[c]);
			if (role == 0)
			{
				Hidden::Sample h;
				h.receiver = v.receiver;
				h.areaWorldSquared = v.area;
				if(sampling){h.correlationGroup=sampling->geometry.at(v.id).correlation;h.statisticalWeight=v.weight;}
				h.eligible = true;
				h.encodingAttested = false;
				h.materialUncertainty = v.luxel.classification.materialUncertainty;
				h.transportSuspected = true;
				for (unsigned c = 0; c < 3; ++c)
					h.rgb[c] = v.luxel.colour.raw.bytes[c];
				hs->push_back(h);
				hidden->directions.push_back(Hidden::DeluxeConstraint(h, v.luxel.deluxe, v.id, 1));
				if(!sampling||coarseIds.count(v.id))solar->hints.push_back(Solar::DeluxeHint(v.luxel.deluxe, v.id));
			}
		}
		keys[role] = k.Finish();
	}
	if (s.evidence)
	{
		auto authored = Solar::AuthoredHints(*s.evidence);
		solar->hints.insert(solar->hints.end(), authored.begin(), authored.end());
	}
	if (s.grid && s.grid->Report().cells)
	{
		uint64_t step = std::max<uint64_t>(1, s.grid->Report().cells / 128);
		for (uint64_t i = 0; i < s.grid->Report().cells; i += step)
		{
			GridCell cell;
			if (s.grid->Cell(i, cell))
			{
				hidden->directions.push_back(
					Hidden::GridConstraint(cell, 0x7000000000000000ull + i, 1));
				solar->hints.push_back(Solar::GridHint(cell, 0x7000000000000000ull + i));
			}
		}
	}
	// Direction evidence seeds are bounded geometry hints, not observations
	// fitted as irradiance.
	const size_t requestedDirections = hidden->directions.size();
	std::vector<Hidden::DirectionConstraint> usableDirections;
	for (const auto &d : hidden->directions)
		if (d.usable)
			usableDirections.push_back(d);
	hidden->directions.clear();
	for (size_t k = 0; k < std::min(size_t(32), usableDirections.size()); ++k)
		hidden->directions.push_back(
			usableDirections[k * usableDirections.size() /
							 std::min(size_t(32), usableDirections.size())]);
	for (auto id : materials)
	{
		Indirect::Reflectance m;
		m.material = id;
		m.knowledge = Indirect::MaterialKnowledge::Bounded;
		m.lower = Vec3(.05, .05, .05);
		m.upper = Vec3(.8, .8, .8);
		m.initial = Vec3(.3, .3, .3);
		m.priorWeight = .01;
		m.provenance = "Explicit broad diffuse reflectance hypothesis, unknown "
					   "original bake material; not current PBR albedo";
		q.materials.push_back(m);
	}
	q.training = jr[0];
	q.count = jr[0]->v->size();
	q.trainingKey = keys[0];
	q.coarseSampling.compareRefined = false;
	q.strictSampling.compareRefined = true;
	q.options.maxSources = 32;
	q.options.maxCandidates = 128;
	q.options.timeLimitMilliseconds = 180000;
	q.options.coarseRows = 128;
	q.options.maximumRequiredBlockRmse = 0;
	hidden->training = Lease(hs);
	solar->training = Lease(ss[0]);
	// Unknown transfer is explicit: these are observed gradients, not certified
	// cast shadows. The existing detector preserves alternative explanations.
	auto shadowSamples = std::make_shared<std::vector<Shadow::Sample>>();
	for (const auto &v : folds[0])
	{
		Shadow::Sample a;
		a.id = v.id;
		a.chart = Id(v.luxel.surface.surface) + 1;
		a.material = v.receiver.material;
		a.page = v.luxel.surface.logicalLightmap;
		a.x = v.luxel.surface.x;
		a.y = v.luxel.surface.y;
		a.receiver = v.receiver;
		a.rgb = Vec3(v.luxel.colour.raw.bytes[0], v.luxel.colour.raw.bytes[1],
					 v.luxel.colour.raw.bytes[2]);
		a.sigmaRgb = 1;
		a.sourceLinear = false;
		a.primariesComparable = false;
		a.issues = Shadow::UnknownTransfer | Shadow::BakeFiltering;
		shadowSamples->push_back(a);
	}
	std::shared_ptr<const Shadow::Evidence> edges;
	Check(Shadow::Detect(q.mapKey, profile.model.profile.contentKey, Lease(shadowSamples), {}, read,
						 edges));
	Shadow::Context sc;
	sc.observations = edges;
	sc.visibility = rays;
	sc.moving = s.dynamic;
	sc.policy.profile = profile.model.profile;
	sc.policy.units = s.metadata.units;
	sc.policy.provenance = "Automatic stock gradients, unknown transfer/optics "
						   "preserved; no oracle correspondences";
	std::shared_ptr<const Shadow::Objective> objective;
	Check(Shadow::Objective::Build(sc, read, objective));
	q.shadow = objective;
	q.shadowWeight = .1;
	Validator validation(q, jr[1], keys[1]);
	Final final(q, jr[2], keys[2]);
	SolarValidator solarValidation;
	solarValidation.rows = Lease(ss[1]);
	solarValidation.map = q.mapKey;
	solarValidation.geometry = q.geometry;
	P::Request request;
	request.map = s.metadata.map;
	request.fitting = q;
	request.hidden = hidden;
	request.solar = solar;
	request.solarValidation = &solarValidation;
	request.validationKey = keys[1];
	request.heldoutKey = keys[2];
	request.settingsIdentity =
		"All-stock full production stages + bounded global sampling policy v1";
	request.decoder = "Raw RGB8 under explicit gamma1/profile hypothesis; stock "
					  "transfer/primaries UNKNOWN";
	request.metadata.units = s.metadata.units;
	request.metadata.lineage = solar->lineage;
	request.metadata.revision = Disk::Hex(archiveHash);
	request.metadata.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::External;
	p.producer = "Original renderer BSP source snapshots, whole-map controller v1";
	p.contentHash = Disk::Hex(q.mapKey);
	request.metadata.provenanceRecords = {p};
	request.coverage.rawComplete = true;
	request.coverage.derivedComplete = data.Counts().derivedComplete;
	request.coverage.rawPixels = census.texels;
	request.coverage.rawPages = census.pages.size();
	request.coverage.derivedPixels = census.texels;
	request.coverage.mappedSamples = census.coverage[2];
	request.coverage.gridIndices = s.grid ? s.grid->Report().cells : 0;
	request.coverage.geometryComplete = rays->Report().complete;
	request.coverage.sourceTransferKnown = request.coverage.primariesKnown =
		request.coverage.historicalProfileKnown = false;
	request.coverage.mappedAreaWorldSquared.known = true;
	request.coverage.mappedAreaWorldSquared.value = float(census.mappedArea);
	request.coverage.limitations = {
		"X09 geometry-only local transport mesh; this whole fitting hypothesis is explicitly "
		"direct-only conditional; missing GI is not zero",
		"Whole-map scan; bounded global fitting subset, not exhaustive full-map "
		"reconstruction",
		"UnknownProfile, material and colour-domain hypotheses; no stock XYZ "
		"truth",
		"Grid/deluxe are proposal hints only: unknown historical aggregation and "
		"mixtures"};
	Disk::InputContent bsp;
	bsp.kind = Disk::InputKind::Bsp;
	bsp.name = s.metadata.map.path;
	bsp.bytes = s.metadata.map.bytes;
	bsp.sha256 = q.mapKey;
	request.fingerprint.inputs.push_back(bsp);
	Disk::InputContent archive;
	archive.kind = Disk::InputKind::Materials;
	archive.name = "original-captured-dataset";
	archive.bytes = data.Counts().fileBytes;
	archive.sha256 = archiveHash;
	request.fingerprint.inputs.push_back(
		archive); // optional provider policy is deliberately NotCacheable
	if (s.evidence)
	{
		Candidates::Provider provider;
		Check(Candidates::FromEvidence("original-emissive/authored", q.geometry.geometryKey, {},
									   s.evidence, provider));
		if (localScope)
		{
			auto candidates = std::make_shared<std::vector<EmitterCandidate>>();
			auto annotations = std::make_shared<std::vector<Candidates::Annotation>>();
			for (size_t i = 0; i < provider.candidates.count; ++i)
			{
				const auto &c = provider.candidates.data[i];
				const auto &e = provider.evidence.data[c.primary];
				if (!e.positionKnown || InSelection(q.sourceSearchBounds, e.position))
				{
					candidates->push_back(c);
					annotations->push_back(provider.annotations.data[i]);
				}
			}
			provider.candidates = Lease(candidates);
			provider.annotations = Lease(annotations);
		}
		request.providers = {provider};
	}
	std::cerr << "production producer " << (linear ? "linear" : "isq") << " train=" << q.count
			  << " seeds=" << hidden->directions.size() << "/" << requestedDirections << "\n";
	P::Result result;
	auto e = P::Run(request, read, result, &validation, &final);
	std::ofstream areas(output + "/" + (linear ? "linear" : "isq") + "-hidden-areas.tsv");
	areas << "area\trequested\tselected\tsaturated\tunsupported\tnodes\tretained\tunknown_"
			 "visibility\n";
	for (const auto &a : result.work.hidden.areas)
		areas << a.id << '\t' << a.requested << '\t' << a.selected << '\t' << a.saturated << '\t'
			  << a.unsupportedReceivers << '\t' << a.nodes << '\t' << a.retained << '\t'
			  << a.unknownVisibility << '\n';
	const std::string name = linear ? "linear" : "isq";
	std::ofstream report(output + "/" + name + ".json");
	report << std::setprecision(12) << "{\"schema\":\"org.openjk.all-stock-fit.v1\",\"profile\":"
		   << Q(profile.model.profile.id) << ",\"code\":" << unsigned(e.code)
		   << ",\"producer\":" << P::ReportJson(result)
		   << ",\"joint_state\":" << Q(J::StateName(result.fitted.state))
		   << ",\"joint_reason\":" << Q(result.fitted.reason) << ",\"hidden_search_complete\":"
		   << (std::any_of(result.work.providers.begin(), result.work.providers.end(),
						   [](const ProviderAssessment &p) { return p.name == "hidden" && p.ran; })
				   ? (result.work.hidden.complete ? "true" : "false")
				   : "null")
		   << ",\"hidden_areas\":" << result.work.hidden.areas.size()
		   << ",\"hidden_forward_calls\":" << result.work.hidden.forwardCalls
		   << ",\"hidden_rays\":" << result.work.hidden.rays
		   << ",\"solar_state\":" << Q(Solar::StateName(solarValidation.result.state))
		   << ",\"solar_alternatives\":" << solarValidation.result.hypotheses.size()
		   << ",\"shadow_observed_gradients\":" << edges->gradients.size()
		   << ",\"shadow_candidates\":" << edges->edges.size()
		   << ",\"no_oracle_correspondence\":true,\"initial_catalogue\":"
		   << result.fitted.checkpoint.catalog.size()
		   << ",\"work\":" << WorkJson(result.fitted.work) << ",\"attempts\":[";
	for (size_t i = 0; i < result.fitted.attempts.size(); ++i)
	{
		const auto &a = result.fitted.attempts[i];
		report << (i ? "," : "") << "{\"proposal\":" << Q(a.proposal)
			   << ",\"reason\":" << Q(a.reason) << ",\"sources\":" << a.sources
			   << ",\"state\":" << Q(J::StateName(a.state)) << '}';
	}
	report << "],\"sources\":[";
	const J::Model *model = result.fitted.preferred != SIZE_MAX
								? &result.fitted.alternatives[result.fitted.preferred]
								: nullptr;
	if (model)
		for (size_t i = 0; i < model->sources.size(); ++i)
		{
			const auto &a = model->sources[i];
			report << (i ? "," : "") << "{\"id\":" << a.candidate.id
				   << ",\"kind\":" << unsigned(a.value.geometry.kind)
				   << ",\"root\":" << unsigned(a.candidate.root) << ",\"position\":";
			Vec(report, a.value.geometry.position.world);
			report << ",\"compiler_relative_rgb\":";
			Vec(report, a.value.rgbAmplitude);
			report << '}';
		}
	report << "],\"final_evaluation_performed\":"
		   << (result.final.score.available ? "true" : "false")
		   << ",\"source_xyz_error\":null,\"photometric_si_error\":null}";
	std::ofstream candidates(output + "/" + name + "-candidate-placement.tsv");
	candidates << "id\tprovider\tkind\tx\ty\tz\tplacement_state\trays\twork\n";
	std::shared_ptr<const PointRefine::PlacementProvider> place;
	auto placed = PointRefine::MakePlacement(q.placement, read, place);
	if (placed)
		for (const auto &c : result.fitted.checkpoint.catalog)
			if (c.root == TransportRootKind::LocalEmitter)
			{
				PointRefine::PlacementResult v;
				auto error = place->Check(c.shape.initial.geometry.position.world,
										  c.shape.initial.geometry.position.world, c.clearanceWorld,
										  read, v);
				const auto &p = c.shape.initial.geometry.position.world;
				candidates << c.id << '\t' << c.provider << '\t'
						   << unsigned(c.shape.initial.geometry.kind) << '\t' << p.x << '\t' << p.y
						   << '\t' << p.z << '\t' << (error ? int(v.state) : -1) << '\t' << v.rays
						   << '\t' << v.work << '\n';
			}
	if (result.artifact.asset)
	{
		// Producer intentionally marks unversioned provider policies NotCacheable.
		// Do not forge completeness to obtain a disk/cache success. Preserve
		// validated data-only payloads under a diagnostic manifest instead.
		std::ofstream manifest(output + "/" + name + "-payloads.json");
		manifest << "{\"cacheable\":false,\"structural_and_payload_validation\":"
					"true,\"chunks\":[";
		for (size_t i = 0; i < result.artifact.chunks.size(); ++i)
		{
			const auto &c = result.artifact.chunks[i];
			std::string file = name + "-chunk-" + std::to_string(i) + ".bin";
			std::ofstream out(output + "/" + file, std::ios::binary);
			out.write(reinterpret_cast<const char *>(c.bytes.data()), c.bytes.size());
			manifest << (i ? "," : "") << "{\"dataset\":" << c.dataset
					 << ",\"first\":" << c.firstElement << ",\"count\":" << c.count
					 << ",\"file\":" << Q(file)
					 << ",\"sha256\":" << Q(Disk::Hex(Disk::Hash(c.bytes.data(), c.bytes.size())))
					 << '}';
		}
		manifest << "]}";
		std::string obj;
		if (P::DebugObj(result.artifact, 128 * 1024, obj))
		{
			std::ofstream debug(output + "/" + name + ".obj");
			debug << obj;
		}
	}

	std::cerr << "profile " << name << " " << P::StateName(result.state) << " " << result.reason
			  << '\n';
}
int main(int argc, char **argv)
{
	try
	{
		Need(argc == 4 || (argc == 5 && (std::string(argv[4]) == "local-density" ||
									   std::string(argv[4]) == "transport-only" ||
									   std::string(argv[4]) == "sampling-only" ||
									   std::string(argv[4]) == "hierarchical")),
			 "usage: lightrecon_map_solver dataset.jkobs output-folder "
			 "expected-bsp-sha256 [local-density|transport-only|sampling-only|hierarchical]");
		bool local = argc == 5 && std::string(argv[4]) == "local-density";
		bool transportOnly = argc == 5 && std::string(argv[4]) == "transport-only";
		bool samplingOnly = argc == 5 && std::string(argv[4]) == "sampling-only";
		bool hierarchical = argc == 5 && std::string(argv[4]) == "hierarchical";
		ReadContext read;
		read.maxBytes = 512ull << 20;
		O::Options limits;
		limits.maxResidentBytes = read.maxBytes;
		limits.maxChunkBytes = 32ull << 20;
		limits.maxMappedTexels = 8ull << 20;
		std::shared_ptr<const O::Dataset> data;
		Check(O::OpenFile(argv[1], limits, data));
		Need(Disk::Hex(data->Info().map.sha256) == argv[3], "Original BSP identity mismatch");
		Need(data->Counts().derivedComplete,
			 "All lightmap pages must be derived before map solver invocation");
		O::Sources source;
		O::RegionSnapshot graph;
		Check(data->Load(source, graph));
		Need(source.geometry && source.images && source.domain,
			 "Original geometry/images/BSP domain required");
		std::unique_ptr<Disk::FileReader> reader;
		auto opened = Disk::FileReader::OpenFile(argv[1], reader);
		Need(bool(opened), "Dataset hash read");
		Disk::Digest archiveHash;
		Need(bool(Disk::HashContent(*reader, archiveHash, 1024ull << 20)), "Dataset hash budget");
		Census census;
		std::cerr << "scan all original pages\n";
		Bounds focus;
		auto folds = Scan(*data, source, census, argv[2], &focus);
		if (local)
		{
			Need(focus.known, "No eligible geometry for dense local follow-up");
			Census ignored;
			folds = Scan(*data, source, ignored, argv[2], nullptr, &focus, 64);
		}
		std::ofstream scope(std::string(argv[2]) + "/selection-scope.json");
		scope << "{\"kind\":"
			  << Q(local ? "geometry-selected dense local follow-up" : "whole-map sparse audit")
			  << ",\"local_min\":";
		Vec(scope, focus.minWorld);
		scope << ",\"local_max\":";
		Vec(scope, focus.maxWorld);
		scope << ",\"selection_uses_observed_rgb\":false}";
		std::ofstream facts(std::string(argv[2]) + "/census.json");
		facts << std::setprecision(12) << "{\"schema\":\"org.openjk.all-stock-census.v1\",\"map\":"
			  << Q(source.metadata.map.path) << ",\"bsp_sha256\":" << Q(argv[3])
			  << ",\"archive_sha256\":" << Q(Disk::Hex(archiveHash))
			  << ",\"source_surfaces\":" << source.geometry->Surfaces().size()
			  << ",\"source_candidates\":"
			  << (source.evidence ? source.evidence->Candidates().size() : 0)
			  << ",\"raw_colour_pages\":" << census.pages.size()
			  << ",\"derived_texels\":" << census.texels << ",\"coverage\":[";
		for (unsigned i = 0; i < 5; ++i)
			facts << (i ? "," : "") << census.coverage[i];
		facts << "],\"eligible_planar\":" << census.eligible
			  << ",\"unsupported_receiver\":" << census.unsupported
			  << ",\"transport_normal_unsupported\":" << census.transportNormalsUnsupported
			  << ",\"partition_gap_rejections\":" << census.gap
			  << ",\"invalid_footprints\":" << census.invalid
			  << ",\"mapped_area_world_squared\":" << census.mappedArea
			  << ",\"eligible_area_world_squared\":" << census.eligibleArea
			  << ",\"world_area_scope\":\"sum of mapped receiver patches, not "
				 "full-world area\",\"geography_strata\":"
			  << census.cells.size() << ",\"selected\":[" << folds[0].size() << ','
			  << folds[1].size() << ',' << folds[2].size()
			  << "],\"censored_channels\":" << census.saturated
			  << ",\"channel_denominator\":" << census.channels
			  << ",\"unknown_transfer\":" << census.unknownTransfer
			  << ",\"unknown_primaries\":" << census.unknownPrimaries
			  << ",\"deluxe_records\":" << census.deluxe << ",\"region_assigned\":" << census.region
			  << ",\"moving_receivers\":" << census.moving
			  << ",\"grid_indices\":" << (source.grid ? source.grid->Report().cells : 0)
			  << ",\"grid_fitting\":\"Unsupported historical "
				 "reduction/calibration; hints only\",\"pages\":[";
		bool first = true;
		for (const auto &p : census.pages)
		{
			facts << (first ? "" : ",") << "{\"page\":" << p.first << ",\"counts\":[";
			first = false;
			for (unsigned i = 0; i < 5; ++i)
				facts << (i ? "," : "") << p.second[i];
			facts << "]}";
		}
		facts << "]}";
		facts.close();
		std::unique_ptr<SamplingSelection> sampled;
		if(samplingOnly||hierarchical){sampled.reset(new SamplingSelection(HierarchicalSelect(*data,source,folds,argv[2],read)));}
		for (const auto &f : folds)
			Need(samplingOnly||hierarchical||f.size() >= 8, "Insufficient independent training/validation/held-out geometry "
								"after footprint gaps");
		auto geometry = Sanitized(source);
		VisibilityBuildOptions b;
		b.maxBytes = 256ull << 20;
		b.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
		std::shared_ptr<const StaticVisibility> rays;
		Check(StaticVisibility::Build(geometry, b, read, rays, source.domain));
		if(samplingOnly){std::ofstream report(std::string(argv[2])+"/sampling-proposals.json");report<<SamplingProposals(*sampled,source,rays,folds,read);return 0;}
		if(hierarchical)folds=sampled->folds;
		Diagnostics(source, graph, rays, folds, read, argv[2]);
		Need(focus.known, "Geometry-only transport scope unavailable");
		auto transport = PrepareTransport(source, geometry, rays, folds, focus, argv[2], read);
		if (transportOnly)
			return 0;
		for (bool linear : {false, true})
			try
			{
				RunProfile(source, *data, graph, geometry, rays, folds, census, argv[2], linear,
						   archiveHash, transport, local ? &focus : nullptr,sampled.get());
			}
			catch (const std::exception &e)
			{
				std::ofstream f(std::string(argv[2]) + "/" + (linear ? "linear" : "isq") +
								"-failure.json");
				f << "{\"state\":\"Failed\",\"reason\":" << Q(e.what()) << '}';
				std::cerr << e.what() << '\n';
			}
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
