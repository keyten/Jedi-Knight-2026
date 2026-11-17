/*
===========================================================================
Ray traced shadows: BVH core.

GL-free and renderer-independent on purpose, so the builder, the refit and
the reference traversal can be compiled and tested on their own
(tools/rend2/test_rt_bvh.cpp).  The GLSL traversal in glsl/rt_shadow_trace.glsl
walks exactly the layout produced here.

Layout (everything is float so it can live in one RGBA32F texture buffer):

  node (2 texels)      : min.xyz, a | max.xyz, b
      a = -1            -> inner node, left child is the next node
      a >= 0            -> leaf, a = first triangle slot
      b = skip * 8 + n  -> skip = node to continue with when this subtree is
                           done or missed; n = triangle count of a leaf
  triangle (3 texels)  : v0.xyz, f0 | e1.xyz, f1 | e2.xyz, f2
      e1 = v1 - v0, e2 = v2 - v0; f* are free for the caller (flags, uv)

Nodes are stored depth first, so the traversal needs no stack: an inner node
that is hit continues with index + 1, anything else continues with skip.
===========================================================================
*/
#pragma once

#include <vector>
#include <stdint.h>

#define RTBVH_MAX_LEAF_TRIS		4
#define RTBVH_SKIP_SCALE		8.0f
// skip * 8 + n has to stay exact in a float
#define RTBVH_MAX_NODES			( ( 1 << 21 ) - 1 )
#define RTBVH_MAX_TRIS			( ( 1 << 24 ) - 1 )

struct rtNode_t
{
	float mins[3];
	float a;
	float maxs[3];
	float b;
};

struct rtTri_t
{
	float v0[3];
	float f0;
	float e1[3];
	float f1;
	float e2[3];
	float f2;
};

struct rtBvh_t
{
	std::vector<rtNode_t>	nodes;
	// slot -> index of the source triangle; the caller stores its triangles in this order
	std::vector<uint32_t>	order;
	int						maxDepth;

	rtBvh_t() : maxDepth( 0 ) {}
};

// positions: 9 floats per triangle (v0, v1, v2).  Returns false when the tree does not fit the encoding.
bool RtBvh_Build( const float *positions, int numTris, rtBvh_t *out );

// Fills tris[slot] from the source positions in bvh order (f0..f2 = 0).
void RtBvh_PackTriangles( const rtBvh_t *bvh, const float *positions, rtTri_t *tris );

// Recomputes every box from packed triangles that kept their slots (skinned meshes).
void RtBvh_Refit( rtNode_t *nodes, int numNodes, const rtTri_t *tris );

// Reference any-hit, the same walk as the shader.  steps (optional) receives the node visit count.
bool RtBvh_AnyHit( const rtNode_t *nodes, int numNodes, const rtTri_t *tris,
	const float *origin, const float *dir, float tMin, float tMax, int *steps );

// Bounds of the whole tree (root box).
void RtBvh_Bounds( const rtBvh_t *bvh, float *mins, float *maxs );
