// Headless checks for the lens water simulation core
// (shared/rd-rend2/tr_lenswater.cpp, r_rainLens). No renderer, no GL:
// scenarios of the design doc (single drop, merge, trail, roof, roll,
// heavy rain, caps) plus spray emitters, world event resolve, camera inertia
// and the acid profile. See README.md.
#include "tr_lenswater.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace lenswater;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Input Dry(Vec2 g = { 0.0f, -1.0f })
{
	Input in;
	in.intensity = 0.0f;
	in.exposed = 0.0f;
	in.gravity = g;
	return in;
}

static void Run(LensWater &w, float seconds, const Input &in, const Params &p)
{
	for (float t = 0.0f; t < seconds; t += 1.0f / 144.0f)
	{
		w.Update(1.0f / 144.0f, in, p);
		w.ClearFilmDirty();
	}
}

int main()
{
	Params p;
	p.maxDrops = 128;

	// single small drop stays pinned on vertical glass
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.2f }, 0.6f * kRefRadius, DROP_BEAD);
		Run(w, 2.0f, Dry(), p);
		CHECK(w.Drops().size() == 1 && std::fabs(w.Drops()[0].pos.y - 0.2f) < 1e-4f,
			"small bead pinned (y=%.4f)", w.Drops().empty() ? -9.0f : w.Drops()[0].pos.y);
	}
	// large drop depins and slides along gravity, trail stays in the field
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.3f }, 1.5f * kRefRadius, DROP_NORMAL);
		const float m0 = w.Drops()[0].mass;
		Run(w, 1.0f, Dry(), p);
		const Drop &d = w.Drops()[0];
		CHECK(d.pos.y < 0.25f && std::fabs(d.pos.x) < 0.05f, "large drop slides down (y=%.3f x=%.3f)", d.pos.y, d.pos.x);
		CHECK(d.mass > 0.75f * m0 - 0.3f, "moderate mass loss: %.3f -> %.3f", m0, d.mass);
		CHECK(w.Film({ d.pos.x, d.pos.y + 0.03f }) > 0.1f, "trail film behind drop (%.3f)", w.Film({ d.pos.x, d.pos.y + 0.03f }));
		CHECK(w.Wetness({ d.pos.x, d.pos.y + 0.03f }) > 0.5f, "trail wetness behind drop");
	}
	// gravity direction follows the camera roll
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.0f }, 1.5f * kRefRadius, DROP_NORMAL);
		Run(w, 0.6f, Dry({ 1.0f, 0.0f }), p);
		const Drop &d = w.Drops()[0];
		CHECK(d.pos.x > 0.05f && std::fabs(d.pos.y) < 0.03f, "rolled gravity moves drop right (x=%.3f y=%.3f)", d.pos.x, d.pos.y);
	}
	// looking up: tangential gravity small, drop stays
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.0f }, 1.3f * kRefRadius, DROP_NORMAL);
		Run(w, 1.0f, Dry({ 0.0f, -0.1f }), p);
		CHECK(std::fabs(w.Drops()[0].pos.y) < 0.01f, "near vertical view pins drop (y=%.4f)", w.Drops()[0].pos.y);
	}
	// merge: two equal pinned drops touch, r grows by cbrt(2), mass conserved, depins
	{
		LensWater w; w.Init(455, 256);
		const float r = 0.9f * kRefRadius;
		w.AddDrop({ -0.01f, 0.1f }, r, DROP_NORMAL);
		w.AddDrop({ 0.01f, 0.1f }, r, DROP_NORMAL);
		const float m0 = w.Drops()[0].mass + w.Drops()[1].mass;
		w.Update(1.0f / 60.0f + 1e-4f, Dry(), p);
		CHECK(w.Drops().size() == 1, "drops merged (%d)", (int)w.Drops().size());
		if (w.Drops().size() == 1)
		{
			const Drop &d = w.Drops()[0];
			const float expect = r * std::cbrt(2.0f);
			CHECK(std::fabs(d.radius - expect) / expect < 0.03f, "merged radius %.5f ~ %.5f (1.26r)", d.radius, expect);
			CHECK(std::fabs(d.mass - m0) / m0 < 0.03f, "mass conserved %.4f ~ %.4f", d.mass, m0);
			CHECK(d.mergeAge < 0.05f && d.lobeRadius > 0.0f, "merge lobe relaxes");
			Run(w, 1.0f, Dry(), p);
			CHECK(w.Drops()[0].pos.y < 0.07f, "merged drop depins (y=%.3f)", w.Drops()[0].pos.y);
		}
	}
	// residual beads behind a long run
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.45f }, 2.0f * kRefRadius, DROP_FLOW);
		Run(w, 2.5f, Dry(), p);
		int residuals = 0;
		for (const Drop &d : w.Drops()) residuals += d.type == DROP_RESIDUAL;
		CHECK(residuals >= 1, "residual beads left behind (%d)", residuals);
	}
	// roof: no new impacts under cover, existing water evolves, film decays
	{
		LensWater w; w.Init(455, 256);
		Input rain; rain.intensity = 1.0f; rain.exposed = 1.0f; rain.facing = 0.5f; rain.weather = PROFILE_HEAVY;
		Run(w, 6.0f, rain, p);
		const int spawnedOutside = w.spawnCount;
		const size_t dropsOutside = w.Drops().size();
		CHECK(dropsOutside > 3, "rain produced drops (%d, spawned %d)", (int)dropsOutside, spawnedOutside);
		float filmBefore = 0.0f;
		for (float y = -0.5f; y < 0.5f; y += 0.01f) for (float x = -0.8f; x < 0.8f; x += 0.01f) filmBefore += w.Film({ x, y });
		Input cover = rain; cover.exposed = 0.0f;
		int movingSeen = 0;
		for (int i = 0; i < 5 * 144; ++i) { w.Update(1.0f / 144.0f, cover, p); w.ClearFilmDirty(); movingSeen += w.GetStats().moving; }
		CHECK(movingSeen > 0, "existing drops keep moving under cover (%d)", movingSeen);
		CHECK(w.spawnCount == spawnedOutside, "no spawns under cover");
		float filmAfter = 0.0f;
		for (float y = -0.5f; y < 0.5f; y += 0.01f) for (float x = -0.8f; x < 0.8f; x += 0.01f) filmAfter += w.Film({ x, y });
		CHECK(filmAfter < filmBefore * 0.6f, "film decays under cover %.1f -> %.1f", filmBefore, filmAfter);
		CHECK(!w.Drops().empty(), "some beads remain under cover (%d)", (int)w.Drops().size());
		Run(w, 2.0f, rain, p);
		CHECK(w.spawnCount > spawnedOutside, "rain resumes on evolved state");
	}
	// profiles: heavy produces flows and sheets, not only more beads
	{
		auto measure = [&](Profile prof, int &flowsSeen, int &sheetsSeen, float &avgBeads) {
			LensWater w; w.Init(455, 256);
			Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 0.5f; in.weather = prof;
			flowsSeen = sheetsSeen = 0; avgBeads = 0.0f; int samples = 0;
			for (int i = 0; i < 144 * 20; ++i)
			{
				w.Update(1.0f / 144.0f, in, p); w.ClearFilmDirty();
				if (i % 36 == 0)
				{
					Stats s = w.GetStats();
					flowsSeen += s.flows; sheetsSeen += s.sheets;
					int big = 0;
					for (const Drop &d : w.Drops()) big += (!d.moving && d.radius > 1.0f * kRefRadius);
					avgBeads += big; ++samples;
				}
			}
			avgBeads /= samples;
		};
		int fl, sl, fn, sn, fh, sh; float bl, bn, bh;
		measure(PROFILE_LIGHT, fl, sl, bl);
		measure(PROFILE_NORMAL, fn, sn, bn);
		measure(PROFILE_HEAVY, fh, sh, bh);
		printf("     light flows %d sheets %d bigStatic %.1f | normal %d %d %.1f | heavy %d %d %.1f\n", fl, sl, bl, fn, sn, bn, fh, sh, bh);
		CHECK(fh > 3 * std::max(fn, 1) && sh > sn, "heavy has many more flows and sheets");
		CHECK(bh < bn * 2.5f + 1.0f, "heavy large static drops not proportional");
		CHECK(sl == 0, "light rain has no sheets");
	}
	// caps and emerge
	{
		LensWater w; w.Init(455, 256);
		Params small = p; small.maxDrops = 48;
		for (int i = 0; i < 10; ++i)
		{
			w.QueueEvent({ EVENT_EMERGE, 1.0f, { 0, 0 }, 0.0f });
			w.QueueEvent({ EVENT_SPLASH, 1.0f, { 1, 0 }, 0.0f });
			Run(w, 0.05f, Dry(), small);
		}
		Stats s = w.GetStats();
		CHECK(s.drops <= 48 && s.micro <= 256 && s.sheets <= 8, "caps hold (%d drops %d micro %d sheets)", s.drops, s.micro, s.sheets);
		CHECK(s.filmVisible, "emerge seeds film");
		Run(w, 40.0f, Dry(), small);
		s = w.GetStats();
		CHECK(!w.Active(), "lens dries completely (drops %d micro %d film %d)", s.drops, s.micro, (int)s.filmVisible);
	}
	// map spray emitter: continuous input, no rain, biased to its side
	{
		LensWater w; w.Init(455, 256);
		Input in = Dry();
		in.sprayStrength = 1.0f;
		in.sprayDir = { 1.0f, 0.0f };
		Run(w, 3.0f, in, p);
		float sumX = 0.0f; int n = 0;
		for (const Drop &d : w.Drops()) { sumX += d.pos.x; ++n; }
		for (const Drop &d : w.Micro()) { sumX += d.pos.x; ++n; }
		CHECK(w.spawnCount > 5 && n > 0 && sumX / n > 0.1f, "spray emitter wets its side (%d spawned, mean x %.2f)",
			w.spawnCount, n ? sumX / n : 0.0f);
		const int spawned = w.spawnCount;
		Run(w, 1.0f, Dry(), p);
		CHECK(w.spawnCount == spawned, "spray stops when the emitter is out of range");
	}
	// world events: distance falloff, facing, lens side
	{
		const float eye[3] = { 0, 0, 0 }, fwd[3] = { 1, 0, 0 }, right[3] = { 0, -1, 0 }, up[3] = { 0, 0, 1 };
		Vec2 side;
		const float ahead[3] = { 100, 0, 0 }, behind[3] = { -100, 0, 0 }, onRight[3] = { 0, -100, 0 }, far[3] = { 400, 0, 0 };
		const float sAhead = ResolveWorldEvent(ahead, 300, eye, fwd, right, up, side);
		CHECK(sAhead > 0.3f && std::fabs(side.x) < 1e-3f && std::fabs(side.y) < 1e-3f, "event ahead: %.2f, centred", sAhead);
		const float sBehind = ResolveWorldEvent(behind, 300, eye, fwd, right, up, side);
		CHECK(sBehind < sAhead * 0.5f, "event behind the camera weaker (%.2f < %.2f)", sBehind, sAhead);
		ResolveWorldEvent(onRight, 300, eye, fwd, right, up, side);
		CHECK(side.x > 0.9f, "event on the right comes from the right (%.2f)", side.x);
		CHECK(ResolveWorldEvent(far, 300, eye, fwd, right, up, side) == 0.0f, "event out of range ignored");
	}
	// camera inertia: water lags behind a strongly accelerating camera, only when enabled
	{
		for (int enabled = 0; enabled < 2; ++enabled)
		{
			LensWater w; w.Init(455, 256);
			w.AddDrop({ 0.0f, 0.0f }, 1.4f * kRefRadius, DROP_NORMAL);
			Params q = p; q.inertia = enabled ? 1.0f : 0.0f;
			Input in = Dry({ 0.0f, 0.0f });
			in.cameraAccel = { 6000.0f, 0.0f };	// strafing hard to the right
			Run(w, 0.5f, in, q);
			const float x = w.Drops()[0].pos.x;
			if (enabled)
				CHECK(x < -0.02f, "inertia: drop lags left (x=%.3f)", x);
			else
				CHECK(std::fabs(x) < 1e-3f, "no inertia by default (x=%.4f)", x);
		}
	}
	// acid profile: tint and distortion scale blend in
	{
		LensWater w; w.Init(455, 256);
		w.SetProfileOverride(PROFILE_ACID);
		Run(w, 1.0f, Dry(), p);
		w.SetProfileOverride(PROFILE_ACID);
		Run(w, 6.0f, Dry(), p);
		const LensWater::ProfileParams &c = w.Current();
		CHECK(c.tint[2] < 0.9f && c.refraction > 1.1f, "acid tint (%.2f %.2f %.2f) refraction %.2f",
			c.tint[0], c.tint[1], c.tint[2], c.refraction);
	}
	// cost at caps
	{
		LensWater w; w.Init(455, 256);
		Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_HEAVY;
		Params dense = p; dense.density = 2.0f;
		Run(w, 5.0f, in, dense);
		double total = 0.0; int n = 0;
		for (int i = 0; i < 600; ++i) { w.Update(1.0f / 60.0f, in, dense); w.ClearFilmDirty(); total += w.GetStats().updateMicroseconds; ++n; }
		Stats s = w.GetStats();
		printf("     heavy x2: %d drops %d micro %d sheets, %.1f us/update avg (agents %.1f, field %.1f)\n",
			s.drops, s.micro, s.sheets, total / n, s.agentMicroseconds, s.fieldMicroseconds);
		std::vector<float> inst(w.MaxInstances(dense) * INSTANCE_FLOATS);
		const int count = w.BuildInstances(inst.data(), w.MaxInstances(dense), 1.0f);
		CHECK(count == s.drops + s.micro + s.sheets, "instances %d", count);
	}

	printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
