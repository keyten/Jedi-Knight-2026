/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Lens water (r_rainLens): persistent water on the virtual camera lens.
// docs/rend2-rain-lens.md describes the model.
//
// Pure CPU state, no GL and no renderer globals, so it can be exercised by a
// headless harness. tr_rainlens.cpp owns an instance, feeds it the weather /
// camera input and rasterises its state through the shared lens field.
//
//  - coherent water: explicit droplet agents (pin, depin, merge, residual
//    beads, flow heads), fixed 60 Hz step;
//  - thin persistent water: a low resolution wetness (R) / film (G) field,
//    decayed at 30 Hz;
//  - violent broad water: a few short art directed sheets;
//  - an artistic controller: weather profiles, Poisson timers, events.
//
// Lens space is normalised by the screen height: x = (u - 0.5) * aspect,
// y = v - 0.5, y up. Drop sizes are in the same units.

#pragma once

#include <cstdint>
#include <vector>

namespace lenswater {

struct Vec2
{
	float x, y;
};

enum DropType : uint8_t
{
	DROP_MICRO,
	DROP_BEAD,
	DROP_NORMAL,
	DROP_FLOW,
	DROP_RESIDUAL,
	DROP_SHEET,		// instance type only
};

enum DropState : uint8_t
{
	STATE_IMPACT,	// 0..50 ms spread, 50..150 ms recoil
	STATE_SETTLING,	// 150..300 ms relax
	STATE_SETTLED,
};

enum Profile
{
	PROFILE_AUTO,	// from the weather input
	PROFILE_LIGHT,
	PROFILE_NORMAL,
	PROFILE_HEAVY,
	PROFILE_ACID,
	PROFILE_COUNT
};

enum EventType
{
	EVENT_SPLASH,
	EVENT_SPRAY,
	EVENT_EMERGE,
	EVENT_SUBMERGE,
	EVENT_CLEAR,
};

struct Drop
{
	Vec2 pos, prevPos, vel;
	float mass;			// (radius / kRefRadius)^3
	float radius;		// lens units, before r_rainLensDropSize
	float age;
	float stateAge;		// since the impact
	float mergeAge;		// since the last merge
	Vec2 lobeOffset;	// absorbed drop relative to pos, relaxes after a merge
	Vec2 mainOffset;	// surviving drop's pre-merge position relative to pos
	float lobeRadius;
	float pinRatio;		// Fdrive / Fpin at the last step (debug)
	uint32_t seed;
	DropType type;
	DropState state;
	bool moving;
};

struct Sheet
{
	Vec2 pos, prevPos, dir;
	float width, length, strength, speed;
	float film;			// film stamp strength (weather profile or event preset)
	float age, lifetime;
	uint32_t seed;
};

struct Event
{
	EventType type;
	float strength;
	Vec2 dir;			// lens space side bias, zero = none
	float duration;		// SPRAY only
};

// user / developer tunables (cvars), per update
struct Params
{
	float density = 1.0f;
	float dropSize = 1.0f;		// geometric scale only
	float pinning = 1.0f;
	float merge = 1.0f;
	float filmDecay = 1.0f;
	float wetDecay = 1.0f;
	float heavyFlow = 1.0f;
	float peripheralBias = 1.0f;
	float inertia = 0.0f;		// camera acceleration response, 0 = off
	int maxDrops = 96;
	int maxMicro = 256;
	int maxSheets = 8;
	bool measure = false;		// take the Stats timings this update
};

// weather and camera, per update
struct Input
{
	float intensity = 0.0f;		// 0..2, rain particle count / 5000
	float exposed = 0.0f;		// 0..1, 0 under cover: no new rain
	float facing = 0.0f;		// 0..1, lens facing into the rain
	Profile weather = PROFILE_NORMAL;	// profile of the weather subtype
	Vec2 gravity = { 0.0f, -1.0f };	// tangential world gravity, |g| <= 1
	Vec2 wind = { 0.0f, 0.0f };		// lens space wind hint
	// continuous spray (map emitters), lens space side bias
	float sprayStrength = 0.0f;
	Vec2 sprayDir = { 0.0f, 0.0f };
	// camera acceleration on the lens plane (world units / s^2, x right, y up)
	Vec2 cameraAccel = { 0.0f, 0.0f };
};

// A world space water event seen from the camera: strength after distance
// falloff and facing (0 = out of range), lens space side it comes from.
float ResolveWorldEvent(const float origin[3], float radius, const float viewOrigin[3],
	const float forward[3], const float right[3], const float up[3], Vec2 &side);

struct Stats
{
	int drops, micro, beads, moving, flows, residuals, sheets;
	int filmWidth, filmHeight;
	bool filmVisible, filmDirty;
	Profile profile;
	float updateMicroseconds;	// whole update
	float agentMicroseconds;	// fixed agent steps incl. trail stamps
	float fieldMicroseconds;	// film / wetness decay
	float eventMicroseconds;	// event processing
	float sinceEvent;			// seconds since the last event
	int sprays;
	float continuousSpray;
};

// Instance record for the lens field raster, 4 RGBA32F texels
//  t0: pos.xy (lens), radius (lens, scaled), weight
//  t1: axis.xy (unit, motion / sheet direction), stretch (tail / length), type
//  t2: lobe offset.xy (lens, scaled), lobe radius (scaled), lopsided seed 0..1
//  t3: pin ratio, state, moving, sheet width (lens, scaled)
enum { INSTANCE_TEXELS = 4, INSTANCE_FLOATS = INSTANCE_TEXELS * 4 };

class LensWater
{
public:
	void Init(int filmWidth, int filmHeight);
	void Clear();
	void QueueEvent(const Event &event);
	void SetProfileOverride(Profile profile) { profileOverride = profile; }
	Profile GetProfileOverride() const { return profileOverride; }

