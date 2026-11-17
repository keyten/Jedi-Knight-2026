// C entry points of the ray traced shadow BVH (shared/rd-rend2/tr_rtbvh.cpp) for the Python GPU test
// (tools/rend2/test_rt_shadows_gl.py). Built by the test with g++ into a temporary DLL.
#include "tr_rtbvh.h"

#include <string.h>

#ifdef _WIN32
#define RT_EXPORT extern "C" __declspec(dllexport)
#else
#define RT_EXPORT extern "C"
#endif

// positions: 9 floats per triangle. Writes nodes (2 texels each) then triangles (3 texels each),
// 4 floats per texel; order[slot] = source triangle. Returns the texel count, -1 on failure.
RT_EXPORT int rt_build( const float *positions, int numTris, float *texels, int maxTexels,
	int *numNodes, int *order )
{
	rtBvh_t bvh;
	if ( !RtBvh_Build( positions, numTris, &bvh ) )
		return -1;
	const int nodes = (int)bvh.nodes.size();
	const int tris = (int)bvh.order.size();
	const int total = nodes * 2 + tris * 3;
	if ( total > maxTexels )
		return -1;
	memcpy( texels, bvh.nodes.data(), (size_t)nodes * sizeof( rtNode_t ) );
	if ( tris > 0 )
		RtBvh_PackTriangles( &bvh, positions, (rtTri_t *)( texels + (size_t)nodes * 8 ) );
	for ( int i = 0; i < tris; i++ )
		order[i] = (int)bvh.order[i];
	*numNodes = nodes;
	return total;
}

// boxes of packed nodes from packed triangles (triangle texel = numNodes * 2)
RT_EXPORT void rt_refit( float *texels, int numNodes )
{
	RtBvh_Refit( (rtNode_t *)texels, numNodes, (const rtTri_t *)( texels + (size_t)numNodes * 8 ) );
}
