/*
===========================================================================
Copyright (C) 2026 OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

/*
Spot lights (docs/rend2-spot-lights.md)

A spot light is an ordinary dlight_t (areaType DLIGHT_POINT: same list, radial
falloff, surface culling and shadow slot budget as a point light) with a cone:

	light = color * pointAtten(radius) * coneAtten * shadow

	coneAtten = smoothstep(cosOuter, cosInner, dot(-L, dir))

The same cone function is used by lightall.glsl (surfaces, legacy loop and
Forward+) and volumetric_inject.glsl (froxel fog); R_SpotConeAttenuation is its
CPU mirror. Point lights carry cosOuter -2 / cosInner -1, for which the factor
is exactly 1, so the shaders need no light type branch for it.

Shadows (r_dlightMode 2): a spot with an outer angle up to
SPOT_PROJECTED_MAX_ANGLE renders one perspective view into layer 6 * slot of
pointShadowArrayImage (the other 5 layers of its slot are unused), sampled
through LightsBlock::spotShadowVP[slot]. Wider cones keep the 6 cube faces.
Same near plane (1), far plane (radius), polygon offset and r_dlightShadowBias
conventions as the cube faces.

Submission: the optional renderer extension GetRefSpotLightAPI
(refSpotLightExport_t). The existing AddLightToScene calls are unchanged.
*/

#include "tr_local.h"

// wider cones keep the cube faces: a single perspective view past ~120 degrees
// wastes most of its texels on the edges
#define SPOT_PROJECTED_MAX_ANGLE	60.0f
#define SPOT_MAX_ANGLE				89.0f

/*
============================================================

Cone

============================================================
*/

static float R_SpotSmoothstep( float e0, float e1, float x )
{
	const float t = Com_Clamp(0.0f, 1.0f, (x - e0) / Q_max(e1 - e0, 1e-6f));
	return t * t * (3.0f - 2.0f * t);
}

// the cone factor of the shaders (SpotConeAttenuation, lightall.glsl and
// volumetric_inject.glsl) at point
float R_SpotConeAttenuation( const dlight_t *dl, const vec3_t point )
{
	vec3_t L;
	VectorSubtract(point, dl->origin, L);	// light to point: -L of the shaders
	const float len = VectorLength(L);
	if ( len < 1e-6f )
		return 1.0f;
	const float cosAngle = DotProduct(L, dl->spotDir) / len;
	return R_SpotSmoothstep(dl->spotCosOuter, dl->spotCosInner, cosAngle);
}

/*
=================
R_SpotBoundingSphere

A sphere around everything the light can reach. Spot cones up to 60 degrees:
the smallest sphere through the apex holding the cone of slant length radius,
center origin + dir * t, radius t = radius / (2 cos outer) (every cap point at
angle <= outer is inside, the apex on the surface). Otherwise the point light
sphere.
=================
*/
void R_SpotBoundingSphere( const dlight_t *dl, vec3_t center, float *radius )
{
	if ( dl->spot && dl->spotCosOuter > 0.5f )
	{
		const float t = dl->radius / (2.0f * dl->spotCosOuter);
		VectorMA(dl->origin, t, dl->spotDir, center);
		*radius = t;
		return;
	}
	VectorCopy(dl->origin, center);
	*radius = dl->radius;
}

// false: outside the cone's reach (a sphere at the far side, a surface behind
// the light). Conservative: sphere against the cone widened by its radius
qboolean R_SpotSphereInCone( const dlight_t *dl, const vec3_t center, float radius )
{
	if ( !dl->spot )
		return qtrue;
	vec3_t v;
	VectorSubtract(center, dl->origin, v);
	const float distSq = DotProduct(v, v);
	if ( distSq <= radius * radius )
		return qtrue;	// the light inside the sphere
	const float along = DotProduct(v, dl->spotDir);
	const float cosO = dl->spotCosOuter;
	const float sinO = sqrtf(Q_max(0.0f, 1.0f - cosO * cosO));
	// distance of the center from the cone surface (negative inside),
	// Eberly's sphere / cone test
	const float perp = sqrtf(Q_max(0.0f, distSq - along * along));
	const float signedDist = perp * cosO - along * sinO;
	if ( signedDist > radius )
		return qfalse;
	return (qboolean)(along > -radius);
}

