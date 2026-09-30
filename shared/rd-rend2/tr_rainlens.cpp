/*
===========================================================================
Copyright (C) 2013 - 2016, OpenJK contributors

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

// Water on the camera lens (r_rainLens), see docs/rend2-rain-lens.md.
//
// Optional polish, off by default. The persistent lens state lives in
// tr_lenswater.cpp (droplet agents, wetness / film field, sheets, weather
// controller). This file feeds it the weather and camera input, rasterises
// it into a lower resolution lens field (glsl/rainlens.glsl: RG = unscaled
// UV offset, B = optical weight, A = blur radius) and runs one fullscreen HDR
// composite (glsl/rainlens_composite.glsl) in RB_PostProcess after the SMAA
// T2x temporal resolve and motion blur, before bloom extraction and tone
// mapping. The composite writes rainLensImage, a dedicated full resolution
// HDR target that the caller uses as the scene source for the remaining
// color passes; modern bloom also reads the lens field to refract the
// emissive MRT.
//
// Active only for the main world view of the frame. Rain exposure only
// controls new water: under cover the existing water keeps moving, merging
// and drying.

#include "tr_local.h"
#include "tr_weather.h"
#include "tr_lenswater.h"

#include <vector>

using namespace lenswater;

namespace {
LensWater s_water;
std::vector<float> s_instanceData;
int s_maxInstances;
int s_lastRefdefTime;
qboolean s_clockValid;
qboolean s_debugOutput;
unsigned s_lastFrame;
// front end water transition state (R_RainLensInput)
qboolean s_inWaterValid;
qboolean s_inWater;
int s_waterEnterTime;
int s_lastEmergeTime;
int s_pendingEvents;

const int MAX_LENS_DROPS = 128;
const int MAX_LENS_MICRO = 256;
const int MAX_LENS_SHEETS = 8;

int QualityLevel( void )
{
	return Com_Clampi(0, 2, r_rainLensQuality->integer);
}

int DropLimit( void )
{
	static const int limits[3] = { 48, 96, 128 };
	if ( r_rainLensAgentLimit->integer > 0 )
		return Com_Clampi(8, MAX_LENS_DROPS, r_rainLensAgentLimit->integer);
	return limits[QualityLevel()];
}

Params CurrentParams( void )
{
	Params p;
	p.density = Com_Clamp(0.0f, 2.0f, r_rainLensDensity->value);
	p.dropSize = Com_Clamp(0.25f, 4.0f, r_rainLensDropSize->value);
	p.pinning = Com_Clamp(0.1f, 4.0f, r_rainLensPinning->value);
	p.merge = Com_Clamp(0.0f, 2.0f, r_rainLensMerge->value);
	p.filmDecay = Com_Clamp(0.1f, 4.0f, r_rainLensFilmDecay->value);
	p.wetDecay = Com_Clamp(0.1f, 4.0f, r_rainLensWetDecay->value);
	p.heavyFlow = Com_Clamp(0.0f, 4.0f, r_rainLensHeavyFlow->value);
	p.peripheralBias = Com_Clamp(0.0f, 2.0f, r_rainLensPeripheralBias->value);
	p.maxDrops = DropLimit();
	p.maxMicro = MAX_LENS_MICRO;
	p.maxSheets = MAX_LENS_SHEETS;
	return p;
}

Profile ProfileFromWeather( int weather, float intensity )
{
	switch ( weather )
	{
	case RAIN_WEATHER_LIGHT: return PROFILE_LIGHT;
	case RAIN_WEATHER_NORMAL: return PROFILE_NORMAL;
	case RAIN_WEATHER_HEAVY: return PROFILE_HEAVY;
	case RAIN_WEATHER_ACID: return PROFILE_ACID;
	default:
		// unknown source: classify by particle count
		return intensity < 0.3f ? PROFILE_LIGHT : (intensity < 0.7f ? PROFILE_NORMAL : PROFILE_HEAVY);
	}
}

// nominal intensity of a forced profile (rainlens_profile without rain)
float ProfileIntensity( Profile profile )
{
	switch ( profile )
	{
	case PROFILE_LIGHT: return 0.2f;
	case PROFILE_HEAVY: return 1.0f;
	default: return 0.4f;
	}
}

void ResetState( void )
{
	s_lastRefdefTime = 0;
	s_clockValid = qfalse;
	s_debugOutput = qfalse;
	s_lastFrame = 0;
	s_inWaterValid = qfalse;
	s_inWater = qfalse;
	s_waterEnterTime = 0;
	s_lastEmergeTime = -100000;
	s_pendingEvents = 0;
}
} // namespace

void R_CreateRainLensImages( int width, int height, int hdrFormat )
{
	ResetState();
	tr.rainLensImage = NULL;
	tr.rainLensFieldImage = NULL;
	tr.rainLensFilmImage = NULL;
	tr.rainLensInstanceImage = NULL;

	if ( !r_rainLens->integer )
	{
		// no allocation, no simulation
		s_water = LensWater();
		s_instanceData.clear();
		s_instanceData.shrink_to_fit();
		return;
	}

	if ( !r_hdr->integer )
	{
		ri.Printf(PRINT_WARNING, "r_rainLens needs r_hdr 1, lens rain disabled\n");
		return;
	}

	tr.rainLensImage = R_CreateImage(
		"*rainLens", NULL, width, height, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, hdrFormat);

	// The lens field is quality bounded, not proportional to the display:
	// 256 / 360 / 540 texels high; only the composite runs at full resolution.
	static const int fieldHeights[3] = { 256, 360, 540 };
	int fieldHeight = r_rainLensFieldHeight->integer > 0 ?
		Com_Clampi(64, 2160, r_rainLensFieldHeight->integer) : fieldHeights[QualityLevel()];
	fieldHeight = Q_min(height, fieldHeight);
	const int fieldWidth = Q_max(1, width * fieldHeight / height);
	tr.rainLensFieldImage = R_CreateImage(
		"*rainLensField", NULL, fieldWidth, fieldHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA16F);

	// persistent wetness (R) and thin film (G)
	const int filmHeight = Q_min(height, 256);
	const int filmWidth = Q_max(1, width * filmHeight / height);
	tr.rainLensFilmImage = R_CreateImage(
		"*rainLensFilm", NULL, filmWidth, filmHeight, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RG16F);

	// droplet / sheet instance records, fetched by gl_InstanceID
	s_maxInstances = MAX_LENS_DROPS + MAX_LENS_MICRO + MAX_LENS_SHEETS;
	tr.rainLensInstanceImage = R_CreateImage(
		"*rainLensInstances", NULL, s_maxInstances, INSTANCE_TEXELS, IMGTYPE_COLORALPHA,
		IMGFLAG_NO_COMPRESSION | IMGFLAG_CLAMPTOEDGE, GL_RGBA32F);
	s_instanceData.assign((size_t)s_maxInstances * INSTANCE_FLOATS, 0.0f);

	const Profile override = s_water.GetProfileOverride();
	s_water = LensWater();
	s_water.Init(filmWidth, filmHeight);
	s_water.SetProfileOverride(override);
}

/*
=============
R_RainLensInput

Front end, R_AddPostProcessCmd. The weather and camera input of the lens:
rain intensity and subtype, whether new rain reaches it (main world view
outside while it rains), how much it faces into the rain, and water surface
transitions of the camera. R_IsOutside mutates a cache, so this runs here
rather than in the back end.
=============
*/
void R_RainLensInput( const trRefdef_t *refdef, const viewParms_t *viewParms, rainLensInput_t *input )
{
	Com_Memset(input, 0, sizeof(*input));

	if ( !r_rainLens->integer || !tr.rainLensImage || !tr.world )
		return;

	if ( refdef->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL) )
		return;

	if ( viewParms->viewParmType != VPT_MAIN || viewParms->isPortal || viewParms->isMirror
		|| (viewParms->flags & VPF_DEPTHSHADOW) )
		return;

	input->active = qtrue;

	// Leaving water seeds a lot of water at once; while submerged the lens
	// is not drawn and entering water wipes it. Eyes bobbing at the surface
	// must not emerge every frame: the camera has to be under for a moment,
	// and emerging repeats at most once a second.
	const qboolean inWater = (qboolean)((ri.CM_PointContents(refdef->vieworg, 0) & CONTENTS_WATER) != 0);
	if ( refdef->time < s_lastEmergeTime || refdef->time < s_waterEnterTime )
	{
		// game time restarted (map change)
		s_waterEnterTime = 0;
		s_lastEmergeTime = -100000;
	}
	if ( s_inWaterValid && inWater != s_inWater )
	{
		if ( inWater )
		{
			s_pendingEvents |= RAINLENS_EVENT_SUBMERGE;
			s_waterEnterTime = refdef->time;
		}
		else if ( refdef->time - s_waterEnterTime >= 250 && refdef->time - s_lastEmergeTime >= 1000 )
		{
			s_pendingEvents |= RAINLENS_EVENT_EMERGE;
			s_lastEmergeTime = refdef->time;
		}
	}
	s_inWater = inWater;
	s_inWaterValid = qtrue;
	input->submerged = inWater;
	input->events = s_pendingEvents;
	s_pendingEvents = 0;

	const weatherObject_t *rain = tr.weatherSystem ? &tr.weatherSystem->weatherSlots[WEATHER_RAIN] : NULL;
	const Profile forced = s_water.GetProfileOverride();
	if ( forced != PROFILE_AUTO && !(rain && rain->active) )
	{
		// rainlens_profile forces rain for testing on dry maps
		input->intensity = ProfileIntensity(forced);
		input->exposed = 1.0f;
		input->facing = Com_Clamp(0.0f, 1.0f, refdef->viewaxis[0][2]);
		return;
	}

	if ( !rain || !rain->active || inWater )
		return;

	// "lightrain" 1000, "rain" / "acidrain" 2000, "heavyrain" 5000 particles
	input->intensity = Com_Clamp(0.0f, 1.0f, rain->particleCount / 5000.0f);
	input->weather = tr.weatherSystem->rainSubtype;

	vec3_t origin;
	VectorCopy(refdef->vieworg, origin);
	input->exposed = R_IsOutside(origin) ? 1.0f : 0.0f;

	// Rain travels down, tilted by the wind; the lens gets more of it when
	// it faces into the rain.
	vec3_t fall;
	VectorSet(fall, tr.weatherSystem->windDirection[0], tr.weatherSystem->windDirection[1],
		-Q_max(rain->gravity, 0.1f) * 100.0f);
	const float horizontal = sqrtf(fall[0] * fall[0] + fall[1] * fall[1]);
	const float maxHorizontal = -fall[2];	// at most 45 degrees
	if ( horizontal > maxHorizontal )
	{
		fall[0] *= maxHorizontal / horizontal;
		fall[1] *= maxHorizontal / horizontal;
	}
	VectorNormalize(fall);
	input->facing = Com_Clamp(0.0f, 1.0f, -DotProduct(refdef->viewaxis[0], fall));

	// wind in lens space (viewaxis[1] points left), a hint for sheets
	vec3_t wind;
	VectorCopy(tr.weatherSystem->windDirection, wind);
	wind[2] = 0.0f;
	const float windSpeed = VectorNormalize(wind);
	const float windAmount = Com_Clamp(0.0f, 1.0f, windSpeed / 100.0f);
	input->windLens[0] = -DotProduct(wind, refdef->viewaxis[1]) * windAmount;
	input->windLens[1] = DotProduct(wind, refdef->viewaxis[2]) * windAmount;
}

