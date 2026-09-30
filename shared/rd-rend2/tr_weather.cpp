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

#include "tr_weather.h"
#include <utility>
#include <vector>
#include <cmath>
#include <chrono>
#include <algorithm>

// Cached to save test tume
int CurrentWeatherBrushIndex;

namespace
{
	const int CHUNK_COUNT = 9;  // in 3x3 arrangement
	const float CHUNK_EXTENDS = 2000.f;
	const float HALF_CHUNK_EXTENDS = CHUNK_EXTENDS * 0.5f;

	// Interleaved transform feedback records, in the order of the XFB
	// varyings (var_Position, var_Velocity[, var_Impact]); position is chunk
	// local. Weather particles use rainVertex_t. With r_rainSplashes the rain
	// slot switches to rainSplashVertex_t: impact is the drop's last impact
	// in world space and its state (life, one impact per fall), see
	// weatherUpdate.glsl. The splash draw reads it; an event driven ripple
	// pass could read the same record.
	struct rainVertex_t
	{
		vec3_t position;
		vec3_t velocity;
	};
	struct rainSplashVertex_t
	{
		vec3_t position;
		vec3_t velocity;
		vec4_t impact;
	};
	static_assert(sizeof(rainVertex_t) == 24 && offsetof(rainVertex_t, velocity) == 12,
		"rainVertex_t must match the interleaved weatherUpdate varyings");
	static_assert(sizeof(rainSplashVertex_t) == 40 && offsetof(rainSplashVertex_t, impact) == 24,
		"rainSplashVertex_t must match the interleaved weatherUpdate varyings");

	void RB_UpdateWindObject( windObject_t *wo )
	{
		if (wo->targetVelocityTimeRemaining == 0)
		{
			if (Q_flrand(0.f, 1.f) < wo->chanceOfDeadTime)
			{
				wo->targetVelocityTimeRemaining = Q_flrand(wo->deadTimeMinMax[0], wo->deadTimeMinMax[1]);
				VectorSet(wo->targetVelocity, 0.0f, 0.0f, 0.0f);
			}
			else
			{
				wo->targetVelocityTimeRemaining = Q_flrand(1000.f, 2500.f);
				VectorSet(
					wo->targetVelocity,
					Q_flrand(wo->minVelocity[0], wo->maxVelocity[0]),
					Q_flrand(wo->minVelocity[1], wo->maxVelocity[1]),
					Q_flrand(wo->minVelocity[2], wo->maxVelocity[2]));
			}
			return;
		}

		wo->targetVelocityTimeRemaining--;
		vec3_t deltaVelocity;
		VectorSubtract(wo->targetVelocity, wo->currentVelocity, deltaVelocity);
		float	DeltaVelocityLen = VectorNormalize(deltaVelocity);
		if (DeltaVelocityLen > 10.f)
		{
			DeltaVelocityLen = 10.f;
		}
		VectorScale(deltaVelocity, DeltaVelocityLen, deltaVelocity);
		VectorAdd(wo->currentVelocity, deltaVelocity, wo->currentVelocity);
	}

	// Fresh particles, resting, at random heights, in the slot's layout.
	std::vector<byte> RainParticles(const weatherObject_t& ws, size_t bufferSize)
	{
		const size_t stride = ws.impactLayout ? sizeof(rainSplashVertex_t) : sizeof(rainVertex_t);
		const size_t count = (size_t)ws.maxParticles * CHUNK_COUNT;
		std::vector<byte> data(MAX(bufferSize, count * stride), 0);
		for (size_t i = 0; i < count; ++i)
		{
			rainVertex_t& vertex = *(rainVertex_t *)(data.data() + i * stride);
			vertex.position[0] = Q_flrand(-HALF_CHUNK_EXTENDS, HALF_CHUNK_EXTENDS);
			vertex.position[1] = Q_flrand(-HALF_CHUNK_EXTENDS, HALF_CHUNK_EXTENDS);
			vertex.position[2] = Q_flrand(tr.world->bmodels[0].bounds[0][2], tr.world->bmodels[0].bounds[1][2]);
			vertex.velocity[0] = 0.0f; //Q_flrand(0.0f, 0.0f);
			vertex.velocity[1] = 0.0f; //Q_flrand(0.0f, 0.0f);
			vertex.velocity[2] = 0.0f; //Q_flrand(-1.0f, 0.0f);
			// rainSplashVertex_t::impact stays zero: no splash, may hit
		}
		return data;
	}

	void SetRainLayout( weatherObject_t& ws, bool impactLayout )
	{
		ws.impactLayout = impactLayout;
		ws.numAttribs = impactLayout ? 3 : 2;
		const int stride = impactLayout ? sizeof(rainSplashVertex_t) : sizeof(rainVertex_t);

		ws.attribsTemplate[0].index = ATTR_INDEX_POSITION;
		ws.attribsTemplate[0].numComponents = 3;
		ws.attribsTemplate[0].offset = offsetof(rainVertex_t, position);
		ws.attribsTemplate[0].stride = stride;
		ws.attribsTemplate[0].type = GL_FLOAT;
		ws.attribsTemplate[0].vbo = nullptr;

		ws.attribsTemplate[1].index = ATTR_INDEX_COLOR;
		ws.attribsTemplate[1].numComponents = 3;
		ws.attribsTemplate[1].offset = offsetof(rainVertex_t, velocity);
		ws.attribsTemplate[1].stride = stride;
		ws.attribsTemplate[1].type = GL_FLOAT;
		ws.attribsTemplate[1].vbo = nullptr;

		// r_rainSplashes impact state (rain slot, impact layout only)
		ws.attribsTemplate[2].index = ATTR_INDEX_TEXCOORD0;
		ws.attribsTemplate[2].numComponents = 4;
		ws.attribsTemplate[2].offset = offsetof(rainSplashVertex_t, impact);
		ws.attribsTemplate[2].stride = stride;
		ws.attribsTemplate[2].type = GL_FLOAT;
		ws.attribsTemplate[2].vbo = nullptr;
	}

	void ResetRainSimulation( weatherObject_t& ws )
	{
		ws.vboLastUpdateFrame = 0;
		VectorSet2(ws.maxHorizontalVelocity, 0.0f, 0.0f);
		ws.minDownwardVelocity = 0.0f;
		ws.maxVerticalVelocity = 0.0f;
		ws.velocityBoundsReliable = true;
	}

	// splashCapable (the rain slot) sizes the buffers for the impact layout,
	// so r_rainSplashes switches layouts in place (SwitchRainLayout)
	void GenerateRainModel( weatherObject_t& ws, const int maxParticleCount,
		bool splashCapable, bool impactLayout )
	{
		ws.maxParticles = maxParticleCount;
		ws.splashCapable = splashCapable;
		SetRainLayout(ws, splashCapable && impactLayout);

		const size_t bufferSize = (size_t)maxParticleCount * CHUNK_COUNT *
			(splashCapable ? sizeof(rainSplashVertex_t) : sizeof(rainVertex_t));
		std::vector<byte> rainVertices = RainParticles(ws, bufferSize);

		ws.lastVBO = R_CreateVBO(
			nullptr,
			bufferSize,
			VBO_USAGE_XFB, "Weather_ping");
		ws.vbo = R_CreateVBO(
			rainVertices.data(),
			bufferSize,
			VBO_USAGE_XFB, "Weather_pong");
		ResetRainSimulation(ws);
	}

	// r_rainSplashes toggled: the records change size, the particles restart
	void SwitchRainLayout( weatherObject_t& ws, bool impactLayout )
	{
		SetRainLayout(ws, impactLayout);
		const std::vector<byte> rainVertices = RainParticles(ws, 0);
		R_BindVBO(ws.vbo);
		qglBufferSubData(GL_ARRAY_BUFFER, 0, rainVertices.size(), rainVertices.data());
		ResetRainSimulation(ws);
	}

	bool intersectPlane(const vec3_t n, const float dist, const vec3_t l0, const vec3_t l, float &t)
	{
		// assuming vectors are all normalized
		float denom = DotProduct(n, l);
		if (denom > 1e-6) {
			t = -(DotProduct(l0, n) + dist) / denom;
			return (t >= 0);
		}

		return false;
	}

	// World geometry depth of the current (orthographic) tr.viewParms into
	// fbo. keepDepth draws over what is there (the weather brushes).
	void RenderWeatherWorldDepth(FBO_t *fbo, bool keepDepth)
	{
		RE_BeginFrame(STEREO_CENTER);

		if (keepDepth)
			tr.viewParms.flags |= VPF_NOCLEAR;

		tr.refdef.numDrawSurfs = 0;
		tr.refdef.drawSurfs = backEndData->drawSurfs;

		tr.refdef.num_entities = 0;
		tr.refdef.entities = backEndData->entities;

		tr.refdef.num_dlights = 0;
		tr.refdef.dlights = backEndData->dlights;

		tr.refdef.fistDrawSurf = 0;

		tr.skyPortalEntities = 0;

		tr.viewParms.targetFbo = fbo;
		tr.viewParms.currentViewParm = 0;
		Com_Memcpy(&tr.cachedViewParms[0], &tr.viewParms, sizeof(viewParms_t));
		tr.numCachedViewParms = 1;

		RB_UpdateConstants(&tr.refdef);

		R_GenerateDrawSurfs(&tr.viewParms, &tr.refdef);
		R_SortAndSubmitDrawSurfs(tr.refdef.drawSurfs, tr.refdef.numDrawSurfs);

		R_IssuePendingRenderCommands();
		tr.refdef.numDrawSurfs = 0;
		tr.numCachedViewParms = 0;

		RE_EndScene();

		R_NewFrameSync();
	}