/*
============================================================

Shadow view

============================================================
*/

// the cone gets one perspective shadow view (else the cube faces, or none)
qboolean R_SpotProjectedShadow( const dlight_t *dl )
{
	if ( !dl->spot || !r_spotShadows->integer )
		return qfalse;
	return (qboolean)(dl->spotCosOuter >= cosf(DEG2RAD(SPOT_PROJECTED_MAX_ANGLE)) - 1e-4f);
}

// false: the light casts no shadow at all (SPOTLIGHT_NOSHADOW, r_spotShadows 0)
qboolean R_DlightCastsShadow( const dlight_t *dl )
{
	if ( !dl->spot )
		return qtrue;
	return (qboolean)(!dl->spotNoShadow && r_spotShadows->integer);
}

// field of view of the shadow view: the outer cone plus two texels, so the
// PCF footprint at the rim stays inside the map
float R_SpotShadowFov( const dlight_t *dl )
{
	const float outer = RAD2DEG(acosf(Com_Clamp(-1.0f, 1.0f, dl->spotCosOuter)));
	const float texel = 2.0f * 2.0f * outer / (float)DSHADOW_MAP_SIZE;
	return Q_min(2.0f * outer + texel, 2.0f * SPOT_PROJECTED_MAX_ANGLE + 4.0f);
}

// view axis of the shadow view: forward = dir, a stable left / up (no roll
// flips from frame to frame while the light turns)
void R_SpotShadowAxis( const dlight_t *dl, vec3_t axis[3] )
{
	VectorCopy(dl->spotDir, axis[0]);
	vec3_t up = { 0.0f, 0.0f, 1.0f };
	if ( fabsf(axis[0][2]) > 0.99f )
		VectorSet(up, 1.0f, 0.0f, 0.0f);
	CrossProduct(up, axis[0], axis[1]);
	VectorNormalize(axis[1]);
	CrossProduct(axis[0], axis[1], axis[2]);
}

/*
============================================================

Submission

============================================================
*/

static void R_SetSpotCone( dlight_t *dl, const vec3_t dir, float innerAngle, float outerAngle )
{
	VectorCopy(dir, dl->spotDir);
	if ( VectorNormalize(dl->spotDir) < 1e-6f )
		VectorSet(dl->spotDir, 0.0f, 0.0f, -1.0f);
	const float outer = Com_Clamp(0.5f, SPOT_MAX_ANGLE, outerAngle);
	const float inner = Com_Clamp(0.0f, outer, innerAngle);
	dl->spotCosOuter = cosf(DEG2RAD(outer));
	// a small smooth band even for inner == outer: no hard edged cones
	dl->spotCosInner = Q_max(cosf(DEG2RAD(inner)), dl->spotCosOuter + 0.002f);
	dl->spot = qtrue;
}

/*
=====================
RE_AddSpotLightToScene

GetRefSpotLightAPI. r_spotLights 0: the light is added as the point light it
would have been without the cone.
=====================
*/
void RE_AddSpotLightToScene( const refSpotLight_t *light )
{
	if ( !light || Q_isnan(light->origin[0]) || Q_isnan(light->origin[1]) || Q_isnan(light->origin[2]) ||
		Q_isnan(light->dir[0]) || Q_isnan(light->dir[1]) || Q_isnan(light->dir[2]) )
	{
		return;
	}
	dlight_t *dl = R_AddSceneDynamicLight(light->origin, light->radius,
		light->color[0], light->color[1], light->color[2], (light->flags & SPOTLIGHT_ADDITIVE) ? qtrue : qfalse);
	if ( !dl || !r_spotLights->integer )
		return;
	R_SetSpotCone(dl, light->dir, light->innerAngle, light->outerAngle);
	dl->spotNoShadow = (light->flags & SPOTLIGHT_NOSHADOW) ? qtrue : qfalse;
}

/*
============================================================

r_spot: renderer side test spot lights (no asset needed)

============================================================
*/

