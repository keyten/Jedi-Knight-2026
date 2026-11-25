// Copyright (C) 2026 OpenJK contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"
#include <string>

static cvar_t *r_softParticles, *r_softParticleDistance, *r_particleNearFade;
static cvar_t *r_particleSort;
static cvar_t *r_particleProfile;
static cvar_t *r_particleBatching;
static cvar_t *r_transparencySort, *r_particleVolume, *r_particleSoftScale, *r_particleCulling;
static cvar_t *r_particleDepthOnDemand;

bool R_TransparencySort() { return r_transparencySort && r_transparencySort->integer != 0; }
bool R_ParticleStableOrder() { return R_TransparencySort() || (r_particleSort && r_particleSort->integer); }
bool R_ParticleStateIsolation() { return R_ParticleStableOrder() || r_softParticles->integer || r_particleNearFade->integer; }
bool R_ParticleShapeData() { return r_softParticles->integer && (r_particleVolume->integer || r_particleSoftScale->value > 0.0f); }
bool R_ParticleShapeShader(const shader_t *shader)
{
	// TexCoord1 is radius metadata only for simple billboard materials. Retain
	// the original UV channel and flat fading for multi-pass/modded materials.
	if (!R_ParticleShapeData() || !shader || shader->numUnfoggedPasses != 1 ||
		shader->fogPass != FP_NONE || shader->numDeforms || shader->useDistortion) return false;
	const shaderStage_t *stage = shader->stages[0];
	if (!stage || stage->ss || stage->glslShaderGroup == tr.lightallShader ||
		stage->bundle[0].tcGen != TCGEN_TEXTURE || stage->bundle[0].numTexMods ||
		stage->bundle[0].isLightmap || stage->bundle[0].isVideoMap) return false;
	for (size_t i = 1; i < ARRAY_LEN(stage->bundle); ++i)
		if (stage->bundle[i].image[0]) return false;
	return true;
}
bool R_ParticleDepthOnDemand() { return r_particleDepthOnDemand->integer != 0; }
static unsigned particleDepthCopies;
static double particleProfileTimes[3];
static unsigned particleProfileCounts[3];
static bool particleProfileFresh;
static std::string particleProfileBuffer;
static bool particleProfileOverflow;

// Diagnostic mode 2 keeps console/file I/O out of the sampled frame loop.
// GPU timers use this same sink, then profile-dump writes complete lines.
void R_ParticleProfileLog(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	Q_vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (r_particleProfile && r_particleProfile->integer == 2)
	{
		const size_t count = strlen(line);
		if (particleProfileBuffer.size() + count <= 1024 * 1024)
			particleProfileBuffer.append(line, count);
		else particleProfileOverflow = true;
	}
	else ri.Printf(PRINT_ALL, "%s", line);
}

static void R_ParticleProfileDump()
{
	if (particleProfileOverflow) ri.Printf(PRINT_ALL, "PARTICLE_PROFILE_BUFFER_OVERFLOW\n");
	for (size_t offset = 0; offset < particleProfileBuffer.size();)
	{
		size_t count = std::min(size_t(2000), particleProfileBuffer.size() - offset);
		const size_t newline = particleProfileBuffer.rfind('\n', offset + count - 1);
		if (newline != std::string::npos && newline >= offset) count = newline + 1 - offset;
		ri.Printf(PRINT_ALL, "%.*s", int(count), particleProfileBuffer.data() + offset);
		offset += count;
	}
	particleProfileBuffer.clear();
	particleProfileOverflow = false;
}

bool R_ParticleProfileActive() { return r_particleProfile && r_particleProfile->integer != 0; }
void R_ParticleProfileFrame(double recordUs, double submitUs, double sortUs, unsigned items, unsigned particles)
{
	particleProfileTimes[0] = recordUs; particleProfileTimes[1] = submitUs; particleProfileTimes[2] = sortUs;
	particleProfileCounts[0] = items; particleProfileCounts[1] = particles; particleProfileCounts[2] = particleDepthCopies;
	particleProfileFresh = true;
	particleDepthCopies = 0;
}

