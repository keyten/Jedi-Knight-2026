/*
===========================================================================
Ray traced shadows: BVH core (see tr_rtbvh.h for the layout).
===========================================================================
*/
#include "tr_rtbvh.h"

#include <algorithm>
#include <float.h>
#include <math.h>
#include <string.h>

namespace
{
	const int	RT_BINS = 12;
	const float	RT_BIG = 1.0e30f;

	struct Box
	{
		float mn[3];
		float mx[3];

		void Clear()
		{
			mn[0] = mn[1] = mn[2] = RT_BIG;
			mx[0] = mx[1] = mx[2] = -RT_BIG;
		}
		void AddPoint( const float *p )
		{
			for ( int i = 0; i < 3; i++ )
			{
				if ( p[i] < mn[i] ) mn[i] = p[i];
				if ( p[i] > mx[i] ) mx[i] = p[i];
			}
		}
		void AddBox( const Box &b )
		{
			for ( int i = 0; i < 3; i++ )
			{
				if ( b.mn[i] < mn[i] ) mn[i] = b.mn[i];
				if ( b.mx[i] > mx[i] ) mx[i] = b.mx[i];
			}
		}
		float HalfArea() const
		{
			const float x = mx[0] - mn[0];
			const float y = mx[1] - mn[1];
			const float z = mx[2] - mn[2];
			if ( x < 0.0f || y < 0.0f || z < 0.0f )
				return 0.0f;
			return x * y + y * z + z * x;
		}
	};

	struct Builder
	{
		const float				*positions;
		std::vector<Box>		triBox;
		std::vector<float>		centroid;	// 3 per triangle
		std::vector<uint32_t>	*order;
		std::vector<rtNode_t>	*nodes;
		int						maxDepth;
		bool					overflow;

		void MakeLeaf( int nodeIndex, int first, int count )
		{
			rtNode_t &n = ( *nodes )[nodeIndex];
			n.a = (float)first;
			n.b = (float)( nodeIndex + 1 ) * RTBVH_SKIP_SCALE + (float)count;
		}

		void Build( int first, int count, int depth )
		{
			const int nodeIndex = (int)nodes->size();
			if ( nodeIndex >= RTBVH_MAX_NODES )
			{
				overflow = true;
				return;
			}
			nodes->push_back( rtNode_t() );
			if ( depth > maxDepth )
				maxDepth = depth;

			Box bounds, cbounds;
			bounds.Clear();
			cbounds.Clear();
			uint32_t *idx = &( *order )[0];
			for ( int i = first; i < first + count; i++ )
			{
				bounds.AddBox( triBox[idx[i]] );
				cbounds.AddPoint( &centroid[idx[i] * 3] );
			}
			{
				rtNode_t &n = ( *nodes )[nodeIndex];
				memcpy( n.mins, bounds.mn, sizeof( n.mins ) );
				memcpy( n.maxs, bounds.mx, sizeof( n.maxs ) );
			}

			if ( count <= RTBVH_MAX_LEAF_TRIS )
			{
				MakeLeaf( nodeIndex, first, count );
				return;
			}

			// binned SAH over the three axes
			int		bestAxis = -1;
			int		bestSplit = 0;
			float	bestCost = FLT_MAX;
			for ( int axis = 0; axis < 3; axis++ )
			{
				const float lo = cbounds.mn[axis];
				const float extent = cbounds.mx[axis] - lo;
				if ( !( extent > 1.0e-6f ) )
					continue;

				Box	binBox[RT_BINS];
				int	binCount[RT_BINS];
				for ( int b = 0; b < RT_BINS; b++ )
				{
					binBox[b].Clear();
					binCount[b] = 0;
				}
				const float scale = (float)RT_BINS / extent;
				for ( int i = first; i < first + count; i++ )
				{
					int b = (int)( ( centroid[idx[i] * 3 + axis] - lo ) * scale );
					if ( b < 0 ) b = 0;
					if ( b >= RT_BINS ) b = RT_BINS - 1;
					binBox[b].AddBox( triBox[idx[i]] );
					binCount[b]++;
				}

				float	rightArea[RT_BINS];
				int		rightCount[RT_BINS];
				Box		acc;
				int		n = 0;
				acc.Clear();
				for ( int b = RT_BINS - 1; b > 0; b-- )
				{
					acc.AddBox( binBox[b] );
					n += binCount[b];
					rightArea[b] = acc.HalfArea();
					rightCount[b] = n;
				}
				acc.Clear();
				n = 0;
				for ( int b = 0; b < RT_BINS - 1; b++ )
				{
					acc.AddBox( binBox[b] );
					n += binCount[b];
					if ( n == 0 || rightCount[b + 1] == 0 )
						continue;
					const float cost = acc.HalfArea() * (float)n + rightArea[b + 1] * (float)rightCount[b + 1];
					if ( cost < bestCost )
					{
						bestCost = cost;
						bestAxis = axis;
						bestSplit = b;
					}
				}
			}

			int mid;
			if ( bestAxis >= 0 )
			{
				const float lo = cbounds.mn[bestAxis];
				const float scale = (float)RT_BINS / ( cbounds.mx[bestAxis] - lo );
				const std::vector<float> &c = centroid;
				const int axis = bestAxis;
				const int split = bestSplit;
				uint32_t *p = std::partition( idx + first, idx + first + count,
					[&c, axis, lo, scale, split]( uint32_t t )
					{
						int b = (int)( ( c[t * 3 + axis] - lo ) * scale );
						if ( b < 0 ) b = 0;
						if ( b >= RT_BINS ) b = RT_BINS - 1;
						return b <= split;
					} );
				mid = (int)( p - idx );
			}
			else
			{
				mid = first;
			}
			if ( mid <= first || mid >= first + count )
			{
				// coincident centroids: any even split keeps the tree bounded
				mid = first + count / 2;
			}

			( *nodes )[nodeIndex].a = -1.0f;
			Build( first, mid - first, depth + 1 );
			Build( mid, first + count - mid, depth + 1 );
			( *nodes )[nodeIndex].b = (float)nodes->size() * RTBVH_SKIP_SCALE;
		}
	};