#define MAX_DEBUG_SPOTS	8

typedef struct
{
	refSpotLight_t light;
	qboolean attached;		// follows the camera (flashlight)
	float spin;				// degrees per second around the world up axis, 0 = still
} debugSpot_t;

static struct
{
	debugSpot_t spots[MAX_DEBUG_SPOTS];
	int numSpots;
	qboolean hasCamera;
	vec3_t cameraOrigin;
	vec3_t cameraAxis[3];
	int lastPrintTime;
} s_spot;

static void R_SpotUsage( void )
{
	ri.Printf(PRINT_ALL,
		"r_spot add [radius] [outer] [inner] [r g b] - a spot at the camera, pointing where it looks\n"
		"r_spot attach [radius] [outer] [inner]      - a flashlight moving with the camera\n"
		"r_spot spin <index> <degrees per second>    - turn a spot around the world up axis\n"
		"r_spot noshadow <index> <0|1>\n"
		"r_spot list | clear\n"
		"defaults: radius 600, outer 30, inner 20, color 1 1 1\n");
}

static float R_SpotArg( int arg, float def )
{
	return ri.Cmd_Argc() > arg ? (float)atof(ri.Cmd_Argv(arg)) : def;
}

static void R_SpotAdd( qboolean attached )
{
	if ( !s_spot.hasCamera )
	{
		ri.Printf(PRINT_ALL, "r_spot: no world scene rendered yet\n");
		return;
	}
	if ( s_spot.numSpots >= MAX_DEBUG_SPOTS )
	{
		ri.Printf(PRINT_ALL, "r_spot: %d spots at most (r_spot clear)\n", MAX_DEBUG_SPOTS);
		return;
	}
	debugSpot_t *d = &s_spot.spots[s_spot.numSpots];
	Com_Memset(d, 0, sizeof(*d));
	d->attached = attached;
	d->light.radius = R_SpotArg(2, 600.0f);
	d->light.outerAngle = R_SpotArg(3, 30.0f);
	d->light.innerAngle = R_SpotArg(4, Q_min(20.0f, d->light.outerAngle));
	d->light.color[0] = R_SpotArg(5, 1.0f);
	d->light.color[1] = R_SpotArg(6, d->light.color[0]);
	d->light.color[2] = R_SpotArg(7, d->light.color[0]);
	VectorCopy(s_spot.cameraOrigin, d->light.origin);
	VectorCopy(s_spot.cameraAxis[0], d->light.dir);
	ri.Printf(PRINT_ALL, "r_spot %d: %s radius %g cone %g / %g\n", s_spot.numSpots,
		attached ? "attached" : "placed", d->light.radius, d->light.innerAngle, d->light.outerAngle);
	s_spot.numSpots++;
}

static void R_SpotList( void )
{
	ri.Printf(PRINT_ALL, "%d r_spot light(s), r_spotLights %d, r_spotShadows %d\n",
		s_spot.numSpots, r_spotLights->integer, r_spotShadows->integer);
	for ( int i = 0; i < s_spot.numSpots; i++ )
	{
		const debugSpot_t *d = &s_spot.spots[i];
		ri.Printf(PRINT_ALL, "  %d: %s origin (%.0f %.0f %.0f) dir (%.2f %.2f %.2f) radius %.0f cone %.1f / %.1f%s spin %.0f\n",
			i, d->attached ? "attached" : "placed",
			d->light.origin[0], d->light.origin[1], d->light.origin[2],
			d->light.dir[0], d->light.dir[1], d->light.dir[2], d->light.radius,
			d->light.innerAngle, d->light.outerAngle,
			(d->light.flags & SPOTLIGHT_NOSHADOW) ? " noshadow" : "", d->spin);
	}
}

