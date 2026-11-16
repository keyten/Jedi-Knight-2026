#include "../../shared/rd-rend2/tr_phong_crease.h"
#include <cassert>
#include <cstdio>

struct Vertex
{
	float position[3];
	uint32_t normal;
	unsigned char bones[4], weights[4];
};

int main()
{
	Vertex vertices[] = {
		{{0, 0, 0}, 10, {0, 1, 0, 0}, {128, 127, 0, 0}},
		{{0, 0, 0}, 10, {0, 1, 0, 0}, {128, 127, 0, 0}}, // UV duplicate
		{{1, 0, 0}, 20, {0, 0, 0, 0}, {255, 0, 0, 0}},
		{{1, 0, 0}, 21, {0, 0, 0, 0}, {255, 0, 0, 0}}, // hard normal
		{{2, 0, 0}, 30, {0, 0, 0, 0}, {255, 0, 0, 0}},
		{{2, 0, 0}, 30, {1, 0, 0, 0}, {255, 0, 0, 0}}, // skinning seam
		{{3, 0, 0}, 40, {0, 0, 0, 0}, {255, 0, 0, 0}},
		{{3.0001f, 0, 0}, 41, {0, 0, 0, 0}, {255, 0, 0, 0}}, // export rounding
		{{4, 0, 0}, 50, {0, 0, 0, 0}, {255, 0, 0, 0}}, // ordinary smooth vertex
	};
	Vertex original[9];
	std::memcpy(original, vertices, sizeof(vertices));
	float weights[9];
	const int count = R_BuildPhongCreaseWeights((const unsigned char *)vertices, 9, sizeof(Vertex),
		offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, bones), offsetof(Vertex, weights), weights);
	assert(count == 6);
	assert(weights[0] == 1 && weights[1] == 1 && weights[8] == 1);
	for (int i = 2; i < 8; ++i) assert(weights[i] == 0);
	assert(!std::memcmp(original, vertices, sizeof(vertices)));
	std::puts("PASS: UV, hard-normal, skinning and export-rounding seam classification; original data unchanged");
}
