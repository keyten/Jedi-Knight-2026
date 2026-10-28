// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_trace.h"
#include "bake_math.h"
#include <functional>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
using namespace Detail;
struct CompilerTrace::Data
{
	struct W
	{
		TracePrimitive t;
		std::vector<V> p;
	};
	struct N
	{
		bool plane = false, solid = false;
		V normal;
		double distance = 0;
		int child[2] = {-1, -1};
		uint64_t count = 0;
		std::vector<W> windings;
		std::vector<TracePrimitive> triangles;
	};
	std::vector<N> nodes, originalNodes;
	uint64_t triangles = 0, work = 0, fragments = 0;
	TraceLimits limits;
	void Check(const ReadContext &c)
	{
		if (Cancelled(c))
			throw Failure(Code::Cancelled, "Compiler trace build cancelled");
		if (++work > limits.maxWork || nodes.size() > limits.maxNodes || fragments > limits.maxFragments)
			throw Failure(Code::WorkLimit, "Compiler trace build budget");
		if ((nodes.capacity() + originalNodes.capacity()) * sizeof(N) +
				fragments * (sizeof(W) + 6 * sizeof(V)) >
			limits.maxBytes)
			throw Failure(Code::MemoryLimit, "Compiler trace build memory");
	}
	static std::vector<V> Split(const std::vector<V> &p, V n, double d, int side)
	{
		std::vector<V> out;
		std::vector<double> ds;
		std::vector<int> ss;
		bool f = false, b = false;
		for (V v : p)
		{
			double q = Dot(v, n) - d;
			ds.push_back(q);
			ss.push_back(q < -.25 ? -1 : q > .25 ? 1 : 0);
			f |= q > .25;
			b |= q < -.25;
		}
		if (!b)
			return side == 0 ? p : out;
		if (!f)
			return side == 1 ? p : out;
		for (size_t i = 0; i < p.size(); ++i)
		{
			size_t j = (i + 1) % p.size();
			if (side == 0 ? ss[i] >= 0 : ss[i] <= 0)
				out.push_back(p[i]);
			if (ss[i] == 0 || ss[j] == 0 || ss[i] == ss[j])
				continue;
			double t = ds[i] / (ds[i] - ds[j]);
			V q = p[i] + (p[j] - p[i]) * t;
			for (size_t k = 0; k < 3; ++k)
			{
				if (n[k] == 1)
					q[k] = d;
				else if (n[k] == -1)
					q[k] = -d;
			}
			out.push_back(q);
		}
		return out;
	}
	void Insert(int index, const W &w, unsigned depth, const ReadContext &c)
	{
		Check(c);
		if (depth > 128)
			throw Failure(Code::InvalidInput, "Cyclic/deep compiler BSP");
		if (!nodes[index].plane)
		{
			nodes[index].windings.push_back(w);
			++fragments;
			Check(c);
			return;
		}
		V normal = Unit(Cross(V(w.t.triangle.verticesWorld[2]) - V(w.t.triangle.verticesWorld[0]),
							  V(w.t.triangle.verticesWorld[1]) - V(w.t.triangle.verticesWorld[0])));
		double cosine = Dot(nodes[index].normal, normal), d = Dot(normal, V(w.t.triangle.verticesWorld[0]));
		if (cosine > .999 && std::abs(nodes[index].distance - d) < .001)
		{
			Insert(nodes[index].child[0], w, depth + 1, c);
			return;
		}
		if (cosine < -.999 && std::abs(nodes[index].distance + d) < .001)
		{
			Insert(nodes[index].child[1], w, depth + 1, c);
			return;
		}
		for (int s = 0; s < 2; ++s)
		{
			W a = w;
			a.p = Split(w.p, nodes[index].normal, nodes[index].distance, s);
			if (a.p.size() >= 3)
				Insert(nodes[index].child[s], a, depth + 1, c);
		}
	}
	void Subdivide(int index, unsigned depth, const ReadContext &c)
	{
		Check(c);
		if (depth >= 32)
			return;
		if (nodes[index].plane)
		{
			Subdivide(nodes[index].child[0], depth + 1, c);
			Subdivide(nodes[index].child[1], depth + 1, c);
			return;
		}
		size_t count = 0;
		V lo(1e100, 1e100, 1e100), hi(-1e100, -1e100, -1e100), sum;
		for (const W &w : nodes[index].windings)
			for (V p : w.p)
			{
				++count;
				sum = sum + p;
				for (int k = 0; k < 3; ++k)
				{
					lo[k] = std::min(lo[k], p[k]);
					hi[k] = std::max(hi[k], p[k]);
				}
			}
		if (count < 2 * nodes[index].windings.size() + 5)
			return;
		int axis = 0;
		for (int k = 1; k < 3; ++k)
			if (hi[k] - lo[k] > hi[axis] - lo[axis])
				axis = k;
		if (hi[axis] - lo[axis] <= 32)
			return;
		double d = std::floor(sum[axis] / count);
		if (d <= lo[axis] || d >= hi[axis])
			d = std::floor((hi[axis] + lo[axis]) / 2);
		V n;
		n[axis] = 1;
		std::vector<W> windings;
		windings.swap(nodes[index].windings);
		nodes[index].plane = true;
		nodes[index].normal = n;
		nodes[index].distance = d;
		for (int s = 0; s < 2; ++s)
		{
			int child = int(nodes.size());
			nodes[index].child[s] = child;
			nodes.push_back(N());
			Check(c);
			for (const W &w : windings)
			{
				W a = w;
				a.p = Split(w.p, n, d, s);
				if (a.p.size() >= 3)
				{
					nodes[child].windings.push_back(a);
					++fragments;
					Check(c);
				}
			}
		}
		Subdivide(nodes[index].child[0], depth + 1, c);
		Subdivide(nodes[index].child[1], depth + 1, c);
	}
	uint64_t Triangulate(int i, const ReadContext &c)
	{
		Check(c);
		N &n = nodes[i];
		if (n.plane)
		{
			n.count = Triangulate(n.child[0], c) + Triangulate(n.child[1], c);
			return n.count;
		}
		for (const W &w : n.windings)
			for (size_t k = 1; k + 1 < w.p.size(); ++k)
			{
				if (Length(Cross(w.p[k] - w.p[0], w.p[k + 1] - w.p[0])) < 1e-10)
					continue;
				TracePrimitive t = w.t;
				t.triangle.verticesWorld[0] = w.p[0].Float();
				t.triangle.verticesWorld[1] = w.p[k].Float();
				t.triangle.verticesWorld[2] = w.p[k + 1].Float();
				n.triangles.push_back(t);
				++triangles;
				Check(c);
			}
		std::vector<W>().swap(n.windings);
		n.count = n.triangles.size();
		return n.count;
	}
};
Error CompilerTrace::Build(const BspSpaceInput &b, const Chunk<TracePrimitive> &t, const Disk::Digest &hash,
						   const TraceLimits &limits, const ReadContext &context,
						   std::shared_ptr<const CompilerTrace> &out)
{
	out.reset();
	try
	{
		if (!t.owner || !t.count || !t.data || b.nodes.empty() || b.leaves.empty())
			return Failure(Code::MissingGeometry, "Compiler trace needs original BSP and triangles");
		if (!limits.completeOpaqueSkyWorldGeometry)
			return Failure(Code::Unavailable,
						   "Compiler trace requires explicit complete opaque/sky geometry coverage");
		std::shared_ptr<Data> d(new Data);
		d->limits = limits;
		if (context.maxBytes)
			d->limits.maxBytes = std::min(d->limits.maxBytes, context.maxBytes);
		std::vector<bool> active(b.nodes.size(), false);
		std::function<int(int, unsigned)> build = [&](int raw, unsigned depth) {
			d->Check(context);
			if (depth > 128)
				throw Failure(Code::InvalidInput, "BSP depth/cycle");
			int index = int(d->nodes.size());
			d->nodes.push_back(Data::N());
			if (raw < 0)
			{
				int64_t leaf = -int64_t(raw) - 1;
				if (leaf >= int64_t(b.leaves.size()))
					throw Failure(Code::InvalidInput, "BSP leaf index");
				d->nodes[index].solid = b.leaves[size_t(leaf)].cluster == -1;
				return index;
			}
			if (size_t(raw) >= b.nodes.size() || active[raw])
				throw Failure(Code::InvalidInput, "BSP node index/cycle");
			active[raw] = true;
			const auto &n = b.nodes[raw];
			if (n.plane >= b.planes.size() || !Normal(b.planes[n.plane].normal) ||
				!std::isfinite(b.planes[n.plane].distance))
				throw Failure(Code::InvalidInput, "BSP plane");
			d->nodes[index].plane = true;
			d->nodes[index].normal = V(b.planes[n.plane].normal);
			d->nodes[index].distance = b.planes[n.plane].distance;
			for (int s = 0; s < 2; ++s)
			{
				int child = build(n.children[s], depth + 1);
				d->nodes[index].child[s] = child;
			}
			active[raw] = false;
			return index;
		};
		build(0, 0);
		d->originalNodes = d->nodes;
		std::ostringstream signature;
		signature.imbue(std::locale::classic());
		signature << std::setprecision(17) << Disk::Hex(hash);
		for (const auto &p : b.planes)
			signature << " plane " << p.normal.x << ' ' << p.normal.y << ' ' << p.normal.z << ' '
					  << p.distance;
		for (const auto &n : b.nodes)
			signature << " node " << n.plane << ' ' << n.children[0] << ' ' << n.children[1];
		for (const auto &l : b.leaves)
			signature << " leaf " << l.cluster;
		for (size_t i = 0; i < t.count; ++i)
		{
			const auto &p = t.data[i];
			if (p.optical != ObstacleKind::Opaque && p.optical != ObstacleKind::Sky)
				return Failure(Code::Unavailable, "Compiler replay has unknown optics");
			Data::W w;
			w.t = p;
			for (Vec3 v : p.triangle.verticesWorld)
			{
				if (!Finite(v))
					return Failure(Code::InvalidInput, "Trace vertex");
				w.p.push_back(V(v));
				signature << ' ' << v.x << ' ' << v.y << ' ' << v.z;
			}
			signature << int(p.optical);
			d->Insert(0, w, 0, context);
		}
		d->Subdivide(0, 0, context);
		d->Triangulate(0, context);
		std::shared_ptr<CompilerTrace> result(new CompilerTrace);
		result->data_ = d;
		std::string bytes = signature.str();
		result->key_ = Disk::Hash(bytes.data(), bytes.size());
		result->scoped_ =
			Disk::Hex(hash) == "0ff603f3537a83b5ca6bc80eae344c6e697b37c356151667a0904c438c8157ba" &&
			d->nodes.size() == 395 && d->triangles == 764 &&
			Disk::Hex(result->key_) == "dff97eb017fcef72fa45e14a7727341648e10fb213845fbd01bb4fe17daed1a2";
		out = result;
		return Error();
	}
	catch (const Error &e)
	{
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Compiler trace allocation");
	}
}
Error CompilerTrace::Query(Vec3 from, Vec3 to, bool sky, const ReadContext &c, uint64_t maxWork,
						   RayResult &out) const
{
	out = RayResult();
	if (!data_ || !Finite(from) || !Finite(to) || Length(V(to) - V(from)) <= .5)
		return Failure(Code::InvalidInput, "Compiler trace ray");
	V start(from), end(to), direction = Unit(end - start);
	double maximum = Length(end - start);
	std::vector<int> leaves;
	uint64_t work = 0;
	std::function<bool(int, V, V)> walk = [&](int i, V a, V b) {
		if (++work > maxWork)
			throw Failure(Code::WorkLimit, "Compiler trace query budget");
		if (Cancelled(c))
			throw Failure(Code::Cancelled, "Compiler trace cancelled");
		const auto &n = data_->nodes[i];
		++out.nodeTests;
		if (!n.plane)
		{
			if (n.solid)
				return true;
			if (!n.triangles.empty())
				leaves.push_back(i);
			return false;
		}
		if (!n.count)
			return false;
		double f = Dot(a, n.normal) - n.distance, g = Dot(b, n.normal) - n.distance;
		if (f >= -.1 && g >= -.1)
			return walk(n.child[0], a, b);
		if (f < .1 && g < .1)
			return walk(n.child[1], a, b);
		int side = f < 0 ? 1 : 0;
		V mid = a + (b - a) * (f / (f - g));
		return walk(n.child[side], a, mid) || walk(n.child[1 - side], mid, b);
	};
	try
	{
		bool solidTermination = walk(0, start, end);
		bool reached = false;
		for (int i : leaves)
			for (const auto &t : data_->nodes[i].triangles)
			{
				if (Cancelled(c))
					return Failure(Code::Cancelled, "Compiler triangle trace cancelled");
				if (++work > maxWork)
					return Failure(Code::WorkLimit, "Compiler trace triangle budget");
				++out.primitiveTests;
				double distance;
				if (!Hit(start, direction, t.triangle, .5, maximum, .01, .25, distance))
					continue;
				if (t.optical == ObstacleKind::Opaque)
				{
					out.state = VisibilityState::Blocked;
					out.sky = SkyReach::NotReached;
					out.reliability = RayReliability::Geometric;
					return Error();
				}
				reached = true;
			}
		out.sky = reached ? SkyReach::Reached : SkyReach::NotReached;
		out.state = solidTermination && !reached
						? VisibilityState::Unknown
						: sky ? (reached ? VisibilityState::Visible : VisibilityState::Unknown)
							  : VisibilityState::Visible;
		out.reliability = RayReliability::Geometric;
		return Error();
	}
	catch (const Error &e)
	{
		return e;
	}
}
uint64_t CompilerTrace::OwnedBytes() const
{
	if (!data_)
		return 0;
	return sizeof(*this) + (data_->nodes.capacity() + data_->originalNodes.capacity()) * sizeof(Data::N) +
		   data_->triangles * sizeof(TracePrimitive);
}
size_t CompilerTrace::NodeCount() const { return data_ ? data_->nodes.size() : 0; }
size_t CompilerTrace::TriangleCount() const { return data_ ? size_t(data_->triangles) : 0; }
PointDomain CompilerTrace::ClassifyOrigin(Vec3 point) const
{
	if (!data_ || !Finite(point))
		return PointDomain::Unknown;
	size_t index = 0;
	for (unsigned depth = 0; depth < 129; ++depth)
	{
		const auto &n = data_->originalNodes[index];
		if (!n.plane)
			return n.solid ? PointDomain::Solid : PointDomain::Free;
		double d = Dot(V(point), n.normal) - n.distance;
		index = size_t(n.child[d < 0 ? 1 : 0]);
	}
	return PointDomain::Unknown;
}
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
