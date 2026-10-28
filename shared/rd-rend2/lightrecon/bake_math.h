// Internal double-precision math; never changes canonical storage conventions.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
namespace Detail
{
struct V
{
	double x = 0, y = 0, z = 0;
	V() = default;
	V(double a, double b, double c) : x(a), y(b), z(c) {}
	V(Vec3 a) : x(a.x), y(a.y), z(a.z) {}
	double &operator[](size_t i) { return i == 0 ? x : i == 1 ? y : z; }
	double operator[](size_t i) const { return i == 0 ? x : i == 1 ? y : z; }
	V operator+(V b) const { return V(x + b.x, y + b.y, z + b.z); }
	V operator-(V b) const { return V(x - b.x, y - b.y, z - b.z); }
	V operator*(double a) const { return V(x * a, y * a, z * a); }
	V operator/(double a) const { return *this * (1 / a); }
	Vec3 Float() const
	{
		Vec3 a;
		a.x = float(x);
		a.y = float(y);
		a.z = float(z);
		return a;
	}
};
inline double Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline double Length(V a) { return std::sqrt(Dot(a, a)); }
inline V Unit(V a)
{
	double l = Length(a);
	return l > 0 ? a / l : V();
}
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline bool Positive(V a) { return Finite(a) && a.x >= 0 && a.y >= 0 && a.z >= 0; }
inline double Peak(V a) { return std::max(a.x, std::max(a.y, a.z)); }
inline double Clamp(double x, double a, double b) { return std::max(a, std::min(b, x)); }
inline bool Normal(V a) { return Finite(a) && std::abs(Length(a) - 1) < 1e-4; }
inline bool Cancelled(const ReadContext &c) { return c.cancellation && c.cancellation->IsCancelled(); }
constexpr double Pi = 3.14159265358979323846;
inline bool Hit(V start, V direction, const Triangle &t, double minimum, double maximum, double epsilon,
				double detEpsilon, double &distance)
{
	V a(t.verticesWorld[0]), e = V(t.verticesWorld[1]) - a, f = V(t.verticesWorld[2]) - a,
							 p = Cross(direction, f);
	double d = Dot(e, p);
	if (std::abs(d) < detEpsilon)
		return false;
	V s = start - a, q = Cross(s, e);
	double u = Dot(s, p) / d, v = Dot(direction, q) / d;
	distance = Dot(f, q) / d;
	return u >= -epsilon && v >= -epsilon && u + v <= 1 + epsilon && distance >= minimum &&
		   distance <= maximum;
}
} // namespace Detail
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
