// Legacy baked fog arithmetic. No source fitting, canonical assets or engine state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Rend2BakedLighting
{
inline float Clamp01(float x)
{
	return std::max(0.0f, std::min(1.0f, x));
}
inline void Split(float ambient, float direct, float fraction, bool hdr, float &baseline,
				  float &sun)
{
	const float a = std::max(ambient, 0.0f), d = std::max(direct, 0.0f);
	const float legacy = hdr ? a + d : std::max(a, d);
	sun = std::min(Clamp01(fraction) * d, legacy);
	baseline = legacy - sun;
}

// Retained general packing utility: finite saturation, round to nearest/even.
// Matches the previous baked B/S upload, including half subnormal boundaries.
inline uint16_t FloatToHalf(float f)
{
	uint32_t x;
	std::memcpy(&x, &f, sizeof(x));
	const uint16_t sign = uint16_t((x >> 16) & 0x8000);
	x &= 0x7fffffff;
	if (x > 0x7f800000)
		return uint16_t(sign | 0x7e00);
	if (x >= 0x477ff000)
		return uint16_t(sign | 0x7bff);
	if (x < 0x38800000)
	{
		if (x < 0x33000000)
			return sign;
		const uint32_t m = (x & 0x7fffff) | 0x800000, shift = 126 - (x >> 23);
		uint32_t h = m >> shift;
		const uint32_t rem = m & ((1u << shift) - 1), halfway = 1u << (shift - 1);
		if (rem > halfway || (rem == halfway && (h & 1)))
			++h;
		return uint16_t(sign | h);
	}
	uint32_t h = (x - 0x38000000) >> 13;
	const uint32_t rem = x & 0x1fff;
	if (rem > 0x1000 || (rem == 0x1000 && (h & 1)))
		++h;
	return uint16_t(sign | h);
}
} // namespace Rend2BakedLighting
