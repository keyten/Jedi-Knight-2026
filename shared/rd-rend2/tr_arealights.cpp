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

/*
LTC area lights (r_ltcAreaLights), Forward+ only. See
docs/rend2-ltc-area-lights.md.

Area lights are ordinary dlight_t entries with areaType != DLIGHT_POINT, so
they ride the Forward+ importance sort and cluster lists unchanged: the
bounding sphere (origin = centre, radius = influence range) is the cull
volume. lightall shades them with LTC (USE_LTC, compiled in only when the
latched cvar is set). They never enter the legacy 32 light path, the legacy
Lights UBO (froxel fog) nor the shadow cube selection.

Sources, merged into the scene light list:
  map file    maps/<map>.arealights.json, loaded with the map, reloaded with
              r_ltcReloadLights; static lamps default to specular only
              (their diffuse light is already in the lightmap)
  scene API   RE_AddAreaLightToScene / RE_AddLineLightToScene (sabers),
              dynamic: diffuse + specular, diffuse feeds SSGI

The LUTs (tr_ltc_data.h) come from tools/ltcfit, never fitted at startup.
*/

#include "tr_local.h"
#include "json.h"
#include "tr_ltc_data.h"
#include "tr_volrecon.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// saber blade radiance per unit of saber color (r_ltcIntensityScale applies too)
#define SABER_AREA_RADIANCE		4.0f
#define AREALIGHT_MIN_RANGE		16.0f
#define AREALIGHT_MAX_RANGE		8192.0f

struct mapAreaLight_t
{
	int type;					// DLIGHT_RECT, DLIGHT_LINE
	vec3_t center;
	vec3_t right;
	vec3_t up;
	float halfWidth;
	float halfHeight;
	vec3_t color;				// radiance = color * intensity
	float intensity;
	float range;
	float halfDiagonal;			// cached geometry for scene selection
	float power;				// cached radiance * emitting area
	qboolean twoSided;
	int mode;					// AREAMODE_*
	qboolean automatic;			// r_ltcAutoAreaLights, not from a file
	char name[64];
};

enum
{
	AREAMODE_STATIC_SPECULAR,	// default: spec only unless r_ltcStaticDiffuse 1
	AREAMODE_STATIC_FULL,		// static, diffuse + specular (lamps missing from the lightmap)
	AREAMODE_DYNAMIC			// treated like a scene light (diffuse feeds SSGI)
};

static const char *s_modeNames[] = { "static_specular", "static_full", "dynamic" };

static struct
{
	std::vector<mapAreaLight_t> lights;
	char mapName[MAX_QPATH];
	qboolean active;			// latched per frame
	qboolean unitsChecked;
	qboolean unitsOk;
	int ltcModCount;
	int fplusModCount;
	int saberModCount;
	qhandle_t debugShader;
	int selected;				// map light highlighted by r_ltcDebug, -1 none
} s_al = { {}, "", qfalse, qfalse, qfalse, -1, -1, -1, 0, -1 };

// Keep near-equal map lights from trading the global budget every frame.
static std::vector<unsigned char> s_previousMapSelection;

qboolean R_AreaLightsActive( void )
{
	return s_al.active;
}

/*
============================================================

Frame state, dependencies

============================================================
*/

void R_AreaLightsBeginFrame( void )
{
	if ( !s_al.unitsChecked )
	{
		GLint units = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
		s_al.unitsOk = (qboolean)(units > TB_LTC_AMPLITUDE);
		s_al.unitsChecked = qtrue;
	}

	const qboolean wanted = (qboolean)(r_ltcAreaLights->integer != 0);
	const qboolean fplus = R_ForwardPlusActive();

	// one message per change of the cvars involved, not per frame; only the
	// first missing dependency is reported
	if ( r_ltcAreaLights->modificationCount != s_al.ltcModCount ||
		r_forwardPlus->modificationCount != s_al.fplusModCount ||
		r_ltcSaberAreaLights->modificationCount != s_al.saberModCount )
	{
		s_al.ltcModCount = r_ltcAreaLights->modificationCount;
		s_al.fplusModCount = r_forwardPlus->modificationCount;
		s_al.saberModCount = r_ltcSaberAreaLights->modificationCount;

		if ( wanted && !s_al.unitsOk )
			ri.Printf(PRINT_WARNING, "LTC area lights need more than %d texture units, disabled\n", TB_LTC_AMPLITUDE + 1);
		else if ( wanted && !fplus )
			ri.Printf(PRINT_ALL, "LTC area lights require r_forwardPlus 1\n");
		else if ( !wanted && r_ltcSaberAreaLights->integer )
			ri.Printf(PRINT_ALL, "Saber area lights require r_ltcAreaLights 1\n");
	}

	s_al.active = (qboolean)(wanted && fplus && s_al.unitsOk && tr.ltcMatrixImage && tr.ltcAmplitudeImage);
}

float R_AreaLightsDebugParam( void )
{
	return s_al.active ? (float)Com_Clampi(0, 8, r_ltcDebug->integer) : 0.0f;
}

/*
============================================================

Lookup tables

============================================================
*/

void R_CreateLtcImages( void )
{
	std::vector<uint16_t> data(LTC_LUT_SIZE * LTC_LUT_SIZE * 4);
	const float *tables[2] = { ltcTable1, ltcTable2 };
	const char *names[2] = { "*ltcMatrixLUT", "*ltcAmplitudeLUT" };
	image_t **images[2] = { &tr.ltcMatrixImage, &tr.ltcAmplitudeImage };
	for ( int t = 0; t < 2; t++ )
	{
		for ( size_t i = 0; i < data.size(); i++ )
			data[i] = FloatToHalf(tables[t][i]);
		*images[t] = R_CreateImage(
			names[t], (byte *)data.data(), LTC_LUT_SIZE, LTC_LUT_SIZE,
			IMGTYPE_COLORALPHA, IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);
	}
}

void RB_AreaLightsBindTextures( SamplerBindingsWriter& samplers )
{
	samplers.AddStaticImage(tr.ltcMatrixImage, TB_LTC_MATRIX);
	samplers.AddStaticImage(tr.ltcAmplitudeImage, TB_LTC_AMPLITUDE);
}

/*
============================================================

Map file

============================================================
*/

static qboolean R_JsonVec3( const char *obj, const char *end, const char *name, vec3_t out )
{
	const char *value = JSON_ObjectGetNamedValue(obj, end, name);
	if ( !value || JSON_ValueGetType(value, end) != JSONTYPE_ARRAY )
		return qfalse;
	const char *indexes[3];
	if ( JSON_ArrayGetIndex(value, end, indexes, 3) < 3 )
		return qfalse;
	for ( int i = 0; i < 3; i++ )
		out[i] = JSON_ValueGetFloat(indexes[i], end);
	return qtrue;
}

static float R_JsonFloat( const char *obj, const char *end, const char *name, float def )
{
	const char *value = JSON_ObjectGetNamedValue(obj, end, name);
	if ( !value || JSON_ValueGetType(value, end) != JSONTYPE_VALUE )
		return def;
	return JSON_ValueGetFloat(value, end);
}

static qboolean R_JsonBool( const char *obj, const char *end, const char *name, qboolean def )
{
	const char *value = JSON_ObjectGetNamedValue(obj, end, name);
	if ( !value || JSON_ValueGetType(value, end) != JSONTYPE_VALUE )
		return def;
	if ( *value == 't' )
		return qtrue;
	if ( *value == 'f' )
		return qfalse;
	return (qboolean)(JSON_ValueGetFloat(value, end) != 0.0f);
}

