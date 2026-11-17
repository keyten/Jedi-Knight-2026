/*
===========================================================================
Ray traced shadows (r_sunShadowMode 2, r_dlightMode 3).

tr_rtbvh.cpp     BVH build / refit / reference traversal (no GL)
tr_rtscene.cpp   what is traced: the static world, brush models, md3 entities
                 and CPU skinned Ghoul2 models, as texture buffers
tr_rtshadows.cpp the screen passes: trace, temporal accumulation, filter
tr_ghoul2.cpp    R_RtGhoul*: surfaces, LOD and bones of a Ghoul2 entity

Included by tr_local.h.
===========================================================================
*/
#pragma once

#define RT_G2_MAX_SURFACES		512
#define RT_G2_MAX_PARTS			8
#define RT_MAX_LIGHTS			4

// one Ghoul2 model of an entity (the body, a bolted on weapon, ...)
struct rtGhoulPart_t
{
	const model_t	*model;
	int				lod;
	const mat3x4_t	*bones;			// bone cache of this frame
	int				numBones;
	uint32_t		surfaceMask[RT_G2_MAX_SURFACES / 32];	// surfaces that are on and opaque
	int				numSurfaces;
	int				numTriangles;
};

// tr_ghoul2.cpp
int		R_RtGhoulCollectParts( trRefEntity_t *ent, int entityNum, rtGhoulPart_t *parts, int maxParts );
void	R_RtGhoulExtract( const rtGhoulPart_t *part, std::vector<float> &xyz,
			std::vector<byte> &bones, std::vector<float> &weights, std::vector<int> &indexes );

// tr_rtscene.cpp
qboolean R_RtSceneWanted( void );		// a latched mode asks for the scene
void	R_RtSceneShutdown( void );
void	R_RtSceneWorldLoaded( void );
void	R_RtSceneWorldFreed( void );
qboolean R_RtSceneValid( void );		// the world is traced
qboolean R_RtSceneFrameValid( void );	// ... and the entities of the current scene were chosen
// front end, before the views of a scene are gathered: which entities are traced
void	R_RtScenePrepare( trRefdef_t *refdef );
void	R_RtSceneNoteGhoulLod( int entityNum, int modelIndex, int lod );
int		R_RtSceneGhoulLod( int entityNum, int modelIndex );
void	R_RtSceneNoteMeshLod( int entityNum, int lod );
// a caster the tracer has: left out of the residual sun layers
qboolean R_RtSceneSurfaceTraced( surfaceType_t *surface, int entityNum, const shader_t *shader,
			foliageResult_t foliage, uint32_t waterKey );
qboolean R_RtShaderTraced( const shader_t *shader );
// back end: skin, refit, build the top level, upload (once per scene); then bind
qboolean RB_RtSceneUpdate( void );
void	RB_RtSceneBind( int staticTmu, int dynamicTmu, vec4_t sceneParams );
void	R_RtSceneStats_f( void );

// tr_rtshadows.cpp
qboolean R_RtShadowsSunEnabled( void );		// r_sunShadowMode 2 with its resources
qboolean R_RtShadowsLightsEnabled( void );	// r_dlightMode 3 with its resources
qboolean R_RtShadowsSunActive( void );		// ... and not compared off
qboolean R_RtShadowsLightsActive( void );
qboolean R_RtShadowsResidualActive( void );	// sun: untraced casters in layers 3-5
void	R_RtShadowsSelectResources( void );
void	R_CreateRtShadowImages( int width, int height );
void	R_CreateRtShadowFBOs( void );
// camera block of a view: z = traced light slots, w = sun (0 cascades, 1 residual layers, 2 traced only)
void	R_RtShadowsViewInfo( const viewParms_t *viewParms, const trRefdef_t *refdef, float *lightSlots, float *sunMode );
void	R_RtShadowsAssignLights( const trRefdef_t *refdef );
int		R_RtShadowsLightChannel( int dlightIndex );	// channel of a traced light, -1: shadow map
void	RB_RenderRtShadows( void );			// after the depth prepass of a view
image_t	*RB_RtShadowsSunImage( void );		// this view's sun visibility, NULL none
image_t	*RB_RtShadowsLightImage( void );	// this view's light visibility (rgba = slots), NULL none
void	R_RtShadowsInvalidate( void );
void	RB_RtShadowsDebugOverlay( void );