void R_Spot_f( void )
{
	if ( ri.Cmd_Argc() < 2 )
	{
		R_SpotList();
		ri.Printf(PRINT_ALL, "(r_spot help for the commands)\n");
		return;
	}
	const char *cmd = ri.Cmd_Argv(1);
	if ( !Q_stricmp(cmd, "add") )
		R_SpotAdd(qfalse);
	else if ( !Q_stricmp(cmd, "attach") )
		R_SpotAdd(qtrue);
	else if ( !Q_stricmp(cmd, "list") )
		R_SpotList();
	else if ( !Q_stricmp(cmd, "clear") )
	{
		s_spot.numSpots = 0;
		ri.Printf(PRINT_ALL, "r_spot lights cleared\n");
	}
	else if ( !Q_stricmp(cmd, "spin") || !Q_stricmp(cmd, "noshadow") )
	{
		const int index = ri.Cmd_Argc() > 2 ? atoi(ri.Cmd_Argv(2)) : -1;
		if ( index < 0 || index >= s_spot.numSpots || ri.Cmd_Argc() < 4 )
		{
			R_SpotUsage();
			return;
		}
		if ( !Q_stricmp(cmd, "spin") )
			s_spot.spots[index].spin = (float)atof(ri.Cmd_Argv(3));
		else if ( atoi(ri.Cmd_Argv(3)) )
			s_spot.spots[index].light.flags |= SPOTLIGHT_NOSHADOW;
		else
			s_spot.spots[index].light.flags &= ~SPOTLIGHT_NOSHADOW;
	}
	else
		R_SpotUsage();
}

/*
============================================================

Scene hook and debug

============================================================
*/

qhandle_t RE_RegisterShaderFromImage(const char *name, const int *lightmapIndexes, const byte *styles, image_t *image, qboolean mipRawImage);

// a camera facing line quad (as the r_foliageInteractionDebug capsules)
static void R_SpotDebugSegment( qhandle_t shader, const refdef_t *fd, const vec3_t a, const vec3_t b, const byte *rgba )
{
	vec3_t dir, toEye, side;
	VectorSubtract(b, a, dir);
	VectorSubtract(a, fd->vieworg, toEye);
	CrossProduct(dir, toEye, side);
	if ( VectorNormalize(side) < 1e-6f )
		return;
	VectorScale(side, 0.35f + 0.0015f * VectorLength(toEye), side);

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
	RE_AddPolyToScene(shader, 4, verts, 1);
}

// r_spotLightDebug 1: cone (yellow outer, orange inner rim, axis); 2: also the
// shadow view frustum (cyan) of lights that get one
static void R_SpotDebugDraw( const refdef_t *fd, const dlight_t *dl )
{
	const qhandle_t shader = RE_RegisterShaderFromImage("*spotLightDebug", lightmaps2d,
		stylesDefault, tr.whiteImage, qfalse);
	static const byte outerColor[4] = { 255, 230, 40, 255 };
	static const byte innerColor[4] = { 255, 140, 20, 255 };
	static const byte frustumColor[4] = { 40, 220, 255, 255 };
	vec3_t axis[3];
	R_SpotShadowAxis(dl, axis);

	vec3_t tip;
	VectorMA(dl->origin, dl->radius, dl->spotDir, tip);
	R_SpotDebugSegment(shader, fd, dl->origin, tip, outerColor);

	const float cosines[2] = { dl->spotCosOuter, dl->spotCosInner };
	const int segments = 24;
	for ( int c = 0; c < 2; c++ )
	{
		const float cosA = cosines[c];
		const float sinA = sqrtf(Q_max(0.0f, 1.0f - cosA * cosA));
		vec3_t prev;
		for ( int s = 0; s <= segments; s++ )
		{
			const float a = (float)s / segments * 2.0f * (float)M_PI;
			vec3_t p;
			VectorMA(dl->origin, dl->radius * cosA, axis[0], p);
			VectorMA(p, dl->radius * sinA * cosf(a), axis[1], p);
			VectorMA(p, dl->radius * sinA * sinf(a), axis[2], p);
			if ( s > 0 )
				R_SpotDebugSegment(shader, fd, prev, p, c ? innerColor : outerColor);
			if ( c == 0 && (s % 6) == 0 && s < segments )
				R_SpotDebugSegment(shader, fd, dl->origin, p, outerColor);
			VectorCopy(p, prev);
		}
	}

	if ( r_spotLightDebug->integer >= 2 && R_SpotProjectedShadow(dl) && R_DlightCastsShadow(dl) )
	{
		// the far plane square of the shadow view (fov square)
		const float h = tanf(DEG2RAD(R_SpotShadowFov(dl) * 0.5f)) * dl->radius;
		vec3_t corners[4];
		for ( int k = 0; k < 4; k++ )
		{
			VectorMA(dl->origin, dl->radius, axis[0], corners[k]);
			VectorMA(corners[k], (k == 0 || k == 3) ? h : -h, axis[1], corners[k]);
			VectorMA(corners[k], (k < 2) ? h : -h, axis[2], corners[k]);
		}
		for ( int k = 0; k < 4; k++ )
		{
			R_SpotDebugSegment(shader, fd, corners[k], corners[(k + 1) & 3], frustumColor);
			R_SpotDebugSegment(shader, fd, dl->origin, corners[k], frustumColor);
		}
	}
}