// default influence range: where the irradiance of the emitter seen face on
// drops to ~1% of its radiance (E ~ L * A / d^2), at least a bit beyond the
// emitter itself
static float R_AreaLightDefaultRange( const mapAreaLight_t *l )
{
	const float area = 4.0f * l->halfWidth * Q_max(l->halfHeight, 1.0f);
	const float radiance = Q_max(l->intensity * MAX(l->color[0], MAX(l->color[1], l->color[2])), 0.05f);
	const float range = sqrtf(area * radiance / 0.01f);
	return Com_Clamp(2.0f * Q_max(l->halfWidth, l->halfHeight) + AREALIGHT_MIN_RANGE, AREALIGHT_MAX_RANGE, range);
}

// right unit, up unit and perpendicular to right; false when degenerate
static qboolean R_AreaLightAxes( vec3_t right, vec3_t up )
{
	if ( VectorNormalize(right) < 1e-4f )
		return qfalse;
	VectorMA(up, -DotProduct(up, right), right, up);
	return (qboolean)(VectorNormalize(up) >= 1e-4f);
}

static qboolean R_ParseAreaLight( const char *obj, const char *end, int index, const char *fileName, mapAreaLight_t *l )
{
	Com_Memset(l, 0, sizeof(*l));
	char type[32] = "rect";
	const char *value = JSON_ObjectGetNamedValue(obj, end, "type");
	if ( value )
		JSON_ValueGetString(value, end, type, sizeof(type));
	value = JSON_ObjectGetNamedValue(obj, end, "name");
	if ( value )
		JSON_ValueGetString(value, end, l->name, sizeof(l->name));

	if ( !Q_stricmp(type, "rect") )
	{
		l->type = DLIGHT_RECT;
		if ( !R_JsonVec3(obj, end, "center", l->center) ||
			!R_JsonVec3(obj, end, "right", l->right) ||
			!R_JsonVec3(obj, end, "up", l->up) )
		{
			ri.Printf(PRINT_WARNING, "%s: light %d: rect needs center, right and up\n", fileName, index);
			return qfalse;
		}
		if ( !R_AreaLightAxes(l->right, l->up) )
		{
			ri.Printf(PRINT_WARNING, "%s: light %d: degenerate right / up axes\n", fileName, index);
			return qfalse;
		}
		l->halfWidth = R_JsonFloat(obj, end, "halfWidth", 0.0f);
		l->halfHeight = R_JsonFloat(obj, end, "halfHeight", 0.0f);
		l->twoSided = R_JsonBool(obj, end, "twoSided", qfalse);
	}
	else if ( !Q_stricmp(type, "line") )
	{
		vec3_t start, stop;
		l->type = DLIGHT_LINE;
		if ( !R_JsonVec3(obj, end, "start", start) || !R_JsonVec3(obj, end, "end", stop) )
		{
			ri.Printf(PRINT_WARNING, "%s: light %d: line needs start and end\n", fileName, index);
			return qfalse;
		}
		VectorAdd(start, stop, l->center);
		VectorScale(l->center, 0.5f, l->center);
		VectorSubtract(stop, start, l->right);
		l->halfWidth = 0.5f * VectorNormalize(l->right);
		l->halfHeight = R_JsonFloat(obj, end, "radius", 1.0f);
		PerpendicularVector(l->up, l->right);
		l->twoSided = qtrue;
	}
	else
	{
		ri.Printf(PRINT_WARNING, "%s: light %d: unknown type \"%s\" (rect, line)\n", fileName, index, type);
		return qfalse;
	}

	if ( l->halfWidth <= 0.0f || l->halfHeight <= 0.0f )
	{
		ri.Printf(PRINT_WARNING, "%s: light %d: half sizes must be > 0\n", fileName, index);
		return qfalse;
	}

	if ( !R_JsonVec3(obj, end, "color", l->color) )
		VectorSet(l->color, 1.0f, 1.0f, 1.0f);
	l->intensity = R_JsonFloat(obj, end, "intensity", 1.0f);

	l->mode = AREAMODE_STATIC_SPECULAR;
	char mode[32] = "";
	value = JSON_ObjectGetNamedValue(obj, end, "mode");
	if ( value && JSON_ValueGetString(value, end, mode, sizeof(mode)) )
	{
		int m;
		for ( m = 0; m < (int)ARRAY_LEN(s_modeNames); m++ )
			if ( !Q_stricmp(mode, s_modeNames[m]) )
				break;
		if ( m == (int)ARRAY_LEN(s_modeNames) )
			ri.Printf(PRINT_WARNING, "%s: light %d: unknown mode \"%s\", static_specular used\n", fileName, index, mode);
		else
			l->mode = m;
	}

	l->range = R_JsonFloat(obj, end, "range", 0.0f);
	if ( l->range <= 0.0f )
		l->range = R_AreaLightDefaultRange(l);
	l->range = Com_Clamp(AREALIGHT_MIN_RANGE, AREALIGHT_MAX_RANGE, l->range);
	return qtrue;
}

static void R_AutoAreaLights( void );
static void R_ClearImageAverages( void );
static void R_ClearAreaCandidates( void );

static void R_CacheMapAreaLightMetrics( void )
{
	for ( mapAreaLight_t& l : s_al.lights )
	{
		l.halfDiagonal = sqrtf(l.halfWidth * l.halfWidth + l.halfHeight * l.halfHeight);
		l.power = 4.0f * l.halfWidth * l.halfHeight * l.intensity *
			(0.2126f * l.color[0] + 0.7152f * l.color[1] + 0.0722f * l.color[2]);
	}
}

// the map file when there is one, else r_ltcAutoAreaLights candidates
static void R_LoadAreaLightFile( void )
{
	s_al.lights.clear();
	s_previousMapSelection.clear();
	s_al.selected = -1;
	if ( !s_al.mapName[0] )
		return;

	char fileName[MAX_QPATH];
	Com_sprintf(fileName, sizeof(fileName), "maps/%s.arealights.json", s_al.mapName);

	union { char *c; void *v; } buffer;
	const int length = ri.FS_ReadFile(fileName, &buffer.v);
	if ( !buffer.c || length <= 0 )
	{
		if ( buffer.c )
			ri.FS_FreeFile(buffer.v);
		R_AutoAreaLights();
		return;
	}
	const char *end = buffer.c + length;

	const char *lights = nullptr;
	if ( JSON_ValueGetType(buffer.c, end) == JSONTYPE_OBJECT )
		lights = JSON_ObjectGetNamedValue(buffer.c, end, "lights");
	if ( !lights || JSON_ValueGetType(lights, end) != JSONTYPE_ARRAY )
	{
		ri.Printf(PRINT_WARNING, "%s: expected { \"lights\": [ ... ] }\n", fileName);
		ri.FS_FreeFile(buffer.v);
		return;
	}

	int index = 0;
	for ( const char *obj = JSON_ArrayGetFirstValue(lights, end); obj; obj = JSON_ArrayGetNextValue(obj, end), index++ )
	{
		if ( JSON_ValueGetType(obj, end) != JSONTYPE_OBJECT )
			continue;
		mapAreaLight_t l;
		if ( R_ParseAreaLight(obj, end, index, fileName, &l) )
			s_al.lights.push_back(l);
	}
	ri.FS_FreeFile(buffer.v);
	ri.Printf(PRINT_ALL, "%s: %d area lights\n", fileName, (int)s_al.lights.size());
}

void R_LoadAreaLights( const char *mapName )
{
	Q_strncpyz(s_al.mapName, mapName ? mapName : "", sizeof(s_al.mapName));
	R_ClearAreaCandidates();
	R_LoadAreaLightFile();
	R_CacheMapAreaLightMetrics();
}

