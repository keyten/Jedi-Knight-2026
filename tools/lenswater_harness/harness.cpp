// Headless checks for the lens water simulation core
// (shared/rd-rend2/tr_lenswater.cpp, r_rainLens). No renderer, no GL:
// scenarios of the design doc (single drop, merge, trail, roof, roll,
// heavy rain, caps) plus spray emitters, world event resolve, camera inertia,
// the acid profile, the emerge transient, film accumulation, rate controls
// and the dry / upload paths. See README.md.
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

static float Length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

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
			// standard weather: each profile at its own particle count
			Input in; in.intensity = LensWater::NominalIntensity(prof); in.exposed = 1.0f; in.facing = 0.5f; in.weather = prof;
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
		// rainlens_profile feeds the nominal intensity and exposes the lens.
		Input in = Dry(); in.intensity = LensWater::NominalIntensity(PROFILE_ACID); in.exposed = 1.0f;
		w.SetProfileOverride(PROFILE_ACID);
		Run(w, 1.0f, in, p);
		w.SetProfileOverride(PROFILE_ACID);
		Run(w, 6.0f, in, p);
		const LensWater::ProfileParams &c = w.Current();
		CHECK(c.tint[2] < 0.9f && c.refraction > 1.1f, "acid tint (%.2f %.2f %.2f) refraction %.2f",
			c.tint[0], c.tint[1], c.tint[2], c.refraction);
	}
	// cost at caps
	{
		LensWater w; w.Init(455, 256);
		Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_HEAVY;
		Params dense = p; dense.density = 2.0f; dense.measure = true;
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

	// emerge: a transient, not a burst. Film and sheets first, no beads;
	// flows follow, the film tears, residual beads come from the moving water
	{
		LensWater w; w.Init(455, 256);
		w.QueueEvent({ EVENT_EMERGE, 1.0f, { 0, 0 }, 0.0f });
		w.Update(1.0f / 60.0f + 1e-4f, Dry(), p);
		Stats s = w.GetStats();
		CHECK(s.drops == 0 && s.micro == 0, "emerge spawns no beads at t=0 (%d drops, %d micro)", s.drops, s.micro);
		CHECK(s.sheets >= 2 && s.filmVisible, "emerge starts with film and sheets (%d)", s.sheets);
		auto holes = [&]() {
			int n = 0, dry = 0;
			for (float y = -0.45f; y < 0.45f; y += 0.01f) for (float x = -0.8f; x < 0.8f; x += 0.01f) { ++n; dry += w.Film({ x, y }) < 0.12f; }
			return (float)dry / n;
		};
		const float holesStart = holes();
		CHECK(holesStart < 0.3f, "emerge film covers most of the lens (%.0f%% thin)", holesStart * 100.0f);
		int flowsSeen = 0;
		for (int i = 0; i < 144 * 3 / 2; ++i) { w.Update(1.0f / 144.0f, Dry(), p); w.ClearFilmDirty(); flowsSeen = std::max(flowsSeen, w.GetStats().flows); }
		CHECK(flowsSeen >= 2, "emerge flow heads by 1.5 s (%d at once)", flowsSeen);
		const float holesLater = holes();
		CHECK(holesLater > holesStart + 0.2f, "emerge film tears (%.0f%% -> %.0f%% thin)", holesStart * 100.0f, holesLater * 100.0f);
		Run(w, 2.0f, Dry(), p);
		int residuals = 0;
		for (const Drop &d : w.Drops()) residuals += d.type == DROP_RESIDUAL;
		CHECK(residuals >= 1, "emerge leaves residual beads behind the flows (%d)", residuals);
	}
	// film accumulates: one pass leaves a trail, repeated passes wet it more
	{
		LensWater w; w.Init(455, 256);
		float one = 0.0f, many = 0.0f;
		for (int pass = 0; pass < 5; ++pass)
		{
			w.AddDrop({ 0.0f, 0.3f }, 1.5f * kRefRadius, DROP_NORMAL);
			Run(w, 0.6f, Dry(), p);
			w.Drops().clear();
			if (pass == 0)
				one = w.Film({ 0.0f, 0.2f });
		}
		many = w.Film({ 0.0f, 0.2f });
		CHECK(one > 0.1f && one < 0.7f, "one pass: a trail, not saturated (%.2f)", one);
		CHECK(many > one * 1.3f && many <= 1.0f, "repeated passes accumulate, bounded (%.2f -> %.2f)", one, many);
	}
	// intensity scales the rate relative to the profile's nominal intensity
	{
		auto spawned = [&](float intensity) {
			LensWater w; w.Init(455, 256);
			Input in; in.intensity = intensity; in.exposed = 1.0f; in.facing = 0.5f; in.weather = PROFILE_HEAVY;
			Run(w, 10.0f, in, p);
			return w.spawnCount;
		};
		const int low = spawned(0.2f), high = spawned(1.0f);
		CHECK(low * 2 < high, "heavy at intensity 0.2 rains less than at 1.0 (%d < %d)", low, high);
	}
	// horizontal view keeps most of the heavy rain flows and sheets
	{
		// a single 30 s run is too noisy for the ratio: sum four random
		// sequences (Clear advances the generator)
		auto flows = [&](float facing, int &sheets) {
			int f = 0; sheets = 0;
			for (int seed = 0; seed < 4; ++seed)
			{
				LensWater w; w.Init(455, 256);
				for (int i = 0; i < seed; ++i) w.Clear();
				Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = facing; in.weather = PROFILE_HEAVY;
				for (int i = 0; i < 144 * 30; ++i) { w.Update(1.0f / 144.0f, in, p); w.ClearFilmDirty(); Stats s = w.GetStats(); f += s.flows; sheets += s.sheets; }
			}
			return f;
		};
		int sh0, sh1;
		const int f0 = flows(0.0f, sh0), f1 = flows(1.0f, sh1);
		CHECK(f0 > f1 * 0.4f && sh0 > sh1 * 0.45f, "horizontal heavy keeps flows %d/%d, sheets %d/%d", f0, f1, sh0, sh1);
	}
	// micro rain impacts spread before settling
	{
		LensWater w; w.Init(455, 256);
		Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_HEAVY;
		int impacts = 0;
		for (int i = 0; i < 144 * 2; ++i) { w.Update(1.0f / 144.0f, in, p); w.ClearFilmDirty(); for (const Drop &m : w.Micro()) impacts += m.state == STATE_IMPACT; }
		CHECK(impacts > 0, "micro impacts have an impact phase (%d samples)", impacts);
	}
	// film texture goes dirty on field ticks only (30 Hz upload)
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.3f }, 1.5f * kRefRadius, DROP_NORMAL);
		Run(w, 0.3f, Dry(), p);		// moving, accumulators at an unknown phase
		w.Update(0.25f, Dry(), p);	// clamped to 0.2: drains both accumulators
		w.ClearFilmDirty();
		w.Update(1.0f / 60.0f + 1e-4f, Dry(), p);	// one agent step, no field tick
		const bool afterAgent = w.FilmDirty();
		w.Update(1.0f / 60.0f + 1e-4f, Dry(), p);	// crosses the 30 Hz field tick
		CHECK(!afterAgent && w.FilmDirty(), "film dirty only on the field tick (%d, %d)", (int)afterAgent, (int)w.FilmDirty());
	}
	// dry lens: early out, no work
	{
		LensWater w; w.Init(455, 256);
		Params q = p; q.measure = true;
		Run(w, 0.1f, Dry(), q);
		const bool visible = w.Update(1.0f / 60.0f, Dry(), q);
		CHECK(!visible && w.GetStats().updateMicroseconds == 0.0f, "dry update skipped");
	}

	// invisible wetness keeps ageing while the lens sleeps (lazy decay)
	{
		LensWater w; w.Init(455, 256);
		w.AddDrop({ 0.0f, 0.3f }, 1.5f * kRefRadius, DROP_NORMAL);
		Run(w, 1.0f, Dry(), p);
		const Vec2 at = { w.Drops()[0].pos.x, w.Drops()[0].pos.y + 0.03f };
		w.Drops().clear();
		int frames = 0;
		while (w.Active() && frames < 144 * 60) { w.Update(1.0f / 144.0f, Dry(), p); w.ClearFilmDirty(); ++frames; }
		const float asleep = w.Wetness(at);
		const bool sleeping = !w.Active();
		Run(w, 30.0f, Dry(), p);		// dormant: no work
		Input rain; rain.intensity = 1.0f; rain.exposed = 1.0f;
		w.Update(1e-4f, rain, p);		// wakes: 30 s of decay at once
		const float woken = w.Wetness(at);
		CHECK(sleeping, "lens sleeps with invisible wetness (%d frames)", frames);
		CHECK(asleep > 0.05f && woken < asleep * 0.2f, "dormant wetness decays on wake (%.3f -> %.3f)", asleep, woken);
	}
	// A deferred film upload must not keep an otherwise dry lens awake.
	{
		LensWater w; w.Init(64, 36);
		w.AddDrop({ 0.0f, 0.0f }, kRefRadius, DROP_NORMAL);
		Run(w, 1.0f, Dry(), p);
		w.Drops().clear();
		for (int i = 0; i < 144 * 60 && w.FilmVisible(); ++i)
			w.Update(1.0f / 144.0f, Dry(), p);
		CHECK(w.FilmDirty() && !w.Active(), "deferred upload does not wake a dry lens");
	}
	// direct micro impacts of a downpour wet the lens
	{
		LensWater w; w.Init(455, 256);
		Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_HEAVY;
		Params q = p; q.heavyFlow = 0.0f;	// no flows / sheets: impacts only
		Run(w, 3.0f, in, q);
		int wet = 0;
		for (float y = -0.45f; y < 0.45f; y += 0.01f)
			for (float x = -0.8f; x < 0.8f; x += 0.01f)
				wet += w.Film({ x, y }) > 0.02f;
		CHECK(wet > 100, "micro impacts leave film (%d samples)", wet);
	}
	// a queued spray produces drops within the next update: the optics
	// gate must see it before any instance exists
	{
		LensWater w; w.Init(64, 36);
		Event spray = {}; spray.type = EVENT_SPRAY; spray.strength = 1.0f; spray.duration = 1.0f;
		w.QueueEvent(spray);
		CHECK(!w.HasInstances() && w.MayProduceWater(), "queued spray may produce water");
	}

	// film-first model (B): micro drops are short impacts, not beads
	{
		auto maxMicroAge = [&](int model) {
			LensWater w; w.Init(455, 256);
			Input in; in.intensity = 0.4f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_NORMAL;
			Params q = p; q.heavyFlow = 0.0f; q.filmModel = model;
			float maxAge = 0.0f;
			for (int i = 0; i < 144 * 6; ++i)
			{
				w.Update(1.0f / 144.0f, in, q);
				w.ClearFilmDirty();
				for (const Drop &m : w.Micro())
					maxAge = std::max(maxAge, m.age);
			}
			return maxAge;
		};
		const float ageA = maxMicroAge(0), ageB = maxMicroAge(1);
		const float lifeMax = LensWater::GetProfileParams(PROFILE_NORMAL).microLifeMax;
		CHECK(ageB <= lifeMax + 0.02f, "B: normal rain micro drops live <= %.1f s (%.2f s)", lifeMax, ageB);
		CHECK(ageA > 2.5f, "A: micro drops still long lived (%.2f s)", ageA);
	}
	// film-first model: an expiring micro drop leaves its water as film
	{
		auto filmAfter = [&](int model, size_t &left) {
			LensWater w; w.Init(455, 256);
			Drop m = {};
			m.pos = m.prevPos = { 0.3f, 0.1f };
			m.radius = 0.3f * kRefRadius;
			m.seed = 12345u;
			m.type = DROP_MICRO;
			m.state = STATE_SETTLED;
			m.stateAge = m.mergeAge = 1.0f;
			w.Micro().push_back(m);
			Params q = p; q.filmModel = model;
			Run(w, 2.5f, Dry(), q);
			left = w.Micro().size();
			return w.Film({ 0.3f, 0.1f });
		};
		size_t leftA, leftB;
		const float filmA = filmAfter(0, leftA), filmB = filmAfter(1, leftB);
		CHECK(leftB == 0 && filmB > 0.03f, "B: expired micro drop becomes film (%d left, film %.3f)", (int)leftB, filmB);
		CHECK(leftA == 1 && filmA < 1e-3f, "A: the micro drop is still a bead (%d left, film %.3f)", (int)leftA, filmA);
	}

	// film-first emerge: drain front, beads grow out of the film, all dries
	{
		Params q = p; q.filmModel = 1;
		int seedsOk = 0, beadsAt2 = 0, peripheral = 0, early = 0, late = 0, formingSmall = 0, formingSeen = 0;
		float maxResidualAge = 0.0f, topFilm = 0.0f, bottomFilm = 0.0f;
		for (int seed = 0; seed < 4; ++seed)
		{
			LensWater w; w.Init(455, 256);
			for (int i = 0; i < seed * 7; ++i) w.AddDrop({ 0.0f, 0.0f }, kRefRadius, DROP_NORMAL);	// shift the rng
			w.Drops().clear();
			Event e = {}; e.type = EVENT_EMERGE; e.strength = 1.0f;
			w.QueueEvent(e);
			std::vector<float> inst((size_t)w.MaxInstances(q) * INSTANCE_FLOATS);
			const int frames = 144 * 12;
			for (int f = 1; f <= frames; ++f)
			{
				w.Update(1.0f / 144.0f, Dry(), q);
				w.ClearFilmDirty();
				const float t = f / 144.0f;
				for (size_t i = 0; i < w.Drops().size(); ++i)
				{
					const Drop &d = w.Drops()[i];
					if (d.type == DROP_RESIDUAL)
						maxResidualAge = std::max(maxResidualAge, d.age);
					if (d.state == STATE_FORMING && d.stateAge < 0.1f && formingSeen < 50)
					{
						// rendered radius while it grows out of the film
						const int n = w.BuildInstances(inst.data(), w.MaxInstances(q), 1.0f);
						const size_t index = w.Sheets().size() + i;
						if ((int)index < n)
						{
							++formingSeen;
							formingSmall += inst[index * 4 + 2] < 0.6f * d.radius;
						}
					}
				}
				if (f == (int)(0.1f * 144.0f))
					early += (int)w.Drops().size();
				if (f == (int)(0.6f * 144.0f))
				{
					// thinned fraction per emerge: the islands hold a little longer
					int n = 0, top = 0, bottom = 0;
					for (float y = 0.25f; y < 0.45f; y += 0.02f)
						for (float x = -0.8f; x < 0.8f; x += 0.02f, ++n) top += w.Film({ x, y }) < 0.3f;
					for (float y = -0.45f; y < -0.25f; y += 0.02f)
						for (float x = -0.8f; x < 0.8f; x += 0.02f) bottom += w.Film({ x, y }) < 0.3f;
					topFilm += top / (float)n;
					bottomFilm += bottom / (float)n;
				}
				if (f == 2 * 144)
				{
					beadsAt2 += (int)w.Drops().size();
					for (const Drop &d : w.Drops()) peripheral += Length(d.pos) > 0.2f;
				}
				if (f == 8 * 144)
					late += (int)w.Drops().size();
			}
			++seedsOk;
		}
		printf("     film-first emerge (4 seeds): %d drops at 0.1 s, %d at 2 s (%d peripheral), %d at 8 s; thin top %.2f bottom %.2f (sum of 4) at 0.6 s; residual max age %.1f s\n",
			early, beadsAt2, peripheral, late, topFilm, bottomFilm, maxResidualAge);
		CHECK(early == 0, "B emerge: no drops at 0.1 s (%d)", early);
		CHECK(beadsAt2 >= 8 * seedsOk && beadsAt2 <= 25 * seedsOk, "B emerge: 8..25 beads per emerge at 2 s (%.1f)", beadsAt2 / (float)seedsOk);
		CHECK(peripheral >= 0.6f * beadsAt2, "B emerge: beads mostly peripheral (%d of %d)", peripheral, beadsAt2);
		CHECK(late <= 3 * seedsOk, "B emerge: dry by 8 s (%.1f drops left)", late / (float)seedsOk);
		CHECK(maxResidualAge < 10.0f, "B emerge: no residual older than 10 s (%.1f)", maxResidualAge);
		CHECK(topFilm > 4.0f * 0.5f && bottomFilm < 4.0f * 0.1f, "B emerge: drains from the top (%.0f%% thin on top, %.0f%% below at 0.6 s)", topFilm * 25.0f, bottomFilm * 25.0f);
		CHECK(formingSeen > 0 && formingSmall == formingSeen, "B emerge: beads grow out of the film (%d of %d small)", formingSmall, formingSeen);
	}
	// film-first rain: small beads, larger drops only by merging
	{
		Params q = p; q.filmModel = 1;
		LensWater w; w.Init(455, 256);
		Input in; in.intensity = 0.4f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_NORMAL;
		float maxRest = 0.0f, maxAny = 0.0f;
		for (int f = 0; f < 144 * 20; ++f)
		{
			w.Update(1.0f / 144.0f, in, q);
			w.ClearFilmDirty();
			for (const Drop &d : w.Drops())
			{
				if (d.restAge > 0.5f && d.mergeAge > 1.0f)
					maxRest = std::max(maxRest, d.radius / kRefRadius);
				maxAny = std::max(maxAny, d.radius / kRefRadius);
			}
		}
		printf("     film-first normal rain 20 s: %d drops, largest %.2f ref (resting unmerged %.2f)\n",
			(int)w.Drops().size(), maxAny, maxRest);
		CHECK(maxRest < 0.9f, "B rain: resting beads stay small (%.2f ref)", maxRest);
		CHECK(maxAny > 0.9f, "B rain: some beads grow by merging (%.2f ref)", maxAny);
	}
	// film-first sliding: speed grows with size, sticky defects stop drops
	// just above their depinning size (stick-slip), not large ones
	{
		Params q = p; q.filmModel = 1;
		auto slide = [&](float rn, int &stopped) {
			float speedSum = 0.0f;
			stopped = 0;
			for (int lane = 0; lane < 8; ++lane)
			{
				LensWater w; w.Init(455, 256);
				const uint32_t seed = w.AddDrop({ -0.7f + 0.2f * lane, 0.45f }, rn * kRefRadius, DROP_NORMAL).seed;
				float travelled = 0.0f, alive = 0.0f, still = 0.0f;
				bool moved = false, stuck = false;
				for (int f = 0; f < 144 * 3; ++f)
				{
					w.Update(1.0f / 144.0f, Dry(), q);
					w.ClearFilmDirty();
					const Drop *d = nullptr;
					for (const Drop &c : w.Drops())
						if (c.seed == seed) d = &c;
					if (!d)
						break;
					const float v = Length(d->vel);
					if (f < 144 * 3 / 2)
					{
						travelled += v / 144.0f;
						alive += 1.0f / 144.0f;
					}
					moved = moved || v > 0.04f;
					still = v < 0.005f ? still + 1.0f / 144.0f : 0.0f;
					stuck = stuck || (moved && still > 0.3f);
				}
				speedSum += alive > 0.0f ? travelled / alive : 0.0f;
				stopped += stuck;
			}
			return speedSum / 8.0f;
		};
		int stops11, stops12, stops20;
		const float v11 = slide(1.1f, stops11), v12 = slide(1.2f, stops12), v20 = slide(2.0f, stops20);
		printf("     film-first slide, 8 lanes: rn 1.1 %.3f/s (%d stuck), rn 1.2 %.3f/s (%d stuck), rn 2.0 %.3f/s (%d stuck)\n",
			v11, stops11, v12, stops12, v20, stops20);
		CHECK(v20 > 2.0f * v12, "B slide: rn 2 runs >= 2x rn 1.2 (%.3f vs %.3f lens/s)", v20, v12);
		CHECK(v20 > 0.2f && v20 < 0.5f, "B slide: rn 2 at a calm pace (%.3f lens/s)", v20);
		CHECK(stops11 + stops12 >= 1 && stops20 == 0, "B slide: stick-slip on defects (%d small, %d large stuck)", stops11 + stops12, stops20);
	}
	// film-first downpour lays down film directly
	{
		auto coverage = [&](int model) {
			Params q = p; q.filmModel = model; q.heavyFlow = 0.0f;
			LensWater w; w.Init(455, 256);
			Input in; in.intensity = 1.0f; in.exposed = 1.0f; in.facing = 1.0f; in.weather = PROFILE_HEAVY;
			Run(w, 5.0f, in, q);
			int wet = 0, n = 0;
			for (float y = -0.45f; y < 0.45f; y += 0.02f)
				for (float x = -0.8f; x < 0.8f; x += 0.02f, ++n) wet += w.Film({ x, y }) > 0.1f;
			return wet / (float)n;
		};
		const float covA = coverage(0), covB = coverage(1);
		CHECK(covB > 0.6f && covB > covA + 0.3f, "B heavy rain wets the lens (%.0f%% vs A %.0f%%)", covB * 100.0f, covA * 100.0f);
	}

	printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
