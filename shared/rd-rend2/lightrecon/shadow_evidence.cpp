// Observation facts, competing interpretations, BVH-local caster discovery and bounded losses.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shadow_evidence.h"
#include "bake_math.h"
#include "bake_trace.h"
#include "geometry_key.h"
#include <algorithm>
#include <iomanip>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
namespace Rend2LightRecon
{
namespace Core
{
namespace Shadow
{
using namespace Bake::Detail;
namespace
{
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
void Cancel(const ReadContext &r) { Need(!Cancelled(r), Code::Cancelled, "Shadow evidence cancelled"); }
bool Zero(const Disk::Digest &d) { return d == Disk::Digest{}; }
Id IdOf(const Disk::Digest &d)
{
	Id v = 0;
	for (unsigned k = 0; k < 8; ++k)
		v |= Id(d[k]) << (k * 8);
	return v ? v : 1;
}
void Text(GeometryKey &k, const std::string &s)
{
	k.U(s.size());
	for (unsigned char c : s)
		k.U(c);
}
bool Less(V a, V b) { return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z; }
double Y(Vec3 v) { return Dot(V(v), V(.2126, .7152, .0722)); }
double Huber(double e, double d)
{
	e = std::abs(e);
	return e <= d ? .5 * e * e : d * (e - .5 * d);
}
double Band(double x, double radius) { return std::max(0., std::abs(x) - radius); }
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> v)
{
	return VectorChunk<T>(v, 0, v->size());
}
typedef std::tuple<uint32_t, uint32_t, uint32_t, uint8_t> Pixel;
struct Link
{
	size_t a = 0, b = 0;
	unsigned axis = 0;
};
Bounds Box(V a, V b, double margin = 0)
{
	Bounds out;
	out.known = true;
	V lo, hi;
	for (int k = 0; k < 3; ++k)
	{
		lo[k] = std::min(a[k], b[k]) - margin;
		hi[k] = std::max(a[k], b[k]) + margin;
	}
	out.minWorld = lo.Float();
	out.maxWorld = hi.Float();
	return out;
}
void Include(Bounds &box, V p)
{
	for (int k = 0; k < 3; ++k)
	{
		float *lo = k == 0 ? &box.minWorld.x : k == 1 ? &box.minWorld.y : &box.minWorld.z;
		float *hi = k == 0 ? &box.maxWorld.x : k == 1 ? &box.maxWorld.y : &box.maxWorld.z;
		*lo = std::min(*lo, float(p[k]));
		*hi = std::max(*hi, float(p[k]));
	}
}
bool Intersects(const Bounds &box, V a, V b)
{
	if (!box.known)
		return true;
	V d = b - a;
	double lo = 0, hi = 1;
	for (int k = 0; k < 3; ++k)
	{
		if (std::abs(d[k]) < 1e-15)
		{
			if (a[k] < V(box.minWorld)[k] || a[k] > V(box.maxWorld)[k])
				return false;
		}
		else
		{
			double x = (V(box.minWorld)[k] - a[k]) / d[k], y = (V(box.maxWorld)[k] - a[k]) / d[k];
			if (x > y)
				std::swap(x, y);
			lo = std::max(lo, x);
			hi = std::min(hi, y);
			if (lo > hi)
				return false;
		}
	}
	return true;
}
} // namespace
Id CasterId(Id surface, Vec3 a, Vec3 b)
{
	if (Less(V(b), V(a)))
		std::swap(a, b);
	GeometryKey key;
	key.U(kVersion);
	key.U(surface);
	key.V(a);
	key.V(b);
	return IdOf(key.Finish());
}
Error Detect(Disk::Digest map, Disk::Digest profile, const Chunk<Sample> &input, const DetectionOptions &o,
			 const ReadContext &read, std::shared_ptr<const Evidence> &out)
{
	out.reset();
	try
	{
		Cancel(read);
		Need(!Zero(map) && !Zero(profile) && input.owner && input.data && input.count &&
				 input.count <= o.maxSamples && o.maxSamples <= 1048576,
			 Code::InvalidInput,
			 "Shadow detector requires pinned observations and source-domain assumptions");
		Need(o.maxGradients && o.maxGradients <= 2097152 && o.maxEdges && o.maxEdges <= 4096 && o.maxWork,
			 Code::InvalidInput, "Shadow detector count/work limits");
		for (double x :
			 {o.minimumRelativeContrast, o.minimumSigma, o.maximumNormalAngle, o.smoothSlopeFactor})
			Need(std::isfinite(x) && x > 0, Code::InvalidInput, "Invalid edge detection thresholds");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, o.maxBytes) : o.maxBytes;
		Need(input.count * (sizeof(Sample) + 256) + uint64_t(o.maxGradients) * sizeof(Gradient) * 2 + 65536 <=
				 cap,
			 Code::MemoryLimit, "Shadow detector preflight");
		auto data = std::make_shared<Evidence>();
		data->map = map;
		data->profileAssumption = profile;
		data->samples.assign(input.data, input.data + input.count);
		std::sort(data->samples.begin(), data->samples.end(),
				  [](const Sample &a, const Sample &b) { return a.id < b.id; });
		std::map<Pixel, size_t> pixels;
		GeometryKey key;
		key.U(kVersion);
		key.Digest(map);
		key.Digest(profile);
		key.F(o.minimumRelativeContrast);
		key.F(o.minimumSigma);
		key.F(o.maximumNormalAngle);
		key.F(o.smoothSlopeFactor);
		for (size_t i = 0; i < data->samples.size(); ++i)
		{
			Cancel(read);
			Need(++data->work <= o.maxWork, Code::WorkLimit, "Shadow sample validation budget");
			const auto &s = data->samples[i];
			Need(s.id && (!i || s.id != data->samples[i - 1].id) && s.receiver.sample == s.id &&
					 s.receiver.surface && s.chart && Finite(s.receiver.world) &&
					 Normal(s.receiver.geometricNormal) && Normal(s.receiver.shadingNormal) &&
					 Positive(s.rgb) && std::isfinite(s.sigmaRgb) && s.sigmaRgb >= 0 &&
					 std::isfinite(s.mappingErrorWorld) && s.mappingErrorWorld >= 0 &&
					 std::isfinite(s.knownFilterRadiusWorld) && s.knownFilterRadiusWorld >= 0,
				 Code::InvalidInput, "Invalid shadow source sample/normal/noise metadata");
			Need(std::isfinite(s.receiver.geometryErrorWorld) && s.receiver.geometryErrorWorld >= 0,
				 Code::InvalidInput, "Invalid receiver geometry uncertainty");
			Need(pixels.emplace(Pixel(s.page, s.x, s.y, s.receiver.style), i).second, Code::InvalidInput,
				 "Overlapping/duplicate lightmap coordinates must be classified before edge detection");
			key.U(s.id);
			key.U(unsigned(s.role));
			key.U(s.chart);
			key.U(s.material);
			key.U(s.page);
			key.U(s.x);
			key.U(s.y);
			key.U(s.receiver.surface);
			key.V(s.receiver.world);
			key.V(s.receiver.geometricNormal);
			key.V(s.receiver.shadingNormal);
			key.F(s.receiver.geometryErrorWorld);
			key.U(s.receiver.style);
			key.V(s.rgb);
			key.U(s.sourceLinear);
			key.U(s.primariesComparable);
			key.U(s.filterKnown);
			key.U(s.issues);
			key.F(s.sigmaRgb);
			key.F(s.mappingErrorWorld);
			key.F(s.knownFilterRadiusWorld);
			key.U(s.receiver.footprint.count);
			if (s.receiver.footprint.count)
			{
				Need(s.receiver.footprint.owner && s.receiver.footprint.data &&
						 s.receiver.footprint.count <= 64,
					 Code::InvalidInput, "Shadow footprint lease");
				for (size_t t = 0; t < s.receiver.footprint.count; ++t)
					for (int k = 0; k < 3; ++k)
					{
						Need(++data->work <= o.maxWork, Code::WorkLimit,
							 "Shadow footprint validation budget");
						const auto &p = s.receiver.footprint.data[t];
						Need(Finite(p.world[k]) && Normal(p.normal[k]), Code::InvalidInput,
							 "Shadow footprint geometry");
						key.V(p.world[k]);
						key.V(p.normal[k]);
					}
			}
		}
		std::vector<Link> links;
		for (size_t i = 0; i < data->samples.size(); ++i)
		{
			Cancel(read);
			const auto &a = data->samples[i];
			for (unsigned axis = 0; axis < 2; ++axis)
			{
				if (++data->work > o.maxWork)
					throw Failure(Code::WorkLimit, "Shadow adjacency budget");
				if ((axis == 0 && a.x == UINT32_MAX) || (axis == 1 && a.y == UINT32_MAX))
					continue;
				auto found =
					pixels.find(Pixel(a.page, a.x + (axis == 0), a.y + (axis == 1), a.receiver.style));
				if (found == pixels.end())
					continue;
				++data->adjacencyTests;
				const auto &b = data->samples[found->second];
				V delta = V(b.receiver.world) - V(a.receiver.world);
				double distance = Length(delta);
				Need(distance > 1e-10 && std::isfinite(distance), Code::InvalidInput,
					 "Collapsed source luxel geometry");
				double difference = std::abs(Y(b.rgb) - Y(a.rgb)), noise = std::hypot(a.sigmaRgb, b.sigmaRgb);
				double contrast = difference / std::max(1e-20, Y(a.rgb) + Y(b.rgb) + noise);
				if (difference < o.minimumSigma * std::max(1e-20, noise) ||
					contrast < o.minimumRelativeContrast)
				{
					++data->smoothRejected;
					continue;
				}
				Need(data->gradients.size() < o.maxGradients, Code::MemoryLimit, "Shadow gradient limit");
				Gradient g;
				g.first = a.id;
				g.second = b.id;
				GeometryKey id;
				id.Digest(map);
				id.U(a.id);
				id.U(b.id);
				g.id = IdOf(id.Finish());
				g.world = ((V(a.receiver.world) + V(b.receiver.world)) * .5).Float();
				g.normal = Unit(delta * (Y(b.rgb) >= Y(a.rgb) ? 1 : -1)).Float();
				g.tangent = Unit(Cross(V(a.receiver.geometricNormal), V(g.normal))).Float();
				g.deltaRgb = (V(b.rgb) - V(a.rgb)).Float();
				if (Y(b.rgb) < Y(a.rgb))
					g.deltaRgb = (V(g.deltaRgb) * -1).Float();
				g.separationWorld = distance;
				g.contrast = contrast;
				g.noise = noise;
				g.alternatives =
					CastShadow | MaterialEdge | SpotCone | IndirectTransition | OverlappingSources;
				g.alternatives |= a.issues | b.issues;
				uint32_t exclusions = a.issues | b.issues;
				if (std::abs(Dot(Unit(delta), V(a.receiver.geometricNormal))) > .05)
				{
					exclusions |= MissingGeometry;
					g.alternatives |= MissingGeometry;
				}
				if (a.material != b.material)
				{
					g.alternatives |= MaterialEdge;
					exclusions |= MaterialEdge;
				}
				if (a.chart != b.chart || a.receiver.surface != b.receiver.surface)
				{
					g.alternatives |= ChartSeam;
					exclusions |= ChartSeam;
				}
				if (Dot(V(a.receiver.geometricNormal), V(b.receiver.geometricNormal)) <
						std::cos(o.maximumNormalAngle) ||
					Dot(V(a.receiver.shadingNormal), V(b.receiver.shadingNormal)) <
						std::cos(o.maximumNormalAngle))
				{
					g.alternatives |= NormalEdge;
					exclusions |= NormalEdge;
				}
				if (!a.sourceLinear || !b.sourceLinear || !a.primariesComparable || !b.primariesComparable)
				{
					g.alternatives |= UnknownTransfer;
					exclusions |= UnknownTransfer;
				}
				if (!a.filterKnown || !b.filterKnown)
					g.alternatives |= BakeFiltering;
				if (noise > 0)
					g.alternatives |= Quantization;
				// Compare local shoulders with the selected jump; smooth falloff
				// remains an alternative, never an automatic hard shadow label.
				auto shoulder = [&](const Sample &s, bool positive) {
					uint32_t x = s.x, y = s.y;
					if (!positive && ((axis == 0 && !x) || (axis == 1 && !y)))
						return 0.;
					if (positive && ((axis == 0 && x == UINT32_MAX) || (axis == 1 && y == UINT32_MAX)))
						return 0.;
					if (axis == 0)
						x = positive ? x + 1 : x - 1;
					else
						y = positive ? y + 1 : y - 1;
					auto n = pixels.find(Pixel(s.page, x, y, s.receiver.style));
					return n == pixels.end() ? 0. : std::abs(Y(data->samples[n->second].rgb) - Y(s.rgb));
				};
				if (difference <=
					o.smoothSlopeFactor * std::min(shoulder(a, false), shoulder(b, true)) + noise)
					g.alternatives |= SmoothFalloff;
				g.geometricallyComparable =
					!(exclusions & (MaterialEdge | NormalEdge | ChartSeam | SaturationEdge | UnknownTransfer |
									MissingGeometry | MovingCaster | OpticalAmbiguity));
				if (!g.geometricallyComparable)
					++data->invalidPairs;
				data->gradients.push_back(g);
				Link link;
				link.a = i;
				link.b = found->second;
				link.axis = axis;
				links.push_back(link);
			}
		}
		// Grid-local connected components only; no O(edges^2) global clustering.
		std::vector<size_t> parent(links.size());
		std::iota(parent.begin(), parent.end(), 0);
		auto root = [&](size_t i) {
			while (parent[i] != i)
			{
				parent[i] = parent[parent[i]];
				i = parent[i];
			}
			return i;
		};
		std::map<std::tuple<uint32_t, uint32_t, uint32_t, unsigned, Id>, size_t> locations;
		for (size_t i = 0; i < links.size(); ++i)
		{
			const auto &l = links[i];
			const auto &a = data->samples[l.a];
			locations[{a.page, a.x, a.y, l.axis, a.chart}] = i;
		}
		for (size_t i = 0; i < links.size(); ++i)
		{
			const auto &l = links[i];
			const auto &a = data->samples[l.a];
			for (unsigned axis = 0; axis < 2; ++axis)
			{
				auto p = locations.find({a.page, a.x + (axis == 0), a.y + (axis == 1), l.axis, a.chart});
				if (p == locations.end())
					continue;
				size_t j = p->second;
				const auto &b = data->samples[links[j].a];
				if (a.receiver.surface == b.receiver.surface && a.material == b.material &&
					data->gradients[i].geometricallyComparable ==
						data->gradients[j].geometricallyComparable &&
					Dot(V(data->gradients[i].normal), V(data->gradients[j].normal)) > .95)
				{
					size_t ra = root(i), rb = root(j);
					parent[std::max(ra, rb)] = std::min(ra, rb);
				}
			}
		}
		std::map<size_t, std::vector<size_t>> groups;
		for (size_t i = 0; i < links.size(); ++i)
			groups[root(i)].push_back(i);
		for (const auto &group : groups)
		{
			Need(data->edges.size() < o.maxEdges, Code::MemoryLimit, "Shadow interpretation limit");
			const auto &first = data->gradients[group.second.front()];
			const auto &sample = data->samples[links[group.second.front()].a];
			Edge e;
			e.surface = sample.receiver.surface;
			e.chart = sample.chart;
			e.material = sample.material;
			e.receiver = sample.receiver;
			e.tangent = first.tangent;
			e.normal = first.normal;
			e.usable = first.geometricallyComparable;
			V centre, delta;
			double sum = 0, minAlong = 1e300, maxAlong = -1e300, minTangent = 1e300, maxTangent = -1e300,
				   spacing = 0;
			std::set<Id> samples;
			bool filterKnown = true;
			double error = 0, filter = 0;
			for (size_t i : group.second)
			{
				const auto &g = data->gradients[i];
				centre = centre + V(g.world) * g.contrast;
				delta = delta + V(g.deltaRgb);
				sum += g.contrast;
				e.alternatives |= g.alternatives;
				e.gradients.push_back(g.id);
				minAlong = std::min(minAlong, Dot(V(g.world), V(e.normal)));
				maxAlong = std::max(maxAlong, Dot(V(g.world), V(e.normal)));
				minTangent = std::min(minTangent, Dot(V(g.world), V(e.tangent)));
				maxTangent = std::max(maxTangent, Dot(V(g.world), V(e.tangent)));
				spacing = std::max(spacing, g.separationWorld);
				for (size_t j : {links[i].a, links[i].b})
				{
					const auto &s = data->samples[j];
					samples.insert(s.id);
					filterKnown &= s.filterKnown;
					error = std::max(error, s.mappingErrorWorld);
					filter = std::max(filter, s.knownFilterRadiusWorld);
				}
			}
			e.world = (centre / sum).Float();
			e.deltaRgb = (delta / double(group.second.size())).Float();
			e.extentWorld = maxTangent - minTangent;
			e.transitionWidthWorld = maxAlong - minAlong + spacing;
			e.locationSigmaWorld = spacing * .5 + error + (filterKnown ? filter : 0);
			e.widthKnown = filterKnown && (maxAlong - minAlong) > spacing;
			e.samples.assign(samples.begin(), samples.end());
			std::sort(e.gradients.begin(), e.gradients.end());
			GeometryKey id;
			id.Digest(map);
			id.U(kVersion);
			for (Id value : e.gradients)
				id.U(value);
			e.id = IdOf(id.Finish());
			e.provenance = "Automatic source-domain gradient component; cast-shadow/material/cone/transport "
						   "alternatives unresolved";
			data->edges.push_back(e);
		}
		std::sort(data->edges.begin(), data->edges.end(),
				  [](const Edge &a, const Edge &b) { return a.id < b.id; });
		data->identity = key.Finish();
		out = data;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow detector allocation");
	}
}

namespace
{
struct SourceNode
{
	V position;
	double weight = 1;
	V emissionNormal;
	double area = 1;
	bool finiteSurface = false;
};
void ValidateSource(const FitEvidence::Source &source)
{
	const auto &e = source.geometry;
	Need(e.source.id && e.position.known && Finite(e.position.world) && Positive(source.rgb),
		 Code::InvalidInput, "Invalid shadow source identity/position/RGB");
	Need(e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot || e.kind == EmitterKind::Rectangle ||
			 e.kind == EmitterKind::Tube,
		 Code::Unavailable, "Finite shadow associations do not implement directional/sky sources");
	if (e.kind == EmitterKind::Point)
		Need(e.angular.kind == AngularKind::Isotropic, Code::Unavailable, "Unknown point angular model");
	if (e.kind == EmitterKind::Spot)
		Need(e.frame.known && Normal(e.frame.forward) && e.angular.kind == AngularKind::SmoothCone &&
				 std::isfinite(e.angular.cosOuter) && std::isfinite(e.angular.cosInner) &&
				 e.angular.cosOuter >= -1 && e.angular.cosInner <= 1 &&
				 e.angular.cosOuter < e.angular.cosInner,
			 Code::Unavailable,
			 "Physical shadow spot requires an explicit supported cone, not an unknown historical law");
	if (e.kind == EmitterKind::Rectangle || e.kind == EmitterKind::Tube)
		Need(e.angular.kind == AngularKind::Lambertian, Code::Unavailable,
			 "Unknown finite-source angular model");
}
std::vector<SourceNode> SourceNodes(const FitEvidence::Source &source, unsigned order)
{
	std::vector<SourceNode> out;
	const auto &e = source.geometry;
	ValidateSource(source);
	if (e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot)
	{
		SourceNode n;
		n.position = V(e.position.world);
		out.push_back(n);
		return out;
	}
	Need(e.geometry.known && e.frame.known, Code::Unavailable, "Extended shadow source geometry unknown");
	Need(Normal(e.frame.right) && Normal(e.frame.up) && Normal(e.frame.forward) &&
			 std::abs(Dot(V(e.frame.right), V(e.frame.up))) < 1e-4 &&
			 std::abs(Dot(V(e.frame.right), V(e.frame.forward))) < 1e-4 &&
			 std::abs(Dot(V(e.frame.up), V(e.frame.forward))) < 1e-4,
		 Code::InvalidInput, "Invalid extended shadow source frame");
	if (e.kind == EmitterKind::Rectangle)
	{
		Need(std::isfinite(e.geometry.halfWidthWorld) && std::isfinite(e.geometry.halfHeightWorld) &&
				 e.geometry.halfWidthWorld > 0 && e.geometry.halfHeightWorld > 0,
			 Code::InvalidInput, "Shadow rectangle extent");
		for (unsigned x = 0; x < order; ++x)
			for (unsigned y = 0; y < order; ++y)
			{
				SourceNode n;
				n.position = V(e.position.world) +
							 V(e.frame.right) * (2 * e.geometry.halfWidthWorld * ((x + .5) / order - .5)) +
							 V(e.frame.up) * (2 * e.geometry.halfHeightWorld * ((y + .5) / order - .5));
				n.weight = 1. / (order * order);
				n.emissionNormal = V(e.frame.forward);
				n.area = 4 * e.geometry.halfWidthWorld * e.geometry.halfHeightWorld;
				n.finiteSurface = true;
				out.push_back(n);
			}
	}
	else if (e.kind == EmitterKind::Tube)
	{
		Need(std::isfinite(e.geometry.halfLengthWorld) && std::isfinite(e.geometry.radiusWorld) &&
				 e.geometry.halfLengthWorld > 0 && e.geometry.radiusWorld > 0,
			 Code::InvalidInput, "Shadow cylinder extent");
		for (unsigned x = 0; x < order; ++x)
			for (unsigned y = 0; y < order * 4; ++y)
			{
				double angle = 2 * Pi * (y + .5) / (order * 4);
				SourceNode n;
				n.position = V(e.position.world) +
							 V(e.frame.right) * (2 * e.geometry.halfLengthWorld * ((x + .5) / order - .5)) +
							 (V(e.frame.up) * std::cos(angle) + V(e.frame.forward) * std::sin(angle)) *
								 e.geometry.radiusWorld;
				n.weight = 1. / (order * order * 4);
				n.emissionNormal = V(e.frame.up) * std::cos(angle) + V(e.frame.forward) * std::sin(angle);
				n.area = 4 * Pi * e.geometry.halfLengthWorld * e.geometry.radiusWorld;
				n.finiteSurface = true;
				out.push_back(n);
			}
	}
	return out;
}
double Extent(const PhysicalEmitter &e)
{
	return e.kind == EmitterKind::Rectangle
			   ? std::hypot(e.geometry.halfWidthWorld, e.geometry.halfHeightWorld)
			   : e.kind == EmitterKind::Tube ? std::hypot(e.geometry.halfLengthWorld, e.geometry.radiusWorld)
											 : 0;
}
std::vector<Caster> Casters(const LocalGeometry &local)
{
	struct Part
	{
		Caster c;
		size_t count = 0;
		V sum;
	};
	std::map<Id, Part> parts;
	for (const auto &t : local.triangles)
	{
		V normal = Unit(Cross(V(t.geometry.verticesWorld[1]) - V(t.geometry.verticesWorld[0]),
							  V(t.geometry.verticesWorld[2]) - V(t.geometry.verticesWorld[0])));
		for (int k = 0; k < 3; ++k)
		{
			Vec3 a = t.geometry.verticesWorld[k], b = t.geometry.verticesWorld[(k + 1) % 3];
			Id id = CasterId(t.surface, a, b);
			auto &p = parts[id];
			if (!p.count)
			{
				p.c.id = id;
				p.c.surface = t.surface;
				p.c.primitive = t.primitive;
				p.c.a = a;
				p.c.b = b;
				p.c.normal = normal.Float();
				p.c.optical = t.optical;
				p.c.geometryErrorWorld = t.errorWorld;
			}
			++p.count;
			p.sum = p.sum + normal;
			p.c.geometryErrorWorld = std::max(p.c.geometryErrorWorld, t.errorWorld);
		}
	}
	std::vector<Caster> result;
	for (auto &pair : parts)
	{
		auto &p = pair.second;
		if (p.count == 2 && Length(p.sum) > 1.999)
			continue;
		result.push_back(p.c);
	}
	return result;
}
} // namespace
struct Objective::Data
{
	Context context;
	Disk::Digest key{};
};
Disk::Digest Objective::Key() const { return data->key; }
Disk::Digest Objective::MapKey() const { return data->context.observations->map; }
Disk::Digest Objective::GeometryKey() const { return data->context.visibility->CertificationKey(); }
Error Objective::Build(Context context, const ReadContext &read, std::shared_ptr<const Objective> &out)
{
	out.reset();
	try
	{
		Cancel(read);
		Need(context.observations && context.visibility && !Zero(context.observations->identity) &&
				 context.observations->map == context.visibility->GeometryIdentity().sha256,
			 Code::MissingGeometry, "Shadow evidence map/visibility generation mismatch");
		const auto &p = context.policy;
		for (const auto &s : context.observations->samples)
			Need(s.role == Sample::Role::Training, Code::InvalidInput,
				 "Shadow optimization cannot read validation/held-out edge colours");
		Need(p.scope <= Scope::OptimizationSurrogate && p.sourceSamples && p.sourceSamples <= 16 &&
				 p.receiverSamples && p.receiverSamples <= 8 && p.maxEdges && p.maxEdges <= 4096 &&
				 context.observations->edges.size() <= p.maxEdges && p.maxSources && p.maxSources <= 16 &&
				 p.maxLocalTriangles && p.maxLocalTriangles <= 65536 && p.maxAssociationsPerEdge &&
				 p.maxAssociationsPerEdge <= 32,
			 Code::InvalidInput, "Shadow association limits");
		Need(p.maxAssociationAttemptsPerEdge && p.maxAssociationAttemptsPerEdge <= 65536, Code::InvalidInput,
			 "Shadow association attempt cap");
		for (double value : {p.searchRadiusWorld, p.nullCost, p.huber, p.minimumPatternChange})
			Need(std::isfinite(value) && value > 0, Code::InvalidInput, "Shadow evidence scale/loss policy");
		for (double value : {p.receiverPushWorld, p.associationSlack, p.edgeWeight, p.tangentWeight,
							 p.patternWeight, p.widthWeight, p.colourWeight})
			Need(std::isfinite(value) && value >= 0, Code::InvalidInput, "Shadow evidence weights/tolerance");
		Need(!p.provenance.empty() && std::isfinite(p.units.worldUnitsPerMeter) &&
				 p.units.worldUnitsPerMeter > 0,
			 Code::InvalidInput, "Shadow scope provenance/units required");
		if (p.scope == Scope::CompilerScoped)
			return Failure(
				Code::Unavailable,
				"Finite-source automatic matching does not implement scoped directional compiler replay; "
				"use the separate S01 source-locked sun operator for compiler values");
		Need(context.moving.size() <= 4096 && context.oracle.size() <= 4096, Code::MemoryLimit,
			 "Shadow dynamic/oracle limits");
		std::sort(context.moving.begin(), context.moving.end(),
				  [](const DynamicBoundary &a, const DynamicBoundary &b) { return a.id < b.id; });
		std::sort(context.oracle.begin(), context.oracle.end(),
				  [](const OracleBinding &a, const OracleBinding &b) {
					  return std::tie(a.edge, a.source, a.caster) < std::tie(b.edge, b.source, b.caster);
				  });
		Core::GeometryKey key;
		key.U(kVersion);
		key.Digest(context.observations->identity);
		key.Digest(context.visibility->CertificationKey());
		key.U(unsigned(p.scope));
		key.Digest(p.profile.contentKey);
		key.F(p.units.worldUnitsPerMeter);
		key.U(p.sourceSamples);
		key.U(p.receiverSamples);
		key.F(p.receiverPushWorld);
		key.F(p.searchRadiusWorld);
		key.F(p.associationSlack);
		key.F(p.edgeWeight);
		key.F(p.tangentWeight);
		key.F(p.patternWeight);
		key.F(p.widthWeight);
		key.F(p.colourWeight);
		key.F(p.nullCost);
		key.F(p.huber);
		key.F(p.minimumPatternChange);
		Text(key, p.provenance);
		for (const auto &b : context.moving)
		{
			Need(b.id && (!b.sweptExtentKnown ||
						  (b.bounds.known && Finite(b.bounds.minWorld) && Finite(b.bounds.maxWorld))),
				 Code::InvalidInput, "Invalid moving geometry bound");
			if (b.sweptExtentKnown)
				for (int k = 0; k < 3; ++k)
					Need(V(b.bounds.minWorld)[k] <= V(b.bounds.maxWorld)[k], Code::InvalidInput,
						 "Inverted moving bound");
			key.U(b.id);
			key.B(b.bounds);
			key.U(b.sweptExtentKnown);
		}
		for (const auto &b : context.oracle)
		{
			Need(b.edge && b.source && b.caster, Code::InvalidInput, "Invalid oracle binding");
			key.U(b.edge);
			key.U(b.source);
			key.U(b.caster);
		}
		auto value = std::make_shared<Data>();
		value->key = key.Finish();
		value->context = std::move(context);
		auto objective = std::make_shared<Objective>();
		objective->data = value;
		out = objective;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow objective allocation");
	}
}

namespace
{
struct Query
{
	const Context &context;
	const ReadContext &read;
	Report &report;
	void Step()
	{
		Cancel(read);
		Need(report.value.rays <= context.policy.maxRays && report.value.work < context.policy.maxWork,
			 Code::WorkLimit, "Shadow matching cumulative budget");
	}
	RayResult Ray(V start, V end, Id receiver, Vec3 normal)
	{
		Step();
		RayQuery q;
		q.startWorld = start.Float();
		q.endWorld = end.Float();
		q.originSurface = receiver;
		q.purpose = RayPurpose::Shadow;
		q.bias.relative = 0;
		q.bias.absoluteWorld = q.bias.maximumWorld = context.policy.receiverPushWorld;
		q.bias.normalKnown = true;
		q.bias.geometricNormal = normal;
		q.maxWork = context.policy.maxWork - report.value.work;
		RayResult result;
		Check(context.visibility->Query(q, read, result));
		++report.value.rays;
		report.value.work += result.nodeTests + result.primitiveTests;
		for (const auto &moving : context.moving)
		{
			if (!moving.sweptExtentKnown || Intersects(moving.bounds, start, end))
			{
				result.state = VisibilityState::Unknown;
				result.issues |= PolicyAssumption;
			}
		}
		Step();
		return result;
	}
	LocalGeometry Local(const Bounds &box)
	{
		Step();
		LocalGeometry local;
		Check(context.visibility->ExtractBox(box, read, context.policy.maxLocalTriangles,
											 context.policy.maxWork - report.value.work, local));
		report.value.work += local.work;
		report.localTriangles += local.triangles.size();
		Step();
		return local;
	}
	struct Vis
	{
		bool known = false;
		double visibility = 0, response = 0, error = 0;
		uint32_t issues = 0;
		std::set<Id> casterSurfaces;
		bool exact = false;
	};
	Vis Visibility(const Edge &edge, const FitEvidence::Source &source, V receiver,
				   const LocalGeometry &local)
	{
		Vis out;
		const auto &p = context.policy;
		auto nodes = SourceNodes(source, p.scope == Scope::CentreVisibility ? 1 : p.sourceSamples);
		if (p.scope == Scope::CentreVisibility)
		{
			nodes.resize(1);
			nodes[0].position = V(source.geometry.position.world);
			nodes[0].weight = 1;
		}
		if (nodes.empty())
			return out;
		if (p.scope == Scope::CompilerScoped)
			return out; // source-specific exact replay remains in S01, never generalized to these point/area
						// hypotheses
		bool uncertain = false;
		if (edge.receiver.geometryErrorWorld > 0)
		{
			out.issues |= PatchApproximation;
			return out; // no exact planar footprint certification through a receiver-error envelope
		}
		(void)local;
		Bounds beam = Box(receiver, nodes[0].position, 1e-4);
		V shift = receiver - V(edge.receiver.world);
		for (const auto &n : nodes)
			Include(beam, n.position);
		if (edge.receiver.footprint.count)
			for (size_t t = 0; t < edge.receiver.footprint.count; ++t)
				for (int k = 0; k < 3; ++k)
					Include(beam, V(edge.receiver.footprint.data[t].world[k]) + shift);
		auto relevant = Local(beam);
		uncertain |= !relevant.coverageComplete;
		for (const auto &d : context.moving)
			if (!d.sweptExtentKnown || !d.bounds.known ||
				(beam.minWorld.x <= d.bounds.maxWorld.x && beam.maxWorld.x >= d.bounds.minWorld.x &&
				 beam.minWorld.y <= d.bounds.maxWorld.y && beam.maxWorld.y >= d.bounds.minWorld.y &&
				 beam.minWorld.z <= d.bounds.maxWorld.z && beam.maxWorld.z >= d.bounds.minWorld.z))
				uncertain = true;
		if (p.scope != Scope::CentreVisibility && !edge.receiver.footprint.count)
			return out;
		if (p.scope == Scope::PhysicalFootprint &&
			(source.geometry.kind == EmitterKind::Point || source.geometry.kind == EmitterKind::Spot ||
			 source.geometry.kind == EmitterKind::Rectangle))
		{
			auto triangles = std::make_shared<std::vector<Bake::TracePrimitive>>();
			for (const auto &t : relevant.triangles)
			{
				const auto cull = context.visibility->Surfaces()[size_t(t.surface - 1)].cull;
				if (cull == SurfaceCull::Unknown)
				{
					uncertain = true;
					out.issues |= SidednessUnknown;
				}
				if (cull == SurfaceCull::Front || cull == SurfaceCull::Back)
				{
					V a(t.geometry.verticesWorld[0]);
					V normal =
						Unit(Cross(V(t.geometry.verticesWorld[1]) - a, V(t.geometry.verticesWorld[2]) - a));
					double side = Dot(normal, V(source.geometry.position.world) - a);
					if (std::abs(side) <= Extent(source.geometry) + 1e-8)
					{
						uncertain = true;
						out.issues |= SidednessUnknown;
					}
					else
					{
						bool front = side < 0;
						if ((cull == SurfaceCull::Front && front) || (cull == SurfaceCull::Back && !front))
							continue;
					}
				}
				Bake::TracePrimitive v;
				v.triangle = t.geometry;
				v.optical = t.optical;
				triangles->push_back(v);
			}
			auto scene = std::make_shared<Bake::IntegralScene>();
			scene->complete = relevant.coverageComplete;
			scene->triangles = Lease(triangles);
			scene->geometryKey = context.visibility->CertificationKey();
			auto pieces = std::make_shared<std::vector<Bake::FootprintTriangle>>(
				edge.receiver.footprint.data, edge.receiver.footprint.data + edge.receiver.footprint.count);
			for (auto &t : *pieces)
				for (auto &v : t.world)
					v = (V(v) + shift).Float();
			Bake::Receiver r = edge.receiver;
			r.world = receiver.Float();
			r.footprint = Lease(pieces);
			auto emitters = std::make_shared<std::vector<Bake::SourceHypothesis>>(1);
			auto &s = (*emitters)[0];
			s.id = source.geometry.source.id;
			s.position = source.geometry.position;
			s.orientation = source.geometry.frame;
			s.angular = source.geometry.angular;
			s.geometry = source.geometry.geometry;
			s.worldUnits = p.units;
			s.kind = source.geometry.kind == EmitterKind::Point
						 ? Bake::SourceKind::Point
						 : source.geometry.kind == EmitterKind::Spot ? Bake::SourceKind::Spot
																	 : Bake::SourceKind::Polygon;
			s.amplitudeDomain = Bake::AmplitudeDomain::CanonicalPhotometry;
			s.calibration.id = 1;
			s.calibration.kind = CalibrationKind::PhysicallyAuthored;
			s.canonicalEmission.calibration = 1;
			s.canonicalEmission.unit =
				s.kind == Bake::SourceKind::Polygon ? PhotometricUnit::Nits : PhotometricUnit::Candela;
			s.canonicalEmission.intensity.known = true;
			s.canonicalEmission.intensity.value = 1;
			s.canonicalEmission.color.known = true;
			s.canonicalEmission.color.rgb = Vec3(1, 1, 1);
			Bake::EvaluationContext cxt;
			cxt.integralScene = scene;
			Bake::Options o;
			o.mode = Bake::Mode::PhysicalIntegral;
			o.maxWork = p.maxWork - report.value.work;
			o.integralTolerance = 1e-6;
			o.quadratureDepth = 4;
			Bake::Prediction value;
			Check(Bake::PhysicalReference(Lease(emitters), r, cxt, o, read, value));
			report.value.work += value.work.quadrature + value.work.triangleTests;
			Step();
			out.known = value.available && !uncertain;
			out.visibility = value.integratedVisibility.value;
			out.response = value.available ? value.rgb.x : 0;
			out.error = value.numericalError.known ? value.numericalError.value : 0;
			out.exact = source.geometry.kind == EmitterKind::Point;
		}
		else
		{
			// Explicit numerical surrogate for finite tubes / multi-sample requests.
			unsigned order = p.scope == Scope::CentreVisibility ? 1 : p.receiverSamples;
			double total = 0;
			std::vector<std::pair<V, double>> probes;
			if (p.scope == Scope::CentreVisibility)
				probes.push_back({receiver, 1});
			else
			{
				double area = 0;
				for (size_t t = 0; t < edge.receiver.footprint.count; ++t)
				{
					const auto &f = edge.receiver.footprint.data[t];
					double a =
						Length(Cross(V(f.world[1]) - V(f.world[0]), V(f.world[2]) - V(f.world[0]))) * .5;
					area += a;
					for (unsigned x = 0; x < order; ++x)
						for (unsigned y = 0; y < order; ++y)
						{
							double u = (x + .333333333333) / order, v = (y + .333333333333) / order;
							if (u + v > 1)
							{
								u = 1 - u;
								v = 1 - v;
							}
							probes.push_back({V(f.world[0]) + (V(f.world[1]) - V(f.world[0])) * u +
												  (V(f.world[2]) - V(f.world[0])) * v + shift,
											  a / (order * order)});
						}
				}
				if (area <= 0)
					return out;
				for (auto &v : probes)
					v.second /= area;
			}
			for (const auto &probe : probes)
				for (const auto &node : nodes)
				{
					V r = probe.first;
					RayResult hit = Ray(r, node.position, edge.surface, edge.receiver.geometricNormal);
					out.issues |= hit.issues;
					double weight = node.weight * probe.second;
					total += weight;
					uncertain |=
						hit.state == VisibilityState::Unknown || hit.reliability != RayReliability::Geometric;
					if (hit.firstHit.present)
						out.casterSurfaces.insert(hit.firstHit.surface);
					if (hit.state == VisibilityState::Visible)
					{
						out.visibility += weight;
						V d = node.position - r;
						double length = Length(d);
						if (length > 1e-12)
						{
							double emission =
								node.finiteSurface
									? (source.geometry.angular.twoSided
										   ? std::abs(Dot(node.emissionNormal, d / length))
										   : std::max(0., -Dot(node.emissionNormal, d / length))) *
										  node.area
									: 1;
							out.response += weight * emission *
											(node.finiteSurface
												 ? 1
												 : p.units.worldUnitsPerMeter * p.units.worldUnitsPerMeter) *
											std::max(0., Dot(V(edge.receiver.shadingNormal), d / length)) /
											(length * length);
						}
					}
				}
			out.known = !uncertain && total > 0;
			out.exact = false;
		}
		// Source/caster association still uses actual BVH hits, independent of
		// reference clipping and without ignoring an unknown moving boundary.
		for (const auto &node : nodes)
		{
			auto hit = Ray(receiver, node.position, edge.surface, edge.receiver.geometricNormal);
			out.issues |= hit.issues;
			if (hit.firstHit.present)
				out.casterSurfaces.insert(hit.firstHit.surface);
			if (hit.state == VisibilityState::Unknown)
				out.known = false;
		}
		return out;
	}
};
bool Project(V source, V caster, V anchor, V normal, V &result)
{
	double numerator = Dot(anchor - source, normal), denominator = Dot(caster - source, normal);
	if (std::abs(denominator) < 1e-10)
		return false;
	double t = numerator / denominator;
	if (t <= 1 + 1e-8)
		return false;
	result = source + (caster - source) * t;
	return Finite(result);
}
} // namespace
Error Objective::Inspect(const std::vector<FitEvidence::Source> &sources, const ReadContext &read,
						 Report &out) const
{
	out = {};
	try
	{
		Cancel(read);
		const auto &c = data->context;
		const auto &p = c.policy;
		Need(sources.size() > 0 && sources.size() <= p.maxSources, Code::InvalidInput, "Shadow source count");
		uint64_t cap = read.maxBytes ? std::min(read.maxBytes, p.maxBytes) : p.maxBytes;
		Need(c.observations->edges.size() *
						 (p.maxAssociationsPerEdge *
							  (sizeof(Association) + sizeof(Caster) + sizeof(Family) + 64) +
						  512) +
					 uint64_t(p.maxLocalTriangles) * (sizeof(VisibilityTriangle) * 4 + sizeof(Caster) * 12) +
					 uint64_t(p.maxAssociationAttemptsPerEdge) * sizeof(Association) * 2 + 65536 <=
				 cap,
			 Code::MemoryLimit, "Shadow matching scratch/report preflight");
		std::vector<FitEvidence::Source> sorted = sources;
		std::sort(sorted.begin(), sorted.end(),
				  [](const FitEvidence::Source &a, const FitEvidence::Source &b) {
					  return a.geometry.source.id < b.geometry.source.id;
				  });
		for (size_t i = 0; i < sorted.size(); ++i)
		{
			const auto &s = sorted[i];
			ValidateSource(s);
			Need(s.geometry.source.id && (!i || s.geometry.source.id != sorted[i - 1].geometry.source.id) &&
					 s.geometry.position.known && Finite(s.geometry.position.world) && Positive(s.rgb),
				 Code::InvalidInput, "Invalid source for shadow association");
			Need(s.geometry.kind == EmitterKind::Point || s.geometry.kind == EmitterKind::Spot ||
					 s.geometry.kind == EmitterKind::Rectangle || s.geometry.kind == EmitterKind::Tube,
				 Code::Unavailable, "Finite shadow associations do not implement directional/sky sources");
		}
		Report report;
		report.scope = p.scope;
		report.oracle = !c.oracle.empty();
		report.rawEdges = c.observations->edges.size();
		Query query{c, read, report};
		double sum = 0;
		std::set<Id> storedCasters;
		for (const auto &edge : c.observations->edges)
		{
			query.Step();
			if (!edge.usable)
				continue;
			++report.eligibleEdges;
			std::vector<Association> candidates;
			uint64_t attempts = 0;
			double best = p.nullCost;
			for (const auto &source : sorted)
			{
				if (Peak(source.rgb) <= 0)
					continue;
				if (!c.oracle.empty() &&
					std::none_of(c.oracle.begin(), c.oracle.end(), [&](const OracleBinding &b) {
						return b.edge == edge.id && b.source == source.geometry.source.id;
					}))
					continue;
				double range = p.searchRadiusWorld;
				Bounds bounds =
					Box(V(edge.world) - V(edge.normal) * range, V(edge.world) + V(edge.normal) * range,
						edge.locationSigmaWorld + Extent(source.geometry) + 1);
				Include(bounds, V(source.geometry.position.world));
				Include(bounds, V(edge.world) + V(edge.tangent) * edge.extentWorld * .5);
				Include(bounds, V(edge.world) - V(edge.tangent) * edge.extentWorld * .5);
				LocalGeometry local = query.Local(bounds);
				report.value.work += local.triangles.size() * 3;
				query.Step();
				auto casters = Casters(local);
				std::set<Id> relevant;
				// Bounded ray fan discovers relevant visual surfaces, no scan of every map triangle.
				double localBand = std::max(2 * edge.locationSigmaWorld, edge.transitionWidthWorld);
				for (double shift : {-range, -range * .25, -localBand, 0., localBand, range * .25, range})
					for (double along : {-.5, 0., .5})
					{
						V r = V(edge.world) + V(edge.normal) * shift +
							  V(edge.tangent) * (edge.extentWorld * along);
						auto hit = query.Ray(r, V(source.geometry.position.world), edge.surface,
											 edge.receiver.geometricNormal);
						if (hit.firstHit.present)
							relevant.insert(hit.firstHit.surface);
						if (hit.terminalHit.present)
							relevant.insert(hit.terminalHit.surface);
					}
				for (const auto &caster : casters)
				{
					if (caster.surface == edge.surface || !relevant.count(caster.surface))
						continue;
					if (!c.oracle.empty() &&
						std::none_of(c.oracle.begin(), c.oracle.end(), [&](const OracleBinding &b) {
							return b.edge == edge.id && b.source == source.geometry.source.id &&
								   b.caster == caster.id;
						}))
						continue;
					Association a;
					++report.associationAttempts;
					Need(++attempts <= p.maxAssociationAttemptsPerEdge, Code::WorkLimit,
						 "Shadow edge association attempt budget");
					a.edge = edge.id;
					a.source = source.geometry.source.id;
					a.caster = caster.id;
					a.casterSurface = caster.surface;
					a.scope = p.scope;
					a.oracle = report.oracle;
					a.total = p.nullCost;
					a.state = MatchState::OutsideSupport;
					if (!storedCasters.count(caster.id))
					{
						Need(report.casters.size() < c.observations->edges.size() * p.maxAssociationsPerEdge,
							 Code::MemoryLimit, "Shadow debug caster report budget");
						storedCasters.insert(caster.id);
						report.casters.push_back(caster);
					}
					if (caster.optical != ObstacleKind::Opaque || caster.geometryErrorWorld > 0)
					{
						a.state = MatchState::UnknownGeometry;
						a.prediction.issues |=
							caster.geometryErrorWorld > 0 ? PatchApproximation : OpticalUnknown;
						++report.unknown;
						candidates.push_back(a);
						continue;
					}
					V pa, pb;
					if (!Project(V(source.geometry.position.world), V(caster.a), V(edge.world),
								 V(edge.receiver.geometricNormal), pa) ||
						!Project(V(source.geometry.position.world), V(caster.b), V(edge.world),
								 V(edge.receiver.geometricNormal), pb))
					{
						++report.rejected;
						continue;
					}
					V line = pb - pa;
					double length = Length(line);
					if (length < 1e-10)
					{
						++report.rejected;
						continue;
					}
					V tangent = line / length;
					V closest = pa + line * Clamp(Dot(V(edge.world) - pa, line) / Dot(line, line), 0, 1);
					double distance = Dot(closest - V(edge.world), V(edge.normal));
					if (Length(closest - V(edge.world)) > range + edge.extentWorld * .5)
					{
						++report.rejected;
						continue;
					}
					a.prediction.edgeWorld = closest.Float();
					a.prediction.tangent = tangent.Float();
					a.prediction.signedEdgeDistanceWorld = distance;
					a.prediction.predictedWidthWorld = 0;
					double low = 1e300, high = -1e300;
					auto nodes = SourceNodes(source, p.sourceSamples);
					if (source.geometry.kind == EmitterKind::Rectangle)
					{
						nodes.clear();
						const auto &g = source.geometry;
						for (double x : {-1., 1.})
							for (double y : {-1., 1.})
							{
								SourceNode node;
								node.position = V(g.position.world) +
												V(g.frame.right) * (x * g.geometry.halfWidthWorld) +
												V(g.frame.up) * (y * g.geometry.halfHeightWorld);
								nodes.push_back(node);
							}
					}
					for (const auto &n : nodes)
					{
						V x, y;
						if (Project(n.position, V(caster.a), V(edge.world), V(edge.receiver.geometricNormal),
									x) &&
							Project(n.position, V(caster.b), V(edge.world), V(edge.receiver.geometricNormal),
									y))
						{
							V d = y - x;
							V q = x + d * Clamp(Dot(V(edge.world) - x, d) / std::max(1e-20, Dot(d, d)), 0, 1);
							double v = Dot(q - V(edge.world), V(edge.normal));
							low = std::min(low, v);
							high = std::max(high, v);
						}
					}
					if (high >= low)
						a.prediction.predictedWidthWorld = high - low + 2 * edge.locationSigmaWorld;
					// Test both sides of the PREDICTED boundary, not only current observed pixels.
					double band = std::max(edge.locationSigmaWorld * 2,
										   a.prediction.predictedWidthWorld * .75 + edge.locationSigmaWorld);
					auto left = query.Visibility(edge, source, closest - V(edge.normal) * band, local),
						 right = query.Visibility(edge, source, closest + V(edge.normal) * band, local);
					a.prediction.visibilityKnown = left.known && right.known;
					a.prediction.issues = left.issues | right.issues;
					a.prediction.visibleLow = left.visibility;
					a.prediction.visibleHigh = right.visibility;
					a.prediction.integratedVisibility = (left.visibility + right.visibility) * .5;
					a.prediction.unitDirectResponse = (left.response + right.response) * .5;
					a.prediction.numericalDifference = left.error + right.error;
					a.prediction.exactPointFootprint = left.exact && right.exact;
					a.prediction.finiteSourceApproximation = source.geometry.kind == EmitterKind::Rectangle ||
															 source.geometry.kind == EmitterKind::Tube;
					if (!a.prediction.visibilityKnown)
					{
						a.state = MatchState::UnknownGeometry;
						++report.unknown;
					}
					else if (!left.casterSurfaces.count(caster.surface))
					{
						a.state = MatchState::WrongOccluder;
						++report.rejected;
					}
					else if (right.visibility - left.visibility < p.minimumPatternChange ||
							 std::max(left.response, right.response) <= 0)
					{
						a.state = MatchState::WrongPattern;
						++report.rejected;
					}
					else
					{
						a.state = MatchState::Candidate;
						a.prediction.available = true;
						double sigma = std::max(1e-6, edge.locationSigmaWorld);
						// Bounded robust edge-distance retains a nonzero proposal signal
						// throughout the search radius without turning a remote match
						// into a mandatory assignment. Null remains a separate model.
						double displacement = Huber(Band(distance, sigma) / sigma, p.huber);
						a.positionLoss =
							displacement / (1 + displacement) +
							p.tangentWeight *
								Huber(std::acos(Clamp(std::abs(Dot(tangent, V(edge.tangent))), 0, 1)) / .1,
									  p.huber);
						auto observedLow =
								 query.Visibility(edge, source, V(edge.world) - V(edge.normal) * band, local),
							 observedHigh =
								 query.Visibility(edge, source, V(edge.world) + V(edge.normal) * band, local);
						if (!observedLow.known || !observedHigh.known)
						{
							a.state = MatchState::UnknownGeometry;
							a.prediction.available = false;
							++report.unknown;
							candidates.push_back(a);
							continue;
						}
						a.patternLoss = Huber(std::max(0., p.minimumPatternChange - (observedHigh.visibility -
																					 observedLow.visibility)),
											  p.huber);
						if (edge.widthKnown)
							a.widthLoss =
								Huber(Band(a.prediction.predictedWidthWorld - edge.transitionWidthWorld,
										   2 * sigma) /
										  std::max(sigma, edge.transitionWidthWorld * .25),
									  p.huber);
						V delta(edge.deltaRgb), rgb(source.rgb);
						if (Length(delta) > 0 && Length(rgb) > 0)
							a.colourLoss =
								Huber(std::acos(Clamp(Dot(Unit(delta), Unit(rgb)), -1, 1)) / .2, p.huber);
						a.total = p.edgeWeight * a.positionLoss + p.patternWeight * a.patternLoss +
								  p.widthWeight * a.widthLoss + p.colourWeight * a.colourLoss;
						best = std::min(best, a.total);
						Family f;
						f.edge = edge.id;
						f.source = a.source;
						f.caster = a.caster;
						f.planeAnchor = caster.a;
						f.planeNormal =
							Unit(Cross(V(caster.b) - V(caster.a), V(edge.world) - V(caster.a))).Float();
						f.receiverBandWorld = edge.locationSigmaWorld;
						f.finiteOrderingChecked = true;
						if (report.families.size() < c.observations->edges.size() * p.maxAssociationsPerEdge)
							report.families.push_back(f);
						else
							++report.omittedDiagnostics;
					}
					candidates.push_back(a);
				}
			}
			std::sort(candidates.begin(), candidates.end(), [](const Association &a, const Association &b) {
				return a.total != b.total ? a.total < b.total
										  : a.source != b.source ? a.source < b.source : a.caster < b.caster;
			});
			size_t kept = 0;
			bool matched = false;
			for (auto &a : candidates)
			{
				a.retained = a.state == MatchState::Candidate && a.total <= best + p.associationSlack;
				matched |= a.retained;
				if (kept < p.maxAssociationsPerEdge)
				{
					report.attempted.push_back(a);
					++kept;
				}
				else
					++report.omittedDiagnostics;
			}
			if (!matched)
			{
				++report.unassigned;
			}
			else
				++report.value.matched;
			sum += std::min(best, p.nullCost);
		}
		report.value.available = report.eligibleEdges > 0 || !p.requireUsableEvidence;
		report.value.informative = report.value.matched > 0;
		report.value.loss = report.eligibleEdges ? sum / report.eligibleEdges : 0;
		report.value.unresolved = report.unassigned;
		report.value.evaluations = 1;
		report.value.approximation =
			p.scope != Scope::PhysicalFootprint ||
			std::any_of(sorted.begin(), sorted.end(), [](const FitEvidence::Source &s) {
				return s.geometry.kind == EmitterKind::Tube || s.geometry.kind == EmitterKind::Rectangle;
			});
		out = std::move(report);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow association allocation");
	}
}
Error Objective::Evaluate(const std::vector<FitEvidence::Source> &sources, const ReadContext &read,
						  FitEvidence::Value &out) const
{
	out = {};
	Report report;
	auto e = Inspect(sources, read, report);
	if (e)
		out = report.value;
	return e;
}
Error Objective::EvaluateReference(const std::vector<FitEvidence::Source> &sources, const ReadContext &read,
								   FitEvidence::Value &out) const
{
	if (data->context.policy.scope == Scope::PhysicalFootprint)
		return Evaluate(sources, read, out);
	Context context = data->context;
	context.policy.scope = Scope::PhysicalFootprint;
	std::shared_ptr<const Objective> reference;
	auto error = Build(context, read, reference);
	if (!error)
		return error;
	return reference->Evaluate(sources, read, out);
}
Error Objective::Probe(const FitEvidence::Source &source, const Bake::Receiver &receiver,
					   const ReadContext &read, Prediction &out) const
{
	out = {};
	try
	{
		ValidateSource(source);
		Need(receiver.sample && Finite(receiver.world) && Normal(receiver.geometricNormal) &&
				 Normal(receiver.shadingNormal),
			 Code::InvalidInput, "Invalid fixed shadow receiver");
		Edge e;
		e.id = receiver.sample;
		e.surface = receiver.surface;
		e.receiver = receiver;
		e.world = receiver.world;
		e.normal = Unit(Cross(V(receiver.geometricNormal),
							  std::abs(receiver.geometricNormal.z) > .9 ? V(0, 1, 0) : V(0, 0, 1)))
					   .Float();
		e.tangent = Cross(V(receiver.geometricNormal), V(e.normal)).Float();
		Report r;
		Query query{data->context, read, r};
		auto local = query.Local(
			Box(V(receiver.world), V(source.geometry.position.world), Extent(source.geometry) + 1));
		auto v = query.Visibility(e, source, V(receiver.world), local);
		out.available = v.known;
		out.visibilityKnown = v.known;
		out.integratedVisibility = v.visibility;
		out.unitDirectResponse = v.response;
		out.exactPointFootprint = v.exact;
		out.numericalDifference = v.error;
		out.issues = v.issues;
		out.finiteSourceApproximation =
			source.geometry.kind == EmitterKind::Rectangle || source.geometry.kind == EmitterKind::Tube;
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shadow probe allocation");
	}
}
Error Objective::DebugTsv(const Report &r, size_t maximum, std::string &out) const
{
	out.clear();
	std::ostringstream s;
	for (const auto &e : data->context.observations->edges)
	{
		s << "# observed_edge\t" << e.id << '\t' << e.usable << '\t' << e.alternatives << '\t' << e.world.x
		  << '\t' << e.world.y << '\t' << e.world.z << '\t' << e.locationSigmaWorld << '\t'
		  << e.transitionWidthWorld << '\n';
		if (size_t(s.tellp()) > maximum)
			return Failure(Code::MemoryLimit, "Shadow edge TSV budget");
	}
	s << std::setprecision(12)
	  << "edge\tsource\tcaster\tsurface\tstate\tretained\tdistance_world\twidth_world\tvisibility_"
		 "low\tvisibility_high\tloss\tissues\toracle\n";
	for (const auto &a : r.attempted)
	{
		s << a.edge << '\t' << a.source << '\t' << a.caster << '\t' << a.casterSurface << '\t'
		  << unsigned(a.state) << '\t' << a.retained << '\t' << a.prediction.signedEdgeDistanceWorld << '\t'
		  << a.prediction.predictedWidthWorld << '\t' << a.prediction.visibleLow << '\t'
		  << a.prediction.visibleHigh << '\t' << a.total << '\t' << a.prediction.issues << '\t' << a.oracle
		  << '\n';
		if (size_t(s.tellp()) > maximum)
			return Failure(Code::MemoryLimit, "Shadow TSV budget");
	}
	out = s.str();
	return {};
}
Error Objective::DebugObj(const Report &r, size_t maximum, std::string &out) const
{
	out.clear();
	std::ostringstream s;
	s << std::setprecision(12);
	size_t id = 1;
	auto line = [&](V a, V b, const std::string &group) {
		if (size_t(s.tellp()) > maximum)
			return;
		s << "g " << group << "\nv " << a.x << ' ' << a.y << ' ' << a.z << "\nv " << b.x << ' ' << b.y << ' '
		  << b.z << "\nl " << id << ' ' << id + 1 << '\n';
		id += 2;
	};
	for (const auto &e : data->context.observations->edges)
		line(V(e.world) - V(e.tangent) * std::max(1., e.extentWorld) * .5,
			 V(e.world) + V(e.tangent) * std::max(1., e.extentWorld) * .5,
			 e.usable ? "observed_candidate" : "excluded_gradient");
	for (const auto &c : r.casters)
		line(V(c.a), V(c.b),
			 c.optical == ObstacleKind::Opaque && c.geometryErrorWorld == 0 ? "caster_candidate"
																			: "uncertain_caster");
	for (const auto &a : r.attempted)
		if (a.prediction.available)
			line(V(a.prediction.edgeWorld) - V(a.prediction.tangent),
				 V(a.prediction.edgeWorld) + V(a.prediction.tangent),
				 a.retained ? "retained_prediction" : "disagreement");
	if (size_t(s.tellp()) > maximum)
		return Failure(Code::MemoryLimit, "Shadow OBJ budget");
	out = s.str();
	return {};
}
} // namespace Shadow
} // namespace Core
} // namespace Rend2LightRecon
