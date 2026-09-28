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
Spot light cookies / gobos (docs/rend2-spot-lights.md, "Cookies")

A cookie modulates the radiance a spot light sends in each direction. It is
applied where the cone is, before the BRDF and before the froxel scattering
injection, so walls and fog show the same pattern:

	light = color * pointAtten(radius) * coneAtten * cookie * shadow

Projection: analytic, from the spot's axis, cos outer and a roll (no
dependency on the shadow view, which unshadowed spots do not have). The basis
is the stable left / up of R_SpotShadowAxis rotated by the roll; the outer cone
maps to the unit disc inscribed in the texture, so the rim of the cookie is
where the cone factor reaches zero and the corners are never sampled:

	d  = unit direction light -> point, t = dot(d, axis)
	uv = 0.5 - 0.5 * (dot(d, left), dot(d, up)) / (t * tan outer)

(u grows to the right and v downwards seen from behind the lamp, the image is
not mirrored). t <= 0: factor 0 (behind the lamp; the cone is 0 there anyway).

Storage (GL 3.2: no bindless, no sampler arrays indexed per light): one
GL_TEXTURE_2D_ARRAY, LIGHT_COOKIE_SIZE^2 RGBA8 with a full mip chain, one
layer per registered cookie, clamp to edge, trilinear. Every light samples it
with textureLod(vec3(uv, layer), lod), a uniform sampler with a per light layer.
The lod is explicit (the Forward+ loop is non-uniform control flow, implicit
derivatives are undefined there) and the same formula on surfaces and in the
fog, from the world size of a pixel / froxel:

	lod = log2(LIGHT_COOKIE_SIZE * 0.5 * footprint / (t * tan outer))

Contents: an opaque image is an intensity (white = full light): a = its
luminance, rgb = its colour (r_spotLightCookies 2). An image with alpha is an
occluder mask, as it is on an alpha tested surface (grate, window frame): the
opaque texels block the light, transmission = 1 - alpha, grey. So the stock
alpha tested grates of the game work as gobos unchanged.
Cookies are spot light only: point lights have no projection (the efx parser
rejects a cookie outside a spot group).
*/

#include "tr_local.h"

#define LIGHT_COOKIE_SIZE		256		// must match lightall.glsl / volumetric_inject.glsl
#define MAX_LIGHT_COOKIES		16

static struct
{
	char names[MAX_LIGHT_COOKIES][MAX_QPATH];
	int numCookies;
	qboolean unitsOk;
	qboolean unitsChecked;
} s_cookie;

static qboolean R_LightCookieUnitsOk( void )
{
	if ( !s_cookie.unitsChecked )
	{
		GLint units = 0;
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
		s_cookie.unitsOk = (qboolean)(units > TB_LIGHTCOOKIES);
		s_cookie.unitsChecked = qtrue;
		if ( !s_cookie.unitsOk )
			ri.Printf(PRINT_WARNING, "r_spotLightCookies: needs more than %d texture units, cookies are ignored\n", TB_LIGHTCOOKIES);
	}
	return s_cookie.unitsOk;
}

qboolean R_LightCookiesActive( void )
{
	return (qboolean)(r_spotLightCookies->integer && tr.lightCookieArray && R_LightCookieUnitsOk());
}

// new renderer (vid_restart, map change with R_DeleteTextures): the array
// is gone, names registered before are reloaded on demand
void R_LightCookiesShutdown( void )
{
	tr.lightCookieArray = NULL;
	s_cookie.numCookies = 0;
	s_cookie.unitsChecked = qfalse;
}

