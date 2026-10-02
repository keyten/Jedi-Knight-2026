/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Lens water simulation core, see tr_lenswater.h. No renderer dependencies.

#include "tr_lenswater.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace lenswater {

namespace {

// Pinning: a drop depins when m |g| > kPin * r * noise * wet (m, r in
// reference units), so a dry reference drop (r = kRefRadius) starts to slide
// under full tangential gravity. Moving drops keep going down to
// kKinetic * Fpin (contact angle hysteresis: stick-slip without scripting).
constexpr float kPin = 1.0f;
constexpr float kKinetic = 0.6f;
constexpr float kPinNoise = 0.25f;
constexpr float kWetPin = 0.4f;			// adhesion factor on a fully wet path
constexpr float kGravityAccel = 1.0f;	// lens units / s^2 at full excess force
constexpr float kDrag = 3.0f;
constexpr float kFlowDrag = 1.6f;
constexpr float kMaxSpeed = 1.6f;
constexpr float kPathNoise = 0.05f;		// sideways meander from surface affinity
constexpr float kWetAttract = 0.0015f;	// weak pull along the wetness gradient
constexpr float kDeposit = 0.006f;		// mass fraction lost per radius travelled
constexpr float kResidual = 5.0f;		// residual beads per lens height travelled
constexpr float kMergeFactor = 0.85f;
constexpr float kMergeRelax = 0.18f;	// seconds
constexpr float kMinRadius = 0.1f * kRefRadius;
constexpr float kMinMicroRadius = 0.08f * kRefRadius;
constexpr float kFlowPromoteSize = 1.25f;	// reference radii
constexpr float kFlowPromoteSpeed = 0.22f;
constexpr float kFlowDemoteSpeed = 0.06f;
constexpr float kNoiseFrequency = 9.0f;	// surface affinity cells per lens height

// Qualitative table of the design doc, section 37. Rates are events per
// second at density 1, full exposure and a lens facing the rain.
const LensWater::ProfileParams s_profiles[PROFILE_COUNT] =
{
	// AUTO (unused: resolved before lookup), same as NORMAL
	{ 14.0f, 3.0f, 0.35f, 0.15f, 0.05f, 0.30f, 1.00f, 1.8f, 0.95f, 1.40f,
	  0.55f, 3.0f, 15.0f, 18.0f, 1.0f, 0.45f, 1.0f, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, 2.0f,
	  4.0f, 8.0f, 0.01f },
	// LIGHT: beads, rare mergers, almost no continuous flow
	{ 6.0f, 1.2f, 0.08f, 0.0f, 0.0f, 0.30f, 0.90f, 2.2f, 0.90f, 1.30f,
	  0.35f, 2.5f, 12.0f, 30.0f, 0.8f, 0.6f, 1.0f, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.5f, 3.0f,
	  6.0f, 10.0f, 0.0f },
	// NORMAL: static beads, moving drops and thin paths together
	{ 14.0f, 3.0f, 0.35f, 0.15f, 0.05f, 0.30f, 1.00f, 1.8f, 0.95f, 1.40f,
	  0.55f, 3.0f, 15.0f, 18.0f, 1.0f, 0.45f, 1.0f, { 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, 2.0f,
	  4.0f, 8.0f, 0.01f },
	// HEAVY: turnover, film, rivulets and sheets rather than more beads
	{ 40.0f, 5.0f, 0.5f, 1.6f, 1.2f, 0.30f, 1.10f, 1.4f, 1.00f, 1.50f,
	  0.90f, 3.5f, 20.0f, 7.0f, 1.5f, 0.3f, 1.0f, { 1.0f, 1.0f, 1.0f }, 1.0f, 0.5f, 1.0f,
	  2.5f, 5.0f, 0.2f },
	// ACID: stickier, longer lasting film, slightly green-yellow and denser
	{ 14.0f, 3.0f, 0.35f, 0.15f, 0.05f, 0.30f, 1.00f, 1.8f, 0.95f, 1.40f,
	  0.60f, 5.0f, 22.0f, 20.0f, 0.9f, 0.45f, 1.25f, { 0.93f, 1.0f, 0.85f }, 1.15f, 1.2f, 2.4f,
	  5.0f, 9.0f, 0.015f },
};

// Film-first model: the rain arrives as small impacts and beads grow by
// accretion and merging; large drops only come from merges and events.
struct FilmFirstTable
{
	LensWater::ProfileParams p[PROFILE_COUNT];
	FilmFirstTable()
	{
		for (int i = 0; i < PROFILE_COUNT; ++i)
		{
			p[i] = s_profiles[i];
			p[i].microRate *= 1.3f;
			p[i].normalRate *= 0.5f;
			p[i].largeRate = 0.0f;
			p[i].sizeMin = 0.2f;
			p[i].sizeMax = 0.7f;
			p[i].sizeBeta = 2.5f;
		}
	}
};

// Rain hits a lens facing into it more often. Only the direct impacts
// follow the facing closely: film turnover, rivulets and sheets of a
// downpour stay heavy when the camera looks at the horizon.
constexpr float kImpactFacingMin = 0.2f;
constexpr float kFlowFacingMin = 0.55f;
constexpr float kSheetFacingMin = 0.65f;
constexpr float kMaxIntensityScale = 2.0f;

// Water events are not rain: their own parameters, independent of whatever
// weather profile happens to be active.
struct EventPreset
{
	float film;			// film stamp strength
	float sheetSpeed;	// lens heights / s at full tangential gravity
	float flowSpeed;	// initial flow head speed, same units
};
constexpr EventPreset kEmergePreset = { 1.0f, 1.4f, 1.0f };
constexpr EventPreset kSplashPreset = { 0.6f, 1.0f, 0.6f };
constexpr float kSprayFilm = 0.35f;
constexpr float kMicroFilm = 0.05f;	// film deposit of a direct micro impact (0.025..0.075)
// film-first model: an expiring micro drop hands its water to the film,
// this much for a 0.3 reference radii drop
constexpr float kMicroAbsorbFilm = 0.12f;
constexpr float kMicroFadeStart = 0.7f;	// of the lifetime: weight / radius fade
// film-first: a bead at rest past its lifetime dries this fast (radius / s)
constexpr float kDryRate = 1.5f * kRefRadius;
constexpr float kFormTime = 0.25f;		// a bead growing out of the film
constexpr float kResidualFormAge = 0.1f;	// residuals pinch off: a shorter growth
// Film-first emerge: the lifted film drains under gravity like Jeffreys'
// similarity solution h ~ sqrt(x / t) (x down the lens): thinner at the top,
// always thickest at the bottom, no front. Normalised so the bottom starts
// at 1; with these numbers the top tears at 0.35 s and the bottom at 3.5 s.
constexpr float kJeffreysX0 = 0.206f;	// lens heights
constexpr float kJeffreysT0 = 0.3f;		// seconds
constexpr float kRupture = 0.28f;		// film thickness where it tears
constexpr float kRuptureTrace = 0.05f, kRuptureTau = 0.2f;
constexpr float kFollowTau = 0.1f;		// the film relaxes onto the draining profile
constexpr float kEmergeDuration = 3.5f;
constexpr float kRimBand = 0.93f;		// fraction down the lens where the rim collects
constexpr float kRimBreak = 1.2f;		// seconds: the rim breaks into a row of beads
constexpr float kFlowShift = 0.04f;		// micro structure drift, lens heights per ln(time)
constexpr float kDewet = 0.2f;			// film left under a bead that formed
// film-first stick-slip: sparse sticky contact line defects. A drop just
// above its depinning size stops on one (until rain or a merge feeds it),
// a large one only slows down.
constexpr float kDefectFrequency = 40.0f;	// cells per lens height
constexpr float kDefectAmount = 1.5f;		// extra adhesion on a full defect
constexpr float kDefectThreshold = 0.62f, kDefectFull = 0.85f;	// noise range of the defects

// emerge choreography (seconds)
constexpr float kEmergeFlowStart = 0.1f, kEmergeFlowEnd = 0.5f;
constexpr float kEmergeRivuletStart = 0.4f, kEmergeRivuletEnd = 1.5f;
constexpr float kEmergeBreakup = 2.5f;	// film tearing ends
constexpr float kEmergeBreakupTau = 0.3f;

// tileable detail noise: lattice cells across the film height
constexpr int kDetailCells = 12;

// camera acceleration (world units / s^2) to lens gravity units
constexpr float kInertiaScale = 2.0e-4f;
constexpr float kMaxInertia = 1.5f;

inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }
inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Length(Vec2 a) { return std::sqrt(Dot(a, a)); }
inline float Saturate(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 Lerp(Vec2 a, Vec2 b, float t) { return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) }; }
inline float Smoothstep(float e0, float e1, float x)
{
	const float t = Saturate((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}
inline Vec2 Normalize(Vec2 a, Vec2 fallback)
{
	const float l = Length(a);
	return l > 1e-6f ? a * (1.0f / l) : fallback;
}

uint32_t Hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

float LatticeValue(int x, int y)
{
	return (Hash((uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u) & 0xffffu) * (1.0f / 65535.0f);
}

// Stable low frequency surface affinity A(p) in 0..1. Never animated in
// time: drops meander along fixed features instead of swimming.
float SurfaceAffinity(Vec2 p)
{
	const float fx = p.x * kNoiseFrequency + 1000.0f;
	const float fy = p.y * kNoiseFrequency + 1000.0f;
	const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
	float tx = fx - ix, ty = fy - iy;
	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	const float a = Lerp(LatticeValue(ix, iy), LatticeValue(ix + 1, iy), tx);
	const float b = Lerp(LatticeValue(ix, iy + 1), LatticeValue(ix + 1, iy + 1), tx);
	return Lerp(a, b, ty);
}

// value noise at lattice coordinates (fx, fy), 0..1
float ValueNoise(float fx, float fy, int salt)
{
	const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
	float tx = fx - ix, ty = fy - iy;
	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	auto at = [salt](int x, int y) { return LatticeValue(x + salt, y + 7001); };
	const float a = Lerp(at(ix, iy), at(ix + 1, iy), tx);
	const float b = Lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx);
	return Lerp(a, b, ty);
}

// Fine static contact line defects (film-first stick-slip), 0..1.
float DefectNoise(Vec2 p)
{
	return ValueNoise(p.x * kDefectFrequency + 1000.0f, p.y * kDefectFrequency + 1000.0f, 5003);
}

// Value noise on a lattice that wraps every period cells, sampled at lattice
// coordinates (x, y): tiles seamlessly, so stamps can use random offsets.
float TileableNoise(float x, float y, int periodX, int periodY)
{
	const int ix = (int)std::floor(x), iy = (int)std::floor(y);
	float tx = x - ix, ty = y - iy;
	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	auto at = [&](int cx, int cy) {
		cx = ((cx % periodX) + periodX) % periodX;
		cy = ((cy % periodY) + periodY) % periodY;
		return LatticeValue(cx + 7919, cy + 104729);
	};
	const float a = Lerp(at(ix, iy), at(ix + 1, iy), tx);
	const float b = Lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx);
	return Lerp(a, b, ty);
}

// Bounded accumulation: repeated deposits approach 1, never exceed it.
inline float Deposit(float cell, float amount)
{
	return 1.0f - (1.0f - cell) * (1.0f - Saturate(amount));
}

float ReferenceSize(const Drop &drop) { return drop.radius / kRefRadius; }

// Impact animation: rapid spread, recoil, relax (radius multiplier and
// lopsided irregularity).
void ImpactShape(const Drop &drop, float &scale, float &irregularity)
{
	const float a = drop.stateAge;
	scale = 1.0f;
	irregularity = 0.0f;
	if (drop.state == STATE_FORMING)
	{
		// grows out of the film, no splash
		scale = Lerp(0.3f, 1.0f, Smoothstep(0.0f, kFormTime, a));
		return;
	}
	if (drop.state == STATE_SETTLED || a >= 0.3f)
		return;
	if (a < 0.05f)
		scale = Lerp(0.55f, 1.25f, Smoothstep(0.0f, 0.05f, a));
	else if (a < 0.15f)
		scale = Lerp(1.25f, 0.92f, Smoothstep(0.05f, 0.15f, a));
	else
		scale = Lerp(0.92f, 1.0f, Smoothstep(0.15f, 0.3f, a));
	irregularity = 0.5f * (1.0f - a / 0.3f);
}

// film-first lifetimes: per drop from its seed, shorter near the screen centre
float SeededLifetime(uint32_t seed, Vec2 pos, float minLife, float maxLife, float centreScale)
{
	const float u = (Hash(seed ^ 0x27d4eb2du) & 0xffffu) * (1.0f / 65535.0f);
	const float centre = Lerp(centreScale, 1.0f, Smoothstep(0.1f, 0.45f, Length(pos)));
	return std::max(Lerp(minLife, maxLife, u) * centre, 0.1f);
}

int TypePriority(DropType type)
{
	switch (type)
	{
	case DROP_FLOW: return 4;
	case DROP_NORMAL: return 3;
	case DROP_BEAD: return 2;
	case DROP_RESIDUAL: return 1;
	default: return 0;
	}
}

} // namespace

