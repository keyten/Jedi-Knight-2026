// Standalone test of the ray traced shadow BVH core.
// Build: g++ -O2 -std=c++11 -I shared/rd-rend2 tools/rend2/test_rt_bvh.cpp shared/rd-rend2/tr_rtbvh.cpp -o test_rt_bvh
#include "tr_rtbvh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static uint32_t s_seed = 12345;
static float Rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return (float)( ( s_seed >> 8 ) & 0xffffff ) / 16777216.0f;
}

static bool Brute( const std::vector<rtTri_t> &tris, const float *o, const float *d, float tMin, float tMax )
{
	// one leaf holding a single triangle, walked by the same intersection code
	for ( size_t i = 0; i < tris.size(); i++ )
	{
		rtNode_t n;
		n.mins[0] = n.mins[1] = n.mins[2] = -1.0e30f;
		n.maxs[0] = n.maxs[1] = n.maxs[2] = 1.0e30f;
		n.a = 0.0f;
		n.b = 1.0f * RTBVH_SKIP_SCALE + 1.0f;
		if ( RtBvh_AnyHit( &n, 1, &tris[i], o, d, tMin, tMax, NULL ) )
			return true;
	}
	return false;
}

static int CheckStructure( const rtBvh_t &bvh, int numTris )
{
	int errors = 0;
	const int numNodes = (int)bvh.nodes.size();
	std::vector<int> seen( numTris > 0 ? numTris : 1, 0 );
	for ( int i = 0; i < numNodes; i++ )
	{
		const rtNode_t &n = bvh.nodes[i];
		const int skip = (int)( n.b / RTBVH_SKIP_SCALE );
		if ( skip <= i || skip > numNodes )
			errors++;
		if ( n.a >= 0.0f )
		{
			const int first = (int)n.a;
			const int count = (int)n.b & 7;
			if ( skip != i + 1 || count > RTBVH_MAX_LEAF_TRIS )
				errors++;
			for ( int t = 0; t < count; t++ )
			{
				if ( first + t >= numTris )
					errors++;
				else
					seen[first + t]++;
			}
		}
	}
	for ( int i = 0; i < numTris; i++ )
		if ( seen[i] != 1 )
			errors++;
	std::vector<int> used( numTris > 0 ? numTris : 1, 0 );
	for ( size_t i = 0; i < bvh.order.size(); i++ )
		used[bvh.order[i]]++;
	for ( int i = 0; i < numTris; i++ )
		if ( used[i] != 1 )
			errors++;
	return errors;
}

static int RunScene( int numTris, float extent, float triSize, int numRays, bool refit )
{
	std::vector<float> pos( (size_t)numTris * 9 );
	for ( int i = 0; i < numTris; i++ )
	{
		float c[3] = { ( Rnd() - 0.5f ) * extent, ( Rnd() - 0.5f ) * extent, ( Rnd() - 0.5f ) * extent * 0.3f };
		for ( int v = 0; v < 3; v++ )
			for ( int k = 0; k < 3; k++ )
				pos[(size_t)i * 9 + v * 3 + k] = c[k] + ( Rnd() - 0.5f ) * triSize;
	}

	rtBvh_t bvh;
	if ( !RtBvh_Build( pos.data(), numTris, &bvh ) )
	{
		printf( "  build failed\n" );
		return 1;
	}
	int errors = CheckStructure( bvh, numTris );
	std::vector<rtTri_t> tris( numTris > 0 ? numTris : 1 );
	RtBvh_PackTriangles( &bvh, pos.data(), tris.data() );
	if ( numTris == 0 )
		tris.clear();

	if ( refit )
	{
		// deform in place (slots kept), then refit: results must still match brute force
		for ( size_t i = 0; i < tris.size(); i++ )
		{
			const float s = 1.0f + 0.5f * sinf( tris[i].v0[0] * 0.01f );
			tris[i].v0[2] += 40.0f * sinf( tris[i].v0[1] * 0.02f );
			for ( int k = 0; k < 3; k++ )
			{
				tris[i].e1[k] *= s;
				tris[i].e2[k] *= s;
			}
		}
		RtBvh_Refit( bvh.nodes.data(), (int)bvh.nodes.size(), tris.data() );
	}

	long long steps = 0;
	int hits = 0;
	for ( int r = 0; r < numRays; r++ )
	{
		float o[3] = { ( Rnd() - 0.5f ) * extent, ( Rnd() - 0.5f ) * extent, ( Rnd() - 0.5f ) * extent * 0.3f };
		float d[3] = { Rnd() - 0.5f, Rnd() - 0.5f, Rnd() - 0.5f };
		if ( r % 7 == 0 ) d[0] = 0.0f;		// axis aligned rays
		if ( r % 11 == 0 ) d[1] = 0.0f;
		const float len = sqrtf( d[0] * d[0] + d[1] * d[1] + d[2] * d[2] );
		if ( len < 1.0e-4f )
			continue;
		for ( int k = 0; k < 3; k++ )
			d[k] /= len;
		const float tMax = ( r % 3 == 0 ) ? extent * 0.2f : 1.0e6f;
		int s = 0;
		const bool a = RtBvh_AnyHit( bvh.nodes.data(), (int)bvh.nodes.size(), tris.data(), o, d, 0.01f, tMax, &s );
		const bool b = Brute( tris, o, d, 0.01f, tMax );
		steps += s;
		if ( a )
			hits++;
		if ( a != b )
			errors++;
	}
	printf( "  tris %7d  nodes %7d  depth %2d  rays %d  hit %4.1f%%  avg steps %.1f  errors %d%s\n",
		numTris, (int)bvh.nodes.size(), bvh.maxDepth, numRays, 100.0f * hits / ( numRays ? numRays : 1 ),
		(double)steps / ( numRays ? numRays : 1 ), errors, refit ? "  (refit)" : "" );
	return errors;
}

int main()
{
	int errors = 0;
	printf( "rt bvh test\n" );
	errors += RunScene( 0, 100.0f, 10.0f, 200, false );
	errors += RunScene( 1, 100.0f, 50.0f, 2000, false );
	errors += RunScene( 5, 100.0f, 50.0f, 2000, false );
	errors += RunScene( 300, 500.0f, 60.0f, 4000, false );
	errors += RunScene( 5000, 2000.0f, 80.0f, 4000, false );
	errors += RunScene( 5000, 2000.0f, 80.0f, 4000, true );
	errors += RunScene( 60000, 8000.0f, 120.0f, 1500, false );
	errors += RunScene( 60000, 8000.0f, 120.0f, 1500, true );
	// all centroids coincide: exercises the median fallback
	errors += RunScene( 2000, 0.0f, 100.0f, 1500, false );
	printf( errors ? "FAILED (%d)\n" : "OK\n", errors );
	return errors ? 1 : 0;
}
