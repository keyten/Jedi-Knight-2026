#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Geometry-only weights; the authored lighting normals and bone weights remain
// unchanged. At a duplicated vertex with incompatible normals/skinning, suppress
// its tangent-plane contribution on BOTH sides. Shared edges then evaluate the
// same polynomial, preserving hard creases instead of opening a tessellation gap.
inline int R_BuildPhongCreaseWeights(const unsigned char *data, int count, size_t stride,
	size_t positionOffset, size_t normalOffset, size_t boneOffset, size_t weightOffset,
	float *result)
{
	struct Vertex
	{
		int index;
		int64_t position[3];
		uint32_t normal;
		unsigned char bones[4], weights[4];
	};
	std::vector<Vertex> vertices(count);
	for (int i = 0; i < count; ++i)
	{
		Vertex& v = vertices[i];
		v.index = i;
		const unsigned char *source = data + i * stride;
		float position[3];
		std::memcpy(position, source + positionOffset, sizeof(position));
		for (int axis = 0; axis < 3; ++axis)
			v.position[axis] = (int64_t)std::llround(position[axis] * 1024.0);
		std::memcpy(&v.normal, source + normalOffset, sizeof(v.normal));
		std::memcpy(v.bones, source + boneOffset, sizeof(v.bones));
		std::memcpy(v.weights, source + weightOffset, sizeof(v.weights));
		result[i] = 1.0f;
	}
	std::sort(vertices.begin(), vertices.end(), [](const Vertex& a, const Vertex& b) {
		for (int axis = 0; axis < 3; ++axis)
			if (a.position[axis] != b.position[axis]) return a.position[axis] < b.position[axis];
		return a.index < b.index;
	});
	int creases = 0;
	for (int first = 0; first < count; )
	{
		int end = first + 1;
		bool discontinuity = false;
		while (end < count && !std::memcmp(vertices[first].position, vertices[end].position,
			sizeof(vertices[first].position)))
		{
			discontinuity |= vertices[first].normal != vertices[end].normal ||
				std::memcmp(vertices[first].bones, vertices[end].bones, 4) != 0 ||
				std::memcmp(vertices[first].weights, vertices[end].weights, 4) != 0;
			++end;
		}
		if (discontinuity)
			for (int i = first; i < end; ++i)
			{
				result[vertices[i].index] = 0.0f;
				++creases;
			}
		first = end;
	}
	return creases;
}
