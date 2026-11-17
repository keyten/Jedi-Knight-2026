/*
===========================================================================
Ray traced shadows: the traced scene (see tr_rtshadows.h).

Two RGBA32F texture buffers, so the mode needs no more than the GL 3.2
context:

  static   texel 0: the world BVH (nodes, then triangles), built once per map;
           behind it the bottom level trees of brush models and md3 frames,
           appended when an entity first uses them
  dynamic  rewritten once per scene:
             [0, RT_TLAS_TEXELS)                 top level nodes over the instances
             [RT_TLAS_TEXELS, RT_HEADER_TEXELS)  instances, 4 texels each: rows of
                                                 world -> local, then node texel,
                                                 triangle texel, node count, buffer
             [RT_HEADER_TEXELS, ...)             Ghoul2 models skinned on the CPU:
                                                 refitted nodes and triangles

Three kinds of geometry, one traversal:
  world        static, world space, traced first without an instance
  rigid        brush models and md3 entities: a static tree in model space,
               the entity transform is the instance
  Ghoul2       the tree is built once over the bind pose of a model / LOD /
               surface set; every frame the vertices are skinned with the bone
               matrices the vertex shader gets, and the boxes are refitted

Whatever is not in here (cutouts, deforming shaders, plants, characters over
the budget) keeps casting through the residual sun shadow layers.
===========================================================================
*/
#include "tr_local.h"
#include "tr_rtbvh.h"
#ifndef REND2_SP
#include "ghoul2/g2_local.h"
#endif

#include <algorithm>
#include <map>

#define RT_MAX_INSTANCES		1024
#define RT_TLAS_TEXELS			( RT_MAX_INSTANCES * 2 * 2 )	// < 2 nodes per instance, 2 texels each
#define RT_HEADER_TEXELS		( RT_TLAS_TEXELS + RT_MAX_INSTANCES * 4 )
#define RT_STATIC_RESERVE		( 1 << 20 )		// texels kept for rigid trees (16 MB)
#define RT_DYNAMIC_TEXELS		( 1 << 20 )		// 16 MB
#define RT_MAX_GHOUL_TEMPLATES	96
#define RT_MAX_RIGID_TRIS		200000

#define RT_TRACED_NONE			0
#define RT_TRACED_RIGID			1
#define RT_TRACED_GHOUL			2

struct rtBlas_t
{
	int		nodeTexel;
	int		triTexel;
	int		numNodes;
	int		numTris;
	vec3_t	mins, maxs;
};

struct rtRigidKey_t
{
	const void	*model;		// bmodel_t or mdvModel_t
	int			frame;
	int			customShader;
	int			customSkin;
	int			skinNum;

	bool operator<( const rtRigidKey_t &o ) const
	{
		if ( model != o.model ) return model < o.model;
		if ( frame != o.frame ) return frame < o.frame;
		if ( customShader != o.customShader ) return customShader < o.customShader;
		if ( customSkin != o.customSkin ) return customSkin < o.customSkin;
		return skinNum < o.skinNum;
	}
};

// a Ghoul2 model / LOD / surface set: skinning source and bind pose tree
struct rtGhoulTemplate_t
{
	const void				*header;
	int						lod;
	uint32_t				surfaceMask[RT_G2_MAX_SURFACES / 32];
	std::vector<float>		xyz;
	std::vector<byte>		bones;
	std::vector<float>		weights;
	std::vector<int>		slotIndexes;	// 3 vertex indexes per triangle slot
	std::vector<rtNode_t>	nodes;
	unsigned				lastUsed;
};

struct rtInstance_t
{
	float	worldToLocal[12];	// rows
	float	mins[3], maxs[3];	// world
	float	nodeTexel, triTexel, numNodes, dynamic;
};

struct rtGhoulJob_t
{
	rtGhoulPart_t		part;
	rtGhoulTemplate_t	*tmpl;
	int					nodeTexel;
	int					triTexel;
	int					instance;
	float				localToWorld[12];
	std::vector<float>	skinned;
};

static struct
{
	qboolean	created;
	qboolean	failed;
	GLuint		staticBuffer;
	GLuint		dynamicBuffer;
	image_t		staticImage;
	image_t		dynamicImage;
	int			staticCapacity;		// texels
	int			staticUsed;
	int			dynamicCapacity;

	qboolean	worldValid;
	rtBlas_t	world;
	std::map<rtRigidKey_t, rtBlas_t>	*rigid;
	std::vector<rtGhoulTemplate_t *>	*ghoulTemplates;

	// scene
	std::vector<byte>			*entityTraced;
	std::vector<signed char>	*ghoulLod;		// 4 models per entity
	std::vector<signed char>	*meshLod;
	std::vector<float>			*dynamicData;
	std::vector<rtInstance_t>	*instances;
	std::vector<rtGhoulJob_t>	*ghoulJobs;
	qboolean	sceneValid;			// R_RtScenePrepare ran for a world scene
	unsigned	updatedFrame;
	int			updatedScene;
	qboolean	updateOk;
	int			tlasNodes;

	// stats of the last update
	int			statInstances;
	int			statGhoulParts;
	int			statGhoulTris;
	int			statDropped;
	int			statDynamicTexels;
	float		statUpdateMs;
	float		statWorldBuildMs;
} s_rt;

/*
============================================================

Buffers

============================================================
*/

static void R_RtSceneAllocContainers( void )
{
	if ( s_rt.rigid )
		return;
	s_rt.rigid = new std::map<rtRigidKey_t, rtBlas_t>();
	s_rt.ghoulTemplates = new std::vector<rtGhoulTemplate_t *>();
	s_rt.entityTraced = new std::vector<byte>();
	s_rt.ghoulLod = new std::vector<signed char>();
	s_rt.meshLod = new std::vector<signed char>();
	s_rt.dynamicData = new std::vector<float>();
	s_rt.instances = new std::vector<rtInstance_t>();
	s_rt.ghoulJobs = new std::vector<rtGhoulJob_t>();
}

