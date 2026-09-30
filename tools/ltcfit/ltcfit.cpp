/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
===========================================================================

ltcfit: offline generator of the LTC (Linearly Transformed Cosines) lookup
tables of the rend2 area lights (shared/rd-rend2/tr_ltc_data.h).

Written from the method of
  E. Heitz, J. Dupuy, S. Hill, D. Neubelt, "Real-Time Polygonal-Light Shading
  with Linearly Transformed Cosines", SIGGRAPH 2016
  S. Hill, E. Heitz, "Real-Time Area Lighting: a Journey from Research to
  Production", SIGGRAPH 2016 course (horizon clipped sphere form factor)
No code or data of the authors' reference implementation is used.

BRDF fitted: GGX, isotropic, alpha = rend2 "roughness" (lightall uses it as
the GGX alpha directly), Rend2's joint Smith visibility approximation, no Fresnel
(the Fresnel split is in table 2).

Tables: LTC_SIZE x LTC_SIZE, texel (i, j):
  u (i) = sqrt(alpha)               (0 .. 1)
  v (j) = sqrt(1 - cos(theta_view))  (0 .. 1), theta_view = angle(N, V)
  table 1 RGBA: inverse LTC matrix M^-1, normalised so M^-1[1][1] = 1,
                (m[0][0], m[0][2], m[2][0], m[2][2])  ([column][row]); in GLSL
                mat3(vec3(t.x, 0, t.y), vec3(0, 1, 0), vec3(t.z, 0, t.w))
  table 2 RGBA: x = norm     = integral of brdf * cos      (F = 1)
                y = fresnel  = integral of brdf * cos * (1 - VH)^5
                z = 0
                w = horizon clipped sphere form factor / |F|, indexed by
                    u = z * 0.5 + 0.5 (z = F.z / |F|), v = |F| (0 .. 1)
                    (a separate parameterisation stored in the same texture)
Specular of a polygon = FF(M^-1 polygon) * (F0 * norm + (1 - F0) * fresnel).

Usage:
  ltcfit out.h          fit and write the header (a few minutes)
  ltcfit -test in.h     compare LTC rectangles against Monte Carlo (uses the
                        tables compiled into this binary by -DLTC_TEST_HEADER)
*/

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

static const int LTC_SIZE = 64;
static const int FIT_SAMPLES = 32;		// per axis, per sampling technique
static const double MIN_ALPHA = 1e-4;
static const double PI = 3.14159265358979323846;

struct vec3
{
	double x, y, z;
	vec3() : x(0), y(0), z(0) {}
	vec3(double a, double b, double c) : x(a), y(b), z(c) {}
	vec3 operator+(const vec3& o) const { return vec3(x + o.x, y + o.y, z + o.z); }
	vec3 operator-(const vec3& o) const { return vec3(x - o.x, y - o.y, z - o.z); }
	vec3 operator*(double s) const { return vec3(x * s, y * s, z * s); }
	vec3 operator-() const { return vec3(-x, -y, -z); }
};
static double dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 cross(const vec3& a, const vec3& b)
{
	return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static double length(const vec3& a) { return sqrt(dot(a, a)); }
static vec3 normalize(const vec3& a) { double l = length(a); return l > 0 ? a * (1.0 / l) : a; }

// column major, m[column][row]
struct mat3
{
	double m[3][3];
	vec3 operator*(const vec3& v) const
	{
		return vec3(
			m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
			m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
			m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z);
	}
	mat3 operator*(const mat3& b) const
	{
		mat3 r;
		for (int c = 0; c < 3; c++)
			for (int rr = 0; rr < 3; rr++)
				r.m[c][rr] = m[0][rr] * b.m[c][0] + m[1][rr] * b.m[c][1] + m[2][rr] * b.m[c][2];
		return r;
	}
};
static mat3 columns(const vec3& a, const vec3& b, const vec3& c)
{
	mat3 r;
	r.m[0][0] = a.x; r.m[0][1] = a.y; r.m[0][2] = a.z;
	r.m[1][0] = b.x; r.m[1][1] = b.y; r.m[1][2] = b.z;
	r.m[2][0] = c.x; r.m[2][1] = c.y; r.m[2][2] = c.z;
	return r;
}
static double det(const mat3& a)
{
	const double (*m)[3] = a.m;
	return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2])
		- m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2])
		+ m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}
