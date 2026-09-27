/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Silhouette parallax occlusion mapping (r_pomSilhouette), docs/rend2-silhouette-pom.md.
//
// A fragment shader only runs inside the projection of the geometry drawn, so
// ordinary POM can shift texture coordinates but never move the outline of a
// surface. Opt-in materials (shader keyword silhouettePOM) get a shell around
// the displaced height field volume instead, built on the CPU at map load:
//
//   planar groups   connected triangles of one surface that are coplanar and
//                   have one affine texture mapping (no seam between them)
//   top cap         the triangles moved to the top of the volume, s = 0
//   side walls      along the boundary edges of each group, down to s = 1
//
// The shell is only a conservative raster volume. Every shell vertex keeps the
// base frame and the texture / lightmap coordinates of its foot point on the
// base plane, so a fragment knows where its camera ray enters the volume. The
// ray is traced through the height field until it hits it or leaves the group
// (slab depth range, or a boundary edge in texture space, from the group
// buffer texture): no hit discards the fragment, a hit gives the displaced
// texture coordinates and the virtual position, written as gl_FragDepth
// (glsl/pom_silhouette.glsl). The depth prepass, the colour pass, the fog pass
// and the sun cascades all trace with the same function and uniforms.
//
// Near surfaces use the shell, far ones ordinary POM; a dithered band blends
// them (both drawn, complementary masks). Unsupported surfaces keep ordinary
// POM with a developer warning.
//
// Sources: the silhouettePOM keyword, or (r_autoPomSilhouette) every material
// that already has ordinary POM, i.e. a lightall stage with a normal + height
// map (explicit normalHeightMap or the discovered _nh image). With
// r_pomSilhouette 1 shells are built for both at map load; which shaders use
// them is decided per frame (global auto mode, per-shader switches saved in
// pomsilhouette.cfg), so every switch works without a map reload.

#include "tr_local.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define POM_MAX_GROUP_EDGES		48		// boundary edges a fragment may test
#define POM_HEADER_TEXELS		3
#define POM_WELD_SCALE			8.0		// positions welded on a 1/8 unit grid
#define POM_COPLANAR_DOT		0.9999
#define POM_COPLANAR_DIST		0.05
#define POM_SMOOTH_NORMAL_DOT	0.995f	// vertex normal vs face normal: curved above ~5.7 degrees
#define POM_UV_EPSILON			1e-4
#define POM_GRADIENT_EPSILON	1e-3

struct pomShellVertex_t
{
	packedVertex_t base;
	vec3_t pom;		// s0, D, group header texel * 2 + wall bottom vertex
};

struct pomCandidate_t
{
	msurface_t *surf;
	const shaderStage_t *stage;
	std::vector<packedVertex_t> verts;
	std::vector<glIndex_t> indexes;
};

struct pomFallback_t
{
	const char *reason;		// static string
	int surfaces;
};

struct pomWorldObjects_t
{
	GLuint buffer;
	image_t *image;
};

// a shader whose surfaces kept ordinary POM, r_autoPomSilhouette list
struct pomSkipped_t
{
	const shader_t *shader;
	const char *reason;		// static string
	int surfaces;
};

// r_autoPomSilhouette <shader> 1|0: exact shader name, or a prefix with a
// trailing '*'
struct pomOverride_t
{
	char pattern[MAX_QPATH];
	int value;
};

#define POM_OVERRIDE_FILE "pomsilhouette.cfg"

static struct
{
	std::vector<pomCandidate_t> candidates;
	std::vector<pomFallback_t> fallbacks;
	std::vector<const shader_t *> warnedShaders;
	std::vector<pomWorldObjects_t> objects;		// GL objects of the loaded worlds

	std::vector<const srfBspSurface_t *> debugBases;	// r_pomSilhouetteDebug 3
	std::vector<pomSkipped_t> skipped;

	// per-shader switches: kept for the lifetime of the module, loaded from
	// POM_OVERRIDE_FILE on first use. A change bumps the generation, shaders
	// look their switch up again (shader_t::pomOverride).
	std::vector<pomOverride_t> overrides;
	int overrideGeneration;
	qboolean overridesLoaded;

	// totals of the loaded worlds, r_pomSilhouetteInfo
	int surfaces;
	int groups;
	int walls;
	int shellVerts;
	int shellTriangles;
	int groupTexels;
	size_t vboBytes;
	size_t iboBytes;
	size_t tboBytes;

	int lastParallaxMapping;
	qboolean warnedParallax;
	qboolean warnedPrograms;
	int lastAutoMode;
	qboolean warnedAuto;
} s_pom;

// Keep a copy of the vector's control bytes outside the vector itself. A
// disabled silhouette POM still reaches shutdown; comparing these bytes does
// not dereference a potentially corrupt element or call vector::size().
static struct pomObjectsDiagnostic_t
{
	unsigned char expected[sizeof(s_pom.objects)];
	int expectedCount;
	uint32_t reportedMarker;

	pomObjectsDiagnostic_t() : expectedCount(0), reportedMarker(0)
	{
		std::memcpy(expected, &s_pom.objects, sizeof(expected));
	}
} s_pomObjectsDiagnostic;

static void R_PomSnapshotObjects( void )
{
	std::memcpy(s_pomObjectsDiagnostic.expected, &s_pom.objects,
		sizeof(s_pomObjectsDiagnostic.expected));
}

static void R_PomCheckObjects( const char *stage )
{
	const unsigned char *actualBytes =
		reinterpret_cast<const unsigned char *>(&s_pom.objects);
	bool disabledVectorNonzero = false;
	if ( r_pomSilhouette && !r_pomSilhouette->integer )
	{
		for ( size_t i = 0; i < sizeof(s_pom.objects); i++ )
			disabledVectorNonzero |= actualBytes[i] != 0;
	}
	if ( s_pomObjectsDiagnostic.reportedMarker == 0x504f4d21u ||
		(!disabledVectorNonzero &&
			!std::memcmp(s_pomObjectsDiagnostic.expected, &s_pom.objects,
				sizeof(s_pomObjectsDiagnostic.expected))) )
		return;

	// Read the representation as bytes: vector::size(), iteration and even a
	// pointer subtraction would be unsafe after the control block was damaged.
	unsigned char actual[sizeof(s_pom.objects)];
	std::memcpy(actual, &s_pom.objects, sizeof(actual));
	char expectedHex[sizeof(actual) * 2 + 1];
	char actualHex[sizeof(actual) * 2 + 1];
	for ( size_t i = 0; i < sizeof(actual); i++ )
	{
		std::snprintf(expectedHex + i * 2, 3, "%02x", s_pomObjectsDiagnostic.expected[i]);
		std::snprintf(actualHex + i * 2, 3, "%02x", actual[i]);
	}

	char report[1024];
	const int length = std::snprintf(report, sizeof(report),
		"rend2 silhouette POM object vector changed unexpectedly\n"
		"stage=%s time_ms=%d r_pomSilhouette=%d expected_objects=%d\n"
		"expected_control=%s\nactual_control=%s\n"
		"The state was overwritten before this check; the writer is not yet known.\n",
		stage, ri.Milliseconds(), r_pomSilhouette ? r_pomSilhouette->integer : -1,
		s_pomObjectsDiagnostic.expectedCount, expectedHex, actualHex);
	if ( length > 0 )
	{
		// FS_WriteFile closes the file, so the report survives an immediate
		// process crash even when the normal console log was not enabled.
		ri.FS_WriteFile("rend2-state-diagnostic.txt", report,
			length < (int)sizeof(report) ? length : (int)sizeof(report) - 1);
		ri.Printf(PRINT_WARNING, "%s", report);
	}
	s_pomObjectsDiagnostic.reportedMarker = 0x504f4d21u;
}

/*
============================================================

Per-shader switches (r_autoPomSilhouette <shader> 1|0|default)

============================================================
*/

// exact names beat patterns, longer prefixes beat shorter ones
static bool R_PomPatternMatches( const char *pattern, const char *name, int *score )
{
	const int len = (int)strlen(pattern);
	if ( len > 0 && pattern[len - 1] == '*' )
	{
		if ( Q_stricmpn(pattern, name, len - 1) )
			return false;
		*score = len - 1;
		return true;
	}
	if ( Q_stricmp(pattern, name) )
		return false;
	*score = 1 << 20;
	return true;
}

static void R_PomSilhouetteLoadOverrides( void )
{
	if ( s_pom.overridesLoaded )
		return;
	s_pom.overridesLoaded = qtrue;
	s_pom.overrideGeneration++;

	char *buffer = nullptr;
	const long size = ri.FS_ReadFile(POM_OVERRIDE_FILE, (void **)&buffer);
	if ( size <= 0 || !buffer )
		return;

	std::string text(buffer, (size_t)size);
	ri.FS_FreeFile(buffer);
	size_t start = 0;
	while ( start < text.size() )
	{
		size_t end = text.find('\n', start);
		if ( end == std::string::npos )
			end = text.size();
		const std::string line = text.substr(start, end - start);
		start = end + 1;

		pomOverride_t o = {};
		if ( line.compare(0, 2, "//") == 0 )
			continue;
		if ( sscanf(line.c_str(), "%63s %d", o.pattern, &o.value) != 2 || (o.value != 0 && o.value != 1) )
			continue;
		s_pom.overrides.push_back(o);
	}
	ri.Printf(PRINT_DEVELOPER, "silhouette POM: %d per-shader switches from %s\n",
		(int)s_pom.overrides.size(), POM_OVERRIDE_FILE);
}

