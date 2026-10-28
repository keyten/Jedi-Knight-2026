// Fern contact harness: StepFrond of tr_plantsim.cpp (extracted by run.sh into extract.inc)
// on a synthetic radial fern with a character capsule standing in it.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstdlib>
typedef int qhandle_t;
struct plantSkeleton_t;
#define PLANTSKEL_MAX_NODES 8
#define MAX_FOLIAGE_INTERACTORS 16
#define M_PI 3.14159265358979323846
#define DEG2RAD(a) ((a) * ((float)M_PI / 180.0f))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static float Com_Clamp(float lo, float hi, float v) { return v < lo ? lo : (v > hi ? hi : v); }
#include "extract.inc"

static void InitFrond(FrondSim &fr, float yaw, float total, bool grassClass)
{
	fr.segments = 7;
	fr.firstBone = 0;
	const float dx = cosf(yaw), dy = sinf(yaw);
	for (int k = 0; k <= fr.segments; ++k)
	{
		const float s = (float)k / fr.segments;
		// rises then droops, like stock ferns
		const float r = s * total * 0.9f;
		const float z = 2.0f + total * 0.45f * sinf(s * 2.4f);
		fr.X[k] = V3(dx * r, dy * r, z);
		fr.x[k] = fr.prevX[k] = fr.X[k];
		fr.v[k] = V3(0, 0, 0);
	}
	float mass[PLANTSKEL_MAX_NODES] = {};
	float sum = 0.0f;
	for (int j = 0; j < fr.segments; ++j)
	{
		fr.D[j] = fr.X[j + 1] - fr.X[j];
		fr.L[j] = MAX(Length(fr.D[j]), 1e-3f);
		sum += fr.L[j];
	}
	for (int j = 0; j < fr.segments; ++j)
	{
		fr.T[j] = fr.D[j] * (1.0f / fr.L[j]);
		const Vec3 lateral = V3(-dy, dx, 0.0f);
		fr.B[j] = Normalized(lateral - fr.T[j] * Dot(lateral, fr.T[j]), V3(1, 0, 0));
		fr.N[j] = Cross(fr.T[j], fr.B[j]);
	}
	float s = 0.0f;
	for (int j = 0; j < fr.segments; ++j)
	{
		const float sMid = (s + 0.5f * fr.L[j]) / sum;
		const float density = grassClass ? 1.0f - 0.3f * sMid : 1.0f - 0.6f * sMid;
		mass[j] += 0.5f * fr.L[j] * density;
		mass[j + 1] += 0.5f * fr.L[j] * density;
		s += fr.L[j];
	}
	fr.invMass[0] = 0.0f;
	for (int k = 1; k <= fr.segments; ++k)
		fr.invMass[k] = 1.0f / MAX(mass[k], 1e-3f);
	const float rootFreq = 4.0f, tipFreq = 1.6f;
	s = 0.0f;
	for (int j = 0; j < fr.segments; ++j)
	{
		const float sj = s / sum;
		float carried = 0.0f;
		for (int k = j + 1; k <= fr.segments; ++k)
			carried += mass[k];
		const float omega = 2.0f * (float)M_PI * (rootFreq + (tipFreq - rootFreq) * sj);
		fr.alpha[j] = 1.0f / (MAX(carried, 1e-3f) * omega * omega);
		const float e = Com_Clamp(0.0f, 1.0f, sj / 0.3f);
		fr.lateralStiffness[j] = 1.0f + 24.0f * e * e * (3.0f - 2.0f * e);
		fr.maxCos[j] = cosf(DEG2RAD(j == 0 ? 75.0f : 55.0f));
		fr.margin[j] = MIN(0.25f * 2.0f * 5.0f, 12.0f);
		s += fr.L[j];
	}
	fr.damping = 2.0f * 0.3f * 2.0f * (float)M_PI * tipFreq;
	const Vec3 chord = fr.X[fr.segments] - fr.X[0];
	const float horizontal = sqrtf(chord.x * chord.x + chord.y * chord.y);
	fr.side = V3(-chord.y / horizontal, chord.x / horizontal, 0.0f);
	fr.contactSide = 0;
	fr.contactTimer = 0.0f;
	fr.contact = false;
}

