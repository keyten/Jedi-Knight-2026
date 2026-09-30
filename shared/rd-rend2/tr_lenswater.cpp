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
	  0.55f, 3.0f, 15.0f, 18.0f, 1.0f, 0.45f, 1.0f },
	// LIGHT: beads, rare mergers, almost no continuous flow
	{ 6.0f, 1.2f, 0.08f, 0.0f, 0.0f, 0.30f, 0.90f, 2.2f, 0.90f, 1.30f,
	  0.35f, 2.5f, 12.0f, 30.0f, 0.8f, 0.6f, 1.0f },
	// NORMAL: static beads, moving drops and thin paths together
	{ 14.0f, 3.0f, 0.35f, 0.15f, 0.05f, 0.30f, 1.00f, 1.8f, 0.95f, 1.40f,
	  0.55f, 3.0f, 15.0f, 18.0f, 1.0f, 0.45f, 1.0f },
	// HEAVY: turnover, film, rivulets and sheets rather than more beads
	{ 40.0f, 5.0f, 0.5f, 1.6f, 1.2f, 0.30f, 1.10f, 1.4f, 1.00f, 1.50f,
	  0.90f, 3.5f, 20.0f, 7.0f, 1.5f, 0.3f, 1.0f },
	// ACID: stickier, longer lasting film
	{ 14.0f, 3.0f, 0.35f, 0.15f, 0.05f, 0.30f, 1.00f, 1.8f, 0.95f, 1.40f,
	  0.60f, 5.0f, 22.0f, 20.0f, 0.9f, 0.45f, 1.25f },
};
static_assert(sizeof(LensWater::ProfileParams) == 17 * sizeof(float), "profile blend layout");

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

Vec2 SurfaceAffinityGradient(Vec2 p)
{
	const float e = 0.25f / kNoiseFrequency;
	return {
		(SurfaceAffinity({ p.x + e, p.y }) - SurfaceAffinity({ p.x - e, p.y })) / (2.0f * e),
		(SurfaceAffinity({ p.x, p.y + e }) - SurfaceAffinity({ p.x, p.y - e })) / (2.0f * e) };
}

float ReferenceSize(const Drop &drop) { return drop.radius / kRefRadius; }