const LensWater::ProfileParams &LensWater::GetProfileParams(Profile profile, int filmModel)
{
	const int index = (profile > PROFILE_AUTO && profile < PROFILE_COUNT) ? profile : PROFILE_NORMAL;
	if (filmModel == 1)
	{
		static const FilmFirstTable filmFirst;
		return filmFirst.p[index];
	}
	return s_profiles[index];
}

float LensWater::NominalIntensity(Profile profile)
{
	switch (profile)
	{
	case PROFILE_LIGHT: return 0.2f;
	case PROFILE_HEAVY: return 1.0f;
	default: return 0.4f;	// normal, acid, auto
	}
}

float LensWater::Random01()
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (rng & 0x00ffffffu) * (1.0f / 16777216.0f);
}

float LensWater::ExpRandom()
{
	return -std::log(1.0f - Random01() * 0.999999f);
}

void LensWater::Init(int width, int height)
{
	filmWidth = std::max(width, 1);
	filmHeight = std::max(height, 1);
	aspect = (float)filmWidth / (float)filmHeight;
	film.assign((size_t)filmWidth * filmHeight * 2, 0.0f);

	// static noise fields, so per drop / per cell work is a lookup
	const float h = (float)filmHeight;
	const int periodY = kDetailCells;
	const int periodX = std::max(1, (int)std::lround(kDetailCells * aspect));
	const float detailScaleX = (float)periodX / (float)filmWidth;
	const float detailScaleY = (float)periodY / (float)filmHeight;
	affinity.resize((size_t)filmWidth * filmHeight);
	detail.resize((size_t)filmWidth * filmHeight);
	defect.resize((size_t)filmWidth * filmHeight);
	for (int y = 0; y < filmHeight; ++y)
	for (int x = 0; x < filmWidth; ++x)
	{
		const size_t i = (size_t)y * filmWidth + x;
		const Vec2 p = { (x + 0.5f) / h - aspect * 0.5f, (y + 0.5f) / h - 0.5f };
		affinity[i] = SurfaceAffinity(p);
		defect[i] = DefectNoise(p);
		detail[i] = TileableNoise(x * detailScaleX, y * detailScaleY, periodX, periodY);
	}

	drops.reserve(256);
	micro.reserve(512);
	sheets.reserve(16);
	sprays.reserve(4);
	pendingEvents.reserve(8);
	processingEvents.reserve(8);
	residualSpawns.reserve(256);
	Clear();
}

void LensWater::Clear()
{
	drops.clear();
	micro.clear();
	sheets.clear();
	sprays.clear();
	pendingEvents.clear();
	std::fill(film.begin(), film.end(), 0.0f);
	filmDirty = true;
	filmVisible = false;
	filmMax = 0.0f;
	dormantTime = 0.0f;
	emerge = {};
	emergeSites.clear();
	rainFilm = 0.0f;
	filmFlow = { 0.0f, 0.0f };
	agentAccumulator = fieldAccumulator = interpolation = 0.0f;
	timerMicro = ExpRandom();
	timerNormal = ExpRandom();
	timerLarge = ExpRandom();
	timerFlow = ExpRandom();
	timerSheet = ExpRandom();
	spawnCount = 0;
}

void LensWater::QueueEvent(const Event &event)
{
	if (pendingEvents.size() < 8)
		pendingEvents.push_back(event);
}

bool LensWater::Active() const
{
	return !drops.empty() || !micro.empty() || !sheets.empty() || filmVisible
		|| !sprays.empty() || !pendingEvents.empty() || emerge.active;
}

int LensWater::MaxInstances(const Params &p) const
{
	return std::max(p.maxDrops, 1) + std::max(p.maxMicro, 0) + std::max(p.maxSheets, 0);
}

void LensWater::BlendProfile(float dt, Profile target)
{
	const ProfileParams &goal = GetProfileParams(target, params.filmModel);
	activeProfile = target;
	if (!currentValid)
	{
		current = goal;
		currentValid = true;
		return;
	}
	// weather changes crossfade over about a second
	const float k = 1.0f - std::exp(-dt);
	auto blend = [k](float &c, float g) { c += (g - c) * k; };
	ProfileParams &c = current;
	blend(c.microRate, goal.microRate);
	blend(c.normalRate, goal.normalRate);
	blend(c.largeRate, goal.largeRate);
	blend(c.flowRate, goal.flowRate);
	blend(c.sheetRate, goal.sheetRate);
	blend(c.sizeMin, goal.sizeMin);
	blend(c.sizeMax, goal.sizeMax);
	blend(c.sizeBeta, goal.sizeBeta);
	blend(c.largeMin, goal.largeMin);
	blend(c.largeMax, goal.largeMax);
	blend(c.filmDeposit, goal.filmDeposit);
	blend(c.filmTau, goal.filmTau);
	blend(c.wetTau, goal.wetTau);
	blend(c.beadLifetime, goal.beadLifetime);
	blend(c.speed, goal.speed);
	blend(c.centerSpawn, goal.centerSpawn);
	blend(c.adhesion, goal.adhesion);
	for (int i = 0; i < 3; ++i)
		blend(c.tint[i], goal.tint[i]);
	blend(c.refraction, goal.refraction);
	blend(c.microLifeMin, goal.microLifeMin);
	blend(c.microLifeMax, goal.microLifeMax);
	blend(c.beadLifeMin, goal.beadLifeMin);
	blend(c.beadLifeMax, goal.beadLifeMax);
	blend(c.filmRain, goal.filmRain);
}

bool LensWater::Update(float dt, const Input &input, const Params &p)
{
	using Clock = std::chrono::steady_clock;
	dt = std::max(dt, 0.0f);
	params = p;
	sinceEvent += dt;

	const bool incoming = (input.exposed > 0.0f && input.intensity > 0.0f && p.density > 0.0f)
		|| input.sprayStrength > 0.0f;
	if (!incoming && !Active())
	{
		// dry lens, nothing arriving: no profile blend, no steps, no timings.
		// Invisible wetness left behind keeps its age; it is decayed in one
		// go when the lens wakes up.
		if (filmMax > 0.0f)
			dormantTime += dt;
		agentAccumulator = fieldAccumulator = interpolation = 0.0f;
		rainFilm = 0.0f;
		lastUpdateMicroseconds = agentMicroseconds = fieldMicroseconds = eventMicroseconds = 0.0f;
		lastInput = input;
		return false;
	}
	if (dormantTime > 0.0f)
	{
		// exp(-t / tau) composes: one decay over the whole sleep
		DecayField(dormantTime);
		dormantTime = 0.0f;
	}

	// timings only when someone reads them (debug views, r_speeds, stats)
	const bool measure = params.measure;
	Clock::time_point mark;
	auto lap = [&](float &out) {
		if (!measure)
			return;
		const Clock::time_point now = Clock::now();
		out = std::chrono::duration<float, std::micro>(now - mark).count();
		mark = now;
	};
	const Clock::time_point start = measure ? Clock::now() : Clock::time_point();
	mark = start;

	// Camera acceleration, weak and optional, acts like extra gravity: the
	// water lags behind a strongly accelerating camera.
	Input in = input;
	if (params.inertia > 0.0f)
	{
		Vec2 inertia = in.cameraAccel * (-kInertiaScale * params.inertia);
		const float l = Length(inertia);
		if (l > kMaxInertia)
			inertia = inertia * (kMaxInertia / l);
		in.gravity = in.gravity + inertia;
	}
	lastInput = in;

	// the profile first: rain spawned this update uses the current weather,
	// events carry their own presets
	const Profile profile = profileOverride != PROFILE_AUTO ? profileOverride : in.weather;
	BlendProfile(dt, profile);
	// film-first: a downpour lays down film directly, not only through the
	// paths and impacts (facing like the flows: the turnover stays heavy)
	rainFilm = 0.0f;
	if (params.filmModel == 1 && current.filmRain > 0.0f)
	{
		const float intensityScale = std::min(std::max(in.intensity, 0.0f) / NominalIntensity(profile),
			kMaxIntensityScale);
		rainFilm = current.filmRain * std::max(params.density, 0.0f) * Saturate(in.exposed) * intensityScale
			* std::min(std::max(in.rainFlux, 0.0f), 2.0f);
	}
	ProcessEvents();
	lap(eventMicroseconds);

	agentAccumulator = std::min(agentAccumulator + dt, 0.2f);
	while (agentAccumulator >= kAgentStep)
	{
		Step(kAgentStep, in);
		agentAccumulator -= kAgentStep;
	}
	interpolation = agentAccumulator / kAgentStep;
	lap(agentMicroseconds);

	fieldAccumulator = std::min(fieldAccumulator + dt, 0.2f);
	while (fieldAccumulator >= kFieldStep)
	{
		DecayField(kFieldStep);
		fieldAccumulator -= kFieldStep;
	}
	lap(fieldMicroseconds);

	if (measure)
		lastUpdateMicroseconds = std::chrono::duration<float, std::micro>(Clock::now() - start).count();

	return Active() || incoming;
}