static int RB_RainLensBeginTimer( const char *name )
{
	if ( !glRefConfig.timerQuery || r_speeds->integer != 100 )
		return -1;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	if ( tr.numTimedBlocks >= (MAX_GPU_TIMERS / 2) || frame->numTimers + 2 > MAX_GPU_TIMERS )
		return -1;

	const int handle = tr.numTimedBlocks++;
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	gpuTimedBlock_t *timedBlock = frame->timedBlocks + handle;
	timedBlock->beginTimer = timer->queryName;
	timedBlock->name = name;
	frame->numTimedBlocks++;

	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
	return handle;
}

static void RB_RainLensEndTimer( int handle )
{
	if ( handle < 0 )
		return;

	gpuFrame_t *frame = &backEndData->frames[backEndData->realFrameNumber % MAX_FRAMES];
	gpuTimer_t *timer = frame->timers + frame->numTimers++;
	frame->timedBlocks[handle].endTimer = timer->queryName;
	qglQueryCounter(timer->queryName, GL_TIMESTAMP);
}

/*
=============
RB_RainLensUpdate

Advances the lens water of the main scene by game time and tells whether
the pass runs this frame: while water is on the lens or new rain reaches it.
A dry lens under cover costs nothing.
=============
*/
qboolean RB_RainLensUpdate( const rainLensInput_t *input )
{
	s_debugOutput = qfalse;

	if ( !input || !input->active )
		return qfalse;

	if ( !tr.rainLensImage || !tr.rainLensFbo || !tr.rainLensFieldImage || !tr.rainLensFieldFbo
		|| !tr.rainLensFilmImage || !tr.rainLensInstanceImage )
		return qfalse;

	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		return qfalse;

	// the first post processed main world scene of a frame owns the lens
	if ( s_clockValid && s_lastFrame == backEndData->realFrameNumber )
		return qfalse;
	s_lastFrame = backEndData->realFrameNumber;

	// game time: pauses with the game, restarts on map change
	const int now = backEnd.refdef.time;
	const int delta = now - s_lastRefdefTime;
	float dt = 0.0f;
	if ( !s_clockValid || delta < 0 || delta > 1000 )
		s_water.Clear();	// cut: no water carried over
	else
		dt = delta * 0.001f;
	s_lastRefdefTime = now;
	s_clockValid = qtrue;

	Event event = {};
	event.strength = 1.0f;
	if ( input->events & RAINLENS_EVENT_SUBMERGE )
	{
		event.type = EVENT_SUBMERGE;
		s_water.QueueEvent(event);
	}
	if ( input->events & RAINLENS_EVENT_EMERGE )
	{
		event.type = EVENT_EMERGE;
		s_water.QueueEvent(event);
	}

	Input in;
	in.intensity = input->intensity;
	in.exposed = input->exposed;
	in.facing = input->facing;
	in.weather = ProfileFromWeather(input->weather, input->intensity);
	// World gravity on the lens plane. viewaxis[1] points left; lens X
	// points right and lens Y up, so looking up or down leaves little
	// tangential gravity and rolling the camera turns it.
	in.gravity.x = backEnd.refdef.viewaxis[1][2];
	in.gravity.y = -backEnd.refdef.viewaxis[2][2];
	in.wind.x = input->windLens[0];
	in.wind.y = input->windLens[1];

	const qboolean active = (qboolean)s_water.Update(dt, in, CurrentParams());

	if ( input->submerged )
		return qfalse;

	return (qboolean)(active || r_rainLensDebug->integer);
}

