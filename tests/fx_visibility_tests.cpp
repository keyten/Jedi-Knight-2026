// Property checks against rotated quad corners, including size waves.
#include "fx/FxVisibility.h"
#include <cassert>
#include <cstdio>
#include <random>

int main()
{
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> value(-1.0f, 1.0f);
	int visible = 0, rejected = 0;
	for (int i = 0; i < 20000; ++i)
	{
		const float angle = value(rng) * 3.14159265f;
		const float start = value(rng) * 40, end = value(rng) * 40;
		const float size = end + (start - end) * std::cos(value(rng) * 3.14159265f);
		const float center[] = {value(rng) * 150, value(rng) * 150, value(rng) * 150};
		const float forward[] = {1, 0, 0};
		bool anyVisible = false;
		for (int a = -1; a <= 1; a += 2)
			for (int b = -1; b <= 1; b += 2)
				anyVisible |= center[0] + size * (a * std::cos(angle) - b * std::sin(angle)) > 0;
		const bool culled = FxVisibility::BehindQuad(center, forward, start, end, 0);
		assert(!culled || !anyVisible);
		visible += anyVisible;
		rejected += culled;
	}
	assert(visible > 1000 && rejected > 1000);
	std::puts("PASS: 20000 oriented-quad visibility/size-wave cases");
}
