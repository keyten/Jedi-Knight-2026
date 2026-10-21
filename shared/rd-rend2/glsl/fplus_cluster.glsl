/*[Compute]*/
// GL 4.3 Forward+ / froxel fog cluster build. Replaces the CPU count / prefix
// sum / fill and writes the same buffers the lighting shaders sample:
//   grid    RG32UI per cluster: absolute offset into the index list, count
//   indices R16UI light indexes (two per uint here)
// With FROXEL_LIGHT_LISTS the output is instead one R32UI buffer: a header per
// cluster (absolute first | count << 24), followed by 32-bit light indexes.
// One invocation per cluster walks the light boxes of the view in importance
// order (the CPU ranges: tile / slice box of the light), keeps the lights
// whose box contains the cluster and, for point and spot lights, whose view
// space bounding sphere touches the cluster's view space AABB, and stops at
// the per-cluster cap. The lists are a subset of the CPU lists (same order,
// same cap) that still holds every light reaching the cluster.
layout(local_size_x = 64) in;

// 0 tiles x, tiles y, slices, clusters | 1 tile size (px), viewport w, h, ranges |
// 2 slice scale, slice bias, near slice, far | 3 P[0], P[5], P[8], P[9] |
// 4 P[12], P[13], sphere test (0/1), lights per cluster |
// 5 grid base (texels), first range, index capacity (entries), fast reservation | 6, 7 unused
uniform vec4 u_FPlusCluster[8];

struct LightRange
{
	uvec4 box;		// x0 | x1 << 16, y0 | y1 << 16, z0 | z1 << 16, light | sphere << 16
	vec4 sphere;	// view space center, radius
};

#if defined(FROXEL_LIGHT_LISTS)
layout(std430, binding = 0) writeonly buffer FroxelLists { uint lists[]; };
#else
layout(std430, binding = 0) writeonly buffer FPlusGrid { uvec2 grid[]; };
layout(std430, binding = 1) writeonly buffer FPlusIndices { uint indices[]; };
#endif
layout(std430, binding = 2) readonly buffer FPlusRanges { LightRange ranges[]; };
layout(std430, binding = 3) buffer FPlusCounter { uint nextIndex; };

// A failed allocation must not advance the shared counter: otherwise one
// oversized cluster creates a permanent hole and can starve smaller lists
// that would still fit. CAS also keeps every successful span disjoint.
uint ReserveIndexes(uint requested, uint capacity, bool guaranteedToFit)
{
	if (guaranteedToFit)
		return atomicAdd(nextIndex, requested);
	uint current = atomicAdd(nextIndex, 0u);
	for (;;)
	{
		if (current > capacity || requested > capacity - current)
			return 0xffffffffu;
		uint observed = atomicCompSwap(nextIndex, current, current + requested);
		if (observed == current)
			return current;
		current = observed;
	}
}

// view depth range of a slice (FPlusSlice in lightall.glsl), widened a little:
// the fragment's slice comes from a logarithm
vec2 SliceDepths(int z)
{
	int slices = int(u_FPlusCluster[0].z);
	float nearSlice = u_FPlusCluster[2].z;
	float farZ = u_FPlusCluster[2].w;
#if defined(FROXEL_LIGHT_LISTS)
	// The injection's temporal sample may move half a froxel in every axis.
	// Cover z - 0.5 through z + 1.5; the last slice deliberately skips the
	// sphere test because its light list is also used by the tail beyond far.
	float k0 = float(z) - 0.5;
	float k1 = float(z) + 1.5;
	float d0 = k0 <= 0.0 ? 0.0 : nearSlice * pow(farZ / nearSlice, k0 / float(slices));
	float d1 = nearSlice * pow(farZ / nearSlice, k1 / float(slices));
	return vec2(d0, d1);
#else
	if (slices <= 1)
		return vec2(0.0, farZ * 1.01);
	if (z == 0)
		return vec2(0.0, nearSlice * 1.01);
	float scale = u_FPlusCluster[2].x, bias = u_FPlusCluster[2].y;
	float d0 = exp2((float(z - 1) - bias) / scale);
	float d1 = z == slices - 1 ? farZ : exp2((float(z) - bias) / scale);
	return vec2(d0 * 0.99, d1 * 1.01);
#endif
}

// eye space x (or y) of an NDC coordinate at view depth d (perspective P)
float EyeCoord(float ndc, float d, float p0, float p8, float p12)
{
	return ((ndc + p8) * d - p12) / p0;
}