static void R_RtSceneClearCaches( void )
{
	if ( !s_rt.rigid )
		return;
	s_rt.rigid->clear();
	for ( size_t i = 0; i < s_rt.ghoulTemplates->size(); i++ )
		delete ( *s_rt.ghoulTemplates )[i];
	s_rt.ghoulTemplates->clear();
}

static void R_RtSceneCreateTexture( image_t *image, const char *name, GLuint buffer )
{
	Com_Memset( image, 0, sizeof( *image ) );
	Q_strncpyz( image->imgName, name, sizeof( image->imgName ) );
	image->flags = IMGFLAG_TEXBUFFER;
	qglGenTextures( 1, &image->texnum );
	GL_BindToTMU( image, TB_COLORMAP );
	qglTexBuffer( GL_TEXTURE_BUFFER, GL_RGBA32F, buffer );
}

static void R_RtSceneDeleteBuffers( void )
{
	if ( s_rt.staticImage.texnum )
		qglDeleteTextures( 1, &s_rt.staticImage.texnum );
	if ( s_rt.dynamicImage.texnum )
		qglDeleteTextures( 1, &s_rt.dynamicImage.texnum );
	if ( s_rt.staticBuffer )
		qglDeleteBuffers( 1, &s_rt.staticBuffer );
	if ( s_rt.dynamicBuffer )
		qglDeleteBuffers( 1, &s_rt.dynamicBuffer );
	Com_Memset( &s_rt.staticImage, 0, sizeof( s_rt.staticImage ) );
	Com_Memset( &s_rt.dynamicImage, 0, sizeof( s_rt.dynamicImage ) );
	s_rt.staticBuffer = s_rt.dynamicBuffer = 0;
	s_rt.created = qfalse;
}

void R_RtSceneShutdown( void )
{
	R_RtSceneDeleteBuffers();
	R_RtSceneClearCaches();
	s_rt.worldValid = qfalse;
	s_rt.sceneValid = qfalse;
}

void R_RtSceneWorldFreed( void )
{
	R_RtSceneDeleteBuffers();
	R_RtSceneClearCaches();
	s_rt.worldValid = qfalse;
	s_rt.sceneValid = qfalse;
	s_rt.updatedFrame = ~0u;
}

qboolean R_RtSceneValid( void )
{
	return ( s_rt.created && s_rt.worldValid ) ? qtrue : qfalse;
}

qboolean R_RtSceneFrameValid( void )
{
	return ( R_RtSceneValid() && s_rt.sceneValid ) ? qtrue : qfalse;
}

/*
============================================================

What is traced

============================================================
*/

// The shader half of R_IsStaticSunShadowCaster: opaque, rigid, where it is drawn.
qboolean R_RtShaderTraced( const shader_t *shader )
{
	if ( !shader )
		return qfalse;
	if ( shader->sort != SS_OPAQUE ||
		shader->depthPrepass != DEPTHPREPASS_SIMPLE ||
		shader->numDeforms != 0 ||
		shader->alphaShadow ||
		shader->foliageHint != FOLIAGE_NONE ||
		shader->numSurfaceSpriteStages != 0 ||
		shader->pomSilhouette ||
		shader->useDistortion ||
		shader->polygonOffset ||
		( shader->surfaceFlags & SURF_FORCESIGHT ) )
	{
		return qfalse;
	}
	return qtrue;
}