static void R_PomSilhouetteSaveOverrides( void )
{
	std::string text = "// r_autoPomSilhouette <shader> 1|0: per-shader silhouette POM switches (rend2), written by the command\n";
	for ( const pomOverride_t& o : s_pom.overrides )
		text += va("%s %d\n", o.pattern, o.value);
	ri.FS_WriteFile(POM_OVERRIDE_FILE, text.c_str(), (int)text.size());
}

// -1 no switch, 0 off, 1 on
static int R_PomSilhouetteOverride( shader_t *shader )
{
	R_PomSilhouetteLoadOverrides();
	if ( shader->pomOverrideGeneration != s_pom.overrideGeneration )
	{
		int value = -1, best = -1;
		for ( const pomOverride_t& o : s_pom.overrides )
		{
			int score;
			if ( R_PomPatternMatches(o.pattern, shader->name, &score) && score > best )
			{
				best = score;
				value = o.value;
			}
		}
		shader->pomOverride = value;
		shader->pomOverrideGeneration = s_pom.overrideGeneration;
	}
	return shader->pomOverride;
}

// does a shader with a shell use it: switch > keyword > automatic mode
static qboolean R_PomSilhouetteShaderEnabled( shader_t *shader )
{
	const int value = R_PomSilhouetteOverride(shader);
	if ( value >= 0 )
		return (qboolean)(value != 0);
	if ( shader->pomSilhouetteSource == POM_SOURCE_KEYWORD )
		return qtrue;
	return (qboolean)(shader->pomSilhouetteSource == POM_SOURCE_AUTO && r_autoPomSilhouetteMode->integer);
}

// fallback reasons are printed for shaders asked for explicitly only (keyword
// or switched on), automatic candidates are just counted
static qboolean R_PomSilhouetteLoud( shader_t *shader )
{
	return (qboolean)(shader->silhouettePOM || R_PomSilhouetteOverride(shader) == 1);
}

/*
============================================================

Load: candidates, planar groups, shell

============================================================
*/

static void R_PomSilhouetteFallback( shader_t *shader, const char *reason )
{
	bool counted = false;
	for ( pomFallback_t& f : s_pom.fallbacks )
	{
		if ( f.reason == reason )
		{
			f.surfaces++;
			counted = true;
			break;
		}
	}
	if ( !counted )
		s_pom.fallbacks.push_back({ reason, 1 });

	counted = false;
	for ( pomSkipped_t& k : s_pom.skipped )
	{
		if ( k.shader == shader && k.reason == reason )
		{
			k.surfaces++;
			counted = true;
			break;
		}
	}
	if ( !counted )
		s_pom.skipped.push_back({ shader, reason, 1 });

	if ( !R_PomSilhouetteLoud(shader) )
		return;

	for ( const shader_t *s : s_pom.warnedShaders )
	{
		if ( s == shader )
			return;
	}
	s_pom.warnedShaders.push_back(shader);
	ri.Printf(PRINT_DEVELOPER, S_COLOR_YELLOW "silhouettePOM: '%s' keeps ordinary POM (%s)\n",
		shader->name, reason);
}

// NULL when the shader can use a shell, else why not
static const char *R_PomSilhouetteShaderReason( const shader_t *shader )
{
	if ( shader->isSky || shader->isPortal )
		return "sky / portal";
	if ( shader->numDeforms )
		return "deformVertexes";
	if ( shader->cullType != CT_FRONT_SIDED )
		return "not front sided (cull)";
	if ( shader->sort != SS_OPAQUE )
		return "not opaque";
	if ( shader->polygonOffset )
		return "polygonOffset";
	if ( shader->useDistortion )
		return "refractive";

	const shaderStage_t *stage = shader->stages[0];
	if ( !stage || !stage->active )
		return "no stage";
	for ( int i = 1; i < MAX_SHADER_STAGES; i++ )
	{
		if ( shader->stages[i] && shader->stages[i]->active )
			return "more than one stage after collapsing";
	}
	if ( stage->ss )
		return "surface sprites";
	if ( stage->glslShaderGroup != tr.lightallShader )
		return "not a lightall stage";

	const int index = stage->glslShaderIndex;
	if ( !(index & LIGHTDEF_USE_PARALLAXMAP) )
		return "no normalHeightMap (parallax)";
	const int lightType = index & LIGHTDEF_LIGHTTYPE_MASK;
	if ( lightType != LIGHTDEF_USE_LIGHTMAP && lightType != LIGHTDEF_USE_LIGHT_VERTEX )
		return "not lightmap / vertex lit";
	if ( (index & LIGHTDEF_USE_TCGEN_AND_TCMOD) ||
		stage->bundle[0].tcGen != TCGEN_TEXTURE || stage->bundle[0].numTexMods )
		return "tcGen / tcMod";
	if ( lightType == LIGHTDEF_USE_LIGHTMAP && stage->bundle[TB_LIGHTMAP].tcGen != TCGEN_LIGHTMAP )
		return "lightmap style";
	if ( stage->rgbGen == CGEN_LIGHTMAPSTYLE )
		return "lightmap style";
	if ( stage->alphaTestType != ALPHA_TEST_NONE )
		return "alpha test";
	if ( stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS) )
		return "blended";
	if ( stage->normalScale[3] <= 0.0f )
		return "parallaxDepth 0";

	const textureBundle_t *heightBundle = &stage->bundle[TB_NORMALMAP];
	if ( !heightBundle->image[0] || heightBundle->image[0]->type != IMGTYPE_NORMALHEIGHT )
		return "no normalHeightMap (parallax)";
	if ( heightBundle->numImageAnimations > 1 )
		return "animated normal map";
	if ( !RB_PomSilhouetteLightallProgram(stage)->program )
		return "no silhouette program for this lightall permutation";

	return nullptr;
}

void R_PomSilhouetteBeginWorld( world_t *world )
{
	R_PomCheckObjects("begin world");
	s_pom.candidates.clear();
	world->numPomShells = 0;
	world->pomShells = nullptr;
	world->pomGroupsImage = nullptr;
	world->pomGroupsBuffer = 0;
	world->pomGroupsTexels = 0;
}

// a lightall stage with ordinary POM (normal + height map): the automatic
// candidates of r_autoPomSilhouette
static bool R_PomSilhouetteHasHeightField( const shader_t *shader )
{
	for ( int i = 0; i < MAX_SHADER_STAGES; i++ )
	{
		const shaderStage_t *stage = shader->stages[i];
		if ( !stage || !stage->active )
			continue;
		const image_t *image = stage->bundle[TB_NORMALMAP].image[0];
		if ( stage->glslShaderGroup == tr.lightallShader &&
			(stage->glslShaderIndex & LIGHTDEF_USE_PARALLAXMAP) &&
			image && image->type == IMGTYPE_NORMALHEIGHT )
			return true;
	}
	return false;
}

void R_PomSilhouetteCollect( world_t *world, msurface_t *surf,
	const packedVertex_t *batchVerts, const glIndex_t *batchIndexes )
{
	if ( !r_pomSilhouette->integer )
		return;

	shader_t *shader = surf->shader;
	if ( !shader->silhouettePOM && !R_PomSilhouetteHasHeightField(shader) )
		return;
	shader->pomSilhouetteSource = shader->silhouettePOM ? POM_SOURCE_KEYWORD : POM_SOURCE_AUTO;

	const char *reason = R_PomSilhouetteShaderReason(shader);
	if ( !reason && *surf->data == SF_GRID )
		reason = "curved patch";
	if ( !reason && *surf->data != SF_FACE && *surf->data != SF_TRIANGLES )
		reason = "unsupported surface type";
	if ( reason )
	{
		R_PomSilhouetteFallback(shader, reason);
		return;
	}

	const srfBspSurface_t *bspSurf = (const srfBspSurface_t *)surf->data;
	pomCandidate_t candidate;
	candidate.surf = surf;
	candidate.stage = shader->stages[0];
	candidate.verts.assign(batchVerts + bspSurf->firstVert,
		batchVerts + bspSurf->firstVert + bspSurf->numVerts);
	candidate.indexes.resize(bspSurf->numIndexes);
	for ( int i = 0; i < bspSurf->numIndexes; i++ )
		candidate.indexes[i] = batchIndexes[bspSurf->firstIndex + i] - bspSurf->firstVert;
	s_pom.candidates.push_back(std::move(candidate));
}