/*
=============
RB_RainLens

srcFbo holds the HDR scene (MSAA resolved, temporally resolved and motion
blurred). Rasterises the lens water into the lower resolution field, then
refracts the HDR scene into rainLensImage. The caller uses rainLensFbo for
subsequent color passes. Debug views are drawn by RB_RainLensDebugOverlay.
=============
*/
void RB_RainLens( FBO_t *srcFbo )
{
	if ( !srcFbo )
		return;

	const int debugView = r_rainLensDebug->integer;
	const Params params = CurrentParams();
	const float aspect = (float)tr.rainLensFieldFbo->width / (float)tr.rainLensFieldFbo->height;

	int timer = RB_RainLensBeginTimer("Rain lens field");

	if ( s_water.FilmDirty() )
	{
		// GL_BindToTMU skips the unit switch when the image is already bound
		GL_BindToTMU(tr.rainLensFilmImage, TB_COLORMAP);
		GL_SelectTexture(TB_COLORMAP);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s_water.FilmWidth(), s_water.FilmHeight(),
			GL_RG, GL_FLOAT, s_water.FilmData());
		s_water.ClearFilmDirty();
	}

	const int numInstances = s_water.BuildInstances(s_instanceData.data(), s_maxInstances, params.dropSize);
	if ( numInstances > 0 )
	{
		GL_BindToTMU(tr.rainLensInstanceImage, TB_COLORMAP);
		GL_SelectTexture(TB_COLORMAP);
		qglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, numInstances, INSTANCE_TEXELS,
			GL_RGBA, GL_FLOAT, s_instanceData.data());
	}

	GL_Cull(CT_TWO_SIDED);
	FBO_Bind(tr.rainLensFieldFbo);
	GL_SetViewportAndScissor(0, 0, tr.rainLensFieldFbo->width, tr.rainLensFieldFbo->height);

	vec4_t params1, params2;
	VectorSet4(params2, 0.0f, 0.0f,
		(float)tr.rainLensFieldFbo->width, (float)tr.rainLensFieldFbo->height);

	// thin film: weak refraction, low optical weight; it overwrites the field
	if ( s_water.FilmVisible() )
	{
		GL_State(GLS_DEPTHTEST_DISABLE);
		shaderProgram_t *film = &tr.rainLensShader[RAINLENSDEF_FILM];
		GLSL_BindProgram(film);
		GL_BindToTMU(tr.rainLensFilmImage, TB_COLORMAP);
		VectorSet4(params1, aspect, Com_Clamp(0.0f, 2.0f, r_rainLensFilm->value), 0.0f, 0.0f);
		GLSL_SetUniformVec4(film, UNIFORM_RAINLENSPARAMS, params1);
		GLSL_SetUniformVec4(film, UNIFORM_RAINLENSPARAMS2, params2);
		RB_InstantTriangle();
	}
	else
	{
		const vec4_t black = { 0.0f, 0.0f, 0.0f, 0.0f };
		qglClearBufferfv(GL_COLOR, 0, black);
	}

	// drops, micro drops and sheets: instanced analytic quads, added so
	// temporary overlaps sum their surface slopes instead of switching
	if ( numInstances > 0 )
	{
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		shaderProgram_t *drops = &tr.rainLensShader[RAINLENSDEF_DROPS];
		GLSL_BindProgram(drops);
		GL_BindToTMU(tr.rainLensInstanceImage, TB_COLORMAP);
		VectorSet4(params1, aspect, Com_Clamp(0.0f, 2.0f, r_rainLensBlur->value), 0.0f, 0.0f);
		GLSL_SetUniformVec4(drops, UNIFORM_RAINLENSPARAMS, params1);
		GLSL_SetUniformVec4(drops, UNIFORM_RAINLENSPARAMS2, params2);
		qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numInstances);
	}

	RB_RainLensEndTimer(timer);
	timer = RB_RainLensBeginTimer("Rain lens composite");

	static const float sampleCounts[3] = { 1.0f, 3.0f, 5.0f };
	GL_State(GLS_DEPTHTEST_DISABLE);
	FBO_Bind(tr.rainLensFbo);
	GL_SetViewportAndScissor(0, 0, tr.rainLensFbo->width, tr.rainLensFbo->height);
	shaderProgram_t *composite = &tr.rainLensCompositeShader;
	GLSL_BindProgram(composite);
	GL_BindToTMU(srcFbo->colorImage[0], TB_COLORMAP);
	GL_BindToTMU(tr.rainLensFieldImage, TB_LIGHTMAP);
	GL_BindToTMU(tr.rainLensFilmImage, TB_NORMALMAP);
	VectorSet4(params1, 0.0f, 0.0f,
		Com_Clamp(0.0f, 4.0f, r_rainLensRefraction->value), sampleCounts[QualityLevel()]);
	GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS, params1);
	VectorSet4(params2, 0.0f, 0.0f, (float)debugView, tr.linearLight ? 0.0f : 1.0f);
	GLSL_SetUniformVec4(composite, UNIFORM_RAINLENSPARAMS2, params2);
	RB_InstantTriangle();

	// agent views over the composited image
	if ( (debugView == 5 || debugView == 7 || debugView == 8) && numInstances > 0 )
	{
		GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		shaderProgram_t *agents = &tr.rainLensShader[RAINLENSDEF_DEBUG_AGENTS];
		GLSL_BindProgram(agents);
		GL_BindToTMU(tr.rainLensInstanceImage, TB_COLORMAP);
		VectorSet4(params1, aspect, 1.0f, 0.0f, (float)debugView);
		GLSL_SetUniformVec4(agents, UNIFORM_RAINLENSPARAMS, params1);
		VectorSet4(params2, 0.0f, 0.0f, (float)tr.rainLensFbo->width, (float)tr.rainLensFbo->height);
		GLSL_SetUniformVec4(agents, UNIFORM_RAINLENSPARAMS2, params2);
		qglDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numInstances);
		GL_State(GLS_DEPTHTEST_DISABLE);
	}
	if ( debugView )
		s_debugOutput = qtrue;

	RB_RainLensEndTimer(timer);
}

