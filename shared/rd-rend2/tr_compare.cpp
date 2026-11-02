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

// One way to compare every renderer feature with the original renderer.
//
// Each feature of tr_compare.inc has r_<feature>Compare, not saved:
//   0  normal: the feature as its own cvars configure it
//   1  split screen: left of r_compareSplit the original, right the feature
//   2  off: the original everywhere, without vid_restart (the feature's
//      latched cvar stays on and keeps its resources)
//   3+ further modes of that feature (tr_compare.inc)
// The split covers the world views of a scene (main view, portals and sky
// portals drawn into it), never cubemap captures or 2D. A thin line marks it.
//
// The features read R_CompareOff / R_CompareSplitX; shaders with the Scene
// block test R_CompareOriginal(RC_x) against u_CompareSplit / u_CompareMask
// (R_CompareSceneParams). Debug cvars only show debug views.

#include "tr_local.h"

namespace
{
struct compareFeature_t
{
	const char *cvarName;
	const char *masterName;
	float activeMin;
	qboolean split;
	const char *title;
	const char *original;
	const char *note;
	int maxMode;
	cvar_t *cvar;
	cvar_t *master;
	char help[1024];
};

compareFeature_t s_compareFeatures[RC_COUNT] = {
#define COMPARE_FEATURE(id, cvar, master, activeMin, split, title, original, note) \
	{ cvar, master, activeMin, split, title, original, note },
#include "tr_compare.inc"
#undef COMPARE_FEATURE
};

int R_CompareExtraModes( const compareFeature_t& f )
{
	// notes of split features list their extra modes, one "  N ..." line each
	if ( !f.split || !f.note )
		return 0;
	int modes = 0;
	for ( const char *s = f.note; *s; s++ )
	{
		if ( *s == '\n' )
			modes++;
	}
	return modes;
}

void R_CompareBuildHelp( compareFeature_t& f )
{
	char *out = f.help;
	const size_t size = sizeof(f.help);
	Com_sprintf(out, size,
		"  0  normal: %s as configured\n", f.title);
	if ( f.split )
		Q_strcat(out, size, va("  1  split screen: left %s, right %s; the line is at r_compareSplit\n",
			f.original, f.title));
	else
		Q_strcat(out, size, va("  1  (no split screen: %s)\n", f.note));
	Q_strcat(out, size, va("  2  off: %s everywhere, without vid_restart\n", f.original));
	if ( f.split && f.note )
		Q_strcat(out, size, f.note);
	// strengths (activeMin between 0 and 1) are on above 0
	if ( f.activeMin > 0.0f && f.activeMin < 1.0f )
		Q_strcat(out, size, va("  compares while %s is above 0; example: %s %i\n",
			f.masterName, f.cvarName, f.split ? 1 : 2));
	else
		Q_strcat(out, size, va("  compares while %s is %g or more; example: %s %i\n",
			f.masterName, f.activeMin, f.cvarName, f.split ? 1 : 2));
}

int s_compareModeCount[RC_COUNT];
int s_compareNotedMaster[RC_COUNT];
} // namespace

cvar_t *r_compareSplit;

void R_CompareRegister( void )
{
	r_compareSplit = R_Cvar("r_compareSplit", "0.5", CVAR_ARCHIVE,
		"Position of the r_<feature>Compare split line: 0 left edge .. 1 right edge of the view");
	R_CvarRange(r_compareSplit, 0.0f, 1.0f, qfalse);

	for ( int i = 0; i < RC_COUNT; i++ )
	{
		compareFeature_t& f = s_compareFeatures[i];
		f.maxMode = 2 + R_CompareExtraModes(f);
		R_CompareBuildHelp(f);
		f.cvar = R_Cvar(f.cvarName, "0", CVAR_TEMP,
			va("Compare %s with the original renderer", f.title));
		// some compare cvars used to be saved: a config's old "seta" line keeps
		// the engine's archive flag and its stale view, so drop both
		if ( f.cvar->flags & CVAR_ARCHIVE )
		{
			f.cvar->flags &= ~CVAR_ARCHIVE;
			ri.Cvar_Set(f.cvarName, "0");
		}
		R_CvarRange(f.cvar, 0, (float)f.maxMode, qtrue);
		R_CvarHelpText(f.cvar, f.help);
		f.master = NULL;
		s_compareModeCount[i] = -1;
		s_compareNotedMaster[i] = -1;
	}
}

static compareFeature_t& R_CompareFeature( rendererCompare_t feature )
{
	return s_compareFeatures[Com_Clampi(0, RC_COUNT - 1, (int)feature)];
}