namespace
{

struct dvec3
{
	double x, y, z;
};

static dvec3 V3( const float *v )
{
	return { v[0], v[1], v[2] };
}
static dvec3 operator+( const dvec3& a, const dvec3& b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static dvec3 operator-( const dvec3& a, const dvec3& b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static dvec3 operator*( const dvec3& a, double s ) { return { a.x * s, a.y * s, a.z * s }; }
static double Dot( const dvec3& a, const dvec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static dvec3 Cross( const dvec3& a, const dvec3& b )
{
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static double Length( const dvec3& a ) { return sqrt(Dot(a, a)); }

struct pomTriangle_t
{
	int v[3];
	int weld[3];
	dvec3 n;			// front plane normal
	double dist;
	dvec3 grad[4];		// u, v, lightmap u, lightmap v gradients in the plane
	int orientation;	// sign of dot(winding normal, n)
	int group;
	bool degenerate;
};

struct pomSurfaceOutput_t
{
	int firstVert;
	int numVerts;
	int firstIndex;
	int numIndexes;
	vec3_t bounds[2];
	float above;
	float below;
	int groups;
	int walls;
};

struct pomBuilder_t
{
	std::vector<pomShellVertex_t> verts;
	std::vector<glIndex_t> indexes;
	std::vector<float> texels;		// RGBA32F

	int AddTexel( float x, float y, float z, float w )
	{
		texels.push_back(x);
		texels.push_back(y);
		texels.push_back(z);
		texels.push_back(w);
		return (int)(texels.size() / 4) - 1;
	}
};

static uint64_t EdgeKey( int a, int b )
{
	if ( a > b )
	{
		const int t = a;
		a = b;
		b = t;
	}
	return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b;
}

static bool GradientsMatch( const dvec3& a, const dvec3& b )
{
	const double scale = Q_max(Length(a), Length(b));
	return Length(a - b) <= POM_GRADIENT_EPSILON * Q_max(scale, 1e-6);
}

static bool CoordsMatch( const vec2_t a, const vec2_t b )
{
	return fabs(a[0] - b[0]) <= POM_UV_EPSILON && fabs(a[1] - b[1]) <= POM_UV_EPSILON;
}

// Builds the shell of one surface into the builder, NULL on success
static const char *R_PomBuildSurface(
	const pomCandidate_t& c, pomBuilder_t& out, pomSurfaceOutput_t& info )
{
	const shaderStage_t *stage = c.stage;
	const int numVerts = (int)c.verts.size();
	const int numTris = (int)c.indexes.size() / 3;
	const bool lightmapped =
		(stage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) == LIGHTDEF_USE_LIGHTMAP;
	const double parallaxDepth = stage->normalScale[3];
	const double bias = stage->parallaxBias;

	// ordinary POM aspect correction of non square height maps
	const image_t *heightMap = stage->bundle[TB_NORMALMAP].image[0];
	const double aspect = (double)Q_max(1, heightMap->uploadHeight) / Q_max(1, heightMap->uploadWidth);
	const double cx = Q_max(1.0, aspect);
	const double cy = Q_max(1.0, 1.0 / aspect);

	if ( numTris <= 0 || numVerts <= 0 )
		return "empty surface";

	// weld positions
	std::vector<int> weld(numVerts);
	{
		std::unordered_map<uint64_t, int> weldMap;
		for ( int i = 0; i < numVerts; i++ )
		{
			const float *p = c.verts[i].position;
			const int64_t qx = (int64_t)floor(p[0] * POM_WELD_SCALE + 0.5);
			const int64_t qy = (int64_t)floor(p[1] * POM_WELD_SCALE + 0.5);
			const int64_t qz = (int64_t)floor(p[2] * POM_WELD_SCALE + 0.5);
			const uint64_t key =
				((uint64_t)(qx & 0x1fffff) << 42) | ((uint64_t)(qy & 0x1fffff) << 21) | (uint64_t)(qz & 0x1fffff);
			auto it = weldMap.find(key);
			if ( it == weldMap.end() )
				it = weldMap.emplace(key, (int)weldMap.size()).first;
			weld[i] = it->second;
		}
	}

	// triangles: planes, texture gradients, curvature test
	std::vector<pomTriangle_t> tris(numTris);
	for ( int t = 0; t < numTris; t++ )
	{
		pomTriangle_t& tri = tris[t];
		tri.group = -1;
		tri.degenerate = false;
		for ( int k = 0; k < 3; k++ )
		{
			tri.v[k] = c.indexes[t * 3 + k];
			if ( tri.v[k] < 0 || tri.v[k] >= numVerts )
				return "bad index";
			tri.weld[k] = weld[tri.v[k]];
		}

		const packedVertex_t& a = c.verts[tri.v[0]];
		const packedVertex_t& b = c.verts[tri.v[1]];
		const packedVertex_t& d = c.verts[tri.v[2]];
		const dvec3 p0 = V3(a.position);
		const dvec3 e1 = V3(b.position) - p0;
		const dvec3 e2 = V3(d.position) - p0;
		const dvec3 r = Cross(e1, e2);
		const double area2 = Length(r);
		if ( area2 < 1e-6 || tri.weld[0] == tri.weld[1] || tri.weld[1] == tri.weld[2] || tri.weld[0] == tri.weld[2] )
		{
			tri.degenerate = true;
			continue;
		}
		const dvec3 nr = r * (1.0 / area2);

		// front side: the side of the vertex normals
		dvec3 vertexNormals = { 0.0, 0.0, 0.0 };
		vec3_t vn[3];
		for ( int k = 0; k < 3; k++ )
		{
			R_VboUnpackNormal(vn[k], c.verts[tri.v[k]].normal);
			vertexNormals = vertexNormals + V3(vn[k]);
		}
		tri.orientation = Dot(vertexNormals, nr) >= 0.0 ? 1 : -1;
		tri.n = nr * (double)tri.orientation;
		tri.dist = Dot(tri.n, p0);

		// smooth shading across the surface: a curved mesh, the planar shell
		// would open cracks along its creases
		for ( int k = 0; k < 3; k++ )
		{
			if ( Dot(V3(vn[k]), tri.n) < POM_SMOOTH_NORMAL_DOT * Length(V3(vn[k])) )
				return "smooth vertex normals (curved)";
		}

		// in-plane gradients of u, v and the lightmap coordinates
		const float *f[3][4];
		const packedVertex_t *pv[3] = { &a, &b, &d };
		for ( int k = 0; k < 3; k++ )
		{
			f[k][0] = &pv[k]->texcoords[0][0];
			f[k][1] = &pv[k]->texcoords[0][1];
			f[k][2] = &pv[k]->texcoords[1][0];
			f[k][3] = &pv[k]->texcoords[1][1];
		}
		const dvec3 c2 = Cross(e2, nr);
		const dvec3 c1 = Cross(nr, e1);
		for ( int g = 0; g < 4; g++ )
		{
			const double f0 = *f[0][g];
			tri.grad[g] = (c2 * (*f[1][g] - f0) + c1 * (*f[2][g] - f0)) * (1.0 / area2);
		}

		const double du1 = b.texcoords[0][0] - a.texcoords[0][0];
		const double dv1 = b.texcoords[0][1] - a.texcoords[0][1];
		const double du2 = d.texcoords[0][0] - a.texcoords[0][0];
		const double dv2 = d.texcoords[0][1] - a.texcoords[0][1];
		if ( fabs(du1 * dv2 - du2 * dv1) < 1e-10 )
			return "degenerate texture mapping";
	}

	// edge adjacency on welded positions
	std::unordered_map<uint64_t, std::vector<int>> edgeMap;
	for ( int t = 0; t < numTris; t++ )
	{
		if ( tris[t].degenerate )
			continue;
		for ( int e = 0; e < 3; e++ )
			edgeMap[EdgeKey(tris[t].weld[e], tris[t].weld[(e + 1) % 3])].push_back(t * 3 + e);
	}

	// neighbour triangle across edge e of t, -1 for a boundary / non manifold edge
	auto neighbour = [&]( int t, int e ) -> int
	{
		const auto& list = edgeMap[EdgeKey(tris[t].weld[e], tris[t].weld[(e + 1) % 3])];
		if ( list.size() != 2 )
			return -1;
		const int other = (list[0] / 3 == t) ? list[1] : list[0];
		return (other / 3 == t) ? -1 : other / 3;
	};

	// can the ray cross from t to u without leaving the group frame?
	auto compatible = [&]( int t, int u, int e ) -> bool
	{
		const pomTriangle_t& a = tris[t];
		const pomTriangle_t& b = tris[u];
		if ( a.orientation != b.orientation )
			return false;
		if ( Dot(a.n, b.n) < POM_COPLANAR_DOT || fabs(a.dist - b.dist) > POM_COPLANAR_DIST )
			return false;
		for ( int g = 0; g < (lightmapped ? 4 : 2); g++ )
		{
			if ( !GradientsMatch(a.grad[g], b.grad[g]) )
				return false;
		}
		// same texture / lightmap coordinates at both ends of the edge
		for ( int k = 0; k < 2; k++ )
		{
			const int w = a.weld[(e + k) % 3];
			int vb = -1;
			for ( int j = 0; j < 3; j++ )
			{
				if ( b.weld[j] == w )
					vb = b.v[j];
			}
			if ( vb < 0 )
				return false;
			const packedVertex_t& pa = c.verts[a.v[(e + k) % 3]];
			const packedVertex_t& pb = c.verts[vb];
			if ( !CoordsMatch(pa.texcoords[0], pb.texcoords[0]) )
				return false;
			if ( lightmapped && !CoordsMatch(pa.texcoords[1], pb.texcoords[1]) )
				return false;
		}
		return true;
	};

	auto internalEdge = [&]( int t, int e ) -> bool
	{
		const int u = neighbour(t, e);
		return u >= 0 && tris[u].group == tris[t].group && compatible(t, u, e);
	};

	// region growing, at most POM_MAX_GROUP_EDGES boundary edges per group
	std::vector<std::vector<int>> groups;
	for ( int seed = 0; seed < numTris; seed++ )
	{
		if ( tris[seed].degenerate || tris[seed].group >= 0 )
			continue;

		const int g = (int)groups.size();
		groups.emplace_back();
		std::vector<int>& members = groups.back();
		tris[seed].group = g;
		members.push_back(seed);
		int boundary = 3;
		for ( size_t q = 0; q < members.size(); q++ )
		{
			const int t = members[q];
			for ( int e = 0; e < 3; e++ )
			{
				const int u = neighbour(t, e);
				if ( u < 0 || tris[u].group >= 0 || tris[u].degenerate || !compatible(t, u, e) )
					continue;

				int inside = 0;
				for ( int j = 0; j < 3; j++ )
				{
					const int w = neighbour(u, j);
					if ( w >= 0 && tris[w].group == g && compatible(u, w, j) )
						inside++;
				}
				const int delta = (3 - inside) - inside;
				if ( boundary + delta > POM_MAX_GROUP_EDGES )
					continue;

				tris[u].group = g;
				boundary += delta;
				members.push_back(u);
			}
		}
	}

	if ( groups.empty() )
		return "no triangles";

	info.firstVert = (int)out.verts.size();
	info.firstIndex = (int)out.indexes.size();
	info.groups = (int)groups.size();
	info.walls = 0;
	info.above = 0.0f;
	info.below = 0.0f;
	ClearBounds(info.bounds[0], info.bounds[1]);

	for ( int g = 0; g < (int)groups.size(); g++ )
	{
		const std::vector<int>& members = groups[g];
		const pomTriangle_t& seed = tris[members[0]];
		const dvec3 n = seed.n;

		// world units per texture unit along the texture axes
		const dvec3& gu = seed.grad[0];
		const dvec3& gv = seed.grad[1];
		const dvec3 gvn = Cross(gv, n);
		const dvec3 ngu = Cross(n, gu);
		const double du = Dot(gu, gvn);
		const double dv = Dot(gv, ngu);
		if ( fabs(du) < 1e-12 || fabs(dv) < 1e-12 )
			return "degenerate texture mapping";
		const double lu = Length(gvn) / fabs(du);
		const double lv = Length(ngu) / fabs(dv);
		const double D = parallaxDepth * sqrt(cx * lu * cy * lv);
		if ( !(D > 1e-3 && D < 4096.0) )
			return "displacement out of range";

		const double topOffset = bias * D;
		const double bottomOffset = (bias - 1.0) * D;
		info.above = Q_max(info.above, (float)Q_max(0.0, topOffset));
		info.below = Q_max(info.below, (float)Q_max(0.0, -bottomOffset));

		// group header: edges, uv -> lightmap affine map
		const int header = (int)(out.texels.size() / 4);
		for ( int i = 0; i < POM_HEADER_TEXELS; i++ )
			out.AddTexel(0.0f, 0.0f, 0.0f, 0.0f);
		{
			float m[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float bofs[2] = { 0.0f, 0.0f };
			if ( lightmapped )
			{
				const packedVertex_t& a = c.verts[seed.v[0]];
				const packedVertex_t& b = c.verts[seed.v[1]];
				const packedVertex_t& d = c.verts[seed.v[2]];
				const double u1 = b.texcoords[0][0] - a.texcoords[0][0], v1 = b.texcoords[0][1] - a.texcoords[0][1];
				const double u2 = d.texcoords[0][0] - a.texcoords[0][0], v2 = d.texcoords[0][1] - a.texcoords[0][1];
				const double s1 = b.texcoords[1][0] - a.texcoords[1][0], t1 = b.texcoords[1][1] - a.texcoords[1][1];
				const double s2 = d.texcoords[1][0] - a.texcoords[1][0], t2 = d.texcoords[1][1] - a.texcoords[1][1];
				const double det = u1 * v2 - u2 * v1;
				// [lm1 lm2] * inverse([uv1 uv2]), column major
				const double i00 = v2 / det, i01 = -u2 / det, i10 = -v1 / det, i11 = u1 / det;
				m[0] = (float)(s1 * i00 + s2 * i10);	// a00
				m[1] = (float)(t1 * i00 + t2 * i10);	// a10
				m[2] = (float)(s1 * i01 + s2 * i11);	// a01
				m[3] = (float)(t1 * i01 + t2 * i11);	// a11
				bofs[0] = a.texcoords[1][0] - (m[0] * a.texcoords[0][0] + m[2] * a.texcoords[0][1]);
				bofs[1] = a.texcoords[1][1] - (m[1] * a.texcoords[0][0] + m[3] * a.texcoords[0][1]);
			}
			float *h = &out.texels[(header + 1) * 4];
			h[0] = m[0]; h[1] = m[1]; h[2] = m[2]; h[3] = m[3];
			h = &out.texels[(header + 2) * 4];
			h[0] = bofs[0]; h[1] = bofs[1];
		}

		// shell vertices of this group
		std::unordered_map<int, int> topMap, bottomMap;
		vec3_t nf = { (float)n.x, (float)n.y, (float)n.z };
		const uint32_t packedNormal = R_VboPackNormal(nf);
		auto emit = [&]( int v, bool bottom ) -> int
		{
			std::unordered_map<int, int>& map = bottom ? bottomMap : topMap;
			auto it = map.find(v);
			if ( it != map.end() )
				return it->second;

			pomShellVertex_t sv;
			sv.base = c.verts[v];
			const dvec3 p = V3(sv.base.position) + n * (bottom ? bottomOffset : topOffset);
			VectorSet(sv.base.position, (float)p.x, (float)p.y, (float)p.z);
			AddPointToBounds(sv.base.position, info.bounds[0], info.bounds[1]);
			sv.base.normal = packedNormal;

			// tangent in the group plane, handedness kept
			vec4_t tangent;
			R_VboUnpackTangent(tangent, c.verts[v].tangent);
			dvec3 t = V3(tangent);
			t = t - n * Dot(n, t);
			double tl = Length(t);
			if ( tl < 1e-4 )
			{
				t = Cross(gvn, n) * -1.0;	// along dP/du
				t = t - n * Dot(n, t);
				tl = Q_max(Length(t), 1e-9);
			}
			t = t * (1.0 / tl);
			vec4_t packedTangent = { (float)t.x, (float)t.y, (float)t.z, tangent[3] < 0.0f ? -1.0f : 1.0f };
			sv.base.tangent = R_VboPackTangent(packedTangent);

			sv.pom[0] = bottom ? 1.0f : 0.0f;
			sv.pom[1] = (float)D;
			sv.pom[2] = (float)(header * 2 + (bottom ? 1 : 0));

			const int index = (int)out.verts.size() - info.firstVert;
			out.verts.push_back(sv);
			map.emplace(v, index);
			return index;
		};

		// top cap, original winding
		for ( int t : members )
		{
			for ( int k = 0; k < 3; k++ )
				out.indexes.push_back(info.firstVert + emit(tris[t].v[k], false));
		}

		// side walls along the boundary edges
		const int firstEdge = (int)(out.texels.size() / 4);
		int numEdges = 0;
		for ( int t : members )
		{
			const pomTriangle_t& tri = tris[t];
			for ( int e = 0; e < 3; e++ )
			{
				if ( internalEdge(t, e) )
					continue;

				const int va = tri.v[e];
				const int vb = tri.v[(e + 1) % 3];
				const int vc = tri.v[(e + 2) % 3];
				const int tA = emit(va, false), tB = emit(vb, false);
				const int bA = emit(va, true), bB = emit(vb, true);

				// outward direction in the plane, away from the third vertex
				const dvec3 pa = V3(c.verts[va].position);
				const dvec3 pb = V3(c.verts[vb].position);
				const dvec3 pc = V3(c.verts[vc].position);
				dvec3 o = Cross(n, pb - pa);
				if ( Dot(o, pc - pa) > 0.0 )
					o = o * -1.0;

				// same facing convention as the base triangles; the last
				// (provoking) vertex is a bottom one: flat wall flag
				const dvec3 qtA = V3(out.verts[info.firstVert + tA].base.position);
				const dvec3 qbA = V3(out.verts[info.firstVert + bA].base.position);
				const dvec3 qbB = V3(out.verts[info.firstVert + bB].base.position);
				const dvec3 r1 = Cross(qbA - qtA, qbB - qtA);
				const int facing = Dot(r1, o) >= 0.0 ? 1 : -1;
				int quad[6];
				if ( facing == tri.orientation )
				{
					quad[0] = tA; quad[1] = bA; quad[2] = bB;
					quad[3] = tB; quad[4] = tA; quad[5] = bB;
				}
				else
				{
					quad[0] = bA; quad[1] = tA; quad[2] = bB;
					quad[3] = tA; quad[4] = tB; quad[5] = bB;
				}
				for ( int k = 0; k < 6; k++ )
					out.indexes.push_back(info.firstVert + quad[k]);

				// the edge in texture space, the group on its left
				const float *uva = c.verts[va].texcoords[0];
				const float *uvb = c.verts[vb].texcoords[0];
				const float *uvc = c.verts[vc].texcoords[0];
				const double side = (uvb[0] - uva[0]) * (uvc[1] - uva[1]) - (uvb[1] - uva[1]) * (uvc[0] - uva[0]);
				if ( side >= 0.0 )
					out.AddTexel(uva[0], uva[1], uvb[0], uvb[1]);
				else
					out.AddTexel(uvb[0], uvb[1], uva[0], uva[1]);
				numEdges++;
				info.walls++;
			}
		}

		float *h = &out.texels[header * 4];
		h[0] = (float)firstEdge;
		h[1] = (float)numEdges;
	}

	info.numVerts = (int)out.verts.size() - info.firstVert;
	info.numIndexes = (int)out.indexes.size() - info.firstIndex;
	return nullptr;
}

// widen culling so the displaced volume is not culled before its base
static void R_PomExpandCulling( world_t *world, msurface_t *surf, const srfPomShell_t *shell )
{
	cullinfo_t *ci = &surf->cullinfo;
	if ( ci->type & CULLINFO_BOX )
	{
		AddPointToBounds(shell->bounds[0], ci->bounds[0], ci->bounds[1]);
		AddPointToBounds(shell->bounds[1], ci->bounds[0], ci->bounds[1]);
	}
	if ( ci->type & CULLINFO_SPHERE )
	{
		for ( int i = 0; i < 8; i++ )
		{
			const vec3_t corner = {
				shell->bounds[(i >> 0) & 1][0],
				shell->bounds[(i >> 1) & 1][1],
				shell->bounds[(i >> 2) & 1][2] };
			ci->radius = Q_max(ci->radius, Distance(corner, ci->localOrigin));
		}
	}
}

} // namespace

void R_PomSilhouetteFinishWorld( world_t *world )
{
	if ( !r_pomSilhouette->integer )
		return;

	std::vector<pomCandidate_t> candidates;
	candidates.swap(s_pom.candidates);
	if ( candidates.empty() )
		return;

	if ( !qglTexBuffer || !tr.lightallSilhouetteShader[0].program )
	{
		for ( const pomCandidate_t& c : candidates )
			R_PomSilhouetteFallback(c.surf->shader, "renderer started without r_pomSilhouette (vid_restart)");
		return;
	}

	pomBuilder_t builder;
	std::vector<pomSurfaceOutput_t> outputs;
	std::vector<msurface_t *> surfaces;
	for ( const pomCandidate_t& c : candidates )
	{
		const size_t verts = builder.verts.size();
		const size_t indexes = builder.indexes.size();
		const size_t texels = builder.texels.size();
		pomSurfaceOutput_t info = {};
		const char *reason = R_PomBuildSurface(c, builder, info);
		if ( !reason && info.numIndexes <= 0 )
			reason = "no triangles";
		if ( reason )
		{
			// roll back a partial surface
			builder.verts.resize(verts);
			builder.indexes.resize(indexes);
			builder.texels.resize(texels);
			R_PomSilhouetteFallback(c.surf->shader, reason);
			continue;
		}
		outputs.push_back(info);
		surfaces.push_back(c.surf);
	}

	if ( outputs.empty() )
		return;

	const int numTexels = (int)(builder.texels.size() / 4);
	if ( numTexels > glRefConfig.maxTextureBufferSize )
	{
		for ( msurface_t *surf : surfaces )
			R_PomSilhouetteFallback(surf->shader, "group buffer larger than GL_MAX_TEXTURE_BUFFER_SIZE");
		return;
	}

	// GPU data: one shell VBO / IBO and one group buffer texture per world
	VBO_t *vbo = R_CreateVBO((byte *)builder.verts.data(),
		builder.verts.size() * sizeof(pomShellVertex_t), VBO_USAGE_STATIC,
		va("%s_pomShell", world->baseName));
	IBO_t *ibo = R_CreateIBO((byte *)builder.indexes.data(),
		builder.indexes.size() * sizeof(glIndex_t), VBO_USAGE_STATIC,
		va("%s_pomShell", world->baseName));

	const size_t stride = sizeof(pomShellVertex_t);
	const size_t baseOffset = offsetof(pomShellVertex_t, base);
	vbo->offsets[ATTR_INDEX_POSITION] = baseOffset + offsetof(packedVertex_t, position);
	vbo->offsets[ATTR_INDEX_NORMAL] = baseOffset + offsetof(packedVertex_t, normal);
	vbo->offsets[ATTR_INDEX_TANGENT] = baseOffset + offsetof(packedVertex_t, tangent);
	vbo->offsets[ATTR_INDEX_TEXCOORD0] = baseOffset + offsetof(packedVertex_t, texcoords[0]);
	vbo->offsets[ATTR_INDEX_TEXCOORD1] = baseOffset + offsetof(packedVertex_t, texcoords[1]);
	vbo->offsets[ATTR_INDEX_TEXCOORD2] = baseOffset + offsetof(packedVertex_t, texcoords[2]);
	vbo->offsets[ATTR_INDEX_TEXCOORD3] = baseOffset + offsetof(packedVertex_t, texcoords[3]);
	vbo->offsets[ATTR_INDEX_TEXCOORD4] = baseOffset + offsetof(packedVertex_t, texcoords[4]);
	vbo->offsets[ATTR_INDEX_COLOR] = baseOffset + offsetof(packedVertex_t, colors);
	vbo->offsets[ATTR_INDEX_LIGHTDIRECTION] = baseOffset + offsetof(packedVertex_t, lightDirection);
	vbo->offsets[ATTR_INDEX_POSITION2] = offsetof(pomShellVertex_t, pom);

	const int attribs[] = {
		ATTR_INDEX_POSITION, ATTR_INDEX_NORMAL, ATTR_INDEX_TANGENT, ATTR_INDEX_TEXCOORD0,
		ATTR_INDEX_TEXCOORD1, ATTR_INDEX_TEXCOORD2, ATTR_INDEX_TEXCOORD3, ATTR_INDEX_TEXCOORD4,
		ATTR_INDEX_COLOR, ATTR_INDEX_LIGHTDIRECTION, ATTR_INDEX_POSITION2 };
	for ( int attrib : attribs )
		vbo->strides[attrib] = stride;

	const packedVertex_t *pv = nullptr;
	vbo->sizes[ATTR_INDEX_POSITION] = sizeof(pv->position);
	vbo->sizes[ATTR_INDEX_NORMAL] = sizeof(pv->normal);
	vbo->sizes[ATTR_INDEX_TANGENT] = sizeof(pv->tangent);
	vbo->sizes[ATTR_INDEX_TEXCOORD0] = sizeof(pv->texcoords[0]);
	vbo->sizes[ATTR_INDEX_TEXCOORD1] = sizeof(pv->texcoords[0]);
	vbo->sizes[ATTR_INDEX_TEXCOORD2] = sizeof(pv->texcoords[0]);
	vbo->sizes[ATTR_INDEX_TEXCOORD3] = sizeof(pv->texcoords[0]);
	vbo->sizes[ATTR_INDEX_TEXCOORD4] = sizeof(pv->texcoords[0]);
	vbo->sizes[ATTR_INDEX_COLOR] = sizeof(pv->colors);
	vbo->sizes[ATTR_INDEX_LIGHTDIRECTION] = sizeof(pv->lightDirection);
	vbo->sizes[ATTR_INDEX_POSITION2] = sizeof(vec3_t);

	pomWorldObjects_t objects = {};
	qglGenBuffers(1, &objects.buffer);
	qglBindBuffer(GL_TEXTURE_BUFFER, objects.buffer);
	qglBufferData(GL_TEXTURE_BUFFER, builder.texels.size() * sizeof(float),
		builder.texels.data(), GL_STATIC_DRAW);
	objects.image = (image_t *)Z_Malloc(sizeof(image_t), TAG_GENERAL, qtrue);
	Q_strncpyz(objects.image->imgName, va("*pomGroups_%s", world->baseName), sizeof(objects.image->imgName));
	objects.image->flags = IMGFLAG_TEXBUFFER;
	qglGenTextures(1, &objects.image->texnum);
	GL_BindToTMU(objects.image, TB_POM_GROUPS);
	qglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, objects.buffer);
	qglBindBuffer(GL_TEXTURE_BUFFER, 0);
	s_pom.objects.push_back(objects);
	s_pomObjectsDiagnostic.expectedCount++;
	R_PomSnapshotObjects();

	world->pomGroupsImage = objects.image;
	world->pomGroupsBuffer = objects.buffer;
	world->pomGroupsTexels = numTexels;
	world->numPomShells = (int)outputs.size();
	world->pomShells = (srfPomShell_t *)Hunk_Alloc(world->numPomShells * sizeof(srfPomShell_t), h_low);

	int groups = 0, walls = 0;
	for ( int i = 0; i < world->numPomShells; i++ )
	{
		const pomSurfaceOutput_t& info = outputs[i];
		msurface_t *surf = surfaces[i];
		srfPomShell_t *shell = &world->pomShells[i];
		shell->surfaceType = SF_POM_SHELL;
		shell->fadeBaseType = SF_POM_FADEBASE;
		shell->base = (srfBspSurface_t *)surf->data;
		shell->surf = surf;
		shell->numVerts = info.numVerts;
		shell->numIndexes = info.numIndexes;
		shell->firstIndex = info.firstIndex;
		shell->minIndex = info.firstVert;
		shell->maxIndex = info.firstVert + info.numVerts - 1;
		shell->vbo = vbo;
		shell->ibo = ibo;
		shell->groupsImage = objects.image;
		VectorCopy(info.bounds[0], shell->bounds[0]);
		VectorCopy(info.bounds[1], shell->bounds[1]);
		// the shell volume contains the base surface
		const srfBspSurface_t *base = shell->base;
		for ( int v = 0; v < base->numVerts; v++ )
			AddPointToBounds(base->verts[v].xyz, shell->bounds[0], shell->bounds[1]);
		shell->above = info.above;
		shell->below = info.below;
		shell->numGroups = info.groups;
		shell->numWalls = info.walls;
		surf->pomShell = shell;
		R_PomExpandCulling(world, surf, shell);
		groups += info.groups;
		walls += info.walls;
	}

	// BSP node / leaf and brush model bounds: frustum culling must keep the
	// surfaces whose shell reaches into view
	const int numLeafs = world->numnodes - world->numDecisionNodes;
	for ( int i = 0; i < numLeafs; i++ )
	{
		mnode_t *leaf = world->nodes + world->numDecisionNodes + i;
		for ( int j = 0; j < leaf->nummarksurfaces; j++ )
		{
			const int surfNum = world->marksurfaces[leaf->firstmarksurface + j];
			if ( surfNum < 0 || surfNum >= world->numsurfaces )
				continue;
			const srfPomShell_t *shell = world->surfaces[surfNum].pomShell;
			if ( !shell )
				continue;
			for ( mnode_t *node = leaf; node; node = node->parent )
			{
				AddPointToBounds(shell->bounds[0], node->mins, node->maxs);
				AddPointToBounds(shell->bounds[1], node->mins, node->maxs);
			}
		}
	}
	for ( int i = 0; i < world->numBModels; i++ )
	{
		bmodel_t *bmodel = &world->bmodels[i];
		for ( int j = 0; j < bmodel->numSurfaces; j++ )
		{
			const srfPomShell_t *shell = world->surfaces[bmodel->firstSurface + j].pomShell;
			if ( !shell )
				continue;
			AddPointToBounds(shell->bounds[0], bmodel->bounds[0], bmodel->bounds[1]);
			AddPointToBounds(shell->bounds[1], bmodel->bounds[0], bmodel->bounds[1]);
		}
	}

	const size_t vboBytes = builder.verts.size() * sizeof(pomShellVertex_t);
	const size_t iboBytes = builder.indexes.size() * sizeof(glIndex_t);
	const size_t tboBytes = builder.texels.size() * sizeof(float);
	s_pom.surfaces += world->numPomShells;
	s_pom.groups += groups;
	s_pom.walls += walls;
	s_pom.shellVerts += (int)builder.verts.size();
	s_pom.shellTriangles += (int)builder.indexes.size() / 3;
	s_pom.groupTexels += numTexels;
	s_pom.vboBytes += vboBytes;
	s_pom.iboBytes += iboBytes;
	s_pom.tboBytes += tboBytes;

	ri.Printf(PRINT_ALL, "...silhouette POM: %d surfaces, %d groups, %d walls, %d verts / %d tris, %.1f KB\n",
		world->numPomShells, groups, walls, (int)builder.verts.size(), (int)builder.indexes.size() / 3,
		(vboBytes + iboBytes + tboBytes) / 1024.0f);
}

/*
============================================================

Front end

============================================================
*/

qboolean R_PomSilhouetteActive( void )
{
	return (qboolean)(r_pomSilhouette->integer &&
		r_parallaxMapping->integer &&
		tr.lightallSilhouetteShader[0].program != 0);
}

void R_PomSilhouetteBeginFrame( void )
{
	R_PomCheckObjects("begin frame");
	if ( r_parallaxMapping->integer != s_pom.lastParallaxMapping )
	{
		s_pom.lastParallaxMapping = r_parallaxMapping->integer;
		s_pom.warnedParallax = qfalse;
	}
	if ( r_autoPomSilhouetteMode->integer != s_pom.lastAutoMode )
	{
		s_pom.lastAutoMode = r_autoPomSilhouetteMode->integer;
		s_pom.warnedAuto = qfalse;
	}

	if ( r_autoPomSilhouetteMode->integer && !r_pomSilhouette->integer && !s_pom.warnedAuto )
	{
		s_pom.warnedAuto = qtrue;
		ri.Printf(PRINT_WARNING, "r_autoPomSilhouette requires r_pomSilhouette 1\n");
	}

	if ( !r_pomSilhouette->integer )
		return;

	if ( !r_parallaxMapping->integer && !s_pom.warnedParallax )
	{
		s_pom.warnedParallax = qtrue;
		ri.Printf(PRINT_WARNING, "Silhouette POM requires r_parallaxMapping 1\n");
	}

	if ( !tr.lightallSilhouetteShader[0].program && !s_pom.warnedPrograms )
	{
		s_pom.warnedPrograms = qtrue;
		ri.Printf(PRINT_WARNING, "Silhouette POM: the renderer was started without it (or with r_normalMapping 0), vid_restart to enable\n");
	}
}

// view distance where a shader's shells end
static float R_PomSilhouetteRange( const shader_t *shader )
{
	float range = r_pomSilhouetteDistance->value;
	if ( shader->silhouetteDistance > 0.0f )
		range = Q_min(range, shader->silhouetteDistance);
	return range;
}

static float R_PomSilhouetteFadeWidth( float range )
{
	return Q_min(Q_max(r_pomSilhouetteFade->value, 0.0f), range);
}

// closest / farthest distance of a point to a box
static void R_PomBoxDistances( const vec3_t point, const vec3_t bounds[2], float *dmin, float *dmax )
{
	float nearSq = 0.0f, farSq = 0.0f;
	for ( int i = 0; i < 3; i++ )
	{
		float d = 0.0f;
		if ( point[i] < bounds[0][i] )
			d = bounds[0][i] - point[i];
		else if ( point[i] > bounds[1][i] )
			d = point[i] - bounds[1][i];
		nearSq += d * d;
		const float f = Q_max(fabsf(point[i] - bounds[0][i]), fabsf(point[i] - bounds[1][i]));
		farSq += f * f;
	}
	*dmin = sqrtf(nearSq);
	*dmax = sqrtf(farSq);
}

// world point / direction to the model space of the current entity (tr.ori)
static void R_PomWorldPointToLocal( const vec3_t in, vec3_t out )
{
	vec3_t delta;
	VectorSubtract(in, tr.ori.origin, delta);
	out[0] = DotProduct(delta, tr.ori.axis[0]);
	out[1] = DotProduct(delta, tr.ori.axis[1]);
	out[2] = DotProduct(delta, tr.ori.axis[2]);
}

int R_PomSilhouetteSurfaceMode( msurface_t *surf )
{
	const srfPomShell_t *shell = surf->pomShell;
	if ( !shell || !R_PomSilhouetteActive() || !R_PomSilhouetteShaderEnabled(surf->shader) )
		return POM_SURF_ORDINARY;

	const int flags = tr.viewParms.flags;
	const float range = R_PomSilhouetteRange(surf->shader);
	if ( range <= 0.0f )
		return POM_SURF_ORDINARY;

	if ( flags & VPF_DEPTHSHADOW )
	{
		// sun cascades only (point light cubes keep the base geometry). The
		// surface stays an ordinary caster for its parts facing away from the
		// sun (flipped culling), the shell adds the displaced sun facing side.
		if ( !(flags & VPF_SHADOWCASCADES) || !r_pomSilhouetteShadows->integer )
			return POM_SURF_ORDINARY;

		vec3_t camera;
		float dmin, dmax;
		R_PomWorldPointToLocal(tr.refdef.vieworg, camera);
		R_PomBoxDistances(camera, shell->bounds, &dmin, &dmax);
		if ( dmin >= range )
			return POM_SURF_ORDINARY;

		vec3_t bounds[2];
		VectorCopy(shell->bounds[0], bounds[0]);
		VectorCopy(shell->bounds[1], bounds[1]);
		if ( R_CullLocalBox(bounds) == CULL_OUT )
			return POM_SURF_ORDINARY;
		return POM_SURF_ORDINARY | POM_SURF_SHELL;
	}

	float dmin, dmax;
	R_PomBoxDistances(tr.ori.viewOrigin, shell->bounds, &dmin, &dmax);
	if ( dmin >= range )
		return POM_SURF_ORDINARY;

	// visibility of the whole volume (the base lies inside it)
	if ( !r_nocull->integer )
	{
		vec3_t bounds[2];
		VectorCopy(shell->bounds[0], bounds[0]);
		VectorCopy(shell->bounds[1], bounds[1]);
		if ( R_CullLocalBox(bounds) == CULL_OUT )
			return 0;

		// front sided plane cull, widened by the volume below the base plane
		if ( *surf->data == SF_FACE && !(flags & VPF_ORTHOGRAPHIC) && r_facePlaneCull->integer )
		{
			const cplane_t *plane = &surf->cullinfo.plane;
			const float d = DotProduct(tr.ori.viewOrigin, plane->normal) - plane->dist;
			if ( d < -(8.0f + shell->below) )
				return 0;
		}
	}

	int mode = POM_SURF_SHELL;
	const float fade = R_PomSilhouetteFadeWidth(range);
	if ( dmax > range - fade || r_pomSilhouetteDebug->integer == 9 )
		mode |= POM_SURF_FADEBASE;
	return mode;
}

void R_PomSilhouetteAddDrawSurfs( msurface_t *surf, int mode, int entityNum, int fogIndex,
	int dlightBits, bool isPostRenderEntity, int cubemapIndex )
{
	srfPomShell_t *shell = surf->pomShell;
	if ( mode & POM_SURF_SHELL )
		R_AddDrawSurf((surfaceType_t *)shell, entityNum, surf->shader, fogIndex,
			dlightBits, isPostRenderEntity, cubemapIndex);
	if ( mode & POM_SURF_FADEBASE )
		R_AddDrawSurf(&shell->fadeBaseType, entityNum, surf->shader, fogIndex,
			dlightBits, isPostRenderEntity, cubemapIndex);
}

/*
============================================================

Back end

============================================================
*/

shaderProgram_t *RB_PomSilhouetteLightallProgram( const shaderStage_t *stage )
{
	int index = 0;
	if ( (stage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) == LIGHTDEF_USE_LIGHT_VERTEX )
		index |= POMSDEF_LIGHT_VERTEX;
	if ( stage->glslShaderIndex & LIGHTDEF_USE_SPEC_GLOSS )
		index |= POMSDEF_SPEC_GLOSS;
	if ( stage->glslShaderIndex & LIGHTDEF_USE_CLOTH_BRDF )
		index |= POMSDEF_CLOTH_BRDF;
	return &tr.lightallSilhouetteShader[index];
}

shaderProgram_t *RB_PomSilhouetteDepthProgram( void )
{
	if ( tr.depthVelocityFbo != nullptr && glState.currentFBO == tr.depthVelocityFbo )
		return &tr.pomSilhouetteDepthShader[POMSDEF_DEPTH_VELOCITY];
	return &tr.pomSilhouetteDepthShader[POMSDEF_DEPTH_ONLY];
}

shaderProgram_t *RB_PomSilhouetteFogProgram( int fogBits )
{
	return &tr.fogSilhouetteShader[(fogBits & FOGDEF_USE_FALLBACK_GLOBAL_FOG) ? 1 : 0];
}

// a depth prepass of this view already holds the virtual depth of the shells
static qboolean RB_PomSilhouetteBehindPrepass( void )
{
	return (qboolean)(r_depthPrepass->integer &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		!(backEnd.viewParms.flags & VPF_DEPTHSHADOW));
}

// Colour and fog passes of shells after a depth pass: LEQUAL against the
// depth written before (the traced hit, pulled a little towards the camera,
// see PomShellDepth), without writing depth
uint32_t RB_PomSilhouetteStateBits( uint32_t stateBits, qboolean fogPass )
{
	if ( tess.pomMode != POM_MODE_SHELL || backEnd.depthFill )
		return stateBits;
	if ( fogPass || RB_PomSilhouetteBehindPrepass() )
		stateBits &= ~(GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_BITS);
	return stateBits;
}

void RB_PomSilhouetteSetupDraw( const shaderStage_t *stage, UniformDataWriter& uniforms,
	SamplerBindingsWriter& samplers, qboolean fogPass )
{
	const shader_t *shader = tess.shader;
	const viewParms_t& viewParms = backEnd.viewParms;
	const bool depthShadow = (viewParms.flags & VPF_DEPTHSHADOW) != 0;
	const bool ortho = (viewParms.flags & VPF_ORTHOGRAPHIC) != 0;

	const float maxSteps = shader->silhouetteSteps > 0 ?
		(float)shader->silhouetteSteps : (float)r_pomSilhouetteMaxSteps->integer;
	const float minSteps = Q_min((float)r_pomSilhouetteSteps->integer, maxSteps);
	const vec4_t params = {
		minSteps,
		maxSteps,
		(float)r_pomSilhouetteBinarySteps->integer,
		r_pomSilhouetteViewDependence->value };

	float depthMode = 0.0f;
	if ( fogPass || (!backEnd.depthFill && RB_PomSilhouetteBehindPrepass()) )
		depthMode = 1.0f;

	// world size of a pixel of an orthographic view (sun cascades)
	float orthoFootprint = 0.0f;
	if ( ortho && viewParms.viewportWidth > 0 && fabsf(viewParms.projectionMatrix[0]) > 1e-12f )
		orthoFootprint = (2.0f / fabsf(viewParms.projectionMatrix[0])) / (float)viewParms.viewportWidth;

	const int debugView = depthShadow ? 0 : r_pomSilhouetteDebug->integer;
	const vec4_t params2 = {
		(float)tess.pomMode,
		depthMode,
		orthoFootprint,
		(float)debugView };

	vec4_t fade;
	if ( depthShadow )
	{
		// no crossfade in shadows; the caster is pushed away from the sun
		VectorSet4(fade, 1e30f, 0.0f, -1.0f, Q_max(0.25f, 2.0f * r_shadowDepthBias->value));
	}
	else
	{
		const float range = R_PomSilhouetteRange(shader);
		const float width = R_PomSilhouetteFadeWidth(range);
		const float split = (debugView == 9) ?
			(float)viewParms.viewportX + 0.5f * (float)viewParms.viewportWidth : -1.0f;
		// .w: screen-space contact shadows on shell pixels (colour passes)
		VectorSet4(fade, range - width, width > 0.0f ? 1.0f / width : 1e6f, split,
			r_pomSilhouetteContactShadows->integer ? 1.0f : 0.0f);
	}

	uniforms.SetUniformVec4(UNIFORM_POMPARAMS, params);
	uniforms.SetUniformVec4(UNIFORM_POMPARAMS2, params2);
	uniforms.SetUniformVec4(UNIFORM_POMFADE, fade);
	// the stage iterator sets u_NormalScale (with the portal sign), the fog pass does not
	if ( fogPass )
		uniforms.SetUniformVec4(UNIFORM_NORMALSCALE, stage->normalScale);

	if ( tess.pomGroupsImage )
		samplers.AddStaticImage(tess.pomGroupsImage, TB_POM_GROUPS);
	if ( backEnd.depthFill || fogPass )
		samplers.AddAnimatedImage((textureBundle_t *)&stage->bundle[TB_NORMALMAP], TB_NORMALMAP);
}

void RB_PomSilhouetteNoteDebugBase( const srfBspSurface_t *base )
{
	if ( s_pom.debugBases.size() < 4096 )
		s_pom.debugBases.push_back(base);
}

int RB_PomSilhouetteDebugBases( const srfBspSurface_t * const **bases )
{
	*bases = s_pom.debugBases.data();
	return (int)s_pom.debugBases.size();
}

void RB_PomSilhouetteClearDebugBases( void )
{
	s_pom.debugBases.clear();
}

qboolean RB_PomSilhouetteDebugBypassesToneMap( void )
{
	const int view = r_pomSilhouetteDebug->integer;
	return (qboolean)(R_PomSilhouetteActive() && view >= 4 && view != 9);
}

/*
============================================================

Commands, shutdown

============================================================
*/

void R_PomSilhouetteInfo_f( void )
{
	ri.Printf(PRINT_ALL, "Silhouette POM: r_pomSilhouette %d, %s\n", r_pomSilhouette->integer,
		R_PomSilhouetteActive() ? "active" :
		(!tr.lightallSilhouetteShader[0].program ? "programs not loaded (vid_restart)" :
		(!r_parallaxMapping->integer ? "needs r_parallaxMapping 1" : "off")));
	ri.Printf(PRINT_ALL, "  shells: %d surfaces, %d groups, %d boundary walls\n",
		s_pom.surfaces, s_pom.groups, s_pom.walls);
	int keyword = 0, automatic = 0, used = 0;
	if ( tr.world )
	{
		for ( int i = 0; i < tr.world->numPomShells; i++ )
		{
			shader_t *shader = tr.world->pomShells[i].surf->shader;
			if ( shader->pomSilhouetteSource == POM_SOURCE_KEYWORD )
				keyword++;
			else
				automatic++;
			if ( R_PomSilhouetteShaderEnabled(shader) )
				used++;
		}
	}
	ri.Printf(PRINT_ALL, "  sources: %d keyword / %d automatic surfaces, %d in use (r_autoPomSilhouette %d, %d switches)\n",
		keyword, automatic, used, r_autoPomSilhouetteMode->integer, (int)s_pom.overrides.size());
	ri.Printf(PRINT_ALL, "  geometry: %d verts (%d bytes each), %d triangles, %d group texels\n",
		s_pom.shellVerts, (int)sizeof(pomShellVertex_t), s_pom.shellTriangles, s_pom.groupTexels);
	ri.Printf(PRINT_ALL, "  memory: VBO %.1f KB, IBO %.1f KB, group buffer %.1f KB\n",
		s_pom.vboBytes / 1024.0f, s_pom.iboBytes / 1024.0f, s_pom.tboBytes / 1024.0f);
	ri.Printf(PRINT_ALL, "  last frame: %d shells, %d shell triangles, %d crossfade base surfaces (all passes)\n",
		backEnd.pc.c_pomShellSurfaces, backEnd.pc.c_pomShellTriangles, backEnd.pc.c_pomFadeSurfaces);
	if ( s_pom.fallbacks.empty() )
	{
		ri.Printf(PRINT_ALL, "  no fallbacks\n");
		return;
	}
	ri.Printf(PRINT_ALL, "  fallbacks to ordinary POM (surfaces):\n");
	for ( const pomFallback_t& f : s_pom.fallbacks )
		ri.Printf(PRINT_ALL, "    %5d  %s\n", f.surfaces, f.reason);
	ri.Printf(PRINT_ALL, "  shaders: developer 1 prints one line per shader at map load\n");
}

void R_ShutdownPomSilhouette( void )
{
	R_PomCheckObjects("shutdown");
	for ( pomWorldObjects_t& o : s_pom.objects )
	{
		if ( o.image )
		{
			qglDeleteTextures(1, &o.image->texnum);
			Z_Free(o.image);
		}
		if ( o.buffer )
			qglDeleteBuffers(1, &o.buffer);
	}
	s_pom.objects.clear();
	s_pomObjectsDiagnostic.expectedCount = 0;
	R_PomSnapshotObjects();
	s_pom.candidates.clear();
	s_pom.fallbacks.clear();
	s_pom.warnedShaders.clear();
	s_pom.debugBases.clear();
	s_pom.skipped.clear();
	s_pom.surfaces = s_pom.groups = s_pom.walls = 0;
	s_pom.shellVerts = s_pom.shellTriangles = s_pom.groupTexels = 0;
	s_pom.vboBytes = s_pom.iboBytes = s_pom.tboBytes = 0;
	s_pom.warnedPrograms = qfalse;
}

/*
============================================================

r_autoPomSilhouette command

============================================================
*/

static const char *R_PomSilhouetteSourceName( int source )
{
	return source == POM_SOURCE_KEYWORD ? "keyword" : (source == POM_SOURCE_AUTO ? "auto" : "-");
}

static const char *R_PomSilhouetteOverrideName( int value )
{
	return value < 0 ? "-" : (value ? "on" : "off");
}

// shaders of the current map with shells: surfaces per shader
static void R_PomSilhouetteShellShaders( std::vector<std::pair<shader_t *, int>>& out )
{
	out.clear();
	if ( !tr.world )
		return;
	for ( int i = 0; i < tr.world->numPomShells; i++ )
	{
		shader_t *shader = tr.world->pomShells[i].surf->shader;
		bool found = false;
		for ( auto& e : out )
		{
			if ( e.first == shader )
			{
				e.second++;
				found = true;
				break;
			}
		}
		if ( !found )
			out.push_back({ shader, 1 });
	}
}

static void R_PomSilhouetteList( void )
{
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "no map loaded\n");
		return;
	}
	if ( !r_pomSilhouette->integer )
		ri.Printf(PRINT_ALL, "r_pomSilhouette is 0: no shells were built (r_pomSilhouette 1, vid_restart)\n");

	std::vector<std::pair<shader_t *, int>> shaders;
	R_PomSilhouetteShellShaders(shaders);
	ri.Printf(PRINT_ALL, "shaders with a silhouette shell on this map (r_autoPomSilhouette %d):\n",
		r_autoPomSilhouetteMode->integer);
	ri.Printf(PRINT_ALL, "  state source  switch  surfaces  shader\n");
	for ( const auto& e : shaders )
	{
		ri.Printf(PRINT_ALL, "  %-5s %-7s %-6s  %8d  %s\n",
			R_PomSilhouetteShaderEnabled(e.first) ? "on" : "off",
			R_PomSilhouetteSourceName(e.first->pomSilhouetteSource),
			R_PomSilhouetteOverrideName(R_PomSilhouetteOverride(e.first)), e.second, e.first->name);
	}
	if ( shaders.empty() )
		ri.Printf(PRINT_ALL, "  (none)\n");

	if ( !s_pom.skipped.empty() )
	{
		ri.Printf(PRINT_ALL, "POM materials without a shell (ordinary POM):\n");
		for ( const pomSkipped_t& k : s_pom.skipped )
			ri.Printf(PRINT_ALL, "  %8d  %s  (%s)\n", k.surfaces, k.shader->name, k.reason);
	}
}

static void R_PomSilhouetteUsage( void )
{
	ri.Printf(PRINT_ALL, "usage:\n"
		"  r_autoPomSilhouette 1|0                    silhouette POM for every material with ordinary POM\n"
		"  r_autoPomSilhouette <shader> 1|0|default   switch one shader ('textures/bespin/*' = prefix), saved to %s\n"
		"  r_autoPomSilhouette <shader>               state of a shader\n"
		"  r_autoPomSilhouette list                   shaders of the current map\n"
		"  r_autoPomSilhouette clear                  remove every per-shader switch\n", POM_OVERRIDE_FILE);
}

// what a switch did to the loaded shaders
static void R_PomSilhouetteReportPattern( const char *pattern )
{
	std::vector<std::pair<shader_t *, int>> shells;
	R_PomSilhouetteShellShaders(shells);
	int matched = 0;
	for ( int i = 0; i < tr.numShaders; i++ )
	{
		shader_t *shader = tr.shaders[i];
		int score;
		if ( !R_PomPatternMatches(pattern, shader->name, &score) )
			continue;
		int surfaces = 0;
		for ( const auto& e : shells )
		{
			if ( e.first == shader )
				surfaces += e.second;
		}
		if ( !surfaces && shader->pomSilhouetteSource == POM_SOURCE_NONE )
			continue;	// not a POM material of this map (other lightmap variants, models)
		matched++;
		ri.Printf(PRINT_ALL, "  %-3s %s: %d shell surfaces, source %s, switch %s\n",
			R_PomSilhouetteShaderEnabled(shader) ? "on" : "off", shader->name, surfaces,
			R_PomSilhouetteSourceName(shader->pomSilhouetteSource),
			R_PomSilhouetteOverrideName(R_PomSilhouetteOverride(shader)));
		if ( !surfaces )
		{
			for ( const pomSkipped_t& k : s_pom.skipped )
			{
				if ( k.shader == shader )
					ri.Printf(PRINT_ALL, "      no shell: %s\n", k.reason);
			}
		}
	}
	if ( !matched )
	{
		ri.Printf(PRINT_ALL, "  no POM material of the current map matches '%s'%s\n", pattern,
			r_pomSilhouette->integer ? " (it needs a normal + height map, e.g. an _nh image)" :
			" (r_pomSilhouette is 0: no shells were built)");
	}
}

void R_AutoPomSilhouette_f( void )
{
	R_PomSilhouetteLoadOverrides();

	const int argc = ri.Cmd_Argc();
	if ( argc < 2 )
	{
		ri.Printf(PRINT_ALL, "r_autoPomSilhouette %d (r_pomSilhouette %d), %d per-shader switches in %s\n",
			r_autoPomSilhouetteMode->integer, r_pomSilhouette->integer, (int)s_pom.overrides.size(), POM_OVERRIDE_FILE);
		for ( const pomOverride_t& o : s_pom.overrides )
			ri.Printf(PRINT_ALL, "  %s %d\n", o.pattern, o.value);
		R_PomSilhouetteUsage();
		return;
	}

	const char *arg = ri.Cmd_Argv(1);
	if ( argc == 2 )
	{
		if ( !Q_stricmp(arg, "list") )
		{
			R_PomSilhouetteList();
		}
		else if ( !strcmp(arg, "0") || !strcmp(arg, "1") )
		{
			ri.Cvar_Set("r_autoPomSilhouetteMode", arg);
			if ( !r_pomSilhouette->integer )
				ri.Printf(PRINT_WARNING, "r_autoPomSilhouette requires r_pomSilhouette 1\n");
			s_pom.warnedAuto = qtrue;
		}
		else if ( !Q_stricmp(arg, "clear") )
		{
			s_pom.overrides.clear();
			s_pom.overrideGeneration++;
			R_PomSilhouetteSaveOverrides();
			ri.Printf(PRINT_ALL, "per-shader switches removed\n");
		}
		else if ( !Q_stricmp(arg, "help") || !Q_stricmp(arg, "?") )
		{
			R_PomSilhouetteUsage();
		}
		else
		{
			R_PomSilhouetteReportPattern(arg);
		}
		return;
	}

	const char *valueArg = ri.Cmd_Argv(2);
	int value;
	if ( !strcmp(valueArg, "1") || !Q_stricmp(valueArg, "on") )
		value = 1;
	else if ( !strcmp(valueArg, "0") || !Q_stricmp(valueArg, "off") )
		value = 0;
	else if ( !Q_stricmp(valueArg, "default") || !strcmp(valueArg, "-1") )
		value = -1;
	else
	{
		R_PomSilhouetteUsage();
		return;
	}
	if ( strlen(arg) >= MAX_QPATH )
	{
		ri.Printf(PRINT_WARNING, "shader name too long\n");
		return;
	}

	for ( size_t i = 0; i < s_pom.overrides.size(); i++ )
	{
		if ( !Q_stricmp(s_pom.overrides[i].pattern, arg) )
		{
			s_pom.overrides.erase(s_pom.overrides.begin() + i);
			break;
		}
	}
	if ( value >= 0 )
	{
		pomOverride_t o = {};
		Q_strncpyz(o.pattern, arg, sizeof(o.pattern));
		o.value = value;
		s_pom.overrides.push_back(o);
	}
	s_pom.overrideGeneration++;
	R_PomSilhouetteSaveOverrides();

	ri.Printf(PRINT_ALL, "%s: %s (saved to %s)\n", arg,
		value < 0 ? "default (keyword / r_autoPomSilhouette)" : (value ? "on" : "off"), POM_OVERRIDE_FILE);
	R_PomSilhouetteReportPattern(arg);
}
