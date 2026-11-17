/*
===========================================================================
Ray traced shadows: the screen passes (see tr_rtshadows.h).

r_sunShadowMode 2   the sun. One ray per pixel after the depth prepass; the
                    result is multiplied into the contact shadow channel of
                    the screen AO map, which lightall already reads. lightall
                    then takes the cascades from the residual layers 3-5
                    (casters the tracer does not have), the camera block
                    tells it so per view.
r_dlightMode 3      up to four shadowed dynamic lights, one channel each;
                    lightall reads the channel instead of the shadow cube.

Both keep the shadow maps: fog, water, portal views and every other reader
of the cascades / cubes see them as before.

Chain per kind:  trace (r_rtShadowsHalfRes: half resolution, rotating phase)
                 -> resolve (upsample + temporal accumulation, r_rtShadowsSoft)
                 -> two a-trous steps (r_rtShadowsSoft)
===========================================================================
*/
#include "tr_local.h"

#define RT_CHAIN_SUN		0
#define RT_CHAIN_LIGHTS		1
#define RT_NUM_CHAINS		2

struct rtChain_t
{
	image_t			*traceImage;
	image_t			*historyImage[2];
	image_t			*historyDepthImage[2];
	image_t			*filterImage[2];
	FBO_t			*traceFbo;
	FBO_t			*historyFbo[2];
	FBO_t			*filterFbo[2];
	screenHistory_t	history;
	image_t			*result;
	unsigned		readyFrame;
	int				readyView;
	int				readyScene;
};

struct rtLight_t
{
	vec3_t	origin;
	float	radius;
	vec4_t	spot;
};

static qboolean		s_sunResources = qfalse;
static qboolean		s_lightResources = qfalse;
static rtChain_t	s_chains[RT_NUM_CHAINS];
static int			s_traceDivisor = 1;

// lights of the scene whose constants were built last (front end), and of the frame before
static rtLight_t	s_lights[RT_MAX_LIGHTS];
static int			s_numLights = 0;
static rtLight_t	s_previousLights[RT_MAX_LIGHTS];
static int			s_numPreviousLights = 0;

static image_t		*s_debugImage = NULL;

void R_RtShadowsInvalidate( void )
{
	for ( int c = 0; c < RT_NUM_CHAINS; c++ )
	{
		s_chains[c].history.valid = qfalse;
		s_chains[c].result = NULL;
		s_chains[c].readyFrame = ~0u;
		s_chains[c].readyView = s_chains[c].readyScene = -1;
	}
	s_numPreviousLights = 0;
	s_debugImage = NULL;
}

void R_RtShadowsSelectResources( void )
{
	const qboolean buffers = (qboolean)( qglTexBuffer != NULL && glRefConfig.maxTextureBufferSize >= ( 1 << 16 ) );
	s_sunResources = (qboolean)( r_sunShadowMode->integer == 2 && r_sunlightMode->integer &&
		r_depthPrepass->integer && buffers );
	s_lightResources = (qboolean)( r_dlightMode->integer == 3 && r_depthPrepass->integer && buffers );

	if ( r_sunShadowMode->integer == 2 && !s_sunResources )
		ri.Printf( PRINT_WARNING, "r_sunShadowMode 2: ray traced sun shadows need r_sunlightMode, r_depthPrepass and buffer textures; the stabilized shadow maps (mode 1) are used.\n" );
	if ( r_dlightMode->integer == 3 && !s_lightResources )
		ri.Printf( PRINT_WARNING, "r_dlightMode 3: ray traced light shadows need r_depthPrepass and buffer textures; the shadow cubes (mode 2) are used.\n" );
	if ( s_lightResources && r_ltcAreaLights->integer && r_ltcSaberShadows->integer == 2 )
		ri.Printf( PRINT_ALL, "r_dlightMode 3: sabers keep their shadow cube; the screen space saber shadows (r_ltcSaberShadows 2) give their texture unit to the traced lights.\n" );

	s_traceDivisor = r_rtShadowsHalfRes->integer ? 2 : 1;
	R_RtShadowsInvalidate();
}

qboolean R_RtShadowsSunEnabled( void )
{
	return s_sunResources;
}

qboolean R_RtShadowsLightsEnabled( void )
{
	return s_lightResources;
}

qboolean R_RtSceneWanted( void )
{
	return (qboolean)( s_sunResources || s_lightResources );
}

qboolean R_RtShadowsSunActive( void )
{
	return (qboolean)( s_sunResources && s_chains[RT_CHAIN_SUN].traceFbo && tr.rtShadowShader[0].program &&
		!R_CompareOff( RC_RTSHADOWS ) );
}