qboolean R_RtSceneSurfaceTraced( surfaceType_t *surface, int entityNum, const shader_t *shader,
	foliageResult_t foliage, uint32_t waterKey )
{
	if ( !s_rt.sceneValid )
		return qfalse;
	if ( entityNum == REFENTITYNUM_WORLD )
		return R_IsStaticSunShadowCaster( surface, entityNum, shader, foliage, waterKey );
	if ( entityNum < 0 || entityNum >= (int)s_rt.entityTraced->size() )
		return qfalse;

	switch ( ( *s_rt.entityTraced )[entityNum] )
	{
	case RT_TRACED_RIGID:
		return ( R_RtShaderTraced( shader ) && foliage.cls == FOLIAGE_NONE && waterKey == 0 ) ? qtrue : qfalse;
	case RT_TRACED_GHOUL:
		return ( shader && shader->sort == SS_OPAQUE ) ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

static void R_RtAppendBspSurface( const msurface_t *surf, std::vector<float> &positions )
{
	const surfaceType_t type = *surf->data;
	if ( type != SF_FACE && type != SF_GRID && type != SF_TRIANGLES )
		return;
	const srfBspSurface_t *bsp = (const srfBspSurface_t *)surf->data;
	if ( !bsp->verts || !bsp->indexes || bsp->numVerts <= 0 )
		return;
	for ( int i = 0; i + 2 < bsp->numIndexes; i += 3 )
	{
		const glIndex_t a = bsp->indexes[i], b = bsp->indexes[i + 1], c = bsp->indexes[i + 2];
		if ( a >= (glIndex_t)bsp->numVerts || b >= (glIndex_t)bsp->numVerts || c >= (glIndex_t)bsp->numVerts )
			continue;
		const float *va = bsp->verts[a].xyz, *vb = bsp->verts[b].xyz, *vc = bsp->verts[c].xyz;
		positions.insert( positions.end(), va, va + 3 );
		positions.insert( positions.end(), vb, vb + 3 );
		positions.insert( positions.end(), vc, vc + 3 );
	}
}

// nodes, then triangles, as texels
static void R_RtFlatten( const rtBvh_t &bvh, const float *positions, std::vector<float> &out, rtBlas_t *blas, int baseTexel )
{
	const int numNodes = (int)bvh.nodes.size();
	const int numTris = (int)bvh.order.size();
	const size_t start = out.size();
	out.resize( start + (size_t)numNodes * 8 + (size_t)numTris * 12 );
	Com_Memcpy( &out[start], bvh.nodes.data(), (size_t)numNodes * sizeof( rtNode_t ) );
	if ( numTris > 0 )
		RtBvh_PackTriangles( &bvh, positions, (rtTri_t *)&out[start + (size_t)numNodes * 8] );

	blas->nodeTexel = baseTexel;
	blas->triTexel = baseTexel + numNodes * 2;
	blas->numNodes = numNodes;
	blas->numTris = numTris;
	RtBvh_Bounds( &bvh, blas->mins, blas->maxs );
}

void R_RtSceneWorldLoaded( void )
{
	R_RtSceneAllocContainers();
	R_RtSceneWorldFreed();
	if ( !R_RtSceneWanted() || !tr.world || s_rt.failed )
		return;

	const int maxTexels = glRefConfig.maxTextureBufferSize;
	if ( !qglTexBuffer || maxTexels < RT_HEADER_TEXELS * 2 )
	{
		s_rt.failed = qtrue;
		ri.Printf( PRINT_WARNING, "Ray traced shadows: buffer textures unavailable (max %d texels), shadow maps are used.\n", maxTexels );
		return;
	}

	const int start = ri.Milliseconds();
	world_t *world = tr.world;
	std::vector<float> positions;
	int numSurfaces = 0;
	for ( int i = 0; i < world->numWorldSurfaces; i++ )
	{
		const msurface_t *surf = &world->surfaces[i];
		if ( !surf->data || !surf->shader )
			continue;
		const shader_t *shader = surf->shader->remappedShader ? surf->shader->remappedShader : surf->shader;
		if ( !R_IsStaticSunShadowCaster( surf->data, REFENTITYNUM_WORLD, shader, {}, surf->waterKey ) )
			continue;
		R_RtAppendBspSurface( surf, positions );
		numSurfaces++;
	}

	const int numTris = (int)( positions.size() / 9 );
	rtBvh_t bvh;
	if ( !RtBvh_Build( positions.data(), numTris, &bvh ) )
	{
		ri.Printf( PRINT_WARNING, "Ray traced shadows: the world (%d triangles) does not fit the BVH encoding, shadow maps are used.\n", numTris );
		return;
	}

	std::vector<float> data;
	R_RtFlatten( bvh, positions.data(), data, &s_rt.world, 0 );
	const int worldTexels = (int)( data.size() / 4 );
	if ( worldTexels > maxTexels )
	{
		ri.Printf( PRINT_WARNING, "Ray traced shadows: the world needs %d texels, the buffer texture limit is %d; shadow maps are used.\n",
			worldTexels, maxTexels );
		return;
	}

	s_rt.staticCapacity = (int)Q_min( (int64_t)maxTexels, (int64_t)worldTexels + RT_STATIC_RESERVE );
	s_rt.staticUsed = worldTexels;
	s_rt.dynamicCapacity = Q_min( maxTexels, RT_DYNAMIC_TEXELS );

	qglGenBuffers( 1, &s_rt.staticBuffer );
	qglBindBuffer( GL_TEXTURE_BUFFER, s_rt.staticBuffer );
	qglBufferData( GL_TEXTURE_BUFFER, (GLsizeiptr)s_rt.staticCapacity * 16, NULL, GL_STATIC_DRAW );
	qglBufferSubData( GL_TEXTURE_BUFFER, 0, (GLsizeiptr)worldTexels * 16, data.data() );
	qglGenBuffers( 1, &s_rt.dynamicBuffer );
	qglBindBuffer( GL_TEXTURE_BUFFER, s_rt.dynamicBuffer );
	qglBufferData( GL_TEXTURE_BUFFER, (GLsizeiptr)s_rt.dynamicCapacity * 16, NULL, GL_STREAM_DRAW );
	qglBindBuffer( GL_TEXTURE_BUFFER, 0 );
	R_RtSceneCreateTexture( &s_rt.staticImage, "*rtStatic", s_rt.staticBuffer );
	R_RtSceneCreateTexture( &s_rt.dynamicImage, "*rtDynamic", s_rt.dynamicBuffer );
	GL_SelectTexture( 0 );

	s_rt.created = qtrue;
	s_rt.worldValid = qtrue;
	s_rt.statWorldBuildMs = (float)( ri.Milliseconds() - start );
	ri.Printf( PRINT_ALL, "Ray traced shadows: world BVH %d triangles of %d surfaces, %d nodes, depth %d, %.1f MB, %d ms\n",
		numTris, numSurfaces, s_rt.world.numNodes, bvh.maxDepth, worldTexels * 16.0f / ( 1024.0f * 1024.0f ),
		(int)s_rt.statWorldBuildMs );
}

/*
============================================================

Front end: the entities of a scene

============================================================
*/

void R_RtSceneNoteGhoulLod( int entityNum, int modelIndex, int lod )
{
	if ( !s_rt.sceneValid || entityNum < 0 || modelIndex < 0 || modelIndex >= 4 )
		return;
	const size_t index = (size_t)entityNum * 4 + modelIndex;
	if ( index < s_rt.ghoulLod->size() )
		( *s_rt.ghoulLod )[index] = (signed char)Q_min( lod, 127 );
}

int R_RtSceneGhoulLod( int entityNum, int modelIndex )
{
	if ( !s_rt.sceneValid || entityNum < 0 || modelIndex < 0 || modelIndex >= 4 )
		return -1;
	const size_t index = (size_t)entityNum * 4 + modelIndex;
	return index < s_rt.ghoulLod->size() ? ( *s_rt.ghoulLod )[index] : -1;
}

void R_RtSceneNoteMeshLod( int entityNum, int lod )
{
	if ( !s_rt.sceneValid || entityNum < 0 || entityNum >= (int)s_rt.meshLod->size() )
		return;
	( *s_rt.meshLod )[entityNum] = (signed char)Q_min( lod, 127 );
}

static bool R_RtEntityIsCaster( const trRefEntity_t *ent )
{
	if ( ent->e.reType != RT_MODEL )
		return false;
	// the view model is drawn with a hacked depth range, the shadow views skip it too
	if ( ent->e.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) )
		return false;
	if ( R_IsPostRenderEntity( ent ) )
		return false;
	return true;
}

void R_RtScenePrepare( trRefdef_t *refdef )
{
	s_rt.sceneValid = qfalse;
	if ( !R_RtSceneValid() || !tr.world || ( refdef->rdflags & RDF_NOWORLDMODEL ) )
		return;
	if ( !R_RtShadowsSunActive() && !R_RtShadowsLightsActive() )
		return;

	const int numEntities = refdef->num_entities;
	s_rt.entityTraced->assign( (size_t)numEntities + 1, RT_TRACED_NONE );
	s_rt.ghoulLod->assign( ( (size_t)numEntities + 1 ) * 4, -1 );
	s_rt.meshLod->assign( (size_t)numEntities + 1, -1 );
	s_rt.sceneValid = qtrue;

	if ( !r_drawentities->integer )
		return;

	const int first = ( tr.world->skyboxportal ) ? tr.skyPortalEntities : 0;
	const float maxDist = Q_max( 0.0f, r_rtShadowsCharacterDistance->value );
	const int maxCharacters = Com_Clampi( 0, 64, r_rtShadowsMaxCharacters->integer );

	struct candidate_t { float dist; int entity; };
	std::vector<candidate_t> characters;
	int numInstances = 0;

	for ( int i = first; i < numEntities; i++ )
	{
		trRefEntity_t *ent = &refdef->entities[i];
		if ( !R_RtEntityIsCaster( ent ) )
			continue;

		if ( ent->e.ghoul2 && G2API_HaveWeGhoul2Models( *( (CGhoul2Info_v *)ent->e.ghoul2 ) ) )
		{
			const float dist = Distance( ent->e.origin, refdef->vieworg );
			if ( dist <= maxDist )
			{
				candidate_t c = { dist, i };
				characters.push_back( c );
			}
			continue;
		}

		const model_t *model = R_GetModelByHandle( ent->e.hModel );
		if ( !model )
			continue;
		if ( numInstances >= RT_MAX_INSTANCES - 64 )
			continue;
		if ( model->type == MOD_BRUSH && model->data.bmodel )
		{
			( *s_rt.entityTraced )[i] = RT_TRACED_RIGID;
			numInstances++;
		}
		else if ( model->type == MOD_MESH && model->data.mdv[0] )
		{
			// plants bend and get cut (tr_plantsim.cpp, tr_plantcut.cpp): shadow maps
			if ( model->data.mdv[0]->plantSkel || ent->plantCutDraw || ent->plantCutDebris )
				continue;
			( *s_rt.entityTraced )[i] = RT_TRACED_RIGID;
			numInstances++;
		}
	}

	std::sort( characters.begin(), characters.end(),
		[]( const candidate_t &a, const candidate_t &b ) { return a.dist < b.dist; } );
	for ( size_t i = 0; i < characters.size() && (int)i < maxCharacters; i++ )
		( *s_rt.entityTraced )[characters[i].entity] = RT_TRACED_GHOUL;
}

/*
============================================================

Back end: bottom level trees

============================================================
*/

static bool R_RtAppendStatic( const std::vector<float> &data )
{
	const int texels = (int)( data.size() / 4 );
	if ( s_rt.staticUsed + texels > s_rt.staticCapacity )
		return false;
	qglBindBuffer( GL_TEXTURE_BUFFER, s_rt.staticBuffer );
	qglBufferSubData( GL_TEXTURE_BUFFER, (GLintptr)s_rt.staticUsed * 16, (GLsizeiptr)texels * 16, data.data() );
	qglBindBuffer( GL_TEXTURE_BUFFER, 0 );
	s_rt.staticUsed += texels;
	return true;
}

// numNodes 0: nothing of this model is traced (remembered, so it is not rebuilt)
static const rtBlas_t *R_RtRigidBlas( const rtRigidKey_t &key, std::vector<float> &positions )
{
	rtBlas_t blas;
	Com_Memset( &blas, 0, sizeof( blas ) );
	const int numTris = (int)( positions.size() / 9 );
	if ( numTris > 0 && numTris <= RT_MAX_RIGID_TRIS )
	{
		rtBvh_t bvh;
		if ( RtBvh_Build( positions.data(), numTris, &bvh ) )
		{
			std::vector<float> data;
			rtBlas_t built;
			R_RtFlatten( bvh, positions.data(), data, &built, s_rt.staticUsed );
			if ( R_RtAppendStatic( data ) )
				blas = built;
		}
	}
	return &( ( *s_rt.rigid )[key] = blas );
}

static const rtBlas_t *R_RtBrushBlas( const bmodel_t *bmodel )
{
	rtRigidKey_t key = { bmodel, 0, 0, 0, 0 };
	std::map<rtRigidKey_t, rtBlas_t>::iterator it = s_rt.rigid->find( key );
	if ( it != s_rt.rigid->end() )
		return &it->second;

	std::vector<float> positions;
	world_t *world = R_GetWorld( bmodel->worldIndex );
	if ( world )
	{
		for ( int i = 0; i < bmodel->numSurfaces; i++ )
		{
			const msurface_t *surf = world->surfaces + bmodel->firstSurface + i;
			if ( !surf->data || !surf->shader || surf->waterKey )
				continue;
			const shader_t *shader = surf->shader->remappedShader ? surf->shader->remappedShader : surf->shader;
			if ( R_RtShaderTraced( shader ) )
				R_RtAppendBspSurface( surf, positions );
		}
	}
	return R_RtRigidBlas( key, positions );
}

static const rtBlas_t *R_RtMeshBlas( const trRefEntity_t *ent, const mdvModel_t *model, int frame )
{
	rtRigidKey_t key = { model, frame, ent->e.customShader, ent->e.customSkin, ent->e.skinNum };
	std::map<rtRigidKey_t, rtBlas_t>::iterator it = s_rt.rigid->find( key );
	if ( it != s_rt.rigid->end() )
		return &it->second;

	std::vector<float> positions;
	for ( int s = 0; s < model->numSurfaces; s++ )
	{
		const mdvSurface_t *surface = &model->surfaces[s];

		// the shader R_AddMD3Surfaces draws it with
		const shader_t *shader;
		if ( ent->e.customShader )
		{
			shader = R_GetShaderByHandle( ent->e.customShader );
		}
		else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins )
		{
			const skin_t *skin = R_GetSkinByHandle( ent->e.customSkin );
			shader = tr.defaultShader;
			for ( int j = 0; j < skin->numSurfaces; j++ )
			{
				if ( !strcmp( skin->surfaces[j]->name, surface->name ) )
				{
					shader = (shader_t *)skin->surfaces[j]->shader;
					break;
				}
			}
		}
		else if ( surface->numShaderIndexes <= 0 )
		{
			shader = tr.defaultShader;
		}
		else
		{
			shader = tr.shaders[surface->shaderIndexes[ent->e.skinNum % surface->numShaderIndexes]];
		}
		if ( !shader )
			continue;
		if ( shader->remappedShader )
			shader = shader->remappedShader;
		if ( !R_RtShaderTraced( shader ) )
			continue;
		if ( R_ResolveAutoFoliage( model, surface, shader, r_autoFoliage->integer ).cls != FOLIAGE_NONE )
			continue;

		const mdvVertex_t *verts = surface->verts + (size_t)frame * surface->numVerts;
		for ( int i = 0; i + 2 < surface->numIndexes; i += 3 )
		{
			const glIndex_t a = surface->indexes[i], b = surface->indexes[i + 1], c = surface->indexes[i + 2];
			if ( a >= (glIndex_t)surface->numVerts || b >= (glIndex_t)surface->numVerts || c >= (glIndex_t)surface->numVerts )
				continue;
			positions.insert( positions.end(), verts[a].xyz, verts[a].xyz + 3 );
			positions.insert( positions.end(), verts[b].xyz, verts[b].xyz + 3 );
			positions.insert( positions.end(), verts[c].xyz, verts[c].xyz + 3 );
		}
	}
	return R_RtRigidBlas( key, positions );
}