void R_ParticleProfilePrint()
{
	if (!R_ParticleProfileActive() || !particleProfileFresh) return;
	// Report outside Main View's GPU timestamp interval; log I/O must not be timed.
	R_ParticleProfileLog("PARTICLE_PROFILE|record=%.3f|submit=%.3f|sort=%.3f|items=%u|particles=%u|depthCopies=%u\n",
		particleProfileTimes[0], particleProfileTimes[1], particleProfileTimes[2],
		particleProfileCounts[0], particleProfileCounts[1], particleProfileCounts[2]);
	particleProfileFresh = false;
}
static float particleTestDistance, particleTestRadius;
static int particleTestCount;
static bool particleTestSurface;
static bool particleFloorPending, particleFloorReady;
static vec3_t particleFloorAnchor;
static int particleTestKind;
static bool RB_ParticleBaseView();

bool R_ParticleBatching() { return r_particleBatching->integer != 0; }
bool R_ParticleDepthProbePending() { return (particleTestSurface || particleFloorPending) && RB_ParticleBaseView(); }

void R_ParticleTest_f()
{
	if (!ri.Cvar_VariableIntegerValue("developer")) return;
	if (!Q_stricmp(ri.Cmd_Argv(1), "profile-reset"))
	{
		particleProfileBuffer.clear();
		particleProfileBuffer.reserve(1024 * 1024);
		particleProfileOverflow = false;
		return;
	}
	if (!Q_stricmp(ri.Cmd_Argv(1), "profile-dump"))
	{
		R_ParticleProfileDump();
		return;
	}
	if (!Q_stricmp(ri.Cmd_Argv(1), "info"))
	{
		cvar_t *vars[] = {r_softParticles, r_softParticleDistance, r_particleNearFade, r_particleCulling,
			r_particleSort, r_particleBatching, r_transparencySort, r_particleVolume, r_particleSoftScale, r_particleDepthOnDemand};
		for (const auto *v : vars) if (v)
			ri.Printf(PRINT_ALL, "PARTICLE_SETTING|%s=%s|pending=%s|latched=%d\n", v->name, v->string,
				v->latchedString ? v->latchedString : "none", (v->flags & CVAR_LATCH) != 0);
		return;
	}
	particleTestKind = !Q_stricmp(ri.Cmd_Argv(1), "sort") ? 1 :
		(!Q_stricmp(ri.Cmd_Argv(1), "sort-reverse") ? 2 :
		(!Q_stricmp(ri.Cmd_Argv(1), "batch") ? 3 :
		(!Q_stricmp(ri.Cmd_Argv(1), "batch-reverse") ? 4 :
		(!Q_stricmp(ri.Cmd_Argv(1), "floor") ? 5 :
		(!Q_stricmp(ri.Cmd_Argv(1), "additive") ? 6 :
		(!Q_stricmp(ri.Cmd_Argv(1), "floor-mix") ? 7 : 0))))));
	particleTestSurface = ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "surface");
	particleFloorPending = particleTestKind == 5 || particleTestKind == 7;
	particleFloorReady = false;
	particleTestDistance = ri.Cmd_Argc() > 1 ? atof(ri.Cmd_Argv(1)) : 0.0f;
	if (particleTestKind == 6) particleTestDistance = 96.0f;
	particleTestRadius = ri.Cmd_Argc() > 2 ? atof(ri.Cmd_Argv(2)) : 24.0f;
	particleTestDistance = Com_Clamp(0.0f, 1024.0f, particleTestDistance);
	particleTestRadius = Com_Clamp(0.1f, 256.0f, particleTestRadius);
	particleTestCount = ri.Cmd_Argc() > 3 ? Com_Clampi(1, 1024, atoi(ri.Cmd_Argv(3))) : 1;
	ri.Printf(PRINT_ALL, "PARTICLE_FIXTURE|kind=%d|distance=%.3f|radius=%.3f|count=%d\n",
		particleTestKind, particleTestDistance, particleTestRadius, particleTestCount);
}