qboolean R_RtShadowsLightsActive( void )
{
	return (qboolean)( s_lightResources && s_chains[RT_CHAIN_LIGHTS].traceFbo && tr.rtShadowShader[1].program &&
		!R_CompareOff( RC_RTLIGHTSHADOWS ) );
}

// the casters the tracer does not have are drawn into the sun layers 3-5 of this scene
qboolean R_RtShadowsResidualActive( void )
{
	return (qboolean)( R_RtShadowsSunActive() && r_rtShadowsResidual->integer &&
		tr.sunShadowHardFbo[0] && R_RtSceneFrameValid() );
}

/*
============================================================

Resources

============================================================
*/

void R_CreateRtShadowImages( int width, int height )
{
	Com_Memset( s_chains, 0, sizeof( s_chains ) );
	R_RtShadowsInvalidate();

	for ( int c = 0; c < RT_NUM_CHAINS; c++ )
	{
		if ( !( c == RT_CHAIN_SUN ? s_sunResources : s_lightResources ) )
			continue;
		rtChain_t *chain = &s_chains[c];
		const int format = c == RT_CHAIN_SUN ? GL_R8 : GL_RGBA8;
		const char *name = c == RT_CHAIN_SUN ? "Sun" : "Light";
		chain->traceImage = R_ScreenCreateImage( va( "*rt%sTrace", name ),
			Q_max( 1, width / s_traceDivisor ), Q_max( 1, height / s_traceDivisor ), format, qfalse );
		for ( int i = 0; i < 2; i++ )
		{
			// bilinear: the history is read at reprojected positions
			chain->historyImage[i] = R_ScreenCreateImage( va( "*rt%sHistory%d", name, i ), width, height, format, qtrue );
			chain->historyDepthImage[i] = R_ScreenCreateImage( va( "*rt%sHistoryDepth%d", name, i ), width, height, GL_R32F, qfalse );
			chain->filterImage[i] = R_ScreenCreateImage( va( "*rt%sFilter%d", name, i ), width, height, format, qfalse );
		}
	}
	GL_SelectTexture( 0 );
}

void R_CreateRtShadowFBOs( void )
{
	for ( int c = 0; c < RT_NUM_CHAINS; c++ )
	{
		rtChain_t *chain = &s_chains[c];
		chain->traceFbo = NULL;
		if ( !chain->traceImage )
			continue;
		const char *name = c == RT_CHAIN_SUN ? "Sun" : "Light";
		chain->traceFbo = R_ScreenCreateLevelFBO( va( "_rt%sTrace", name ), chain->traceImage, 0 );
		for ( int i = 0; i < 2; i++ )
		{
			chain->historyFbo[i] = R_ScreenCreatePairFBO( va( "_rt%sHistory%d", name, i ),
				chain->historyImage[i], chain->historyDepthImage[i] );
			chain->filterFbo[i] = R_ScreenCreateLevelFBO( va( "_rt%sFilter%d", name, i ), chain->filterImage[i], 0 );
		}
	}
}

/*
============================================================

Front end

============================================================
*/

// a view the passes run in: a perspective view of the world into renderFbo
static qboolean R_RtShadowsViewEligible( const viewParms_t *viewParms, const trRefdef_t *refdef )
{
	if ( viewParms->viewParmType != VPT_MAIN || viewParms->isPortal || viewParms->isSkyPortal )
		return qfalse;
	if ( viewParms->flags & VPF_DEPTHSHADOW )
		return qfalse;
	if ( viewParms->targetFbo != NULL && viewParms->targetFbo != tr.renderFbo )
		return qfalse;
	if ( refdef->rdflags & RDF_NOWORLDMODEL )
		return qfalse;
	if ( !r_depthPrepass->integer || !R_RtSceneFrameValid() )
		return qfalse;
	return qtrue;
}

static qboolean R_RtShadowsViewSun( const viewParms_t *viewParms, const trRefdef_t *refdef )
{
	return (qboolean)( R_RtShadowsSunActive() && ( viewParms->flags & VPF_USESUNLIGHT ) &&
		R_RtShadowsViewEligible( viewParms, refdef ) );
}

static qboolean R_RtShadowsViewLights( const viewParms_t *viewParms, const trRefdef_t *refdef )
{
	return (qboolean)( R_RtShadowsLightsActive() && s_numLights > 0 &&
		R_RtShadowsViewEligible( viewParms, refdef ) );
}