// r_rainLensDebug views, drawn at the end of the post process chain
void RB_RainLensDebugOverlay( void )
{
	if ( !s_debugOutput || !r_rainLensDebug->integer )
		return;

	vec4i_t dstBox;
	VectorSet4(dstBox, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
	FBO_FastBlitFromTexture(tr.rainLensImage, NULL, dstBox, NULL, 0);
}

/*
=============
Console commands (developer testing, independent of the weather)
=============
*/
static qboolean R_RainLensAvailable( void )
{
	if ( !tr.rainLensImage )
	{
		ri.Printf(PRINT_ALL, "Lens water is not active (r_rainLens 1, r_hdr 1, vid_restart)\n");
		return qfalse;
	}
	return qtrue;
}

void R_RainLensClear_f( void )
{
	if ( R_RainLensAvailable() )
		s_water.Clear();
}

void R_RainLensEvent_f( void )
{
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: rainlens_event <splash|spray|emerge> [strength] [side: left|right|up|down]\n");
		return;
	}
	if ( !R_RainLensAvailable() )
		return;

	Event event = {};
	const char *type = ri.Cmd_Argv(1);
	if ( !Q_stricmp(type, "splash") )
		event.type = EVENT_SPLASH;
	else if ( !Q_stricmp(type, "spray") )
		event.type = EVENT_SPRAY;
	else if ( !Q_stricmp(type, "emerge") )
		event.type = EVENT_EMERGE;
	else
	{
		ri.Printf(PRINT_ALL, "unknown lens water event '%s'\n", type);
		return;
	}
	event.strength = ri.Cmd_Argc() > 2 ? Com_Clamp(0.05f, 2.0f, atof(ri.Cmd_Argv(2))) : 1.0f;
	event.duration = 2.5f;
	if ( ri.Cmd_Argc() > 3 )
	{
		const char *side = ri.Cmd_Argv(3);
		if ( !Q_stricmp(side, "left") ) event.dir = { -1.0f, 0.0f };
		else if ( !Q_stricmp(side, "right") ) event.dir = { 1.0f, 0.0f };
		else if ( !Q_stricmp(side, "up") ) event.dir = { 0.0f, 1.0f };
		else if ( !Q_stricmp(side, "down") ) event.dir = { 0.0f, -1.0f };
	}
	s_water.QueueEvent(event);
}