// Impact animation: rapid spread, recoil, relax (radius multiplier and
// lopsided irregularity).
void ImpactShape(const Drop &drop, float &scale, float &irregularity)
{
	const float a = drop.stateAge;
	scale = 1.0f;
	irregularity = 0.0f;
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

const LensWater::ProfileParams &LensWater::GetProfileParams(Profile profile)
{
	return s_profiles[(profile > PROFILE_AUTO && profile < PROFILE_COUNT) ? profile : PROFILE_NORMAL];
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
	drops.reserve(256);
	micro.reserve(512);
	sheets.reserve(16);
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
	return !drops.empty() || !micro.empty() || !sheets.empty() || filmVisible || filmDirty
		|| !sprays.empty() || !pendingEvents.empty();
}

int LensWater::MaxInstances(const Params &p) const
{
	return std::max(p.maxDrops, 1) + std::max(p.maxMicro, 0) + std::max(p.maxSheets, 0);
}

void LensWater::BlendProfile(float dt, Profile target)
{
	const ProfileParams &goal = GetProfileParams(target);
	activeProfile = target;
	if (!currentValid)
	{
		current = goal;
		currentValid = true;
		return;
	}
	// weather changes crossfade over about a second
	const float k = 1.0f - std::exp(-dt);
	float *c = &current.microRate;
	const float *g = &goal.microRate;
	for (int i = 0; i < 17; ++i)
		c[i] += (g[i] - c[i]) * k;
}

bool LensWater::Update(float dt, const Input &input, const Params &p)
{
	const auto start = std::chrono::steady_clock::now();
	params = p;
	lastInput = input;

	ProcessEvents();
	BlendProfile(dt, profileOverride != PROFILE_AUTO ? profileOverride : input.weather);

	agentAccumulator = std::min(agentAccumulator + std::max(dt, 0.0f), 0.2f);
	while (agentAccumulator >= kAgentStep)
	{
		Step(kAgentStep, input);
		agentAccumulator -= kAgentStep;
	}
	interpolation = agentAccumulator / kAgentStep;

	fieldAccumulator = std::min(fieldAccumulator + std::max(dt, 0.0f), 0.2f);
	while (fieldAccumulator >= kFieldStep)
	{
		DecayField(kFieldStep);
		fieldAccumulator -= kFieldStep;
	}

	lastUpdateMicroseconds = std::chrono::duration<float, std::micro>(
		std::chrono::steady_clock::now() - start).count();

	const bool incoming = input.exposed > 0.0f && input.intensity > 0.0f && p.density > 0.0f;
	return Active() || incoming;
}

void LensWater::Step(float dt, const Input &input)
{
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
		m.radius -= evaporation * 1.5f * dt;
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
	const float noise = 1.0f + kPinNoise * (SurfaceAffinity(drop.pos) * 2.0f - 1.0f);
	const float seedAdhesion = 0.92f + 0.16f * ((drop.seed >> 8) & 0xffu) / 255.0f;
	const float wet = Saturate(SampleField(drop.pos, 0));
	const float pin = kPin * rn * noise * Lerp(1.0f, kWetPin, wet) * seedAdhesion
		* std::max(params.pinning, 0.01f) * current.adhesion;
	drop.pinRatio = drop.mass * g / std::max(pin, 1e-6f);

	if (drop.state == STATE_IMPACT)
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
	drop.prevPos = drop.pos;

	// evaporation: a reference drop dries in beadLifetime / 0.6
	const float evaporation = kRefRadius * 0.6f / std::max(current.beadLifetime, 0.5f);
	drop.radius -= evaporation * dt;
	if (drop.radius < kMinRadius)
	{
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
		accel = accel + SurfaceAffinityGradient(drop.pos) * (kPathNoise * g);
		accel = accel + FieldGradient(drop.pos, 0) * kWetAttract;
		drop.vel = drop.vel + accel * dt;
		drop.vel = drop.vel * std::exp(-(drop.type == DROP_FLOW ? kFlowDrag : kDrag) * dt);
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
	drop.pos = drop.pos + drop.vel * dt;

	const float ds = speed * dt;
	if (ds > 0.0f)
	{
		// Trail: a small mass fraction becomes thin film along the swept
		// segment; after this the trail belongs to the field and only decays.
		const float deposit = 1.0f - std::exp(-kDeposit * ds / std::max(drop.radius, 1e-5f)
			* (drop.type == DROP_FLOW ? 0.7f : 1.0f));
		drop.mass *= 1.0f - deposit;
		const float filmAmount = current.filmDeposit * Saturate(0.45f + 0.4f * rn)
			* (drop.type == DROP_FLOW ? 1.4f : 1.0f);
		StampCapsule(drop.prevPos, drop.pos, drop.radius * params.dropSize * 0.75f, 1.0f, filmAmount);

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
		d.state = STATE_SETTLED;
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

void LensWater::SpawnMicro(Vec2 pos, float radius)
{
	// an impact on an existing drop feeds it
	for (Drop &drop : drops)
	{
		const Vec2 d = drop.pos - pos;
		const float contact = drop.radius * params.dropSize;
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
	m.state = STATE_SETTLED;
	m.stateAge = m.mergeAge = 1.0f;
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
				kRefRadius * (0.12f + 0.15f * Random01()));
		}
	}
	Evict();
}

void LensWater::SpawnFlow(Vec2 pos, float rn, Vec2 vel)
{
	Drop &drop = AddDrop(pos, rn * kRefRadius, DROP_FLOW);
	drop.moving = true;
	drop.vel = vel;
	drop.state = STATE_SETTLING;
	drop.stateAge = 0.15f;
	StampDisc(pos, drop.radius * params.dropSize * 1.5f, 1.0f, current.filmDeposit, false);
	++spawnCount;
	Evict();
}

void LensWater::SpawnSheet(Vec2 pos, float strength, float scale)
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
	sheet.speed = (0.7f + 0.7f * Random01()) * current.speed * std::max(Length(g), 0.3f);
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
		StampCapsule(sheet.prevPos, sheet.pos, sheet.width * params.dropSize * 0.45f, 1.0f,
			0.45f * sheet.strength * current.filmDeposit);
	}
	sheets.erase(std::remove_if(sheets.begin(), sheets.end(),
		[](const Sheet &s) { return s.age >= s.lifetime; }), sheets.end());
}