void LensWater::Step(float dt, const Input &input)
{
	UpdateEmerge(dt);
	Spawn(dt, input);

	for (Drop &drop : drops)
		UpdateDrop(drop, dt, input);
	drops.erase(std::remove_if(drops.begin(), drops.end(),
		[](const Drop &d) { return d.mass <= 0.0f; }), drops.end());

	MergeDrops(input);
	AbsorbMicro();

	const float evaporation = kRefRadius * 0.6f / std::max(current.beadLifetime, 0.5f);
	for (Drop &m : micro)
	{
		m.age += dt;
		m.stateAge += dt;
		if (m.state == STATE_IMPACT && m.stateAge >= 0.15f)
			m.state = STATE_SETTLING;
		if (m.state == STATE_SETTLING && m.stateAge >= 0.3f)
			m.state = STATE_SETTLED;
		m.radius -= evaporation * 1.5f * dt;
		// Film-first (B): a micro drop is a short impact, not a bead. Once
		// its lifetime is over the water joins the film and the drop is gone.
		if (params.filmModel == 1 && m.radius >= kMinMicroRadius && m.age >= MicroLifetime(m))
		{
			StampDisc(m.pos, m.radius * params.dropSize * 2.0f, 0.3f,
				kMicroAbsorbFilm * m.radius / (0.3f * kRefRadius), false);
			m.radius = 0.0f;
		}
	}
	micro.erase(std::remove_if(micro.begin(), micro.end(),
		[](const Drop &d) { return d.radius < kMinMicroRadius; }), micro.end());

	UpdateSheets(dt, input);
}

/*
Pinning. Fdrive = m |g|, Fpin = kPin r noise(p) wet(p): a small drop stays
put on tilted glass, a merge (mass ~ r^3 grows faster than r) depins it and
a wet path lets later drops follow it.
*/
void LensWater::UpdatePin(Drop &drop, const Input &input) const
{
	const float g = Length(input.gravity);
	const float rn = ReferenceSize(drop);
	const float noise = 1.0f + kPinNoise * (SampleAffinity(drop.pos) * 2.0f - 1.0f);
	const float seedAdhesion = 0.92f + 0.16f * ((drop.seed >> 8) & 0xffu) / 255.0f;
	// Film-first: the advancing contact line meets the glass ahead of a
	// moving drop, not its own fresh trail (which would lubricate it forever).
	Vec2 contact = drop.pos;
	const float speed = Length(drop.vel);
	if (params.filmModel == 1 && drop.moving && speed > 1e-4f)
		contact = contact + drop.vel * (drop.radius * params.dropSize * 1.2f / speed);
	const float wet = Saturate(SampleField(contact, 0));
	float pin = kPin * rn * noise * Lerp(1.0f, kWetPin, wet) * seedAdhesion
		* std::max(params.pinning, 0.01f) * current.adhesion;
	// film-first: sparse sticky defects catch the contact line (stick-slip)
	if (params.filmModel == 1)
		pin *= 1.0f + kDefectAmount * Smoothstep(kDefectThreshold, kDefectFull, SampleDefect(contact));
	drop.pinRatio = drop.mass * g / std::max(pin, 1e-6f);

	// a forming bead holds still; a flow head growing out of the film runs
	if (drop.state == STATE_IMPACT || (drop.state == STATE_FORMING && drop.type != DROP_FLOW))
		drop.moving = false;
	else if (!drop.moving && drop.pinRatio > 1.0f)
		drop.moving = true;
	else if (drop.moving && drop.pinRatio < kKinetic)
		drop.moving = false;
}

void LensWater::UpdateDrop(Drop &drop, float dt, const Input &input)
{
	drop.age += dt;
	drop.stateAge += dt;
	drop.mergeAge += dt;
	if (drop.state == STATE_IMPACT && drop.stateAge >= 0.15f)
		drop.state = STATE_SETTLING;
	if (drop.state == STATE_SETTLING && drop.stateAge >= 0.3f)
		drop.state = STATE_SETTLED;
	if (drop.state == STATE_FORMING && drop.stateAge >= kFormTime)
		drop.state = STATE_SETTLED;
	drop.prevPos = drop.pos;

	// evaporation: a reference drop dries in beadLifetime / 0.6
	const bool filmFirst = params.filmModel == 1;
	float evaporation = kRefRadius * 0.6f / std::max(current.beadLifetime, 0.5f);
	// film-first: a bead at rest for its lifetime dries within a second
	if (filmFirst && drop.type != DROP_FLOW && drop.restAge > RestLifetime(drop))
		evaporation = std::max(evaporation, kDryRate);
	drop.radius -= evaporation * dt;
	if (drop.radius < kMinRadius)
	{
		if (filmFirst)
			StampDisc(drop.pos, 0.6f * kRefRadius * params.dropSize, 0.3f, 0.5f * kMicroAbsorbFilm, false);
		drop.mass = 0.0f;
		return;
	}
	float rn = ReferenceSize(drop);
	drop.mass = rn * rn * rn;

	UpdatePin(drop, input);

	const float g = Length(input.gravity);
	if (drop.moving && g > 1e-4f)
	{
		const float pinForce = drop.mass * g / std::max(drop.pinRatio, 1e-6f);
		const float excess = std::max(drop.mass * g - kKinetic * pinForce, 0.0f) / drop.mass;
		Vec2 accel = input.gravity * (excess / g * kGravityAccel * current.speed);
		accel = accel + AffinityGradient(drop.pos) * (kPathNoise * g);
		accel = accel + FieldGradient(drop.pos, 0) * kWetAttract;
		drop.vel = drop.vel + accel * dt;
		// film-first: a larger drop runs faster, smaller ones creep; a flow
		// keeps only a little of its lower drag (rn 2 about 0.4 lens / s)
		const float drag = filmFirst
			? kDrag * (drop.type == DROP_FLOW ? 1.2f : 1.5f) / std::max(rn, 0.6f)
			: (drop.type == DROP_FLOW ? kFlowDrag : kDrag);
		drop.vel = drop.vel * std::exp(-drag * dt);
	}
	else
	{
		drop.vel = drop.vel * std::exp(-20.0f * dt);
	}
	float speed = Length(drop.vel);
	const float maxSpeed = kMaxSpeed * current.speed;
	if (speed > maxSpeed)
	{
		drop.vel = drop.vel * (maxSpeed / speed);
		speed = maxSpeed;
	}
	if (speed < 1e-4f)
	{
		drop.vel = { 0.0f, 0.0f };
		speed = 0.0f;
	}
	// stuck on a defect counts as rest too
	drop.restAge = speed > 0.01f ? 0.0f : drop.restAge + dt;
	drop.pos = drop.pos + drop.vel * dt;

	const float ds = speed * dt;
	if (ds > 0.0f)
	{
		// Trail: a small mass fraction becomes thin film along the swept
		// segment; after this the trail belongs to the field and only decays.
		// The coverage spreads one pass over the overlapping 60 Hz capsules,
		// so a single drop leaves filmAmount and repeated passes accumulate.
		const float deposit = 1.0f - std::exp(-kDeposit * ds / std::max(drop.radius, 1e-5f)
			* (drop.type == DROP_FLOW ? 0.7f : 1.0f));
		drop.mass *= 1.0f - deposit;
		const float filmAmount = current.filmDeposit * Saturate(0.45f + 0.4f * rn)
			* (drop.type == DROP_FLOW ? 1.4f : 1.0f);
		const float stampRadius = drop.radius * params.dropSize * 0.75f;
		StampCapsule(drop.prevPos, drop.pos, stampRadius, 1.0f, filmAmount,
			std::min(ds / std::max(2.0f * stampRadius, 1e-5f), 1.0f));

		// residual beads left behind a sliding drop
		if (rn > 0.55f && Random01() < 1.0f - std::exp(-kResidual * ds))
		{
			const float fraction = 0.02f + 0.03f * Random01();
			const float residualMass = drop.mass * fraction;
			if (std::cbrt(residualMass) >= 0.18f)
			{
				drop.mass -= residualMass;
				const float residualRadius = kRefRadius * std::cbrt(residualMass);
				// left just outside the merge contact, or it would be collected again
				const Vec2 dir = Normalize(drop.vel, { 0.0f, -1.0f });
				const Vec2 side = { -dir.y, dir.x };
				const float behind = (drop.radius + residualRadius) * params.dropSize
					* kMergeFactor * std::max(params.merge, 0.0f) * 1.15f;
				const Vec2 at = drop.pos - dir * behind
					+ side * ((Random01() - 0.5f) * drop.radius * params.dropSize * 0.6f);
				residualSpawns.push_back({ at, residualRadius });
			}
		}

		drop.radius = kRefRadius * std::cbrt(drop.mass);
		rn = ReferenceSize(drop);
		if (drop.type != DROP_FLOW && rn > kFlowPromoteSize && speed > kFlowPromoteSpeed * current.speed)
			drop.type = DROP_FLOW;
	}
	if (drop.type == DROP_FLOW && speed < kFlowDemoteSpeed)
		drop.type = DROP_NORMAL;
	if (drop.type == DROP_BEAD && rn > 0.8f)
		drop.type = DROP_NORMAL;

	const float margin = drop.radius * params.dropSize * 2.0f;
	if (std::fabs(drop.pos.x) > aspect * 0.5f + margin || std::fabs(drop.pos.y) > 0.5f + margin)
		drop.mass = 0.0f;
}

