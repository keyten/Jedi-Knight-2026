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

// Skin subsurface scattering (r_skinSSS), only on stages classified as skin.
//
//   r_skinSSS 0  off: lightall is compiled without USE_SKIN_SSS, output unchanged
//   r_skinSSS 1  cheap: per channel wrapped diffuse for the direct, sun and
//                dynamic lights of skin stages (red wraps furthest). An
//                approximation of the soft terminator, not subsurface scattering.
//   r_skinSSS 2  screen-space diffusion: lightall also writes the diffuse light
//                of skin stages into attachment 7 of renderFbo (RGBA16F, rgb =
//                skin diffuse in scene space, a = view depth). After the opaque
//                surfaces (tr_screenspace.cpp, before SSGI and SSR) a separable
//                depth / normal aware blur with a skin profile diffuses it and
//                the composite swaps it in:  color += strength * (diffused - sharp).
//                Specular, eyes, metal, emission stay sharp. Needs a float
//                renderFbo (r_hdr 1) and 8 color attachments, else mode 1.
//
// Which stages are skin (R_SkinSSSClassifyShader, once per shader):
//   the MATCLASS_SKIN class of the auto PBR classifier (tr_autopbr.cpp), minus
//   eyes / teeth / mouth / cap textures, minus *_head textures (hair, ears and neck in
//   one texture) unless r_skinSSSMixedHeads, minus shaders with an alpha blended lit
//   layer on top (jedi_tf). Shader keywords override it: skinScatter <0..1>,
//   skinMask <image> (R = scatter per texel, implies skinScatter 1).
//
// Kernel: sum of 6 Gaussians skin profile (d'Eon and Luebke, GPU Gems 3,
// ch. 14; variances in mm^2), sampled as a separable 1D kernel with importance
// spaced offsets (Jimenez et al., separable SSS). Red scatters furthest. The
// radius is r_skinSSSWidth millimeters, 1 map unit = 28 mm (a 64 unit tall
// player is 1.8 m), projected with the view depth of each pixel: close faces
// get a few pixels, distant characters none.
//
// See docs/rend2-skin-sss.md.

#include "tr_local.h"

#define SKIN_SSS_MM_PER_UNIT	28.0f
#define SKIN_SSS_MAX_PIXELS		48.0f
#define SKIN_SSS_PROFILE_MM		8.0f	// kernel radius of the physical profile

// r_skinSSSDebug
enum
{
	SKIN_DEBUG_CLASSIFICATION = 1,
	SKIN_DEBUG_MASK,
	SKIN_DEBUG_RAW,
	SKIN_DEBUG_HORIZONTAL,
	SKIN_DEBUG_VERTICAL,
	SKIN_DEBUG_DELTA,
	SKIN_DEBUG_NOT_SKIN,
	SKIN_DEBUG_RADIUS,
};

static int s_mode = 0;					// as built (GLSL defines), see R_SkinSSSSelectResources
static qboolean s_resources = qfalse;	// mode 2 attachment and passes
static qboolean s_maskUnitsOk = qfalse;

/*
============================================================

Resources

============================================================
*/

int R_SkinSSSMode( void )
{
	return s_mode;
}

qboolean R_SkinSSSResourcesEnabled( void )
{
	return s_resources;
}

// called by R_CreateScreenSpaceImages when the GPU shaders are (re)built
void R_SkinSSSSelectResources( void )
{
	s_mode = r_skinSSS->integer;
	s_resources = qfalse;

	GLint units = 0;
	qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
	s_maskUnitsOk = (qboolean)(units > TB_SKINMASK);
	if ( s_mode && !s_maskUnitsOk )
		ri.Printf(PRINT_WARNING, "r_skinSSS: skinMask needs more than %d texture units, masks are ignored\n", TB_SKINMASK);

	if ( s_mode != 2 )
		return;

	GLint drawBuffers = 0;
	qglGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
	if ( glRefConfig.maxColorAttachments <= SCREEN_ATTACHMENT_SKIN || drawBuffers <= SCREEN_ATTACHMENT_SKIN )
	{
		ri.Printf(PRINT_WARNING, "r_skinSSS 2 needs %d color attachments / draw buffers (have %d / %d), using r_skinSSS 1\n",
			SCREEN_ATTACHMENT_SKIN + 1, glRefConfig.maxColorAttachments, drawBuffers);
		s_mode = 1;
		return;
	}
	if ( !r_hdr->integer )
	{
		// the composite adds a signed difference to color 0
		ri.Printf(PRINT_WARNING, "r_skinSSS 2 needs r_hdr 1 (float scene buffer), using r_skinSSS 1\n");
		s_mode = 1;
		return;
	}

	s_resources = qtrue;
}