void LensWater::Spawn(float dt, const Input &input)
{
	// Rain hits a lens facing into it more often; spray and turbulence keep
	// a baseline.
	const float facing = Lerp(0.2f, 1.0f, std::pow(Saturate(input.facing), 1.5f));
	const float rate = std::max(params.density, 0.0f) * Saturate(input.exposed) * facing
		* (input.intensity > 0.0f ? 1.0f : 0.0f);

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
		fire(timerMicro, current.microRate * rate, [&]() {
			SpawnMicro(RandomPosition(false), kRefRadius * (0.1f + 0.2f * Random01()));
		});
		fire(timerNormal, current.normalRate * rate, [&]() {
			const float rn = Lerp(current.sizeMin, current.sizeMax, std::pow(Random01(), current.sizeBeta));
			SpawnRainDrop(rn, false, nullptr, 0.0f);
		});
		fire(timerLarge, current.largeRate * rate, [&]() {
			SpawnRainDrop(Lerp(current.largeMin, current.largeMax, Random01()), true, nullptr, 0.0f);
		});
		fire(timerFlow, current.flowRate * heavyFlow * rate, [&]() {
			Vec2 pos = RandomPosition(true);
			// runs start high on the lens so they cross it
			pos = pos - Normalize(input.gravity, { 0.0f, -1.0f }) * (0.25f * Random01());
			SpawnFlow(pos, 1.4f + 0.6f * Random01(), input.gravity * (0.25f * current.speed));
		});
		fire(timerSheet, current.sheetRate * heavyFlow * rate, [&]() {
			SpawnSheet(RandomPosition(true), 0.6f + 0.4f * Random01(), 1.0f);
		});
	}

	for (Spray &spray : sprays)
	{
		spray.remaining -= dt;
		const float s = spray.strength;
		if (Random01() < std::min(12.0f * s * dt, 0.5f))
		{
			const Vec2 at = SideBiasedPosition(spray.dir, 0.35f);
			SpawnRainDrop(0.3f + 0.5f * Random01(), false, &at, 0.05f);
		}
		if (Random01() < std::min(30.0f * s * dt, 0.8f))
			SpawnMicro(SideBiasedPosition(spray.dir, 0.4f), kRefRadius * (0.1f + 0.2f * Random01()));
		if (Random01() < std::min(0.6f * s * dt, 0.2f))
		{
			const Vec2 at = SideBiasedPosition(spray.dir, 0.3f);
			SpawnRainDrop(1.1f + 0.5f * Random01(), true, &at, 0.02f);
			StampDisc(at, 0.06f, 1.0f, 0.35f * s, false);
		}
	}
	sprays.erase(std::remove_if(sprays.begin(), sprays.end(),
		[](const Spray &s) { return s.remaining <= 0.0f; }), sprays.end());
}

void LensWater::ProcessEvents()
{
	if (pendingEvents.empty())
		return;
	std::vector<Event> events;
	events.swap(pendingEvents);
	for (const Event &event : events)
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
			StampDisc(center, 0.1f + 0.08f * s, 1.0f, 0.5f * s, true);
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
					kRefRadius * (0.1f + 0.2f * Random01()));
			}
			break;
		}

		case EVENT_SPRAY:
			if (sprays.size() < 4)
				sprays.push_back({ s, event.duration > 0.0f ? event.duration : 2.5f, event.dir });
			break;

		case EVENT_EMERGE:
		{
			// Leaving water: broad film immediately, then breakup into sheets,
			// several large streams and residual beads.
			StampDisc({ 0.0f, 0.0f }, 2.0f, 1.0f, 0.8f * std::min(s, 1.25f), true);
			const Vec2 down = Normalize(lastInput.gravity, { 0.0f, -1.0f });
			const int numSheets = 3 + (int)(2.0f * s);
			for (int i = 0; i < numSheets; ++i)
			{
				const Vec2 at = { (Random01() - 0.5f) * aspect * 0.9f, (Random01() - 0.5f) * 0.6f };
				SpawnSheet(at - down * 0.2f, 0.8f + 0.2f * Random01(), 1.3f);
			}
			const int flows = 3 + (int)(4.0f * s);
			for (int i = 0; i < flows; ++i)
			{
				const Vec2 at = { (Random01() - 0.5f) * aspect * 0.9f, (Random01() - 0.5f) * 0.8f };
				SpawnFlow(at - down * 0.2f, 1.5f + 0.7f * Random01(), lastInput.gravity * 0.2f);
			}
			const int beads = 15 + (int)(25.0f * s);
			for (int i = 0; i < beads; ++i)
			{
				Drop &bead = AddDrop(RandomPosition(false), kRefRadius * (0.3f + 0.7f * Random01()), DROP_BEAD);
				bead.state = STATE_SETTLED;
				++spawnCount;
			}
			Evict();
			break;
		}
		}
	}
}