void R_AddParticleTest(const refdef_t *view)
{
	if (view->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL)) return;
	if (particleTestKind == 5 || particleTestKind == 7)
	{
		if (!particleFloorReady) return;
		// Diagnostic floor-contact fixture anchored to actual visible floor depth.
		// No EFX replacement: the real stock sprite/material path is used.
		for (int i = 0; i < particleTestCount; ++i)
		{
			refEntity_t puff = {};
			puff.reType = RT_SPRITE;
			if (particleTestKind == 7 && i % 2)
			{
				puff.reType = RT_ORIENTED_QUAD;
				for (int axis = 0; axis < 3; ++axis) VectorCopy(view->viewaxis[axis], puff.axis[axis]);
			}
			puff.radius = particleTestRadius;
			puff.rotation = i * 67.0f;
			puff.customShader = RE_RegisterShader("gfx/effects/alpha_smoke");
			VectorCopy(particleFloorAnchor, puff.origin);
			VectorMA(puff.origin, ((i % 3) - 1) * particleTestRadius * 0.65f, view->viewaxis[1], puff.origin);
			puff.origin[2] += particleTestRadius * (0.20f + (i % 2) * 0.15f);
			puff.shaderRGBA[0] = puff.shaderRGBA[1] = puff.shaderRGBA[2] = 10;
			puff.shaderRGBA[3] = 255;
			RE_AddRefEntityToScene(&puff);
		}
		return;
	}
	if (particleTestKind >= 3 && particleTestKind <= 4)
	{
		// Interleaved materials, colors and unique depths exercise both the
		// boundaries of merged runs and per-quad order inside each run.
		for (int n = 0; n < 48; ++n)
		{
			const int i = particleTestKind == 4 ? 47 - n : n;
			const float distance = 32.0f + ((i * 17) % 48) * 1.25f;
			refEntity_t puff = {};
			puff.reType = RT_SPRITE;
			puff.radius = distance * 0.2f;
			puff.rotation = i * 37.0f;
			puff.customShader = RE_RegisterShader(i % 5 == 0 ? "gfx/effects/alpha_smoke2" : "gfx/effects/alpha_smoke");
			VectorMA(view->vieworg, distance, view->viewaxis[0], puff.origin);
			puff.shaderRGBA[0] = 40 + (i * 71) % 216;
			puff.shaderRGBA[1] = 40 + (i * 43) % 216;
			puff.shaderRGBA[2] = 40 + (i * 19) % 216;
			puff.shaderRGBA[3] = 180;
			RE_AddRefEntityToScene(&puff);
		}
		return;
	}
	if (particleTestKind == 1 || particleTestKind == 2)
	{
		for (int n = 0; n < 16; ++n)
		{
			const int i = particleTestKind == 2 ? (n < 8 ? n + 8 : n - 8) : n;
			const bool farCloud = i >= 8;
			const float distance = farCloud ? 64.0f : 32.0f;
			refEntity_t puff = {};
			puff.reType = RT_SPRITE;
			puff.radius = distance * 0.35f;
			puff.rotation = (i % 8) * 37.0f;
			puff.customShader = RE_RegisterShader(farCloud ? "gfx/effects/alpha_smoke" : "gfx/effects/alpha_smoke2");
			VectorMA(view->vieworg, distance, view->viewaxis[0], puff.origin);
			puff.shaderRGBA[0] = farCloud ? 20 : 255;
			puff.shaderRGBA[1] = farCloud ? 100 : 70;
			puff.shaderRGBA[2] = farCloud ? 255 : 20;
			puff.shaderRGBA[3] = 255;
			RE_AddRefEntityToScene(&puff);
		}
		return;
	}
	if (particleTestDistance <= 0.0f) return;
	for (int i = 0; i < particleTestCount; ++i)
	{
		refEntity_t puff = {};
		puff.reType = RT_SPRITE;
		puff.radius = particleTestRadius;
		puff.rotation = i * 37.0f;
		puff.customShader = RE_RegisterShader(particleTestKind == 6 ? "gfx/effects/flamejet" : "gfx/effects/alpha_smoke");
		VectorMA(view->vieworg, particleTestDistance, view->viewaxis[0], puff.origin);
		if (particleTestCount > 1)
		{
			VectorMA(puff.origin, ((i % 4) - 1.5f) * particleTestRadius * 0.2f, view->viewaxis[1], puff.origin);
			VectorMA(puff.origin, ((i / 4) % 3 - 1.0f) * particleTestRadius * 0.2f, view->viewaxis[2], puff.origin);
		}
		for (int c = 0; c < 4; ++c) puff.shaderRGBA[c] = 255;
		RE_AddRefEntityToScene(&puff);
	}
}