	inline void TriBox( const rtTri_t &t, Box &b )
	{
		float v[3];
		b.AddPoint( t.v0 );
		v[0] = t.v0[0] + t.e1[0]; v[1] = t.v0[1] + t.e1[1]; v[2] = t.v0[2] + t.e1[2];
		b.AddPoint( v );
		v[0] = t.v0[0] + t.e2[0]; v[1] = t.v0[1] + t.e2[1]; v[2] = t.v0[2] + t.e2[2];
		b.AddPoint( v );
	}
}

bool RtBvh_Build( const float *positions, int numTris, rtBvh_t *out )
{
	out->nodes.clear();
	out->order.clear();
	out->maxDepth = 0;

	if ( numTris <= 0 )
	{
		// one empty leaf with an inverted box: never hit
		rtNode_t n;
		n.mins[0] = n.mins[1] = n.mins[2] = RT_BIG;
		n.maxs[0] = n.maxs[1] = n.maxs[2] = -RT_BIG;
		n.a = 0.0f;
		n.b = 1.0f * RTBVH_SKIP_SCALE;
		out->nodes.push_back( n );
		return true;
	}
	if ( numTris > RTBVH_MAX_TRIS )
		return false;

	Builder b;
	b.positions = positions;
	b.triBox.resize( numTris );
	b.centroid.resize( (size_t)numTris * 3 );
	b.order = &out->order;
	b.nodes = &out->nodes;
	b.maxDepth = 0;
	b.overflow = false;

	out->order.resize( numTris );
	out->nodes.reserve( (size_t)numTris );
	for ( int i = 0; i < numTris; i++ )
	{
		const float *p = positions + (size_t)i * 9;
		Box &box = b.triBox[i];
		box.Clear();
		box.AddPoint( p );
		box.AddPoint( p + 3 );
		box.AddPoint( p + 6 );
		for ( int k = 0; k < 3; k++ )
			b.centroid[(size_t)i * 3 + k] = 0.5f * ( box.mn[k] + box.mx[k] );
		out->order[i] = (uint32_t)i;
	}

	b.Build( 0, numTris, 1 );
	out->maxDepth = b.maxDepth;
	if ( b.overflow )
	{
		out->nodes.clear();
		out->order.clear();
		return false;
	}
	return true;
}

void RtBvh_PackTriangles( const rtBvh_t *bvh, const float *positions, rtTri_t *tris )
{
	const size_t count = bvh->order.size();
	for ( size_t i = 0; i < count; i++ )
	{
		const float *p = positions + (size_t)bvh->order[i] * 9;
		rtTri_t &t = tris[i];
		for ( int k = 0; k < 3; k++ )
		{
			t.v0[k] = p[k];
			t.e1[k] = p[3 + k] - p[k];
			t.e2[k] = p[6 + k] - p[k];
		}
		t.f0 = t.f1 = t.f2 = 0.0f;
	}
}