	void GenerateDepthMap()
	{
		R_IssuePendingRenderCommands();
		R_InitNextFrame();
		RE_BeginFrame(STEREO_CENTER);

		vec3_t mapSize;
		vec3_t halfMapSize;
		VectorSubtract(
			tr.world->bmodels[0].bounds[0],
			tr.world->bmodels[0].bounds[1],
			mapSize);
		VectorScale(mapSize, -0.5f, halfMapSize);
		mapSize[2] = 0.0f;

		const vec3_t forward = {0.0f, 0.0f, -1.0f};
		const vec3_t left = {0.0f, 1.0f, 0.0f};
		const vec3_t up = {-1.0f, 0.0f, 0.0f};

		vec3_t viewOrigin;
		VectorMA(tr.world->bmodels[0].bounds[1], 0.5f, mapSize, viewOrigin);
		viewOrigin[2] = tr.world->bmodels[0].bounds[1][2];

		orientationr_t orientation;
		R_SetOrientationOriginAndAxis(orientation, viewOrigin, forward, left, up);

		const vec3_t viewBounds[2] = {
			{ 0.0f, -halfMapSize[1], -halfMapSize[0] },
			{ halfMapSize[2] * 2.0f, halfMapSize[1], halfMapSize[0] }
		};

		R_SetupViewParmsForOrthoRendering(
			tr.weatherDepthFbo->width,
			tr.weatherDepthFbo->height,
			tr.weatherDepthFbo,
			VPF_DEPTHCLAMP | VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC | VPF_NOVIEWMODEL,
			orientation,
			viewBounds);
		Matrix16Multiply(
			tr.viewParms.projectionMatrix,
			tr.viewParms.world.modelViewMatrix,
			tr.weatherSystem->weatherMVP);

		if (tr.weatherSystem->numWeatherBrushes > 0)
		{
			FBO_Bind(tr.weatherDepthFbo);

			GL_SetViewportAndScissor(0, 0, tr.weatherDepthFbo->width, tr.weatherDepthFbo->height);
			uint32_t glState = GLS_DEPTHMASK_TRUE;

			if (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE) // used outside brushes
			{
				qglClearDepth(0.0f);
				glState = GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_GREATER;
			}
			else // used inside brushes
			{
				qglClearDepth(1.0f);
			}

			GL_State(glState);
			qglClear(GL_DEPTH_BUFFER_BIT);
			qglClearDepth(1.0f);

			backEnd.currentEntity = &tr.worldEntity;

			vec3_t stepSize = {
				abs(mapSize[0]) / tr.weatherDepthFbo->width,
				abs(mapSize[1]) / tr.weatherDepthFbo->height,
				0.0,
			};

			vec3_t up = {
				stepSize[0] * 1.05f,
				0.0f,
				0.0f
			};
			vec3_t left = {
				0.0f,
				stepSize[1] * 1.05f,
				0.0f
			};
			vec3_t traceVec = {
				0.0f,
				0.0f,
				-1.0f
			};

			for (int i = 0; i < tr.weatherSystem->numWeatherBrushes; i++)
			{
				RE_BeginFrame(STEREO_CENTER);

				GL_State(glState);
				qglEnable(GL_DEPTH_CLAMP);
				GL_Cull(CT_TWO_SIDED);

				weatherBrushes_t *currentWeatherBrush = &tr.weatherSystem->weatherBrushes[i];

				// RBSP brushes actually store their bounding box in the first 6 planes! Nice
				vec3_t mins = {
					-currentWeatherBrush->planes[0][3],
					-currentWeatherBrush->planes[2][3],
					-currentWeatherBrush->planes[4][3],
				};
				vec3_t maxs = {
					currentWeatherBrush->planes[1][3],
					currentWeatherBrush->planes[3][3],
					currentWeatherBrush->planes[5][3],
				};

				ivec2_t numSteps = {
					int((maxs[0] - mins[0]) / stepSize[0]) + 2,
					int((maxs[1] - mins[1]) / stepSize[1]) + 2
				};

				vec2_t rayOrigin = {
					tr.world->bmodels[0].bounds[0][0] + (floorf((mins[0] - tr.world->bmodels[0].bounds[0][0]) / stepSize[0]) + 0.5f) * stepSize[0],
					tr.world->bmodels[0].bounds[0][1] + (floorf((mins[1] - tr.world->bmodels[0].bounds[0][1]) / stepSize[1]) + 0.5f) * stepSize[1]
				};

				for (int y = 0; y < (int)numSteps[1]; y++)
				{
					for (int x = 0; x < (int)numSteps[0]; x++)
					{
						vec3_t rayPos = {
							rayOrigin[0] + x * stepSize[0],
							rayOrigin[1] + y * stepSize[1],
							tr.world->bmodels[0].bounds[1][2]
						};

						// Find intersection point with the brush
						float t = 0.0f;
						for (int j = 0; j < currentWeatherBrush->numPlanes; j++)
						{
							vec3_t plane_normal;
							float plane_dist;
							if (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE)
							{
								plane_normal[0] = currentWeatherBrush->planes[j][0];
								plane_normal[1] = currentWeatherBrush->planes[j][1];
								plane_normal[2] = currentWeatherBrush->planes[j][2];
								plane_dist = -currentWeatherBrush->planes[j][3];
							}
							else
							{
								plane_normal[0] = -currentWeatherBrush->planes[j][0];
								plane_normal[1] = -currentWeatherBrush->planes[j][1];
								plane_normal[2] = -currentWeatherBrush->planes[j][2];
								plane_dist = currentWeatherBrush->planes[j][3];
							}

							float dist = 0.0f;
							if (intersectPlane(plane_normal, plane_dist, rayPos, traceVec, dist))
								t = MAX(t, dist);
						}

						bool hit = true;
						rayPos[2] -= t;

						// Now test if the intersected point is actually on the brush
						for (int j = 0; j < currentWeatherBrush->numPlanes; j++)
						{
							vec3_t normal = {
								currentWeatherBrush->planes[j][0],
								currentWeatherBrush->planes[j][1],
								currentWeatherBrush->planes[j][2]
							};
							if (DotProduct(rayPos, normal) > currentWeatherBrush->planes[j][3] + 1e-3)
								hit = false;
						}

						if (!hit)
							continue;

						// Just draw it when batch is full
						if (tess.numVertexes + 4 >= SHADER_MAX_VERTEXES || tess.numIndexes + 6 >= SHADER_MAX_INDEXES)
						{
							RB_UpdateVBOs(ATTR_POSITION);
							GLSL_VertexAttribsState(ATTR_POSITION, NULL);
							GLSL_BindProgram(&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES]);
							GLSL_SetUniformMatrix4x4(
								&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES],
								UNIFORM_MODELVIEWPROJECTIONMATRIX,
								tr.weatherSystem->weatherMVP);
							R_DrawElementsVBO(tess.numIndexes, tess.firstIndex, tess.minIndex, tess.maxIndex);

							RB_CommitInternalBufferData();

							tess.numIndexes = 0;
							tess.numVertexes = 0;
							tess.firstIndex = 0;
							tess.multiDrawPrimitives = 0;
							tess.externalIBO = nullptr;
						}

						int ndx = tess.numVertexes;

						tess.indexes[tess.numIndexes] = ndx;
						tess.indexes[tess.numIndexes + 1] = ndx + 1;
						tess.indexes[tess.numIndexes + 2] = ndx + 3;

						tess.indexes[tess.numIndexes + 3] = ndx + 3;
						tess.indexes[tess.numIndexes + 4] = ndx + 1;
						tess.indexes[tess.numIndexes + 5] = ndx + 2;

						tess.xyz[ndx][0] = rayPos[0] - left[0];
						tess.xyz[ndx][1] = rayPos[1] - left[1];
						tess.xyz[ndx][2] = rayPos[2] - left[2];

						tess.xyz[ndx + 1][0] = rayPos[0] + up[0];
						tess.xyz[ndx + 1][1] = rayPos[1] + up[1];
						tess.xyz[ndx + 1][2] = rayPos[2] + up[2];

						tess.xyz[ndx + 2][0] = rayPos[0] + left[0];
						tess.xyz[ndx + 2][1] = rayPos[1] + left[1];
						tess.xyz[ndx + 2][2] = rayPos[2] + left[2];

						tess.xyz[ndx + 3][0] = rayPos[0] - up[0];
						tess.xyz[ndx + 3][1] = rayPos[1] - up[1];
						tess.xyz[ndx + 3][2] = rayPos[2] - up[2];

						tess.numVertexes += 4;
						tess.numIndexes += 6;
					}
				}
				R_NewFrameSync();
			}