static mat3 inverse(const mat3& a)
{
	const double (*m)[3] = a.m;
	const double d = 1.0 / det(a);
	mat3 r;
	r.m[0][0] = (m[1][1] * m[2][2] - m[2][1] * m[1][2]) * d;
	r.m[0][1] = (m[2][1] * m[0][2] - m[0][1] * m[2][2]) * d;
	r.m[0][2] = (m[0][1] * m[1][2] - m[1][1] * m[0][2]) * d;
	r.m[1][0] = (m[2][0] * m[1][2] - m[1][0] * m[2][2]) * d;
	r.m[1][1] = (m[0][0] * m[2][2] - m[2][0] * m[0][2]) * d;
	r.m[1][2] = (m[1][0] * m[0][2] - m[0][0] * m[1][2]) * d;
	r.m[2][0] = (m[1][0] * m[2][1] - m[2][0] * m[1][1]) * d;
	r.m[2][1] = (m[2][0] * m[0][1] - m[0][0] * m[2][1]) * d;
	r.m[2][2] = (m[0][0] * m[1][1] - m[1][0] * m[0][1]) * d;
	return r;
}

/*
GGX, isotropic, tangent space (N = +z). eval returns brdf * cos(L) and the pdf
of the visible normal sampling below.
*/
static double GgxLambda(double cosTheta, double alpha)
{
	if (cosTheta <= 0.0)
		return 0.0;
	const double tan2 = (1.0 - cosTheta * cosTheta) / (cosTheta * cosTheta);
	return 0.5 * (-1.0 + sqrt(1.0 + alpha * alpha * tan2));
}

static double GgxEval(const vec3& V, const vec3& L, double alpha, double& pdf)
{
	pdf = 0.0;
	if (V.z <= 0.0 || L.z <= 0.0)
		return 0.0;
	const vec3 H = normalize(V + L);
	const double a2 = alpha * alpha;
	const double d = H.z * H.z * (a2 - 1.0) + 1.0;
	const double D = a2 / (PI * d * d);
	const double lambdaV = GgxLambda(V.z, alpha);
	const double G1V = 1.0 / (1.0 + lambdaV);
	pdf = G1V * D / (4.0 * V.z);
	// Match lightall.glsl::V_SmithJointApprox exactly. The sampler above
	// remains valid importance sampling; only its importance distribution
	// differs slightly from this target BRDF.
	const double visibility = 0.5 / (L.z * (V.z * (1.0 - alpha) + alpha) +
		V.z * (L.z * (1.0 - alpha) + alpha));
	return D * visibility * L.z;
}

// visible normal sampling [Heitz 2018]
static vec3 GgxSample(const vec3& V, double alpha, double u1, double u2)
{
	const vec3 Vh = normalize(vec3(alpha * V.x, alpha * V.y, V.z));
	const double lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	const vec3 T1 = lensq > 0.0 ? vec3(-Vh.y, Vh.x, 0.0) * (1.0 / sqrt(lensq)) : vec3(1.0, 0.0, 0.0);
	const vec3 T2 = cross(Vh, T1);
	const double r = sqrt(u1);
	const double phi = 2.0 * PI * u2;
	const double t1 = r * cos(phi);
	double t2 = r * sin(phi);
	const double s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(std::max(0.0, 1.0 - t1 * t1)) + s * t2;
	const vec3 Nh = T1 * t1 + T2 * t2 + Vh * sqrt(std::max(0.0, 1.0 - t1 * t1 - t2 * t2));
	const vec3 Ne = normalize(vec3(alpha * Nh.x, alpha * Nh.y, std::max(0.0, Nh.z)));
	return Ne * (2.0 * dot(V, Ne)) - V;
}