/*
=================
R_SpotLightsBeginScene

RE_RenderScene, before the scene takes its lights: the r_spot lights of a
world scene, then the r_spotLightDebug cones of every spot in it.
=================
*/
void R_SpotLightsBeginScene( const refdef_t *fd, int firstSceneDlight )
{
	if ( !tr.world || (fd->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) )
		return;

	s_spot.hasCamera = qtrue;
	VectorCopy(fd->vieworg, s_spot.cameraOrigin);
	for ( int i = 0; i < 3; i++ )
		VectorCopy(fd->viewaxis[i], s_spot.cameraAxis[i]);

	const float seconds = fd->time * 0.001f;
	for ( int i = 0; i < s_spot.numSpots; i++ )
	{
		refSpotLight_t light = s_spot.spots[i].light;
		if ( s_spot.spots[i].attached )
		{
			// a flashlight: slightly right of and below the eye
			VectorMA(fd->vieworg, -8.0f, fd->viewaxis[1], light.origin);
			VectorMA(light.origin, -6.0f, fd->viewaxis[2], light.origin);
			VectorCopy(fd->viewaxis[0], light.dir);
		}
		else if ( s_spot.spots[i].spin != 0.0f )
		{
			const float a = DEG2RAD(s_spot.spots[i].spin * seconds);
			const float c = cosf(a), s = sinf(a);
			const vec3_t d = { light.dir[0], light.dir[1], light.dir[2] };
			light.dir[0] = d[0] * c - d[1] * s;
			light.dir[1] = d[0] * s + d[1] * c;
		}
		RE_AddSpotLightToScene(&light);
	}

	if ( !r_spotLightDebug->integer )
		return;

	extern int r_numdlights;
	int spots = 0;
	for ( int i = firstSceneDlight; i < r_numdlights; i++ )
	{
		const dlight_t *dl = &backEndData->dlights[i];
		if ( !dl->spot )
			continue;
		spots++;
		if ( r_spotLightDebug->integer <= 2 )
			R_SpotDebugDraw(fd, dl);
	}

	const int now = ri.Milliseconds();
	if ( r_spotLightDebug->integer == 1 && now - s_spot.lastPrintTime >= 1000 )
	{
		s_spot.lastPrintTime = now;
		for ( int i = firstSceneDlight; i < r_numdlights; i++ )
		{
			const dlight_t *dl = &backEndData->dlights[i];
			ri.Printf(PRINT_ALL, "dlight %d: %s radius %.0f", i - firstSceneDlight,
				dl->spot ? "spot" : (dl->areaType == DLIGHT_POINT ? "point" : "area"), dl->radius);
			if ( dl->spot )
			{
				ri.Printf(PRINT_ALL, " dir (%.2f %.2f %.2f) cone %.1f / %.1f shadow %s", dl->spotDir[0],
					dl->spotDir[1], dl->spotDir[2], RAD2DEG(acosf(dl->spotCosInner)),
					RAD2DEG(acosf(dl->spotCosOuter)),
					!R_DlightCastsShadow(dl) ? "off" : R_SpotProjectedShadow(dl) ? "projected" : "cube");
			}
			ri.Printf(PRINT_ALL, "\n");
		}
		ri.Printf(PRINT_ALL, "%d spot light(s) this scene\n", spots);
	}
}