			// draw remaining quads
			RB_UpdateVBOs(ATTR_POSITION);
			GLSL_VertexAttribsState(ATTR_POSITION, NULL);
			GLSL_BindProgram(&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES]);
			GLSL_SetUniformMatrix4x4(
				&tr.textureColorShader[TEXCOLORDEF_USE_VERTICES],
				UNIFORM_MODELVIEWPROJECTIONMATRIX,
				tr.weatherSystem->weatherMVP);
			R_DrawElementsVBO(tess.numIndexes, tess.firstIndex, tess.minIndex, tess.maxIndex);

			RB_CommitInternalBufferData();

			tess.numIndexes = 0;
			tess.numVertexes = 0;
			tess.firstIndex = 0;
			tess.multiDrawPrimitives = 0;
			tess.externalIBO = nullptr;

			qglDisable(GL_DEPTH_CLAMP);
		}

		RenderWeatherWorldDepth(tr.weatherDepthFbo, tr.weatherSystem->numWeatherBrushes > 0);

		// r_rainSplashes: weather brushes are invisible, a splash must not
		// land on them. The world alone, same view, into its own map.
		tr.weatherSystem->surfaceMapValid = false;
		if (tr.weatherSystem->numWeatherBrushes > 0 && tr.weatherSurfaceFbo)
		{
			R_SetupViewParmsForOrthoRendering(
				tr.weatherSurfaceFbo->width,
				tr.weatherSurfaceFbo->height,
				tr.weatherSurfaceFbo,
				VPF_DEPTHCLAMP | VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC | VPF_NOVIEWMODEL,
				orientation,
				viewBounds);
			RenderWeatherWorldDepth(tr.weatherSurfaceFbo, false);
			tr.weatherSystem->surfaceMapValid = true;
		}

		tr.weatherSystem->depthRangeWorld = MAX(
			tr.world->bmodels[0].bounds[1][2] - tr.world->bmodels[0].bounds[0][2], 1.0f);
		tr.weatherSystem->texelSizeWorld = MAX(
			fabsf(mapSize[0]) / tr.weatherDepthFbo->width,
			fabsf(mapSize[1]) / tr.weatherDepthFbo->height);
		tr.weatherSystem->depthMapValid = true;
	}

	/*
	r_rainSplashes: impact events

	weatherUpdate.glsl compares each rain drop's step with the static rain
	occlusion map (tr.weatherDepthImage): a drop that goes from above to
	below the occluder of the column it lands in has hit that surface. The
	crossing point, snapped onto the surface, and a life of 1 go into the
	drop's transform feedback record (rainVertex_t::impact); the life then
	runs out over r_rainSplashLifetime. The drop itself falls on unchanged.
	weatherSplash.glsl draws the live impacts. No CPU trace, no readback.
	*/
	bool RainSplashesEnabled(int weatherType)
	{
		return weatherType == WEATHER_RAIN && r_rainSplashes->integer != 0 &&
			tr.weatherSystem->depthMapValid;
	}

	float RainSplashLifetime()
	{
		return Com_Clamp(50.0f, 2000.0f, r_rainSplashLifetime->value);
	}

	void RB_SetSplashUpdateParams(const weatherObject_t *ws, const float (*slotZones)[2],
		UniformDataWriter& uniformDataWriter, SamplerBindingsWriter& samplerBindingsWriter)
	{
		const weatherSystem_t& weather = *tr.weatherSystem;
		const float texelWorld = weather.texelSizeWorld;
		const vec4_t params = {
			1.0f / RainSplashLifetime(),
			(float)MAX(ws->particleCount, 1),
			weather.depthRangeWorld,
			0.0f
		};
		const vec4_t params2 = {
			1.0f / (float)tr.weatherDepthImage->width,
			// neighbour rise that is still a slope or a stair, not an edge
			MAX(3.0f * texelWorld, 32.0f),
			// snapping error allowed: about a texel of a 45 degree slope
			4.0f + texelWorld,
			weather.surfaceMapValid ? 1.0f : 0.0f
		};
		uniformDataWriter.SetUniformVec4(UNIFORM_SPLASHPARAMS, params);
		uniformDataWriter.SetUniformVec4(UNIFORM_SPLASHPARAMS2, params2);
		uniformDataWriter.SetUniformVec2(UNIFORM_ZONEOFFSET, &slotZones[0][0], CHUNK_COUNT);
		uniformDataWriter.SetUniformMatrix4x4(UNIFORM_WEATHERMVP, weather.weatherMVP);

		samplerBindingsWriter.AddStaticImage(tr.weatherDepthImage, TB_SHADOWMAP);
		samplerBindingsWriter.AddStaticImage(weather.surfaceMapValid ?
			tr.weatherSurfaceImage : tr.weatherDepthImage, TB_NORMALMAP);
	}

	// Once per simulated frame: remember where each slot was before a remap.
	void RB_TrackSplashSlots(const float (*slotZones)[2])
	{
		// the simulation uses the first view of the frame (RB_SimulateWeather)
		static int trackedFrame = -1;
		if (trackedFrame == (int)backEndData->realFrameNumber)
			return;
		trackedFrame = (int)backEndData->realFrameNumber;

		weatherSystem_t& weather = *tr.weatherSystem;
		const float now = backEnd.refdef.floatTime * 1000.0f;
		for (int slot = 0; slot < CHUNK_COUNT; ++slot)
		{
			if (!weather.splashSlotsValid)
			{
				VectorCopy2(slotZones[slot], weather.splashSlotPrevZone[slot]);
				weather.splashSlotRemapTime[slot] = -1e9f;
			}
			else if (weather.splashSlotZone[slot][0] != slotZones[slot][0] ||
				weather.splashSlotZone[slot][1] != slotZones[slot][1])
			{
				VectorCopy2(weather.splashSlotZone[slot], weather.splashSlotPrevZone[slot]);
				weather.splashSlotRemapTime[slot] = now;
			}
			VectorCopy2(slotZones[slot], weather.splashSlotZone[slot]);
		}
		weather.splashSlotsValid = true;
	}

	void RB_SimulateWeather(weatherObject_t *ws, const float (*slotZones)[2])
	{
		if (ws->vboLastUpdateFrame == backEndData->realFrameNumber ||
			tr.weatherSystem->frozen)
		{
			// Already simulated for this frame
			return;
		}

		// Intentionally switched. Previous frame's VBO would be in ws.vbo and
		// this frame's VBO would be ws.lastVBO.
		VBO_t *lastRainVBO = ws->vbo;
		VBO_t *rainVBO = ws->lastVBO;

		Allocator& frameAllocator = *backEndData->perFrameMemory;

		DrawItem item = {};
		item.renderState.transformFeedback = true;
		item.transformFeedbackBuffer = {rainVBO->vertexesVBO, 0, rainVBO->vertexesSize};
		// r_rainSplashes: the rain slot's impact layout has its own program
		item.program = ws->impactLayout ? &tr.weatherUpdateSplashShader : &tr.weatherUpdateShader;

		const size_t numAttribs = ws->numAttribs;
		item.numAttributes = numAttribs;
		item.attributes = ojkAllocArray<vertexAttribute_t>(
			*backEndData->perFrameMemory, numAttribs);
		memcpy(
			item.attributes,
			ws->attribsTemplate,
			sizeof(*item.attributes) * numAttribs);
		for (size_t i = 0; i < numAttribs; ++i)
			item.attributes[i].vbo = lastRainVBO;

		UniformDataWriter uniformDataWriter;
		uniformDataWriter.Start(item.program);
		if (ws->impactLayout)
		{
			SamplerBindingsWriter samplerBindingsWriter;
			RB_SetSplashUpdateParams(ws, slotZones, uniformDataWriter, samplerBindingsWriter);
			item.samplerBindings = samplerBindingsWriter.Finish(
				frameAllocator, &item.numSamplerBindings);
		}

		const vec2_t mapZExtents = {
			tr.world->bmodels[0].bounds[0][2],
			tr.world->bmodels[0].bounds[1][2]
		};
		const float frictionInverse = 0.7f;
		vec3_t envForce = {
			tr.weatherSystem->windDirection[0] * frictionInverse,
			tr.weatherSystem->windDirection[1] * frictionInverse,
			-ws->gravity * frictionInverse
		};
		// Mirror the update shader's velocity mix, including the XY reset on
		// respawn. These are bounds, not copies of per-particle state.
		const float deltaTime = backEnd.refdef.frameTime;
		const float mixFactor = deltaTime * 0.002f;
		if (!std::isfinite(mixFactor) || mixFactor < 0.0f || mixFactor > 1.0f ||
			!std::isfinite(envForce[0]) || !std::isfinite(envForce[1]) ||
			!std::isfinite(envForce[2]) || envForce[2] > 0.0f)
		{
			ws->velocityBoundsReliable = false;
		}
		else if (ws->velocityBoundsReliable)
		{
			for (int axis = 0; axis < 2; ++axis)
			{
				const float force = std::fabs(envForce[axis]);
				ws->maxHorizontalVelocity[axis] = std::max(force,
					(1.0f - mixFactor) * ws->maxHorizontalVelocity[axis] +
					mixFactor * force);
			}
			ws->minDownwardVelocity =
				(1.0f - mixFactor) * ws->minDownwardVelocity +
				mixFactor * std::fabs(envForce[2]);
			ws->maxVerticalVelocity = std::max(ws->maxVerticalVelocity,
				std::fabs(envForce[2]));
		}
		vec4_t randomOffset = {
			Q_flrand(-4.0f, 4.0f),
			Q_flrand(-4.0f, 4.0f),
			Q_flrand(0.0f, 1.0f),
			tr.world->bmodels[0].bounds[1][2] - backEnd.viewParms.ori.origin[2]
		};
		uniformDataWriter.SetUniformVec2(UNIFORM_MAPZEXTENTS, mapZExtents);
		uniformDataWriter.SetUniformVec3(UNIFORM_ENVFORCE, envForce);
		uniformDataWriter.SetUniformVec4(UNIFORM_RANDOMOFFSET, randomOffset);

		item.uniformData = uniformDataWriter.Finish(*backEndData->perFrameMemory);

		const byte currentFrameScene = backEndData->currentFrame->currentScene;
		const GLuint currentFrameUbo = backEndData->currentFrame->ubo[currentFrameScene];
		const UniformBlockBinding uniformBlockBindings[] = {
			{ currentFrameUbo, (size_t)tr.sceneUboOffset, UNIFORM_BLOCK_SCENE }
		};
		DrawItemSetUniformBlockBindings(
			item, uniformBlockBindings, frameAllocator);

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_POINTS;
		item.draw.params.arrays.numVertices = ws->particleCount * CHUNK_COUNT;

		// This is a bit dodgy. Push this towards the front of the queue so we
		// guarantee this happens before the actual drawing
		const uint32_t key = RB_CreateSortKey(item, 0, SS_ENVIRONMENT);
		RB_AddDrawItem(backEndData->currentPass, key, item);

		ws->vboLastUpdateFrame = backEndData->realFrameNumber;
		std::swap(ws->lastVBO, ws->vbo);
	}

	struct weatherDebugVertex_t
	{
		vec3_t position;
		vec4_t texcoord;
	};

	VBO_t *CreateWeatherBoundsVBO()
	{
		const int edges[12][2] = {
			{0, 1}, {2, 3}, {4, 5}, {6, 7},
			{0, 2}, {1, 3}, {4, 6}, {5, 7},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}
		};
		weatherDebugVertex_t vertices[24] = {};
		for (int edge = 0; edge < 12; ++edge)
		{
			for (int end = 0; end < 2; ++end)
			{
				weatherDebugVertex_t& vertex = vertices[edge * 2 + end];
				const int corner = edges[edge][end];
				for (int axis = 0; axis < 3; ++axis)
					vertex.position[axis] = (corner & (1 << axis)) ? 1.0f : -1.0f;
			}
		}
		return R_CreateVBO((byte *)vertices,
			sizeof(vertices), VBO_USAGE_STATIC, "Weather_chunk_bounds");
	}

	void RB_AddWeatherBounds(const vec3_t bounds[2], const vec4_t color)
	{
		VBO_t *boundsVBO = tr.weatherSystem->debugBoundsVBO;

		matrix_t boxModel, viewProjection, mvp;
		Matrix16Identity(boxModel);
		for (int axis = 0; axis < 3; ++axis)
		{
			boxModel[axis * 5] = (bounds[1][axis] - bounds[0][axis]) * 0.5f;
			boxModel[12 + axis] = (bounds[0][axis] + bounds[1][axis]) * 0.5f;
		}
		Matrix16Multiply(backEnd.viewParms.projectionMatrix,
			backEnd.viewParms.world.modelViewMatrix, viewProjection);
		Matrix16Multiply(viewProjection, boxModel, mvp);

		Allocator& frameAllocator = *backEndData->perFrameMemory;
		DrawItem item = {};
		item.program = &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES];
		item.renderState.stateBits = GLS_DEPTHTEST_DISABLE |
			GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
		item.renderState.cullType = CT_TWO_SIDED;
		item.renderState.depthRange = { 0.0f, 1.0f };
		const vertexAttribute_t attributes[] = {
			{ boundsVBO, ATTR_INDEX_POSITION, 3, GL_FALSE, GL_FLOAT,
				GL_FALSE, sizeof(weatherDebugVertex_t), offsetof(weatherDebugVertex_t, position), 0 },
			{ boundsVBO, ATTR_INDEX_TEXCOORD0, 4, GL_FALSE, GL_FLOAT,
				GL_FALSE, sizeof(weatherDebugVertex_t), offsetof(weatherDebugVertex_t, texcoord), 0 }
		};
		DrawItemSetVertexAttributes(item, attributes, ARRAY_LEN(attributes), frameAllocator);

		UniformDataWriter uniforms;
		uniforms.Start(item.program);
		uniforms.SetUniformMatrix4x4(UNIFORM_MODELVIEWPROJECTIONMATRIX, mvp);
		uniforms.SetUniformVec4(UNIFORM_COLOR, color);
		item.uniformData = uniforms.Finish(frameAllocator);
		SamplerBindingsWriter samplers;
		samplers.AddStaticImage(tr.whiteImage, TB_DIFFUSEMAP);
		item.samplerBindings = samplers.Finish(frameAllocator, &item.numSamplerBindings);

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_LINES;
		item.draw.params.arrays.numVertices = 24;
		RB_AddDrawItem(backEndData->currentPass,
			RB_CreateSortKey(item, 15, SS_SEE_THROUGH), item);
	}

	// r_rainStreaks: streaks follow the particle velocity relative to the
	// camera. The velocity of the scene camera (not of a portal or mirror
	// view; world units per ms, like the particle velocity) is measured once
	// per frame and smoothed so frame time jitter does not shake the rain;
	// teleports and respawns reset it.
	const float RAIN_MAX_CAMERA_SPEED = 1.0f;

	void RB_RainCameraVelocity(vec3_t velocity)
	{
		static vec3_t smoothed = {};
		static vec3_t lastOrigin = {};
		static int lastFrame = -1;
		static bool valid = false;

		if (lastFrame != backEndData->realFrameNumber)
		{
			const float dt = backEnd.refdef.frameTime;
			vec3_t delta;
			VectorSubtract(backEnd.refdef.vieworg, lastOrigin, delta);
			const float speed = dt > 0.0f ? VectorLength(delta) / dt : 0.0f;
			if (!valid || lastFrame != backEndData->realFrameNumber - 1 ||
				!std::isfinite(speed) || dt <= 0.0f || dt > 250.0f || speed > 3.0f)
			{
				VectorClear(smoothed);
			}
			else
			{
				const float blend = 1.0f - expf(-dt / 80.0f);
				for (int axis = 0; axis < 3; ++axis)
					smoothed[axis] += (delta[axis] / dt - smoothed[axis]) * blend;
				const float length = VectorLength(smoothed);
				if (length > RAIN_MAX_CAMERA_SPEED)
					VectorScale(smoothed, RAIN_MAX_CAMERA_SPEED / length, smoothed);
			}
			VectorCopy(backEnd.refdef.vieworg, lastOrigin);
			lastFrame = backEndData->realFrameNumber;
			valid = true;
		}
		VectorCopy(smoothed, velocity);
	}

	// Largest half length the rain geometry shader can produce (vertical),
	// see EmitRain in weather.glsl: preset * cvar * variation * speed factor.
	float RainMaxHalfLength(const weatherObject_t *weatherObject)
	{
		return std::fabs(weatherObject->size[1]) * r_rainStreakLength->value * 1.15f * 2.0f;
	}

	struct rainSplashStats_t
	{
		int draws;
		int culled;
	};

	// r_rainSplashes: one GL_POINTS draw per visible VBO chunk slot through
	// weatherSplash.glsl, which emits geometry for live impacts only.
	void RB_AddRainSplashes(const weatherObject_t *weatherObject,
		const float (*viewSlotZones)[2], rainSplashStats_t& stats)
	{
		weatherSystem_t& weather = *tr.weatherSystem;
		Allocator& frameAllocator = *backEndData->perFrameMemory;
		const int debugMode = Com_Clampi(0, 3, r_rainSplashDebug->integer);
		const float size = Com_Clamp(1.0f, 64.0f, r_rainSplashSize->value);
		const float lifetime = RainSplashLifetime();
		// splashes are a few units wide: gone well before the rain fades
		const float fadeDistance = debugMode ? weatherObject->fadeDistance :
			MIN(1500.0f, weatherObject->fadeDistance);

		DrawItem item = {};
		// premultiplied over; debug views opaque, impact points through walls
		item.renderState.stateBits = debugMode == 0 ?
			GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA :
			debugMode == 1 ? GLS_DEPTHTEST_DISABLE : GLS_DEPTHFUNC_LESS;
		item.renderState.cullType = CT_TWO_SIDED;
		item.renderState.depthRange = { 0.0f, 1.0f };
		item.program = &tr.weatherSplashShader;

		const size_t numAttribs = weatherObject->numAttribs;
		item.numAttributes = numAttribs;
		item.attributes = ojkAllocArray<vertexAttribute_t>(frameAllocator, numAttribs);
		memcpy(item.attributes, weatherObject->attribsTemplate,
			sizeof(*item.attributes) * numAttribs);
		for (size_t i = 0; i < numAttribs; ++i)
			item.attributes[i].vbo = weatherObject->vbo;

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_POINTS;
		item.draw.params.arrays.numVertices = weatherObject->particleCount;

		const byte currentFrameScene = backEndData->currentFrame->currentScene;
		const GLuint currentFrameUbo = backEndData->currentFrame->ubo[currentFrameScene];
		const UniformBlockBinding uniformBlockBindings[] = {
			{ currentFrameUbo, (size_t)tr.cameraUboOffsets[tr.viewParms.currentViewParm], UNIFORM_BLOCK_CAMERA },
			{ currentFrameUbo, (size_t)tr.sceneUboOffset, UNIFORM_BLOCK_SCENE }
		};
		DrawItemSetUniformBlockBindings(item, uniformBlockBindings, frameAllocator);

		// the rain's light (r_rainStreakLighting): merged light grid, else the sun
		image_t *grid = r_rainStreakLighting->value > 0.0f && tr.world->volumetricLightMaps[0] ?
			tr.world->volumetricLightMaps[0] : nullptr;
		vec4_t light = { 1.0f, 1.0f, 1.0f, grid ? 1.0f : 0.0f };
		if (r_rainStreakLighting->value > 0.0f && !grid)
		{
			vec3_t sunLight;
			VectorMA(backEnd.refdef.sunAmbCol, 0.5f, backEnd.refdef.sunCol, sunLight);
			if (sunLight[0] + sunLight[1] + sunLight[2] > 1e-3f)
				VectorCopy(sunLight, light);
		}

		SamplerBindingsWriter samplerBindingsWriter;
		samplerBindingsWriter.AddStaticImage(tr.weatherDepthImage, TB_SHADOWMAP);
		samplerBindingsWriter.AddStaticImage(grid ? grid : tr.whiteImage3D, TB_LIGHTMAP);
		item.samplerBindings = samplerBindingsWriter.Finish(
			frameAllocator, &item.numSamplerBindings);

		const vec4_t params = { size, Com_Clamp(0.0f, 4.0f, r_rainSplashOpacity->value),
			fadeDistance, (float)debugMode };
		const vec4_t params2 = { weather.texelSizeWorld, weather.depthRangeWorld,
			backEnd.refdef.frameTime, 1.0f / lifetime };

		// an upper bound for r_rainSplashDebug: every drop hits once per fall
		// through the map height (the respawn lifts it by exactly that)
		const float fallSpeed = weatherObject->gravity * 0.7f;
		const float mapHeight = tr.world->bmodels[0].bounds[1][2] -
			tr.world->bmodels[0].bounds[0][2];
		weather.splashExpected = fallSpeed > 0.0f && mapHeight > 0.0f ?
			(float)(weatherObject->particleCount * CHUNK_COUNT) * lifetime * fallSpeed / mapHeight : 0.0f;

		const bool canCull = r_weatherCull->integer != 0 && debugMode < 2;
		const float now = backEnd.refdef.floatTime * 1000.0f;
		const float marginXY = 2.0f * size + 8.0f;
		const float marginZ = 3.0f * size + 8.0f;
		for (int slot = 0; slot < CHUNK_COUNT; ++slot)
		{
			// live impacts are where the simulation put them: the slot's zone
			// at simulation time, or its previous zone for a lifetime after a
			// remap
			const float *zone = weather.splashSlotsValid ?
				weather.splashSlotZone[slot] : viewSlotZones[slot];
			vec3_t bounds[2] = {
				{ zone[0] - HALF_CHUNK_EXTENDS - marginXY, zone[1] - HALF_CHUNK_EXTENDS - marginXY,
				  tr.world->bmodels[0].bounds[0][2] - marginZ },
				{ zone[0] + HALF_CHUNK_EXTENDS + marginXY, zone[1] + HALF_CHUNK_EXTENDS + marginXY,
				  tr.world->bmodels[0].bounds[1][2] + marginZ }
			};
			if (weather.splashSlotsValid && now - weather.splashSlotRemapTime[slot] < lifetime + 100.0f)
			{
				const float *prev = weather.splashSlotPrevZone[slot];
				for (int axis = 0; axis < 2; ++axis)
				{
					bounds[0][axis] = MIN(bounds[0][axis], prev[axis] - HALF_CHUNK_EXTENDS - marginXY);
					bounds[1][axis] = MAX(bounds[1][axis], prev[axis] + HALF_CHUNK_EXTENDS + marginXY);
				}
			}
			const bool culled = canCull && R_CullBoxView(bounds, &backEnd.viewParms) == CULL_OUT;
			if (r_weatherDebugChunks->integer != 0)
			{
				const vec4_t visibleColor = { 0.2f, 0.6f, 1.0f, 0.65f };
				const vec4_t culledColor = { 0.6f, 0.1f, 0.9f, 0.65f };
				RB_AddWeatherBounds(bounds, culled ? culledColor : visibleColor);
				if (backEndData->realFrameNumber % 60 == 0)
					ri.Printf(PRINT_ALL, "  splash slot %d zone (%.0f %.0f)%s %s\n", slot,
						zone[0], zone[1],
						weather.splashSlotsValid && now - weather.splashSlotRemapTime[slot] < lifetime + 100.0f ?
							" + previous zone" : "",
						culled ? "culled" : "drawn");
			}
			if (culled)
			{
				++stats.culled;
				continue;
			}

			UniformDataWriter uniformDataWriter;
			uniformDataWriter.Start(&tr.weatherSplashShader);
			// the particle positions of the debug views follow this view
			uniformDataWriter.SetUniformVec2(UNIFORM_ZONEOFFSET, viewSlotZones[slot][0], viewSlotZones[slot][1]);
			uniformDataWriter.SetUniformVec4(UNIFORM_COLOR, weatherObject->color);
			uniformDataWriter.SetUniformMatrix4x4(UNIFORM_WEATHERMVP, weather.weatherMVP);
			uniformDataWriter.SetUniformVec4(UNIFORM_SPLASHPARAMS, params);
			uniformDataWriter.SetUniformVec4(UNIFORM_SPLASHPARAMS2, params2);
			uniformDataWriter.SetUniformVec4(UNIFORM_RAINLIGHT, light);
			if (grid)
			{
				uniformDataWriter.SetUniformVec3(UNIFORM_LIGHTGRIDORIGIN, tr.world->lightGridOrigin);
				uniformDataWriter.SetUniformVec3(UNIFORM_LIGHTGRIDCELLINVERSESIZE, tr.world->lightGridInverseSize);
			}
			item.uniformData = uniformDataWriter.Finish(frameAllocator);
			item.draw.params.arrays.firstVertex = weatherObject->particleCount * slot;

			RB_AddDrawItem(backEndData->currentPass, RB_CreateSortKey(item, 15, SS_SEE_THROUGH), item);
			++stats.draws;
		}
	}
}