void LensWater::MergeDrops(const Input &input)
{
	for (const auto &r : residualSpawns)
	{
		Drop &d = AddDrop(r.pos, r.radius, DROP_RESIDUAL);
		if (params.filmModel == 1)
		{
			// pinches off the tail: grows rather than pops
			d.state = STATE_FORMING;
			d.stateAge = kResidualFormAge;
		}
	}
	residualSpawns.clear();

	const float scale = params.dropSize * kMergeFactor * std::max(params.merge, 0.0f);
	const size_t count = drops.size();
	for (size_t i = 0; i < count; ++i)
	{
		Drop &a = drops[i];
		if (a.mass <= 0.0f)
			continue;
		for (size_t j = i + 1; j < count; ++j)
		{
			Drop &b = drops[j];
			if (b.mass <= 0.0f)
				continue;
			const Vec2 d = a.pos - b.pos;
			const float contact = (a.radius + b.radius) * scale;
			if (Dot(d, d) >= contact * contact)
				continue;

			Drop &keep = a.mass >= b.mass ? a : b;
			Drop &gone = a.mass >= b.mass ? b : a;
			const float mass = a.mass + b.mass;
			const float wa = a.mass / mass, wb = b.mass / mass;
			const Vec2 pos = a.pos * wa + b.pos * wb;
			const Vec2 prevPos = a.prevPos * wa + b.prevPos * wb;
			const Vec2 vel = a.vel * wa + b.vel * wb;

			// render the two caps relaxing into one instead of popping
			keep.mainOffset = keep.pos - pos;
			keep.lobeOffset = gone.pos - pos;
			keep.lobeRadius = gone.radius;
			keep.mergeAge = 0.0f;
			keep.restAge = 0.0f;
			keep.pos = pos;
			keep.prevPos = prevPos;
			keep.vel = vel;
			keep.mass = mass;
			keep.radius = kRefRadius * std::cbrt(mass);
			keep.moving = keep.moving || gone.moving;
			if (TypePriority(gone.type) > TypePriority(keep.type))
				keep.type = gone.type;
			if ((keep.type == DROP_BEAD || keep.type == DROP_RESIDUAL) && ReferenceSize(keep) > 0.8f)
				keep.type = DROP_NORMAL;
			if (gone.state == STATE_SETTLED && keep.state != STATE_SETTLED)
			{
				keep.state = STATE_SETTLED;
				keep.stateAge = 1.0f;
			}
			gone.mass = 0.0f;
			UpdatePin(keep, input);
			if (&keep == &b)
				break;	// a is gone
		}
	}
	drops.erase(std::remove_if(drops.begin(), drops.end(),
		[](const Drop &d) { return d.mass <= 0.0f; }), drops.end());
	Evict();
}

// Moving drops and flows collect the micro drops they cross.
void LensWater::AbsorbMicro()
{
	if (micro.empty())
		return;
	for (Drop &drop : drops)
	{
		if (!drop.moving && drop.type != DROP_FLOW)
			continue;
		const float reach = drop.radius * (drop.type == DROP_FLOW ? 1.4f : 1.0f);
		for (Drop &m : micro)
		{
			if (m.radius <= 0.0f)
				continue;
			const Vec2 d = drop.pos - m.pos;
			const float contact = (reach + m.radius) * params.dropSize;
			if (Dot(d, d) < contact * contact)
			{
				const float mr = m.radius / kRefRadius;
				drop.mass += mr * mr * mr;
				drop.radius = kRefRadius * std::cbrt(drop.mass);
				m.radius = 0.0f;
			}
		}
	}
	micro.erase(std::remove_if(micro.begin(), micro.end(),
		[](const Drop &d) { return d.radius <= 0.0f; }), micro.end());
}

float LensWater::Importance(const Drop &drop) const
{
	const float peripheral = Smoothstep(0.1f, 0.5f, Length(drop.pos));
	float importance = 2.0f * ReferenceSize(drop) + 3.0f * Length(drop.vel)
		+ std::exp(-drop.age / 3.0f) + 0.5f * peripheral;
	if (drop.type == DROP_FLOW)
		importance += 5.0f;
	if (drop.state != STATE_SETTLED)
		importance += 1.0f;
	return importance;
}

// Film-first model: a micro drop's lifetime, per drop from its seed and
// shorter near the screen centre.
float LensWater::MicroLifetime(const Drop &m) const
{
	return SeededLifetime(m.seed, m.pos, current.microLifeMin, current.microLifeMax, 0.6f);
}

// Film-first model: how long a bead may rest before it dries; bigger beads
// last a little longer, the screen centre clears twice as fast.
float LensWater::RestLifetime(const Drop &drop) const
{
	const float size = std::min(std::max(0.7f + 0.3f * ReferenceSize(drop), 0.7f), 1.5f);
	return SeededLifetime(drop.seed, drop.pos, current.beadLifeMin, current.beadLifeMax, 0.5f) * size;
}

// Hard caps: evict the least visually valuable state (tiny old beads first).
void LensWater::Evict()
{
	const size_t maxDrops = (size_t)std::max(params.maxDrops, 1);
	while (drops.size() > maxDrops)
	{
		size_t worst = 0;
		float worstScore = 1e30f;
		for (size_t i = 0; i < drops.size(); ++i)
		{
			const float score = Importance(drops[i]);
			if (score < worstScore)
			{
				worstScore = score;
				worst = i;
			}
		}
		drops[worst] = drops.back();
		drops.pop_back();
	}
}

Drop &LensWater::AddDrop(Vec2 pos, float radius, DropType type)
{
	Drop drop = {};
	drop.pos = drop.prevPos = pos;
	drop.radius = std::max(radius, kMinRadius);
	const float rn = drop.radius / kRefRadius;
	drop.mass = rn * rn * rn;
	drop.mergeAge = 1.0f;
	drop.stateAge = 1.0f;
	drop.seed = Hash(seedCounter++ ^ 0x9e3779b9u);
	drop.type = type;
	drop.state = STATE_SETTLED;
	drops.push_back(drop);
	return drops.back();
}

Vec2 LensWater::RandomPosition(bool obstructive)
{
	Vec2 p = { 0.0f, 0.0f };
	float centre = 1.0f - (1.0f - current.centerSpawn) * std::max(params.peripheralBias, 0.0f);
	centre = std::max(centre, 0.05f);
	for (int attempt = 0; attempt < 6; ++attempt)
	{
		p = { (Random01() - 0.5f) * aspect, Random01() - 0.5f };
		if (!obstructive)
			return p;
		// keep large, long lived water away from the screen centre
		const float weight = Lerp(centre, 1.0f, Smoothstep(0.12f, 0.5f, Length(p)));
		if (Random01() < weight)
			return p;
	}
	return p;
}

Vec2 LensWater::SideBiasedPosition(Vec2 dir, float spread)
{
	if (Dot(dir, dir) < 1e-6f)
		return RandomPosition(false);
	const Vec2 d = Normalize(dir, { 0.0f, 0.0f });
	Vec2 p = { d.x * aspect * 0.3f, d.y * 0.3f };
	p = p + Vec2{ (Random01() - 0.5f) * 2.0f * spread, (Random01() - 0.5f) * 2.0f * spread };
	p.x = std::max(-aspect * 0.5f, std::min(aspect * 0.5f, p.x));
	p.y = std::max(-0.5f, std::min(0.5f, p.y));
	return p;
}

// impact: a direct rain hit spreads, recoils and settles (satellites of a
// larger impact are thrown droplets and land settled)
void LensWater::SpawnMicro(Vec2 pos, float radius, bool impact)
{
	// an impact on an existing drop feeds it (film-first: a wider catch, the
	// beads grow by accretion)
	const float catchScale = params.filmModel == 1 ? 1.5f : 1.0f;
	for (Drop &drop : drops)
	{
		const Vec2 d = drop.pos - pos;
		const float contact = drop.radius * params.dropSize * catchScale;
		if (Dot(d, d) < contact * contact)
		{
			const float mr = radius / kRefRadius;
			drop.mass += mr * mr * mr;
			drop.radius = kRefRadius * std::cbrt(drop.mass);
			return;
		}
	}
	if ((int)micro.size() >= std::max(params.maxMicro, 0))
	{
		if (micro.empty())
			return;
		// replace the oldest
		size_t oldest = 0;
		for (size_t i = 1; i < micro.size(); ++i)
			if (micro[i].age > micro[oldest].age)
				oldest = i;
		micro[oldest] = micro.back();
		micro.pop_back();
	}
	Drop m = {};
	m.pos = m.prevPos = pos;
	m.radius = radius;
	m.seed = Hash(seedCounter++ ^ 0x85ebca6bu);
	m.type = DROP_MICRO;
	m.state = impact ? STATE_IMPACT : STATE_SETTLED;
	m.stateAge = impact ? 0.0f : 1.0f;
	m.mergeAge = 1.0f;
	micro.push_back(m);
}

void LensWater::SpawnRainDrop(float rn, bool large, const Vec2 *center, float spread)
{
	Vec2 pos;
	if (center)
	{
		const float angle = Random01() * 6.2831853f;
		const float dist = std::sqrt(Random01()) * spread;
		pos = *center + Vec2{ std::cos(angle) * dist, std::sin(angle) * dist };
	}
	else
	{
		pos = RandomPosition(large);
	}
	Drop &drop = AddDrop(pos, rn * kRefRadius, rn < 0.7f ? DROP_BEAD : DROP_NORMAL);
	drop.state = STATE_IMPACT;
	drop.stateAge = 0.0f;
	const float radius = drop.radius;
	++spawnCount;

	// large impacts throw a few satellites
	if (large || rn > 1.1f)
	{
		const int satellites = 2 + (int)(Random01() * 3.0f);
		for (int i = 0; i < satellites; ++i)
		{
			const float angle = Random01() * 6.2831853f;
			const float dist = radius * params.dropSize * (1.6f + Random01());
			SpawnMicro(pos + Vec2{ std::cos(angle) * dist, std::sin(angle) * dist },
				kRefRadius * (0.12f + 0.15f * Random01()), false);
		}
	}
	Evict();
}

void LensWater::SpawnFlow(Vec2 pos, float rn, Vec2 vel, float filmAmount)
{
	Drop &drop = AddDrop(pos, rn * kRefRadius, DROP_FLOW);
	drop.moving = true;
	drop.vel = vel;
	drop.state = params.filmModel == 1 ? STATE_FORMING : STATE_SETTLING;
	drop.stateAge = params.filmModel == 1 ? 0.0f : 0.15f;
	StampDisc(pos, drop.radius * params.dropSize * 1.5f, 1.0f, filmAmount, false);
	++spawnCount;
	Evict();
}

void LensWater::SpawnSheet(Vec2 pos, float strength, float scale, float speed, float filmAmount)
{
	const size_t maxSheets = (size_t)std::max(params.maxSheets, 0);
	if (maxSheets == 0)
		return;
	if (sheets.size() >= maxSheets)
	{
		size_t oldest = 0;
		for (size_t i = 1; i < sheets.size(); ++i)
			if (sheets[i].age / sheets[i].lifetime > sheets[oldest].age / sheets[oldest].lifetime)
				oldest = i;
		sheets[oldest] = sheets.back();
		sheets.pop_back();
	}
	const Vec2 &g = lastInput.gravity;
	Vec2 dir = g + lastInput.wind * 0.3f;
	if (Length(g) < 0.15f)
	{
		// looking up or down: no dominant run-off direction
		const float angle = Random01() * 6.2831853f;
		dir = dir + Vec2{ std::cos(angle), std::sin(angle) } * 0.3f;
	}
	dir = Normalize(dir, { 0.0f, -1.0f });

	Sheet sheet = {};
	sheet.dir = dir;
	sheet.width = (0.08f + 0.10f * Random01()) * scale;
	sheet.length = (0.18f + 0.22f * Random01()) * scale;
	sheet.strength = strength;
	sheet.speed = (0.7f + 0.7f * Random01()) * speed * std::max(Length(g), 0.3f);
	sheet.film = filmAmount;
	sheet.lifetime = 0.25f + 0.55f * Random01();
	sheet.seed = Hash(seedCounter++ ^ 0xc2b2ae35u);
	sheet.pos = sheet.prevPos = pos - dir * (sheet.length * 0.5f);
	sheets.push_back(sheet);
	++spawnCount;
}