void R_ClearAreaLights( void )
{
	s_al.lights.clear();
	R_ClearAreaCandidates();
	s_al.mapName[0] = '\0';
	s_al.selected = -1;
	s_al.debugShader = 0;
	s_al.unitsChecked = qfalse;
	R_ClearImageAverages();
}

void R_ReloadAreaLights_f( void )
{
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "r_ltcReloadLights: no map loaded\n");
		return;
	}
	R_LoadAreaLightFile();
	R_CacheMapAreaLightMetrics();
	if ( s_al.lights.empty() )
		ri.Printf(PRINT_ALL, "maps/%s.arealights.json: none loaded (r_ltcAutoAreaLights %d)\n",
			s_al.mapName, r_ltcAutoAreaLights->integer);
}

/*
============================================================

Scene

============================================================
*/



static dlight_t *R_AddAreaDlight( int type, const vec3_t center, const vec3_t right, const vec3_t up,
	float halfWidth, float halfHeight, float halfDiagonal, float range,
	const vec3_t radiance, int flags, int id )
{
	dlight_t *dl = R_AllocSceneDlight();
	if ( !dl )
		return nullptr;
	VectorCopy(center, dl->origin);
	VectorCopy(radiance, dl->color);
	// the cull sphere must hold every point the window reaches
	dl->areaHalfDiagonal = halfDiagonal >= 0.0f ? halfDiagonal :
		sqrtf(halfWidth * halfWidth + halfHeight * halfHeight);
	dl->radius = range + dl->areaHalfDiagonal;
	dl->areaType = type;
	dl->areaFlags = flags;
	dl->areaId = id;
	VectorCopy(right, dl->areaRight);
	VectorCopy(up, dl->areaUp);
	dl->halfWidth = halfWidth;
	dl->halfHeight = halfHeight;
	return dl;
}

static int R_MapLightFlags( const mapAreaLight_t *l )
{
	int flags = l->twoSided ? AREALIGHT_TWO_SIDED : 0;
	if ( l->mode == AREAMODE_DYNAMIC )
		flags |= AREALIGHT_DYNAMIC;
	else if ( l->mode == AREAMODE_STATIC_SPECULAR && !r_ltcStaticDiffuse->integer )
		flags |= AREALIGHT_SPECULAR_ONLY;
	return flags;
}

static int R_NearestMapLight( const vec3_t point )
{
	int best = -1;
	float bestDist = 1e30f;
	for ( int i = 0; i < (int)s_al.lights.size(); i++ )
	{
		const float d = Distance(point, s_al.lights[i].center);
		if ( d < bestDist )
		{
			bestDist = d;
			best = i;
		}
	}
	return best;
}

static void R_AreaLightsDebugPolys( const refdef_t *fd );

// map lights of a world scene, nearest r_ltcMaxLights first (their spheres
// decide which surfaces they reach; mirrors and portals see the rest of the
// map, so no view frustum cut here: Forward+ culls per view)
void R_AddAreaLightsToScene( const refdef_t *fd )
{
	if ( !s_al.active || (fd->rdflags & RDF_NOWORLDMODEL) || !tr.world )
		return;

	s_al.selected = -1;
	if ( r_ltcDebug->integer && !s_al.lights.empty() )
	{
		const int want = r_ltcDebugLight->integer;
		s_al.selected = want < 0 ? R_NearestMapLight(fd->vieworg) :
			(want < (int)s_al.lights.size() ? want : -1);
	}

	extern int r_numdlights;
	const int maxLights = Q_min(Com_Clampi(0, MAX_RENDER_DLIGHTS, r_ltcMaxLights->integer),
		Q_max(0, R_DlightCapacity() - r_numdlights));
	const int numLights = (int)s_al.lights.size();
	if ( (int)s_previousMapSelection.size() != numLights )
		s_previousMapSelection.assign(numLights, 0);
	std::vector<std::pair<float, int>> order;
	order.reserve(numLights);
	for ( int i = 0; i < numLights; i++ )
	{
		// most important first: emitted power over squared distance (lights
		// the view is inside of rank by their size), so a big ceiling panel
		// is not pushed out by small indicators next to the camera; lights
		// whose sphere cannot reach the view origin's surroundings rank last
		const mapAreaLight_t *l = &s_al.lights[i];
		const float distSq = DistanceSquared(fd->vieworg, l->center);
		const float minDist = 0.1f * l->range;
		float score = l->power / Q_max(distSq, minDist * minDist);
		const float farDistance = l->range + l->halfDiagonal + 0.5f * l->range;
		if ( distSq > farDistance * farDistance )
			score *= 0.01f;
		if ( s_previousMapSelection[i] )
			score *= 1.15f;
		order.push_back(std::make_pair(-score, i));
	}
	const int count = Q_min(maxLights, numLights);
	std::partial_sort(order.begin(), order.begin() + count, order.end());
	std::fill(s_previousMapSelection.begin(), s_previousMapSelection.end(), 0);

	const float scale = Q_max(r_ltcIntensityScale->value, 0.0f);
	for ( int k = 0; k < count; k++ )
	{
		const int i = order[k].second;
		const mapAreaLight_t *l = &s_al.lights[i];
		vec3_t radiance;
		VectorScale(l->color, l->intensity * scale, radiance);
		int flags = R_MapLightFlags(l);
		if ( i == s_al.selected )
			flags |= AREALIGHT_SELECTED;
		if ( !R_AddAreaDlight(l->type, l->center, l->right, l->up, l->halfWidth, l->halfHeight,
				l->halfDiagonal, l->range, radiance, flags, i) )
		{
			break;
		}
		s_previousMapSelection[i] = 1;
	}

	R_AreaLightsDebugPolys(fd);
}

void RE_AddAreaLightToScene( const vec3_t center, const vec3_t right, const vec3_t up,
	float halfWidth, float halfHeight, float range, float r, float g, float b, int twoSided )
{
	if ( !tr.registered || !s_al.active || halfWidth <= 0.0f || halfHeight <= 0.0f )
		return;
	vec3_t axisRight, axisUp;
	VectorCopy(right, axisRight);
	VectorCopy(up, axisUp);
	if ( !R_AreaLightAxes(axisRight, axisUp) )
		return;
	vec3_t radiance;
	VectorSet(radiance, r, g, b);
	VectorScale(radiance, Q_max(r_ltcIntensityScale->value, 0.0f), radiance);
	range = Com_Clamp(AREALIGHT_MIN_RANGE, AREALIGHT_MAX_RANGE, range);
	R_AddAreaDlight(DLIGHT_RECT, center, axisRight, axisUp, halfWidth, halfHeight, -1.0f, range, radiance,
		AREALIGHT_DYNAMIC | (twoSided ? AREALIGHT_TWO_SIDED : 0), -1);
}

/*
A line emitter (saber blade). Returns qfalse when the renderer does not take
it (area lights inactive, r_ltcSaberAreaLights 0, no room): the caller then adds
its old point light, so the two never light the same frame twice.
*/
qboolean RE_AddLineLightToScene( const vec3_t start, const vec3_t end, float radius,
	float range, float r, float g, float b )
{
	if ( !tr.registered || !s_al.active || !r_ltcSaberAreaLights->integer )
		return qfalse;

	vec3_t center, axis, up;
	VectorAdd(start, end, center);
	VectorScale(center, 0.5f, center);
	VectorSubtract(end, start, axis);
	const float halfLength = 0.5f * VectorNormalize(axis);
	if ( halfLength < 0.25f )
		return qfalse;
	PerpendicularVector(up, axis);

	vec3_t radiance;
	VectorSet(radiance, r, g, b);
	VectorScale(radiance, SABER_AREA_RADIANCE * Q_max(r_ltcIntensityScale->value, 0.0f), radiance);
	range = Com_Clamp(AREALIGHT_MIN_RANGE, AREALIGHT_MAX_RANGE, range);
	return (qboolean)(R_AddAreaDlight(DLIGHT_LINE, center, axis, up, halfLength,
		Com_Clamp(0.25f, 16.0f, radius), -1.0f, range, radiance,
		AREALIGHT_DYNAMIC | AREALIGHT_TWO_SIDED, -1) != nullptr);
}