/*
r_rainSplashDebug: count what the splash geometry shader emits. Called by
RB_DrawItems around a run of weatherSplash draws. Only the first run of a
frame is measured; results are read once the GPU has them.
*/
void RB_RainSplashQuery(bool begin)
{
	weatherSystem_t *weather = tr.weatherSystem;
	if (!weather || !r_rainSplashDebug->integer)
		return;

	const unsigned frame = backEndData->realFrameNumber;
	if (!begin)
	{
		if (weather->splashQueryOpen)
		{
			qglEndQuery(GL_PRIMITIVES_GENERATED);
			weather->splashQueryOpen = 0;
		}
		return;
	}

	if (!weather->splashQueries[0])
		qglGenQueries(ARRAY_LEN(weather->splashQueries), weather->splashQueries);

	const int count = ARRAY_LEN(weather->splashQueries);
	const int current = frame % count;
	if (weather->splashQueryFrame[current] == frame + 1 || weather->splashQueryOpen)
		return;	// this frame is measured already

	// the oldest query: printed if the GPU is done with it, else skipped
	const int oldest = (frame + 1) % count;
	if (weather->splashQueryFrame[oldest] != 0)
	{
		GLint available = 0;
		qglGetQueryObjectiv(weather->splashQueries[oldest], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available && (int)frame - weather->splashLastPrint >= 60)
		{
			GLuint primitives = 0;
			qglGetQueryObjectuiv(weather->splashQueries[oldest], GL_QUERY_RESULT, &primitives);
			const int debugMode = r_rainSplashDebug->integer;
			ri.Printf(PRINT_ALL,
				"Rain splashes (frame %u): %u GS triangles = %u %s; at most %.0f live splashes if every column were exposed\n",
				weather->splashQueryFrame[oldest] - 1, primitives,
				debugMode == 1 ? primitives / 2 : primitives / 4,
				debugMode == 1 ? "impact markers" : debugMode >= 2 ? "(debug particles)" : "live splashes (2 quads each, fewer without spray)",
				weather->splashExpected);
			weather->splashLastPrint = (int)frame;
		}
		weather->splashQueryFrame[oldest] = 0;
	}

	// a query still in flight is never restarted
	if (weather->splashQueryFrame[current] != 0)
		return;
	qglBeginQuery(GL_PRIMITIVES_GENERATED, weather->splashQueries[current]);
	weather->splashQueryFrame[current] = frame + 1;
	weather->splashQueryOpen = current + 1;
}

void R_InitWeatherForMap()
{
	tr.weatherSystem->splashSlotsValid = false;
	for (int i = 0; i < NUM_WEATHER_TYPES; i++)
		if (tr.weatherSystem->weatherSlots[i].active)
			GenerateRainModel(tr.weatherSystem->weatherSlots[i], maxWeatherTypeParticles[i],
				i == WEATHER_RAIN, false);
	GenerateDepthMap();
}

void R_InitWeatherSystem()
{
	Com_Printf("Initializing weather system\n");
	tr.weatherSystem =
		(weatherSystem_t *)R_Malloc(sizeof(*tr.weatherSystem), TAG_R_TERRAIN, qtrue);
	tr.weatherSystem->debugBoundsVBO = CreateWeatherBoundsVBO();
	tr.weatherSystem->weatherSurface.surfaceType = SF_WEATHER;
	tr.weatherSystem->frozen = false;
	tr.weatherSystem->shaking = false;
	tr.weatherSystem->activeWeatherTypes = 0;
	tr.weatherSystem->constWindDirection[0] = .0f;
	tr.weatherSystem->constWindDirection[1] = .0f;

	CurrentWeatherBrushIndex = -1;
	tr.weatherSystem->depthMapValid = false;

	for (int i = 0; i < NUM_WEATHER_TYPES; i++)
		tr.weatherSystem->weatherSlots[i].active = false;
}

void R_ShutdownWeatherSystem()
{
	if (tr.weatherSystem != nullptr)
	{
		Com_Printf("Shutting down weather system\n");

		Z_Free(tr.weatherSystem);
		tr.weatherSystem = nullptr;
	}
	else
	{
		ri.Printf(PRINT_DEVELOPER,
			"Weather system shutdown requested, but it is already shut down.\n");
	}
}

/*
===============
WE_ParseVector
===============
*/
qboolean WE_ParseVector(const char **text, int count, float *v) {
	char	*token;
	int		i;

	// FIXME: spaces are currently required after parens, should change parseext...
	token = COM_ParseExt(text, qfalse);
	if (strcmp(token, "(")) {
		ri.Printf(PRINT_WARNING, "WARNING: missing parenthesis in weather effect\n");
		return qfalse;
	}

	for (i = 0; i < count; i++) {
		token = COM_ParseExt(text, qfalse);
		if (!token[0]) {
			ri.Printf(PRINT_WARNING, "WARNING: missing vector element in weather effect\n");
			return qfalse;
		}
		v[i] = atof(token);
	}

	token = COM_ParseExt(text, qfalse);
	if (strcmp(token, ")")) {
		ri.Printf(PRINT_WARNING, "WARNING: missing parenthesis in weather effect\n");
		return qfalse;
	}

	return qtrue;
}

void R_AddWeatherBrush(uint8_t numPlanes, vec4_t *planes)
{
	if (tr.weatherSystem->numWeatherBrushes >= (MAX_WEATHER_ZONES * 2))
	{
		ri.Printf(PRINT_WARNING, "Max weather brushes hit. Skipping new inside/outside brush\n");
		return;
	}
	tr.weatherSystem->weatherBrushes[tr.weatherSystem->numWeatherBrushes].numPlanes = numPlanes;
	memcpy(tr.weatherSystem->weatherBrushes[tr.weatherSystem->numWeatherBrushes].planes, planes, numPlanes * sizeof(vec4_t));

	tr.weatherSystem->numWeatherBrushes++;
}

void R_LoadWeatherImages()
{
	if (!tr.weatherSystem)
		return;

	// Image flags and type
	imgType_t type = IMGTYPE_COLORALPHA;
	int flags = IMGFLAG_CLAMPTOEDGE;
	if (tr.linearLight)
		flags |= IMGFLAG_SRGB;

	if (tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
	{
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].drawImage = R_FindImageFile("gfx/world/rain.jpg", type, flags);
		// r_rainStreakLighting reads the merged light grid (built at load only
		// for volumetric fog). Small, so built for every rain map, which
		// keeps r_rainStreaks switchable at run time.
		if (tr.world)
			R_BuildLightGridColorTexture(tr.world);
	}
	if (tr.weatherSystem->weatherSlots[WEATHER_SNOW].active)
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].drawImage = R_FindImageFile("gfx/effects/snowflake1", type, flags);
	if (tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].active)
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].drawImage = R_FindImageFile("gfx/effects/snowpuff1", type, flags);
	if (tr.weatherSystem->weatherSlots[WEATHER_SAND].active)
		tr.weatherSystem->weatherSlots[WEATHER_SAND].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);
	if (tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
		tr.weatherSystem->weatherSlots[WEATHER_FOG].drawImage = R_FindImageFile("gfx/effects/alpha_smoke2b", type, flags);
}