void LensWater::UpdateSheets(float dt, const Input &)
{
	for (Sheet &sheet : sheets)
	{
		sheet.age += dt;
		sheet.prevPos = sheet.pos;
		sheet.pos = sheet.pos + sheet.dir * (sheet.speed * dt);
		const float stampRadius = sheet.width * params.dropSize * 0.45f;
		const float ds = sheet.speed * dt;
		StampCapsule(sheet.prevPos, sheet.pos, stampRadius, 1.0f, 0.45f * sheet.strength * sheet.film,
			std::min(ds / std::max(2.0f * stampRadius, 1e-5f), 1.0f));
	}
	sheets.erase(std::remove_if(sheets.begin(), sheets.end(),
		[](const Sheet &s) { return s.age >= s.lifetime; }), sheets.end());
}

void LensWater::Spawn(float dt, const Input &input)
{
	// Direct impacts follow the facing closely, flows and sheets much less
	// (see kImpactFacingMin). The intensity scales the rates relative to
	// the particle count the profile is tuned for, so the standard weather
	// presets are unchanged and custom rain counts.
	// The film-first model takes the rain flux onto the lens instead: no
	// rain looking down or level while standing, more running into it.
	const bool filmFirst = params.filmModel == 1;
	const float flux = std::min(std::max(input.rainFlux, 0.0f), 2.0f);
	const float f = Saturate(input.facing);
	const float impactFacing = filmFirst ? flux : Lerp(kImpactFacingMin, 1.0f, std::pow(f, 1.5f));
	const float flowFacing = filmFirst ? flux : Lerp(kFlowFacingMin, 1.0f, f);
	const float sheetFacing = filmFirst ? flux : Lerp(kSheetFacingMin, 1.0f, f);
	// rain driven into the lens hits harder: slightly larger impacts
	const float impactSize = filmFirst ? Lerp(1.0f, 1.2f, Smoothstep(0.5f, 1.5f, flux)) : 1.0f;
	const Profile profile = profileOverride != PROFILE_AUTO ? profileOverride : input.weather;
	const float intensityScale = std::min(std::max(input.intensity, 0.0f) / NominalIntensity(profile),
		kMaxIntensityScale);
	const float rate = std::max(params.density, 0.0f) * Saturate(input.exposed) * intensityScale;

	// Poisson timers: a unit exponential variate consumed by rate * dt
	auto fire = [&](float &timer, float eventsPerSecond, auto &&spawn)
	{
		if (eventsPerSecond <= 0.0f)
			return;
		timer -= eventsPerSecond * dt;
		for (int guard = 0; timer <= 0.0f && guard < 16; ++guard)
		{
			spawn();
			timer += ExpRandom();
		}
		timer = std::max(timer, 0.0f);
	};

	if (rate > 0.0f)
	{
		const float heavyFlow = std::max(params.heavyFlow, 0.0f);
		fire(timerMicro, current.microRate * rate * impactFacing, [&]() {
			const Vec2 pos = RandomPosition(false);
			const float size = Random01();
			const float radius = kRefRadius * (0.1f + 0.2f * size);
			SpawnMicro(pos, radius, true);
			// each hit leaves a trace of film, more for a bigger one: a
			// downpour wets the lens over time through the bounded accumulation
			StampDisc(pos, radius * params.dropSize * 2.5f, 0.2f, kMicroFilm * (0.5f + size), false);
		});
		fire(timerNormal, current.normalRate * rate * impactFacing, [&]() {
			const float rn = Lerp(current.sizeMin, current.sizeMax, std::pow(Random01(), current.sizeBeta));
			SpawnRainDrop(rn * impactSize, false, nullptr, 0.0f);
		});
		fire(timerLarge, current.largeRate * rate * impactFacing, [&]() {
			SpawnRainDrop(Lerp(current.largeMin, current.largeMax, Random01()), true, nullptr, 0.0f);
		});
		fire(timerFlow, current.flowRate * heavyFlow * rate * flowFacing, [&]() {
			Vec2 pos = RandomPosition(true);
			// runs start high on the lens so they cross it
			pos = pos - Normalize(input.gravity, { 0.0f, -1.0f }) * (0.25f * Random01());
			SpawnFlow(pos, 1.4f + 0.6f * Random01(), input.gravity * (0.25f * current.speed),
				current.filmDeposit);
		});
		fire(timerSheet, current.sheetRate * heavyFlow * rate * sheetFacing, [&]() {
			SpawnSheet(RandomPosition(true), 0.6f + 0.4f * Random01(), 1.0f, current.speed,
				current.filmDeposit);
		});
	}

	for (Spray &spray : sprays)
	{
		spray.remaining -= dt;
		SpraySpawn(spray.strength, spray.dir, dt);
	}
	sprays.erase(std::remove_if(sprays.begin(), sprays.end(),
		[](const Spray &s) { return s.remaining <= 0.0f; }), sprays.end());

	// map emitters (waterfalls): a continuous spray while the camera is near,
	// independent of rain and cover
	if (input.sprayStrength > 0.0f)
		SpraySpawn(std::min(input.sprayStrength, 2.0f), input.sprayDir, dt);
}

// Spray: repeated short impacts biased toward the side the water comes from.
void LensWater::SpraySpawn(float s, Vec2 dir, float dt)
{
	const float density = std::max(params.density, 0.0f);
	if (Random01() < std::min(12.0f * s * density * dt, 0.5f))
	{
		const Vec2 at = SideBiasedPosition(dir, 0.35f);
		SpawnRainDrop(0.3f + 0.5f * Random01(), false, &at, 0.05f);
	}
	if (Random01() < std::min(30.0f * s * density * dt, 0.8f))
		SpawnMicro(SideBiasedPosition(dir, 0.4f), kRefRadius * (0.1f + 0.2f * Random01()), true);
	if (Random01() < std::min(0.6f * s * density * dt, 0.2f))
	{
		const Vec2 at = SideBiasedPosition(dir, 0.3f);
		SpawnRainDrop(1.1f + 0.5f * Random01(), true, &at, 0.02f);
		StampDisc(at, 0.06f, 1.0f, kSprayFilm * s, false);
	}
}

float ResolveWorldEvent(const float origin[3], float radius, const float viewOrigin[3],
	const float forward[3], const float right[3], const float up[3], Vec2 &side)
{
	side = { 0.0f, 0.0f };
	const float d[3] = { origin[0] - viewOrigin[0], origin[1] - viewOrigin[1], origin[2] - viewOrigin[2] };
	const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (radius <= 0.0f || dist >= radius)
		return 0.0f;
	float falloff = 1.0f - dist / radius;
	falloff *= falloff;
	if (dist < 1e-3f)
		return falloff;	// at the camera: no side
	const float inv = 1.0f / dist;
	const float toSource[3] = { d[0] * inv, d[1] * inv, d[2] * inv };
	// water thrown from behind the camera reaches the lens less
	const float facing = Lerp(0.3f, 1.0f, Saturate(toSource[0] * forward[0]
		+ toSource[1] * forward[1] + toSource[2] * forward[2]));
	side = {
		toSource[0] * right[0] + toSource[1] * right[1] + toSource[2] * right[2],
		toSource[0] * up[0] + toSource[1] * up[1] + toSource[2] * up[2] };
	return falloff * facing;
}

void LensWater::ProcessEvents()
{
	if (pendingEvents.empty())
		return;
	// swap, not move: both vectors keep their capacity
	processingEvents.clear();
	processingEvents.swap(pendingEvents);
	sinceEvent = 0.0f;
	for (const Event &event : processingEvents)
	{
		const float s = std::max(event.strength, 0.05f);
		switch (event.type)
		{
		case EVENT_CLEAR:
		case EVENT_SUBMERGE:
			Clear();
			break;

		case EVENT_SPLASH:
		{
			const Vec2 center = SideBiasedPosition(event.dir, 0.3f);
			StampDisc(center, 0.1f + 0.08f * s, 1.0f, kSplashPreset.film * s, true);
			const int normal = 5 + (int)(8.0f * s);
			for (int i = 0; i < normal; ++i)
				SpawnRainDrop(0.4f + 0.7f * Random01(), false, &center, 0.12f + 0.1f * s);
			const int large = 1 + (s > 0.6f ? 1 : 0);
			for (int i = 0; i < large; ++i)
				SpawnRainDrop(1.3f + 0.5f * Random01(), true, &center, 0.08f);
			const int small = 12 + (int)(20.0f * s);
			for (int i = 0; i < small; ++i)
			{
				const float angle = Random01() * 6.2831853f;
				const float dist = std::sqrt(Random01()) * 0.2f;
				SpawnMicro(center + Vec2{ std::cos(angle) * dist, std::sin(angle) * dist },
					kRefRadius * (0.1f + 0.2f * Random01()), true);
			}
			break;
		}

		case EVENT_SPRAY:
			if (sprays.size() < 4)
				sprays.push_back({ s, event.duration > 0.0f ? event.duration : 2.5f, event.dir });
			break;

		case EVENT_EMERGE:
			StartEmerge(s);
			break;
		}
	}
	processingEvents.clear();
}