/*
Film / wetness field. R = wetness (path affinity, slow decay), G = optical
thin film (fast decay). Deposits only add where new water passes; decay
continues unchanged under cover.
*/
void LensWater::DecayField(float dt)
{
	if (filmMax <= 0.0f)
		return;
	const float wetK = std::exp(-dt / std::max(current.wetTau * std::max(params.wetDecay, 0.01f), 0.05f));
	const float filmK = std::exp(-dt / std::max(current.filmTau * std::max(params.filmDecay, 0.01f), 0.05f));
	float maxWet = 0.0f, maxFilm = 0.0f;
	float *cell = film.data();
	const size_t count = film.size() / 2;
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
	const float maxValue = std::max(maxWet, maxFilm) >= 1e-3f ? std::max(maxWet, maxFilm) : 0.0f;
	filmMax = maxValue;
	filmVisible = maxFilm > 0.01f;
	filmDirty = true;
}

void LensWater::StampCapsule(Vec2 a, Vec2 b, float radius, float wet, float filmAmount)
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
		cell[0] = std::max(cell[0], wet * falloff);
		cell[1] = std::max(cell[1], filmAmount * falloff);
		touched = true;
	}
	if (touched)
	{
		filmMax = std::max(filmMax, std::max(wet, filmAmount));
		filmDirty = true;
		if (filmAmount > 0.01f)
			filmVisible = true;
	}
}

void LensWater::StampDisc(Vec2 center, float radius, float wet, float filmAmount, bool noisy)
{
	if (!noisy)
	{
		StampCapsule(center, center, radius, wet, filmAmount);
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
	const float jitter = Random01() * 100.0f;
	for (int y = y0; y <= y1; ++y)
	for (int x = x0; x <= x1; ++x)
	{
		const float d = Length(Vec2{ x - cc.x, y - cc.y }) / std::max(rc, 1e-4f);
		if (d >= 1.0f)
			continue;
		const Vec2 lens = { (x + 0.5f) / h - aspect * 0.5f, (y + 0.5f) / h - 0.5f };
		const float n = SurfaceAffinity({ lens.x * 2.3f + jitter, lens.y * 2.3f });
		const float falloff = (1.0f - Smoothstep(0.5f, 1.0f, d)) * (0.55f + 0.45f * n);
		float *cell = &film[((size_t)y * filmWidth + x) * 2];
		cell[0] = std::max(cell[0], wet * falloff);
		cell[1] = std::max(cell[1], filmAmount * falloff);
	}
	filmMax = std::max(filmMax, std::max(wet, filmAmount));
	filmDirty = true;
	if (filmAmount > 0.01f)
		filmVisible = true;
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
		write(0, p.x, p.y, radius, 1.0f);
		write(1, axis.x, axis.y, tail, (float)drop.type);
		write(2, lobe.x, lobe.y, lobeRadius, (drop.seed & 0xffffu) / 65535.0f);
		write(3, drop.pinRatio, (float)drop.state, drop.moving ? 1.0f : 0.0f, irregularity);
		++index;
	}
	for (const Drop &m : micro)
	{
		if (index >= n)
			break;
		write(0, m.pos.x, m.pos.y, m.radius * dropSize, 0.85f);
		write(1, 0.0f, -1.0f, 1.0f, (float)DROP_MICRO);
		write(2, 0.0f, 0.0f, 0.0f, (m.seed & 0xffffu) / 65535.0f);
		write(3, 0.0f, (float)STATE_SETTLED, 0.0f, 0.0f);
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
	return s;
}

} // namespace lenswater