void R_RtShadowsViewInfo( const viewParms_t *viewParms, const trRefdef_t *refdef, float *lightSlots, float *sunMode )
{
	*lightSlots = R_RtShadowsViewLights( viewParms, refdef ) ? (float)s_numLights : 0.0f;
	*sunMode = 0.0f;
	if ( R_RtShadowsViewSun( viewParms, refdef ) )
		*sunMode = R_RtShadowsResidualActive() ? 1.0f : 2.0f;
}

// r_dlightMode 3: the shadowed point / spot lights of the scene that are traced, most important
// first (the order of the lights block). Front end, before the constants of the scene are built.
#define RT_LIGHT_TABLE	1024
static signed char s_lightChannel[RT_LIGHT_TABLE];

void R_RtShadowsAssignLights( const trRefdef_t *refdef )
{
	s_numLights = 0;
	Com_Memset( s_lightChannel, -1, sizeof( s_lightChannel ) );
	if ( !R_RtShadowsLightsActive() || !R_RtSceneFrameValid() || ( refdef->rdflags & RDF_NOWORLDMODEL ) )
		return;

	const int maxLights = Com_Clampi( 0, RT_MAX_LIGHTS, r_rtShadowsMaxLights->integer );
	int lightIndexes[MAX_DLIGHTS];
	int shadowLayers[MAX_DLIGHTS];
	const int numLights = R_GetUboDlights( refdef, lightIndexes, shadowLayers );
	for ( int i = 0; i < numLights && s_numLights < maxLights; i++ )
	{
		const int index = lightIndexes[i];
		if ( index < 0 || index >= RT_LIGHT_TABLE )
			continue;
		const dlight_t *dlight = refdef->dlights + index;
		// a light without a shadow map casts no shadow in mode 2 either
		if ( shadowLayers[i] < 0 || !R_DlightCastsShadow( dlight ) || dlight->areaType != DLIGHT_POINT )
			continue;
		rtLight_t *light = &s_lights[s_numLights];
		VectorCopy( dlight->origin, light->origin );
		light->radius = dlight->radius;
		VectorSet4( light->spot, dlight->spotDir[0], dlight->spotDir[1], dlight->spotDir[2],
			dlight->spotCosOuter > -1.0f ? dlight->spotCosOuter : -2.0f );
		s_lightChannel[index] = (signed char)s_numLights++;
	}
}

// channel of the traced visibility of a dynamic light of the scene, -1: it reads its shadow map
int R_RtShadowsLightChannel( int dlightIndex )
{
	if ( dlightIndex < 0 || dlightIndex >= RT_LIGHT_TABLE )
		return -1;
	return s_lightChannel[dlightIndex];
}

/*
============================================================

Passes

============================================================
*/

static void RB_RtSetCommon( shaderProgram_t *sp, const screenViewInfo_t& info, const vec4_t *params,
	const vec4_t texelSize, const vec4_t historyWeights )
{
	RB_ScreenSetViewUniforms( sp, info );
	GLSL_SetUniformVec4( sp, UNIFORM_SSRTEXELSIZE, texelSize );
	GLSL_SetUniformVec4N( sp, UNIFORM_RTPARAMS, (const float *)params, 4 );
	GLSL_SetUniformVec4( sp, UNIFORM_RTHISTORY, historyWeights );
	GL_BindToTMU( tr.renderDepthImage, TB_COLORMAP );
}