/*
============================================================

Debug: r_ltcDebug 6 outlines, 7 normals (polygons, seen through walls);
the other views are in lightall (LtcDebugColor)

============================================================
*/

qhandle_t RE_RegisterShaderFromImage( const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage );

static void R_DebugSegment( const refdef_t *fd, const vec3_t a, const vec3_t b, const byte *rgba, float width )
{
	vec3_t dir, toEye, side;
	VectorSubtract(b, a, dir);
	VectorSubtract(a, fd->vieworg, toEye);
	CrossProduct(dir, toEye, side);
	if ( VectorNormalize(side) < 1e-6f )
		return;
	const float w = width * (0.35f + 0.0015f * VectorLength(toEye));
	VectorScale(side, w, side);

	polyVert_t verts[4];
	VectorSubtract(a, side, verts[0].xyz);
	VectorAdd(a, side, verts[1].xyz);
	VectorAdd(b, side, verts[2].xyz);
	VectorSubtract(b, side, verts[3].xyz);
	for ( int i = 0; i < 4; i++ )
	{
		verts[i].st[0] = verts[i].st[1] = 0.5f;
		Com_Memcpy(verts[i].modulate, rgba, 4);
	}
	RE_AddPolyToScene(s_al.debugShader, 4, verts, 1);
}

static void R_AreaLightsDebugPolys( const refdef_t *fd )
{
	const int mode = r_ltcDebug->integer;
	if ( mode != 6 && mode != 7 )
		return;
	if ( !s_al.debugShader )
		s_al.debugShader = RE_RegisterShaderFromImage("*ltcDebugLines", lightmaps2d, stylesDefault, tr.whiteImage, qfalse);

	// every area light of the scene so far (map lights and dynamic ones)
	extern int r_numdlights;
	extern int r_firstSceneDlight;
	for ( int i = r_firstSceneDlight; i < r_numdlights; i++ )
	{
		const dlight_t *dl = &backEndData->dlights[i];
		if ( dl->areaType == DLIGHT_POINT )
			continue;
		const qboolean selected = (qboolean)((dl->areaFlags & AREALIGHT_SELECTED) != 0);
		byte rgba[4] = { 255, 220, 40, 220 };	// map lights yellow
		if ( dl->areaFlags & AREALIGHT_DYNAMIC )
		{
			rgba[1] = 120;	// dynamic orange
			rgba[2] = 20;
		}
		if ( selected )
			rgba[1] = rgba[2] = 255;
		const float width = selected ? 2.0f : 1.0f;

		vec3_t R, U, corners[4];
		VectorScale(dl->areaRight, dl->halfWidth, R);
		VectorScale(dl->areaUp, dl->halfHeight, U);
		if ( dl->areaType == DLIGHT_LINE )
			VectorClear(U);
		for ( int c = 0; c < 4; c++ )
		{
			VectorMA(dl->origin, (c == 0 || c == 1) ? -1.0f : 1.0f, R, corners[c]);
			VectorMA(corners[c], (c == 0 || c == 3) ? -1.0f : 1.0f, U, corners[c]);
		}

		if ( mode == 6 )
		{
			for ( int c = 0; c < 4; c++ )
				R_DebugSegment(fd, corners[c], corners[(c + 1) & 3], rgba, width);
			// influence bounds of the selected light: three great circles
			if ( selected )
			{
				const byte ring[4] = { 255, 255, 255, 90 };
				const int segments = 32;
				for ( int axis = 0; axis < 3; axis++ )
					for ( int s = 0; s < segments; s++ )
					{
						vec3_t p[2];
						for ( int e = 0; e < 2; e++ )
						{
							const float a = 2.0f * M_PI * (s + e) / segments;
							vec3_t o = { 0.0f, 0.0f, 0.0f };
							o[(axis + 1) % 3] = cosf(a) * dl->radius;
							o[(axis + 2) % 3] = sinf(a) * dl->radius;
							VectorAdd(dl->origin, o, p[e]);
						}
						R_DebugSegment(fd, p[0], p[1], ring, 1.0f);
					}
			}
		}
		else
		{
			// emitting normal (both ways when two sided), right (red), up (green)
			vec3_t normal, tip;
			CrossProduct(dl->areaRight, dl->areaUp, normal);
			const float len = Com_Clamp(8.0f, 64.0f, 0.25f * dl->radius);
			VectorMA(dl->origin, len, normal, tip);
			if ( dl->areaType != DLIGHT_LINE )
				R_DebugSegment(fd, dl->origin, tip, rgba, width);
			if ( (dl->areaFlags & AREALIGHT_TWO_SIDED) && dl->areaType != DLIGHT_LINE )
			{
				VectorMA(dl->origin, -len, normal, tip);
				R_DebugSegment(fd, dl->origin, tip, rgba, width * 0.5f);
			}
			const byte red[4] = { 255, 40, 40, 220 }, green[4] = { 40, 255, 40, 220 };
			VectorMA(dl->origin, dl->halfWidth, dl->areaRight, tip);
			R_DebugSegment(fd, dl->origin, tip, red, 0.75f);
			if ( dl->areaType != DLIGHT_LINE )
			{
				VectorMA(dl->origin, dl->halfHeight, dl->areaUp, tip);
				R_DebugSegment(fd, dl->origin, tip, green, 0.75f);
			}
		}
	}
}

/*
============================================================

Console

============================================================
*/

static void R_PrintMapLight( int i )
{
	const mapAreaLight_t *l = &s_al.lights[i];
	ri.Printf(PRINT_ALL,
		"%3d %-5s %-15s%s centre (%.1f %.1f %.1f) half %.1f x %.1f range %.0f color (%.2f %.2f %.2f) x %.2f%s %s\n",
		i, l->type == DLIGHT_LINE ? "line" : "rect", s_modeNames[l->mode], l->automatic ? " auto" : "",
		l->center[0], l->center[1], l->center[2], l->halfWidth, l->halfHeight, l->range,
		l->color[0], l->color[1], l->color[2], l->intensity, l->twoSided ? " two sided" : "", l->name);
}

void R_AreaLightsList_f( void )
{
	ri.Printf(PRINT_ALL, "maps/%s.arealights.json: %d lights, r_ltcAreaLights %s\n",
		s_al.mapName, (int)s_al.lights.size(), s_al.active ? "active" : "inactive");
	for ( int i = 0; i < (int)s_al.lights.size(); i++ )
		R_PrintMapLight(i);
}

void R_AreaLightsNearest_f( void )
{
	const int i = R_NearestMapLight(tr.refdef.vieworg);
	if ( i < 0 )
	{
		ri.Printf(PRINT_ALL, "no area lights on this map\n");
		return;
	}
	ri.Printf(PRINT_ALL, "nearest area light (%.0f units, r_ltcDebugLight %d shows it):\n",
		Distance(tr.refdef.vieworg, s_al.lights[i].center), i);
	R_PrintMapLight(i);
}