static void R_LightCookieCreateArray( void )
{
	tr.lightCookieArray = R_Create2DImageArray("*lightCookies", NULL,
		LIGHT_COOKIE_SIZE, LIGHT_COOKIE_SIZE, MAX_LIGHT_COOKIES, IMGTYPE_COLORALPHA,
		IMGFLAG_CLAMPTOEDGE | IMGFLAG_MUTABLE | IMGFLAG_NO_COMPRESSION, GL_RGBA8);

	// mip chain storage (mutable texture, level 0 made by R_Create2DImageArray)
	GL_SelectTexture(0);
	qglBindTexture(GL_TEXTURE_2D_ARRAY, tr.lightCookieArray->texnum);
	int levels = 1;
	for ( int size = LIGHT_COOKIE_SIZE / 2; size >= 1; size /= 2, levels++ )
		qglTexImage3D(GL_TEXTURE_2D_ARRAY, levels, GL_RGBA8, size, size, MAX_LIGHT_COOKIES, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels - 1);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	qglTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	qglBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

// bilinear sample of an RGBA8 image, clamped
static void R_LightCookieSample( const byte *pic, int w, int h, float x, float y, float out[4] )
{
	x = Com_Clamp(0.0f, (float)(w - 1), x);
	y = Com_Clamp(0.0f, (float)(h - 1), y);
	const int x0 = (int)x, y0 = (int)y;
	const int x1 = Q_min(x0 + 1, w - 1), y1 = Q_min(y0 + 1, h - 1);
	const float fx = x - x0, fy = y - y0;
	for ( int c = 0; c < 4; c++ )
	{
		const float a = pic[(y0 * w + x0) * 4 + c] * (1.0f - fx) + pic[(y0 * w + x1) * 4 + c] * fx;
		const float b = pic[(y1 * w + x0) * 4 + c] * (1.0f - fx) + pic[(y1 * w + x1) * 4 + c] * fx;
		out[c] = a * (1.0f - fy) + b * fy;
	}
}

/*
=================
R_LightCookieConvert

Source image -> LIGHT_COOKIE_SIZE^2 RGBA8, box filtered (4x4 bilinear taps per
texel: no aliasing of fine slits when a big source is shrunk). a = intensity,
rgb = colour: luminance and colour of an opaque image, 1 - alpha (grey) of an
occluder mask. The shaders use a, or rgb with r_spotLightCookies 2.
=================
*/
static void R_LightCookieConvert( const byte *pic, int w, int h, byte *out )
{
	qboolean hasAlpha = qfalse;
	for ( int i = 0; i < w * h && !hasAlpha; i++ )
		hasAlpha = (qboolean)(pic[i * 4 + 3] < 250);

	const float sx = (float)w / LIGHT_COOKIE_SIZE, sy = (float)h / LIGHT_COOKIE_SIZE;
	for ( int y = 0; y < LIGHT_COOKIE_SIZE; y++ )
	{
		for ( int x = 0; x < LIGHT_COOKIE_SIZE; x++ )
		{
			float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			for ( int j = 0; j < 4; j++ )
			{
				for ( int i = 0; i < 4; i++ )
				{
					float s[4];
					R_LightCookieSample(pic, w, h,
						(x + (i + 0.5f) * 0.25f) * sx - 0.5f,
						(y + (j + 0.5f) * 0.25f) * sy - 0.5f, s);
					if ( hasAlpha )
					{
						const float t = 255.0f - s[3];	// occluder: opaque blocks
						sum[0] += t; sum[1] += t; sum[2] += t; sum[3] += t;
						continue;
					}
					for ( int c = 0; c < 3; c++ )
						sum[c] += s[c];
					sum[3] += 0.2126f * s[0] + 0.7152f * s[1] + 0.0722f * s[2];
				}
			}
			byte *o = out + (y * LIGHT_COOKIE_SIZE + x) * 4;
			for ( int c = 0; c < 4; c++ )
				o[c] = (byte)Com_Clampi(0, 255, (int)(sum[c] / 16.0f + 0.5f));
		}
	}
}

static qboolean R_LightCookieUpload( const char *name, int layer )
{
	byte *pic = NULL;
	int width = 0, height = 0;
	R_LoadImage(name, &pic, &width, &height);
	if ( !pic || width <= 0 || height <= 0 )
	{
		if ( pic )
			Z_Free(pic);
		ri.Printf(PRINT_WARNING, "light cookie: couldn't load \"%s\"\n", name);
		return qfalse;
	}
	byte *data = (byte *)Z_Malloc(LIGHT_COOKIE_SIZE * LIGHT_COOKIE_SIZE * 4, TAG_TEMP_WORKSPACE, qfalse);
	R_LightCookieConvert(pic, width, height, data);
	Z_Free(pic);

	GL_SelectTexture(0);
	qglBindTexture(GL_TEXTURE_2D_ARRAY, tr.lightCookieArray->texnum);
	qglTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, LIGHT_COOKIE_SIZE, LIGHT_COOKIE_SIZE, 1,
		GL_RGBA, GL_UNSIGNED_BYTE, data);
	qglGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	qglBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	// GL_Bind's cache of unit 0 no longer matches
	glState.currenttextures[0] = 0;
	Z_Free(data);
	ri.Printf(PRINT_DEVELOPER, "light cookie %d: %s (%dx%d)\n", layer, name, width, height);
	return qtrue;
}

