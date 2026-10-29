/*
===========================================================================
Soft joint limits for Ghoul2 ragdolls (r_ragSoftLimits) and GLA animation
(r_animSoftLimits).

Header-only and free of engine types so tools/ragdoll_softlimit_test can
compile it standalone.

A joint axis has a range [lo, hi]. The last `zone` fraction of each half of
the range is the soft zone: penetration p goes 0 -> 1 from the zone start to
the hard end. Motion into the zone is resisted, a restoring tension builds
with p^2, and the hard end is never reached.
===========================================================================
*/

#pragma once

#include <math.h>
#include <algorithm>

namespace g2soft
{

static inline float Clamp(float x, float a, float b)
{
	return x < a ? a : (x > b ? b : x);
}

// Signed penetration into the soft zone: 0 in the free middle, +-1 at the
// hard end (sign = which end). Returns 0 for degenerate ranges.
static inline float Penetration(float x, float lo, float hi, float zone)
{
	const float h = 0.5f * (hi - lo);
	if (h <= 1e-4f || zone <= 0.0f)
	{
		return 0.0f;
	}
	const float d = x - 0.5f * (lo + hi);
	const float zw = zone * h;
	const float p = Clamp((fabsf(d) - (h - zw)) / zw, 0.0f, 1.0f);
	return d < 0.0f ? -p : p;
}

// Stateless soft knee: identity in the free middle, in the zone the excess u
// maps to z*(1-exp(-u/z)) (C1 at the zone start, approaches but never reaches
// the hard end).
static inline float Knee(float x, float lo, float hi, float zone)
{
	const float h = 0.5f * (hi - lo);
	if (h <= 1e-4f || zone <= 0.0f)
	{
		return x;
	}
	const float c = 0.5f * (lo + hi);
	const float zw = zone * h;
	const float edge = h - zw;
	const float d = x - c;
	const float ad = fabsf(d);
	if (ad <= edge)
	{
		return x;
	}
	const float m = edge + zw * (1.0f - expf(-(ad - edge) / zw));
	return d < 0.0f ? c - m : c + m;
}

// Inverse of Knee inside the range (maps a kneed value back to the raw one).
static inline float KneeInv(float m, float lo, float hi, float zone)
{
	const float h = 0.5f * (hi - lo);
	if (h <= 1e-4f || zone <= 0.0f)
	{
		return m;
	}
	const float c = 0.5f * (lo + hi);
	const float zw = zone * h;
	const float edge = h - zw;
	const float d = m - c;
	const float ad = fabsf(d);
	if (ad <= edge)
	{
		return m;
	}
	const float f = Clamp((ad - edge) / zw, 0.0f, 0.999999f);
	const float u = edge - zw * logf(1.0f - f);
	return d < 0.0f ? c - u : c + u;
}

// Move from a by step with tension: outward motion inside the zone shrinks
// with (1-|p|) (the knee slope), so the hard end is never reached however big
// the step. Inward steps pass unchanged. Returns the resulting step.
static inline float KneeStep(float a, float step, float lo, float hi, float zone)
{
	const float d = a - 0.5f * (lo + hi);
	if (step * d <= 0.0f)
	{
		return step;
	}
	return Knee(KneeInv(a, lo, hi, zone) + step, lo, hi, zone) - a;
}

/*
Ragdoll axis update (one solver iteration, per-iteration units like the rest
of G2_RagDollSolve). `a` is the current angle, `step` the gradient step, `w`
the rebound velocity carried between iterations. The caller applies the
solver damping blend to (a + step) and then adds the returned w.
*/
struct RagParams
{
	float zone;			// soft zone fraction of the half range
	float stiffness;	// tension at the hard end, in zone widths per iteration^2
	float neutral;		// pull toward the range centre, per iteration^2
	float damping;		// per-iteration multiplier on w
};

static inline float RagStep(float a, float lo, float hi, float step, float &w, const RagParams &rp)
{
	const float h = 0.5f * (hi - lo);
	if (h <= 1e-4f)
	{
		w = 0.0f;
		return step;
	}
	const float d = a - 0.5f * (lo + hi);
	const float p = Penetration(a, lo, hi, rp.zone);
	const float zw = rp.zone * h;

	step = KneeStep(a, step, lo, hi, rp.zone);

	w *= rp.damping;
	w -= (p < 0.0f ? -1.0f : 1.0f) * rp.stiffness * zw * p * p;
	w -= rp.neutral * d;

	// keep one iteration of rebound from jumping across the whole range
	w = Clamp(w, -h, h);
	return step;
}

/*
Animation axis filter. State per axis: o (deviation from the knee output),
v (its velocity), prevT/prevTc (last raw / knee value). dt in seconds.
Returns the filtered angle.
*/
struct AnimParams
{
	float zone;
	float bounce;	// fraction of the velocity removed by the knee that is kept
	float omega;	// spring angular frequency, rad/s
	float zeta;		// damping ratio
};

struct AnimAxis
{
	float o, v, prevT, prevTc;
};

static inline float AnimFilter(float t, float lo, float hi, AnimAxis &s, float dt, bool reset, const AnimParams &ap)
{
	const float tc = Knee(t, lo, hi, ap.zone);
	if (reset)
	{
		s.o = s.v = 0.0f;
	}
	else if (dt > 0.0f)
	{
		const float dT = t - s.prevT;
		const float dTc = tc - s.prevTc;
		const float p = Penetration(t, lo, hi, ap.zone);
		if (p != 0.0f && dT * p > 0.0f)
		{
			// moving outward inside the zone: keep part of the momentum the
			// knee took away, the spring then carries it out and back
			s.v += (dT - dTc) / dt * ap.bounce;
		}
		const float k = ap.omega * ap.omega;
		const float c = 2.0f * ap.zeta * ap.omega;
		s.v += (-k * s.o - c * s.v) * dt;
		s.o += s.v * dt;
		if (fabsf(s.o) < 1e-6f && fabsf(s.v) < 1e-5f)
		{
			s.o = s.v = 0.0f;
		}
	}
	if (dt > 0.0f || reset)
	{
		s.prevT = t;
		s.prevTc = tc;
	}
	if (s.o == 0.0f)
	{
		return tc;
	}
	// never reach the hard end, even with the rebound
	const float h = 0.5f * (hi - lo);
	const float c = 0.5f * (lo + hi);
	const float lim = h * 0.995f;
	return c + Clamp(tc + s.o - c, -lim, lim);
}

// Rotation vector (log map) of an orthonormal 3x3 matrix m[row][col].
static inline void LogMap(const float m[3][3], float r[3])
{
	const float tr = m[0][0] + m[1][1] + m[2][2];
	const float cosA = Clamp(0.5f * (tr - 1.0f), -1.0f, 1.0f);
	const float ang = acosf(cosA);
	const float sx = m[2][1] - m[1][2];
	const float sy = m[0][2] - m[2][0];
	const float sz = m[1][0] - m[0][1];
	if (ang < 1e-4f)
	{
		r[0] = 0.5f * sx; r[1] = 0.5f * sy; r[2] = 0.5f * sz;
		return;
	}
	if (ang > 3.1415926f - 1e-3f)
	{
		// near pi: axis from the diagonal
		float ax = sqrtf(Clamp((m[0][0] + 1.0f) * 0.5f, 0.0f, 1.0f));
		float ay = sqrtf(Clamp((m[1][1] + 1.0f) * 0.5f, 0.0f, 1.0f));
		float az = sqrtf(Clamp((m[2][2] + 1.0f) * 0.5f, 0.0f, 1.0f));
		if (m[0][1] + m[1][0] < 0.0f) ay = -ay;
		if (m[0][2] + m[2][0] < 0.0f) az = -az;
		r[0] = ax * ang; r[1] = ay * ang; r[2] = az * ang;
		return;
	}
	const float f = ang / (2.0f * sinf(ang));
	r[0] = sx * f; r[1] = sy * f; r[2] = sz * f;
}

// Rodrigues: rotation matrix m[row][col] of rotation vector r.
static inline void ExpMap(const float r[3], float m[3][3])
{
	const float ang = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
	if (ang < 1e-7f)
	{
		m[0][0] = 1; m[0][1] = -r[2]; m[0][2] = r[1];
		m[1][0] = r[2]; m[1][1] = 1; m[1][2] = -r[0];
		m[2][0] = -r[1]; m[2][1] = r[0]; m[2][2] = 1;
		return;
	}
	const float x = r[0] / ang, y = r[1] / ang, z = r[2] / ang;
	const float c = cosf(ang), s = sinf(ang), t = 1.0f - c;
	m[0][0] = t * x * x + c;     m[0][1] = t * x * y - s * z; m[0][2] = t * x * z + s * y;
	m[1][0] = t * x * y + s * z; m[1][1] = t * y * y + c;     m[1][2] = t * y * z - s * x;
	m[2][0] = t * x * z - s * y; m[2][1] = t * y * z + s * x; m[2][2] = t * z * z + c;
}

// Gram-Schmidt on the rows of a 3x3 (lerped bone matrices are not orthonormal).
static inline void Orthonormalize(const float in[3][3], float out[3][3])
{
	float a[3] = { in[0][0], in[0][1], in[0][2] };
	float b[3] = { in[1][0], in[1][1], in[1][2] };
	float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	if (l < 1e-8f) l = 1.0f;
	a[0] /= l; a[1] /= l; a[2] /= l;
	const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	b[0] -= d * a[0]; b[1] -= d * a[1]; b[2] -= d * a[2];
	l = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
	if (l < 1e-8f) l = 1.0f;
	b[0] /= l; b[1] /= l; b[2] /= l;
	out[0][0] = a[0]; out[0][1] = a[1]; out[0][2] = a[2];
	out[1][0] = b[0]; out[1][1] = b[1]; out[1][2] = b[2];
	out[2][0] = a[1] * b[2] - a[2] * b[1];
	out[2][1] = a[2] * b[0] - a[0] * b[2];
	out[2][2] = a[0] * b[1] - a[1] * b[0];
}

static inline void Mul33(const float a[3][3], const float b[3][3], float o[3][3])
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			o[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
}

static inline void Transpose33(const float a[3][3], float o[3][3])
{
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			o[i][j] = a[j][i];
}

// Quaternion (w, x, y, z) of an orthonormal 3x3 matrix m[row][col].
static inline void MatToQuat(const float m[3][3], float q[4])
{
	const float tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0.0f)
	{
		const float s = sqrtf(tr + 1.0f) * 2.0f;
		q[0] = 0.25f * s;
		q[1] = (m[2][1] - m[1][2]) / s;
		q[2] = (m[0][2] - m[2][0]) / s;
		q[3] = (m[1][0] - m[0][1]) / s;
	}
	else if (m[0][0] > m[1][1] && m[0][0] > m[2][2])
	{
		const float s = sqrtf(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
		q[0] = (m[2][1] - m[1][2]) / s;
		q[1] = 0.25f * s;
		q[2] = (m[0][1] + m[1][0]) / s;
		q[3] = (m[0][2] + m[2][0]) / s;
	}
	else if (m[1][1] > m[2][2])
	{
		const float s = sqrtf(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
		q[0] = (m[0][2] - m[2][0]) / s;
		q[1] = (m[0][1] + m[1][0]) / s;
		q[2] = 0.25f * s;
		q[3] = (m[1][2] + m[2][1]) / s;
	}
	else
	{
		const float s = sqrtf(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
		q[0] = (m[1][0] - m[0][1]) / s;
		q[1] = (m[0][2] + m[2][0]) / s;
		q[2] = (m[1][2] + m[2][1]) / s;
		q[3] = 0.25f * s;
	}
}

static inline void QuatToMat(const float q[4], float m[3][3])
{
	const float w = q[0], x = q[1], y = q[2], z = q[3];
	m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - w * z);     m[0][2] = 2 * (x * z + w * y);
	m[1][0] = 2 * (x * y + w * z);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - w * x);
	m[2][0] = 2 * (x * z - w * y);     m[2][1] = 2 * (y * z + w * x);     m[2][2] = 1 - 2 * (x * x + y * y);
}

// lo/hi = the frac and 1-frac quantiles of v[0..n) (reorders v).
static inline void Percentiles(float *v, int n, float frac, float &lo, float &hi)
{
	if (n <= 0)
	{
		lo = hi = 0.0f;
		return;
	}
	const int il = (int)(frac * (n - 1));
	const int ih = (n - 1) - il;
	std::nth_element(v, v + il, v + n);
	lo = v[il];
	std::nth_element(v, v + ih, v + n);
	hi = v[ih];
}

static inline void QuatMul(const float a[4], const float b[4], float o[4])
{
	o[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
	o[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
	o[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
	o[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

// Rotation vector of a unit quaternion (same result as LogMap of its matrix).
static inline void QuatLog(const float q[4], float r[3])
{
	const float s = q[0] < 0.0f ? -1.0f : 1.0f;
	const float vx = s * q[1], vy = s * q[2], vz = s * q[3];
	const float vl = sqrtf(vx * vx + vy * vy + vz * vz);
	if (vl < 1e-7f)
	{
		r[0] = 2.0f * vx; r[1] = 2.0f * vy; r[2] = 2.0f * vz;
		return;
	}
	const float f = 2.0f * atan2f(vl, s * q[0]) / vl;
	r[0] = vx * f; r[1] = vy * f; r[2] = vz * f;
}

/*
Joint range of one bone from its unique animation poses. quats[i] are the
GLA's per-frame bone rotations (w, x, y, z, model space), B the orthonormal
bind rotation. The range is measured on the rotation vector of the local
rotation relative to the mean pose ref (ref is in the bind frame), between
the 0.5% and 99.5% quantiles, widened by margin * span. Axes that barely move
(< 2 deg) or wrap (> 300 deg), and bones with fewer than 32 poses, get no limit.
*/
struct BoneRange
{
	float ref[3][3];
	float lo[3], hi[3];
	bool axisValid[3];
	bool valid;
};

// work must hold 7 * n floats.
static inline void ScanBoneRange(const float (*quats)[4], int n, const float B[3][3], float margin, float *work, BoneRange &out)
{
	out.valid = false;
	for (int k = 0; k < 3; k++)
	{
		out.lo[k] = out.hi[k] = 0.0f;
		out.axisValid[k] = false;
	}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			out.ref[i][j] = (i == j) ? 1.0f : 0.0f;
	// too few poses (props, single-frame GLAs) say nothing about a joint range
	if (n < 32)
	{
		return;
	}

	float qB[4], qBc[4];
	MatToQuat(B, qB);
	qBc[0] = qB[0]; qBc[1] = -qB[1]; qBc[2] = -qB[2]; qBc[3] = -qB[3];

	// local rotations D = B^-1 R B
	float *d = work;
	for (int i = 0; i < n; i++)
	{
		float t[4];
		QuatMul(qBc, quats[i], t);
		QuatMul(t, qB, d + 4 * i);
	}

	// sign-aligned mean, refined once against the first estimate
	float mean[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	for (int pass = 0; pass < 2; pass++)
	{
		float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		for (int i = 0; i < n; i++)
		{
			const float *q = d + 4 * i;
			const float dot = q[0] * mean[0] + q[1] * mean[1] + q[2] * mean[2] + q[3] * mean[3];
			const float sgn = dot < 0.0f ? -1.0f : 1.0f;
			for (int k = 0; k < 4; k++) sum[k] += sgn * q[k];
		}
		const float l = sqrtf(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2] + sum[3] * sum[3]);
		if (l < 1e-6f)
		{
			break;
		}
		for (int k = 0; k < 4; k++) mean[k] = sum[k] / l;
	}
	QuatToMat(mean, out.ref);

	const float meanC[4] = { mean[0], -mean[1], -mean[2], -mean[3] };
	float *axis[3] = { work + 4 * n, work + 5 * n, work + 6 * n };
	for (int i = 0; i < n; i++)
	{
		float rel[4], r[3];
		QuatMul(meanC, d + 4 * i, rel);
		QuatLog(rel, r);
		axis[0][i] = r[0];
		axis[1][i] = r[1];
		axis[2][i] = r[2];
	}

	for (int k = 0; k < 3; k++)
	{
		float lo, hi;
		Percentiles(axis[k], n, 0.005f, lo, hi);
		const float span = hi - lo;
		if (span < 2.0f * 0.01745329f || span > 300.0f * 0.01745329f)
		{
			continue;
		}
		out.lo[k] = lo - span * margin;
		out.hi[k] = hi + span * margin;
		out.axisValid[k] = true;
		out.valid = true;
	}
}

// Decode a GLA compressed bone pool entry's rotation as a unit quaternion.
static inline void UnpackGLAQuat(const unsigned char *comp, float q[4])
{
	const unsigned short *p = (const unsigned short *)comp;
	float l = 0.0f;
	for (int k = 0; k < 4; k++)
	{
		q[k] = p[k] / 16383.0f - 2.0f;
		l += q[k] * q[k];
	}
	l = l > 1e-12f ? 1.0f / sqrtf(l) : 1.0f;
	for (int k = 0; k < 4; k++) q[k] *= l;
}

} // namespace g2soft
