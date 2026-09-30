/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Optional persistent water state for r_rainLensSimulation. The CPU updates
// a small lens-space lattice at 30 Hz; the existing GPU lens field converts
// its mass and wetness into the shared refraction/bloom representation.

#include "tr_local.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
struct LensCell {
	float mass, vx, vy, wet;
};
static_assert(sizeof(LensCell) == sizeof(float) * 4, "LensCell upload layout");

constexpr float kStep = 1.0f / 30.0f;
static std::vector<LensCell> s_cells;
static std::vector<LensCell> s_next;
static int s_width, s_height;
static float s_accumulator, s_spawnCarry;
static uint32_t s_rng = 0x8f6a92d1u;
static qboolean s_dirty, s_visible;

static float Random01()
{
	s_rng ^= s_rng << 13;
	s_rng ^= s_rng >> 17;
	s_rng ^= s_rng << 5;
	return (s_rng & 0x00ffffffu) * (1.0f / 16777216.0f);
}

static void AddMass(int x, int y, float mass, float vx, float vy)
{
	if (x < 0 || x >= s_width || y < 0 || y >= s_height || mass <= 0.0f)
		return;
	LensCell &out = s_next[y * s_width + x];
	out.mass += mass;
	out.vx += mass * vx; // momentum until the normalization below
	out.vy += mass * vy;
}

static void Step(float exposure)
{
	std::fill(s_next.begin(), s_next.end(), LensCell{});
	const float density = r_rainLensDebug->integer ? 1.0f :
		Com_Clamp(0.0f, 1.0f, r_rainLensDensity->value);
	// viewaxis[1] points left. Lens X points right; lens Y points up.
	const float gravityX = backEnd.refdef.viewaxis[1][2];
	const float gravityY = -backEnd.refdef.viewaxis[2][2];
	const float massDecay = expf(-kStep * (exposure > 0.0f ? 0.10f : 0.55f));
	const float wetDecay = expf(-kStep * (exposure > 0.0f ? 0.35f : 0.80f));

	for (int y = 0; y < s_height; ++y)
	for (int x = 0; x < s_width; ++x)
	{
		const int index = y * s_width + x;
		const LensCell &cell = s_cells[index];
		const float mass = cell.mass * massDecay;
		s_next[index].wet = cell.wet * wetDecay;
		if (mass < 0.0005f)
			continue;

		// Small isolated beads pin to the surface. A wet path lowers the
		// threshold, so a large drop follows and merges with existing water.
		const float mobility = Com_Clamp(0.0f, 1.0f,
			(mass + 0.35f * cell.wet - 0.11f) / 0.42f);
		const float damping = expf(-kStep * (6.0f - 4.2f * mobility));
		float vx = (cell.vx + gravityX * 100.0f * mobility * kStep) * damping;
		float vy = (cell.vy + gravityY * 100.0f * mobility * kStep) * damping;
		const float speed = sqrtf(vx * vx + vy * vy);
		if (speed > 35.0f)
		{
			vx *= 35.0f / speed;
			vy *= 35.0f / speed;
		}

		// Conservative forward scatter. Flow off the lens leaves the grid;
		// neighboring deposits merge simply by adding their mass and momentum.
		const float fx = x + vx * kStep;
		const float fy = y + vy * kStep;
		const int ix = (int)floorf(fx);
		const int iy = (int)floorf(fy);
		const float ax = fx - ix, ay = fy - iy;
		AddMass(ix,     iy,     mass * (1.0f - ax) * (1.0f - ay), vx, vy);
		AddMass(ix + 1, iy,     mass * ax * (1.0f - ay), vx, vy);
		AddMass(ix,     iy + 1, mass * (1.0f - ax) * ay, vx, vy);
		AddMass(ix + 1, iy + 1, mass * ax * ay, vx, vy);
	}

	// The flux is reduced when the lens points away from falling rain, but
	// retains a baseline for wind/splash that the weather system does not expose.
	const float facing = Com_Clamp(0.0f, 1.0f, backEnd.refdef.viewaxis[0][2]);
	const float flux = 0.35f + 0.65f * facing;
	s_spawnCarry += (10.0f + 35.0f * density) * density * exposure * flux * kStep;
	const int impacts = (int)s_spawnCarry;
	s_spawnCarry -= impacts;
	for (int impact = 0; impact < impacts; ++impact)
	{
		const float cx = Random01() * s_width;
		const float cy = Random01() * s_height;
		const float radius = (1.8f + 2.5f * Random01()) *
			Com_Clamp(0.25f, 4.0f, r_rainLensDropSize->value);
		const float amount = 0.30f + 0.50f * Random01();
		const int minX = Q_max(0, (int)floorf(cx - radius * 2.0f));
		const int maxX = Q_min(s_width - 1, (int)ceilf(cx + radius * 2.0f));
		const int minY = Q_max(0, (int)floorf(cy - radius * 2.0f));
		const int maxY = Q_min(s_height - 1, (int)ceilf(cy + radius * 2.0f));
		for (int y = minY; y <= maxY; ++y)
		for (int x = minX; x <= maxX; ++x)
		{
			const float dx = (x + 0.5f - cx) / radius;
			const float dy = (y + 0.5f - cy) / radius;
			const float mass = amount * expf(-1.5f * (dx * dx + dy * dy));
			s_next[y * s_width + x].mass += mass;
		}
	}

	s_visible = qfalse;
	for (LensCell &cell : s_next)
	{
		if (cell.mass > 0.0005f)
		{
			cell.vx /= cell.mass;
			cell.vy /= cell.mass;
			cell.wet = Q_max(cell.wet, Q_min(cell.mass * 0.7f, 1.0f));
		}
		else
		{
			cell.mass = cell.vx = cell.vy = 0.0f;
		}
		if (cell.mass > 0.025f || cell.wet > 0.035f)
			s_visible = qtrue;
	}
	s_cells.swap(s_next);
	s_dirty = qtrue;
}
} // namespace

void R_RainLensSimInit(int width, int height)
{
	s_width = width;
	s_height = height;
	s_cells.assign(width * height, LensCell{});
	s_next.assign(width * height, LensCell{});
	R_RainLensSimClear();
}

void R_RainLensSimClear(void)
{
	std::fill(s_cells.begin(), s_cells.end(), LensCell{});
	s_accumulator = s_spawnCarry = 0.0f;
	s_rng = 0x8f6a92d1u;
	s_visible = qfalse;
	s_dirty = qtrue;
}

qboolean RB_RainLensSimUpdate(float dt, float exposure)
{
	if (s_cells.empty())
		return qfalse;
	// Once all visible water has drained, an indoor frame needs no lattice
	// scan or texture upload. Tiny remnants cannot be seen in the field.
	if (exposure <= 0.0f && !s_visible)
	{
		s_accumulator = s_spawnCarry = 0.0f;
		return qfalse;
	}
	s_accumulator = Q_min(s_accumulator + Q_max(dt, 0.0f), 0.2f);
	while (s_accumulator >= kStep)
	{
		Step(exposure);
		s_accumulator -= kStep;
	}
	return (qboolean)(s_visible || (exposure > 0.0f &&
		(r_rainLensDebug->integer || r_rainLensDensity->value > 0.0f)));
}

qboolean RB_RainLensSimUpload(void)
{
	if (!s_dirty || !tr.rainLensSimImage || s_cells.empty())
		return qfalse;
	GL_BindToTMU(tr.rainLensSimImage, TB_COLORMAP);
	qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_width, s_height,
		GL_RGBA, GL_FLOAT, s_cells.data());
	s_dirty = qfalse;
	return qtrue;
}
