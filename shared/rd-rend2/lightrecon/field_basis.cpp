// Independent CPU SH/SG reference; no copied renderer implementation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "field_basis.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace Rend2LightRecon
{
namespace Core
{
namespace Angular
{
V Add(V a, V b)
{
	for (unsigned c = 0; c < 3; ++c)
		a[c] += b[c];
	return a;
}
V Mul(V a, double b)
{
	for (double &c : a)
		c *= b;
	return a;
}
double Dot(V a, V b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
bool Finite(V a)
{
	return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}
V Unit(V a)
{
	double d = std::sqrt(Dot(a, a));
	return d > 0 && std::isfinite(d) ? Mul(a, 1 / d) : V{{0, 0, 0}};
}
bool IsDirection(V a)
{
	return Finite(a) && std::abs(Dot(a, a) - 1) < 1e-9;
}
namespace
{
std::vector<std::array<double, 2>> Gauss(unsigned n)
{
	std::vector<std::array<double, 2>> out(n);
	for (unsigned i = 0; i < (n + 1) / 2; ++i)
	{
		double x = std::cos(Pi * (i + .75) / (n + .5)), der = 0;
		for (unsigned k = 0; k < 64; ++k)
		{
			double a = 1, b = x;
			for (unsigned j = 2; j <= n; ++j)
			{
				double t = ((2 * j - 1) * x * b - (j - 1) * a) / j;
				a = b;
				b = t;
			}
			der = n * (x * b - a) / (x * x - 1);
			double dx = b / der;
			x -= dx;
			if (std::abs(dx) < 2e-15)
				break;
		}
		double w = 2 / ((1 - x * x) * der * der);
		out[i] = {{-x, w}};
		out[n - 1 - i] = {{x, w}};
	}
	return out;
}
bool Positive(V v)
{
	return Finite(v) && v[0] >= 0 && v[1] >= 0 && v[2] >= 0;
}
bool ValidSamples(const std::vector<Sample> &s)
{
	if (s.empty() || s.size() > 131072)
		return false;
	double w = 0;
	for (const auto &a : s)
	{
		if (!IsDirection(a.node.direction) || !std::isfinite(a.node.weight) || a.node.weight < 0 ||
			!Positive(a.rgb))
			return false;
		w += a.node.weight;
	}
	return std::isfinite(w) && w > 0;
}
} // namespace
Code Basis(V d, unsigned L, std::vector<double> &out)
{
	if (L > 8 || !IsDirection(d))
		return Code::Invalid;
	out.assign((L + 1) * (L + 1), 0);
	double phi = std::atan2(d[1], d[0]), z = d[2];
	for (unsigned m = 0; m <= L; ++m)
	{
		double p = 1;
		for (unsigned k = 1; k <= m; ++k)
			p *= -(2 * double(k) - 1) * std::sqrt(std::max(0., 1 - z * z));
		double prev = 0;
		for (unsigned l = m; l <= L; ++l)
		{
			if (l > m)
			{
				double t = ((2 * l - 1) * z * p - (l + m - 1) * prev) / (l - m);
				prev = p;
				p = t;
			}
			double ratio = 1;
			for (unsigned k = l - m + 1; k <= l + m; ++k)
				ratio /= k;
			double norm = std::sqrt((2 * l + 1) / (4 * Pi) * ratio), v = norm * p;
			if (m == 0)
				out[l * l + l] = v;
			else
			{
				out[l * l + l - m] = std::sqrt(2.) * v * std::sin(m * phi);
				out[l * l + l + m] = std::sqrt(2.) * v * std::cos(m * phi);
			}
		}
	}
	return Code::Ok;
}
std::vector<Node> Sphere(unsigned n, double offset)
{
	std::vector<Node> out;
	if (n < 2 || n > 256 || !std::isfinite(offset))
		return out;
	for (const auto &q : Gauss(n))
		for (unsigned j = 0; j < 2 * n; ++j)
		{
			double phi = 2 * Pi * (j + offset) / (2 * n),
				   r = std::sqrt(std::max(0., 1 - q[0] * q[0]));
			out.push_back(Node{V{{r * std::cos(phi), r * std::sin(phi), q[0]}}, q[1] * Pi / n});
		}
	return out;
}
std::vector<std::array<double, 2>> Interval(unsigned n)
{
	if (n < 2 || n > 256)
		return {};
	auto q = Gauss(n);
	for (auto &x : q)
	{
		x[0] = (x[0] + 1) / 2;
		x[1] /= 2;
	}
	return q;
}
std::vector<V> Directions(unsigned n)
{
	std::vector<V> out;
	if (!n || n > 65536)
		return out;
	for (unsigned i = 0; i < n; ++i)
	{
		double z = 1 - 2 * (i + .5) / n, p = i * Pi * (3 - std::sqrt(5.)), r = std::sqrt(1 - z * z);
		out.push_back(V{{r * std::cos(p), r * std::sin(p), z}});
	}
	return out;
}
std::vector<double> CosineFactors(unsigned L)
{
	if (L > 8)
		return {};
	std::vector<double> f(L + 1, 0);
	for (auto q : Gauss(16))
	{
		double x = (q[0] + 1) / 2, w = q[1] / 2, a = 1, b = x;
		f[0] += 2 * Pi * w * x;
		if (L)
			f[1] += 2 * Pi * w * x * b;
		for (unsigned l = 2; l <= L; ++l)
		{
			double c = ((2 * l - 1) * x * b - (l - 1) * a) / l;
			f[l] += 2 * Pi * w * x * c;
			a = b;
			b = c;
		}
	}
	return f;
}
Code Project(const std::vector<Sample> &s, unsigned L, Field &output)
{
	if (!ValidSamples(s) || L > 8)
		return Code::Invalid;
	Field f;
	f.present = true;
	f.degree = L;
	f.sh.assign((L + 1) * (L + 1), V{{0, 0, 0}});
	std::vector<double> Y;
	for (const auto &a : s)
	{
		Basis(a.node.direction, L, Y);
		for (size_t k = 0; k < Y.size(); ++k)
			f.sh[k] = Add(f.sh[k], Mul(a.rgb, Y[k] * a.node.weight));
	}
	if (Validate(f) != Code::Ok)
		return Code::Invalid;
	output = std::move(f);
	return Code::Ok;
}
Code Validate(const Field &f)
{
	if (!f.present)
		return Code::Unavailable;
	if (unsigned(f.quantity) > unsigned(Quantity::DiffuseIrradiance) || f.degree > 8 ||
		f.sg.size() > 8 || (!f.sh.empty() && f.sh.size() != (f.degree + 1) * (f.degree + 1)) ||
		(f.sh.empty() && f.sg.empty()) ||
		(f.quantity == Quantity::DiffuseIrradiance && !f.sg.empty()))
		return Code::Invalid;
	for (V c : f.sh)
		if (!Finite(c))
			return Code::Invalid;
	for (const auto &l : f.sg)
		if (!IsDirection(l.axis) || !std::isfinite(l.sharpness) || l.sharpness < 0 ||
			l.sharpness > 4096 || !Positive(l.energy))
			return Code::Invalid;
	return Code::Ok;
}
double SG(V d, V a, double lambda)
{
	if (!IsDirection(d) || !IsDirection(a) || !std::isfinite(lambda) || lambda < 0 || lambda > 4096)
		return std::numeric_limits<double>::quiet_NaN();
	if (lambda < 1e-8)
		return 1 / (4 * Pi);
	return lambda / (-2 * Pi * std::expm1(-2 * lambda)) *
		   std::exp(lambda * (std::min(1., Dot(d, a)) - 1));
}
double SGDiffuse(V n, V a, double lambda, unsigned order)
{
	if (!IsDirection(n) || !IsDirection(a) || !std::isfinite(lambda) || lambda < 0 ||
		lambda > 4096 || order < 4 || order > 128)
		return std::numeric_limits<double>::quiet_NaN();
	if (lambda < 1e-8)
		return .25;
	double cosine = std::max(-1., std::min(1., Dot(n, a))),
		   sine = std::sqrt(std::max(0., 1 - cosine * cosine));
	double end = std::min(2 * lambda, 40.), sum = 0;
	std::vector<double> cuts{0, end};
	for (double u : {-sine, sine})
	{
		double t = lambda * (1 - u);
		if (t > 0 && t < end)
			cuts.push_back(t);
	}
	std::sort(cuts.begin(), cuts.end());
	auto gauss = Gauss(order);
	for (size_t i = 1; i < cuts.size(); ++i)
	{
		double half = (cuts[i] - cuts[i - 1]) / 2, mid = (cuts[i] + cuts[i - 1]) / 2;
		// Cosine change of variable regularizes the grazing square-root endpoints.
		for (auto q : gauss)
		{
			double theta = (q[0] + 1) * Pi / 2, t = mid - half * std::cos(theta),
				   u = 1 - t / lambda, A = cosine * u,
				   B = sine * std::sqrt(std::max(0., 1 - u * u)), avg = 0;
			if (A >= B)
				avg = A;
			else if (A > -B && B > 0)
			{
				double v = std::max(-1., std::min(1., -A / B));
				avg = (A * std::acos(v) + B * std::sqrt(std::max(0., 1 - v * v))) / Pi;
			}
			sum += half * std::sin(theta) * Pi / 2 * q[1] * std::exp(-t) * avg;
		}
	}
	return sum / (-std::expm1(-2 * lambda));
}
Evaluation Evaluate(const Field &f, V d, Quantity q)
{
	Evaluation e;
	e.code = Validate(f);
	if (e.code != Code::Ok)
		return e;
	if (!IsDirection(d) || unsigned(q) > 1)
	{
		e.code = Code::Invalid;
		return e;
	}
	if (f.quantity == Quantity::DiffuseIrradiance && q == Quantity::IncomingRadiance)
	{
		e.code = Code::Unsupported;
		return e;
	}
	if (!f.sh.empty())
	{
		std::vector<double> Y;
		Basis(d, f.degree, Y);
		static const auto factors = CosineFactors(8);
		for (unsigned l = 0; l <= f.degree; ++l)
			for (unsigned k = l * l; k < (l + 1) * (l + 1); ++k)
				e.rgb = Add(e.rgb, Mul(f.sh[k], Y[k] * ((q == Quantity::DiffuseIrradiance &&
														 f.quantity == Quantity::IncomingRadiance)
															? factors[l]
															: 1)));
	}
	for (auto l : f.sg)
		e.rgb = Add(e.rgb, Mul(l.energy, q == Quantity::IncomingRadiance
											 ? SG(d, l.axis, l.sharpness)
											 : SGDiffuse(d, l.axis, l.sharpness)));
	if (!Finite(e.rgb))
	{
		e.code = Code::Invalid;
		return e;
	}
	e.negative = e.rgb[0] < 0 || e.rgb[1] < 0 || e.rgb[2] < 0;
	return e;
}
Code FitSG(const std::vector<Sample> &s, const FitOptions &o, Field &output, FitWork &work)
{
	work = FitWork();
	if (!ValidSamples(s) || o.lobes > 7 || o.iterations > 128 || !o.amplitudeSweeps ||
		o.amplitudeSweeps > 64 || !o.maxKernelEvaluations)
		return Code::Invalid;
	double sw = 0;
	for (auto a : s)
		sw += a.node.weight;
	Field f;
	f.present = true;
	f.sg.push_back(Lobe{V{{0, 0, 1}}, 0, V{{0, 0, 0}}});
	std::vector<std::vector<double>> columns(1, std::vector<double>(s.size(), 1 / (4 * Pi)));
	auto makeColumn = [&](Lobe l, std::vector<double> &col) -> bool {
		if (work.kernelEvaluations + s.size() > o.maxKernelEvaluations)
		{
			work.budgetLimited = true;
			return false;
		}
		col.resize(s.size());
		for (size_t i = 0; i < s.size(); ++i)
			col[i] = SG(s[i].node.direction, l.axis, l.sharpness);
		work.kernelEvaluations += s.size();
		return true;
	};
	auto refit = [&](Field &model, const std::vector<std::vector<double>> &cols) -> double {
		std::vector<V> residual;
		residual.reserve(s.size());
		for (auto a : s)
			residual.push_back(a.rgb);
		for (size_t j = 0; j < cols.size(); ++j)
			for (size_t i = 0; i < s.size(); ++i)
				residual[i] = Add(residual[i], Mul(model.sg[j].energy, -cols[j][i]));
		for (unsigned sweep = 0; sweep < o.amplitudeSweeps; ++sweep)
			for (size_t j = 0; j < cols.size(); ++j)
			{
				V num{{0, 0, 0}};
				double den = 0;
				for (size_t i = 0; i < s.size(); ++i)
				{
					double w = s[i].node.weight, v = cols[j][i];
					den += w * v * v;
					num = Add(num, Mul(residual[i], w * v));
				}
				V old = model.sg[j].energy;
				for (unsigned c = 0; c < 3; ++c)
					model.sg[j].energy[c] = den > 0 ? std::max(0., old[c] + num[c] / den) : 0;
				V diff = Add(old, Mul(model.sg[j].energy, -1));
				for (size_t i = 0; i < s.size(); ++i)
					residual[i] = Add(residual[i], Mul(diff, cols[j][i]));
				++work.amplitudeSweeps;
			}
		double loss = 0;
		for (size_t i = 0; i < s.size(); ++i)
			loss += s[i].node.weight * Dot(residual[i], residual[i]);
		return loss / (3 * sw);
	};
	double best = refit(f, columns);
	work.initialMSE = best;
	auto axes = Directions(64);
	for (unsigned j = 0; j < o.lobes && !work.budgetLimited; ++j)
	{
		Field winner = f;
		double score = best;
		std::vector<double> winning;
		std::vector<V> residual;
		for (auto a : s)
			residual.push_back(a.rgb);
		for (size_t k = 0; k < columns.size(); ++k)
			for (size_t i = 0; i < s.size(); ++i)
				residual[i] = Add(residual[i], Mul(f.sg[k].energy, -columns[k][i]));
		for (V axis : axes)
			for (double width : {2., 8., 32., 128., 512.})
			{
				Lobe l{axis, width, V{{0, 0, 0}}};
				std::vector<double> col;
				if (!makeColumn(l, col))
					break;
				V num{{0, 0, 0}};
				double den = 0;
				for (size_t i = 0; i < s.size(); ++i)
				{
					double w = s[i].node.weight;
					den += w * col[i] * col[i];
					num = Add(num, Mul(residual[i], w * col[i]));
				}
				for (unsigned c = 0; c < 3; ++c)
					l.energy[c] = den > 0 ? std::max(0., num[c] / den) : 0;
				double loss = 0;
				for (size_t i = 0; i < s.size(); ++i)
				{
					V r = Add(residual[i], Mul(l.energy, -col[i]));
					loss += s[i].node.weight * Dot(r, r);
				}
				loss /= 3 * sw;
				if (loss < score)
				{
					score = loss;
					winner = f;
					winner.sg.push_back(l);
					winning = std::move(col);
				}
			}
		if (winning.empty())
			break;
		f = winner;
		columns.push_back(winning);
		best = refit(f, columns);
	}
	double step = .35;
	for (unsigned it = 0; it < o.iterations && !work.budgetLimited; ++it)
	{
		double before = best;
		for (size_t j = 1; j < f.sg.size() && !work.budgetLimited; ++j)
		{
			V axis = f.sg[j].axis, helper = std::abs(axis[2]) < .8 ? V{{0, 0, 1}} : V{{1, 0, 0}};
			V tangent = Unit(Add(helper, Mul(axis, -Dot(helper, axis))));
			V bitangent{{axis[1] * tangent[2] - axis[2] * tangent[1],
						 axis[2] * tangent[0] - axis[0] * tangent[2],
						 axis[0] * tangent[1] - axis[1] * tangent[0]}};
			for (unsigned move = 0; move < 6; ++move)
			{
				auto l = f.sg[j];
				double sign = move % 2 ? 1 : -1;
				// Angular step tracks width to avoid a narrow lobe jumping over all evidence.
				double angle = step / std::sqrt(std::max(1., l.sharpness / 8));
				if (move < 4)
					l.axis = Unit(Add(l.axis, Mul(move < 2 ? tangent : bitangent, sign * angle)));
				else
					l.sharpness =
						std::max(.05, std::min(4096., l.sharpness * std::exp(sign * step)));
				std::vector<double> col;
				if (!makeColumn(l, col))
					break;
				auto cc = columns;
				cc[j] = col;
				Field trial = f;
				trial.sg[j] = l;
				double loss = refit(trial, cc);
				if (loss < best)
				{
					best = loss;
					f = std::move(trial);
					columns[j] = std::move(col);
				}
			}
		}
		++work.iterations;
		if (before - best <= std::max(1e-20, before * 1e-5))
			step *= .5;
		if (step < .002)
		{
			work.localStagnation = true;
			break;
		}
	}
	work.finalMSE = refit(f, columns);
	work.finalAngularStep = step;
	work.iterationLimited =
		work.iterations == o.iterations && !work.localStagnation && !work.budgetLimited;
	for (auto l : f.sg)
		if (Dot(l.energy, l.energy) < 1e-20)
			++work.inactiveLobes;
	output = std::move(f);
	return work.budgetLimited ? Code::BudgetExceeded : Code::Ok;
}
namespace
{
void Put(std::vector<uint8_t> &b, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		b.push_back(uint8_t(v >> (8 * i)));
}
uint32_t Get(const std::vector<uint8_t> &b, size_t p)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; ++i)
		v |= uint32_t(b[p + i]) << (8 * i);
	return v;
}
uint32_t Checksum(const std::vector<uint8_t> &b, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i)
		h = (h ^ b[i]) * 16777619u;
	return h;
}
// IEEE binary16, round to nearest-even; reject overflow rather than saturate.
uint16_t Half(float f)
{
	uint32_t v;
	std::memcpy(&v, &f, 4);
	uint32_t sign = (v >> 16) & 0x8000, exp = (v >> 23) & 255, m = v & 0x7fffff;
	int e = int(exp) - 127 + 15;
	if (e >= 31)
		return uint16_t(sign | 0x7c00);
	if (e <= 0)
	{
		if (e < -10)
			return uint16_t(sign);
		m |= 0x800000;
		unsigned shift = unsigned(14 - e);
		uint32_t q = m >> shift, r = m & ((1u << shift) - 1), mid = 1u << (shift - 1);
		return uint16_t(sign + q + (r > mid || (r == mid && (q & 1))));
	}
	uint32_t q = m >> 13, r = m & 8191;
	return uint16_t(sign + (uint32_t(e) << 10) + q + (r > 4096 || (r == 4096 && (q & 1))));
}
float Unhalf(uint16_t h)
{
	int e = (h >> 10) & 31;
	unsigned m = h & 1023;
	double x = e ? std::ldexp(1 + m / 1024., e - 15) : std::ldexp(double(m), -24);
	if (e == 31)
		x = std::numeric_limits<double>::infinity();
	return float(h & 0x8000 ? -x : x);
}
} // namespace
Code Encode(const Field &f, Storage storage, std::vector<uint8_t> &output)
{
	if (Validate(f) != Code::Ok || unsigned(storage) > 2)
		return Code::Invalid;
	// Block quantization mixes incompatible SG axes/width/energy scales: unsupported.
	if (storage == Storage::BlockInt16 && !f.sg.empty())
		return Code::Unsupported;
	std::vector<uint8_t> b;
	Put(b, 0x3141474a);
	Put(b, kVersion);
	Put(b, unsigned(f.quantity));
	Put(b, unsigned(storage));
	Put(b, f.degree);
	Put(b, uint32_t(f.sh.size()));
	Put(b, uint32_t(f.sg.size()));
	std::vector<double> values;
	for (V c : f.sh)
		for (double v : c)
			values.push_back(v);
	for (auto l : f.sg)
	{
		for (double v : l.axis)
			values.push_back(v);
		values.push_back(l.sharpness);
		for (double v : l.energy)
			values.push_back(v);
	}
	double scale = 0;
	if (storage == Storage::BlockInt16)
	{
		for (double v : values)
			scale = std::max(scale, std::abs(v));
		scale = scale > 0 ? scale / 32767 : 1;
		float sf = float(scale);
		if (!std::isfinite(sf) || sf <= 0)
			return Code::Invalid;
		scale = sf;
		uint32_t bits;
		std::memcpy(&bits, &sf, 4);
		Put(b, bits);
	}
	for (double v : values)
	{
		float f32 = float(v);
		if (!std::isfinite(f32))
			return Code::Invalid;
		if (storage == Storage::Float32)
		{
			uint32_t u;
			std::memcpy(&u, &f32, 4);
			Put(b, u);
		}
		else
		{
			uint16_t u =
				storage == Storage::Float16
					? Half(f32)
					: uint16_t(int16_t(std::max(-32767., std::min(32767., std::round(v / scale)))));
			if (storage == Storage::Float16 && !std::isfinite(Unhalf(u)))
				return Code::Invalid;
			b.push_back(uint8_t(u));
			b.push_back(uint8_t(u >> 8));
		}
	}
	Put(b, Checksum(b, b.size()));
	output = std::move(b);
	return Code::Ok;
}
Code Decode(const std::vector<uint8_t> &b, Field &output)
{
	if (b.size() < 32 || b.size() > 8192 || Get(b, 0) != 0x3141474a || Get(b, 4) != kVersion ||
		Get(b, b.size() - 4) != Checksum(b, b.size() - 4))
		return Code::Corrupt;
	uint32_t q = Get(b, 8), s = Get(b, 12), L = Get(b, 16), nc = Get(b, 20), ng = Get(b, 24);
	if (q > 1 || s > 2 || L > 8 || ng > 8 || (nc && nc != (L + 1) * (L + 1)) || (!nc && !ng) ||
		(q && ng) || (s == 2 && ng))
		return Code::Corrupt;
	size_t pos = 28, count = nc * 3 + ng * 7;
	double scale = 1;
	if (s == 2)
	{
		if (b.size() < 36)
			return Code::Corrupt;
		uint32_t bits = Get(b, pos);
		float v;
		std::memcpy(&v, &bits, 4);
		scale = v;
		pos += 4;
		if (!std::isfinite(scale) || scale <= 0)
			return Code::Corrupt;
	}
	if (b.size() != pos + count * (s == 0 ? 4 : 2) + 4)
		return Code::Corrupt;
	auto read = [&]() -> double {
		double v;
		if (s == 0)
		{
			uint32_t bits = Get(b, pos);
			float x;
			std::memcpy(&x, &bits, 4);
			v = x;
			pos += 4;
		}
		else
		{
			uint16_t bits = uint16_t(b[pos]) | uint16_t(b[pos + 1]) << 8;
			v = s == 1 ? Unhalf(bits) : double(int16_t(bits)) * scale;
			pos += 2;
		}
		return v;
	};
	Field f;
	f.present = true;
	f.quantity = Quantity(q);
	f.degree = L;
	for (unsigned i = 0; i < nc; ++i)
	{
		V v;
		for (double &c : v)
			c = read();
		f.sh.push_back(v);
	}
	for (unsigned i = 0; i < ng; ++i)
	{
		Lobe l;
		for (double &c : l.axis)
			c = read();
		l.sharpness = read();
		for (double &c : l.energy)
			c = read();
		if (!Finite(l.axis) || std::abs(Dot(l.axis, l.axis) - 1) > .01)
			return Code::Corrupt;
		l.axis = Unit(l.axis);
		f.sg.push_back(l);
	}
	if (Validate(f) != Code::Ok)
		return Code::Corrupt;
	output = std::move(f);
	return Code::Ok;
}
} // namespace Angular
} // namespace Core
} // namespace Rend2LightRecon