struct Ltc
{
	double m11, m22, m13;
	double amplitude;
	vec3 X, Y, Z;
	mat3 M, invM;
	double detM;

	void Update()
	{
		mat3 local;
		local.m[0][0] = m11; local.m[0][1] = 0; local.m[0][2] = 0;
		local.m[1][0] = 0;   local.m[1][1] = m22; local.m[1][2] = 0;
		local.m[2][0] = m13; local.m[2][1] = 0; local.m[2][2] = 1;
		M = columns(X, Y, Z) * local;
		invM = inverse(M);
		detM = fabs(det(M));
	}

	double Eval(const vec3& L) const
	{
		const vec3 Lo = normalize(invM * L);
		const vec3 Lt = M * Lo;
		const double l = length(Lt);
		const double jacobian = detM / (l * l * l);
		const double D = std::max(0.0, Lo.z) / PI;
		return amplitude * D / jacobian;
	}

	vec3 Sample(double u1, double u2) const
	{
		const double theta = acos(sqrt(u1));
		const double phi = 2.0 * PI * u2;
		const vec3 L(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
		return normalize(M * L);
	}
};

static double FitError(const Ltc& ltc, const vec3& V, double alpha)
{
	double error = 0.0;
	for (int j = 0; j < FIT_SAMPLES; j++)
		for (int i = 0; i < FIT_SAMPLES; i++)
		{
			const double u1 = (i + 0.5) / FIT_SAMPLES;
			const double u2 = (j + 0.5) / FIT_SAMPLES;
			for (int technique = 0; technique < 2; technique++)
			{
				const vec3 L = technique == 0 ? ltc.Sample(u1, u2) : GgxSample(V, alpha, u1, u2);
				double pdfBrdf;
				const double evalBrdf = GgxEval(V, L, alpha, pdfBrdf);
				const double evalLtc = ltc.Eval(L);
				const double pdfLtc = evalLtc / ltc.amplitude;
				const double e = fabs(evalBrdf - evalLtc);
				if (pdfLtc + pdfBrdf > 0.0)
					error += e * e * e / (pdfLtc + pdfBrdf);
			}
		}
	return error / (double)(FIT_SAMPLES * FIT_SAMPLES);
}

// integrals of the BRDF (F = 1), the Schlick weight and the mean direction
static void BrdfMoments(const vec3& V, double alpha, double& norm, double& fresnel, vec3& averageDir)
{
	norm = fresnel = 0.0;
	averageDir = vec3();
	const int n = 128;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			const vec3 L = GgxSample(V, alpha, (i + 0.5) / n, (j + 0.5) / n);
			double pdf;
			const double eval = GgxEval(V, L, alpha, pdf);
			if (pdf <= 0.0)
				continue;
			const double weight = eval / pdf;
			const vec3 H = normalize(V + L);
			norm += weight;
			fresnel += weight * pow(1.0 - std::max(dot(V, H), 0.0), 5.0);
			averageDir = averageDir + L * weight;
		}
	norm /= (double)(n * n);
	fresnel /= (double)(n * n);
	averageDir.y = 0.0;
	averageDir = normalize(averageDir);
}