/*
Emerge. Leaving water is choreographed over about 2.5 s instead of dropping
everything at once:
  t = 0          uneven film over most of the lens (thick patches, thin holes)
                 and 2-4 broad sheets
  0.1 .. 0.5 s   2-5 flow heads start upstream and run with gravity
  0.4 .. 1.5 s   narrower rivulets, a late thin sheet
  0 .. 2.5 s     the film tears: holes open where the detail noise is low
No beads are spawned: the residual mechanism leaves them behind the moving
water, where they belong.
*/
void LensWater::StartEmerge(float strength)
{
	const float s = std::min(strength, 1.25f);
	if (emerge.active && emerge.age < kEmergeFlowEnd)
	{
		// a second emerge right away: stronger, not doubled
		emerge.strength = std::max(emerge.strength, s);
		return;
	}
	emerge = {};
	emerge.active = true;
	emerge.strength = s;
	const int detailX = (int)(Random01() * filmWidth);
	const int detailY = (int)(Random01() * filmHeight);
	const int octaveX = (int)(Random01() * filmWidth);
	const int octaveY = (int)(Random01() * filmHeight);
	emergePattern.resize((size_t)filmWidth * filmHeight);
	for (int y = 0; y < filmHeight; ++y)
	for (int x = 0; x < filmWidth; ++x)
		emergePattern[(size_t)y * filmWidth + x] = 0.7f * Detail(x, y, detailX, detailY)
			+ 0.3f * Detail(x * 2, y * 2, octaveX, octaveY);
	emerge.lateSheetsLeft = s > 0.6f ? 2 : 1;
	emerge.nextSheet = Lerp(0.5f, 0.9f, Random01());
	if (params.filmModel == 1)
	{
		StartFilmEmerge(s);
	}
	else
	{
		emerge.flowsLeft = 2 + (int)(3.0f * std::min(s, 1.0f) + 0.5f);
		emerge.rivuletsLeft = 2 + (int)(2.0f * std::min(s, 1.0f) + 0.5f);
		emerge.nextFlow = Lerp(kEmergeFlowStart, kEmergeFlowStart + 0.1f, Random01());
		emerge.nextRivulet = Lerp(kEmergeRivuletStart, kEmergeRivuletStart + 0.2f, Random01());
		StampEmergeFilm(s);
	}

	// broad sheets start high on the lens and run through the film (hybrid
	// model only: the draining film has no fast sheets)
	if (emerge.filmFirst)
		return;
	const Vec2 down = Normalize(lastInput.gravity, { 0.0f, -1.0f });
	const int numSheets = 2 + (int)(2.0f * std::min(s, 1.0f) + 0.5f);
	for (int i = 0; i < numSheets; ++i)
	{
		const Vec2 at = { (Random01() - 0.5f) * aspect * 0.85f, (Random01() - 0.5f) * 0.5f };
		SpawnSheet(at - down * 0.2f, 0.8f + 0.2f * Random01(), 1.3f + 0.3f * Random01(),
			kEmergePreset.sheetSpeed, kEmergePreset.film);
	}
}

void LensWater::UpdateEmerge(float dt)
{
	if (!emerge.active)
		return;
	emerge.age += dt;
	if (emerge.filmFirst)
	{
		UpdateFilmEmerge();
		return;
	}
	const float s = emerge.strength;
	const Vec2 down = Normalize(lastInput.gravity, { 0.0f, -1.0f });
	// upstream edge of the lens: runs start there and cross it
	auto upstream = [&](float spread) {
		const Vec2 side = { -down.y, down.x };
		const float across = (Random01() - 0.5f) * 2.0f * spread;
		Vec2 p = down * -(0.3f + 0.15f * Random01());
		p = p + Vec2{ side.x * across * aspect * 0.5f, side.y * across * 0.5f };
		p.x = std::max(-aspect * 0.5f, std::min(aspect * 0.5f, p.x));
		p.y = std::max(-0.5f, std::min(0.5f, p.y));
		return p;
	};

	const float flowSpan = kEmergeFlowEnd - kEmergeFlowStart;
	while (emerge.flowsLeft > 0 && emerge.age >= emerge.nextFlow)
	{
		SpawnFlow(upstream(0.9f), 1.5f + 0.6f * Random01() * s,
			down * (0.35f * kEmergePreset.flowSpeed), kEmergePreset.film);
		--emerge.flowsLeft;
		emerge.nextFlow += flowSpan / 4.0f * (0.4f + 1.2f * Random01());
	}

	const float rivuletSpan = kEmergeRivuletEnd - kEmergeRivuletStart;
	while (emerge.rivuletsLeft > 0 && emerge.age >= emerge.nextRivulet)
	{
		// narrower, slower: rivulets draining what is left of the film
		SpawnFlow(upstream(0.8f) + down * (0.2f * Random01()), 1.2f + 0.3f * Random01(),
			down * (0.2f * kEmergePreset.flowSpeed), 0.6f * kEmergePreset.film);
		--emerge.rivuletsLeft;
		emerge.nextRivulet += rivuletSpan / 4.0f * (0.4f + 1.2f * Random01());
	}

	while (emerge.lateSheetsLeft > 0 && emerge.age >= emerge.nextSheet)
	{
		SpawnSheet(upstream(0.7f), 0.5f + 0.2f * Random01(), 0.8f + 0.3f * Random01(),
			kEmergePreset.sheetSpeed * 0.8f, 0.6f * kEmergePreset.film);
		--emerge.lateSheetsLeft;
		emerge.nextSheet += 0.3f + 0.4f * Random01();
	}

	if (emerge.age >= kEmergeBreakup && emerge.flowsLeft == 0 && emerge.rivuletsLeft == 0
		&& emerge.lateSheetsLeft == 0)
		emerge.active = false;
}

// Uneven film: thickness from the tileable detail noise, a smoothstep gives
// patches with fairly sharp edges and thin holes. The film shader refracts
// through the thickness gradient, so the structure is what makes a wet lens
// visible; a uniform film would not show.
void LensWater::StampEmergeFilm(float strength)
{
	if (film.empty())
		return;
	const float amount = kEmergePreset.film * std::min(strength, 1.0f);
	for (int y = 0; y < filmHeight; ++y)
	for (int x = 0; x < filmWidth; ++x)
	{
		const float pattern = emergePattern[(size_t)y * filmWidth + x];
		const float thickness = 0.15f + 0.85f * Smoothstep(0.32f, 0.55f, pattern);
		float *cell = &film[((size_t)y * filmWidth + x) * 2];
		cell[0] = Deposit(cell[0], 0.85f + 0.15f * thickness);
		cell[1] = Deposit(cell[1], amount * thickness);
	}
	filmMax = std::max(filmMax, 1.0f);
	filmVisible = true;
}

/*
Film / wetness field. R = wetness (path affinity, slow decay), G = optical
thin film (fast decay). Deposits accumulate, bounded by one, where new water
passes; decay continues unchanged under cover. Stamps only write the CPU
field: it is marked dirty here, on the 30 Hz field tick, so the texture
upload never runs faster than that.
*/
void LensWater::DecayField(float dt)
{
	if (filmMax <= 0.0f && rainFilm <= 0.0f)
		return;
	if (rainFilm > 0.0f)
	{
		// film-first downpour: film everywhere, unevenly (cached detail noise)
		const float amount = rainFilm * dt;
		float *c = film.data();
		for (size_t i = 0, n = film.size() / 2; i < n; ++i, c += 2)
		{
			const float a = amount * (0.4f + 0.6f * detail[i]);
			c[0] = Deposit(c[0], a);
			c[1] = Deposit(c[1], a);
		}
	}
	const float wetK = std::exp(-dt / std::max(current.wetTau * std::max(params.wetDecay, 0.01f), 0.05f));
	const float filmK = std::exp(-dt / std::max(current.filmTau * std::max(params.filmDecay, 0.01f), 0.05f));
	float maxWet = 0.0f, maxFilm = 0.0f;
	float *cell = film.data();
	const size_t count = film.size() / 2;
	if (emerge.active && emerge.filmFirst)
	{
		// Film-first emerge: the film drains, h = sqrt((s + x0) / (t + t0))
		// along the drainage streaks, never thicker than it was. Where it
		// gets thinner than its rupture thickness it tears to a trace (the
		// sites of UpdateFilmEmerge dewet into beads); the rim at the bottom
		// holds until it breaks into beads.
		const float t = emerge.age;
		const float norm = std::sqrt(kJeffreysT0 / ((1.0f + kJeffreysX0) * (t + kJeffreysT0)));
		const float followK = std::exp(-dt / kFollowTau);
		const float tearK = std::exp(-dt / kRuptureTau);
		const float rim = t < kRimBreak ? 0.9f : 0.0f;
		for (size_t i = 0; i < count; ++i, cell += 2)
		{
			const float s = emergeDrain[i];
			const float h = std::sqrt(s + kJeffreysX0) * norm * (0.8f + 0.4f * emergeStreak[i]);
			const float f0 = cell[1];
			float f = f0 > h ? h + (f0 - h) * followK : f0 * filmK;
			if (h < emergeRupture[i] && f > kRuptureTrace)
				f = kRuptureTrace + (f - kRuptureTrace) * tearK;
			if (rim > 0.0f)
				f = std::max(f, std::min(f0, rim * Smoothstep(kRimBand, kRimBand + 0.04f, s)));
			const float w = cell[0] * wetK;
			cell[0] = w >= 1e-3f ? w : 0.0f;
			cell[1] = f >= 1e-3f ? f : 0.0f;
			maxWet = std::max(maxWet, w);
			maxFilm = std::max(maxFilm, f);
		}
	}
	else if (emerge.active && emerge.age < kEmergeBreakup)
	{
		// emerge breakup: the film tears where the detail noise is low, the
		// holes grow as the threshold rises
		const float threshold = Lerp(0.2f, 0.62f, emerge.age / kEmergeBreakup);
		const float tearK = std::exp(-dt / kEmergeBreakupTau);
		const float *pattern = emergePattern.data();
		for (size_t i = 0; i < count; ++i, cell += 2)
		{
			const float n = pattern[i];
			const float tear = Lerp(filmK, tearK, Smoothstep(threshold, threshold - 0.08f, n));
			const float w = cell[0] * wetK;
			const float f = cell[1] * tear;
			cell[0] = w >= 1e-3f ? w : 0.0f;
			cell[1] = f >= 1e-3f ? f : 0.0f;
			maxWet = std::max(maxWet, w);
			maxFilm = std::max(maxFilm, f);
		}
	}
	else
	{
		// branch free so the compiler vectorises it
		for (size_t i = 0; i < count; ++i, cell += 2)
		{
			const float w = cell[0] * wetK;
			const float f = cell[1] * filmK;
			cell[0] = w >= 1e-3f ? w : 0.0f;
			cell[1] = f >= 1e-3f ? f : 0.0f;
			maxWet = std::max(maxWet, w);
			maxFilm = std::max(maxFilm, f);
		}
	}
	const float maxValue = std::max(maxWet, maxFilm) >= 1e-3f ? std::max(maxWet, maxFilm) : 0.0f;
	filmMax = maxValue;
	filmVisible = maxFilm > 0.01f;
	filmDirty = true;
}

