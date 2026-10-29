// Standalone checks for shared/rd-rend2/G2_softlimits.h.
// Build: g++ -O2 -std=c++11 -I../../shared/rd-rend2 ragdoll_softlimit_test.cpp -o ragdoll_softlimit_test

#include "G2_softlimits.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static const g2soft::RagParams kRag = { 0.18f, 0.25f, 0.0002f, 0.80f };
static const g2soft::AnimParams kAnim = { 0.18f, 0.35f, 18.0f, 0.45f };

// one solver iteration as G2_RagDollSolve does it with r_ragSoftLimits 1
static float RagIter(float a, float lo, float hi, float step, float &w)
{
	const float s = g2soft::RagStep(a, lo, hi, step, w, kRag);
	float cur = a + s;
	cur = (a - cur) * 0.75f + cur;	// magicFactor9 blend
	cur += w;
	return g2soft::Clamp(cur, lo, hi);
}

static void TestRagConstantPush()
{
	const float lo = -45.0f, hi = 45.0f;
	for (float step = 0.5f; step <= 40.0f; step *= 2.0f)
	{
		float a = 0.0f, w = 0.0f, maxA = -1e9f;
		for (int i = 0; i < 400; i++)
		{
			a = RagIter(a, lo, hi, step, w);
			if (a > maxA) maxA = a;
		}
		CHECK(maxA < hi - 0.01f, "constant push %.1f reached the hard end (max %.3f)", step, maxA);
		CHECK(step < 1.0f || a > hi * (1.0f - 2.0f * kRag.zone), "constant push %.1f rests outside the zone (%.3f)", step, a);
		printf("rag push %5.1f deg/iter: max %.2f rest %.2f (hi %.0f)\n", step, maxA, a, hi);
	}
}

static void TestRagBounce()
{
	// a fast swing that stops pushing once it is in the zone
	const float lo = -45.0f, hi = 45.0f;
	float a = 0.0f, w = 0.0f, prev = 0.0f, maxA = -1e9f;
	int reversals = 0;
	float lastDir = 0.0f;
	for (int i = 0; i < 300; i++)
	{
		const float step = (a < 38.0f && i < 40) ? 40.0f : 0.0f;
		a = RagIter(a, lo, hi, step, w);
		const float dir = a - prev;
		if (fabsf(dir) > 1e-3f)
		{
			if (lastDir != 0.0f && dir * lastDir < 0.0f) reversals++;
			lastDir = dir;
		}
		prev = a;
		if (a > maxA) maxA = a;
	}
	CHECK(maxA < hi, "bounce reached the hard end (%.3f)", maxA);
	CHECK(reversals >= 1, "no bounce back (reversals %d)", reversals);
	CHECK(a < maxA - 1.0f, "did not come back from the end (end %.2f max %.2f)", a, maxA);
	printf("rag bounce: max %.2f final %.2f reversals %d\n", maxA, a, reversals);
}

static void TestRagNeutral()
{
	// no gradient, joint left at 30 deg: drifts toward 0 slowly
	const float lo = -45.0f, hi = 45.0f;
	float a = 30.0f, w = 0.0f;
	for (int i = 0; i < 200; i++)
	{
		a = RagIter(a, lo, hi, 0.0f, w);
	}
	CHECK(a < 29.0f && a > 15.0f, "neutral pull after 200 iterations: %.2f", a);
	printf("rag neutral: 30 -> %.2f after 200 iterations\n", a);

	// free middle with zero neutral pull: steps pass unchanged
	g2soft::RagParams rp = kRag;
	rp.neutral = 0.0f;
	float w2 = 0.0f;
	const float s = g2soft::RagStep(5.0f, lo, hi, 3.0f, w2, rp);
	CHECK(s == 3.0f && w2 == 0.0f, "free middle changed the step (%f, w %f)", s, w2);
}