void R_CreateSkinSSSImages( int width, int height )
{
	tr.skinDiffuseImage = NULL;
	tr.skinBlurImage[0] = NULL;
	tr.skinBlurImage[1] = NULL;

	if ( !s_resources )
		return;

	// attachment of renderFbo (MSAA: resolve target)
	tr.skinDiffuseImage = R_ScreenCreateImage("*skinDiffuse", width, height, GL_RGBA16F, qfalse);
	tr.skinBlurImage[0] = R_ScreenCreateImage("*skinBlurH", width, height, GL_RGBA16F, qfalse);
	tr.skinBlurImage[1] = R_ScreenCreateImage("*skinBlurV", width, height, GL_RGBA16F, qfalse);
}

void R_CreateSkinSSSFBOs( void )
{
	tr.skinBlurFbo[0] = NULL;
	tr.skinBlurFbo[1] = NULL;

	if ( !s_resources )
		return;

	tr.skinBlurFbo[0] = R_ScreenCreateLevelFBO("_skinBlurH", tr.skinBlurImage[0], 0);
	tr.skinBlurFbo[1] = R_ScreenCreateLevelFBO("_skinBlurV", tr.skinBlurImage[1], 0);
}

/*
============================================================

Classification (once per shader, at registration)

============================================================
*/

static const char *skinExcludedTokens[] = {
	"eyes", "eye", "eyesmouth", "moutheyes", "teeth", "mouth",
	"cap", "caps",	// hats (bespin_cop), dismemberment caps
};

static qboolean IsOpaqueBlend( uint32_t stateBits )
{
	const uint32_t blendBits = stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
	return (qboolean)(blendBits == 0 || blendBits == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO));
}

static qboolean IsLitStage( const shaderStage_t *stage )
{
	return (qboolean)(stage->glslShaderGroup == tr.lightallShader &&
		(stage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK) != 0);
}

static void SetSkin( shaderStage_t *stage, float scatter, const char *reason )
{
	stage->skinScatter = scatter;
	stage->skinReason = reason;
}