/*
============================================================

Candidates from the emissive surfaces of the loaded map: used by
r_ltcAutoAreaLights (in memory, at map load, when the map has no
.arealights.json) and by r_ltcExtractLights (written for review, never
loaded by itself; the map is never changed).

Stock JA lamps are a lightmapped surface plus an additive "glow" stage
whose mask texture is lit only where the lamp is (often a thin strip of a
larger texture); surfacelight is almost never set. So:
  1. emitting shaders: glow or emissive stage, or a surfacelight hint;
     sky and nodraw excluded
  2. coplanar, vertex connected triangles of one shader form a group
  3. the group is sampled in texture space: samples inside its triangles
     whose mask texel is lit become world points (through the triangle
     barycentrics), with their linear color
  4. the rectangle is fitted to the lit points (principal axis in the
     plane), emitting along the face normal
  5. radiance = lit power / rectangle area (sum of lit colors times the
     area per sample), so a strip of a big texture keeps its energy;
     confidence = lit area / rectangle area
Stages whose texture coordinates move (tcMod, tcGen) cannot be sampled:
the whole group is the rectangle with the texture average as radiance.

The mask is a small mip level read back once per image, at map load.

============================================================
*/

#define AUTO_LIT_LUMINANCE		0.1f	// linear: a mask texel above this emits
#define AUTO_MAX_SAMPLES		96		// per texture axis and group
#define AUTO_MIN_LIT_AREA		16.0f	// square units: skip indicators and buttons
#define AUTO_MIN_HALF_SIZE		1.0f
#define AUTO_CONFIDENCE			0.6f	// r_ltcAutoAreaLights 1
#define AUTO_CONFIDENCE_WIDE	0.35f	// r_ltcAutoAreaLights 2
#define AUTO_MIN_RADIANCE		0.02f

struct extractTri_t
{
	const shader_t *shader;
	vec3_t v[3];
	vec2_t st[3];
	vec3_t normal;
	float area;
};

struct litPoint_t
{
	vec3_t p;
};

struct areaCandidate_t
{
	mapAreaLight_t light;
	const shader_t *shader;
	float litArea;			// square units
	float confidence;
	qboolean hinted;		// surfacelight given
	qboolean animated;		// blinking / pulsing / animMap / deformed emitter
	qboolean sampled;		// fitted to the lit texels (else the whole surface)
};

struct imageMask_t
{
	int width, height;
	std::vector<float> rgb;	// linear
	vec3_t average;
};

static std::unordered_map<const image_t *, imageMask_t> s_imageMasks;

// the emissive surface candidates of the loaded map, scanned once for both the
// LTC auto lights and the volumetric reconstruction anchors
static std::vector<areaCandidate_t> s_candidates;
static int s_candidateTriangles;
static qboolean s_candidatesValid;

// images die with the renderer
static void R_ClearImageAverages( void )
{
	s_imageMasks.clear();
}

static int R_FindRoot( std::vector<int>& parent, int i )
{
	while ( parent[i] != i )
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

static qboolean R_ShaderEmits( const shader_t *sh, qboolean *hinted )
{
	*hinted = (qboolean)(sh->surfaceLight > 0.0f);
	if ( sh->isSky || (sh->surfaceFlags & (SURF_SKY | SURF_NODRAW)) )
		return qfalse;
	if ( *hinted )
		return qtrue;
	for ( int s = 0; s < MAX_SHADER_STAGES; s++ )
		if ( sh->stages[s] && sh->stages[s]->active && (sh->stages[s]->glow || sh->stages[s]->emissive) )
			return qtrue;
	return qfalse;
}

static float R_SrgbToLinear( float c )
{
	return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

// linear colors of a mip level of at most 128 x 128; NULL = no data
// (constant white for the white image)
static const imageMask_t *R_ImageMask( image_t *image )
{
	if ( !image || (image->flags & IMGFLAG_CUBEMAP) )
		return nullptr;
	auto it = s_imageMasks.find(image);
	if ( it != s_imageMasks.end() )
		return it->second.width > 0 ? &it->second : nullptr;

	imageMask_t& mask = s_imageMasks[image];
	mask.width = mask.height = 0;
	VectorSet(mask.average, 1.0f, 1.0f, 1.0f);

	GL_Bind(image);
	int level = -1, width = 0, height = 0;
	for ( int l = 0; l < 16; l++ )
	{
		GLint w = 0, h = 0;
		qglGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_WIDTH, &w);
		qglGetTexLevelParameteriv(GL_TEXTURE_2D, l, GL_TEXTURE_HEIGHT, &h);
		if ( w <= 0 || h <= 0 )
			break;
		level = l;
		width = w;
		height = h;
		if ( w <= 128 && h <= 128 )
			break;
	}
	if ( level < 0 )
		return nullptr;

	std::vector<float> texels((size_t)width * height * 4);
	qglGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, texels.data());
	// float images hold linear values, the rest is sRGB authored
	const qboolean linear = (qboolean)(image->internalFormat == GL_RGBA16F ||
		image->internalFormat == GL_RGB16F || image->internalFormat == GL_RGBA32F);
	mask.width = width;
	mask.height = height;
	mask.rgb.resize((size_t)width * height * 3);
	double sum[3] = { 0.0, 0.0, 0.0 };
	for ( int i = 0; i < width * height; i++ )
		for ( int c = 0; c < 3; c++ )
		{
			const float v = texels[i * 4 + c];
			const float lin = linear ? v : R_SrgbToLinear(Com_Clamp(0.0f, 1.0f, v));
			mask.rgb[i * 3 + c] = lin;
			sum[c] += lin;
		}
	for ( int c = 0; c < 3; c++ )
		mask.average[c] = (float)(sum[c] / (width * height));
	return &mask;
}

// the stage the lamp is drawn with, its image bundle and color multiplier
struct emitterStage_t
{
	const shaderStage_t *stage;
	int bundle;
	vec3_t scale;
	qboolean animated;
	qboolean staticCoords;	// plain texture coordinates: the mask can be sampled
};

static qboolean R_ShaderEmitterStage( const shader_t *sh, emitterStage_t *out )
{
	Com_Memset(out, 0, sizeof(*out));
	for ( int s = 0; s < MAX_SHADER_STAGES && !out->stage; s++ )
	{
		const shaderStage_t *st = sh->stages[s];
		if ( st && st->active && st->emissive && st->bundle[TB_EMISSIVEMAP].image[0] )
		{
			out->stage = st;
			out->bundle = TB_EMISSIVEMAP;
		}
	}
	for ( int s = 0; s < MAX_SHADER_STAGES && !out->stage; s++ )
	{
		const shaderStage_t *st = sh->stages[s];
		if ( st && st->active && st->glow )
			out->stage = st;
	}
	// surfacelight hint only: the base texture, as the lamp is drawn
	if ( !out->stage && sh->stages[0] && sh->stages[0]->active )
		out->stage = sh->stages[0];
	if ( !out->stage )
		return qfalse;

	const shaderStage_t *st = out->stage;
	const textureBundle_t *b = &st->bundle[out->bundle];
	VectorSet(out->scale, 1.0f, 1.0f, 1.0f);
	if ( out->bundle == TB_EMISSIVEMAP )
		VectorScale(st->emissiveColor, st->emissiveIntensity, out->scale);
	else if ( st->rgbGen == CGEN_CONST )
		VectorCopy(st->constantColor, out->scale);

	out->animated = (qboolean)(st->rgbGen == CGEN_WAVEFORM || b->numImageAnimations > 1 || sh->numDeforms > 0);
	out->staticCoords = (qboolean)(b->tcGen == TCGEN_TEXTURE && b->numTexMods == 0);
	return qtrue;
}

