// Shared emissive surface geometry. No renderer, image handles or physical units.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Emission
{
struct Triangle
{
	uint32_t material = 0, surface = 0;
	float v[3][3] = {}, st[3][2] = {}, normal[3] = {}, area = 0;
};
struct Mask
{
	uint32_t width = 0, height = 0;
	bool clamp = false;
	std::vector<float> rgb; // source-decoded relative linear RGB, not nits
	float average[3] = {};
};
struct Point
{
	float p[3] = {};
};
struct Blob
{
	std::vector<Point> points;
	float power[3] = {}, peak[3] = {}, area = 0;
	bool sampled = false;
};
struct Rectangle
{
	float center[3] = {}, right[3] = {}, up[3] = {}, halfWidth = 0, halfHeight = 0;
};
// Legacy scanner quantization retained explicitly; no merge merely by colour.
std::vector<std::vector<int>> Groups(const std::vector<Triangle> &, bool sourceConnected = false);
// Caller validates finite unit normal, nonempty points and bounded counts.
void FitRectangle(const std::vector<const float *> &, const float *normal, float margin,
				  Rectangle &);
// Same UV barycentrics, area weighting, threshold and 8-neighbour blobs as LTC.
// No minimum emitter size: thin tubes and small fixtures survive this boundary.
Error Sample(const std::vector<Triangle> &, const std::vector<int> &, const Mask *,
			 bool staticCoords, float threshold, uint32_t maxAxis, const ReadContext &,
			 std::vector<Blob> &);
} // namespace Emission
} // namespace Core
} // namespace Rend2LightRecon
