// Independent legacy B/S arithmetic and general half packing; no renderer/GL.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../../shared/rd-rend2/tr_bakedlighting.h"
#include "../../shared/rd-rend2/tr_pbr_contract.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks = 0;
static void Check(bool value)
{
	++checks;
	if (!value) { std::fprintf(stderr, "baked-light check %u failed\n", checks); std::exit(1); }
}
static float HalfValue(unsigned h)
{
	const unsigned e = (h >> 10) & 31, m = h & 1023;
	const float value = e ? std::ldexp(float(1024 + m), int(e) - 25) : std::ldexp(float(m), -24);
	return (h & 0x8000) ? -value : value;
}
int main()
{
	for (bool hdr : {false, true})
		for (unsigned a = 0; a < 256; ++a)
			for (unsigned d = 0; d < 256; ++d)
				for (float f : {0.0f, .2f, .5f, 1.0f})
				{
					float b, s;
					const float A = a / 255.0f, D = d / 255.0f;
					Rend2BakedLighting::Split(A, D, f, hdr, b, s);
					const float legacy = hdr ? A + D : std::max(A, D);
					Check(b >= 0 && s >= 0 && std::abs(b + s - legacy) < 3e-7f);
					Check(std::abs(s - f * D) < 3e-7f);
				}
	// The retained general utility is exhaustively checked at every finite half.
	for (unsigned h = 0; h < 65536; ++h)
		if (((h >> 10) & 31) != 31)
			Check(Rend2BakedLighting::FloatToHalf(HalfValue(h)) == h);
	Check(Rend2BakedLighting::FloatToHalf(1.00048828125f) == 0x3c00); // tie -> even
	Check(Rend2BakedLighting::FloatToHalf(1.00146484375f) == 0x3c02);
	Check(Rend2BakedLighting::FloatToHalf(1e20f) == 0x7bff);
	float b, s;
	Rend2BakedLighting::Split(10, 20, .2f, true, b, s);
	Check(std::abs(b - 26) < 1e-5f && std::abs(s - 4) < 1e-5f);
	Rend2BakedLighting::Split(-10, -20, 1, false, b, s);
	Check(b == 0 && s == 0);
	// Map presentation calibration survives without any source reconstruction.
	for (float p50 : {.01f, .1f, 1.0f, 10.0f})
		Check(std::isfinite(Rend2PBR::LegacyMapCalibration(p50, p50 * 2)) &&
			  Rend2PBR::LegacyMapCalibration(p50, p50 * 2) > 0);
	std::printf("PASS: %u independent baked fog/PBR/half checks\n", checks);
}
