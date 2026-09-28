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

// tr_pom.cpp -- parallax occlusion mapping extensions, docs/rend2-pom.md:
// self shadowing of direct light (r_pomSelfShadow), adaptive view ray steps
// (r_pomAdaptiveSteps), distance fade (r_pomFadeStart / r_pomFadeEnd) and
// debug views (r_pomDebug). The GLSL side is GetPomSelfShadow and the
// PomSurface state in lightall.glsl; everything is driven by per draw
// uniforms, so none of the cvars needs a restart. With every cvar at its
// default the shaders run the legacy 16 + 8 step POM unchanged.

#include "tr_local.h"

static struct
{
	int			lastSelfShadow;
	int			lastParallaxMapping;
	qboolean	warnedParallax;
	qboolean	frozen;
	vec3_t		frozenSun;
} s_pomState = { -1, -1, qfalse, qfalse, { 0.0f, 0.0f, 0.0f } };

void R_PomBeginFrame( void )
{
	if ( r_pomSelfShadow->integer != s_pomState.lastSelfShadow ||
		r_parallaxMapping->integer != s_pomState.lastParallaxMapping )
	{
		s_pomState.lastSelfShadow = r_pomSelfShadow->integer;
		s_pomState.lastParallaxMapping = r_parallaxMapping->integer;
		s_pomState.warnedParallax = qfalse;
	}

	// the feature stays inactive until its dependency is enabled; it is
	// never switched on for the user
	if ( r_pomSelfShadow->integer && !r_parallaxMapping->integer && !s_pomState.warnedParallax )
	{
		s_pomState.warnedParallax = qtrue;
		ri.Printf( PRINT_WARNING, "POM self-shadowing requires r_parallaxMapping 1\n" );
	}

	if ( !r_pomDebugFreezeLight->integer )
		s_pomState.frozen = qfalse;
}

// local lights with a self shadow ray per pixel (u_PomLod.z): 0 none,
// >= 256 all of them
static float R_PomLocalLights( void )
{
	switch ( r_pomSelfShadowLightMode->integer )
	{
	case 0:
		return 0.0f;
	case 1:
		return 1.0f;
	case 2:
		return (float)r_pomSelfShadowMaxLocalLights->integer;
	default:
		return 256.0f;
	}
}

void R_PomSetUniforms( const shaderStage_t *stage, UniformDataWriter& uniforms )
{
	const viewParms_t& viewParms = backEnd.viewParms;
	const bool lit = !backEnd.depthFill && !(viewParms.flags & VPF_DEPTHSHADOW);
	const bool parallax = r_parallaxMapping->integer &&
		(stage->glslShaderIndex & LIGHTDEF_USE_PARALLAXMAP);

	// global strength x material strength, never above 1 (no negative light)
	float strength = 0.0f;
	if ( lit && parallax && r_pomSelfShadow->integer )
	{
		const float material = stage->pomSelfShadowSet ? stage->pomSelfShadowStrength : 1.0f;
		strength = Com_Clamp( 0.0f, 1.0f, r_pomSelfShadowStrength->value * material );
	}
	const vec4_t shadow = {
		strength,
		(float)r_pomSelfShadowSteps->integer,
		r_pomSelfShadowBias->value,
		r_pomSelfShadowSoftness->value };

	const float minSteps = (float)r_pomMinSteps->integer;
	const vec4_t traversal = {
		r_pomAdaptiveSteps->integer ? 1.0f : 0.0f,
		minSteps,
		Q_max( minSteps, (float)r_pomMaxSteps->integer ),
		(float)r_pomBinarySteps->integer };

	const float fadeStart = r_pomFadeStart->value;
	const float fadeEnd = r_pomFadeEnd->value;
	const vec4_t lod = {
		fadeStart,
		fadeEnd > fadeStart ? 1.0f / (fadeEnd - fadeStart) : 0.0f,
		R_PomLocalLights(),
		0.0f };

	vec4_t debug = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( lit )
	{
		if ( r_pomDebugFreezeLight->integer )
		{
			if ( !s_pomState.frozen )
			{
				VectorCopy( backEnd.refdef.sunDir, s_pomState.frozenSun );
				s_pomState.frozen = qtrue;
			}
			VectorCopy( s_pomState.frozenSun, debug );
		}
		debug[3] = (float)r_pomDebug->integer;
	}

	uniforms.SetUniformVec4( UNIFORM_POMSHADOW, shadow );
	uniforms.SetUniformVec4( UNIFORM_POMTRAVERSAL, traversal );
	uniforms.SetUniformVec4( UNIFORM_POMLOD, lod );
	uniforms.SetUniformVec4( UNIFORM_POMDEBUG, debug );
}