static rtGhoulTemplate_t *R_RtGhoulTemplate( const rtGhoulPart_t *part, unsigned frame )
{
	const void *header = part->model->data.glm->header;
	std::vector<rtGhoulTemplate_t *> &list = *s_rt.ghoulTemplates;
	for ( size_t i = 0; i < list.size(); i++ )
	{
		rtGhoulTemplate_t *t = list[i];
		if ( t->header == header && t->lod == part->lod &&
			!memcmp( t->surfaceMask, part->surfaceMask, sizeof( t->surfaceMask ) ) )
		{
			t->lastUsed = frame;
			return t;
		}
	}

	if ( list.size() >= RT_MAX_GHOUL_TEMPLATES )
	{
		size_t oldest = 0;
		for ( size_t i = 1; i < list.size(); i++ )
			if ( list[i]->lastUsed < list[oldest]->lastUsed )
				oldest = i;
		// one in use by this scene stays
		if ( list[oldest]->lastUsed == frame )
			return NULL;
		delete list[oldest];
		list.erase( list.begin() + oldest );
	}

	rtGhoulTemplate_t *t = new rtGhoulTemplate_t();
	t->header = header;
	t->lod = part->lod;
	Com_Memcpy( t->surfaceMask, part->surfaceMask, sizeof( t->surfaceMask ) );
	t->lastUsed = frame;

	std::vector<int> indexes;
	R_RtGhoulExtract( part, t->xyz, t->bones, t->weights, indexes );
	const int numTris = (int)( indexes.size() / 3 );
	std::vector<float> positions( (size_t)numTris * 9 );
	for ( size_t i = 0; i < indexes.size(); i++ )
		Com_Memcpy( &positions[i * 3], &t->xyz[(size_t)indexes[i] * 3], sizeof( float ) * 3 );

	rtBvh_t bvh;
	if ( !RtBvh_Build( positions.data(), numTris, &bvh ) )
	{
		// kept, empty: the model is skipped instead of rebuilt every frame
		list.push_back( t );
		return t;
	}
	t->nodes = bvh.nodes;
	t->slotIndexes.resize( (size_t)numTris * 3 );
	for ( int slot = 0; slot < numTris; slot++ )
		for ( int c = 0; c < 3; c++ )
			t->slotIndexes[(size_t)slot * 3 + c] = indexes[(size_t)bvh.order[slot] * 3 + c];
	list.push_back( t );
	return t;
}