void R_InitParticleCvars()
{
	particleTestDistance = 0.0f;
	particleTestSurface = false;
	particleFloorPending = particleFloorReady = false;
	particleTestKind = 0;
	r_softParticles = R_Cvar("r_softParticles", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Soft intersections: 0 off, 1 alpha sprites, 2 also additive sprites; vid_restart");
	R_CvarRange(r_softParticles, 0, 2, qtrue);
	r_softParticleDistance = R_Cvar("r_softParticleDistance", "8", CVAR_ARCHIVE | CVAR_LATCH,
		"Soft particle intersection distance in world units; 0 disables fading for A/B");
	R_CvarRange(r_softParticleDistance, 0.0f, 64.0f, qfalse);
	r_particleNearFade = R_Cvar("r_particleNearFade", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Smooth world FX near the camera instead of legacy distance culling");
	r_particleSort = R_Cvar("r_particleSort", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Order compatible world FX sprites back to front within their authored render layer");
	R_CvarRange(r_particleSort, 0, 1, qtrue);
	r_particleProfile = R_Cvar("r_particleProfile", "0", 0,
		"Developer profiler: 1 stream CPU/GPU records, 2 buffer until r_particleTest profile-dump");
	particleDepthCopies = 0;
	particleProfileFresh = false;
	r_particleBatching = R_Cvar("r_particleBatching", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Batch adjacent compatible single-pass sprites after particle depth sorting");
	R_CvarRange(r_particleBatching, 0, 1, qtrue);
	r_particleCulling = R_Cvar("r_particleCulling", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Conservative world FX near-camera culling; 0 restores legacy culling; vid_restart");
	r_transparencySort = R_Cvar("r_transparencySort", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Depth-order compatible translucent surfaces and keep each material's passes together; vid_restart");
	R_CvarRange(r_transparencySort, 0, 1, qtrue);
	r_particleVolume = R_Cvar("r_particleVolume", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Rounded thickness for alpha sprite intersections, retaining stock texture detail; vid_restart");
	R_CvarRange(r_particleVolume, 0, 1, qtrue);
	r_particleSoftScale = R_Cvar("r_particleSoftScale", "0.1", CVAR_ARCHIVE | CVAR_LATCH,
		"Flat sprite soft distance as a fraction of radius, capped by r_softParticleDistance; 0 uses fixed distance; vid_restart");
	R_CvarRange(r_particleSoftScale, 0, 1, qfalse);
	r_particleDepthOnDemand = R_Cvar("r_particleDepthOnDemand", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Copy particle depth only when a draw needs it; 0 copies every supported soft-particle view; vid_restart");
	R_CvarRange(r_particleDepthOnDemand, 0, 1, qtrue);
}

void R_CreateParticleImages(int width, int height)
{
	tr.particleDepthImage = r_softParticles->integer ?
		R_ScreenCreateImage("*particleDepth", width, height, GL_DEPTH24_STENCIL8, qfalse) : nullptr;
}

void R_CreateParticleFBOs()
{
	tr.particleDepthFbo = nullptr;
	if (!tr.particleDepthImage) return;
	tr.particleDepthFbo = FBO_Create("_particleDepth", tr.particleDepthImage->width, tr.particleDepthImage->height);
	FBO_Bind(tr.particleDepthFbo);
	qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
		tr.particleDepthImage->texnum, 0);
	qglDrawBuffer(GL_NONE);
	qglReadBuffer(GL_NONE);
	R_CheckFBO(tr.particleDepthFbo);
}

static bool RB_ParticleBaseView()
{
	return tr.renderFbo && !backEnd.depthFill && !backEnd.refractionFill &&
		!(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		!backEnd.viewParms.isSkyPortal &&
		(!backEnd.viewParms.targetFbo || backEnd.viewParms.targetFbo == tr.renderFbo);
}

bool RB_ParticleDepthView()
{
	return r_softParticles->integer && tr.particleDepthFbo && RB_ParticleBaseView();
}

void RB_CopyParticleDepth()
{
	const int timer = R_ParticleProfileActive() ? RB_ScreenBeginTimer("Particle depth") : -1;
	if (R_ParticleProfileActive()) ++particleDepthCopies;
	FBO_t *old = glState.currentFBO;
	if (!tr.particleDepthFbo)
	{
		// A diagnostic depth probe also works with every feature disabled. Allocate
		// lazily in the backend; ordinary legacy play allocates no particle images.
		tr.particleDepthImage = R_ScreenCreateImage("*particleProbeDepth", tr.renderFbo->width,
			tr.renderFbo->height, GL_DEPTH24_STENCIL8, qfalse);
		R_CreateParticleFBOs();
	}
	// A distinct attachment avoids texture feedback, and resolves MSAA depth.
	FBO_FastBlit(old, nullptr, tr.particleDepthFbo, nullptr, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	if (particleTestSurface || particleFloorPending)
	{
		// Developer-only contact fixture: put its plane two units in front of
		// the actual opaque surface under the viewport center. Never used in play.
		FBO_Bind(tr.particleDepthFbo);
		GLint packBuffer;
		qglGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		float depth;
		const int probeY = particleFloorPending ? backEnd.viewParms.viewportHeight / 5 : backEnd.viewParms.viewportHeight / 2;
		qglReadPixels(backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth / 2,
			backEnd.viewParms.viewportY + probeY,
			1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
		const float *p = backEnd.viewParms.projectionMatrix;
		const float viewZ = depth < 1.0f ? p[14] / (depth * 2.0f - 1.0f + p[10]) : 0.0f;
		if (particleFloorPending)
		{
			VectorMA(backEnd.viewParms.ori.origin, viewZ, backEnd.viewParms.ori.axis[0], particleFloorAnchor);
			const float ndcY = (probeY + 0.5f) * 2.0f / backEnd.viewParms.viewportHeight - 1.0f;
			VectorMA(particleFloorAnchor, ndcY * viewZ / p[5], backEnd.viewParms.ori.axis[2], particleFloorAnchor);
			particleFloorReady = viewZ > 0.0f;
			particleFloorPending = false;
			ri.Printf(PRINT_ALL, "PARTICLE_FLOOR|%.3f %.3f %.3f\n", particleFloorAnchor[0], particleFloorAnchor[1], particleFloorAnchor[2]);
		}
		else particleTestDistance = viewZ > 0 ? Com_Clamp(1.0f, 1024.0f, viewZ - 2.0f) : 0.0f;
		particleTestSurface = false;
		ri.Printf(PRINT_ALL, "particleTest: contact distance %.3f\n", particleTestDistance);
	}
	FBO_Bind(old);
	RB_ScreenEndTimer(timer);
}

bool R_ParticleEntity(const refEntity_t& ent)
{
	return (ent.reType == RT_SPRITE || ent.reType == RT_ORIENTED_QUAD) &&
		!(ent.renderfx & (RF_DEPTHHACK | RF_FIRST_PERSON | RF_NODEPTH | RF_VOLUMETRIC));
}

bool RB_ParticleFadeStage(const shader_t *shader, const shaderStage_t *stage)
{
	const trRefEntity_t *ent = backEnd.currentEntity;
	if (ent && R_ParticleEntity(ent->e) &&
		!backEnd.depthFill && !backEnd.refractionFill &&
		!(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) && shader->sort > SS_FOG &&
		!(stage->stateBits & (GLS_DEPTHMASK_TRUE | GLS_DEPTHTEST_DISABLE)))
	{
		const uint32_t src = stage->stateBits & GLS_SRCBLEND_BITS;
		const uint32_t dst = stage->stateBits & GLS_DSTBLEND_BITS;
		if ((src == GLS_SRCBLEND_SRC_ALPHA || src == GLS_SRCBLEND_ONE) &&
			(dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA || dst == GLS_DSTBLEND_ONE))
			return r_particleNearFade->integer || RB_ParticleDepthView();
	}
	return false;
}

static int RB_CompatibleTransparency(const shader_t *shader, const refEntity_t& ent)
{
	if (!shader || (ent.renderfx & (RF_FORCEPOST | RF_ALPHA_FADE | RF_FORCE_ENT_ALPHA |
		RF_DEPTHHACK | RF_FIRST_PERSON | RF_NODEPTH | RF_VOLUMETRIC)) ||
		shader->sort <= SS_FOG || shader->isSky || shader->numDeforms || shader->useDistortion ||
		!shader->numUnfoggedPasses || backEnd.depthFill || backEnd.refractionFill ||
		(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) ||
		(backEnd.refdef.rdflags & RDF_NOWORLDMODEL)) return 0;
	int kind = 1;
	for (int i = 0; i < shader->numUnfoggedPasses; ++i)
	{
		const shaderStage_t *s = shader->stages[i];
		if (!s || (s->stateBits & (GLS_DEPTHMASK_TRUE | GLS_DEPTHTEST_DISABLE))) return 0;
		const uint32_t src = s->stateBits & GLS_SRCBLEND_BITS;
		const uint32_t dst = s->stateBits & GLS_DSTBLEND_BITS;
		if ((src != GLS_SRCBLEND_SRC_ALPHA && src != GLS_SRCBLEND_ONE) ||
			(dst != GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA && dst != GLS_DSTBLEND_ONE)) return 0;
		if (dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA) kind = 2;
	}
	return kind;
}

int RB_ParticleSortClass(const shader_t *shader, const refEntity_t& ent)
{
	return r_particleSort->integer && R_ParticleEntity(ent) ? RB_CompatibleTransparency(shader, ent) : 0;
}

int RB_OrderedSortClass(const shader_t *shader, const refEntity_t& ent)
{
	return R_TransparencySort() ? RB_CompatibleTransparency(shader, ent) : RB_ParticleSortClass(shader, ent);
}

bool RB_ParticleBatchable(const shader_t *shader, const refEntity_t& ent)
{
	// Sprite positions/colors are already in the vertex buffer. Only merge stages
	// whose remaining uniforms are independent of the particular entity. Keep
	// multi-pass and separate fog passes per particle, and all special FX flags.
	if (!R_ParticleBatching() || ent.renderfx || ent.frame || ent.oldframe ||
		shader->numUnfoggedPasses != 1 || shader->fogPass != FP_NONE ||
		shader->cullType != CT_TWO_SIDED) return false;
	const shaderStage_t *stage = shader->stages[0];
	if (stage->ss || stage->glslShaderGroup == tr.lightallShader ||
		(stage->rgbGen != CGEN_VERTEX && stage->rgbGen != CGEN_EXACT_VERTEX) ||
		(stage->alphaGen != AGEN_VERTEX && stage->alphaGen != AGEN_IDENTITY)) return false;
	for (const auto& bundle : stage->bundle)
		if (bundle.numTexMods || bundle.isVideoMap ||
			(bundle.image[0] && bundle.tcGen != TCGEN_TEXTURE)) return false;
	return true;
}

void RB_SetupParticleFade(const shader_t *shader, const shaderStage_t *stage,
	UniformDataWriter& uniforms, SamplerBindingsWriter& samplers)
{
	vec4_t fade = {}, depth = {};
	if (RB_ParticleFadeStage(shader, stage))
	{
		const uint32_t src = stage->stateBits & GLS_SRCBLEND_BITS;
		const uint32_t dst = stage->stateBits & GLS_DSTBLEND_BITS;
		// Fade alpha for straight alpha; RGB as well for premultiplied/additive ONE.
		fade[0] = src == GLS_SRCBLEND_ONE ? 2.0f : 1.0f;
		fade[1] = r_particleNearFade->integer ? 16.0f : 0.0f;
		const bool soft = RB_ParticleDepthView() && r_softParticleDistance->value > 0.0f &&
			(dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA || r_softParticles->integer >= 2);
		fade[2] = soft ? 1.0f / r_softParticleDistance->value : 0.0f;
		const bool shape = soft && backEnd.currentEntity->e.reType == RT_SPRITE && R_ParticleShapeShader(shader);
		fade[3] = shape ? ((r_particleVolume->integer && dst == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA) ?
			-1.0f : r_particleSoftScale->value) : 0.0f;
		depth[0] = backEnd.viewParms.projectionMatrix[10];
		depth[1] = backEnd.viewParms.projectionMatrix[14];
		depth[2] = depth[1] / (depth[0] - 1.0f);
		if (soft)
			samplers.AddStaticImage(tr.particleDepthImage, TB_SHADOWMAP);
	}
	uniforms.SetUniformVec4(UNIFORM_PARTICLEFADE, fade);
	uniforms.SetUniformVec4(UNIFORM_PARTICLEDEPTH, depth);
}