// coverage: the fraction of a full application this stamp represents (a
// moving stamp overlaps its predecessors, see UpdateDrop)
void LensWater::StampCapsule(Vec2 a, Vec2 b, float radius, float wet, float filmAmount, float coverage)
{
	if (film.empty() || radius <= 0.0f)
		return;
	const float h = (float)filmHeight;
	auto toCell = [&](Vec2 p) { return Vec2{ (p.x + aspect * 0.5f) * h - 0.5f, (p.y + 0.5f) * h - 0.5f }; };
	const Vec2 ca = toCell(a), cb = toCell(b);
	const float rc = radius * h;
	const int x0 = std::max(0, (int)std::floor(std::min(ca.x, cb.x) - rc));
	const int x1 = std::min(filmWidth - 1, (int)std::ceil(std::max(ca.x, cb.x) + rc));
	const int y0 = std::max(0, (int)std::floor(std::min(ca.y, cb.y) - rc));
	const int y1 = std::min(filmHeight - 1, (int)std::ceil(std::max(ca.y, cb.y) + rc));
	if (x0 > x1 || y0 > y1)
		return;
	const Vec2 ab = cb - ca;
	const float abLen2 = std::max(Dot(ab, ab), 1e-8f);
	bool touched = false;
	for (int y = y0; y <= y1; ++y)
	for (int x = x0; x <= x1; ++x)
	{
		const Vec2 c = { (float)x, (float)y };
		const float t = Saturate(Dot(c - ca, ab) / abLen2);
		const Vec2 closest = ca + ab * t;
		const float d = Length(c - closest) / std::max(rc, 1e-4f);
		if (d >= 1.0f)
			continue;
		const float falloff = 1.0f - Smoothstep(0.35f, 1.0f, d);
		float *cell = &film[((size_t)y * filmWidth + x) * 2];
		if (coverage >= 1.0f)
		{
			cell[0] = Deposit(cell[0], wet * falloff);
			cell[1] = Deposit(cell[1], filmAmount * falloff);
		}
		else
		{
			cell[0] = Deposit(cell[0], 1.0f - std::pow(1.0f - Saturate(wet * falloff), coverage));
			cell[1] = Deposit(cell[1], 1.0f - std::pow(1.0f - Saturate(filmAmount * falloff), coverage));
		}
		touched = true;
	}
	if (touched)
	{
		filmMax = std::max(filmMax, std::min(std::max(wet, filmAmount), 1.0f));
		if (filmAmount > 0.01f)
			filmVisible = true;
	}
}

void LensWater::StampDisc(Vec2 center, float radius, float wet, float filmAmount, bool noisy)
{
	if (!noisy)
	{
		StampCapsule(center, center, radius, wet, filmAmount, 1.0f);
		return;
	}
	// broad irregular sheet of water (splash, emerge)
	if (film.empty())
		return;
	const float h = (float)filmHeight;
	const Vec2 cc = { (center.x + aspect * 0.5f) * h - 0.5f, (center.y + 0.5f) * h - 0.5f };
	const float rc = radius * h;
	const int x0 = std::max(0, (int)std::floor(cc.x - rc)), x1 = std::min(filmWidth - 1, (int)std::ceil(cc.x + rc));
	const int y0 = std::max(0, (int)std::floor(cc.y - rc)), y1 = std::min(filmHeight - 1, (int)std::ceil(cc.y + rc));
	// cached tileable noise at a random offset: no per cell hashing
	const int ox = (int)(Random01() * filmWidth), oy = (int)(Random01() * filmHeight);
	for (int y = y0; y <= y1; ++y)
	for (int x = x0; x <= x1; ++x)
	{
		const float d = Length(Vec2{ x - cc.x, y - cc.y }) / std::max(rc, 1e-4f);
		if (d >= 1.0f)
			continue;
		const float n = Detail(x * 2, y * 2, ox, oy);
		const float falloff = (1.0f - Smoothstep(0.5f, 1.0f, d)) * (0.55f + 0.45f * n);
		float *cell = &film[((size_t)y * filmWidth + x) * 2];
		cell[0] = Deposit(cell[0], wet * falloff);
		cell[1] = Deposit(cell[1], filmAmount * falloff);
	}
	filmMax = std::max(filmMax, std::min(std::max(wet, filmAmount), 1.0f));
	if (filmAmount > 0.01f)
		filmVisible = true;
}

// Dewetting: water gathers into a bead, the film around it is pulled in
// (keep: the film fraction left at the centre).
void LensWater::StampDrain(Vec2 center, float radius, float keep)
{
	if (film.empty() || radius <= 0.0f)
		return;
	const float h = (float)filmHeight;
	const Vec2 cc = { (center.x + aspect * 0.5f) * h - 0.5f, (center.y + 0.5f) * h - 0.5f };
	const float rc = radius * h;
	const int x0 = std::max(0, (int)std::floor(cc.x - rc)), x1 = std::min(filmWidth - 1, (int)std::ceil(cc.x + rc));
	const int y0 = std::max(0, (int)std::floor(cc.y - rc)), y1 = std::min(filmHeight - 1, (int)std::ceil(cc.y + rc));
	for (int y = y0; y <= y1; ++y)
	for (int x = x0; x <= x1; ++x)
	{
		const float d = Length(Vec2{ x - cc.x, y - cc.y }) / std::max(rc, 1e-4f);
		if (d >= 1.0f)
			continue;
		film[((size_t)y * filmWidth + x) * 2 + 1] *= Lerp(1.0f, keep, 1.0f - Smoothstep(0.5f, 1.0f, d));
	}
}

/*
Film-first emerge, after the physics of a film lifted out of water. The
lens leaves the water with an even sheet on it, which drains under gravity
(Jeffreys: h ~ sqrt(x / t), DecayField): it thins everywhere, thinnest at
the top and thickest at the bottom, where a rim collects. Streaks of the
drainage show as thickness variation, and the micro structure drifts down
with the water (FilmFlow). The thin top tears first: there the dewetting
film leaves small beads, later and larger ones further down (site time from
the analytic profile). The rim breaks into a row of beads along the bottom
edge (Rayleigh-Plateau). No sheets, no flow heads from nowhere.
*/
void LensWater::StartFilmEmerge(float s)
{
	emerge.filmFirst = true;
	emerge.down = Normalize(lastInput.gravity, { 0.0f, -1.0f });
	emerge.drainTime = kEmergeDuration;
	emerge.flowBase = filmFlow;
	emerge.lateSheetsLeft = 0;

	const Vec2 down = emerge.down;
	const Vec2 side = { -down.y, down.x };
	const float extentDown = std::max(0.5f * (aspect * std::fabs(down.x) + std::fabs(down.y)), 1e-3f);
	const float extentSide = 0.5f * (aspect * std::fabs(side.x) + std::fabs(side.y));
	const float h = (float)filmHeight;
	const float amount = kEmergePreset.film * std::min(s, 1.0f);
	const float streakX = Random01() * 512.0f, streakY = Random01() * 512.0f;
	auto cellPos = [&](int x, int y) { return Vec2{ (x + 0.5f) / h - aspect * 0.5f, (y + 0.5f) / h - 0.5f }; };

	const size_t count = (size_t)filmWidth * filmHeight;
	emergeDrain.resize(count);
	emergeStreak.resize(count);
	emergeRupture.resize(count);
	for (int y = 0; y < filmHeight; ++y)
	for (int x = 0; x < filmWidth; ++x)
	{
		const size_t i = (size_t)y * filmWidth + x;
		const Vec2 p = cellPos(x, y);
		const float across = Dot(p, side), along = Dot(p, down);
		const float pattern = emergePattern[i];
		emergeDrain[i] = Saturate((along + extentDown) / (2.0f * extentDown));
		// drainage streaks: fine across the flow, long along it
		emergeStreak[i] = 0.65f * ValueNoise(across * 18.0f + streakX, along * 3.0f + streakY, 911)
			+ 0.35f * ValueNoise(across * 40.0f + streakY, along * 6.0f + streakX, 1777);
		emergeRupture[i] = kRupture * (1.0f + 0.35f * (pattern - 0.5f));
		// an even sheet: the micro refraction makes it visible
		float *cell = &film[i * 2];
		cell[0] = Deposit(cell[0], 0.95f);
		cell[1] = Deposit(cell[1], amount * (0.85f + 0.15f * pattern));
	}
	filmMax = std::max(filmMax, 1.0f);
	filmVisible = true;

	// when the analytic profile of a cell gets thinner than its rupture
	// thickness (DecayField)
	auto ruptureTime = [&](size_t i) {
		const float m = 0.8f + 0.4f * emergeStreak[i];
		const float r = emergeRupture[i] / m;
		return (emergeDrain[i] + kJeffreysX0) * kJeffreysT0 / ((1.0f + kJeffreysX0) * r * r) - kJeffreysT0;
	};

	// dewetting sites on the thicker spots of the torn film, away from the
	// screen centre and above the rim
	emergeSites.clear();
	const int numSites = 10 + (int)(10.0f * std::min(s, 1.0f));
	for (int attempt = 0; attempt < numSites * 8 && (int)emergeSites.size() < numSites; ++attempt)
	{
		int x = std::min((int)(Random01() * filmWidth), filmWidth - 1);
		int y = std::min((int)(Random01() * filmHeight), filmHeight - 1);
		for (int step = 0; step < 6; ++step)
		{
			int bx = x, by = y;
			float best = emergePattern[(size_t)y * filmWidth + x];
			for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
			{
				const int nx = std::max(0, std::min(filmWidth - 1, x + dx * 3));
				const int ny = std::max(0, std::min(filmHeight - 1, y + dy * 3));
				const float v = emergePattern[(size_t)ny * filmWidth + nx];
				if (v > best)
				{
					best = v;
					bx = nx;
					by = ny;
				}
			}
			if (bx == x && by == y)
				break;
			x = bx;
			y = by;
		}
		const size_t i = (size_t)y * filmWidth + x;
		const Vec2 p = cellPos(x, y);
		if (emergeDrain[i] > kRimBand - 0.05f)
			continue;
		if (Random01() > Lerp(0.3f, 1.0f, Smoothstep(0.15f, 0.5f, Length(p))))
			continue;
		bool crowded = false;
		for (const EmergeSite &other : emergeSites)
			crowded = crowded || Length(other.pos - p) < 0.06f;
		if (crowded)
			continue;
		EmergeSite site;
		site.pos = p;
		// the further down, the more water the torn film had gathered
		site.rn = Lerp(0.2f, 0.45f, emergeDrain[i]) * Lerp(0.85f, 1.15f, Random01());
		site.time = std::max(ruptureTime(i), 0.15f) + Lerp(0.05f, 0.2f, Random01());
		site.done = false;
		emergeSites.push_back(site);
	}

	// the rim: a row of beads along the bottom edge of the lens (the
	// furthest point down the gravity for each position across it)
	const float halfX = aspect * 0.5f, halfY = 0.5f;
	for (float across = -extentSide + Lerp(0.02f, 0.06f, Random01()); across < extentSide;
		across += Lerp(0.12f, 0.18f, Random01()))
	{
		float u = 1e9f;
		if (std::fabs(down.x) > 1e-4f)
			u = std::min(u, ((down.x > 0.0f ? halfX : -halfX) - across * side.x) / down.x);
		if (std::fabs(down.y) > 1e-4f)
			u = std::min(u, ((down.y > 0.0f ? halfY : -halfY) - across * side.y) / down.y);
		if (u > 1e8f)
			continue;
		const Vec2 p = side * across + down * (u - Lerp(0.025f, 0.045f, Random01()));
		if (std::fabs(p.x) > halfX || std::fabs(p.y) > halfY)
			continue;
		EmergeSite site;
		site.pos = p;
		site.rn = Lerp(0.45f, 0.9f, Random01());
		site.time = kRimBreak + Lerp(0.0f, 0.4f, Random01());
		site.done = false;
		emergeSites.push_back(site);
	}
}

