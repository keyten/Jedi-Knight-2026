#include "emission_geometry.h"
#include <cmath>
#include <algorithm>
#include <map>
#include <unordered_map>
#include <cstdio>
namespace Rend2LightRecon
{
namespace Core
{
namespace Emission
{
namespace
{
void Add(const float *a, const float *b, float *c)
{
	for (int i = 0; i < 3; ++i)
		c[i] = a[i] + b[i];
}
void Sub(const float *a, const float *b, float *c)
{
	for (int i = 0; i < 3; ++i)
		c[i] = a[i] - b[i];
}
void Scale(const float *a, float b, float *c)
{
	for (int i = 0; i < 3; ++i)
		c[i] = a[i] * b;
}
void MA(const float *a, float b, const float *c, float *d)
{
	for (int i = 0; i < 3; ++i)
		d[i] = a[i] + b * c[i];
}
float Dot(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void Cross(const float *a, const float *b, float *c)
{
	c[0] = a[1] * b[2] - a[2] * b[1];
	c[1] = a[2] * b[0] - a[0] * b[2];
	c[2] = a[0] * b[1] - a[1] * b[0];
}
void Perpendicular(float *d, const float *s)
{
	int pos = 0;
	float minelem = 1;
	for (int i = 0; i < 3; ++i)
		if (fabsf(s[i]) < minelem)
		{
			pos = i;
			minelem = fabsf(s[i]);
		}
	float t[3] = {};
	t[pos] = 1;
	float inv = 1 / Dot(s, s), v = Dot(s, t) * inv, n[3];
	Scale(s, inv, n);
	MA(t, -v, n, d);
	Scale(d, 1 / sqrtf(Dot(d, d)), d);
}
int Root(std::vector<int> &p, int i)
{
	while (p[i] != i)
	{
		p[i] = p[p[i]];
		i = p[i];
	}
	return i;
}
float Clamp(float a, float b, float x)
{
	return std::max(a, std::min(b, x));
}
int ClampI(int a, int b, int x)
{
	return std::max(a, std::min(b, x));
}
void Copy(const float *a, float *b)
{
	for (int i = 0; i < 3; ++i)
		b[i] = a[i];
}
} // namespace
void FitRectangle(const std::vector<const float *> &points, const float *normal, float margin,
				  Rectangle &rect)
{
	Rectangle *l = &rect;
	float centroid[3] = {0.0f, 0.0f, 0.0f};
	for (const float *p : points)
		Add(centroid, p, centroid);
	Scale(centroid, 1.0f / points.size(), centroid);

	float a1[3], a2[3];
	Perpendicular(a1, normal);
	Cross(normal, a1, a2);
	float cxx = 0, cxy = 0, cyy = 0;
	for (const float *p : points)
	{
		float d[3];
		Sub(p, centroid, d);
		const float x = Dot(d, a1), y = Dot(d, a2);
		cxx += x * x;
		cxy += x * y;
		cyy += y * y;
	}
	const float angle = 0.5f * atan2f(2.0f * cxy, cxx - cyy);
	Scale(a1, cosf(angle), l->right);
	MA(l->right, sinf(angle), a2, l->right);
	Cross(normal, l->right, l->up); // cross(right, up) = normal

	float minR = 1e30f, maxR = -1e30f, minU = 1e30f, maxU = -1e30f;
	for (const float *p : points)
	{
		float d[3];
		Sub(p, centroid, d);
		minR = std::min(minR, Dot(d, l->right));
		maxR = std::max(maxR, Dot(d, l->right));
		minU = std::min(minU, Dot(d, l->up));
		maxU = std::max(maxU, Dot(d, l->up));
	}
	l->halfWidth = 0.5f * (maxR - minR) + margin;
	l->halfHeight = 0.5f * (maxU - minU) + margin;
	MA(centroid, 0.5f * (maxR + minR), l->right, l->center);
	MA(l->center, 0.5f * (maxU + minU), l->up, l->center);
}

std::vector<std::vector<int>> Groups(const std::vector<Triangle> &tris, bool sourceConnected)
{
	// union triangles of one shader and plane sharing a vertex
	const int n = (int)tris.size();
	std::vector<int> parent(n);
	for (int i = 0; i < n; i++)
		parent[i] = i;
	std::unordered_map<std::string, int> firstAt;
	char key[256];
	for (int i = 0; i < n; i++)
	{
		const Triangle *t = &tris[i];
		const float dist = Dot(t->normal, t->v[0]);
		for (int k = 0; k < 3; k++)
		{
			std::snprintf(
				key, sizeof(key), "%u %d %d %d %d %d %d %d", t->material,
				(int)floorf(t->normal[0] * 50.0f + 0.5f), (int)floorf(t->normal[1] * 50.0f + 0.5f),
				(int)floorf(t->normal[2] * 50.0f + 0.5f), (int)floorf(dist + 0.5f),
				(int)floorf(t->v[k][0] * 2.0f + 0.5f), (int)floorf(t->v[k][1] * 2.0f + 0.5f),
				(int)floorf(t->v[k][2] * 2.0f + 0.5f));
			if (sourceConnected)
				std::snprintf(key, sizeof(key), "%u %.9g %.9g %.9g %.9g %.9g %.9g %.9g",
							  t->material, double(t->normal[0]), double(t->normal[1]),
							  double(t->normal[2]), double(dist),
							  double(t->v[k][0] == 0 ? 0 : t->v[k][0]),
							  double(t->v[k][1] == 0 ? 0 : t->v[k][1]),
							  double(t->v[k][2] == 0 ? 0 : t->v[k][2]));
			auto it = firstAt.find(key);
			if (it == firstAt.end())
				firstAt[key] = i;
			else
				parent[Root(parent, i)] = Root(parent, it->second);
		}
	}

	std::map<int, std::vector<int>> groups; // ordered: the same output every run
	for (int i = 0; i < n; i++)
		groups[Root(parent, i)].push_back(i);
	std::vector<std::vector<int>> out;
	for (auto &g : groups)
		out.push_back(std::move(g.second));
	return out;
}
Error Sample(const std::vector<Triangle> &tris, const std::vector<int> &members, const Mask *mask,
			 bool staticCoords, float threshold, uint32_t maxAxis, const ReadContext &ctx,
			 std::vector<Blob> &out)
{
	out.clear();
	if (maxAxis < 4 || maxAxis > 1024 || !std::isfinite(threshold) || threshold < 0)
		return Failure(Code::InvalidInput, "Invalid sample options");
	if (uint64_t(maxAxis) * maxAxis * 128 > ctx.maxBytes)
		return Failure(Code::MemoryLimit, "Emission scratch limit");
	if (ctx.cancellation && ctx.cancellation->IsCancelled())
		return Failure(Code::Cancelled, "Emission sampling cancelled");
	float area = 0;
	for (int m : members)
	{
		if (m < 0 || size_t(m) >= tris.size())
			return Failure(Code::InvalidInput, "Bad triangle reference");
		area += tris[m].area;
		for (int k = 0; k < 3; ++k)
		{
			for (float c : tris[m].v[k])
				if (!std::isfinite(c) || std::abs(c) > 1e8f)
					return Failure(Code::InvalidInput, "Invalid emission vertex");
			for (float c : tris[m].st[k])
				if (!std::isfinite(c) || std::abs(c) > 1e6f)
					return Failure(Code::InvalidInput, "Invalid emission UV");
		}
	}
	if (!std::isfinite(area) || area <= 0)
		return Failure(Code::InvalidInput, "Invalid emission area");
	if (mask && (!mask->width || !mask->height ||
				 uint64_t(mask->width) * mask->height * 3 != mask->rgb.size()))
		return Failure(Code::InvalidInput, "Invalid emission mask");
	if (mask)
	{
		for (float c : mask->rgb)
			if (!std::isfinite(c) || c < 0)
				return Failure(Code::InvalidInput, "Invalid emission mask values");
		for (float c : mask->average)
			if (!std::isfinite(c) || c < 0)
				return Failure(Code::InvalidInput, "Invalid mask average");
	}
	if (mask && staticCoords)
	{
		// texture space bounds of the group
		float smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f;
		for (int m : members)
			for (int k = 0; k < 3; k++)
			{
				smin = std::min(smin, tris[m].st[k][0]);
				smax = std::max(smax, tris[m].st[k][0]);
				tmin = std::min(tmin, tris[m].st[k][1]);
				tmax = std::max(tmax, tris[m].st[k][1]);
			}
		const int ns = ClampI(4, int(maxAxis),
							  (int)std::min(float(maxAxis), ceilf((smax - smin) * mask->width)));
		const int nt = ClampI(4, int(maxAxis),
							  (int)std::min(float(maxAxis), ceilf((tmax - tmin) * mask->height)));

		// sample grid: -1 outside the surface, 0 unlit, 1 lit
		std::vector<signed char> state((size_t)ns * nt, -1);
		std::vector<Point> points((size_t)ns * nt);
		std::vector<Point> colors((size_t)ns * nt);
		std::vector<float> weights((size_t)ns * nt, 0.0f);
		int inside = 0;
		float totalWeight = 0.0f;
		for (int j = 0; j < nt; j++)
			for (int i = 0; i < ns; i++)
			{
				if (ctx.cancellation && ctx.cancellation->IsCancelled())
					return Failure(Code::Cancelled, "Emission sampling cancelled");
				const float s = smin + (smax - smin) * (i + 0.5f) / ns;
				const float t = tmin + (tmax - tmin) * (j + 0.5f) / nt;
				for (int m : members)
				{
					// barycentrics in texture space
					const Triangle *tri = &tris[m];
					const float d =
						(tri->st[1][1] - tri->st[2][1]) * (tri->st[0][0] - tri->st[2][0]) +
						(tri->st[2][0] - tri->st[1][0]) * (tri->st[0][1] - tri->st[2][1]);
					if (fabsf(d) < 1e-10f)
						continue;
					const float b0 = ((tri->st[1][1] - tri->st[2][1]) * (s - tri->st[2][0]) +
									  (tri->st[2][0] - tri->st[1][0]) * (t - tri->st[2][1])) /
									 d;
					const float b1 = ((tri->st[2][1] - tri->st[0][1]) * (s - tri->st[2][0]) +
									  (tri->st[0][0] - tri->st[2][0]) * (t - tri->st[2][1])) /
									 d;
					const float b2 = 1.0f - b0 - b1;
					if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f)
						continue;

					inside++;
					const size_t cell = (size_t)j * ns + i;
					const float fs = (mask->clamp) ? Clamp(0.0f, 1.0f, s) : s - floorf(s);
					const float ft = (mask->clamp) ? Clamp(0.0f, 1.0f, t) : t - floorf(t);
					// One UV cell represents a different world area on each triangle.
					weights[cell] =
						2.0f * tri->area * ((smax - smin) / ns) * ((tmax - tmin) / nt) / fabsf(d);
					totalWeight += weights[cell];
					const int x = ClampI(0, mask->width - 1, (int)(fs * mask->width));
					const int y = ClampI(0, mask->height - 1, (int)(ft * mask->height));
					const float *rgb = &mask->rgb[((size_t)y * mask->width + x) * 3];
					const float lum = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
					state[cell] = lum > threshold ? 1 : 0;
					Scale(tri->v[0], b0, points[cell].p);
					MA(points[cell].p, b1, tri->v[1], points[cell].p);
					MA(points[cell].p, b2, tri->v[2], points[cell].p);
					Copy(rgb, colors[cell].p);
					break;
				}
			}
		if (inside == 0 || totalWeight <= 0.0f)
			return Error();
		// Normalize the discrete coverage back to the known geometry area.
		const float weightScale = area / totalWeight;

		// one light per connected lit blob (8-neighbours): a texture with two
		// tubes or a row of bulbs gives one rectangle each, not one with gaps
		std::vector<int> stack;
		for (size_t start = 0; start < state.size(); start++)
		{
			if (state[start] != 1)
				continue;
			Blob blob;
			float power[3] = {0.0f, 0.0f, 0.0f}, peak[3] = {0.0f, 0.0f, 0.0f};
			float litArea = 0.0f;
			state[start] = 2;
			stack.push_back((int)start);
			while (!stack.empty())
			{
				const int cell = stack.back();
				stack.pop_back();
				blob.points.push_back(points[cell]);
				const float sampleArea = weights[cell] * weightScale;
				litArea += sampleArea;
				MA(power, sampleArea, colors[cell].p, power);
				for (int k = 0; k < 3; k++)
					peak[k] = std::max(peak[k], colors[cell].p[k]);
				const int ci = cell % ns, cj = cell / ns;
				for (int dj = -1; dj <= 1; dj++)
					for (int di = -1; di <= 1; di++)
					{
						const int ni = ci + di, nj = cj + dj;
						if (ni < 0 || nj < 0 || ni >= ns || nj >= nt)
							continue;
						const int next = nj * ns + ni;
						if (state[next] == 1)
						{
							state[next] = 2;
							stack.push_back(next);
						}
					}
			}
			blob.area = litArea;
			blob.sampled = true;
			Copy(power, blob.power);
			Copy(peak, blob.peak);
			out.push_back(std::move(blob));
		}
		return Error();
	}

	// moving texture coordinates, or no image data: the whole surface
	Blob blob;
	for (int m : members)
		for (int k = 0; k < 3; k++)
		{
			Point p;
			Copy(tris[m].v[k], p.p);
			blob.points.push_back(p);
		}
	float power[3] = {1.0f, 1.0f, 1.0f};
	if (mask)
		Copy(mask->average, power);
	const float peak[3] = {1e30f, 1e30f, 1e30f};
	Scale(power, area, power);
	blob.area = area;
	Copy(power, blob.power);
	Copy(peak, blob.peak);
	out.push_back(std::move(blob));
	return Error();
}
} // namespace Emission
} // namespace Core
} // namespace Rend2LightRecon