// Nelder-Mead over 3 parameters
template <typename F>
static void NelderMead(double *best, const double *start, double delta, double tolerance, int maxIters, F cost)
{
	const int D = 3;
	double s[D + 1][D];
	double f[D + 1];
	for (int i = 0; i < D + 1; i++)
	{
		for (int k = 0; k < D; k++)
			s[i][k] = start[k];
		if (i > 0)
			s[i][i - 1] += delta;
		f[i] = cost(s[i]);
	}

	for (int iter = 0; iter < maxIters; iter++)
	{
		int lo = 0, hi = 0, nh = 0;
		for (int i = 0; i < D + 1; i++)
		{
			if (f[i] < f[lo]) lo = i;
			if (f[i] > f[hi]) hi = i;
		}
		nh = lo;
		for (int i = 0; i < D + 1; i++)
			if (i != hi && f[i] > f[nh]) nh = i;

		if (fabs(f[hi] - f[lo]) <= tolerance * (fabs(f[lo]) + 1e-30))
			break;

		double o[D] = {};
		for (int i = 0; i < D + 1; i++)
			if (i != hi)
				for (int k = 0; k < D; k++)
					o[k] += s[i][k] / D;

		double r[D];
		for (int k = 0; k < D; k++)
			r[k] = o[k] + (o[k] - s[hi][k]);
		const double fr = cost(r);
		if (fr < f[lo])
		{
			double e[D];
			for (int k = 0; k < D; k++)
				e[k] = o[k] + 2.0 * (r[k] - o[k]);
			const double fe = cost(e);
			if (fe < fr) { memcpy(s[hi], e, sizeof(e)); f[hi] = fe; }
			else { memcpy(s[hi], r, sizeof(r)); f[hi] = fr; }
			continue;
		}
		if (fr < f[nh])
		{
			memcpy(s[hi], r, sizeof(r));
			f[hi] = fr;
			continue;
		}
		double c[D];
		for (int k = 0; k < D; k++)
			c[k] = o[k] + 0.5 * (s[hi][k] - o[k]);
		const double fc = cost(c);
		if (fc < f[hi])
		{
			memcpy(s[hi], c, sizeof(c));
			f[hi] = fc;
			continue;
		}
		for (int i = 0; i < D + 1; i++)
		{
			if (i == lo)
				continue;
			for (int k = 0; k < D; k++)
				s[i][k] = s[lo][k] + 0.5 * (s[i][k] - s[lo][k]);
			f[i] = cost(s[i]);
		}
	}

	int lo = 0;
	for (int i = 1; i < D + 1; i++)
		if (f[i] < f[lo]) lo = i;
	memcpy(best, s[lo], sizeof(double) * D);
}

static void ApplyParams(Ltc& ltc, const double *p, bool isotropic)
{
	ltc.m11 = std::max(p[0], MIN_ALPHA);
	ltc.m22 = std::max(p[1], MIN_ALPHA);
	ltc.m13 = p[2];
	if (isotropic)
	{
		ltc.m22 = ltc.m11;
		ltc.m13 = 0.0;
	}
	ltc.Update();
}

static double ViewCos(int j)
{
	const double v = (double)j / (LTC_SIZE - 1);
	return std::max(1.0 - v * v, cos(1.57));
}

static double RoughnessAlpha(int i)
{
	const double u = (double)i / (LTC_SIZE - 1);
	return std::max(u * u, MIN_ALPHA);
}

// one roughness row, from normal to grazing incidence (each fit starts from the previous)
static void FitRow(int i, float *table1, float *table2)
{
	const double alpha = RoughnessAlpha(i);
	double params[3] = { alpha, alpha, 0.0 };
	for (int j = 0; j < LTC_SIZE; j++)
	{
		const double ct = ViewCos(j);
		const vec3 V(sqrt(1.0 - ct * ct), 0.0, ct);

		double norm, fresnel;
		vec3 averageDir;
		BrdfMoments(V, alpha, norm, fresnel, averageDir);

		Ltc ltc;
		ltc.amplitude = norm;
		const bool isotropic = j == 0;
		if (isotropic)
		{
			ltc.X = vec3(1, 0, 0);
			ltc.Y = vec3(0, 1, 0);
			ltc.Z = vec3(0, 0, 1);
		}
		else
		{
			ltc.Z = averageDir;
			ltc.Y = vec3(0, 1, 0);
			ltc.X = normalize(cross(ltc.Y, ltc.Z));
		}

		double start[3] = { params[0], params[1], params[2] };
		NelderMead(params, start, 0.05, 1e-5, 200, [&](const double *p) {
			Ltc t = ltc;
			ApplyParams(t, p, isotropic);
			return FitError(t, V, alpha);
		});
		ApplyParams(ltc, params, isotropic);
		if (isotropic)
		{
			params[1] = params[0];
			params[2] = 0.0;
		}

		mat3 invM = ltc.invM;
		const double s = 1.0 / invM.m[1][1];
		for (int c = 0; c < 3; c++)
			for (int r = 0; r < 3; r++)
				invM.m[c][r] *= s;

		float *t1 = table1 + (j * LTC_SIZE + i) * 4;
		t1[0] = (float)invM.m[0][0];
		t1[1] = (float)invM.m[0][2];
		t1[2] = (float)invM.m[2][0];
		t1[3] = (float)invM.m[2][2];
		float *t2 = table2 + (j * LTC_SIZE + i) * 4;
		t2[0] = (float)norm;
		t2[1] = (float)fresnel;
	}
}

