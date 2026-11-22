// Copyright (C) 2026 OpenJK contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>

namespace FxVisibility {
inline bool BehindQuad(const float *offset, const float *forward,
	float sizeStart, float sizeEnd, float currentRadius)
{
	// Waves can exceed both endpoints: end + (start-end)*cos(t).
	const float radius = std::max(std::fabs(currentRadius),
		std::fabs(sizeEnd) + std::fabs(sizeStart - sizeEnd));
	const float depth = offset[0] * forward[0] + offset[1] * forward[1] + offset[2] * forward[2];
	return depth < -1.414214f * radius;
}
}