static void RB_RtRunChain( int c, const screenViewInfo_t& info )
{
	rtChain_t *chain = &s_chains[c];
	const int width = tr.renderFbo->width;
	const int height = tr.renderFbo->height;
	const qboolean soft = (qboolean)( r_rtShadowsSoft->integer != 0 );
	const int debugView = r_rtShadowsDebug->integer;
	const unsigned frame = backEndData->realFrameNumber;

	vec4_t params[4];

	// [0] the sun
	const float *sunDir = backEnd.refdef.sunDir;
	const float angularRadius = Com_Clamp( 0.0f, 8.0f, r_shadowSunAngularDiameter->value ) * (float)M_PI / 360.0f;
	VectorSet4( params[0], sunDir[0], sunDir[1], sunDir[2], soft ? tanf( angularRadius ) : 0.0f );

	// [1] bias: a floor in world units, plus the footprint and the depth buffer error of the pixel
	const float bias = Com_Clamp( 0.0f, 16.0f, r_rtShadowsBias->value );
	VectorSet4( params[1], 0.1f * bias, bias * ( 1.5f * info.depthParams[3] + 3.0e-4f ), 131072.0f,
		(float)( frame & 63 ) );

	// [2] the pixel a trace texel stands for
	int phaseX = 0, phaseY = 0;
	if ( s_traceDivisor > 1 && soft )
	{
		phaseX = frame & 1;
		phaseY = ( frame >> 1 ) & 1;
	}
	VectorSet4( params[2], (float)s_traceDivisor, (float)phaseX, (float)phaseY, (float)debugView );

	// [3] lights
	const qboolean historyValid = (qboolean)( soft && RB_ScreenHistoryValid( chain->history, 1.0f / s_traceDivisor ) );
	const float lightRadius = soft ? Com_Clamp( 0.0f, 64.0f, r_rtShadowsLightRadius->value ) : 0.0f;
	VectorSet4( params[3], (float)s_numLights, lightRadius, 0.0f,
		( historyValid && RB_ScreenVelocityValid() ) ? 1.0f : 0.0f );

	vec4_t historyWeights = { 0.0f, 0.0f, 0.0f, 0.0f };
	if ( historyValid )
	{
		if ( c == RT_CHAIN_SUN )
		{
			VectorSet4( historyWeights, 0.9f, 0.9f, 0.9f, 0.9f );
		}
		else
		{
			// a channel keeps its history while the same light stays in it
			for ( int i = 0; i < s_numLights && i < s_numPreviousLights; i++ )
			{
				const float move = Distance( s_lights[i].origin, s_previousLights[i].origin );
				if ( move < 0.25f * s_lights[i].radius && fabsf( s_lights[i].radius - s_previousLights[i].radius ) < 0.25f * s_lights[i].radius )
					historyWeights[i] = 0.9f * expf( -move / 16.0f );
			}
		}
	}

	vec4_t texelSize;
	RB_ScreenTexelSize( texelSize, width, height, chain->traceFbo->width, chain->traceFbo->height );

	vec4_t lights[RT_MAX_LIGHTS * 2];
	Com_Memset( lights, 0, sizeof( lights ) );
	for ( int i = 0; i < s_numLights; i++ )
	{
		VectorSet4( lights[i * 2], s_lights[i].origin[0], s_lights[i].origin[1], s_lights[i].origin[2], s_lights[i].radius );
		VectorCopy4( s_lights[i].spot, lights[i * 2 + 1] );
	}

	// trace
	int timer = RB_ScreenBeginTimer( c == RT_CHAIN_SUN ? "RT sun shadow trace" : "RT light shadow trace" );
	shaderProgram_t *sp = &tr.rtShadowShader[c];
	RB_ScreenBeginPass( chain->traceFbo, sp, chain->traceFbo->width, chain->traceFbo->height );
	RB_RtSetCommon( sp, info, params, texelSize, historyWeights );
	vec4_t sceneParams;
	RB_RtSceneBind( TB_SHADOWMAP, TB_SHADOWMAPARRAY, sceneParams );
	GLSL_SetUniformVec4( sp, UNIFORM_RTSCENE, sceneParams );
	GLSL_SetUniformVec4N( sp, UNIFORM_RTLIGHTS, (const float *)lights, RT_MAX_LIGHTS * 2 );
	GLSL_SetUniformMatrix4x4( sp, UNIFORM_RTVIEWTOWORLD, info.viewToWorld, 1 );
	RB_InstantTriangle();
	RB_ScreenEndTimer( timer );

	chain->result = chain->traceImage;
	const qboolean rawDebug = (qboolean)( debugView >= 3 );

	// upsample and accumulate
	if ( s_traceDivisor > 1 || soft )
	{
		const int previous = chain->history.current & 1;
		const int current = previous ^ 1;
		matrix_t reproject;
		Matrix16Multiply( historyValid ? chain->history.viewProjection : info.viewProjection, info.viewToWorld, reproject );

		timer = RB_ScreenBeginTimer( c == RT_CHAIN_SUN ? "RT sun shadow resolve" : "RT light shadow resolve" );
		sp = &tr.rtShadowShader[2];
		RB_ScreenBeginPass( chain->historyFbo[current], sp, width, height );
		RB_RtSetCommon( sp, info, params, texelSize, historyWeights );
		GL_BindToTMU( chain->traceImage, TB_SHADOWMAP );
		GL_BindToTMU( chain->historyImage[previous], TB_CUBEMAP );
		GL_BindToTMU( chain->historyDepthImage[previous], TB_ENVBRDFMAP );
		GL_BindToTMU( RB_ScreenVelocityValid() ? tr.velocityImage : tr.whiteImage, TB_SSAOMAP );
		GLSL_SetUniformMatrix4x4( sp, UNIFORM_SSRREPROJECT, reproject, 1 );
		RB_InstantTriangle();
		RB_ScreenEndTimer( timer );

		RB_ScreenStoreHistory( chain->history, info, 1.0f / s_traceDivisor, current );
		chain->result = chain->historyImage[current];

		if ( soft && !rawDebug )
		{
			timer = RB_ScreenBeginTimer( c == RT_CHAIN_SUN ? "RT sun shadow filter" : "RT light shadow filter" );
			sp = &tr.rtShadowShader[3];
			for ( int pass = 0; pass < 2; pass++ )
			{
				RB_ScreenBeginPass( chain->filterFbo[pass], sp, width, height );
				vec4_t filterParams[4];
				Com_Memcpy( filterParams, params, sizeof( filterParams ) );
				filterParams[2][0] = (float)( 1 << pass );
				RB_RtSetCommon( sp, info, filterParams, texelSize, historyWeights );
				GL_BindToTMU( pass == 0 ? chain->historyImage[current] : chain->filterImage[0], TB_SHADOWMAP );
				RB_InstantTriangle();
			}
			RB_ScreenEndTimer( timer );
			chain->result = chain->filterImage[1];
		}
	}
	else
	{
		chain->history.valid = qfalse;
	}

	chain->readyFrame = frame;
	chain->readyView = backEnd.viewParms.currentViewParm;
	chain->readyScene = backEndData->currentFrame->currentScene;
}