// form factor of a sphere seen under angular radius sigma (sin^2 = len), axis
// elevation cos = z, clipped by the horizon; stored divided by len
static double SphereScale(double z, double len)
{
	if (len <= 1e-6)
		return std::max(z, 0.0);
	len = std::min(len, 1.0);
	const double cosSigma = sqrt(std::max(0.0, 1.0 - len));
	const double sinAxis = sqrt(std::max(0.0, 1.0 - z * z));
	const int n = 256;
	double sum = 0.0;
	for (int a = 0; a < n; a++)
	{
		const double cosA = 1.0 - (1.0 - cosSigma) * (a + 0.5) / n;	// uniform in solid angle
		const double sinA = sqrt(std::max(0.0, 1.0 - cosA * cosA));
		for (int p = 0; p < n; p++)
		{
			const double phi = 2.0 * PI * (p + 0.5) / n;
			const double wz = cosA * z + sinA * cos(phi) * sinAxis;
			sum += std::max(0.0, wz);
		}
	}
	const double solidAngle = 2.0 * PI * (1.0 - cosSigma);
	const double ff = solidAngle * (sum / (n * n)) / PI;
	return ff / len;
}

static void WriteTable(FILE *f, const char *name, const float *t)
{
	fprintf(f, "static const float %s[LTC_LUT_SIZE * LTC_LUT_SIZE * 4] = {\n", name);
	for (int k = 0; k < LTC_SIZE * LTC_SIZE; k++)
		fprintf(f, "\t%.8g, %.8g, %.8g, %.8g,\n", t[k * 4], t[k * 4 + 1], t[k * 4 + 2], t[k * 4 + 3]);
	fprintf(f, "};\n\n");
}

static int Fit(const char *outPath)
{
	std::vector<float> table1(LTC_SIZE * LTC_SIZE * 4, 0.0f);
	std::vector<float> table2(LTC_SIZE * LTC_SIZE * 4, 0.0f);

	std::atomic<int> next(0);
	std::atomic<int> done(0);
	const unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());
	std::vector<std::thread> threads;
	for (unsigned t = 0; t < numThreads; t++)
		threads.emplace_back([&]() {
			for (int i = next++; i < LTC_SIZE; i = next++)
			{
				FitRow(i, table1.data(), table2.data());
				fprintf(stderr, "row %d/%d\n", ++done, LTC_SIZE);
			}
		});
	for (std::thread& t : threads)
		t.join();

	for (int j = 0; j < LTC_SIZE; j++)
		for (int i = 0; i < LTC_SIZE; i++)
		{
			const double z = 2.0 * i / (LTC_SIZE - 1) - 1.0;
			const double len = (double)j / (LTC_SIZE - 1);
			table2[(j * LTC_SIZE + i) * 4 + 3] = (float)SphereScale(z, len);
		}

	FILE *f = fopen(outPath, "w");
	if (!f)
	{
		fprintf(stderr, "cannot write %s\n", outPath);
		return 1;
	}
	fprintf(f,
		"// Generated by tools/ltcfit/ltcfit.cpp. Do not edit.\n"
		"// LTC lookup tables of the rend2 area lights, see tools/ltcfit/ltcfit.cpp and\n"
		"// docs/rend2-ltc-area-lights.md for the parameterisation:\n"
		"//   texel (i, j) at [(j * LTC_LUT_SIZE + i) * 4], u = i = sqrt(alpha),\n"
		"//   v = j = sqrt(1 - cos(theta_view)); table 2 .w: u = z * 0.5 + 0.5, v = |F|\n"
		"// GGX (Rend2 joint Smith approximation), fitted with %d x %d samples per technique.\n\n"
		"#pragma once\n\n"
		"#define LTC_LUT_SIZE %d\n\n",
		FIT_SAMPLES, FIT_SAMPLES, LTC_SIZE);
	WriteTable(f, "ltcTable1", table1.data());
	WriteTable(f, "ltcTable2", table2.data());
	fclose(f);
	fprintf(stderr, "wrote %s\n", outPath);
	return 0;
}