	// Advances by dt seconds of game time; returns whether anything is
	// visible or will become visible (the pass can be skipped otherwise).
	bool Update(float dt, const Input &input, const Params &params);
	bool Active() const;

	// Writes up to maxInstances records (row major: INSTANCE_TEXELS rows of
	// numInstances texels, see above), interpolated between fixed steps.
	int BuildInstances(float *out, int maxInstances, float dropSize) const;
	int MaxInstances(const Params &params) const;

	// wetness / film field, 2 floats per cell
	const float *FilmData() const { return film.data(); }
	int FilmWidth() const { return filmWidth; }
	int FilmHeight() const { return filmHeight; }
	bool FilmDirty() const { return filmDirty; }
	bool FilmVisible() const { return filmVisible; }
	void ClearFilmDirty() { filmDirty = false; }

	Stats GetStats() const;

	// harness access
	std::vector<Drop> &Drops() { return drops; }
	std::vector<Drop> &Micro() { return micro; }
	std::vector<Sheet> &Sheets() { return sheets; }
	float Wetness(Vec2 p) const { return SampleField(p, 0); }
	float Film(Vec2 p) const { return SampleField(p, 1); }
	float Aspect() const { return aspect; }
	Drop &AddDrop(Vec2 pos, float radius, DropType type);
	int spawnCount = 0;	// drops born from rain / events since Clear

	struct ProfileParams
	{
		float microRate, normalRate, largeRate, flowRate, sheetRate;
		float sizeMin, sizeMax, sizeBeta;	// normal drops, in reference radii
		float largeMin, largeMax;
		float filmDeposit;		// film stamp strength
		float filmTau, wetTau;	// seconds
		float beadLifetime;		// seconds for a reference bead to evaporate
		float speed;
		float centerSpawn;		// spawn weight at the screen centre
		float adhesion;
		float tint[3];			// transmitted colour (acid rain)
		float refraction;		// distortion scale
	};
	static const ProfileParams &GetProfileParams(Profile profile);
	// rain intensity the profile is tuned for (standard weather particle count / 5000)
	static float NominalIntensity(Profile profile);
	// the crossfaded parameters in use
	const ProfileParams &Current() const { return current; }

private:
	struct Spray
	{
		float strength, remaining;
		Vec2 dir;
	};