// skin, write the triangles, refit: runs on the job workers (no GL, no engine calls)
static void R_RtGhoulSkinJobs( void *user, int begin, int end )
{
	float *dynamicData = (float *)user;
	for ( int j = begin; j < end; j++ )
	{
		rtGhoulJob_t &job = ( *s_rt.ghoulJobs )[j];
		const rtGhoulTemplate_t *t = job.tmpl;
		const int numVerts = (int)( t->xyz.size() / 3 );
		const int numBones = job.part.numBones;
		float *out = job.skinned.data();
		for ( int v = 0; v < numVerts; v++ )
		{
			const float *p = &t->xyz[(size_t)v * 3];
			const byte *b = &t->bones[(size_t)v * 4];
			const float *w = &t->weights[(size_t)v * 4];
			float x = 0.0f, y = 0.0f, z = 0.0f;
			for ( int k = 0; k < 4; k++ )
			{
				if ( w[k] == 0.0f || b[k] >= numBones )
					continue;
				const float *m = job.part.bones[b[k]];
				x += w[k] * ( m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3] );
				y += w[k] * ( m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7] );
				z += w[k] * ( m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11] );
			}
			out[(size_t)v * 3] = x;
			out[(size_t)v * 3 + 1] = y;
			out[(size_t)v * 3 + 2] = z;
		}

		const int numTris = (int)( t->slotIndexes.size() / 3 );
		rtTri_t *tris = (rtTri_t *)( dynamicData + (size_t)job.triTexel * 4 );
		for ( int s = 0; s < numTris; s++ )
		{
			const float *a = &out[(size_t)t->slotIndexes[(size_t)s * 3] * 3];
			const float *bb = &out[(size_t)t->slotIndexes[(size_t)s * 3 + 1] * 3];
			const float *c = &out[(size_t)t->slotIndexes[(size_t)s * 3 + 2] * 3];
			rtTri_t &tri = tris[s];
			for ( int k = 0; k < 3; k++ )
			{
				tri.v0[k] = a[k];
				tri.e1[k] = bb[k] - a[k];
				tri.e2[k] = c[k] - a[k];
			}
			tri.f0 = tri.f1 = tri.f2 = 0.0f;
		}

		rtNode_t *nodes = (rtNode_t *)( dynamicData + (size_t)job.nodeTexel * 4 );
		Com_Memcpy( nodes, t->nodes.data(), t->nodes.size() * sizeof( rtNode_t ) );
		RtBvh_Refit( nodes, (int)t->nodes.size(), tris );
	}
}