void RE_WorldEffectCommand(const char *command)
{
	if (!command)
	{
		return;
	}
#ifndef REND2_SP
	COM_BeginParseSession("RE_WorldEffectCommand");
#else
	COM_BeginParseSession();
#endif
	const char	*token;//, *origCommand;

	token = COM_ParseExt(&command, qfalse);

	if (!token)
	{
		return;
	}

	//Die - clean up the whole weather system -rww
	if (Q_stricmp(token, "die") == 0)
	{
		for (int i = 0; i < NUM_WEATHER_TYPES; i++)
			tr.weatherSystem->weatherSlots[i].active = false;
		tr.weatherSystem->activeWeatherTypes = 0;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_NONE;
		tr.weatherSystem->frozen = false;
		return;
	}

	// Clear - Removes All Particle Clouds And Wind Zones
	//----------------------------------------------------
	else if (Q_stricmp(token, "clear") == 0)
	{
		for (int i = 0; i < NUM_WEATHER_TYPES; i++)
			tr.weatherSystem->weatherSlots[i].active = false;
		tr.weatherSystem->activeWeatherTypes = 0;
		tr.weatherSystem->activeWindObjects = 0;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_NONE;
		tr.weatherSystem->frozen = false;
	}

	// Freeze / UnFreeze - Stops All Particle Motion Updates
	//--------------------------------------------------------
	else if (Q_stricmp(token, "freeze") == 0)
	{
		tr.weatherSystem->frozen = !tr.weatherSystem->frozen;
	}

	//// Add a zone
	////---------------
	else if (Q_stricmp(token, "zone") == 0)
	{
		ri.Printf(PRINT_DEVELOPER, "Weather zones aren't used in rend2, but inside/outside brushes\n");
	}

	// Basic Wind
	//------------
	else if (Q_stricmp(token, "wind") == 0)
	{
		windObject_t *currentWindObject = &tr.weatherSystem->windSlots[tr.weatherSystem->activeWindObjects];
		currentWindObject->chanceOfDeadTime = 0.3f;
		currentWindObject->deadTimeMinMax[0] = 1000.0f;
		currentWindObject->deadTimeMinMax[1] = 3000.0f;
		currentWindObject->maxVelocity[0] = 1.5f;
		currentWindObject->maxVelocity[1] = 1.5f;
		currentWindObject->maxVelocity[2] = 0.01f;
		currentWindObject->minVelocity[0] = -1.5f;
		currentWindObject->minVelocity[1] = -1.5f;
		currentWindObject->minVelocity[2] = -0.01f;
		currentWindObject->targetVelocityTimeRemaining = 0;
		tr.weatherSystem->activeWindObjects++;
	}

	// Constant Wind
	//---------------
	else if (Q_stricmp(token, "constantwind") == 0)
	{
		vec3_t parsedWind;
		vec3_t defaultWind = { 0.f, 0.8f, 0.f };
		if (!WE_ParseVector(&command, 3, parsedWind))
			VectorAdd(
				tr.weatherSystem->constWindDirection,
				defaultWind,
				tr.weatherSystem->constWindDirection);
		else
			VectorMA(
				tr.weatherSystem->constWindDirection,
				0.001f,
				parsedWind,
				tr.weatherSystem->constWindDirection);
	}

	// Gusting Wind
	//--------------
	else if (Q_stricmp(token, "gustingwind") == 0)
	{
		windObject_t *currentWindObject = &tr.weatherSystem->windSlots[tr.weatherSystem->activeWindObjects];
		currentWindObject->chanceOfDeadTime = 0.3f;
		currentWindObject->deadTimeMinMax[0] = 2000.0f;
		currentWindObject->deadTimeMinMax[1] = 4000.0f;
		currentWindObject->maxVelocity[0] = 3.0f;
		currentWindObject->maxVelocity[1] = 3.0f;
		currentWindObject->maxVelocity[2] = 0.1f;
		currentWindObject->minVelocity[0] = -3.0f;
		currentWindObject->minVelocity[1] = -3.0f;
		currentWindObject->minVelocity[2] = -0.1f;
		currentWindObject->targetVelocityTimeRemaining = 0;
		tr.weatherSystem->activeWindObjects++;
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "lightrain") == 0)
	{
		/*nCloud.Initialize(500, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 1000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_LIGHT;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "rain") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 2000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_NORMAL;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "acidrain") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 2.0f;
		nCloud.mGravity = 2000.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 100.0f;

		nCloud.mColor[0] = 0.34f;
		nCloud.mColor[1] = 0.70f;
		nCloud.mColor[2] = 0.34f;
		nCloud.mColor[3] = 0.70f;

		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 2000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_ACID;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 2.0f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		tr.weatherSystem->pain = 0.1f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.34f, 0.7f, 0.34f, 0.7f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.7f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Rain Storm
	//---------------------
	else if (Q_stricmp(token, "heavyrain") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/world/rain.jpg", 3);
		nCloud.mHeight = 80.0f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 2800.0f;
		nCloud.mFilterMode = 1;
		nCloud.mBlendMode = 1;
		nCloud.mFade = 15.0f;
		nCloud.mColor = 0.5f;
		nCloud.mOrientWithVelocity = true;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].particleCount = 5000;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].active = true;
		tr.weatherSystem->rainSubtype = RAIN_WEATHER_HEAVY;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity = 2.8f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_RAIN].size[1] = 14.0f;

		tr.weatherSystem->weatherSlots[WEATHER_RAIN].velocityOrientationScale = 1.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_RAIN].color, 0.5f, 0.5f, 0.5f, 0.5f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color,
			0.5f,
			tr.weatherSystem->weatherSlots[WEATHER_RAIN].color);
	}

	// Create A Snow Storm
	//---------------------
	else if (Q_stricmp(token, "snow") == 0)
	{
		/*nCloud.Initialize(1000, "gfx/effects/snowflake1.bmp");
		nCloud.mBlendMode = 1;
		nCloud.mRotationChangeNext = 0;
		nCloud.mColor = 0.75f;
		nCloud.mWaterParticles = true;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_SNOW].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].particleCount = 1000;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].gravity = 0.3f;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].fadeDistance = 6000.0f;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].size[0] = 1.5f;
		tr.weatherSystem->weatherSlots[WEATHER_SNOW].size[1] = 1.5f;

		tr.weatherSystem->weatherSlots[WEATHER_SNOW].velocityOrientationScale = 0.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SNOW].color, 0.75f, 0.75f, 0.75f, 0.75f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_SNOW].color,
			0.75f,
			tr.weatherSystem->weatherSlots[WEATHER_SNOW].color);
	}

	// Create A Some stuff
	//---------------------
	else if (Q_stricmp(token, "spacedust") == 0)
	{
		/*nCloud.Initialize(count, "gfx/effects/snowpuff1.tga");
		nCloud.mHeight = 1.2f;
		nCloud.mWidth = 1.2f;
		nCloud.mGravity = 0.0f;
		nCloud.mBlendMode = 1;
		nCloud.mRotationChangeNext = 0;
		nCloud.mColor = 0.75f;
		nCloud.mWaterParticles = true;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[0] = -1500.0f;
		nCloud.mSpawnRange.mMins[1] = -1500.0f;
		nCloud.mSpawnRange.mMins[2] = -1500.0f;
		nCloud.mSpawnRange.mMaxs[0] = 1500.0f;
		nCloud.mSpawnRange.mMaxs[1] = 1500.0f;
		nCloud.mSpawnRange.mMaxs[2] = 1500.0f;*/
		int count;
		token = COM_ParseExt(&command, qfalse);
		count = atoi(token);

		if (!tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].particleCount = count;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].fadeDistance = 3000.f;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].size[0] = 2.5f;
		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].size[1] = 2.5f;

		tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].velocityOrientationScale = 0.0f;

		

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color, 0.75f, 0.75f, 0.75f, 0.75f);
		VectorScale(
			tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color,
			0.75f,
			tr.weatherSystem->weatherSlots[WEATHER_SPACEDUST].color);
	}

	// Create A Sand Storm
	//---------------------
	else if (Q_stricmp(token, "sand") == 0)
	{
		/*nCloud.Initialize(400, "gfx/effects/alpha_smoke2b.tga");

		nCloud.mGravity = 0;
		nCloud.mWidth = 70;
		nCloud.mHeight = 70;
		nCloud.mColor[0] = 0.9f;
		nCloud.mColor[1] = 0.6f;
		nCloud.mColor[2] = 0.0f;
		nCloud.mColor[3] = 0.5f;
		nCloud.mFade = 5.0f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_SAND].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].particleCount = 400;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_SAND].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_SAND].velocityOrientationScale = 0.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_SAND].color, 0.9f, 0.6f, 0.0f, 0.5f);
	}

	// Create Blowing Clouds Of Fog
	//------------------------------
	else if (Q_stricmp(token, "fog") == 0)
	{
		/*nCloud.Initialize(60, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 70;
		nCloud.mHeight = 70;
		nCloud.mColor = 0.2f;
		nCloud.mFade = 5.0f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 60;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.2f, 0.2f, 0.2f, 0.2f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.2f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	// Create Heavy Rain Particle Cloud
	//-----------------------------------
	else if (Q_stricmp(token, "heavyrainfog") == 0)
	{
		/*nCloud.Initialize(70, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 100;
		nCloud.mHeight = 100;
		nCloud.mColor = 0.3f;
		nCloud.mFade = 1.0f;
		nCloud.mMass.mMax = 10.0f;
		nCloud.mMass.mMin = 5.0f;

		nCloud.mSpawnRange.mMins = -(nCloud.mSpawnPlaneDistance*1.25f);
		nCloud.mSpawnRange.mMaxs = (nCloud.mSpawnPlaneDistance*1.25f);
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 70;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2400.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.3f, 0.3f, 0.3f, 0.3f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.3f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	// Create Blowing Clouds Of Fog
	//------------------------------
	else if (Q_stricmp(token, "light_fog") == 0)
	{
		/*nCloud.Initialize(40, "gfx/effects/alpha_smoke2b.tga");
		nCloud.mBlendMode = 1;
		nCloud.mGravity = 0;
		nCloud.mWidth = 100;
		nCloud.mHeight = 100;
		nCloud.mColor[0] = 0.19f;
		nCloud.mColor[1] = 0.6f;
		nCloud.mColor[2] = 0.7f;
		nCloud.mColor[3] = 0.12f;
		nCloud.mFade = 0.10f;
		nCloud.mMass.mMax = 30.0f;
		nCloud.mMass.mMin = 10.0f;
		nCloud.mSpawnRange.mMins[2] = -150;
		nCloud.mSpawnRange.mMaxs[2] = 150;

		nCloud.mRotationChangeNext = 0;*/
		if (!tr.weatherSystem->weatherSlots[WEATHER_FOG].active)
			tr.weatherSystem->activeWeatherTypes++;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].particleCount = 70;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].active = true;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].gravity = 0.0f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].fadeDistance = 2000.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[0] = 300.f;
		tr.weatherSystem->weatherSlots[WEATHER_FOG].size[1] = 300.f;

		tr.weatherSystem->weatherSlots[WEATHER_FOG].velocityOrientationScale = 0.0f;

		VectorSet4(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.19f, 0.6f, 0.7f, 0.12f);
		VectorScale(tr.weatherSystem->weatherSlots[WEATHER_FOG].color, 0.12f, tr.weatherSystem->weatherSlots[WEATHER_FOG].color);
	}

	else if (Q_stricmp(token, "outsideshake") == 0)
	{
#ifdef REND2_SP
		tr.weatherSystem->shaking = true;
#else
		ri.Printf(PRINT_DEVELOPER, "outsideshake isn't supported in MP\n");
#endif
	}
	else if (Q_stricmp(token, "outsidepain") == 0)
	{
#ifdef REND2_SP
		tr.weatherSystem->pain = !tr.weatherSystem->pain;
#else
		ri.Printf(PRINT_DEVELOPER, "outsidepain isn't supported in MP\n");
#endif
	}
	else
	{
		ri.Printf(PRINT_ALL, "Weather Effect: Please enter a valid command.\n");
		ri.Printf(PRINT_ALL, "	die\n");
		ri.Printf(PRINT_ALL, "	clear\n");
		ri.Printf(PRINT_ALL, "	freeze\n");
		ri.Printf(PRINT_ALL, "	zone (mins) (maxs)\n");
		ri.Printf(PRINT_ALL, "	wind\n");
		ri.Printf(PRINT_ALL, "	constantwind (velocity)\n");
		ri.Printf(PRINT_ALL, "	gustingwind\n");
		//ri.Printf(PRINT_ALL, "	windzone (mins) (maxs) (velocity)\n");
		ri.Printf(PRINT_ALL, "	lightrain\n");
		ri.Printf(PRINT_ALL, "	rain\n");
		ri.Printf(PRINT_ALL, "	acidrain\n");
		ri.Printf(PRINT_ALL, "	heavyrain\n");
		ri.Printf(PRINT_ALL, "	snow\n");
		ri.Printf(PRINT_ALL, "	spacedust (count)\n");
		ri.Printf(PRINT_ALL, "	sand\n");
		ri.Printf(PRINT_ALL, "	fog\n");
		ri.Printf(PRINT_ALL, "	heavyrainfog\n");
		ri.Printf(PRINT_ALL, "	light_fog\n");
		ri.Printf(PRINT_ALL, "	outsideshake\n"); // not available in MP
		ri.Printf(PRINT_ALL, "	outsidepain\n"); // not available in MP
	}