#if defined(LTC_TEST_HEADER)
#include LTC_TEST_HEADER

/*
Self test: the rectangle shading of lightall (same math, CPU) against a Monte
Carlo integration of the BRDF and of the cosine over the rectangle.
*/
static void Lut(const float *table, double u, double v, double out[4])
{
	// GL_LINEAR with clamp to edge, texel centres at (k + 0.5) / size
	const double x = std::min(std::max(u * (LTC_LUT_SIZE - 1), 0.0), (double)(LTC_LUT_SIZE - 1));
	const double y = std::min(std::max(v * (LTC_LUT_SIZE - 1), 0.0), (double)(LTC_LUT_SIZE - 1));
	const int x0 = (int)x, row0 = (int)y;
	const int x1 = std::min(x0 + 1, LTC_LUT_SIZE - 1), row1 = std::min(row0 + 1, LTC_LUT_SIZE - 1);
	const double fx = x - x0, fy = y - row0;
	for (int c = 0; c < 4; c++)
	{
		const double a = table[(row0 * LTC_LUT_SIZE + x0) * 4 + c] * (1 - fx) + table[(row0 * LTC_LUT_SIZE + x1) * 4 + c] * fx;
		const double b = table[(row1 * LTC_LUT_SIZE + x0) * 4 + c] * (1 - fx) + table[(row1 * LTC_LUT_SIZE + x1) * 4 + c] * fx;
		out[c] = a * (1 - fy) + b * fy;
	}
}

static vec3 IntegrateEdgeVec(const vec3& v1, const vec3& v2)
{
	const double x = dot(v1, v2);
	const double y = fabs(x);
	const double a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
	const double b = 3.4175940 + (4.1616724 + y) * y;
	const double v = a / b;
	const double thetaSinTheta = x > 0.0 ? v : 0.5 / sqrt(std::max(1.0 - x * x, 1e-7)) - v;
	return cross(v1, v2) * thetaSinTheta;
}

// same as LtcRectFormFactor in lightall.glsl: corners in the receiver's frame
static double RectFormFactor(const mat3& Minv, const vec3 p[4], bool twoSided)
{
	vec3 L[4];
	for (int k = 0; k < 4; k++)
		L[k] = normalize(Minv * p[k]);
	vec3 F = IntegrateEdgeVec(L[0], L[1]) + IntegrateEdgeVec(L[1], L[2]) +
		IntegrateEdgeVec(L[2], L[3]) + IntegrateEdgeVec(L[3], L[0]);
	// the rational fit of theta / sin(theta) above already includes 1 / (2 pi)
	const double len = length(F);
	if (len <= 0.0)
		return 0.0;
	double z = F.z / len;
	const bool behind = dot(p[0], cross(p[1] - p[0], p[3] - p[0])) < 0.0;
	if (behind)
		z = -z;
	if (behind && !twoSided)
		return 0.0;
	double t[4];
	Lut(ltcTable2, z * 0.5 + 0.5, len, t);
	return len * t[3];
}