void LensWater::UpdateFilmEmerge()
{
	// the draining water carries the micro structure down, fast at first
	// (the surface velocity goes with h^2 ~ 1 / t)
	filmFlow = emerge.flowBase + emerge.down * (kFlowShift * std::log(1.0f + emerge.age / 0.2f));

	bool spawned = false, pending = false;
	for (EmergeSite &site : emergeSites)
	{
		if (site.done)
			continue;
		if (emerge.age < site.time)
		{
			pending = true;
			continue;
		}
		site.done = true;
		Drop &drop = AddDrop(site.pos, site.rn * kRefRadius, site.rn < 0.7f ? DROP_BEAD : DROP_NORMAL);
		drop.state = STATE_FORMING;
		drop.stateAge = 0.0f;
		StampDrain(site.pos, drop.radius * params.dropSize * 2.5f, kDewet);
		++spawnCount;
		spawned = true;
	}
	if (spawned)
		Evict();

	if (!pending && emerge.age > emerge.drainTime)
		emerge.active = false;
}

float LensWater::SampleField(Vec2 p, int channel) const
{
	if (film.empty())
		return 0.0f;
	const float h = (float)filmHeight;
	const float fx = std::max(0.0f, std::min((p.x + aspect * 0.5f) * h - 0.5f, (float)filmWidth - 1.001f));
	const float fy = std::max(0.0f, std::min((p.y + 0.5f) * h - 0.5f, (float)filmHeight - 1.001f));
	const int x = (int)fx, y = (int)fy;
	const float tx = fx - x, ty = fy - y;
	auto at = [&](int cx, int cy) {
		cx = std::min(cx, filmWidth - 1);
		cy = std::min(cy, filmHeight - 1);
		return film[((size_t)cy * filmWidth + cx) * 2 + channel];
	};
	return Lerp(Lerp(at(x, y), at(x + 1, y), tx), Lerp(at(x, y + 1), at(x + 1, y + 1), tx), ty);
}

float LensWater::SampleDefect(Vec2 p) const
{
	if (defect.empty())
		return DefectNoise(p);
	const float h = (float)filmHeight;
	const float fx = std::max(0.0f, std::min((p.x + aspect * 0.5f) * h - 0.5f, (float)filmWidth - 1.001f));
	const float fy = std::max(0.0f, std::min((p.y + 0.5f) * h - 0.5f, (float)filmHeight - 1.001f));
	const int x = (int)fx, y = (int)fy;
	const float tx = fx - x, ty = fy - y;
	auto at = [&](int cx, int cy) {
		return defect[(size_t)std::min(cy, filmHeight - 1) * filmWidth + std::min(cx, filmWidth - 1)];
	};
	return Lerp(Lerp(at(x, y), at(x + 1, y), tx), Lerp(at(x, y + 1), at(x + 1, y + 1), tx), ty);
}

float LensWater::SampleAffinity(Vec2 p) const
{
	if (affinity.empty())
		return SurfaceAffinity(p);
	const float h = (float)filmHeight;
	const float fx = std::max(0.0f, std::min((p.x + aspect * 0.5f) * h - 0.5f, (float)filmWidth - 1.001f));
	const float fy = std::max(0.0f, std::min((p.y + 0.5f) * h - 0.5f, (float)filmHeight - 1.001f));
	const int x = (int)fx, y = (int)fy;
	const float tx = fx - x, ty = fy - y;
	auto at = [&](int cx, int cy) {
		return affinity[(size_t)std::min(cy, filmHeight - 1) * filmWidth + std::min(cx, filmWidth - 1)];
	};
	return Lerp(Lerp(at(x, y), at(x + 1, y), tx), Lerp(at(x, y + 1), at(x + 1, y + 1), tx), ty);
}

Vec2 LensWater::AffinityGradient(Vec2 p) const
{
	const float e = 0.25f / kNoiseFrequency;
	return {
		(SampleAffinity({ p.x + e, p.y }) - SampleAffinity({ p.x - e, p.y })) / (2.0f * e),
		(SampleAffinity({ p.x, p.y + e }) - SampleAffinity({ p.x, p.y - e })) / (2.0f * e) };
}

// tileable detail noise at cell (x, y) shifted by (ox, oy), wrapping
float LensWater::Detail(int x, int y, int ox, int oy) const
{
	const int cx = (x + ox) % filmWidth;
	const int cy = (y + oy) % filmHeight;
	return detail[(size_t)cy * filmWidth + cx];
}

Vec2 LensWater::FieldGradient(Vec2 p, int channel) const
{
	const float e = 1.0f / (float)std::max(filmHeight, 1);
	return {
		(SampleField({ p.x + e, p.y }, channel) - SampleField({ p.x - e, p.y }, channel)) / (2.0f * e),
		(SampleField({ p.x, p.y + e }, channel) - SampleField({ p.x, p.y - e }, channel)) / (2.0f * e) };
}

int LensWater::BuildInstances(float *out, int maxInstances, float dropSize) const
{
	const int total = (int)(drops.size() + micro.size() + sheets.size());
	const int n = std::min(total, maxInstances);
	if (n <= 0)
		return 0;
	int index = 0;
	auto write = [&](int texel, float a, float b, float c, float d)
	{
		float *o = out + ((size_t)texel * n + index) * 4;
		o[0] = a; o[1] = b; o[2] = c; o[3] = d;
	};
	const float t = Saturate(interpolation);

	// sheets first: never dropped when the instance buffer is short
	for (const Sheet &sheet : sheets)
	{
		if (index >= n)
			break;
		const Vec2 p = Lerp(sheet.prevPos, sheet.pos, t);
		const float life = sheet.age / std::max(sheet.lifetime, 1e-3f);
		const float fade = Smoothstep(0.0f, 0.06f, sheet.age) * (1.0f - Smoothstep(0.65f, 1.0f, life));
		write(0, p.x, p.y, sheet.length * 0.5f * dropSize, sheet.strength * fade);
		write(1, sheet.dir.x, sheet.dir.y, 1.0f, (float)DROP_SHEET);
		write(2, 0.0f, 0.0f, 0.0f, (sheet.seed & 0xffffu) / 65535.0f);
		write(3, 0.0f, (float)STATE_SETTLED, 1.0f, sheet.width * 0.5f * dropSize);
		++index;
	}
	for (const Drop &drop : drops)
	{
		if (index >= n)
			break;
		float scale, irregularity;
		ImpactShape(drop, scale, irregularity);
		const float progress = Smoothstep(0.0f, kMergeRelax, drop.mergeAge);
		const Vec2 base = Lerp(drop.prevPos, drop.pos, t);
		const Vec2 p = base + drop.mainOffset * (1.0f - progress);
		const float radius = drop.radius * dropSize * scale * Lerp(0.8f, 1.0f, progress);
		const float speed = Length(drop.vel);
		const Vec2 axis = speed > 1e-3f ? drop.vel * (1.0f / speed) : Vec2{ 0.0f, -1.0f };
		const float tail = drop.moving ? std::min(1.0f + 3.0f * speed,
			drop.type == DROP_FLOW ? 2.4f : 1.6f) : 1.0f;
		// lobe relative to the rendered main cap; both converge on pos
		const Vec2 lobe = (drop.lobeOffset - drop.mainOffset) * (1.0f - progress);
		const float lobeRadius = drop.lobeRadius * dropSize * (1.0f - progress * progress);
		// a forming bead fades in as it grows out of the film
		const float weight = drop.state == STATE_FORMING
			? Lerp(0.3f, 1.0f, Smoothstep(0.0f, kFormTime, drop.stateAge)) : 1.0f;
		write(0, p.x, p.y, radius, weight);
		write(1, axis.x, axis.y, tail, (float)drop.type);
		write(2, lobe.x, lobe.y, lobeRadius, (drop.seed & 0xffffu) / 65535.0f);
		write(3, drop.pinRatio, (float)drop.state, drop.moving ? 1.0f : 0.0f, irregularity);
		++index;
	}
	for (const Drop &m : micro)
	{
		if (index >= n)
			break;
		// a direct hit spreads wider than a bead (more refraction, lopsided),
		// then recoils into the settled micro drop
		float scale, irregularity;
		ImpactShape(m, scale, irregularity);
		if (scale > 1.0f)
			scale = 1.0f + (scale - 1.0f) * 2.0f;
		float weight = 0.85f;
		if (params.filmModel == 1)
		{
			// film-first: dissolve into the film instead of popping
			const float fade = Smoothstep(kMicroFadeStart, 1.0f, m.age / MicroLifetime(m));
			weight *= 1.0f - fade;
			scale *= 1.0f - 0.2f * fade;
		}
		write(0, m.pos.x, m.pos.y, m.radius * dropSize * scale, weight);
		write(1, 0.0f, -1.0f, 1.0f, (float)DROP_MICRO);
		write(2, 0.0f, 0.0f, 0.0f, (m.seed & 0xffffu) / 65535.0f);
		write(3, 0.0f, (float)m.state, 0.0f, irregularity * 1.4f);
		++index;
	}
	return n;
}

Stats LensWater::GetStats() const
{
	Stats s = {};
	s.drops = (int)drops.size();
	s.micro = (int)micro.size();
	s.sheets = (int)sheets.size();
	for (const Drop &d : drops)
	{
		s.moving += d.moving ? 1 : 0;
		s.flows += d.type == DROP_FLOW ? 1 : 0;
		s.residuals += d.type == DROP_RESIDUAL ? 1 : 0;
		s.beads += d.type == DROP_BEAD ? 1 : 0;
	}
	s.filmWidth = filmWidth;
	s.filmHeight = filmHeight;
	s.filmVisible = filmVisible;
	s.filmDirty = filmDirty;
	s.profile = activeProfile;
	s.updateMicroseconds = lastUpdateMicroseconds;
	s.agentMicroseconds = agentMicroseconds;
	s.fieldMicroseconds = fieldMicroseconds;
	s.eventMicroseconds = eventMicroseconds;
	s.sinceEvent = sinceEvent;
	s.sprays = (int)sprays.size();
	s.continuousSpray = lastInput.sprayStrength;
	return s;
}

} // namespace lenswater