#ifdef REND2_SP
	COM_EndParseSession();
#endif
	if (tr.world)
		R_LoadWeatherImages();
}

void R_WorldEffect_f(void)
{
	char temp[2048] = { 0 };
	ri.Cmd_ArgsBuffer(temp, sizeof(temp));
	RE_WorldEffectCommand(temp);
}

void R_AddWeatherSurfaces()
{
	assert(tr.weatherSystem);

	if (tr.weatherSystem->activeWeatherTypes == 0 &&
		r_debugWeather->integer == 0)
		return;

	R_AddDrawSurf(
		(surfaceType_t *)&tr.weatherSystem->weatherSurface,
		REFENTITYNUM_WORLD,
		tr.weatherInternalShader,
		0, /* fogIndex */
		qfalse, /* dlightMap */
		qfalse, /* postRender */
		0 /* cubemapIndex */
	);
}

void RB_SurfaceWeather( srfWeather_t *surf )
{
	const bool measureWeather = r_speeds->integer == 100;
	const auto cpuStart = measureWeather ? std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point{};
	int simulationVertices = 0;
	int renderedVertices = 0;
	int weatherDrawCalls = 0;
	int culledChunks = 0;

	assert(tr.weatherSystem);

	weatherSystem_t& ws = *tr.weatherSystem;
	assert(surf == &ws.weatherSurface);

	RB_EndSurface();

	const float numMinZonesX = std::floor((abs(tr.world->bmodels[0].bounds[0][0]) / CHUNK_EXTENDS) + 0.5f);
	const float numMinZonesY = std::floor((abs(tr.world->bmodels[0].bounds[0][1]) / CHUNK_EXTENDS) + 0.5f);
	vec3_t viewOrigin;
	VectorCopy(backEnd.viewParms.ori.origin, viewOrigin);
	float centerZoneOffsetX =
		std::floor((viewOrigin[0] / CHUNK_EXTENDS) + 0.5f);
	float centerZoneOffsetY =
		std::floor((viewOrigin[1] / CHUNK_EXTENDS) + 0.5f);

	GLint  zoneMapping[9];
	{
		int chunkIndex = 0;
		int currentIndex = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x, ++currentIndex)
			{
				chunkIndex  = ((int(centerZoneOffsetX + numMinZonesX) + x + 1) % 3 + 3) % 3;
				chunkIndex += (((int(centerZoneOffsetY + numMinZonesY) + y + 1) % 3 + 3) % 3) * 3;
				zoneMapping[currentIndex] = chunkIndex;
			}
		}
	}

	// r_rainSplashes: world XY offset of each VBO slot (zoneMapping inverted)
	float slotZones[CHUNK_COUNT][2];
	{
		int currentIndex = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x, ++currentIndex)
			{
				slotZones[zoneMapping[currentIndex]][0] = (centerZoneOffsetX + x) * CHUNK_EXTENDS;
				slotZones[zoneMapping[currentIndex]][1] = (centerZoneOffsetY + y) * CHUNK_EXTENDS;
			}
		}
	}
	RB_TrackSplashSlots(slotZones);
	rainSplashStats_t splashStats = {};

	// Get current global wind vector
	VectorCopy(tr.weatherSystem->constWindDirection, tr.weatherSystem->windDirection);
	for (int i = 0; i < tr.weatherSystem->activeWindObjects; i++)
	{
		windObject_t *windObject = &tr.weatherSystem->windSlots[i];
		RB_UpdateWindObject(windObject);
		VectorAdd(windObject->currentVelocity, tr.weatherSystem->windDirection, tr.weatherSystem->windDirection);
	}

	Allocator& frameAllocator = *backEndData->perFrameMemory;

	// Simulate and render all the weather zones
	for (int weatherType = 0; weatherType < NUM_WEATHER_TYPES; weatherType++)
	{
		weatherObject_t *weatherObject = &ws.weatherSlots[weatherType];
		if (!weatherObject->active)
			continue;

		// r_rainSplashes switches the rain records to the impact layout; only
		// at the frame's simulation, not between the views of a frame
		const bool splashes = RainSplashesEnabled(weatherType);
		if (weatherObject->vbo == nullptr)
			GenerateRainModel(
				tr.weatherSystem->weatherSlots[weatherType],
				maxWeatherTypeParticles[weatherType],
				weatherType == WEATHER_RAIN, splashes);
		else if (weatherObject->splashCapable && weatherObject->impactLayout != splashes &&
			weatherObject->vboLastUpdateFrame != backEndData->realFrameNumber)
			SwitchRainLayout(*weatherObject, splashes);
		const bool splashLayout = weatherObject->impactLayout;

		if (weatherObject->vboLastUpdateFrame != backEndData->realFrameNumber &&
			!tr.weatherSystem->frozen)
			simulationVertices += weatherObject->particleCount * CHUNK_COUNT;
		RB_SimulateWeather(weatherObject, slotZones);

		vec4_t viewInfo = {
			weatherObject->size[0],
			weatherObject->size[1],
			weatherObject->velocityOrientationScale,
			weatherObject->fadeDistance
		};

		// r_rainStreaks: the rain slot alone switches to lit, premultiplied
		// streaks; snow, spacedust, sand and fog keep the legacy path
		const bool rainStreaks = weatherType == WEATHER_RAIN &&
			r_rainStreaks->integer != 0 && tr.weatherSystem->depthMapValid;

		int stateBits = weatherType == WEATHER_SAND ?
			GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA :
			GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE;
		if (rainStreaks)
			stateBits = GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;

		DrawItem item = {};

		item.renderState.stateBits = stateBits;
		item.renderState.cullType = CT_TWO_SIDED;
		item.renderState.depthRange = { 0.0f, 1.0f };
		item.program = &tr.weatherShader;

		// weather.glsl reads position and velocity only
		const size_t numAttribs = 2;
		item.numAttributes = numAttribs;
		item.attributes = ojkAllocArray<vertexAttribute_t>(
			*backEndData->perFrameMemory, numAttribs);
		memcpy(
			item.attributes,
			weatherObject->attribsTemplate,
			sizeof(*item.attributes) * numAttribs);
		item.attributes[0].vbo = weatherObject->vbo;
		item.attributes[1].vbo = weatherObject->vbo;

		item.draw.type = DRAW_COMMAND_ARRAYS;
		item.draw.numInstances = 1;
		item.draw.primitiveType = GL_POINTS;
		item.draw.params.arrays.numVertices = weatherObject->particleCount;

		const byte currentFrameScene = backEndData->currentFrame->currentScene;
		const GLuint currentFrameUbo = backEndData->currentFrame->ubo[currentFrameScene];
		const UniformBlockBinding uniformBlockBindings[] = {
			{ currentFrameUbo, (size_t)tr.cameraUboOffsets[tr.viewParms.currentViewParm], UNIFORM_BLOCK_CAMERA },
			{ currentFrameUbo, (size_t)tr.sceneUboOffset, UNIFORM_BLOCK_SCENE }
		};
		DrawItemSetUniformBlockBindings(item, uniformBlockBindings, frameAllocator);

		// rain light: the merged light grid when the map has one, else the
		// sun's ambient + half its direct light, else the legacy brightness
		image_t *rainGrid = rainStreaks && tr.world->volumetricLightMaps[0] ?
			tr.world->volumetricLightMaps[0] : nullptr;
		vec4_t rainLight = { 1.0f, 1.0f, 1.0f, rainGrid ? 1.0f : 0.0f };
		if (rainStreaks && !rainGrid)
		{
			vec3_t sunLight;
			VectorMA(backEnd.refdef.sunAmbCol, 0.5f, backEnd.refdef.sunCol, sunLight);
			if (sunLight[0] + sunLight[1] + sunLight[2] > 1e-3f)
				VectorCopy(sunLight, rainLight);
		}

		SamplerBindingsWriter samplerBindingsWriter;
		samplerBindingsWriter.AddStaticImage(tr.weatherDepthImage, TB_SHADOWMAP);
		samplerBindingsWriter.AddStaticImage(
			weatherObject->drawImage != nullptr ? weatherObject->drawImage : tr.whiteImage,
			TB_DIFFUSEMAP);
		// always a 3D texture on the sampler3D unit, read only with a grid
		samplerBindingsWriter.AddStaticImage(rainGrid ? rainGrid : tr.whiteImage3D, TB_LIGHTMAP);
		item.samplerBindings = samplerBindingsWriter.Finish(
			frameAllocator, &item.numSamplerBindings);

		vec3_t cameraVelocity = {};
		vec4_t rainStreak = {}, rainShade = {};
		if (rainStreaks)
		{
			RB_RainCameraVelocity(cameraVelocity);
			const image_t *rainImage = weatherObject->drawImage;
			rainStreak[0] = r_rainStreakWidth->value;
			rainStreak[1] = r_rainStreakLength->value;
			rainStreak[2] = tr.weatherSystem->depthRangeWorld;
			rainStreak[3] = (rainImage && (rainImage->flags & IMGFLAG_SRGB)) ? 1.0f : 0.0f;
			rainShade[0] = r_rainStreakOpacity->value;
			rainShade[1] = r_rainStreakLighting->value;
			// world size of one pixel at distance 1
			rainShade[2] = 2.0f * tanf(DEG2RAD(backEnd.viewParms.fovY) * 0.5f) /
				(float)MAX(backEnd.viewParms.viewportHeight, 1);
			rainShade[3] = (float)r_rainStreakDebug->integer;
		}

		// The update shader now wraps local XY at +/-1000, so every VBO slot
		// stays inside its nominal zone even when the camera remaps the slots.
		// Expand that box by the geometry shader's width and velocity tilt.
		const float streakHeight = std::fabs(weatherObject->size[1]);
		const float streakWidth = std::fabs(weatherObject->size[0]);
		const float verticalVelocity = std::max(0.00001f,
			weatherObject->minDownwardVelocity *
			std::fabs(weatherObject->velocityOrientationScale));
		float tiltX = weatherObject->velocityOrientationScale != 0.0f ?
			streakHeight * weatherObject->maxHorizontalVelocity[0] / verticalVelocity : 0.0f;
		float tiltY = weatherObject->velocityOrientationScale != 0.0f ?
			streakHeight * weatherObject->maxHorizontalVelocity[1] / verticalVelocity : 0.0f;
		float marginWidth = streakWidth;
		float marginHeight = streakHeight;
		if (rainStreaks)
		{
			// the rain geometry shader bounds its tilt at 3:1 and clamps the
			// width to a pixel at most at the fade distance
			marginHeight = RainMaxHalfLength(weatherObject);
			tiltX = tiltY = 3.0f * marginHeight;
			marginWidth = std::max(streakWidth * r_rainStreakWidth->value * 1.15f,
				0.5f * weatherObject->fadeDistance * rainShade[2]);
		}
		const float marginX = marginWidth + tiltX + 8.0f;
		const float marginY = marginWidth + tiltY + 8.0f;
		const float marginZ = marginHeight + 8.0f +
			weatherObject->maxVerticalVelocity * 50.0f;
		const float mapHeight = tr.world->bmodels[0].bounds[1][2] -
			tr.world->bmodels[0].bounds[0][2];
		const bool canCull = r_weatherCull->integer != 0 &&
			weatherObject->velocityBoundsReliable &&
			std::isfinite(centerZoneOffsetX) && std::isfinite(centerZoneOffsetY) &&
			std::isfinite(mapHeight) &&
			std::isfinite(marginX) && std::isfinite(marginY) &&
			std::isfinite(marginZ) &&
			mapHeight > weatherObject->maxVerticalVelocity * 50.0f;
		const bool debugChunks = r_weatherDebugChunks->integer != 0 &&
			backEndData->realFrameNumber % 60 == 0;
		if (debugChunks)
			ri.Printf(PRINT_ALL,
			"Weather type %d: 3x3 zones (center *), slot, culling, rendered particles\n",
			weatherType);
		int currentIndex = 0;
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x, ++currentIndex)
			{
				const float zoneX = (centerZoneOffsetX + x) * CHUNK_EXTENDS;
				const float zoneY = (centerZoneOffsetY + y) * CHUNK_EXTENDS;
				vec3_t bounds[2] = {
					{ zoneX - HALF_CHUNK_EXTENDS - marginX,
					  zoneY - HALF_CHUNK_EXTENDS - marginY,
					  tr.world->bmodels[0].bounds[0][2] - marginZ },
					{ zoneX + HALF_CHUNK_EXTENDS + marginX,
					  zoneY + HALF_CHUNK_EXTENDS + marginY,
					  tr.world->bmodels[0].bounds[1][2] + marginZ }
				};
				const bool culled = canCull &&
					R_CullBoxView(bounds, &backEnd.viewParms) == CULL_OUT;
				if (debugChunks)
				{
					const char *status = canCull ? (culled ? "culled" : "visible") :
						(r_weatherCull->integer ? "fallback" : "disabled");
					ri.Printf(PRINT_ALL, "  %c(%+d,%+d) slot %d %s %d particles\n",
						x == 0 && y == 0 ? '*' : ' ', x, y,
						zoneMapping[currentIndex], status,
						culled ? 0 : weatherObject->particleCount);
					if (r_weatherDebugChunks->integer >= 2)
						ri.Printf(PRINT_ALL,
						"    AABB (%.1f %.1f %.1f) - (%.1f %.1f %.1f)\n",
						bounds[0][0], bounds[0][1], bounds[0][2],
						bounds[1][0], bounds[1][1], bounds[1][2]);
				}
				if (r_weatherDebugChunks->integer != 0)
				{
					const vec4_t visibleColor = { 0.1f, 0.9f, 0.2f, 0.65f };
					const vec4_t culledColor = { 0.95f, 0.15f, 0.1f, 0.65f };
					const vec4_t centerColor = { 1.0f, 0.9f, 0.1f, 0.9f };
					RB_AddWeatherBounds(bounds, x == 0 && y == 0 ? centerColor :
						culled ? culledColor : visibleColor);
				}
				if (culled)
				{
					++culledChunks;
					continue;
				}

				UniformDataWriter uniformDataWriter;
				uniformDataWriter.Start(&tr.weatherShader);
				uniformDataWriter.SetUniformVec2(
					UNIFORM_ZONEOFFSET,
					zoneX, zoneY);
				uniformDataWriter.SetUniformVec4(UNIFORM_COLOR, weatherObject->color);
				uniformDataWriter.SetUniformVec4(UNIFORM_VIEWINFO, viewInfo);
				uniformDataWriter.SetUniformMatrix4x4(UNIFORM_SHADOWMVP, tr.weatherSystem->weatherMVP);
				// set for every draw: a uniform left out keeps the value of
				// the previous weather type's draw
				uniformDataWriter.SetUniformInt(UNIFORM_WEATHERTYPE, rainStreaks ? 1 : 0);
				if (rainStreaks)
				{
					uniformDataWriter.SetUniformVec4(UNIFORM_RAINSTREAK, rainStreak);
					uniformDataWriter.SetUniformVec4(UNIFORM_RAINSHADE, rainShade);
					uniformDataWriter.SetUniformVec4(UNIFORM_RAINLIGHT, rainLight);
					uniformDataWriter.SetUniformVec3(UNIFORM_CAMERAVELOCITY, cameraVelocity);
					if (rainGrid)
					{
						uniformDataWriter.SetUniformVec3(UNIFORM_LIGHTGRIDORIGIN, tr.world->lightGridOrigin);
						uniformDataWriter.SetUniformVec3(UNIFORM_LIGHTGRIDCELLINVERSESIZE, tr.world->lightGridInverseSize);
					}
				}
				item.uniformData = uniformDataWriter.Finish(*backEndData->perFrameMemory);

				item.draw.params.arrays.firstVertex = weatherObject->particleCount * zoneMapping[currentIndex];

				uint32_t key = RB_CreateSortKey(item, 15, SS_SEE_THROUGH);
				RB_AddDrawItem(backEndData->currentPass, key, item);
				weatherDrawCalls++;
				renderedVertices += item.draw.params.arrays.numVertices;
			}
		}

		if (splashLayout)
			RB_AddRainSplashes(weatherObject, slotZones, splashStats);
	}
	if (measureWeather)
	{
		const double cpuMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - cpuStart).count();
		ri.Printf(PRINT_ALL,
			"Weather: %d draws, %d culled chunks, %d render vertices, %d TF vertices, "
			"%d splash draws, %d culled splash slots, RB_SurfaceWeather %.3f ms CPU\n",
			weatherDrawCalls, culledChunks, renderedVertices, simulationVertices,
			splashStats.draws, splashStats.culled, cpuMs);
	}
}