static cvar_t *R_CompareMaster( compareFeature_t& f )
{
	if ( !f.master )
		f.master = R_CvarFind(f.masterName);
	return f.master;
}

int R_CompareMode( rendererCompare_t feature )
{
	compareFeature_t& f = R_CompareFeature(feature);
	if ( !f.cvar )
		return COMPARE_ON;

	int mode = Com_Clampi(0, f.maxMode, f.cvar->integer);
	if ( mode == COMPARE_SPLIT && !f.split )
	{
		// R_CompareValidate explains and resets it; until then: normal
		mode = COMPARE_ON;
	}
	return mode;
}

qboolean R_CompareOff( rendererCompare_t feature )
{
	return (qboolean)(R_CompareMode(feature) == COMPARE_OFF);
}

qboolean R_CompareSplit( rendererCompare_t feature )
{
	return (qboolean)(R_CompareMode(feature) == COMPARE_SPLIT);
}

float R_CompareSplitX( rendererCompare_t feature )
{
	if ( !R_CompareSplit(feature) )
		return -1.0f;
	return backEnd.refdef.compareSplitX;
}

float R_CompareSplitPosition( int viewportX, int viewportWidth )
{
	return (float)viewportX + Com_Clamp(0.0f, 1.0f, r_compareSplit->value) * (float)viewportWidth;
}

void R_CompareSceneParams( vec4_t split, int mask[4] )
{
	mask[0] = mask[1] = mask[2] = mask[3] = 0;
	for ( int i = 0; i < RC_COUNT; i++ )
	{
		if ( R_CompareSplit((rendererCompare_t)i) )
			mask[i >> 5] |= 1 << (i & 31);
	}
	VectorSet4(split, backEnd.refdef.compareSplitX, 0.0f, 0.0f, 0.0f);
}

qboolean R_CompareAnySplit( void )
{
	for ( int i = 0; i < RC_COUNT; i++ )
	{
		if ( R_CompareSplit((rendererCompare_t)i) )
			return qtrue;
	}
	return qfalse;
}

/*
=================
R_CompareValidate

Once per frame after R_CvarValidateAll: a split screen asked of a feature
without one is rejected with its help, and a compare of a feature that is
off says so once.
=================
*/
void R_CompareValidate( void )
{
	for ( int i = 0; i < RC_COUNT; i++ )
	{
		compareFeature_t& f = s_compareFeatures[i];
		if ( !f.cvar || f.cvar->modificationCount == s_compareModeCount[i] )
			continue;
		s_compareModeCount[i] = f.cvar->modificationCount;

		if ( f.cvar->integer == COMPARE_SPLIT && !f.split )
		{
			ri.Printf(PRINT_WARNING, "%s 1: no split screen for %s, %s; use %s 2 to turn it off.\n",
				f.cvarName, f.title, f.note, f.cvarName);
			R_CvarHelpPrint(f.cvarName);
			ri.Cvar_Set(f.cvarName, "0");
			s_compareModeCount[i] = f.cvar->modificationCount;
			continue;
		}

		cvar_t *master = R_CompareMaster(f);
		if ( f.cvar->integer != COMPARE_ON && master && master->value < f.activeMin &&
			s_compareNotedMaster[i] != f.cvar->modificationCount )
		{
			s_compareNotedMaster[i] = f.cvar->modificationCount;
			ri.Printf(PRINT_ALL, "%s: %s is %s, so there is nothing to compare (needs %s %s).\n",
				f.cvarName, f.masterName, master->string, f.masterName,
				(f.activeMin > 0.0f && f.activeMin < 1.0f) ? "above 0" : va("%g or more", f.activeMin));
		}
	}
}

/*
=================
GL_CompareScissor

A scissor narrower than the viewport (one side of the split). The viewport
cache is cleared, so the next GL_SetViewportAndScissor sets both again.
=================
*/
void GL_CompareScissor( int x, int y, int width, int height )
{
	qglScissor(x, y, Q_max(0, width), Q_max(0, height));
	glState.viewportSize[0] = -1;
}

// the left of the split line of this view, or qfalse
static qboolean RB_CompareLeftBox( rendererCompare_t feature, int box[4] )
{
	const float splitX = R_CompareSplitX(feature);
	const viewParms_t& v = backEnd.viewParms;
	box[0] = v.viewportX;
	box[1] = v.viewportY;
	box[2] = Q_min((int)splitX, v.viewportX + v.viewportWidth) - v.viewportX;
	box[3] = v.viewportHeight;
	return (qboolean)(splitX >= 0.0f && box[2] > 0);
}