// rectangle in the plane (normal) around the points, along their principal axis
static void R_FitRect( const std::vector<const float *>& points, const vec3_t normal, float margin, mapAreaLight_t *l )
{
	vec3_t centroid = { 0.0f, 0.0f, 0.0f };
	for ( const float *p : points )
		VectorAdd(centroid, p, centroid);
	VectorScale(centroid, 1.0f / points.size(), centroid);

	vec3_t a1, a2;
	PerpendicularVector(a1, normal);
	CrossProduct(normal, a1, a2);
	float cxx = 0, cxy = 0, cyy = 0;
	for ( const float *p : points )
	{
		vec3_t d;
		VectorSubtract(p, centroid, d);
		const float x = DotProduct(d, a1), y = DotProduct(d, a2);
		cxx += x * x; cxy += x * y; cyy += y * y;
	}
	const float angle = 0.5f * atan2f(2.0f * cxy, cxx - cyy);
	VectorScale(a1, cosf(angle), l->right);
	VectorMA(l->right, sinf(angle), a2, l->right);
	CrossProduct(normal, l->right, l->up);	// cross(right, up) = normal

	float minR = 1e30f, maxR = -1e30f, minU = 1e30f, maxU = -1e30f;
	for ( const float *p : points )
	{
		vec3_t d;
		VectorSubtract(p, centroid, d);
		minR = Q_min(minR, DotProduct(d, l->right)); maxR = Q_max(maxR, DotProduct(d, l->right));
		minU = Q_min(minU, DotProduct(d, l->up)); maxU = Q_max(maxU, DotProduct(d, l->up));
	}
	l->halfWidth = 0.5f * (maxR - minR) + margin;
	l->halfHeight = 0.5f * (maxU - minU) + margin;
	VectorMA(centroid, 0.5f * (maxR + minR), l->right, l->center);
	VectorMA(l->center, 0.5f * (maxU + minU), l->up, l->center);
	VectorMA(l->center, 0.25f, normal, l->center);	// off the lamp surface itself
}

// one candidate from the points of a lit region (sampled: blob of texel
// samples of sampleArea each, else the surface corners with its area);
// peak = brightest texel of the region, the radiance never exceeds it
static void R_EmitCandidate( const areaCandidate_t& base, const std::vector<const float *>& points,
	const vec3_t power, const vec3_t peak, float litArea, qboolean sampled, const vec3_t normal,
	const vec3_t scale, std::vector<areaCandidate_t>& out )
{
	if ( points.size() < 3 )
		return;
	areaCandidate_t c = base;
	mapAreaLight_t *l = &c.light;
	c.sampled = sampled;
	// a lit sample stands for a small square: half its side of margin
	R_FitRect(points, normal, sampled ? 0.5f * sqrtf(litArea / points.size()) : 0.0f, l);
	if ( l->halfWidth < 0.5f || l->halfHeight < 0.5f )
		return;

	const float rectArea = 4.0f * l->halfWidth * l->halfHeight;
	if ( sampled )
		c.litArea = litArea;
	c.confidence = Com_Clamp(0.0f, 1.0f, c.litArea / rectArea);

	// radiance: lit power spread over the rectangle (a blob of a few samples
	// can get a rectangle smaller than its samples: capped at the peak)
	for ( int k = 0; k < 3; k++ )
		l->color[k] = Q_min(power[k] / rectArea, peak[k]) * scale[k];
	l->intensity = 1.0f;
	l->mode = AREAMODE_STATIC_SPECULAR;
	l->twoSided = qfalse;
	l->range = R_AreaLightDefaultRange(l);
	l->automatic = qtrue;
	Q_strncpyz(l->name, c.shader->name, sizeof(l->name));
	out.push_back(c);
}

static void R_BuildCandidate( const std::vector<extractTri_t>& tris, const std::vector<int>& members,
	std::vector<areaCandidate_t>& out )
{
	const shader_t *sh = tris[members[0]].shader;

	// plane: area weighted normal
	vec3_t normal = { 0, 0, 0 };
	float area = 0.0f;
	for ( int m : members )
	{
		VectorMA(normal, tris[m].area, tris[m].normal, normal);
		area += tris[m].area;
	}
	if ( area < 1.0f || VectorNormalize(normal) < 1e-4f )
		return;

	areaCandidate_t c;
	Com_Memset(&c, 0, sizeof(c));
	mapAreaLight_t *l = &c.light;
	l->type = DLIGHT_RECT;
	c.shader = sh;
	R_ShaderEmits(sh, &c.hinted);

	emitterStage_t emitter;
	if ( !R_ShaderEmitterStage(sh, &emitter) )
		return;
	c.animated = emitter.animated;
	image_t *image = emitter.stage->bundle[emitter.bundle].image[0];
	const imageMask_t *mask = image == tr.whiteImage ? nullptr : R_ImageMask(image);

	if ( mask && emitter.staticCoords )
	{
		// texture space bounds of the group
		float smin = 1e30f, smax = -1e30f, tmin = 1e30f, tmax = -1e30f;
		for ( int m : members )
			for ( int k = 0; k < 3; k++ )
			{
				smin = Q_min(smin, tris[m].st[k][0]); smax = Q_max(smax, tris[m].st[k][0]);
				tmin = Q_min(tmin, tris[m].st[k][1]); tmax = Q_max(tmax, tris[m].st[k][1]);
			}
		const int ns = Com_Clampi(4, AUTO_MAX_SAMPLES, (int)ceilf((smax - smin) * mask->width));
		const int nt = Com_Clampi(4, AUTO_MAX_SAMPLES, (int)ceilf((tmax - tmin) * mask->height));

		// sample grid: -1 outside the surface, 0 unlit, 1 lit
		std::vector<signed char> state((size_t)ns * nt, -1);
		std::vector<litPoint_t> points((size_t)ns * nt);
		std::vector<litPoint_t> colors((size_t)ns * nt);
		std::vector<float> weights((size_t)ns * nt, 0.0f);
		int inside = 0;
		float totalWeight = 0.0f;
		for ( int j = 0; j < nt; j++ )
			for ( int i = 0; i < ns; i++ )
			{
				const float s = smin + (smax - smin) * (i + 0.5f) / ns;
				const float t = tmin + (tmax - tmin) * (j + 0.5f) / nt;
				for ( int m : members )
				{
					// barycentrics in texture space
					const extractTri_t *tri = &tris[m];
					const float d = (tri->st[1][1] - tri->st[2][1]) * (tri->st[0][0] - tri->st[2][0]) +
						(tri->st[2][0] - tri->st[1][0]) * (tri->st[0][1] - tri->st[2][1]);
					if ( fabsf(d) < 1e-10f )
						continue;
					const float b0 = ((tri->st[1][1] - tri->st[2][1]) * (s - tri->st[2][0]) +
						(tri->st[2][0] - tri->st[1][0]) * (t - tri->st[2][1])) / d;
					const float b1 = ((tri->st[2][1] - tri->st[0][1]) * (s - tri->st[2][0]) +
						(tri->st[0][0] - tri->st[2][0]) * (t - tri->st[2][1])) / d;
					const float b2 = 1.0f - b0 - b1;
					if ( b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f )
						continue;

					inside++;
					const size_t cell = (size_t)j * ns + i;
					const float fs = (image->flags & IMGFLAG_CLAMPTOEDGE) ?
						Com_Clamp(0.0f, 1.0f, s) : s - floorf(s);
					const float ft = (image->flags & IMGFLAG_CLAMPTOEDGE) ?
						Com_Clamp(0.0f, 1.0f, t) : t - floorf(t);
					// One UV cell represents a different world area on each triangle.
					weights[cell] = 2.0f * tri->area *
						((smax - smin) / ns) * ((tmax - tmin) / nt) / fabsf(d);
					totalWeight += weights[cell];
					const int x = Com_Clampi(0, mask->width - 1, (int)(fs * mask->width));
					const int y = Com_Clampi(0, mask->height - 1, (int)(ft * mask->height));
					const float *rgb = &mask->rgb[((size_t)y * mask->width + x) * 3];
					const float lum = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
					state[cell] = lum > AUTO_LIT_LUMINANCE ? 1 : 0;
					VectorScale(tri->v[0], b0, points[cell].p);
					VectorMA(points[cell].p, b1, tri->v[1], points[cell].p);
					VectorMA(points[cell].p, b2, tri->v[2], points[cell].p);
					VectorCopy(rgb, colors[cell].p);
					break;
				}
			}
		if ( inside == 0 || totalWeight <= 0.0f )
			return;
		// Normalize the discrete coverage back to the known geometry area.
		const float weightScale = area / totalWeight;

		// one light per connected lit blob (8-neighbours): a texture with two
		// tubes or a row of bulbs gives one rectangle each, not one with gaps
		std::vector<int> stack;
		for ( size_t start = 0; start < state.size(); start++ )
		{
			if ( state[start] != 1 )
				continue;
			std::vector<const float *> blob;
			vec3_t power = { 0.0f, 0.0f, 0.0f }, peak = { 0.0f, 0.0f, 0.0f };
			float litArea = 0.0f;
			state[start] = 2;
			stack.push_back((int)start);
			while ( !stack.empty() )
			{
				const int cell = stack.back();
				stack.pop_back();
				blob.push_back(points[cell].p);
				const float sampleArea = weights[cell] * weightScale;
				litArea += sampleArea;
				VectorMA(power, sampleArea, colors[cell].p, power);
				for ( int k = 0; k < 3; k++ )
					peak[k] = Q_max(peak[k], colors[cell].p[k]);
				const int ci = cell % ns, cj = cell / ns;
				for ( int dj = -1; dj <= 1; dj++ )
					for ( int di = -1; di <= 1; di++ )
					{
						const int ni = ci + di, nj = cj + dj;
						if ( ni < 0 || nj < 0 || ni >= ns || nj >= nt )
							continue;
						const int next = nj * ns + ni;
						if ( state[next] == 1 )
						{
							state[next] = 2;
							stack.push_back(next);
						}
					}
			}
			R_EmitCandidate(c, blob, power, peak, litArea, qtrue, normal, emitter.scale, out);
		}
		return;
	}

	// moving texture coordinates, or no image data: the whole surface
	std::vector<const float *> corners;
	for ( int m : members )
		for ( int k = 0; k < 3; k++ )
			corners.push_back(tris[m].v[k]);
	vec3_t power = { 1.0f, 1.0f, 1.0f };
	if ( mask )
		VectorCopy(mask->average, power);
	const vec3_t peak = { 1e30f, 1e30f, 1e30f };
	VectorScale(power, area, power);
	c.litArea = area;
	R_EmitCandidate(c, corners, power, peak, 1.0f, qfalse, normal, emitter.scale, out);
}