bool IsInsideBrush(const weatherBrushes_t* Brush, const vec3_t pos)
{
	// RBSP brushes actually store their bounding box in the first 6 planes! Nice
	const vec3_t mins = {
		-Brush->planes[0][3],
		-Brush->planes[2][3],
		-Brush->planes[4][3],
	};
	const vec3_t maxs = {
		Brush->planes[1][3],
		Brush->planes[3][3],
		Brush->planes[5][3],
	};

	return (pos[0] > mins[0] && pos[1] > mins[1] && pos[2] > mins[2]
		&& pos[0] < maxs[0] && pos[1] < maxs[1] && pos[2] < maxs[2]);
}

bool R_IsOutside(vec3_t pos)
{
	if (!tr.weatherSystem)
	{
		return false;
	}

	// check cashed brush first
	if (CurrentWeatherBrushIndex >= 0 && CurrentWeatherBrushIndex < tr.weatherSystem->numWeatherBrushes)
	{
		if (IsInsideBrush(&tr.weatherSystem->weatherBrushes[CurrentWeatherBrushIndex], pos))
		{
			return (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE);
		}
	}

	// check all weather brushes
	for (int i = 0; i < tr.weatherSystem->numWeatherBrushes; i++)
	{
		const weatherBrushes_t* WeatherBrush = &tr.weatherSystem->weatherBrushes[i];

		if (IsInsideBrush(WeatherBrush, pos))
		{
			CurrentWeatherBrushIndex = i;
			return (tr.weatherSystem->weatherBrushType == WEATHER_BRUSHES_OUTSIDE);
		}
	}

	CurrentWeatherBrushIndex = -1;
	return (tr.weatherSystem->weatherBrushType != WEATHER_BRUSHES_OUTSIDE);
}

bool R_IsShaking(vec3_t pos)
{
	return (tr.weatherSystem
		&& tr.weatherSystem->shaking
		&& R_IsOutside(pos));
}

bool R_GetWindVector(vec3_t windVector, vec3_t atPoint)
{
	// @TODO: need to process "Windzone" command

	if (!tr.weatherSystem)
	{
		return false;
	}

	VectorCopy(tr.weatherSystem->windDirection, windVector);
	VectorNormalize(windVector);
	// everything is processed in RB_SurfaceWeather, no need to add something here
	return (VectorLength(windVector) > 0.0f);
}

bool R_GetWindGusting(vec3_t atPoint)
{
	if (!tr.weatherSystem)
		return false;

	float windSpeed = VectorLength(tr.weatherSystem->windDirection);
	return (windSpeed > 1.0f);
}

float R_IsOutsideCausingPain(vec3_t pos)
{
	return (R_IsOutside(pos) && tr.weatherSystem->pain);
}

float R_GetChanceOfSaberFizz()
{
	float	chance = 0.0f;
	int		numWater = 0;
	if (tr.weatherSystem->weatherSlots[WEATHER_RAIN].active)
	{
		chance += (tr.weatherSystem->weatherSlots[WEATHER_RAIN].gravity / 20.0f);
		numWater++;
	}
	if (tr.weatherSystem->weatherSlots[WEATHER_SNOW].active)
	{
		chance += (tr.weatherSystem->weatherSlots[WEATHER_SNOW].gravity / 20.0f);
		numWater++;
	}
	if (numWater)
	{
		return (chance / numWater);
	}
	return 0.0f;
}

bool R_IsRaining()
{
	if (!tr.weatherSystem)
		return false;

	return tr.weatherSystem->weatherSlots[WEATHER_RAIN].active;
}

bool R_IsPuffing()
{
	return false;
}

/*
==============================================================================
Rain wetness (r_weatherWetness)

lightall reuses the static rain occlusion map of GenerateDepthMap: a surface
point is rain exposed where the rain particle test of weather.glsl would keep
a particle. The wet surface only changes the PBR inputs (roughness, diffuse,
normal) before any lighting, see ComputeRainExposure in lightall.glsl. Rain
is static per map, so there is no accumulation or drying over time.
==============================================================================
*/

qboolean R_WeatherWetnessEnabled(void)
{
	if (!r_weatherWetness || !r_weatherWetness->integer)
		return qfalse;

	// queried once: called for every lightall draw, the GPU does not change
	static GLint maxFragmentSamplers = -1;
	if (maxFragmentSamplers < 0)
		qglGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxFragmentSamplers);
	if (maxFragmentSamplers <= TB_WEATHERDEPTH)
	{
		static bool warned = false;
		if (!warned)
			ri.Printf(PRINT_WARNING, "r_weatherWetness: %d fragment texture units, %d needed, disabled\n",
				maxFragmentSamplers, TB_WEATHERDEPTH + 1);
		warned = true;
		return qfalse;
	}
	return qtrue;
}

// Why a stage stays dry, u_WeatherMaterial.w for r_weatherSurfaceDebug 31.
// Structural exclusions only: sky, portals, liquids, fog, blended / glow /
// uniformly emissive stages and the view weapon, plus weatherResponse 0.
typedef enum {
	WETEXCLUDE_NONE,
	WETEXCLUDE_SKY,
	WETEXCLUDE_PORTAL,
	WETEXCLUDE_LIQUID,			// water, slime, lava, fog volumes
	WETEXCLUDE_TRANSLUCENT,		// sort after opaque
	WETEXCLUDE_BLENDED,			// additive / blended stage
	WETEXCLUDE_GLOW,
	WETEXCLUDE_EMISSIVE,		// emissive over the whole surface (no emissive map)
	WETEXCLUDE_FIRSTPERSON,
	WETEXCLUDE_AUTHORED,		// weatherResponse 0
	WETEXCLUDE_PASS,			// depth / shadow pass, r_lightmap
} wetExclusion_t;

static const char *const wetExclusionNames[] = {
	"none", "sky", "portal", "liquid/fog", "translucent sort", "blended", "glow",
	"emissive", "first person", "weatherResponse 0", "pass"
};

static wetExclusion_t R_WetnessStageExclusion(const shader_t *shader, const shaderStage_t *pStage)
{
	if (shader->isSky || (shader->surfaceFlags & SURF_SKY))
		return WETEXCLUDE_SKY;
	if (shader->isPortal)
		return WETEXCLUDE_PORTAL;
	if (shader->contentFlags & (CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_FOG))
		return WETEXCLUDE_LIQUID;
	if (shader->sort > SS_OPAQUE)
		return WETEXCLUDE_TRANSLUCENT;
	const uint32_t dstBlend = pStage->stateBits & GLS_DSTBLEND_BITS;
	if (dstBlend != 0 && dstBlend != GLS_DSTBLEND_ZERO)
		return WETEXCLUDE_BLENDED;
	if (pStage->glow)
		return WETEXCLUDE_GLOW;
	// emissiveColor / emissiveScale without an emissive map: a light panel
	// glowing over its whole surface, a water film on it reads as plastic
	if (pStage->emissive && pStage->bundle[TB_EMISSIVEMAP].image[0] == tr.whiteImage &&
		pStage->emissiveIntensity * VectorLength(pStage->emissiveColor) > 0.0f)
		return WETEXCLUDE_EMISSIVE;
	if (backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity &&
		(backEnd.currentEntity->e.renderfx & RF_FIRST_PERSON))
		return WETEXCLUDE_FIRSTPERSON;
	if (pStage->weatherScale[0] <= 0.0f && pStage->weatherScale[1] <= 0.0f && pStage->weatherScale[2] <= 0.0f)
		return WETEXCLUDE_AUTHORED;
	return WETEXCLUDE_NONE;
}

// The command requests one frame of drawn materials with a non-default response.
static bool weatherMaterialListRequested = false;
static int weatherMaterialListFrame = -1;