void main()
{
	int c = int(gl_GlobalInvocationID.x);
	int tilesX = int(u_FPlusCluster[0].x), tilesY = int(u_FPlusCluster[0].y);
	int numClusters = int(u_FPlusCluster[0].w);
	if (c >= numClusters)
		return;
	int x = c % tilesX;
	int y = (c / tilesX) % tilesY;
	int z = c / (tilesX * tilesY);

	bool sphereTest = u_FPlusCluster[4].z > 0.5;
#if defined(FROXEL_LIGHT_LISTS)
	sphereTest = sphereTest && z != int(u_FPlusCluster[0].z) - 1;
#endif
	vec3 boxMin = vec3(0.0), boxMax = vec3(0.0);
	if (sphereTest)
	{
		float tileSize = u_FPlusCluster[1].x;
		vec2 viewport = u_FPlusCluster[1].yz;
#if defined(FROXEL_LIGHT_LISTS)
		// viewport is the froxel grid and tileSize is measured in froxels.
		// Half a froxel on either side covers the temporal injection jitter.
		vec2 ndc0 = (vec2(x, y) * tileSize - 0.5) / viewport * 2.0 - 1.0;
		vec2 ndc1 = (min(vec2(x + 1, y + 1) * tileSize, viewport) + 0.5) / viewport * 2.0 - 1.0;
#else
		vec2 ndc0 = vec2(x, y) * tileSize / viewport * 2.0 - 1.0;
		vec2 ndc1 = min(vec2(x + 1, y + 1) * tileSize, viewport) / viewport * 2.0 - 1.0;
#endif
		vec2 depths = SliceDepths(z);
		vec4 p = u_FPlusCluster[3];
		vec4 q = u_FPlusCluster[4];
		float ex[4] = float[4](
			EyeCoord(ndc0.x, depths.x, p.x, p.z, q.x), EyeCoord(ndc1.x, depths.x, p.x, p.z, q.x),
			EyeCoord(ndc0.x, depths.y, p.x, p.z, q.x), EyeCoord(ndc1.x, depths.y, p.x, p.z, q.x));
		float ey[4] = float[4](
			EyeCoord(ndc0.y, depths.x, p.y, p.w, q.y), EyeCoord(ndc1.y, depths.x, p.y, p.w, q.y),
			EyeCoord(ndc0.y, depths.y, p.y, p.w, q.y), EyeCoord(ndc1.y, depths.y, p.y, p.w, q.y));
		boxMin = vec3(min(min(ex[0], ex[1]), min(ex[2], ex[3])), min(min(ey[0], ey[1]), min(ey[2], ey[3])), -depths.y);
		boxMax = vec3(max(max(ex[0], ex[1]), max(ex[2], ex[3])), max(max(ey[0], ey[1]), max(ey[2], ey[3])), -depths.x);
	}

	uvec3 cell = uvec3(x, y, z);
	int first = int(u_FPlusCluster[5].y);
	int numRanges = int(u_FPlusCluster[1].w);
	uint cap = uint(u_FPlusCluster[4].w);

	// pass 1: count (importance order, capped)
	uint count = 0u;
	for (int i = 0; i < numRanges && count < cap; i++)
	{
		LightRange r = ranges[first + i];
		uvec3 lo = r.box.xyz & 0xffffu, hi = r.box.xyz >> 16;
		if (any(lessThan(cell, lo)) || any(greaterThan(cell, hi)))
			continue;
		if (sphereTest && (r.box.w >> 16) != 0u)
		{
			vec3 d = r.sphere.xyz - clamp(r.sphere.xyz, boxMin, boxMax);
			float reach = r.sphere.w * 1.001 + 0.01;
			if (dot(d, d) > reach * reach)
				continue;
		}
		count++;
	}

	// Allocate the compact result. Forward+ rounds to pairs because its R16UI
	// texture packs two entries per uint; froxel indexes are native uints.
	uint offset = 0u;
	if (count > 0u)
	{
#if defined(FROXEL_LIGHT_LISTS)
		uint allocated = count;
#else
		uint allocated = (count + 1u) & ~1u;
#endif
		offset = ReserveIndexes(allocated, uint(u_FPlusCluster[5].z),
			u_FPlusCluster[5].w > 0.5);
		if (offset == 0xffffffffu)
		{
			offset = 0u;
			count = 0u;		// index list full: this cluster gets no light
		}
	}
#if defined(FROXEL_LIGHT_LISTS)
	lists[c] = count > 0u ? uint(numClusters) + offset | (count << 24) : 0u;
#else
	grid[int(u_FPlusCluster[5].x) + c] = uvec2(offset, count);
#endif
	if (count == 0u)
		return;

	// pass 2: the same walk, writing the light indexes
	uint written = 0u;
	uint pending = 0u;
	for (int i = 0; i < numRanges && written < count; i++)
	{
		LightRange r = ranges[first + i];
		uvec3 lo = r.box.xyz & 0xffffu, hi = r.box.xyz >> 16;
		if (any(lessThan(cell, lo)) || any(greaterThan(cell, hi)))
			continue;
		if (sphereTest && (r.box.w >> 16) != 0u)
		{
			vec3 d = r.sphere.xyz - clamp(r.sphere.xyz, boxMin, boxMax);
			float reach = r.sphere.w * 1.001 + 0.01;
			if (dot(d, d) > reach * reach)
				continue;
		}
		uint light = r.box.w & 0xffffu;
#if defined(FROXEL_LIGHT_LISTS)
		lists[uint(numClusters) + offset + written] = light;
#else
		if ((written & 1u) == 0u)
			pending = light;
		else
			indices[(offset + written) >> 1] = pending | (light << 16);
#endif
		written++;
	}
#if !defined(FROXEL_LIGHT_LISTS)
	if ((written & 1u) != 0u)
		indices[(offset + written) >> 1] = pending;
#endif
}