/*
============================================================

Back end: instances and the top level

============================================================
*/

// rows of world -> local of an entity; false when the axes are degenerate
static bool R_RtEntityMatrix( const trRefEntity_t *ent, float *worldToLocal, float *localToWorld )
{
	const float *a = ent->e.axis[0], *b = ent->e.axis[1], *c = ent->e.axis[2], *o = ent->e.origin;
	// local -> world: columns are the axes
	localToWorld[0] = a[0]; localToWorld[1] = b[0]; localToWorld[2] = c[0]; localToWorld[3] = o[0];
	localToWorld[4] = a[1]; localToWorld[5] = b[1]; localToWorld[6] = c[1]; localToWorld[7] = o[1];
	localToWorld[8] = a[2]; localToWorld[9] = b[2]; localToWorld[10] = c[2]; localToWorld[11] = o[2];

	vec3_t bc, ca, ab;
	CrossProduct( b, c, bc );
	CrossProduct( c, a, ca );
	CrossProduct( a, b, ab );
	const float det = DotProduct( a, bc );
	if ( fabsf( det ) < 1.0e-12f )
		return false;
	const float inv = 1.0f / det;
	const float *rows[3] = { bc, ca, ab };
	for ( int r = 0; r < 3; r++ )
	{
		worldToLocal[r * 4] = rows[r][0] * inv;
		worldToLocal[r * 4 + 1] = rows[r][1] * inv;
		worldToLocal[r * 4 + 2] = rows[r][2] * inv;
		worldToLocal[r * 4 + 3] = -( worldToLocal[r * 4] * o[0] + worldToLocal[r * 4 + 1] * o[1] + worldToLocal[r * 4 + 2] * o[2] );
	}
	return true;
}

static void R_RtInstanceBounds( rtInstance_t *inst, const float *localToWorld, const float *mins, const float *maxs )
{
	inst->mins[0] = inst->mins[1] = inst->mins[2] = 1.0e30f;
	inst->maxs[0] = inst->maxs[1] = inst->maxs[2] = -1.0e30f;
	for ( int corner = 0; corner < 8; corner++ )
	{
		const float x = ( corner & 1 ) ? maxs[0] : mins[0];
		const float y = ( corner & 2 ) ? maxs[1] : mins[1];
		const float z = ( corner & 4 ) ? maxs[2] : mins[2];
		for ( int r = 0; r < 3; r++ )
		{
			const float v = localToWorld[r * 4] * x + localToWorld[r * 4 + 1] * y + localToWorld[r * 4 + 2] * z + localToWorld[r * 4 + 3];
			if ( v < inst->mins[r] ) inst->mins[r] = v;
			if ( v > inst->maxs[r] ) inst->maxs[r] = v;
		}
	}
}