static void R_FindAreaLightCandidates( const world_t *world, std::vector<areaCandidate_t>& out, int *numTriangles )
{
	out.clear();
	std::vector<extractTri_t> tris;
	for ( int s = 0; s < world->numsurfaces; s++ )
	{
		const msurface_t *surf = &world->surfaces[s];
		qboolean hinted;
		if ( !surf->shader || !surf->data || !R_ShaderEmits(surf->shader, &hinted) )
			continue;
		if ( *surf->data != SF_FACE && *surf->data != SF_TRIANGLES )
			continue;
		const srfBspSurface_t *bsp = (const srfBspSurface_t *)surf->data;
		if ( !bsp->verts || !bsp->indexes )
			continue;
		for ( int i = 0; i + 2 < bsp->numIndexes; i += 3 )
		{
			extractTri_t t;
			t.shader = surf->shader;
			for ( int k = 0; k < 3; k++ )
			{
				const srfVert_t *v = &bsp->verts[bsp->indexes[i + k]];
				VectorCopy(v->xyz, t.v[k]);
				t.st[k][0] = v->st[0];
				t.st[k][1] = v->st[1];
			}
			vec3_t e1, e2;
			VectorSubtract(t.v[1], t.v[0], e1);
			VectorSubtract(t.v[2], t.v[0], e2);
			CrossProduct(e1, e2, t.normal);
			t.area = 0.5f * VectorNormalize(t.normal);
			if ( t.area > 0.01f )
				tris.push_back(t);
		}
	}
	*numTriangles = (int)tris.size();

	// union triangles of one shader and plane sharing a vertex
	const int n = (int)tris.size();
	std::vector<int> parent(n);
	for ( int i = 0; i < n; i++ )
		parent[i] = i;
	std::unordered_map<std::string, int> firstAt;
	char key[256];
	for ( int i = 0; i < n; i++ )
	{
		const extractTri_t *t = &tris[i];
		const float dist = DotProduct(t->normal, t->v[0]);
		for ( int k = 0; k < 3; k++ )
		{
			Com_sprintf(key, sizeof(key), "%p %d %d %d %d %d %d %d", (const void *)t->shader,
				(int)floorf(t->normal[0] * 50.0f + 0.5f), (int)floorf(t->normal[1] * 50.0f + 0.5f),
				(int)floorf(t->normal[2] * 50.0f + 0.5f), (int)floorf(dist + 0.5f),
				(int)floorf(t->v[k][0] * 2.0f + 0.5f), (int)floorf(t->v[k][1] * 2.0f + 0.5f),
				(int)floorf(t->v[k][2] * 2.0f + 0.5f));
			auto it = firstAt.find(key);
			if ( it == firstAt.end() )
				firstAt[key] = i;
			else
				parent[R_FindRoot(parent, i)] = R_FindRoot(parent, it->second);
		}
	}

	std::map<int, std::vector<int>> groups;	// ordered: the same output every run
	for ( int i = 0; i < n; i++ )
		groups[R_FindRoot(parent, i)].push_back(i);
	for ( auto& group : groups )
		R_BuildCandidate(tris, group.second, out);
}

static void R_ClearAreaCandidates( void )
{
	s_candidates.clear();
	s_candidateTriangles = 0;
	s_candidatesValid = qfalse;
}

static const std::vector<areaCandidate_t>& R_AreaLightCandidates( const world_t *world )
{
	if ( !s_candidatesValid )
	{
		R_FindAreaLightCandidates(world, s_candidates, &s_candidateTriangles);
		s_candidatesValid = qtrue;
	}
	return s_candidates;
}

static qboolean R_AutoAccepts( const areaCandidate_t *c, int mode )
{
	const mapAreaLight_t *l = &c->light;
	if ( c->animated || c->litArea < AUTO_MIN_LIT_AREA * (mode >= 2 ? 0.5f : 1.0f) ||
		l->halfWidth < AUTO_MIN_HALF_SIZE || l->halfHeight < AUTO_MIN_HALF_SIZE )
	{
		return qfalse;
	}
	if ( MAX(l->color[0], MAX(l->color[1], l->color[2])) < AUTO_MIN_RADIANCE )
		return qfalse;
	return (qboolean)(c->confidence >= (mode >= 2 ? AUTO_CONFIDENCE_WIDE : AUTO_CONFIDENCE));
}

