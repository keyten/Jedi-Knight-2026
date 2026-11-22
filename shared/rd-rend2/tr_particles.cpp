// Copyright (C) 2026 OpenJK contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"

static cvar_t *r_softParticles, *r_softParticleDistance, *r_particleNearFade;
static float particleTestDistance, particleTestRadius;
static bool particleTestSurface;

void R_ParticleTest_f()
{
	if (!ri.Cvar_VariableIntegerValue("developer")) return;
	particleTestSurface = ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "surface");
	particleTestDistance = ri.Cmd_Argc() > 1 ? atof(ri.Cmd_Argv(1)) : 0.0f;
	particleTestRadius = ri.Cmd_Argc() > 2 ? atof(ri.Cmd_Argv(2)) : 24.0f;
	particleTestDistance = Com_Clamp(0.0f, 1024.0f, particleTestDistance);
	particleTestRadius = Com_Clamp(0.1f, 256.0f, particleTestRadius);
}

void R_AddParticleTest(const refdef_t *view)
{
	if (particleTestDistance <= 0.0f || (view->rdflags & (RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL))) return;
	refEntity_t puff = {};
	puff.reType = RT_SPRITE;
	puff.radius = particleTestRadius;
	puff.customShader = RE_RegisterShader("gfx/misc/black_smoke");
	VectorMA(view->vieworg, particleTestDistance, view->viewaxis[0], puff.origin);
	for (int i = 0; i < 4; ++i) puff.shaderRGBA[i] = i == 3 ? 200 : 255;
	RE_AddRefEntityToScene(&puff);
}

void R_InitParticleCvars()
{
	particleTestDistance = 0.0f;
	particleTestSurface = false;
	r_softParticles = R_Cvar("r_softParticles", "1", CVAR_ARCHIVE | CVAR_LATCH,
		"Soft intersections: 0 off, 1 alpha sprites, 2 also additive sprites; vid_restart");
	R_CvarRange(r_softParticles, 0, 2, qtrue);
	r_softParticleDistance = R_Cvar("r_softParticleDistance", "8", CVAR_ARCHIVE,
		"Soft particle intersection distance in world units; 0 disables fading for A/B");
	R_CvarRange(r_softParticleDistance, 0.0f, 64.0f, qfalse);
	r_particleNearFade = R_Cvar("r_particleNearFade", "1", CVAR_ARCHIVE,
		"Smooth world FX near the camera instead of legacy distance culling");
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

bool RB_ParticleDepthView()
{
	return tr.renderFbo && tr.particleDepthFbo && !backEnd.depthFill && !backEnd.refractionFill &&
		!(backEnd.viewParms.flags & (VPF_DEPTHSHADOW | VPF_ORTHOGRAPHIC)) &&
		!(backEnd.refdef.rdflags & RDF_NOWORLDMODEL) &&
		!backEnd.viewParms.isSkyPortal &&
		(!backEnd.viewParms.targetFbo || backEnd.viewParms.targetFbo == tr.renderFbo);
}

void RB_CopyParticleDepth()
{
	FBO_t *old = glState.currentFBO;
	// A distinct attachment avoids texture feedback, and resolves MSAA depth.
	FBO_FastBlit(old, nullptr, tr.particleDepthFbo, nullptr, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	if (particleTestSurface)
	{
		// Developer-only contact fixture: put its plane two units in front of
		// the actual opaque surface under the viewport center. Never used in play.
		FBO_Bind(tr.particleDepthFbo);
		GLint packBuffer;
		qglGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		float depth;
		qglReadPixels(backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth / 2,
			backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight / 2,
			1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
		qglBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
		const float *p = backEnd.viewParms.projectionMatrix;
		particleTestDistance = depth < 1.0f ? Com_Clamp(1.0f, 1024.0f, p[14] / (depth * 2.0f - 1.0f + p[10]) - 2.0f) : 0.0f;
		particleTestSurface = false;
		ri.Printf(PRINT_ALL, "particleTest: contact distance %.3f\n", particleTestDistance);
	}
	FBO_Bind(old);
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
		depth[0] = backEnd.viewParms.projectionMatrix[10];
		depth[1] = backEnd.viewParms.projectionMatrix[14];
		depth[2] = depth[1] / (depth[0] - 1.0f);
		if (soft)
			samplers.AddStaticImage(tr.particleDepthImage, TB_SHADOWMAP);
	}
	uniforms.SetUniformVec4(UNIFORM_PARTICLEFADE, fade);
	uniforms.SetUniformVec4(UNIFORM_PARTICLEDEPTH, depth);
}
