// P03 identical-data angular codec comparison, owned analytic geometry only.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/lightrecon/field_basis.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
namespace A = Rend2LightRecon::Core::Angular;
using A::V;
using Clock = std::chrono::steady_clock;
double Seconds(Clock::time_point t)
{
	return std::chrono::duration<double>(Clock::now() - t).count();
}
double Y(V a)
{
	return .2126 * a[0] + .7152 * a[1] + .0722 * a[2];
}
double Angle(V a, V b)
{
	return std::acos(std::max(-1., std::min(1., A::Dot(A::Unit(a), A::Unit(b))))) * 180 / A::Pi;
}
V Black{{0, 0, 0}};
V Cross(V a, V b)
{
	return V{{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
}
double RectangleE(V n, std::vector<V> polygon)
{
	// Exact projected solid angle of a convex planar rectangle, hemisphere clipped.
	for (V &p : polygon)
		p = A::Unit(p);
	std::vector<V> clipped;
	for (size_t i = 0; i < polygon.size(); ++i)
	{
		V a = polygon[i], b = polygon[(i + 1) % polygon.size()];
		double da = A::Dot(n, a), db = A::Dot(n, b);
		if (da >= 0)
			clipped.push_back(a);
		if ((da < 0) != (db < 0))
		{
			double t = da / (da - db);
			clipped.push_back(A::Unit(A::Add(A::Mul(a, 1 - t), A::Mul(b, t))));
		}
	}
	V vector = Black;
	if (clipped.size() < 3)
		return 0;
	for (size_t i = 0; i < clipped.size(); ++i)
	{
		V a = clipped[i], b = clipped[(i + 1) % clipped.size()], c = Cross(a, b);
		double length = std::sqrt(A::Dot(c, c));
		if (length > 1e-15)
			vector = A::Add(vector, A::Mul(c, .5 * std::atan2(length, A::Dot(a, b)) / length));
	}
	return std::abs(A::Dot(n, vector));
}
struct Delta
{
	V axis, energy;
};
struct Scene
{
	std::string id;
	unsigned kind = 0, sourceCount = 0;
	std::vector<Delta> delta;
	bool HasDelta() const
	{
		return !delta.empty();
	}
	V Residual(V w) const
	{
		if (kind == 17)
			return A::Mul(V{{0, 0, .00005}}, A::SG(w, V{{-1, 0, 0}}, 16));
		if (kind == 0)
			return w[2] > 0 ? V{{.5, .4, .3}} : Black;
		if (kind == 2)
			return A::Add(A::Mul(V{{1, 1, 1}}, A::SG(w, V{{0, 0, 1}}, 64)),
						  A::Mul(V{{1, 1, 1}}, A::SG(w, V{{0, 0, -1}}, 64)));
		if (kind == 3)
			return A::Add(A::Mul(V{{1, .01, .01}}, A::SG(w, V{{1, 0, 0}}, 32)),
						  A::Mul(V{{.01, .01, 1}}, A::SG(w, V{{-1, 0, 0}}, 32)));
		if (kind == 10)
			return w[2] > 0 ? V{{.15 + .2 * w[2], .2 + .25 * w[2], .35 + .3 * w[2]}} : Black;
		if (kind == 11)
			return V{{.03 + .02 * w[2] + .005 * w[0], .04 + .02 * w[2], .05 + .01 * w[2]}};
		if (kind == 13)
		{ // Explicit opaque finite wall masks a smooth incident environment.
			bool hit = false;
			if (w[0] > 0)
			{
				double t = .4 / w[0];
				hit = std::abs(t * w[1]) < 4 && t * w[2] > -3 && t * w[2] < 1.2;
			}
			return hit ? Black : V{{.1, .2, .4}};
		}
		if (kind == 15)
		{
			V v{{.015, .015, .015}};
			V axes[] = {A::Unit(V{{.2, .1, 1}}), A::Unit(V{{-.9, .4, .1}}),
						A::Unit(V{{.2, -.9, .2}})};
			V rgb[] = {{{1, .03, .01}}, {{.01, .08, .8}}, {{.05, .6, .02}}};
			for (int i = 0; i < 3; ++i)
				v = A::Add(v, A::Mul(rgb[i], A::SG(w, axes[i], 32)));
			return v;
		}
		return V{{.002, .003, .004}};
	}
	V Direct(V w) const
	{
		if (kind == 17)
			return A::Mul(V{{1000000, 0, 0}}, A::SG(w, V{{0, 0, 1}}, 32));
		if (kind == 1)
			return A::Dot(w, A::Unit(V{{.3, .2, 1}})) >= std::cos(6 * A::Pi / 180) ? V{{30, 25, 20}}
																				   : Black;
		if (kind == 5 || kind == 6)
		{ // Luminous disk aperture + outgoing spot cone, not angular point smearing.
			if (w[2] <= 0)
				return Black;
			double t = 4 / w[2], x = t * w[0] - .2, y = t * w[1], r = kind == 5 ? .5 : 1.4;
			double cone = std::cos((kind == 5 ? 8 : 50) * A::Pi / 180);
			if (x * x + y * y > r * r || w[2] < cone)
				return Black;
			double g = (w[2] - cone) / (1 - cone);
			return A::Mul(V{{4, 3, 1}}, g * g);
		}
		if (kind == 7 || kind == 9)
		{ // Constant Lambertian rectangular emitter; two-sided evaluated from either side.
			if (w[2] <= 0)
				return Black;
			// kind9 has outward normal +z: seen from its back; only two-sided emission permits it.
			double t = 3 / w[2];
			return std::abs(t * w[0]) <= 1.2 && std::abs(t * w[1]) <= .5 ? V{{2, .7, .2}} : Black;
		}
		if (kind == 8)
		{ // Finite cylindrical lateral emitter, x axis, no emissive end caps.
			double qa = w[1] * w[1] + w[2] * w[2], qb = -6 * w[2], qc = 9 - .15 * .15,
				   D = qb * qb - 4 * qa * qc;
			if (qa <= 0 || D < 0)
				return Black;
			double t = (-qb - std::sqrt(D)) / (2 * qa);
			return t > 0 && std::abs(t * w[0]) < 2 ? V{{1.2, 2, .6}} : Black;
		}
		if (kind == 10)
			return w[2] > std::cos(6 * A::Pi / 180) ? V{{20, 18, 12}} : Black;
		if (kind == 12)
		{
			if (w[2] <= 0)
				return Black;
			double t = 2 / w[2];
			return std::abs(t * w[0]) < .22 && std::abs(t * w[1]) < 1 ? V{{8, 7, 4}} : Black;
		}
		if (kind == 14)
			return A::Mul(V{{2, .6, .1}}, A::SG(w, A::Unit(V{{-.3, .4, 1}}), 256));
		return Black;
	}
	V Radiance(V w) const
	{
		return A::Add(Direct(w), Residual(w));
	}
	V DeltaE(V n) const
	{
		V v = Black;
		for (auto d : delta)
			v = A::Add(v, A::Mul(d.energy, std::max(0., A::Dot(n, d.axis))));
		return v;
	}
};
std::vector<Scene> Scenes()
{
	std::vector<Scene> out;
	const char *ids[] = {"uniform_hemisphere",
						 "finite_directional_disk",
						 "opposing_directions",
						 "red_blue_directions",
						 "coloured_local_points",
						 "narrow_spot_aperture",
						 "broad_spot_aperture",
						 "rectangle",
						 "tube",
						 "two_sided_sheet",
						 "sun_and_soft_sky",
						 "diffuse_indirect",
						 "bright_window",
						 "wall_occlusion",
						 "sharp_light_weak_fill",
						 "previous_three_coloured_lobes",
						 "ideal_parallel_delta",
						 "extreme_dynamic_range"};
	for (unsigned i = 0; i < 18; ++i)
	{
		Scene s;
		s.id = ids[i];
		s.kind = i;
		s.sourceCount = (i == 1 || (i >= 5 && i <= 10) || i == 12 || i == 14 || i == 17) ? 1 : 0;
		if (i == 4)
		{
			s.delta = {{A::Unit(V{{2, .4, 3}}), V{{1, .03, .01}}},
					   {A::Unit(V{{-3, 1, 2}}), V{{.01, .1, .8}}},
					   {A::Unit(V{{.2, -3, 1}}), V{{.05, .6, .02}}}};
			s.sourceCount = 3;
		}
		if (i == 16)
		{
			s.delta = {{A::Unit(V{{.3, .2, 1}}), V{{1, .9, .7}}}};
			s.sourceCount = 1;
		}
		out.push_back(s);
	}
	return out;
}
std::vector<V> Irradiance(const Scene &s, const std::vector<A::Node> &grid,
						  const std::vector<V> &normals, bool direct = false)
{
	std::vector<V> light;
	light.reserve(grid.size());
	for (auto a : grid)
		light.push_back(direct ? s.Direct(a.direction) : s.Radiance(a.direction));
	std::vector<V> result;
	for (V n : normals)
	{
		V e = s.DeltaE(n);
		for (size_t i = 0; i < grid.size(); ++i)
			e = A::Add(
				e, A::Mul(light[i], grid[i].weight * std::max(0., A::Dot(n, grid[i].direction))));
		result.push_back(e);
	}
	return result;
}
std::vector<V> Accurate(const Scene &s, const std::vector<V> &normals, unsigned order,
						bool directOnly = false)
{
	// Analytic projected polygons / source-area integration are independent of fitted bases.
	auto q = A::Interval(order);
	std::vector<V> out;
	for (size_t ni = 0; ni < normals.size(); ++ni)
	{
		V n = normals[ni], direct = s.DeltaE(n), residual = A::Mul(V{{.002, .003, .004}}, A::Pi);
		if (s.kind == 0)
			residual = A::Mul(V{{.5, .4, .3}}, A::Pi * .5 * (1 + n[2]));
		else if (s.kind == 11)
			residual = V{{.03 * A::Pi + 2 * A::Pi / 3 * (.02 * n[2] + .005 * n[0]),
						  .04 * A::Pi + 2 * A::Pi / 3 * .02 * n[2],
						  .05 * A::Pi + 2 * A::Pi / 3 * .01 * n[2]}};
		else if (s.kind == 2 || s.kind == 3 || s.kind == 15)
		{
			residual = s.kind == 15 ? V{{.015 * A::Pi, .015 * A::Pi, .015 * A::Pi}} : Black;
			std::vector<Delta> lobes;
			if (s.kind == 2)
				lobes = {{V{{0, 0, 1}}, V{{1, 1, 1}}}, {V{{0, 0, -1}}, V{{1, 1, 1}}}};
			if (s.kind == 3)
				lobes = {{V{{1, 0, 0}}, V{{1, .01, .01}}}, {V{{-1, 0, 0}}, V{{.01, .01, 1}}}};
			if (s.kind == 15)
				lobes = {{A::Unit(V{{.2, .1, 1}}), V{{1, .03, .01}}},
						 {A::Unit(V{{-.9, .4, .1}}), V{{.01, .08, .8}}},
						 {A::Unit(V{{.2, -.9, .2}}), V{{.05, .6, .02}}}};
			for (auto l : lobes)
				residual =
					A::Add(residual, A::Mul(l.energy, A::SGDiffuse(n, l.axis, s.kind == 2 ? 64 : 32,
																   order > 128 ? 128 : order)));
		}
		else if (s.kind == 13)
		{
			double blocked =
				RectangleE(n, {V{{.4, -4, -3}}, V{{.4, 4, -3}}, V{{.4, 4, 1.2}}, V{{.4, -4, 1.2}}});
			residual = A::Mul(V{{.1, .2, .4}}, A::Pi - blocked);
		}
		else if (s.kind == 10)
		{ // Sky uses a separate dense reference; disk below remains exact/integrated.
			residual = Black;
			auto angular = A::Sphere(order, .311);
			for (auto p : angular)
				residual =
					A::Add(residual, A::Mul(s.Residual(p.direction),
											p.weight * std::max(0., A::Dot(n, p.direction))));
		}
		if (s.kind == 7 || s.kind == 9)
			direct = A::Mul(V{{2, .7, .2}}, RectangleE(n, {V{{-1.2, -.5, 3}}, V{{1.2, -.5, 3}},
														   V{{1.2, .5, 3}}, V{{-1.2, .5, 3}}}));
		if (s.kind == 12)
			direct = A::Mul(V{{8, 7, 4}}, RectangleE(n, {V{{-.22, -1, 2}}, V{{.22, -1, 2}},
														 V{{.22, 1, 2}}, V{{-.22, 1, 2}}}));
		if (s.kind == 14)
			direct = A::Mul(V{{2, .6, .1}}, A::SGDiffuse(n, A::Unit(V{{-.3, .4, 1}}), 256,
														 order > 128 ? 128 : order));
		if (s.kind == 1 || s.kind == 10)
		{
			V axis = s.kind == 10 ? V{{0, 0, 1}} : A::Unit(V{{.3, .2, 1}});
			double beta = A::Dot(n, axis), sinbeta = std::sqrt(std::max(0., 1 - beta * beta)),
				   mu0 = std::cos(6 * A::Pi / 180), value = 0;
			for (auto a : q)
			{
				double mu = mu0 + (1 - mu0) * a[0], A = beta * mu,
					   B = sinbeta * std::sqrt(1 - mu * mu), average = 0;
				if (A >= B)
					average = A;
				else if (A > -B && B > 0)
					average =
						(A * std::acos(-A / B) + B * std::sqrt(std::max(0., 1 - A * A / (B * B)))) /
						A::Pi;
				value += a[1] * (1 - mu0) * 2 * A::Pi * average;
			}
			direct = A::Mul(s.kind == 10 ? V{{20, 18, 12}} : V{{30, 25, 20}}, value);
		}
		if (s.kind == 17)
		{
			direct = A::Mul(V{{1000000, 0, 0}},
							A::SGDiffuse(n, V{{0, 0, 1}}, 32, order > 128 ? 128 : order));
			residual = A::Mul(V{{0, 0, .00005}},
							  A::SGDiffuse(n, V{{-1, 0, 0}}, 16, order > 128 ? 128 : order));
		}
		if (s.kind == 5 || s.kind == 6 || s.kind == 8)
		{
			direct = Black;
			for (auto a : q)
				for (auto b : q)
				{
					V pos, normal;
					double area;
					if (s.kind == 8)
					{
						double phi = 2 * A::Pi * b[0];
						normal = V{{0, std::sin(phi), std::cos(phi)}};
						pos = V{{4 * a[0] - 2, .15 * normal[1], 3 + .15 * normal[2]}};
						area = 4 * 2 * A::Pi * .15;
					}
					else
					{
						double r = (s.kind == 5 ? .5 : 1.4) * std::sqrt(a[0]),
							   phi = 2 * A::Pi * b[0];
						pos = V{{.2 + r * std::cos(phi), r * std::sin(phi), 4}};
						normal = V{{0, 0, -1}};
						area = A::Pi * (s.kind == 5 ? .5 * .5 : 1.4 * 1.4);
					}
					double r2 = A::Dot(pos, pos);
					V d = A::Unit(pos);
					double response =
						std::max(0., -A::Dot(normal, d)) * std::max(0., A::Dot(n, d)) / r2;
					V L = s.kind == 8 ? V{{1.2, 2, .6}} : s.Direct(d);
					direct = A::Add(direct, A::Mul(L, response * area * a[1] * b[1]));
				}
		}
		out.push_back(directOnly ? direct : A::Add(direct, residual));
	}
	return out;
}
std::vector<A::Sample> Samples(const Scene &s, const std::vector<A::Node> &nodes,
							   bool residual = false)
{
	std::vector<A::Sample> out;
	for (auto n : nodes)
		out.push_back(A::Sample{n, residual ? s.Residual(n.direction) : s.Radiance(n.direction)});
	return out;
}
void AddDelta(A::Field &f, const Scene &s)
{
	std::vector<double> Y;
	for (auto d : s.delta)
	{
		A::Basis(d.axis, f.degree, Y);
		for (size_t k = 0; k < Y.size(); ++k)
			f.sh[k] = A::Add(f.sh[k], A::Mul(d.energy, Y[k]));
	}
}
std::vector<V> Eval(const A::Field &f, const std::vector<V> &d, A::Quantity q)
{
	std::vector<V> out;
	for (V v : d)
		out.push_back(A::Evaluate(f, v, q).rgb);
	return out;
}
struct Metric
{
	double rmse = 0, relative = 0, luminance = 0, energyBias = 0, clampedEnergyBias = 0,
		   negativeIntegral = 0, negativeFraction = 0, falseDark = 0, colour = 0, peak = 0,
		   dominant = 0;
	bool relativeKnown = false, dominantKnown = false, peakAmbiguous = false;
	V integral{{0, 0, 0}};
};
Metric Metrics(const std::vector<V> &truth, const std::vector<V> &pred,
			   const std::vector<A::Node> &nodes)
{
	Metric m;
	double err = 0, ref = 0, sw = 0, yerr = 0, neg = 0, col = 0, cw = 0, peakT = -1, peakP = -1,
		   maxChannel = 0;
	size_t ti = 0, pi = 0;
	V sumT = Black, sumP = Black, clamped = Black, dirT = Black, dirP = Black;
	for (V a : truth)
		for (double c : a)
			maxChannel = std::max(maxChannel, c);
	for (size_t i = 0; i < truth.size(); ++i)
	{
		V t = truth[i], p = pred[i];
		double w = nodes[i].weight, yt = Y(t), yp = Y(p);
		sw += w;
		yerr += w * (yp - yt) * (yp - yt);
		if (yt > peakT)
		{
			peakT = yt;
			ti = i;
		}
		if (yp > peakP)
		{
			peakP = yp;
			pi = i;
		}
		sumT = A::Add(sumT, A::Mul(t, w));
		sumP = A::Add(sumP, A::Mul(p, w));
		dirT = A::Add(dirT, A::Mul(nodes[i].direction, w * yt));
		dirP = A::Add(dirP, A::Mul(nodes[i].direction, w * yp));
		double st = 0, sp = 0;
		for (unsigned c = 0; c < 3; ++c)
		{
			err += w * (p[c] - t[c]) * (p[c] - t[c]);
			ref += w * t[c] * t[c];
			if (p[c] < 0)
			{
				neg += w;
				m.negativeIntegral -= w * p[c];
			}
			clamped[c] += w * std::max(0., p[c]);
			if (t[c] <= maxChannel * 1e-6)
				m.falseDark += w * std::max(0., p[c]);
			st += t[c];
			sp += std::max(0., p[c]);
		}
		if (st > maxChannel * 1e-6 && sp > 0)
		{
			for (unsigned c = 0; c < 3; ++c)
				col += w * st * std::pow(std::max(0., p[c]) / sp - t[c] / st, 2);
			cw += w * st;
		}
	}
	m.rmse = std::sqrt(err / (3 * sw));
	m.relativeKnown = ref > 0;
	m.relative = ref > 0 ? std::sqrt(err / ref) : 0;
	m.luminance = std::sqrt(yerr / sw);
	m.integral = sumP;
	double et = std::sqrt(A::Dot(sumT, sumT));
	m.energyBias =
		et ? std::sqrt(A::Dot(A::Add(sumP, A::Mul(sumT, -1)), A::Add(sumP, A::Mul(sumT, -1)))) / et
		   : 0;
	m.clampedEnergyBias = et ? std::sqrt(A::Dot(A::Add(clamped, A::Mul(sumT, -1)),
												A::Add(clamped, A::Mul(sumT, -1)))) /
								   et
							 : 0;
	m.negativeFraction = neg / (3 * sw);
	m.colour = cw ? std::sqrt(col / cw) : 0;
	m.peak = Angle(nodes[ti].direction, nodes[pi].direction);
	for (size_t i = 0; i < truth.size(); ++i)
		if (Y(truth[i]) >= peakT * (1 - 1e-4) &&
			Angle(nodes[i].direction, nodes[ti].direction) > 15)
			m.peakAmbiguous = true;
	m.dominantKnown =
		std::sqrt(A::Dot(dirT, dirT)) > std::abs(Y(sumT)) * 1e-6 && A::Dot(dirP, dirP) > 1e-24;
	if (m.dominantKnown)
		m.dominant = Angle(dirT, dirP);
	return m;
}
void WriteMetric(std::ostream &o, const Metric &m)
{
	o << "{\"rgb_rmse\":" << m.rmse << ",\"relative_rms\":";
	if (m.relativeKnown)
		o << m.relative;
	else
		o << "null";
	o << ",\"luminance_rmse\":" << m.luminance
	  << ",\"energy_vector_relative_bias\":" << m.energyBias
	  << ",\"clamped_energy_vector_relative_bias\":" << m.clampedEnergyBias
	  << ",\"negative_integral\":" << m.negativeIntegral
	  << ",\"negative_fraction\":" << m.negativeFraction
	  << ",\"false_dark_channel_energy\":" << m.falseDark << ",\"chromaticity_rms\":" << m.colour
	  << ",\"peak_error_degrees\":" << m.peak
	  << ",\"peak_ambiguous\":" << (m.peakAmbiguous ? "true" : "false")
	  << ",\"dominant_error_degrees\":";
	if (m.dominantKnown)
		o << m.dominant;
	else
		o << "null";
	o << "}";
}
void JsonV(std::ostream &o, V a)
{
	o << "[" << a[0] << "," << a[1] << "," << a[2] << "]";
}
int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	std::ofstream o(argv[1]);
	if (!o)
		return 2;
	o << std::setprecision(12);
	auto train = A::Sphere(40), dense = A::Sphere(80), test = A::Sphere(64, .173),
		 reference = A::Sphere(128, .311), coarse = A::Sphere(96, .739);
	auto normals = A::Directions(129);
	std::vector<A::Node> normalNodes;
	for (V n : normals)
		normalNodes.push_back(A::Node{n, 4 * A::Pi / normals.size()});
	std::vector<V> testDir;
	for (auto n : test)
		testDir.push_back(n.direction);
	o << "{\"schema\":\"org.openjk.angular-comparison.v1\",\"training_directions\":" << train.size()
	  << ",\"heldout_directions\":" << test.size() << ",\"normals\":" << normals.size()
	  << ",\"reference_directions\":" << reference.size() << ",\"rows\":[";
	bool comma = false;
	volatile double sink = 0;
	for (const auto &scene : Scenes())
	{
		auto samples = Samples(scene, train), residual = Samples(scene, train, true);
		A::Field denseProjection;
		A::Project(Samples(scene, dense), 8, denseProjection);
		AddDelta(denseProjection, scene);
		A::Field all, res;
		auto t0 = Clock::now();
		A::Project(samples, 8, all);
		AddDelta(all, scene);
		double projectionTime = Seconds(t0);
		A::Project(residual, 2, res);
		auto truthE = Accurate(scene, normals, 128), coarseE = Accurate(scene, normals, 96);
		t0 = Clock::now();
		auto directE = Accurate(scene, normals, 128, true);
		double explicitReferenceSeconds = Seconds(t0);
		double convergence = Metrics(truthE, coarseE, normalNodes).relative;
		std::vector<V> truthL;
		for (V d : testDir)
			truthL.push_back(scene.Radiance(d));
		struct Model
		{
			std::string id;
			A::Field f;
			A::FitWork work;
			double seconds = 0;
			bool hybrid = false;
			A::Code fitCode = A::Code::Ok;
		};
		std::vector<Model> models;
		for (unsigned L : {1u, 2u, 3u, 4u, 8u})
		{
			Model m;
			m.id = "SH_L" + std::to_string(L);
			t0 = Clock::now();
			A::Project(samples, L, m.f);
			AddDelta(m.f, scene);
			m.seconds = Seconds(t0);
			models.push_back(m);
		}
		if (!scene.HasDelta())
			for (unsigned k : {1u, 3u, 6u})
			{
				Model m;
				m.id = "SG_" + std::to_string(k);
				A::FitOptions options;
				options.lobes = k;
				options.iterations = 32;
				options.amplitudeSweeps = 4;
				t0 = Clock::now();
				m.fitCode = A::FitSG(samples, options, m.f, m.work);
				m.seconds = Seconds(t0);
				models.push_back(m);
				if (k == 3)
				{
					Model combined = m;
					combined.id = "SG_3_SH_L2";
					std::vector<A::Sample> remainder = residual;
					// Project signed residual directly: negative coefficients/values remain
					// visible.
					combined.f.sh.assign(9, Black);
					combined.f.degree = 2;
					std::vector<double> basis;
					for (auto a : samples)
					{
						V error = A::Add(a.rgb, A::Mul(A::Evaluate(m.f, a.node.direction,
																   A::Quantity::IncomingRadiance)
														   .rgb,
													   -1));
						A::Basis(a.node.direction, 2, basis);
						for (size_t i = 0; i < 9; ++i)
							combined.f.sh[i] =
								A::Add(combined.f.sh[i], A::Mul(error, basis[i] * a.node.weight));
					}
					combined.seconds = Seconds(t0);
					models.push_back(combined);
				}
			}
		Model hybrid;
		hybrid.id = "Explicit_SH_L2";
		hybrid.f = res;
		hybrid.hybrid = true;
		models.push_back(hybrid);
		for (auto &m : models)
		{
			if (comma)
				o << ",";
			comma = true;
			auto predL = Eval(m.f, testDir, A::Quantity::IncomingRadiance),
				 predE = Eval(m.f, normals, A::Quantity::DiffuseIrradiance);
			if (m.hybrid)
			{
				for (size_t i = 0; i < predL.size(); ++i)
					predL[i] = A::Add(predL[i], scene.Direct(testDir[i]));
				for (size_t i = 0; i < predE.size(); ++i)
					predE[i] = A::Add(predE[i], directE[i]);
			}
			o << "{\"scene\":\"" << scene.id << "\",\"representation\":\"" << m.id
			  << "\",\"sh_rgb_coefficients\":" << m.f.sh.size()
			  << ",\"sg_lobes_including_constant\":" << m.f.sg.size()
			  << ",\"stored_scalar_count\":" << m.f.sh.size() * 3 + m.f.sg.size() * 7
			  << ",\"fitted_degrees_of_freedom\":"
			  << m.f.sh.size() * 3 + (m.f.sg.empty() ? 0 : 3 + (m.f.sg.size() - 1) * 6)
			  << ",\"explicit_sources\":" << (m.hybrid ? scene.sourceCount : 0)
			  << ",\"explicit_geometry_supplied\":" << (m.hybrid ? "true" : "false")
			  << ",\"radiance\":";
			if (scene.HasDelta())
				o << "null";
			else
				WriteMetric(o, Metrics(truthL, predL, test));
			o << ",\"irradiance\":";
			WriteMetric(o, Metrics(truthE, predE, normalNodes));
			o << ",\"dense_projection_control\":";
			if (m.id.substr(0, 3) == "SH_")
			{
				auto p = denseProjection;
				p.degree = m.f.degree;
				p.sh.resize(m.f.sh.size());
				o << "{\"training_directions\":" << dense.size() << ",\"irradiance\":";
				WriteMetric(o, Metrics(truthE, Eval(p, normals, A::Quantity::DiffuseIrradiance),
									   normalNodes));
				o << "}";
			}
			else
				o << "null";
			double maximumCorrelation = 0;
			for (size_t a = 1; a < m.f.sg.size(); ++a)
				for (size_t b = a + 1; b < m.f.sg.size(); ++b)
				{
					double ab = 0, aa = 0, bb = 0;
					for (auto s : samples)
					{
						double x = A::SG(s.node.direction, m.f.sg[a].axis, m.f.sg[a].sharpness),
							   y = A::SG(s.node.direction, m.f.sg[b].axis, m.f.sg[b].sharpness);
						ab += s.node.weight * x * y;
						aa += s.node.weight * x * x;
						bb += s.node.weight * y * y;
					}
					if (aa * bb > 0)
						maximumCorrelation = std::max(maximumCorrelation, ab / std::sqrt(aa * bb));
				}
			o << ",\"reference_96_vs_128_relative\":" << convergence
			  << ",\"fit\":{\"budget_limited\":" << (m.work.budgetLimited ? "true" : "false")
			  << ",\"local_stagnation\":" << (m.work.localStagnation ? "true" : "false")
			  << ",\"iteration_limited\":" << (m.work.iterationLimited ? "true" : "false")
			  << ",\"maximum_lobe_response_cosine\":" << maximumCorrelation
			  << ",\"iterations\":" << m.work.iterations
			  << ",\"kernel_evaluations\":" << m.work.kernelEvaluations
			  << ",\"amplitude_sweeps\":" << m.work.amplitudeSweeps
			  << ",\"initial_mse\":" << m.work.initialMSE << ",\"final_mse\":" << m.work.finalMSE
			  << ",\"inactive_lobes\":" << m.work.inactiveLobes
			  << ",\"final_step\":" << m.work.finalAngularStep << "}";
			o << ",\"lobes\":[";
			for (size_t i = 0; i < m.f.sg.size(); ++i)
			{
				if (i)
					o << ",";
				o << "{\"axis\":";
				JsonV(o, m.f.sg[i].axis);
				o << ",\"sharpness\":" << m.f.sg[i].sharpness << ",\"energy\":";
				JsonV(o, m.f.sg[i].energy);
				o << "}";
			}
			o << "]";
			o << ",\"quantization\":[";
			bool qc = false;
			for (auto storage : {A::Storage::Float32, A::Storage::Float16, A::Storage::BlockInt16})
			{
				std::vector<uint8_t> bytes;
				auto code = A::Encode(m.f, storage, bytes);
				if (code != A::Code::Ok)
				{
					if (qc)
						o << ",";
					qc = true;
					o << "{\"storage\":" << unsigned(storage)
					  << ",\"status\":\"UnsupportedOrOverflow\"}";
					continue;
				}
				A::Field decoded;
				if (A::Decode(bytes, decoded) != A::Code::Ok)
					return 3;
				auto e = Eval(decoded, normals, A::Quantity::DiffuseIrradiance),
					 base = Eval(m.f, normals, A::Quantity::DiffuseIrradiance);
				auto l = Eval(decoded, normals, A::Quantity::IncomingRadiance),
					 bl = Eval(m.f, normals, A::Quantity::IncomingRadiance);
				if (qc)
					o << ",";
				qc = true;
				o << "{\"storage\":" << unsigned(storage)
				  << ",\"status\":\"Ok\",\"bytes\":" << bytes.size()
				  << ",\"extra_irradiance_rmse\":" << Metrics(base, e, normalNodes).rmse
				  << ",\"extra_radiance_rmse\":" << Metrics(bl, l, normalNodes).rmse
				  << ",\"channel_irradiance_rmse\":[";
				for (unsigned c = 0; c < 3; ++c)
				{
					double sq = 0;
					for (size_t i = 0; i < e.size(); ++i)
						sq += (e[i][c] - base[i][c]) * (e[i][c] - base[i][c]);
					if (c)
						o << ",";
					o << std::sqrt(sq / e.size());
				}
				o << "]}";
			}
			o << "]";
			// Same normalized generic CPU API. SG diffuse includes its actual 1D integral.
			t0 = Clock::now();
			for (unsigned rep = 0; rep < 2; ++rep)
				for (V n : normals)
					sink += A::Evaluate(m.f, n, A::Quantity::DiffuseIrradiance).rgb[0];
			double evalE = Seconds(t0) / (2 * normals.size());
			t0 = Clock::now();
			for (V d : testDir)
			{
				sink += A::Evaluate(m.f, d, A::Quantity::IncomingRadiance).rgb[0];
				if (m.hybrid)
					sink += scene.Direct(d)[0];
			}
			double evalL = Seconds(t0) / testDir.size();
			// Eight-neighbour value interpolation: never interpolate SG axes/widths as
			// coefficients.
			t0 = Clock::now();
			for (V n : normals)
			{
				V v = Black;
				for (unsigned j = 0; j < 8; ++j)
					v = A::Add(
						v, A::Mul(A::Evaluate(m.f, n, A::Quantity::DiffuseIrradiance).rgb, 1. / 8));
				sink += v[0];
			}
			double eight = Seconds(t0) / normals.size();
			o << ",\"timing\":{\"build_seconds\":" << m.seconds
			  << ",\"radiance_seconds_per_query\":" << evalL
			  << ",\"field_irradiance_seconds_per_query\":" << evalE
			  << ",\"eight_neighbour_field_irradiance_seconds\":" << eight
			  << ",\"explicit_reference_irradiance_seconds_per_query\":"
			  << (m.hybrid ? explicitReferenceSeconds / normals.size() : 0)
			  << ",\"radiance_includes_explicit_geometry\":true}}";
		}
		std::cerr << scene.id << " complete\n";
	}
	o << "],\"timing_sink_finite\":" << (std::isfinite(sink) ? "true" : "false") << "}\n";
	return o ? 0 : 2;
}
