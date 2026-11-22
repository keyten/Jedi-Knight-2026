// Copyright (C) 2026 OpenJK contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ParticleOrder {
struct Entry { float depth; uint32_t owner, stage; };

inline bool Before(Entry a, Entry b)
{
	// Invalid custom content must not violate std::sort's strict weak ordering.
	const float az = std::isfinite(a.depth) ? a.depth : 0.0f;
	const float bz = std::isfinite(b.depth) ? b.depth : 0.0f;
	if (az != bz) return az > bz;
	if (a.owner != b.owner) return a.owner < b.owner;
	return a.stage < b.stage;
}

// The caller supplies one authored render layer at a time. Non-particle slots
// stay in place; complete multi-pass particles are ordered before render state.
template<class Lookup>
unsigned SortLayer(uint32_t *order, unsigned count, uint32_t *scratch, Lookup lookup)
{
	uint32_t *slots = scratch + count;
	unsigned particles = 0;
	for (unsigned i = 0; i < count; ++i)
		if (lookup(order[i]).owner) { slots[particles] = i; scratch[particles++] = order[i]; }
	std::sort(scratch, scratch + particles, [&](uint32_t a, uint32_t b) {
		const Entry ea = lookup(a), eb = lookup(b);
		if (Before(ea, eb)) return true;
		if (Before(eb, ea)) return false;
		return a < b;
	});
	for (unsigned i = 0; i < particles; ++i) order[slots[i]] = scratch[i];
	return particles;
}
}