static int Test()
{
	std::mt19937 rng(1234);
	std::uniform_real_distribution<double> uni(0.0, 1.0);

	double sumSpec = 0, sumDiff = 0, maxDiff = 0;
	double sumRefSpec = 0, sumRefDiff = 0;
	int cases = 0;
	for (int c = 0; c < 200; c++)
	{
		// receiver at the origin, N = +z; view in the xz plane
		const double alpha = std::max(0.02, uni(rng));
		const double ct = 0.1 + 0.9 * uni(rng);
		const vec3 V(sqrt(1 - ct * ct), 0, ct);

		// a rectangle above the receiver, facing it
		const vec3 centre(uni(rng) * 4 - 2, uni(rng) * 4 - 2, 1.0 + uni(rng) * 2.0);
		const vec3 lightN = normalize(vec3(uni(rng) - 0.5, uni(rng) - 0.5, -1.0));
		const vec3 right = normalize(cross(lightN, vec3(0.3, 1, 0.1)));
		const vec3 up = cross(right, lightN) * -1.0;		// cross(right, up) = lightN
		const double hw = 0.2 + uni(rng), hh = 0.1 + uni(rng) * 0.5;
		const vec3 R = right * hw, U = up * hh;
		const vec3 p[4] = { centre - R - U, centre - R + U, centre + R + U, centre + R - U };

		// LTC
		double t1[4], t2[4];
		const double u = sqrt(alpha), v = sqrt(1.0 - ct);
		Lut(ltcTable1, u, v, t1);
		Lut(ltcTable2, u, v, t2);
		mat3 Minv = columns(vec3(t1[0], 0, t1[1]), vec3(0, 1, 0), vec3(t1[2], 0, t1[3]));
		const double spec = RectFormFactor(Minv, p, false) * t2[0];
		mat3 I = columns(vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
		const double diff = RectFormFactor(I, p, false);

		// Monte Carlo over the rectangle area: dw = dA cos(light) / r^2
		double refSpec = 0, refDiff = 0;
		const int n = 400;
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++)
			{
				const vec3 q = centre + R * (2.0 * (i + 0.5) / n - 1.0) + U * (2.0 * (j + 0.5) / n - 1.0);
				const double r2 = dot(q, q);
				const vec3 L = q * (1.0 / sqrt(r2));
				const double cosLight = -dot(L, lightN);
				if (cosLight <= 0.0 || L.z <= 0.0)
					continue;
				const double dw = (4.0 * hw * hh / (n * n)) * cosLight / r2;
				double pdf;
				refSpec += GgxEval(V, L, alpha, pdf) * dw;
				refDiff += L.z / PI * dw;
			}

		sumSpec += fabs(spec - refSpec);
		sumRefSpec += refSpec;
		sumDiff += fabs(diff - refDiff);
		sumRefDiff += refDiff;
		maxDiff = std::max(maxDiff, fabs(diff - refDiff) / std::max(refDiff, 1e-6));
		cases++;
		if (c < 12)
			printf("alpha %.2f cos %.2f  spec %.5f ref %.5f   diffuse %.5f ref %.5f\n",
				alpha, ct, spec, refSpec, diff, refDiff);
	}
	printf("%d cases: specular mean abs error %.2f%% of the mean, diffuse %.2f%% (max relative %.2f%%)\n",
		cases, 100.0 * sumSpec / sumRefSpec, 100.0 * sumDiff / sumRefDiff, 100.0 * maxDiff);

	// normalisation: the LTC of a mirror-like BRDF at normal incidence
	double t2[4];
	Lut(ltcTable2, 0.0, 0.0, t2);
	printf("norm at alpha -> 0, normal incidence: %.4f (1 expected)\n", t2[0]);
	return 0;
}
#endif

int main(int argc, char **argv)
{
#if defined(LTC_TEST_HEADER)
	if (argc >= 2 && !strcmp(argv[1], "-test"))
		return Test();
#endif
	if (argc < 2)
	{
		fprintf(stderr, "usage: ltcfit out.h | ltcfit -test\n");
		return 1;
	}
	return Fit(argv[1]);
}