// median split over the instance boxes, one instance per leaf; same node layout as the triangle trees
static void R_RtBuildTlas( std::vector<rtInstance_t> &instances, int first, int count, std::vector<rtNode_t> &nodes )
{
	const int nodeIndex = (int)nodes.size();
	nodes.push_back( rtNode_t() );

	float mins[3] = { 1.0e30f, 1.0e30f, 1.0e30f }, maxs[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
	float cmins[3] = { 1.0e30f, 1.0e30f, 1.0e30f }, cmaxs[3] = { -1.0e30f, -1.0e30f, -1.0e30f };
	for ( int i = first; i < first + count; i++ )
	{
		for ( int k = 0; k < 3; k++ )
		{
			mins[k] = Q_min( mins[k], instances[i].mins[k] );
			maxs[k] = Q_max( maxs[k], instances[i].maxs[k] );
			const float c = 0.5f * ( instances[i].mins[k] + instances[i].maxs[k] );
			cmins[k] = Q_min( cmins[k], c );
			cmaxs[k] = Q_max( cmaxs[k], c );
		}
	}
	Com_Memcpy( nodes[nodeIndex].mins, mins, sizeof( mins ) );
	Com_Memcpy( nodes[nodeIndex].maxs, maxs, sizeof( maxs ) );

	if ( count <= 1 )
	{
		nodes[nodeIndex].a = (float)first;
		nodes[nodeIndex].b = (float)( nodeIndex + 1 ) * RTBVH_SKIP_SCALE + (float)count;
		return;
	}

	int axis = 0;
	if ( cmaxs[1] - cmins[1] > cmaxs[axis] - cmins[axis] ) axis = 1;
	if ( cmaxs[2] - cmins[2] > cmaxs[axis] - cmins[axis] ) axis = 2;
	const int mid = first + count / 2;
	std::nth_element( instances.begin() + first, instances.begin() + mid, instances.begin() + first + count,
		[axis]( const rtInstance_t &a, const rtInstance_t &b )
		{ return a.mins[axis] + a.maxs[axis] < b.mins[axis] + b.maxs[axis]; } );

	nodes[nodeIndex].a = -1.0f;
	R_RtBuildTlas( instances, first, mid - first, nodes );
	R_RtBuildTlas( instances, mid, first + count - mid, nodes );
	nodes[nodeIndex].b = (float)nodes.size() * RTBVH_SKIP_SCALE;
}

qboolean RB_RtSceneUpdate( void )
{
	if ( !R_RtSceneValid() || !s_rt.sceneValid )
		return qfalse;

	const unsigned frame = backEndData->realFrameNumber;
	const int scene = backEndData->currentFrame ? backEndData->currentFrame->currentScene : 0;
	if ( s_rt.updatedFrame == frame && s_rt.updatedScene == scene )
		return s_rt.updateOk;
	s_rt.updatedFrame = frame;
	s_rt.updatedScene = scene;
	s_rt.updateOk = qfalse;

	const int start = ri.Milliseconds();
	const trRefdef_t *refdef = &backEnd.refdef;
	std::vector<rtInstance_t> &instances = *s_rt.instances;
	std::vector<rtGhoulJob_t> &jobs = *s_rt.ghoulJobs;
	instances.clear();
	jobs.clear();
	s_rt.statDropped = 0;
	s_rt.statGhoulTris = 0;

	int dynamicUsed = RT_HEADER_TEXELS;
	const int numEntities = Q_min( refdef->num_entities, (int)s_rt.entityTraced->size() );
	for ( int i = 0; i < numEntities; i++ )
	{
		const byte traced = ( *s_rt.entityTraced )[i];
		if ( traced == RT_TRACED_NONE )
			continue;
		trRefEntity_t *ent = &refdef->entities[i];
		float worldToLocal[12], localToWorld[12];
		if ( !R_RtEntityMatrix( ent, worldToLocal, localToWorld ) )
			continue;

		if ( traced == RT_TRACED_RIGID )
		{
			const model_t *model = R_GetModelByHandle( ent->e.hModel );
			const rtBlas_t *blas = NULL;
			if ( model && model->type == MOD_BRUSH && model->data.bmodel )
			{
				blas = R_RtBrushBlas( model->data.bmodel );
			}
			else if ( model && model->type == MOD_MESH && model->data.mdv[0] )
			{
				int lod = ( *s_rt.meshLod )[i];
				if ( lod < 0 || lod >= MD3_MAX_LODS || !model->data.mdv[lod] )
					lod = 0;
				const mdvModel_t *mdv = model->data.mdv[lod];
				// the frame the mesh is nearer to; the lerp itself is not traced
				int frame = ent->e.backlerp > 0.5f ? ent->e.oldframe : ent->e.frame;
				if ( frame < 0 || frame >= mdv->numFrames )
					frame = 0;
				blas = R_RtMeshBlas( ent, mdv, frame );
			}
			if ( !blas || blas->numNodes <= 0 )
			{
				if ( blas == NULL )
					s_rt.statDropped++;
				continue;
			}
			if ( (int)instances.size() >= RT_MAX_INSTANCES )
			{
				s_rt.statDropped++;
				continue;
			}
			rtInstance_t inst;
			Com_Memcpy( inst.worldToLocal, worldToLocal, sizeof( worldToLocal ) );
			R_RtInstanceBounds( &inst, localToWorld, blas->mins, blas->maxs );
			inst.nodeTexel = (float)blas->nodeTexel;
			inst.triTexel = (float)blas->triTexel;
			inst.numNodes = (float)blas->numNodes;
			inst.dynamic = 0.0f;
			instances.push_back( inst );
			continue;
		}

		rtGhoulPart_t parts[RT_G2_MAX_PARTS];
		const int numParts = R_RtGhoulCollectParts( ent, i, parts, RT_G2_MAX_PARTS );
		for ( int p = 0; p < numParts; p++ )
		{
			rtGhoulTemplate_t *t = R_RtGhoulTemplate( &parts[p], frame );
			if ( !t || t->nodes.empty() || t->slotIndexes.empty() )
				continue;
			const int numNodes = (int)t->nodes.size();
			const int numTris = (int)( t->slotIndexes.size() / 3 );
			const int texels = numNodes * 2 + numTris * 3;
			if ( dynamicUsed + texels > s_rt.dynamicCapacity || (int)instances.size() >= RT_MAX_INSTANCES )
			{
				s_rt.statDropped++;
				continue;
			}
			jobs.push_back( rtGhoulJob_t() );
			rtGhoulJob_t &job = jobs.back();
			job.part = parts[p];
			job.tmpl = t;
			job.nodeTexel = dynamicUsed;
			job.triTexel = dynamicUsed + numNodes * 2;
			job.instance = (int)instances.size();
			Com_Memcpy( job.localToWorld, localToWorld, sizeof( localToWorld ) );
			job.skinned.resize( t->xyz.size() );
			dynamicUsed += texels;
			s_rt.statGhoulTris += numTris;

			rtInstance_t inst;
			Com_Memcpy( inst.worldToLocal, worldToLocal, sizeof( worldToLocal ) );
			// bounds: after the refit
			inst.nodeTexel = (float)job.nodeTexel;
			inst.triTexel = (float)job.triTexel;
			inst.numNodes = (float)numNodes;
			inst.dynamic = 1.0f;
			inst.mins[0] = inst.mins[1] = inst.mins[2] = 0.0f;
			inst.maxs[0] = inst.maxs[1] = inst.maxs[2] = 0.0f;
			instances.push_back( inst );
		}
	}

	std::vector<float> &data = *s_rt.dynamicData;
	if ( (int)data.size() < dynamicUsed * 4 )
		data.resize( (size_t)dynamicUsed * 4 );

	if ( !jobs.empty() )
	{
		R_ParallelFor( (int)jobs.size(), 1, R_RtGhoulSkinJobs, data.data() );
		for ( size_t j = 0; j < jobs.size(); j++ )
		{
			const rtNode_t *root = (const rtNode_t *)&data[(size_t)jobs[j].nodeTexel * 4];
			rtInstance_t &inst = instances[jobs[j].instance];
			R_RtInstanceBounds( &inst, jobs[j].localToWorld, root->mins, root->maxs );
		}
	}

	// top level
	std::vector<rtNode_t> tlas;
	if ( !instances.empty() )
		R_RtBuildTlas( instances, 0, (int)instances.size(), tlas );
	s_rt.tlasNodes = (int)tlas.size();
	if ( s_rt.tlasNodes * 2 > RT_TLAS_TEXELS )
	{
		// cannot happen with one instance per leaf; keep the world rather than read garbage
		s_rt.tlasNodes = 0;
	}
	if ( s_rt.tlasNodes > 0 )
		Com_Memcpy( data.data(), tlas.data(), tlas.size() * sizeof( rtNode_t ) );
	for ( size_t i = 0; i < instances.size(); i++ )
	{
		float *out = &data[( (size_t)RT_TLAS_TEXELS + i * 4 ) * 4];
		Com_Memcpy( out, instances[i].worldToLocal, sizeof( float ) * 12 );
		out[12] = instances[i].nodeTexel;
		out[13] = instances[i].triTexel;
		out[14] = instances[i].numNodes;
		out[15] = instances[i].dynamic;
	}

	// only what is used goes up: the header up to the last instance, then the models
	const int headerTexels = RT_TLAS_TEXELS + (int)instances.size() * 4;
	qglBindBuffer( GL_TEXTURE_BUFFER, s_rt.dynamicBuffer );
	qglBufferData( GL_TEXTURE_BUFFER, (GLsizeiptr)s_rt.dynamicCapacity * 16, NULL, GL_STREAM_DRAW );
	if ( s_rt.tlasNodes > 0 )
		qglBufferSubData( GL_TEXTURE_BUFFER, 0, (GLsizeiptr)s_rt.tlasNodes * 32, data.data() );
	if ( !instances.empty() )
		qglBufferSubData( GL_TEXTURE_BUFFER, (GLintptr)RT_TLAS_TEXELS * 16,
			(GLsizeiptr)( headerTexels - RT_TLAS_TEXELS ) * 16, &data[(size_t)RT_TLAS_TEXELS * 4] );
	if ( dynamicUsed > RT_HEADER_TEXELS )
		qglBufferSubData( GL_TEXTURE_BUFFER, (GLintptr)RT_HEADER_TEXELS * 16,
			(GLsizeiptr)( dynamicUsed - RT_HEADER_TEXELS ) * 16, &data[(size_t)RT_HEADER_TEXELS * 4] );
	qglBindBuffer( GL_TEXTURE_BUFFER, 0 );

	s_rt.statInstances = (int)instances.size();
	s_rt.statGhoulParts = (int)jobs.size();
	s_rt.statDynamicTexels = dynamicUsed;
	s_rt.statUpdateMs = (float)( ri.Milliseconds() - start );
	s_rt.updateOk = qtrue;

	if ( r_rtShadowsStats->integer && scene == 0 )
	{
		ri.Printf( PRINT_ALL, "rt scene: %d instances (%d Ghoul2 models, %d triangles skinned), %d dropped, %.2f MB dynamic, %d ms\n",
			s_rt.statInstances, s_rt.statGhoulParts, s_rt.statGhoulTris, s_rt.statDropped,
			dynamicUsed * 16.0f / ( 1024.0f * 1024.0f ), (int)s_rt.statUpdateMs );
	}
	return qtrue;
}

// sceneParams: world node count, world triangle texel, top level node count, instance texel
void RB_RtSceneBind( int staticTmu, int dynamicTmu, vec4_t sceneParams )
{
	GL_BindToTMU( &s_rt.staticImage, staticTmu );
	GL_BindToTMU( &s_rt.dynamicImage, dynamicTmu );
	VectorSet4( sceneParams, (float)s_rt.world.numNodes, (float)s_rt.world.triTexel,
		(float)s_rt.tlasNodes, (float)RT_TLAS_TEXELS );
}

void R_RtSceneStats_f( void )
{
	if ( !R_RtSceneValid() )
	{
		ri.Printf( PRINT_ALL, "Ray traced shadows: no traced scene (r_sunShadowMode 2 or r_dlightMode 3, vid_restart, load a map)\n" );
		return;
	}
	ri.Printf( PRINT_ALL, "Ray traced scene\n" );
	ri.Printf( PRINT_ALL, "  world: %d triangles, %d nodes, built in %d ms\n",
		s_rt.world.numTris, s_rt.world.numNodes, (int)s_rt.statWorldBuildMs );
	ri.Printf( PRINT_ALL, "  static buffer: %.1f of %.1f MB, %d rigid trees (brush models, md3 frames)\n",
		s_rt.staticUsed * 16.0f / ( 1024.0f * 1024.0f ), s_rt.staticCapacity * 16.0f / ( 1024.0f * 1024.0f ),
		(int)s_rt.rigid->size() );
	ri.Printf( PRINT_ALL, "  last scene: %d instances, %d Ghoul2 models / %d triangles skinned, %d dropped, %.2f MB dynamic, %d ms\n",
		s_rt.statInstances, s_rt.statGhoulParts, s_rt.statGhoulTris, s_rt.statDropped,
		s_rt.statDynamicTexels * 16.0f / ( 1024.0f * 1024.0f ), (int)s_rt.statUpdateMs );
	ri.Printf( PRINT_ALL, "  %d Ghoul2 templates cached\n", (int)s_rt.ghoulTemplates->size() );
}