void R_RainLensProfile_f( void )
{
	static const char *names[PROFILE_COUNT] = { "auto", "light", "rain", "heavy", "acid" };
	if ( ri.Cmd_Argc() < 2 )
	{
		ri.Printf(PRINT_ALL, "usage: rainlens_profile <auto|light|rain|heavy|acid> (current: %s)\n",
			names[s_water.GetProfileOverride()]);
		return;
	}
	for ( int i = 0; i < PROFILE_COUNT; i++ )
	{
		if ( !Q_stricmp(ri.Cmd_Argv(1), names[i]) )
		{
			s_water.SetProfileOverride((Profile)i);
			if ( i != PROFILE_AUTO )
				ri.Printf(PRINT_ALL, "Lens water profile forced to %s (rains on the lens even without weather)\n", names[i]);
			return;
		}
	}
	ri.Printf(PRINT_ALL, "unknown profile '%s'\n", ri.Cmd_Argv(1));
}

void R_RainLensStats_f( void )
{
	if ( !R_RainLensAvailable() )
		return;
	static const char *names[PROFILE_COUNT] = { "auto", "light", "rain", "heavy", "acid" };
	const Stats s = s_water.GetStats();
	ri.Printf(PRINT_ALL, "Lens water (%s profile%s):\n", names[s.profile],
		s_water.GetProfileOverride() != PROFILE_AUTO ? ", forced" : "");
	ri.Printf(PRINT_ALL, "  drops %d / %d (beads %d, residual %d, moving %d, flow heads %d)\n",
		s.drops, DropLimit(), s.beads, s.residuals, s.moving, s.flows);
	ri.Printf(PRINT_ALL, "  micro drops %d / %d, sheets %d / %d\n", s.micro, MAX_LENS_MICRO, s.sheets, MAX_LENS_SHEETS);
	ri.Printf(PRINT_ALL, "  lens field %dx%d, film %dx%d (%s, %s)\n",
		tr.rainLensFieldImage ? tr.rainLensFieldImage->width : 0,
		tr.rainLensFieldImage ? tr.rainLensFieldImage->height : 0,
		s.filmWidth, s.filmHeight, s.filmVisible ? "visible" : "dry", s.filmDirty ? "dirty" : "uploaded");
	ri.Printf(PRINT_ALL, "  agent update %.1f us CPU; GPU field / composite: r_speeds 100\n", s.updateMicroseconds);
}