void R_WeatherMaterialList_f(void)
{
	R_IssuePendingRenderCommands();
	weatherMaterialListRequested = true;
	weatherMaterialListFrame = -1;
	ri.Printf(PRINT_ALL, "Weather material list requested for the next rain rendering frame.\n");
}
static void R_WeatherMaterialPrint(const shader_t *shader, const vec3_t scale, wetExclusion_t reason)
{
	int &printFrame = weatherMaterialListFrame;
	static byte printed[MAX_SHADERS];
	if (!weatherMaterialListRequested)
	{
		printFrame = -1;
		return;
	}
	if (printFrame < 0)
	{
		printFrame = tr.frameCount;
		memset(printed, 0, sizeof(printed));
		ri.Printf(PRINT_ALL, "weather material response (wetness puddle runoff, exclusion):\n");
	}
	else if (printFrame != tr.frameCount)
	{
		weatherMaterialListRequested = false;
		printFrame = -1;
		return;
	}
	if (shader->index < 0 || shader->index >= MAX_SHADERS || printed[shader->index])
		return;
	if (reason == WETEXCLUDE_NONE && scale[0] == 1.0f && scale[1] == 1.0f && scale[2] == 1.0f)
		return;
	printed[shader->index] = 1;
	ri.Printf(PRINT_ALL, "  %-48s %.2f %.2f %.2f  %s\n", shader->name,
		scale[0], scale[1], scale[2], wetExclusionNames[reason]);
}

void RB_WeatherWetnessBind(const shader_t *shader, const shaderStage_t *pStage,
	UniformDataWriter &uniformDataWriter, SamplerBindingsWriter &samplerBindingsWriter)
{
	const weatherSystem_t *ws = tr.weatherSystem;
	const bool raining = ws && ws->depthMapValid && tr.weatherDepthImage &&
		ws->weatherSlots[WEATHER_RAIN].active;
	if (!raining)
	{
		// strength 0: the shader never samples u_WeatherDepthMap
		const vec4_t off = {};
		uniformDataWriter.SetUniformVec4(UNIFORM_WETNESSPARAMS, off);
		uniformDataWriter.SetUniformVec4(UNIFORM_WETNESSPARAMS2, off);
		uniformDataWriter.SetUniformVec4(UNIFORM_PUDDLEPARAMS, off);
		uniformDataWriter.SetUniformVec4(UNIFORM_RUNOFFPARAMS, off);
		return;
	}

	wetExclusion_t exclusion = R_WetnessStageExclusion(shader, pStage);
	if (exclusion == WETEXCLUDE_NONE && (backEnd.depthFill ||
		(backEnd.viewParms.flags & VPF_DEPTHSHADOW) || r_lightmap->integer))
		exclusion = WETEXCLUDE_PASS;
	const bool eligible = exclusion == WETEXCLUDE_NONE;

	// weatherResponse scales of the material. Cloth soaks but never holds a
	// standing mirror puddle, and sheds less of a visible film.
	vec3_t scale;
	VectorCopy(pStage->weatherScale, scale);
	if (pStage->cloth || pStage->materialClass == MATCLASS_CLOTH)
	{
		scale[1] = 0.0f;
		scale[2] *= 0.3f;
	}
	R_WeatherMaterialPrint(shader, scale, exclusion);
	const vec4_t material = { scale[0], scale[1], scale[2], (float)exclusion };
	uniformDataWriter.SetUniformVec4(UNIFORM_WEATHERMATERIAL, material);

	// strength < 0 marks an ineligible draw for r_weatherSurfaceDebug 2
	const float strength = eligible ?
		Com_Clamp(0.0f, 1.0f, r_weatherWetnessStrength->value * scale[0]) : -1.0f;
	// per material class response (tr_autopbr.cpp): cloth only darkens,
	// armor / metal get glossier
	vec3_t response;
	R_WetnessResponse(pStage, response);
	const vec4_t params = { strength, response[1], response[0], response[2] };
	// characters and props stand in the rain: their sides get almost as wet
	// as their tops, world walls about half
	const bool entity = backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity;
	const vec4_t params3 = {
		entity ? Com_Clamp(0.0f, 1.0f, r_weatherWetnessEntityFacing->value) : 0.5f,
		pStage->materialClass == MATCLASS_GENERIC ? 1.0f : 0.0f,
		(float)pStage->materialClass,
		0.0f
	};
	uniformDataWriter.SetUniformVec4(UNIFORM_WETNESSPARAMS3, params3);
	const vec4_t params2 = {
		MAX(r_weatherWetnessBias->value, 0.0f) / ws->depthRangeWorld,	// world units -> depth
		0.5f * ws->texelSizeWorld,
		(float)r_weatherSurfaceDebug->integer,
		0.5f * (float)glConfig.vidWidth
	};
	uniformDataWriter.SetUniformVec4(UNIFORM_WETNESSPARAMS, params);
	uniformDataWriter.SetUniformVec4(UNIFORM_WETNESSPARAMS2, params2);

	// puddles: static world geometry only, never entities (characters,
	// weapons, props, movers). coverage < 0 marks an ineligible draw for
	// r_weatherSurfaceDebug 10, 0 turns the layer off.
	float coverage = 0.0f;
	if (r_weatherPuddles->integer)
	{
		const bool world = !backEnd.currentEntity || backEnd.currentEntity == &tr.worldEntity;
		coverage = (eligible && world && scale[1] > 0.0f) ?
			Com_Clamp(0.001f, 1.0f, r_weatherPuddleCoverage->value * scale[1]) : -1.0f;
	}
	float slopeMin = 0.90f, slopeMax = 0.98f;
	sscanf(r_weatherPuddleSlope->string, "%f %f", &slopeMin, &slopeMax);
	slopeMin = Com_Clamp(0.0f, 0.999f, slopeMin);
	slopeMax = Com_Clamp(slopeMin + 0.001f, 1.0f, slopeMax);
	const vec4_t puddle = {
		coverage,
		Com_Clamp(0.02f, 1.0f, r_weatherPuddleRoughness->value),
		slopeMin,
		slopeMax
	};
	const vec4_t puddle2 = { 1.0f / MAX(r_weatherPuddleScale->value, 8.0f), 0.0f, 0.0f, 0.0f };

	// height aware puddles: only materials with a normalHeightMap (the
	// USE_PARALLAXMAP shaders) and a relief of at least a few steps, the
	// others keep the macro mask (y = 0)
	vec4_t puddleHeight = {
		0.0f,
		0.0f,
		Com_Clamp(0.01f, 0.5f, r_weatherPuddleHeightSoftness->value),
		Com_Clamp(-1.0f, 1.0f, r_weatherPuddleWaterLevelBias->value)
	};
	const image_t *heightImage = pStage->bundle[TB_NORMALMAP].image[0];
	if (r_weatherPuddleUseHeightMap->integer && heightImage && heightImage->type == IMGTYPE_NORMALHEIGHT &&
		heightImage->heightRange[1] - heightImage->heightRange[0] >= 4.0f / 255.0f)
	{
		puddleHeight[0] = heightImage->heightRange[0];
		puddleHeight[1] = 1.0f / (heightImage->heightRange[1] - heightImage->heightRange[0]);
	}
	uniformDataWriter.SetUniformVec4(UNIFORM_PUDDLEPARAMS, puddle);
	uniformDataWriter.SetUniformVec4(UNIFORM_PUDDLEPARAMS2, puddle2);
	uniformDataWriter.SetUniformVec4(UNIFORM_PUDDLEHEIGHT, puddleHeight);

	// rain ripples on the puddles (lightall PuddleRipples): the ring clock is
	// integrated here once per frame, so r_weatherPuddleRippleRate changes never
	// jump the phase, and wrapped at 256 cycles for float precision (the
	// shader wraps its ring index the same way, so the wrap is seamless)
	static double rippleClock = 0.0;
	static int rippleFrame = -1;
	static float rippleTime = 0.0f;
	if (rippleFrame != tr.frameCount)
	{
		const float dt = backEnd.refdef.floatTime - rippleTime;
		if (rippleFrame >= 0 && dt > 0.0f)
			rippleClock = fmod(rippleClock + MIN(dt, 0.1f) * Com_Clamp(0.0f, 8.0f, r_weatherPuddleRippleRate->value), 256.0);
		rippleFrame = tr.frameCount;
		rippleTime = backEnd.refdef.floatTime;
	}
	// share of the cells that ring each cycle: light rain fewer than a
	// downpour (1000 / 2000 particles); the shader skips 1 cycle in 4 more
	const float rainAmount = Com_Clamp(0.3f, 1.0f, (float)ws->weatherSlots[WEATHER_RAIN].particleCount / 2000.0f);
	const vec4_t ripple = {
		(r_weatherPuddleRipples->integer && coverage > 0.0f) ? Com_Clamp(0.0f, 2.0f, r_weatherPuddleRippleStrength->value) : 0.0f,
		1.0f / Com_Clamp(4.0f, 256.0f, r_weatherPuddleRippleScale->value),
		(float)rippleClock,
		rainAmount
	};
	uniformDataWriter.SetUniformVec4(UNIFORM_PUDDLERIPPLE, ripple);

	// runoff (lightall RunoffStreaks): gravity driven film on slopes and
	// walls. World geometry, and entities with r_weatherRunoffEntities, whose
	// pattern frame follows their origin and yaw so it does not swim on them.
	float runoffStrength = 0.0f;
	if (r_weatherRunoff->integer)
	{
		const bool world = !backEnd.currentEntity || backEnd.currentEntity == &tr.worldEntity;
		runoffStrength = (eligible && (world || r_weatherRunoffEntities->integer) && scale[2] > 0.0f) ?
			Com_Clamp(0.001f, 2.0f, r_weatherRunoffStrength->value * scale[2]) : -1.0f;
	}
	// the flow clock counts pattern cells of the along axis (1 / scale per
	// world unit), integrated once per frame and wrapped at 256 cells: the
	// shader's along lattice repeats every 256 cells, so the wrap is seamless
	const float runoffInvScale = 1.0f / Com_Clamp(4.0f, 1024.0f, r_weatherRunoffScale->value);
	static double runoffClock = 0.0;
	static int runoffFrame = -1;
	static float runoffTime = 0.0f;
	if (runoffFrame != tr.frameCount)
	{
		const float dt = backEnd.refdef.floatTime - runoffTime;
		if (runoffFrame >= 0 && dt > 0.0f)
			runoffClock = fmod(runoffClock + MIN(dt, 0.1f) * Com_Clamp(0.0f, 256.0f, r_weatherRunoffSpeed->value) * runoffInvScale, 256.0);
		runoffFrame = tr.frameCount;
		runoffTime = backEnd.refdef.floatTime;
	}
	const vec4_t runoff = {
		runoffStrength,
		runoffInvScale,
		(float)runoffClock,
		MIN(Com_Clamp(0.0f, 4.0f, r_weatherRunoffProbe->value) * ws->texelSizeWorld, 32.0f)
	};
	// wind leans the streaks downwind (never upward: the shear is per unit of
	// fall), at most about 20 degrees; windward faces run a little more
	const float windX = ws->windDirection[0], windY = ws->windDirection[1];
	const float windLength = sqrtf(windX * windX + windY * windY);
	const float lean = MIN(windLength / 400.0f, 0.35f);
	vec2_t windDir = { 0.0f, 0.0f };
	if (windLength > 1.0f)
	{
		windDir[0] = windX / windLength;
		windDir[1] = windY / windLength;
	}
	// pattern frame: world axes, or the entity's horizontal yaw axis
	vec4_t frame = { 1.0f, 0.0f, 0.0f, 0.0f };
	float frameOriginZ = 0.0f;
	if (backEnd.currentEntity && backEnd.currentEntity != &tr.worldEntity)
	{
		const refEntity_t &e = backEnd.currentEntity->e;
		float ax = e.axis[0][0], ay = e.axis[0][1];
		if (ax * ax + ay * ay < 0.01f)
		{
			// forward axis near vertical: the left axis gives the yaw
			ax = e.axis[1][1];
			ay = -e.axis[1][0];
		}
		const float axisLength = sqrtf(ax * ax + ay * ay);
		if (axisLength > 1e-4f)
		{
			frame[0] = ax / axisLength;
			frame[1] = ay / axisLength;
		}
		frame[2] = e.origin[0];
		frame[3] = e.origin[1];
		frameOriginZ = e.origin[2];
	}
	const vec4_t runoff2 = {
		windDir[0] * lean,
		windDir[1] * lean,
		lean > 0.0f ? MIN(windLength / 400.0f, 1.0f) * 0.3f : 0.0f,
		frameOriginZ
	};
	uniformDataWriter.SetUniformVec4(UNIFORM_RUNOFFPARAMS, runoff);
	uniformDataWriter.SetUniformVec4(UNIFORM_RUNOFFPARAMS2, runoff2);
	uniformDataWriter.SetUniformVec4(UNIFORM_RUNOFFFRAME, frame);
	uniformDataWriter.SetUniformMatrix4x4(UNIFORM_WEATHERMVP, ws->weatherMVP);
	samplerBindingsWriter.AddStaticImage(tr.weatherDepthImage, TB_WEATHERDEPTH);
}