/*
=================
RB_CompareSaveScene / RB_CompareRestoreScene

Split screen of a composite that draws over the scene in renderFbo (fog,
atmosphere, clouds): save the scene color before it, put the left of the
split line back after it. Only color attachment 0 (the glow MRT keeps the
composite).
=================
*/
static rendererCompare_t s_compareSaved = RC_COUNT;

qboolean RB_CompareSaveScene( rendererCompare_t feature )
{
	int box[4];
	FBO_t *scene = glState.currentFBO;
	if ( !RB_CompareLeftBox(feature, box) || !scene || scene != tr.renderFbo )
		return qfalse;

	FBO_t *scratch = FBO_CompareScratch();
	if ( !scratch )
		return qfalse;

	FBO_BlitColorRect(scene, 0, scratch, 0, box);
	s_compareSaved = feature;
	return qtrue;
}

void RB_CompareRestoreScene( rendererCompare_t feature )
{
	int box[4];
	if ( s_compareSaved != feature )
		return;
	s_compareSaved = RC_COUNT;
	if ( !tr.compareFbo || !RB_CompareLeftBox(feature, box) || glState.currentFBO != tr.renderFbo )
		return;

	FBO_BlitColorRect(tr.compareFbo, 0, tr.renderFbo, 0, box);
}

/*
=================
RB_CompareRestoreLeft

Split screen of a post-processing pass that reads src and writes dst (motion
blur, lens water): the left of the split line of dst gets src again.
=================
*/
void RB_CompareRestoreLeft( rendererCompare_t feature, FBO_t *src, FBO_t *dst )
{
	int box[4];
	if ( !src || !dst || src == dst || !RB_CompareLeftBox(feature, box) )
		return;
	if ( src->width != dst->width || src->height != dst->height )
		return;

	FBO_BlitColorRect(src, 0, dst, 0, box);
}

/*
=================
RB_CompareDrawSplitLine

A one pixel line on the split of the world view, after post-processing.
=================
*/
void RB_CompareDrawSplitLine( void )
{
	if ( backEnd.refdef.compareSplitX < 0.0f || !R_CompareAnySplit() )
		return;

	// a scissored clear: no program or blend state involved
	GL_CompareScissor((int)backEnd.refdef.compareSplitX, backEnd.viewParms.viewportY, 1,
		backEnd.viewParms.viewportHeight);
	qglClearColor(0.9f, 0.9f, 0.9f, 1.0f);
	qglClear(GL_COLOR_BUFFER_BIT);
	GL_SetViewportAndScissor(backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight);
}

/*
=================
R_Compare_f

r_compare: the compare cvars, their state and their features.
r_compare reset: everything back to 0.
=================
*/
void R_Compare_f( void )
{
	const qboolean reset = (qboolean)(ri.Cmd_Argc() >= 2 && !Q_stricmp(ri.Cmd_Argv(1), "reset"));
	if ( ri.Cmd_Argc() >= 2 && !reset )
	{
		ri.Printf(PRINT_ALL,
			"usage: r_compare            list the r_<feature>Compare cvars and what they compare\n"
			"       r_compare reset      set all of them back to 0\n"
			"each one: 0 normal, 1 split screen (left original), 2 off, without vid_restart\n"
			"example: r_ssrCompare 1, r_compareSplit 0.3\n");
		return;
	}

	int active = 0;
	for ( int i = 0; i < RC_COUNT; i++ )
	{
		compareFeature_t& f = s_compareFeatures[i];
		if ( !f.cvar )
			continue;
		if ( reset )
		{
			if ( f.cvar->integer )
				ri.Cvar_Set(f.cvarName, "0");
			continue;
		}

		cvar_t *master = R_CompareMaster(f);
		const qboolean on = (qboolean)(master && master->value >= f.activeMin);
		ri.Printf(PRINT_ALL, "%-40s %s  %s%s%s\n", f.cvarName, f.cvar->string, f.title,
			f.split ? "" : " (no split)", on ? "" : " [feature off]");
		active += f.cvar->integer ? 1 : 0;
	}

	if ( reset )
		ri.Printf(PRINT_ALL, "all compare cvars are 0\n");
	else
		ri.Printf(PRINT_ALL, "%i compare cvars, %i set; 0 normal, 1 split, 2 off; r_help <cvar> for details\n",
			RC_COUNT, active);
}