// one scenario: the capsule walks from outside to (cx, cy) in 1 s and stands
static void Run(const char *name, float cx, float cy, float radius, float sway, bool leave = false)
{
	PlantInstance inst = {};
	inst.groundZ = 1.0f;
	std::vector<FrondSim> fronds(9);
	for (size_t f = 0; f < fronds.size(); ++f)
		InitFrond(fronds[f], 2.0f * (float)M_PI * f / fronds.size() + 0.2f, 40.0f, false);
	const float h = 1.0f / 120.0f;
	double ke = 0.0, maxSpeed = 0.0;
	int samples = 0;
	const int total = 120 * 12;
	const float startX = cx - 80.0f;
	for (int step = 0; step < total; ++step)
	{
		const float t = step * h;
		const float u = MIN(t / 1.0f, 1.0f);
		Capsule cap;
		cap.radius = radius;
		// idle sway of the body (cgame capsule follows the animated model)
		const float sx = sway * sinf(t * 2.1f), sy = sway * cosf(t * 1.3f);
		float px = startX + (cx - startX) * u + sx, py = cy + sy;
		if (leave && t > 5.0f)
			px += MIN(t - 5.0f, 1.0f) * 120.0f;
		cap.a0 = V3(px, py, 0.0f + radius);
		cap.a1 = V3(px, py, 64.0f - radius);
		cap.velocity = u < 1.0f ? V3(80.0f, 0.0f, 0.0f) : V3(0, 0, 0);
		for (FrondSim &fr : fronds)
		{
			for (int k = 0; k <= fr.segments; ++k)
				fr.prevX[k] = fr.x[k];
			StepFrond(fr, inst, h, 4, &cap, 1, V3(0, 0, 0));
			fr.contact = false;
		}
		if (t > 7.0f)
		{
			for (const FrondSim &fr : fronds)
				for (int k = 1; k <= fr.segments; ++k)
				{
					const double v2 = Dot(fr.v[k], fr.v[k]);
					ke += v2;
					maxSpeed = std::max(maxSpeed, sqrt(v2));
				}
			++samples;
		}
	}
	if (getenv("FERN_DEBUG"))
		for (size_t f = 0; f < fronds.size(); ++f)
		{
			const FrondSim &fr = fronds[f];
			printf("  frond %zu:", f);
			for (int k = 1; k <= fr.segments; ++k)
				printf(" (%5.1f %5.1f %5.1f |v| %7.1f)", fr.x[k].x, fr.x[k].y, fr.x[k].z, Length(fr.v[k]));
			printf("\n");
		}
	int penetrating = 0;
	for (const FrondSim &fr : fronds)
		for (int k = 1; k <= fr.segments; ++k)
		{
			const float dx = fr.x[k].x - cx, dy = fr.x[k].y - cy;
			if (sqrtf(dx * dx + dy * dy) < radius - 1.0f && fr.x[k].z < 64.0f)
				++penetrating;
		}
	printf("%-28s rms speed %8.2f  max speed %8.2f  nodes inside %d\n", name,
		sqrt(ke / (samples * fronds.size() * 7)), maxSpeed, penetrating);
}

int main()
{
	Run("center r15", 0, 0, 15, 0);
	Run("center r15 sway 1", 0, 0, 15, 1);
	Run("offset 6 r15", 6, 3, 15, 0);
	Run("offset 12 r15 sway 1", 12, 0, 15, 1);
	Run("edge 18 r15", 18, 4, 15, 0);
	Run("far 40 r15", 40, 0, 15, 0);
	Run("far 30 r15 sway 2", 30, 5, 15, 2);
	Run("edge 18 r15 sway 2", 18, 4, 15, 2);
	Run("center r15 sway 3", 0, 0, 15, 3);
	Run("offset 4 r12 sway 2", 4, -3, 12, 2);
	Run("center leave", 0, 0, 15, 1, true);
	Run("offset 12 leave", 12, 0, 15, 1, true);
	return 0;
}