// one lamp split in fragments of the same size: keep the first
static qboolean R_AutoDuplicate( const mapAreaLight_t *l )
{
	for ( const mapAreaLight_t& o : s_al.lights )
	{
		const float d = DotProduct(o.right, l->right);
		if ( Distance(o.center, l->center) < 4.0f && d * d > 0.98f &&
			fabsf(o.halfWidth - l->halfWidth) < 2.0f && fabsf(o.halfHeight - l->halfHeight) < 2.0f )
		{
			return qtrue;
		}
	}
	return qfalse;
}

static void R_AutoAreaLights( void )
{
	// latched r_ltcAreaLights: turning it on reloads the map (vid_restart),
	// so maps loaded without it skip the scan and the texture readbacks
	const int mode = r_ltcAutoAreaLights->integer;
	if ( mode <= 0 || !r_ltcAreaLights->integer || !tr.world )
		return;

	const std::vector<areaCandidate_t>& candidates = R_AreaLightCandidates(tr.world);

	int skipped = 0;
	for ( const areaCandidate_t& c : candidates )
	{
		if ( !R_AutoAccepts(&c, mode) || R_AutoDuplicate(&c.light) )
		{
			skipped++;
			continue;
		}
		s_al.lights.push_back(c.light);
	}
	ri.Printf(PRINT_ALL, "r_ltcAutoAreaLights %d: %d area lights from %d emissive surfaces (%d skipped)\n",
		mode, (int)s_al.lights.size(), (int)candidates.size(), skipped);
}

void R_ExtractAreaLights_f( void )
{
	if ( !tr.world )
	{
		ri.Printf(PRINT_ALL, "r_ltcExtractLights: no map loaded\n");
		return;
	}

	std::vector<areaCandidate_t> candidates;
	int numTriangles = 0;
	R_FindAreaLightCandidates(tr.world, candidates, &numTriangles);

	const int autoMode = Q_max(1, r_ltcAutoAreaLights->integer);
	std::string out = "{\n\t\"generator\": \"r_ltcExtractLights\",\n\t\"lights\": [\n";
	int numOut = 0, numReview = 0;
	for ( const areaCandidate_t& c : candidates )
	{
		const mapAreaLight_t *l = &c.light;
		// what r_ltcAutoAreaLights would take needs no review
		const qboolean review = (qboolean)!R_AutoAccepts(&c, autoMode);
		char entry[1400];
		Com_sprintf(entry, sizeof(entry),
			"%s\t\t{\n"
			"\t\t\t\"type\": \"rect\",\n"
			"\t\t\t\"name\": \"%s\",\n"
			"\t\t\t\"center\": [%.2f, %.2f, %.2f],\n"
			"\t\t\t\"right\": [%.4f, %.4f, %.4f],\n"
			"\t\t\t\"up\": [%.4f, %.4f, %.4f],\n"
			"\t\t\t\"halfWidth\": %.2f,\n"
			"\t\t\t\"halfHeight\": %.2f,\n"
			"\t\t\t\"color\": [%.3f, %.3f, %.3f],\n"
			"\t\t\t\"intensity\": 1.0,\n"
			"\t\t\t\"range\": %.0f,\n"
			"\t\t\t\"mode\": \"static_specular\",\n"
			"\t\t\t\"twoSided\": false,\n"
			"\t\t\t\"surfacelight\": %.1f,\n"
			"\t\t\t\"litArea\": %.1f,\n"
			"\t\t\t\"fittedToTexels\": %s,\n"
			"\t\t\t\"animated\": %s,\n"
			"\t\t\t\"confidence\": %.2f,\n"
			"\t\t\t\"review\": %s\n"
			"\t\t}",
			numOut ? ",\n" : "", c.shader->name,
			l->center[0], l->center[1], l->center[2], l->right[0], l->right[1], l->right[2],
			l->up[0], l->up[1], l->up[2], l->halfWidth, l->halfHeight,
			l->color[0], l->color[1], l->color[2], l->range, c.shader->surfaceLight, c.litArea,
			c.sampled ? "true" : "false", c.animated ? "true" : "false", c.confidence,
			review ? "true" : "false");
		out += entry;
		numOut++;
		if ( review )
			numReview++;
	}
	out += "\n\t]\n}\n";

	char fileName[MAX_QPATH];
	Com_sprintf(fileName, sizeof(fileName), "maps/%s.arealights.generated.json", tr.world->baseName);
	ri.FS_WriteFile(fileName, out.c_str(), (int)out.size());
	ri.Printf(PRINT_ALL, "%s: %d candidates (%d marked for review) from %d emissive triangles\n",
		fileName, numOut, numReview, numTriangles);
}

/*
=================
R_CollectStaticAreaSources

The static emitters the directional baked light reconstruction of the froxel
fog (tr_volumetric_reconstruct.cpp) may anchor light grid energy to: the lamps
of maps/<map>.arealights.json that are not dynamic, and the emissive surface
candidates r_ltcAutoAreaLights 2 would take (not animated), whatever
r_ltcAreaLights is. They only attribute existing baked light, never add any.
=================
*/
void R_CollectStaticAreaSources( const world_t *world, std::vector<vrAreaSource>& out, int *numCandidates )
{
	out.clear();
	*numCandidates = 0;
	if ( !world )
		return;

	auto add = [&]( const mapAreaLight_t& l, float confidence ) {
		// a line / tube (right = axis, halfHeight = radius) is no rectangle: the
		// reconstruction's quadrature would treat it as a thin two-sided panel
		if ( l.type == DLIGHT_LINE )
			return;
		vrAreaSource a;
		Com_Memset(&a, 0, sizeof(a));
		VectorCopy(l.center, a.center);
		VectorCopy(l.right, a.right);
		VectorCopy(l.up, a.up);
		a.halfWidth = l.halfWidth;
		a.halfHeight = l.halfHeight;
		VectorScale(l.color, l.intensity, a.color);
		a.confidence = confidence;
		a.twoSided = l.twoSided;
		out.push_back(a);
	};

	// explicit lamps: high confidence anchors
	for ( const mapAreaLight_t& l : s_al.lights )
		if ( !l.automatic && l.mode != AREAMODE_DYNAMIC )
			add(l, 1.0f);
	const size_t numExplicit = out.size();

	const std::vector<areaCandidate_t>& candidates = R_AreaLightCandidates(world);
	*numCandidates = (int)candidates.size();
	for ( const areaCandidate_t& c : candidates )
	{
		if ( !R_AutoAccepts(&c, 2) )
			continue;
		const mapAreaLight_t *l = &c.light;
		// one lamp split in fragments of the same size, or an explicit lamp over the surface
		bool duplicate = false;
		for ( size_t k = 0; k < out.size() && !duplicate; k++ )
		{
			const vrAreaSource& o = out[k];
			const float d = DotProduct(o.right, l->right);
			if ( Distance(o.center, l->center) < 4.0f && d * d > 0.98f &&
				fabsf(o.halfWidth - l->halfWidth) < 2.0f && fabsf(o.halfHeight - l->halfHeight) < 2.0f )
				duplicate = true;
			else if ( k < numExplicit && Distance(o.center, l->center) < Q_max(o.halfWidth, o.halfHeight) )
				duplicate = true;
		}
		if ( duplicate )
			continue;
		// a surfacelight hint and a rectangle fitted to the lit texels are stronger evidence
		const float confidence = Com_Clamp(0.0f, 1.0f, c.confidence + (c.hinted ? 0.1f : 0.0f) + (c.sampled ? 0.05f : 0.0f));
		add(*l, confidence);
	}
}