void RtBvh_Refit( rtNode_t *nodes, int numNodes, const rtTri_t *tris )
{
	// depth first order: children always come after their parent
	for ( int i = numNodes - 1; i >= 0; i-- )
	{
		rtNode_t &n = nodes[i];
		Box box;
		box.Clear();
		if ( n.a >= 0.0f )
		{
			const int first = (int)n.a;
			const int count = (int)n.b & 7;
			for ( int t = 0; t < count; t++ )
				TriBox( tris[first + t], box );
		}
		else
		{
			const rtNode_t &left = nodes[i + 1];
			const int right = (int)( left.b / RTBVH_SKIP_SCALE );
			memcpy( box.mn, left.mins, sizeof( box.mn ) );
			memcpy( box.mx, left.maxs, sizeof( box.mx ) );
			if ( right < numNodes )
			{
				Box rb;
				memcpy( rb.mn, nodes[right].mins, sizeof( rb.mn ) );
				memcpy( rb.mx, nodes[right].maxs, sizeof( rb.mx ) );
				box.AddBox( rb );
			}
		}
		memcpy( n.mins, box.mn, sizeof( n.mins ) );
		memcpy( n.maxs, box.mx, sizeof( n.maxs ) );
	}
}

bool RtBvh_AnyHit( const rtNode_t *nodes, int numNodes, const rtTri_t *tris,
	const float *origin, const float *dir, float tMin, float tMax, int *steps )
{
	float inv[3];
	for ( int k = 0; k < 3; k++ )
	{
		const float d = fabsf( dir[k] ) > 1.0e-20f ? dir[k] : ( dir[k] < 0.0f ? -1.0e-20f : 1.0e-20f );
		inv[k] = 1.0f / d;
	}

	int visited = 0;
	bool hit = false;
	int i = 0;
	while ( i < numNodes )
	{
		const rtNode_t &n = nodes[i];
		visited++;

		float t0 = tMin, t1 = tMax;
		for ( int k = 0; k < 3; k++ )
		{
			const float a = ( n.mins[k] - origin[k] ) * inv[k];
			const float b = ( n.maxs[k] - origin[k] ) * inv[k];
			const float lo = a < b ? a : b;
			const float hi = a < b ? b : a;
			if ( lo > t0 ) t0 = lo;
			if ( hi < t1 ) t1 = hi;
		}
		const int skip = (int)( n.b / RTBVH_SKIP_SCALE );
		if ( !( t0 <= t1 ) )
		{
			i = skip;
			continue;
		}
		if ( n.a < 0.0f )
		{
			i++;
			continue;
		}

		const int first = (int)n.a;
		const int count = (int)n.b & 7;
		for ( int t = 0; t < count && !hit; t++ )
		{
			const rtTri_t &tri = tris[first + t];
			// Moeller-Trumbore, two sided
			const float px = dir[1] * tri.e2[2] - dir[2] * tri.e2[1];
			const float py = dir[2] * tri.e2[0] - dir[0] * tri.e2[2];
			const float pz = dir[0] * tri.e2[1] - dir[1] * tri.e2[0];
			const float det = tri.e1[0] * px + tri.e1[1] * py + tri.e1[2] * pz;
			if ( fabsf( det ) < 1.0e-12f )
				continue;
			const float invDet = 1.0f / det;
			const float sx = origin[0] - tri.v0[0];
			const float sy = origin[1] - tri.v0[1];
			const float sz = origin[2] - tri.v0[2];
			const float u = ( sx * px + sy * py + sz * pz ) * invDet;
			if ( u < 0.0f || u > 1.0f )
				continue;
			const float qx = sy * tri.e1[2] - sz * tri.e1[1];
			const float qy = sz * tri.e1[0] - sx * tri.e1[2];
			const float qz = sx * tri.e1[1] - sy * tri.e1[0];
			const float v = ( dir[0] * qx + dir[1] * qy + dir[2] * qz ) * invDet;
			if ( v < 0.0f || u + v > 1.0f )
				continue;
			const float dist = ( tri.e2[0] * qx + tri.e2[1] * qy + tri.e2[2] * qz ) * invDet;
			if ( dist > tMin && dist < tMax )
				hit = true;
		}
		if ( hit )
			break;
		i = skip;
	}

	if ( steps )
		*steps = visited;
	return hit;
}

void RtBvh_Bounds( const rtBvh_t *bvh, float *mins, float *maxs )
{
	if ( bvh->nodes.empty() )
	{
		mins[0] = mins[1] = mins[2] = 0.0f;
		maxs[0] = maxs[1] = maxs[2] = 0.0f;
		return;
	}
	memcpy( mins, bvh->nodes[0].mins, sizeof( float ) * 3 );
	memcpy( maxs, bvh->nodes[0].maxs, sizeof( float ) * 3 );
}