/*
===============
R_SkinSSSClassifyShader

Called after CollapseStagesToGLSL. Only the opaque lit base of a skin
material scatters; the decision is kept on the stage (skinScatter, skinReason)
whatever r_skinSSS is, so the debug views and r_skinSSSList work in every mode.
===============
*/
void R_SkinSSSClassifyShader( const shader_t *sh, shaderStage_t *stages, int numStages )
{
	for ( int i = 0; i < numStages; i++ )
	{
		shaderStage_t *stage = &stages[i];
		if ( !stage->active )
			continue;

		const float forced = stage->skinScatterSet ? stage->skinScatter :
			(stage->skinMaskImage ? 1.0f : -1.0f);
		SetSkin( stage, 0.0f, "not skin" );

		if ( !IsLitStage( stage ) )
		{
			stage->skinReason = "not lit";
			continue;
		}
		if ( !IsOpaqueBlend( stage->stateBits ) )
		{
			stage->skinReason = "blended";
			continue;
		}

		// a lit layer alpha blended on top (jedi_tf tinted skin + painted
		// details): the attachment only has the base, the difference would
		// show through the layer
		qboolean layered = qfalse;
		for ( int j = i + 1; j < numStages; j++ )
		{
			const shaderStage_t *over = &stages[j];
			if ( over->active && IsLitStage( over ) && !IsOpaqueBlend( over->stateBits ) &&
				(over->stateBits & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_SRC_ALPHA )
			{
				layered = qtrue;
			}
		}

		if ( forced >= 0.0f )
		{
			SetSkin( stage, forced, stage->skinMaskImage ? "skinMask keyword" : "skinScatter keyword" );
			if ( layered && forced > 0.0f )
				SetSkin( stage, 0.0f, "layered" );
			continue;
		}

		// the auto PBR classes; stages it leaves alone (authored PBR maps)
		// are classified here with the same rules, without touching their class
		materialMatch_t match;
		if ( stage->materialReason )
		{
			match.cls = stage->materialClass;
			match.reason = stage->materialReason;
			match.token = stage->materialToken;
		}
		else
		{
			const image_t *diffuse = stage->bundle[TB_DIFFUSEMAP].image[0];
			R_ClassifyMaterialName( &match, sh->name, diffuse ? diffuse->imgName : NULL );
		}

		if ( match.cls != MATCLASS_SKIN )
			continue;

		if ( match.token )
		{
			qboolean excluded = qfalse;
			for ( size_t t = 0; t < ARRAY_LEN( skinExcludedTokens ); t++ )
			{
				if ( !Q_stricmp( match.token, skinExcludedTokens[t] ) )
					excluded = qtrue;
			}
			if ( excluded )
			{
				SetSkin( stage, 0.0f, "excluded part" );
				continue;
			}
			if ( !Q_stricmp( match.token, "head" ) && !r_skinSSSMixedHeads->integer )
			{
				SetSkin( stage, 0.0f, "mixed head" );
				continue;
			}
		}

		if ( layered )
		{
			SetSkin( stage, 0.0f, "layered" );
			continue;
		}

		SetSkin( stage, 1.0f, "skin" );
	}
}

/*
============================================================

Draw time (lightall)

============================================================
*/

void R_SkinSSSSetupDraw( const shaderStage_t *stage, UniformDataWriter& uniforms, SamplerBindingsWriter& samplers )
{
	if ( !s_mode )
		return;

	const viewParms_t& viewParms = backEnd.viewParms;
	vec4_t params = { 0.0f, 0.0f, -1.0f, 0.0f };
	vec4_t wrap = { 0.0f, 0.0f, 0.0f, 0.0f };

	if ( stage->skinScatter > 0.0f && !backEnd.depthFill && !(viewParms.flags & VPF_DEPTHSHADOW) )
	{
		params[0] = stage->skinScatter;
		if ( stage->skinMaskImage && s_maskUnitsOk )
		{
			params[1] = 1.0f;
			samplers.AddStaticImage(stage->skinMaskImage, TB_SKINMASK);
		}

		// r_skinSSSCompare: left half without (the wrap / transmission of
		// lightall; the diffusion is split by its composite)
		if ( r_skinSSSCompare->integer )
			params[2] = viewParms.viewportX + 0.5f * viewParms.viewportWidth;

		if ( s_mode == 1 )
		{
			const float w = Com_Clamp(0.0f, 1.0f, r_skinSSSWrap->value);
			VectorSet(wrap, w, 0.45f * w, 0.25f * w);
		}
		wrap[3] = Q_max(0.0f, r_skinSSSTransmission->value);
	}

	uniforms.SetUniformVec4(UNIFORM_SKINPARAMS, params);
	uniforms.SetUniformVec4(UNIFORM_SKINWRAP, wrap);
}

// r_skinSSSDebug 1: flat classification colors through u_MaterialDebug
qboolean R_SkinSSSDebugColor( const shaderStage_t *stage, vec4_t out )
{
	if ( r_skinSSSDebug->integer != SKIN_DEBUG_CLASSIFICATION || !s_mode || !IsLitStage( stage ) )
		return qfalse;

	const char *reason = stage->skinReason ? stage->skinReason : "";
	if ( stage->skinScatter > 0.0f )
		VectorSet(out, 1.0f, 0.45f, 0.1f);			// scatters: orange
	else if ( !Q_stricmp( reason, "excluded part" ) )
		VectorSet(out, 1.0f, 0.95f, 0.2f);			// skin class, excluded: yellow
	else if ( !Q_stricmp( reason, "mixed head" ) || !Q_stricmp( reason, "layered" ) )
		VectorSet(out, 0.75f, 0.2f, 0.95f);			// needs a mask: purple
	else if ( stage->skinScatterSet )
		VectorSet(out, 0.2f, 0.5f, 1.0f);			// turned off by keyword: blue
	else
		VectorSet(out, 0.35f, 0.35f, 0.35f);		// not skin: gray
	out[3] = 1.0f;
	return qtrue;
}

qboolean RB_SkinSSSDebugBypassesToneMap( void )
{
	const int view = r_skinSSSDebug->integer;
	return (qboolean)(s_mode && (view == SKIN_DEBUG_CLASSIFICATION ||
		(s_resources && (view == SKIN_DEBUG_MASK || view == SKIN_DEBUG_DELTA || view == SKIN_DEBUG_RADIUS))));
}

const char *R_SkinSSSStageInfo( const shaderStage_t *stage )
{
	return stage->skinReason ? stage->skinReason : "-";
}

/*
============================================================

Kernel

============================================================
*/

// d'Eon and Luebke skin profile: variance (mm^2), RGB weight
static const struct
{
	float variance;
	float weight[3];
} skinProfile[6] = {
	{ 0.0064f, { 0.233f, 0.455f, 0.649f } },
	{ 0.0484f, { 0.100f, 0.336f, 0.344f } },
	{ 0.187f,  { 0.118f, 0.198f, 0.000f } },
	{ 0.567f,  { 0.113f, 0.007f, 0.007f } },
	{ 1.99f,   { 0.358f, 0.004f, 0.000f } },
	{ 7.41f,   { 0.078f, 0.000f, 0.000f } },
};

static void SkinProfile( float x, vec3_t out )
{
	VectorClear(out);
	for ( size_t i = 0; i < ARRAY_LEN(skinProfile); i++ )
	{
		// 1D Gaussian: the marginal of the radial profile along one axis
		const float v = skinProfile[i].variance;
		const float g = expf(-x * x / (2.0f * v)) / sqrtf(2.0f * M_PI * v);
		for ( int c = 0; c < 3; c++ )
			out[c] += skinProfile[i].weight[c] * g;
	}
}

static int SkinTaps( void )
{
	static const int taps[3] = { 11, 17, 25 };
	return taps[Com_Clampi(0, 2, r_skinSSSQuality->integer)];
}

static struct
{
	int taps;
	vec4_t kernel[SKIN_SSS_MAX_TAPS];	// rgb weight, a = offset in radii; [0] = centre
} s_kernel;

// the physical profile over +-SKIN_SSS_PROFILE_MM; r_skinSSSWidth only scales
// its screen size (as the SSS width of Jimenez et al.)
static void SkinBuildKernel( int taps )
{
	if ( s_kernel.taps == taps )
		return;

	s_kernel.taps = taps;

	// offsets in [-1, 1], denser near the centre (exponent 2)
	float offset[SKIN_SSS_MAX_TAPS];
	const float step = 2.0f / (taps - 1);
	for ( int i = 0; i < taps; i++ )
	{
		const float o = -1.0f + i * step;
		offset[i] = (o < 0.0f ? -1.0f : 1.0f) * o * o;
	}

	vec4_t k[SKIN_SSS_MAX_TAPS];
	for ( int i = 0; i < taps; i++ )
	{
		// the profile integrated over the interval the tap stands for
		const float lo = i > 0 ? offset[i - 1] : offset[i];
		const float hi = i < taps - 1 ? offset[i + 1] : offset[i];
		float area = 0.5f * (hi - lo);
		if ( i == 0 || i == taps - 1 )
			area *= 2.0f;

		vec3_t p;
		SkinProfile(offset[i] * SKIN_SSS_PROFILE_MM, p);
		VectorScale(p, area, k[i]);
		k[i][3] = offset[i];
	}

	// centre first
	const int center = taps / 2;
	VectorCopy4(k[center], s_kernel.kernel[0]);
	int n = 1;
	for ( int i = 0; i < taps; i++ )
	{
		if ( i != center )
		{
			// VectorCopy4 is a macro and evaluates its destination four times.
			VectorCopy4(k[i], s_kernel.kernel[n]);
			n++;
		}
	}

	// each channel sums to 1: the diffusion moves light, it does not add any
	vec3_t sum = { 0.0f, 0.0f, 0.0f };
	for ( int i = 0; i < taps; i++ )
		VectorAdd(sum, s_kernel.kernel[i], sum);
	for ( int i = 0; i < taps; i++ )
	{
		for ( int c = 0; c < 3; c++ )
			s_kernel.kernel[i][c] /= Q_max(sum[c], 1e-6f);
	}
	for ( int i = taps; i < SKIN_SSS_MAX_TAPS; i++ )
		VectorSet4(s_kernel.kernel[i], 0.0f, 0.0f, 0.0f, 0.0f);
}

static float SkinWidthMM( void )
{
	return Com_Clamp(0.1f, 40.0f, r_skinSSSWidth->value);
}

// r_skinSSSKernel: prints the kernel and the pixel radius at a few depths
void R_SkinSSSKernel_f( void )
{
	const int taps = SkinTaps();
	const float widthMM = SkinWidthMM();
	SkinBuildKernel(taps);

	ri.Printf(PRINT_ALL, "skin SSS kernel: %d taps, radius %.1f mm (%.3f units), profile x %.2f\n",
		taps, widthMM, widthMM / SKIN_SSS_MM_PER_UNIT, widthMM / SKIN_SSS_PROFILE_MM);
	ri.Printf(PRINT_ALL, "%4s %8s %8s %8s %8s\n", "tap", "mm", "red", "green", "blue");
	for ( int i = 0; i < taps; i++ )
	{
		const vec_t *k = s_kernel.kernel[i];
		ri.Printf(PRINT_ALL, "%4d %8.3f %8.4f %8.4f %8.4f\n", i, k[3] * widthMM, k[0], k[1], k[2]);
	}

	// pixels per unit at depth 1: P[5] * height / 2
	const float fovY = backEnd.viewParms.fovY > 0.0f ? backEnd.viewParms.fovY : 73.74f;
	const float height = (float)glConfig.vidHeight;
	const float projScale = 0.5f * height / tanf(DEG2RAD(fovY * 0.5f));
	const float radiusUnits = widthMM / SKIN_SSS_MM_PER_UNIT;
	ri.Printf(PRINT_ALL, "pixel radius (height %d, fov y %.1f):", (int)height, fovY);
	const float depths[] = { 20.0f, 30.0f, 60.0f, 100.0f, 200.0f, 300.0f };
	for ( size_t i = 0; i < ARRAY_LEN(depths); i++ )
		ri.Printf(PRINT_ALL, " %.0fu %.2fpx", depths[i], Q_min(radiusUnits * projScale / depths[i], SKIN_SSS_MAX_PIXELS));
	ri.Printf(PRINT_ALL, "\n");
}

/*
============================================================

View

============================================================
*/

qboolean RB_SkinSSSWantsView( void )
{
	if ( !s_resources )
		return qfalse;

	// strength 0 without debug views is mode 1 without wrap: skip the passes
	return (qboolean)(r_skinSSSStrength->value > 0.0f || r_skinSSSDebug->integer >= SKIN_DEBUG_MASK ||
		r_skinSSSCompare->integer);
}

/*
=================
RB_RenderSkinSSS

Called by RB_RenderScreenSpaceOpaque between the opaque sort and the rest of
the main pass of a view, after the shared inputs (depth pyramid mip 0 = view
depth, MSAA resolve of the attachments) and before SSGI and SSR.
=================
*/
void RB_RenderSkinSSS( const screenViewInfo_t& info )
{
	const viewParms_t& viewParms = backEnd.viewParms;
	const int width = tr.renderFbo->width;
	const int height = tr.renderFbo->height;
	const int debugView = r_skinSSSDebug->integer;

	R_PushDebugGroup(AL_STAGE, "Skin SSS");

	const int taps = SkinTaps();
	const float widthMM = SkinWidthMM();
	SkinBuildKernel(taps);

	// pixel radius = radius (units) * pixels per unit at depth 1 / view depth
	const float radiusUnits = widthMM / SKIN_SSS_MM_PER_UNIT;
	const float pixelsPerUnit = 0.5f * viewParms.projectionMatrix[5] * viewParms.viewportHeight;

	vec4_t settings, settings2;
	VectorSet4(settings,
		radiusUnits * pixelsPerUnit,
		SKIN_SSS_MAX_PIXELS,
		Q_max(0.0f, r_skinSSSFollowSurface->value),
		(float)taps);

	image_t *source = tr.skinDiffuseImage;
	for ( int pass = 0; pass < 2; pass++ )
	{
		const int timer = RB_ScreenBeginTimer(pass == 0 ? "Skin SSS H" : "Skin SSS V");
		shaderProgram_t *sp = &tr.skinSSSShader[pass == 0 ? SKINSSSDEF_BLUR_H : SKINSSSDEF_BLUR_V];
		RB_ScreenBeginPass(tr.skinBlurFbo[pass], sp, width, height);
		RB_ScreenBindGeometry();
		GL_BindToTMU(source, TB_SHADOWMAP);
		RB_ScreenSetViewUniforms(sp, info);

		VectorSet4(settings2, radiusUnits, 0.0f, -1.0f, 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SKINSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SKINSETTINGS2, settings2);
		GLSL_SetUniformVec4N(sp, UNIFORM_SKINKERNEL, &s_kernel.kernel[0][0], SKIN_SSS_MAX_TAPS);
		RB_InstantTriangle();
		RB_ScreenEndTimer(timer);

		source = tr.skinBlurImage[pass];
	}

	// swap the sharp skin diffuse for the diffused one in the scene color
	const int timer = RB_ScreenBeginTimer("Skin SSS composite");
	{
		shaderProgram_t *sp = &tr.skinSSSShader[SKINSSSDEF_COMPOSITE];
		FBO_Bind(tr.screenCompositeFbo);
		GL_SetViewportAndScissor(viewParms.viewportX, viewParms.viewportY,
			viewParms.viewportWidth, viewParms.viewportHeight);
		GL_Cull(CT_TWO_SIDED);
		const qboolean replace = (qboolean)(debugView >= SKIN_DEBUG_MASK && debugView != SKIN_DEBUG_NOT_SKIN);
		if ( replace )
			GL_State(GLS_DEPTHTEST_DISABLE);
		else
			GL_State(GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		GLSL_BindProgram(sp);
		RB_ScreenBindGeometry();
		GL_BindToTMU(tr.skinBlurImage[1], TB_SHADOWMAP);
		GL_BindToTMU(tr.skinDiffuseImage, TB_CUBEMAP);
		GL_BindToTMU(tr.skinBlurImage[0], TB_SPECULARMAP);
		RB_ScreenSetViewUniforms(sp, info);

		const float splitX = r_skinSSSCompare->integer ?
			viewParms.viewportX + 0.5f * viewParms.viewportWidth : -1.0f;
		VectorSet4(settings2,
			radiusUnits,
			Com_Clamp(0.0f, 1.0f, r_skinSSSStrength->value),
			splitX,
			debugView >= SKIN_DEBUG_MASK ? (float)debugView : 0.0f);
		GLSL_SetUniformVec4(sp, UNIFORM_SKINSETTINGS, settings);
		GLSL_SetUniformVec4(sp, UNIFORM_SKINSETTINGS2, settings2);
		RB_InstantTriangle();
	}
	RB_ScreenEndTimer(timer);

	GL_SelectTexture(0);
}

/*
============================================================

r_skinSSSList

============================================================
*/

// r_skinSSSList [used|all|skin]: the skin decision of the lit stages
void R_SkinSSSList_f( void )
{
	const char *filter = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "used";
	const qboolean all = (qboolean)!Q_stricmp(filter, "all");
	const qboolean skinOnly = (qboolean)!Q_stricmp(filter, "skin");
	int scatter = 0, listed = 0;

	ri.Printf(PRINT_ALL, "r_skinSSS %d (built as %d), r_skinSSSMixedHeads %d\n", r_skinSSS->integer, s_mode, r_skinSSSMixedHeads->integer);
	ri.Printf(PRINT_ALL, "%-4s %-48s %6s %-20s %s\n", "used", "shader", "scatter", "reason", "diffuse / mask");
	for ( int i = 0; i < tr.numShaders; i++ )
	{
		const shader_t *sh = tr.shaders[i];
		for ( int s = 0; s < MAX_SHADER_STAGES; s++ )
		{
			const shaderStage_t *stage = sh->stages[s];
			if ( !stage || !stage->active || !IsLitStage(stage) || stage->rgbGen == CGEN_LIGHTMAPSTYLE )
				continue;
			if ( !all && !skinOnly && !stage->pbrDrawn )
				continue;
			const char *reason = stage->skinReason ? stage->skinReason : "-";
			if ( !Q_stricmp(reason, "not skin") || !Q_stricmp(reason, "not lit") )
			{
				if ( !all )
					continue;
			}
			const image_t *diffuse = stage->bundle[TB_DIFFUSEMAP].image[0];
			ri.Printf(PRINT_ALL, "%-4s %-48s %6.2f %-20s %s%s%s\n",
				stage->pbrDrawn ? "*" : "", sh->name, stage->skinScatter, reason,
				diffuse ? diffuse->imgName : "-",
				stage->skinMaskImage ? " mask " : "",
				stage->skinMaskImage ? stage->skinMaskImage->imgName : "");
			listed++;
			if ( stage->skinScatter > 0.0f )
				scatter++;
		}
	}
	ri.Printf(PRINT_ALL, "%d stages listed, %d scatter\n", listed, scatter);
}