	void Step(float dt, const Input &input);
	void DecayField(float dt);
	void Spawn(float dt, const Input &input);
	void SpraySpawn(float strength, Vec2 dir, float dt);
	void ProcessEvents();
	void SpawnRainDrop(float rn, bool large, const Vec2 *center, float spread);
	void SpawnMicro(Vec2 pos, float radius, bool impact);
	void SpawnFlow(Vec2 pos, float rn, Vec2 vel, float filmAmount);
	void SpawnSheet(Vec2 pos, float strength, float scale, float speed, float filmAmount);
	void StartEmerge(float strength);
	void UpdateEmerge(float dt);
	void StampEmergeFilm(float strength);
	void UpdateDrop(Drop &drop, float dt, const Input &input);
	void UpdatePin(Drop &drop, const Input &input) const;
	void MergeDrops(const Input &input);
	void AbsorbMicro();
	void UpdateSheets(float dt, const Input &input);
	void Evict();
	float Importance(const Drop &drop) const;
	Vec2 RandomPosition(bool obstructive);
	Vec2 SideBiasedPosition(Vec2 dir, float spread);
	void StampCapsule(Vec2 a, Vec2 b, float radius, float wet, float filmAmount, float coverage);
	void StampDisc(Vec2 center, float radius, float wet, float filmAmount, bool noisy);
	float SampleField(Vec2 p, int channel) const;
	float SampleAffinity(Vec2 p) const;
	Vec2 AffinityGradient(Vec2 p) const;
	float Detail(int x, int y, int ox, int oy) const;
	Vec2 FieldGradient(Vec2 p, int channel) const;
	float Random01();
	float ExpRandom();
	void BlendProfile(float dt, Profile target);

	std::vector<Drop> drops;
	std::vector<Drop> micro;
	std::vector<Sheet> sheets;
	std::vector<Spray> sprays;
	std::vector<Event> pendingEvents;
	std::vector<Event> processingEvents;	// swapped with pendingEvents, keeps capacity
	struct PendingResidual
	{
		Vec2 pos;
		float radius;
	};
	std::vector<PendingResidual> residualSpawns;	// born while drops are iterated
	std::vector<float> film;	// RG: wetness, film
	std::vector<float> affinity;	// SurfaceAffinity per film cell (static)
	std::vector<float> detail;		// tileable value noise per film cell (splash / emerge structure)
	int filmWidth = 0, filmHeight = 0;
	float aspect = 16.0f / 9.0f;
	bool filmDirty = false;
	bool filmVisible = false;
	float filmMax = 0.0f;

	Params params;
	ProfileParams current = {};
	bool currentValid = false;
	Profile activeProfile = PROFILE_NORMAL;
	Profile profileOverride = PROFILE_AUTO;

	float agentAccumulator = 0.0f;
	float fieldAccumulator = 0.0f;
	float interpolation = 0.0f;	// accumulator / step, for rendering
	// Poisson timers: remaining unit exponential time per event family
	float timerMicro = 0.0f, timerNormal = 0.0f, timerLarge = 0.0f;
	float timerFlow = 0.0f, timerSheet = 0.0f;
	uint32_t rng = 0x8f6a92d1u;
	uint32_t seedCounter = 1;
	float lastUpdateMicroseconds = 0.0f;
	float agentMicroseconds = 0.0f, fieldMicroseconds = 0.0f, eventMicroseconds = 0.0f;
	float sinceEvent = 1000.0f;
	Input lastInput;

	// Leaving water: a short choreographed transient rather than a burst.
	// Film first, sheets break it up, flow heads and rivulets follow, the
	// residual beads come from the moving water itself.
	struct EmergeState
	{
		bool active;
		float age, strength;
		int flowsLeft, rivuletsLeft, lateSheetsLeft;
		float nextFlow, nextRivulet, nextSheet;
		int detailX, detailY;	// breakup pattern offset, same as the film stamp
	};
	EmergeState emerge = {};
};

// reference drop radius (lens units): mass 1, the dry-glass depinning size
constexpr float kRefRadius = 0.02f;
constexpr float kAgentStep = 1.0f / 60.0f;
constexpr float kFieldStep = 1.0f / 30.0f;

} // namespace lenswater