static void TestKnee()
{
	const float lo = -1.0f, hi = 1.0f;
	float prev = g2soft::Knee(-1.5f, lo, hi, 0.18f);
	for (float x = -1.5f; x <= 1.5f; x += 0.001f)
	{
		const float k = g2soft::Knee(x, lo, hi, 0.18f);
		CHECK(k >= prev - 1e-6f, "knee not monotone at %f", x);
		CHECK(k > lo && k < hi, "knee left the range at %f -> %f", x, k);
		if (fabsf(x) <= 0.82f) CHECK(k == x, "knee changed the free middle at %f", x);
		prev = k;
	}
}

static void TestAnim()
{
	const float lo = -1.0f, hi = 1.0f;
	const float dt = 1.0f / 60.0f;

	// free middle: exact
	{
		g2soft::AnimAxis s = {};
		g2soft::AnimFilter(0.0f, lo, hi, s, 0.0f, true, kAnim);
		for (int i = 0; i < 600; i++)
		{
			const float t = 0.6f * sinf(i * dt * 7.0f);
			const float a = g2soft::AnimFilter(t, lo, hi, s, dt, false, kAnim);
			CHECK(a == t, "free middle changed at %d (%f vs %f)", i, a, t);
		}
	}

	// fast swing into the end, then hold beyond it
	{
		g2soft::AnimAxis s = {};
		g2soft::AnimFilter(0.0f, lo, hi, s, 0.0f, true, kAnim);
		float maxA = -1e9f, prev = 0.0f, lastDir = 0.0f, a = 0.0f;
		int reversals = 0;
		for (int i = 0; i < 240; i++)
		{
			const float t = fminf(i * dt * 8.0f, 1.05f);
			a = g2soft::AnimFilter(t, lo, hi, s, dt, false, kAnim);
			CHECK(a < hi, "anim reached the hard end at %d (%f)", i, a);
			const float dir = a - prev;
			if (fabsf(dir) > 1e-5f)
			{
				if (lastDir != 0.0f && dir * lastDir < 0.0f) reversals++;
				lastDir = dir;
			}
			prev = a;
			if (a > maxA) maxA = a;
		}
		const float rest = g2soft::Knee(1.05f, lo, hi, 0.18f);
		CHECK(reversals >= 1, "anim did not bounce");
		CHECK(fabsf(a - rest) < 1e-3f, "anim did not settle on the knee (%f vs %f)", a, rest);
		printf("anim swing: max %.4f rest %.4f reversals %d\n", maxA, a, reversals);
	}

	// dt == 0 re-evaluation does not advance the state
	{
		g2soft::AnimAxis s = {};
		g2soft::AnimFilter(0.0f, lo, hi, s, 0.0f, true, kAnim);
		for (int i = 0; i < 20; i++) g2soft::AnimFilter(i * 0.06f, lo, hi, s, dt, false, kAnim);
		const g2soft::AnimAxis before = s;
		const float a1 = g2soft::AnimFilter(1.14f, lo, hi, s, 0.0f, false, kAnim);
		const float a2 = g2soft::AnimFilter(1.14f, lo, hi, s, 0.0f, false, kAnim);
		CHECK(a1 == a2 && s.o == before.o && s.v == before.v, "dt=0 changed the state");
	}
}

static void TestMaps()
{
	srand(7);
	for (int n = 0; n < 2000; n++)
	{
		float r[3];
		for (int k = 0; k < 3; k++) r[k] = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f) * 1.7f;
		float m[3][3], r2[3];
		g2soft::ExpMap(r, m);
		g2soft::LogMap(m, r2);
		const float e = fabsf(r[0] - r2[0]) + fabsf(r[1] - r2[1]) + fabsf(r[2] - r2[2]);
		CHECK(e < 1e-3f, "log/exp round trip error %f", e);
	}
}

int main()
{
	TestRagConstantPush();
	TestRagBounce();
	TestRagNeutral();
	TestKnee();
	TestAnim();
	TestMaps();
	printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
