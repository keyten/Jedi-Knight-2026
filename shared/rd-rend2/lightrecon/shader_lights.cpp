// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_lights.h"
#include "bake_math.h"
#include "geometry_key.h"
#include "source_orientation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <new>
#include <set>

namespace Rend2LightRecon
{
namespace Core
{
namespace ShaderLights
{
namespace
{
using namespace Bake::Detail;
constexpr uint32_t Magic = 0x314c5353;
uint64_t Limit(const ReadContext &r) { return r.maxBytes ? r.maxBytes : 128ull << 20; }
void Need(bool b, const char *s, Code c = Code::InvalidInput)
{
	if (!b)
		throw Failure(c, s);
}
void Cancel(const ReadContext &r)
{
	Need(!r.cancellation || !r.cancellation->IsCancelled(), "Shader light extraction cancelled",
		 Code::Cancelled);
}
struct Codec
{
	std::vector<uint8_t> bytes;
	size_t at = 0;
	bool reading = false;
	uint64_t limit;
	explicit Codec(uint64_t n) : limit(n) {}
	Codec(const std::vector<uint8_t> &b, uint64_t n) : bytes(b), reading(true), limit(n)
	{
		Need(b.size() <= n, "Shader payload limit", Code::MemoryLimit);
	}
	void U(uint32_t &v)
	{
		if (reading)
		{
			Need(at <= bytes.size() && bytes.size() - at >= 4, "Truncated shader surface payload");
			v = 0;
			for (unsigned j = 0; j < 4; ++j)
				v |= uint32_t(bytes[at++]) << (8 * j);
		}
		else
		{
			Need(bytes.size() <= limit && limit - bytes.size() >= 4, "Shader payload limit",
				 Code::MemoryLimit);
			for (unsigned j = 0; j < 4; ++j)
				bytes.push_back(uint8_t(v >> (8 * j)));
		}
	}
	void F(float &f)
	{
		uint32_t v = 0;
		if (!reading)
			std::memcpy(&v, &f, 4);
		U(v);
		if (reading)
			std::memcpy(&f, &v, 4);
		Need(std::isfinite(f), "Nonfinite shader value");
	}
	void D(double &f)
	{
		uint64_t v = 0;
		if (!reading)
			std::memcpy(&v, &f, 8);
		uint32_t lo = uint32_t(v), hi = uint32_t(v >> 32);
		U(lo);
		U(hi);
		if (reading)
		{
			v = uint64_t(lo) | (uint64_t(hi) << 32);
			std::memcpy(&f, &v, 8);
		}
		Need(std::isfinite(f), "Nonfinite shader UV");
	}
	void V3(Vec3 &v)
	{
		F(v.x);
		F(v.y);
		F(v.z);
	}
	void Text(std::string &s)
	{
		Need(s.size() <= 4096, "Shader path limit");
		uint32_t n = uint32_t(s.size());
		U(n);
		Need(n <= 4096, "Shader path limit");
		if (reading)
		{
			Need(n <= bytes.size() - at, "Truncated shader path");
			s.assign(reinterpret_cast<const char *>(bytes.data() + at), n);
			at += n;
		}
		else
		{
			Need(n <= limit - bytes.size(), "Shader text budget", Code::MemoryLimit);
			bytes.insert(bytes.end(), s.begin(), s.end());
		}
	}
};
void Fields(Codec &c, Surface &s)
{
	uint32_t magic = Magic, version = kVersion, origin = unsigned(s.origin),
			 type = unsigned(s.type), motion = unsigned(s.motion), cull = unsigned(s.cull);
	c.U(magic);
	c.U(version);
	Need(magic == Magic && version == kVersion, "Shader surface codec version");
	c.U(origin);
	c.U(type);
	c.U(motion);
	c.U(cull);
	s.origin = Origin(origin);
	s.type = BakedSurfaceKind(type);
	s.motion = SurfaceMotion(motion);
	s.cull = SurfaceCull(cull);
	c.U(s.ordinal);
	c.U(s.model);
	c.U(s.materialIndex);
	c.Text(s.material);
	c.Text(s.emissionTexture);
	c.F(s.legacySurfaceLight);
	c.V3(s.legacySourceColour);
	c.U(s.materialSignals);
	c.U(s.materialUncertainty);
	c.U(s.patchWidth);
	c.U(s.patchHeight);
	for (auto &style : s.styles)
	{
		uint32_t v = style;
		c.U(v);
		Need(v <= 255, "Invalid shader surface style");
		style = uint8_t(v);
	}
	for (auto &basis : s.lightmapBasis)
		c.V3(basis);
	uint32_t nv = uint32_t(s.vertices.size()), ni = uint32_t(s.indices.size());
	c.U(nv);
	c.U(ni);
	Need(nv <= 1048576 && ni <= 4194304 &&
			 uint64_t(nv) * sizeof(Vertex) + uint64_t(ni) * 4 <= c.limit,
		 "Shader geometry budget", Code::MemoryLimit);
	if (c.reading)
	{
		Need(uint64_t(nv) * 40 + uint64_t(ni) * 4 <= c.bytes.size() - c.at,
			 "Truncated shader geometry");
		s.vertices.resize(nv);
		s.indices.resize(ni);
	}
	for (auto &v : s.vertices)
	{
		c.V3(v.position);
		c.V3(v.normal);
		c.D(v.uv.u);
		c.D(v.uv.v);
	}
	for (auto &i : s.indices)
		c.U(i);
}
void Check(const Surface &s)
{
	Need(s.origin == Origin::Shader && unsigned(s.type) <= unsigned(BakedSurfaceKind::Flare) &&
			 unsigned(s.motion) <= unsigned(SurfaceMotion::Unknown) &&
			 unsigned(s.cull) <= unsigned(SurfaceCull::Back),
		 "Invalid shader surface type/origin/motion");
	Need(!s.material.empty() && s.material.size() <= 4096 && s.emissionTexture.size() <= 4096 &&
			 std::isfinite(s.legacySurfaceLight) && s.legacySurfaceLight > 0 &&
			 Finite(V(s.legacySourceColour)),
		 "Invalid shader emission metadata");
	for (auto &v : s.vertices)
		Need(Finite(V(v.position)) && Finite(V(v.normal)) && std::isfinite(v.uv.u) &&
				 std::isfinite(v.uv.v),
			 "Invalid shader vertices");
	for (auto i : s.indices)
		Need(i < s.vertices.size(), "Invalid shader surface index");
	if (s.type == BakedSurfaceKind::Patch)
		Need(s.patchWidth >= 3 && s.patchHeight >= 3 && (s.patchWidth & 1) && (s.patchHeight & 1) &&
				 uint64_t(s.patchWidth) * s.patchHeight == s.vertices.size(),
			 "Invalid shader Bezier control net");
	if (s.type == BakedSurfaceKind::Face || s.type == BakedSurfaceKind::Triangles)
		Need(s.indices.size() % 3 == 0, "Invalid shader triangles");
}
std::vector<uint8_t> Encode(const Surface &s, uint64_t limit)
{
	Check(s);
	Codec c(limit);
	Surface copy = s;
	Fields(c, copy);
	return std::move(c.bytes);
}
Disk::Digest Key(const Catalogue &c, uint64_t limit)
{
	GeometryKey k;
	k.U(kVersion);
	k.Digest(c.map.sha256);
	k.U(c.map.bytes);
	k.U(c.surfaces.size());
	for (auto &s : c.surfaces)
	{
		auto b = Encode(s, limit);
		k.Digest(Disk::Hash(b.data(), b.size()));
	}
	return k.Finish();
}
Id Identity(const Catalogue &c, const Surface &s, const char *role)
{
	GeometryKey k;
	k.Digest(c.map.sha256);
	k.U(kVersion);
	k.U(s.ordinal);
	std::string name = std::string(kEncoding) + role;
	for (auto ch : name)
		k.U(uint8_t(ch));
	auto d = k.Finish();
	Id id = 0;
	for (unsigned i = 0; i < 7; ++i)
		id |= Id(d[i]) << (8 * i);
	return id | (Id(2) << 56);
}
bool Rectangle(const Surface &s, PhysicalEmitter &e)
{
	if ((s.type != BakedSurfaceKind::Face && s.type != BakedSurfaceKind::Triangles) ||
		s.motion != SurfaceMotion::StaticWorld || s.vertices.size() != 4 || s.indices.size() != 6 ||
		(s.materialSignals & (MaterialSky | MaterialDeformed)))
		return false;
	std::map<std::pair<uint32_t, uint32_t>, unsigned> edges;
	for (size_t i = 0; i < 6; i += 3)
	{
		auto a = s.indices[i], b = s.indices[i + 1], c = s.indices[i + 2];
		if (a == b || a == c || b == c)
			return false;
		for (auto pair : {std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a)})
			++edges[std::minmax(pair.first, pair.second)];
	}
	unsigned boundary = 0, diagonal = 0;
	std::vector<unsigned> near;
	for (auto edge : edges)
	{
		if (edge.second == 1)
		{
			++boundary;
			if (edge.first.first == 0)
				near.push_back(edge.first.second);
			else if (edge.first.second == 0)
				near.push_back(edge.first.first);
		}
		else if (edge.second == 2)
			++diagonal;
		else
			return false;
	}
	if (boundary != 4 || diagonal != 1 || near.size() != 2)
		return false;
	unsigned opposite = 1;
	while (opposite == near[0] || opposite == near[1])
		++opposite;
	V a(s.vertices[0].position), x = V(s.vertices[near[0]].position) - a,
								 y = V(s.vertices[near[1]].position) - a;
	double lx = Length(x), ly = Length(y), tol = std::max(1., std::max(lx, ly)) * 1e-5;
	if (lx <= 1e-6 || ly <= 1e-6 || std::abs(Dot(x, y)) > lx * ly * 1e-5 ||
		Length(a + x + y - V(s.vertices[opposite].position)) > tol)
		return false;
	V n = Cross(x, y) / (lx * ly);
	// RBSP winding can oppose its authored normal. Reuse the source-side
	// contract instead of silently flipping a panel's photon direction.
	BakedSurface original;
	original.kind = s.type;
	original.materialFacts.cull = s.cull;
	std::copy(s.lightmapBasis, s.lightmapBasis + 3, original.lightmapBasis);
	for (const auto &v : s.vertices)
	{
		BakedVertex vertex;
		vertex.position = v.position;
		vertex.shadingNormal = v.normal;
		original.vertices.push_back(vertex);
	}
	V first, second;
	if (!SourceOrientation::Resolve(original, s.indices.data(), first) ||
		!SourceOrientation::Resolve(original, s.indices.data() + 3, second) ||
		Dot(first, second) < 1 - 1e-5 || std::abs(Dot(n, first)) < 1 - 1e-5)
		return false;
	if (Dot(n, first) < 0)
	{
		std::swap(x, y);
		std::swap(lx, ly);
		n = n * -1;
	}
	e.kind = EmitterKind::Rectangle;
	e.position.known = true;
	e.position.world = (a + (x + y) * .5).Float();
	e.frame.known = true;
	e.frame.right = (x / lx).Float();
	e.frame.up = (y / ly).Float();
	e.frame.forward = n.Float();
	e.geometry.known = true;
	e.geometry.halfWidthWorld = float(lx * .5);
	e.geometry.halfHeightWorld = float(ly * .5);
	e.angular.kind = AngularKind::Lambertian;
	e.angular.twoSided = s.cull == SurfaceCull::None;
	return true;
}
void CheckResult(Error e)
{
	if (!e)
		throw e;
}
} // namespace
Error Extract(const SurfaceGeometry &geometry, const ReadContext &read, Catalogue &out)
{
	try
	{
		Cancel(read);
		Catalogue c;
		c.map = geometry.Map();
		uint64_t bytes = 0;
		std::set<uint32_t> ordinals;
		for (auto &g : geometry.Surfaces())
		{
			Cancel(read);
			auto &f = g.materialFacts;
			Need(std::isfinite(f.legacySurfaceLight), "Nonfinite shader surface intensity");
			if (f.legacySurfaceLight <= 0)
				continue;
			Need(ordinals.insert(g.ordinal).second, "Duplicate shader source surface");
			Surface s;
			s.ordinal = g.ordinal;
			s.model = g.model;
			s.materialIndex = g.materialIndex;
			s.type = g.kind;
			s.motion = g.motion;
			s.material = g.material;
			s.emissionTexture = f.emissionTexture;
			s.legacySurfaceLight = f.legacySurfaceLight;
			s.legacySourceColour = f.legacySourceColour;
			s.cull = f.cull;
			s.materialSignals = f.signals;
			s.materialUncertainty = f.uncertainty;
			s.patchWidth = g.patchWidth;
			s.patchHeight = g.patchHeight;
			std::copy(g.styles, g.styles + 4, s.styles);
			std::copy(g.lightmapBasis, g.lightmapBasis + 3, s.lightmapBasis);
			uint64_t size = sizeof(Surface) + g.vertices.size() * sizeof(Vertex) +
							g.indices.size() * 4 + s.material.size() + s.emissionTexture.size();
			Need(size <= Limit(read) - bytes, "Shader catalogue memory budget", Code::MemoryLimit);
			bytes += size;
			for (auto &v : g.vertices)
			{
				Vertex p;
				p.position = v.position;
				p.normal = v.shadingNormal;
				p.uv = v.textureUv;
				s.vertices.push_back(p);
			}
			s.indices = g.indices;
			Check(s);
			c.surfaces.push_back(std::move(s));
		}
		std::sort(c.surfaces.begin(), c.surfaces.end(),
				  [](const Surface &a, const Surface &b) { return a.ordinal < b.ordinal; });
		c.contentKey = Key(c, Limit(read));
		Cancel(read);
		out = std::move(c);
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shader extraction allocation");
	}
}
bool IsShaderSource(const ReconstructedLightingAsset &a, const PhysicalEmitter &e)
{
	for (auto &p : a.provenanceRecords)
		if (p.id == e.source.provenance)
			return p.kind == ProvenanceKind::Authored && p.producer == kProvenance;
	return false;
}
Error Append(const Catalogue &catalogue, ReconstructedLightingAsset &asset,
			 std::vector<Disk::Chunk> &chunks, const ReadContext &read)
{
	try
	{
		Cancel(read);
		Need(catalogue.contentKey == Key(catalogue, Limit(read)), "Shader catalogue changed");
		// Stage only additions. Callers see no partially appended source on failure.
		std::vector<PhysicalEmitter> emitters;
		std::vector<Provenance> provenance;
		std::vector<Calibration> calibrations;
		std::vector<Dataset> datasets;
		std::vector<Disk::Chunk> payloads;
		std::set<Id> used;
		for (auto &p : asset.provenanceRecords)
			used.insert(p.id);
		for (auto &c : asset.calibrations)
			used.insert(c.id);
		for (auto &d : asset.datasets)
			used.insert(d.id);
		for (auto &e : asset.emitters)
			used.insert(e.source.id);
		for (auto &e : asset.environments)
			used.insert(e.source.id);
		for (auto &e : asset.transport)
			used.insert(e.source.id);
		for (auto &r : asset.regions)
			used.insert(r.id);
		for (auto &g : asset.hypothesisGroups)
			used.insert(g.id);
		for (auto &c : asset.lightingConfigurations)
			used.insert(c.id);
		for (auto &u : asset.uncertainty)
			used.insert(u.id);
		uint64_t bytes = 0;
		for (auto &s : catalogue.surfaces)
		{
			Cancel(read);
			Id source = Identity(catalogue, s, "source"),
			   prov = Identity(catalogue, s, "provenance"),
			   cal = Identity(catalogue, s, "calibration"),
			   data = Identity(catalogue, s, "geometry");
			for (auto id : {source, prov, cal, data})
				Need(used.insert(id).second, "Shader source ID collision or duplicate append");
			Disk::Chunk c;
			c.dataset = data;
			c.count = 1;
			c.bytes = Encode(s, Limit(read));
			Need(c.bytes.size() <= Limit(read) - bytes, "Shader payload budget", Code::MemoryLimit);
			bytes += c.bytes.size();
			Dataset d;
			d.id = data;
			d.purpose = DatasetPurpose::EmitterGeometry;
			d.layout = DatasetLayout::Chunked;
			d.encoding = kEncoding;
			d.encodingVersion = kVersion;
			d.elementCount = d.chunkElements = 1;
			d.contentHash = "sha256:" + Disk::Hex(Disk::Hash(c.bytes.data(), c.bytes.size()));
			d.locator = "embedded://shader-surfaces/" + std::to_string(s.ordinal);
			Provenance p;
			p.id = prov;
			p.kind = ProvenanceKind::Authored;
			p.producer = kProvenance;
			p.locator =
				catalogue.map.path + "#surface/" + std::to_string(s.ordinal) + "/" + s.material;
			p.contentHash = "sha256:" + Disk::Hex(catalogue.map.sha256);
			p.method = "Shader q3map_surfacelight; original surface geometry; legacy bake "
					   "intensity retained in payload; no physical calibration";
			if (asset.provenance)
				p.parents.push_back(asset.provenance);
			Calibration calibration;
			calibration.id = cal;
			calibration.kind = CalibrationKind::UncalibratedRelative;
			calibration.provenance = prov;
			calibration.method = "Unconverted legacy shader emission; unknown physical scale";
			PhysicalEmitter e;
			e.source.id = source;
			e.source.provenance = prov;
			e.kind = EmitterKind::Extension;
			e.extensionType = "org.openjk.shader-surface/1";
			e.geometry.extension = data;
			e.emission.calibration = cal;
			// The legacy parameter stays in Surface, never in physical photometry.
			if (Rectangle(s, e))
				e.extensionType.clear();
			emitters.push_back(e);
			provenance.push_back(p);
			calibrations.push_back(calibration);
			datasets.push_back(d);
			payloads.push_back(std::move(c));
		}
		Cancel(read);
		asset.emitters.reserve(asset.emitters.size() + emitters.size());
		asset.provenanceRecords.reserve(asset.provenanceRecords.size() + provenance.size());
		asset.calibrations.reserve(asset.calibrations.size() + calibrations.size());
		asset.datasets.reserve(asset.datasets.size() + datasets.size());
		chunks.reserve(chunks.size() + payloads.size());
		asset.emitters.insert(asset.emitters.end(), std::make_move_iterator(emitters.begin()),
							  std::make_move_iterator(emitters.end()));
		asset.provenanceRecords.insert(asset.provenanceRecords.end(),
									   std::make_move_iterator(provenance.begin()),
									   std::make_move_iterator(provenance.end()));
		asset.calibrations.insert(asset.calibrations.end(),
								  std::make_move_iterator(calibrations.begin()),
								  std::make_move_iterator(calibrations.end()));
		asset.datasets.insert(asset.datasets.end(), std::make_move_iterator(datasets.begin()),
							  std::make_move_iterator(datasets.end()));
		chunks.insert(chunks.end(), std::make_move_iterator(payloads.begin()),
					  std::make_move_iterator(payloads.end()));
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shader append allocation");
	}
}
Error Read(const ReconstructedLightingAsset &asset, const std::vector<Disk::Chunk> &chunks,
		   const PhysicalEmitter &e, const ReadContext &read, Surface &out)
{
	try
	{
		Cancel(read);
		Need(IsShaderSource(asset, e), "Emitter is not shader-authored");
		const Dataset *d = nullptr;
		for (auto &v : asset.datasets)
			if (v.id == e.geometry.extension)
			{
				Need(!d, "Duplicate shader dataset");
				d = &v;
			}
		Need(d && d->purpose == DatasetPurpose::EmitterGeometry &&
				 d->layout == DatasetLayout::Chunked && d->encoding == kEncoding &&
				 d->encodingVersion == kVersion && d->elementCount == 1 && d->chunkElements == 1,
			 "Shader geometry descriptor missing/incompatible");
		const Disk::Chunk *c = nullptr;
		for (auto &v : chunks)
			if (v.dataset == d->id)
			{
				Need(!c, "Duplicate shader chunk");
				c = &v;
			}
		Need(c && c->firstElement == 0 && c->count == 1 && c->bytes.size() <= Limit(read),
			 "Missing/invalid shader geometry chunk");
		Need(d->contentHash == "sha256:" + Disk::Hex(Disk::Hash(c->bytes.data(), c->bytes.size())),
			 "Shader geometry checksum mismatch");
		Codec codec(c->bytes, Limit(read));
		Surface s;
		Fields(codec, s);
		Need(codec.at == codec.bytes.size(), "Trailing shader surface data");
		Check(s);
		PhysicalEmitter expected;
		bool rect = Rectangle(s, expected);
		Need(e.kind == (rect ? EmitterKind::Rectangle : EmitterKind::Extension),
			 "Shader surface primitive changed");
		if (rect)
			Need(e.position.known && e.frame.known && e.geometry.known &&
					 Length(V(e.position.world) - V(expected.position.world)) < 1e-4 &&
					 Length(V(e.frame.right) - V(expected.frame.right)) < 1e-4 &&
					 Length(V(e.frame.up) - V(expected.frame.up)) < 1e-4 &&
					 Length(V(e.frame.forward) - V(expected.frame.forward)) < 1e-4 &&
					 e.geometry.halfWidthWorld == expected.geometry.halfWidthWorld &&
					 e.geometry.halfHeightWorld == expected.geometry.halfHeightWorld &&
					 e.angular.kind == expected.angular.kind &&
					 e.angular.twoSided == expected.angular.twoSided,
				 "Shader rectangle no longer matches original surface");
		else
			Need(e.extensionType == "org.openjk.shader-surface/1",
				 "Shader surface extension changed");
		Need(e.emission.unit == PhotometricUnit::Relative && !e.emission.intensity.known &&
				 !e.emission.intensity.bounded && e.source.state == HypothesisState::Candidate,
			 "Shader metadata promoted to calibrated/fitted light");
		Cancel(read);
		out = std::move(s);
		return {};
	}
	catch (const Error &x)
	{
		return x;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Shader decode allocation");
	}
}
Error Validate(const ReconstructedLightingAsset &a, const std::vector<Disk::Chunk> &chunks,
			   const ReadContext &r)
{
	try
	{
		std::set<Id> used;
		for (auto &e : a.emitters)
			if (IsShaderSource(a, e))
			{
				Surface s;
				CheckResult(Read(a, chunks, e, r, s));
				Need(used.insert(e.geometry.extension).second, "Duplicate shader surface geometry");
			}
		for (auto &d : a.datasets)
			if (d.encoding == kEncoding)
				Need(used.count(d.id) != 0, "Orphan shader surface payload");
		return {};
	}
	catch (const Error &e)
	{
		return e;
	}
}
} // namespace ShaderLights
} // namespace Core
} // namespace Rend2LightRecon