/*
=================
RE_RegisterLightCookie

GetRefSpotLightAPI. Returns a handle (layer + 1) for refSpotLight_t::cookie,
0 when the cookie can't be used. Call at load time (effect registration): the
first registration of a name loads and filters the image.
=================
*/
int RE_RegisterLightCookie( const char *name )
{
	if ( !name || !name[0] || !R_LightCookieUnitsOk() )
		return 0;
	char path[MAX_QPATH];
	Q_strncpyz(path, name, sizeof(path));
	COM_StripExtension(path, path, sizeof(path));
	for ( int i = 0; i < s_cookie.numCookies; i++ )
	{
		if ( !Q_stricmp(s_cookie.names[i], path) )
			return i + 1;
	}
	if ( s_cookie.numCookies >= MAX_LIGHT_COOKIES )
	{
		ri.Printf(PRINT_WARNING, "light cookie: more than %d cookies, \"%s\" ignored\n", MAX_LIGHT_COOKIES, path);
		return 0;
	}
	if ( !tr.lightCookieArray )
		R_LightCookieCreateArray();
	const int layer = s_cookie.numCookies;
	if ( !R_LightCookieUpload(path, layer) )
		return 0;
	Q_strncpyz(s_cookie.names[layer], path, sizeof(s_cookie.names[layer]));
	s_cookie.numCookies++;
	return layer + 1;
}

const char *R_LightCookieName( int layer )
{
	return (layer >= 0 && layer < s_cookie.numCookies) ? s_cookie.names[layer] : "none";
}

/*
=================
R_SpotSetCookie

handle: RE_RegisterLightCookie result (0 none). up: optional orientation of the
cookie's top (zero: the stable basis). The roll is taken against the stable
basis of R_SpotShadowAxis, so an explicit up stays continuous where that basis
switches (axis near vertical).
=================
*/
void R_SpotSetCookie( dlight_t *dl, int handle, const vec3_t up )
{
	dl->cookieLayer = -1;
	dl->cookieRoll = 0.0f;
	if ( !dl->spot || handle <= 0 || handle > s_cookie.numCookies )
		return;
	dl->cookieLayer = handle - 1;
	if ( up && DotProduct(up, up) > 1e-8f )
	{
		vec3_t axis[3];
		R_SpotShadowAxis(dl, axis);
		const float u = DotProduct(up, axis[2]);
		const float l = DotProduct(up, axis[1]);
		if ( u * u + l * l > 1e-8f )
			dl->cookieRoll = atan2f(-l, u);
	}
}

/*
=================
R_SpotCookieUV

CPU mirror of SpotCookieUV (lightall.glsl, volumetric_inject.glsl): the cookie
coordinate of point, qfalse behind the lamp.
=================
*/
qboolean R_SpotCookieUV( const dlight_t *dl, const vec3_t point, vec2_t uv )
{
	vec3_t d;
	VectorSubtract(point, dl->origin, d);
	if ( VectorNormalize(d) < 1e-6f )
		return qfalse;
	vec3_t axis[3];
	R_SpotShadowAxis(dl, axis);
	const float t = DotProduct(d, axis[0]);
	if ( t <= 1e-4f )
		return qfalse;
	const float c = cosf(dl->cookieRoll), s = sinf(dl->cookieRoll);
	const float x = DotProduct(d, axis[1]), y = DotProduct(d, axis[2]);
	const float left = c * x + s * y;
	const float up = -s * x + c * y;
	const float cosO = dl->spotCosOuter;
	const float tanO = sqrtf(Q_max(1.0f - cosO * cosO, 1e-6f)) / cosO;
	uv[0] = 0.5f - 0.5f * left / (t * tanO);
	uv[1] = 0.5f - 0.5f * up / (t * tanO);
	return qtrue;
}

// u_LightCookieParams: enabled, rgb, footprint (world size of a pixel / froxel
// at distance 1), debug mode
void R_LightCookieParams( float footprintPerDistance, vec4_t out )
{
	VectorSet4(out,
		R_LightCookiesActive() ? 1.0f : 0.0f,
		r_spotLightCookies->integer >= 2 ? 1.0f : 0.0f,
		footprintPerDistance,
		(float)Com_Clampi(0, 3, r_spotLightCookieDebug->integer));
}

// NULL until a cookie is registered (nothing samples the unit then)
image_t *R_LightCookieImage( void )
{
	return tr.lightCookieArray;
}
