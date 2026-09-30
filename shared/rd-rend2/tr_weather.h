/*
===========================================================================
Copyright (C) 2016, OpenJK contributors

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
#pragma once

#include "qcommon/qcommon.h"
#include "tr_local.h"

#define MAX_WINDOBJECTS 10
#define MAX_WEATHER_ZONES 100

enum weatherType_t
{
	WEATHER_RAIN,
	WEATHER_SNOW,
	WEATHER_SPACEDUST,
	WEATHER_SAND,
	WEATHER_FOG,

	NUM_WEATHER_TYPES
};

const int maxWeatherTypeParticles[NUM_WEATHER_TYPES] = {
	30000,
	10000,
	5000,
	1000,
	1000
};

struct weatherObject_t
{
	VBO_t *lastVBO;
	VBO_t *vbo;
	unsigned vboLastUpdateFrame;
	vertexAttribute_t attribsTemplate[3];	// position, velocity[, impact] (tr_weather.cpp rain*Vertex_t)
	int numAttribs;			// 3 in the impact layout
	int maxParticles;		// per chunk, buffer capacity
	bool splashCapable;		// buffers sized for the impact layout (the rain slot)
	bool impactLayout;		// r_rainSplashes records with impact state
	float maxHorizontalVelocity[2];
	float minDownwardVelocity;
	float maxVerticalVelocity;
	bool velocityBoundsReliable;

	bool active;

	float	gravity;
	float	fadeDistance;
	float	velocityOrientationScale;
	int		particleCount;
	image_t *drawImage;
	vec4_t  color;
	vec2_t	size;
};

struct windObject_t
{
	vec3_t currentVelocity;
	vec3_t targetVelocity;
	vec3_t maxVelocity;
	vec3_t minVelocity;
	float chanceOfDeadTime;
	vec2_t deadTimeMinMax;
	int targetVelocityTimeRemaining;
};

struct weatherBrushes_t
{
	uint8_t	numPlanes;
	vec4_t	planes[64];

};

enum weatherBrushType_t
{
	WEATHER_BRUSHES_NONE,
	WEATHER_BRUSHES_OUTSIDE,
	WEATHER_BRUSHES_INSIDE,

	NUM_WEATHER_BRUSH_TYPES
};

// rain command of the weather, for the lens water profile (tr_rainlens.cpp)
enum rainWeather_t
{
	RAIN_WEATHER_NONE,
	RAIN_WEATHER_LIGHT,
	RAIN_WEATHER_NORMAL,
	RAIN_WEATHER_HEAVY,
	RAIN_WEATHER_ACID
};

struct weatherSystem_t
{
	weatherObject_t weatherSlots[NUM_WEATHER_TYPES];
	VBO_t *debugBoundsVBO;
	windObject_t windSlots[MAX_WINDOBJECTS];
	weatherBrushes_t weatherBrushes[MAX_WEATHER_ZONES * 2];
	weatherBrushType_t weatherBrushType = WEATHER_BRUSHES_NONE;

	int activeWeatherTypes = 0;
	int activeWindObjects = 0;
	int numWeatherBrushes = 0;
	bool frozen;
	bool shaking;
	float pain = 0.0f;
	int rainSubtype = RAIN_WEATHER_NONE;	// rainWeather_t

	srfWeather_t weatherSurface;

	vec3_t		constWindDirection;
	vec3_t		windDirection;
	float		windSpeed;

	float		weatherMVP[16];

	// r_weatherWetness: weatherMVP / weatherDepthImage are valid for this map,
	// world units covered by the depth range and by one depth map texel
	bool		depthMapValid = false;
	float		depthRangeWorld = 1.0f;
	float		texelSizeWorld = 1.0f;

	// r_rainSplashes: tr.weatherSurfaceImage (no weather brushes) holds this
	// map; world XY offset of each VBO chunk slot at the last simulation, the
	// offset before it and when it changed. Impacts are world space, so a
	// slot's live splashes can still be in its previous zone after a remap.
	bool		surfaceMapValid = false;
	bool		splashSlotsValid = false;
	float		splashSlotZone[9][2];
	float		splashSlotPrevZone[9][2];
	float		splashSlotRemapTime[9];

	// r_rainSplashDebug: GL_PRIMITIVES_GENERATED of the first splash batch
	// of a frame, read back frames later only once available (no stall)
	GLuint		splashQueries[4];
	unsigned	splashQueryFrame[4];
	int			splashQueryOpen;		// query index + 1 while a query runs
	float		splashExpected;			// live splashes if every column were exposed
	int			splashLastPrint;
};
struct srfWeather_t;

void R_InitWeatherSystem();
void R_InitWeatherForMap();
void R_AddWeatherSurfaces();
void R_AddWeatherBrush(uint8_t numPlanes, vec4_t *planes);
void R_LoadWeatherImages();
void R_ShutdownWeatherSystem();
void RB_SurfaceWeather( srfWeather_t *surfaceType );
bool R_IsOutside(vec3_t pos);
bool R_IsShaking(vec3_t pos);
float R_IsOutsideCausingPain(vec3_t pos);
float R_GetChanceOfSaberFizz();
bool R_GetWindVector(vec3_t windVector, vec3_t atPoint); // doesn't work?
bool R_GetWindGusting(vec3_t atPoint); // doesn't work

void RE_WorldEffectCommand(const char *cmd);
void R_WorldEffect_f(void);