static qboolean RB_RtChainReady( const rtChain_t *chain )
{
	return (qboolean)( chain->result &&
		chain->readyFrame == backEndData->realFrameNumber &&
		chain->readyScene == backEndData->currentFrame->currentScene &&
		chain->readyView == backEnd.viewParms.currentViewParm );
}

image_t *RB_RtShadowsSunImage( void )
{
	return RB_RtChainReady( &s_chains[RT_CHAIN_SUN] ) ? s_chains[RT_CHAIN_SUN].result : NULL;
}

image_t *RB_RtShadowsLightImage( void )
{
	return RB_RtChainReady( &s_chains[RT_CHAIN_LIGHTS] ) ? s_chains[RT_CHAIN_LIGHTS].result : NULL;
}

/*
=================
RB_RenderRtShadows

After the depth prepass of a view, before the screen AO composite (which
takes the sun result) and the main pass (which takes the light result).
=================
*/
void RB_RenderRtShadows( void )
{
	const qboolean sun = R_RtShadowsViewSun( &backEnd.viewParms, &backEnd.refdef );
	const qboolean lights = R_RtShadowsViewLights( &backEnd.viewParms, &backEnd.refdef );
	if ( !sun && !lights )
		return;
	if ( !RB_RtSceneUpdate() )
		return;

	FBO_t *oldFbo = glState.currentFBO;
	if ( tr.msaaResolveFbo )
		FBO_FastBlit( tr.renderFbo, NULL, tr.msaaResolveFbo, NULL, GL_DEPTH_BUFFER_BIT, GL_NEAREST );

	screenViewInfo_t info;
	RB_ScreenGetViewInfo( info );

	if ( sun )
		RB_RtRunChain( RT_CHAIN_SUN, info );
	if ( lights )
	{
		RB_RtRunChain( RT_CHAIN_LIGHTS, info );
		Com_Memcpy( s_previousLights, s_lights, sizeof( s_previousLights ) );
		s_numPreviousLights = s_numLights;
	}

	const int debugView = r_rtShadowsDebug->integer;
	s_debugImage = NULL;
	if ( debugView == 2 && lights )
		s_debugImage = s_chains[RT_CHAIN_LIGHTS].result;
	else if ( debugView >= 1 )
		s_debugImage = sun ? s_chains[RT_CHAIN_SUN].result : ( lights ? s_chains[RT_CHAIN_LIGHTS].result : NULL );

	FBO_Bind( oldFbo );
	GL_SetViewportAndScissor( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	GL_SelectTexture( 0 );
}

// r_rtShadowsDebug: 1 sun visibility, 2 light visibility (rgba = channels), 3 this frame's rays,
// 4 BVH nodes visited per pixel (white = 256)
void RB_RtShadowsDebugOverlay( void )
{
	if ( r_rtShadowsDebug->integer <= 0 || !s_debugImage )
		return;
	FBO_FastBlitFromTexture( s_debugImage, NULL, NULL, NULL, 0 );
	s_debugImage = NULL;
}
