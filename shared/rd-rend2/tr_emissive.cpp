#include "tr_local.h"
#include "tr_pbr_contract.h"

#include <cctype>

namespace {
struct inference_t { emissiveClass_t cls; float nits, confidence; const char *reason; };

bool Contains(const char *s, const char *word) {
	if (!s || !word) return false;
	for (; *s; ++s) { const char *a=s, *b=word;
		while (*a && *b && std::tolower((unsigned char)*a)==std::tolower((unsigned char)*b)) { ++a; ++b; }
		if (!*b) return true; }
	return false;
}
bool Any(const char *a, const char *b, const char *const *words, int count) {
	for (int i=0; i<count; ++i) if (Contains(a,words[i]) || Contains(b,words[i])) return true;
	return false;
}
inference_t Infer(const shader_t *sh, const shaderStage_t *st) {
	const char *sn=sh ? sh->name : "";
	const image_t *im=st->bundle[0].image[0]; const char *in=im ? im->imgName : "";
	static const char *energy[]={"saber","blade","blaster","bolt","beam","plasma","laser","energy"};
	static const char *hot[]={"lava","molten","magma","fire","flame","explosion"};
	static const char *indicator[]={"indicator","button","switch","status","warning","led","control"};
	static const char *screen[]={"screen","display","monitor","console","terminal","holo"};
	static const char *panel[]={"fluor","lightpanel","light_panel","ceilinglight","striplight","tube_light","neon"};
	static const char *lamp[]={"lamp","fixture","spotlight","lightglow","light_glow","brightlight"};
	static const char *decor[]={"glow","pulse","spark","aura"};
	if (Any(sn,in,energy,ARRAY_LEN(energy))) return {EMISSIVE_CLASS_ENERGY,40000,0.95f,"energy token"};
	if (Any(sn,in,hot,ARRAY_LEN(hot))) return {EMISSIVE_CLASS_HOT,5000,0.90f,"hot-source token"};
	if (Any(sn,in,indicator,ARRAY_LEN(indicator))) return {EMISSIVE_CLASS_INDICATOR,60,0.85f,"indicator token"};
	if (Any(sn,in,screen,ARRAY_LEN(screen))) return {EMISSIVE_CLASS_SCREEN,250,0.85f,"screen token"};
	if (Any(sn,in,panel,ARRAY_LEN(panel))) return {EMISSIVE_CLASS_LIGHT_PANEL,2000,0.90f,"panel token"};
	if ((sh && sh->surfaceLight>0) || Any(sn,in,lamp,ARRAY_LEN(lamp)))
		return {EMISSIVE_CLASS_LAMP,3000,sh&&sh->surfaceLight>0?0.95f:0.80f,sh&&sh->surfaceLight>0?"surfacelight":"lamp token"};
	if (Any(sn,in,decor,ARRAY_LEN(decor))) return {EMISSIVE_CLASS_DECORATIVE,600,0.65f,"decorative token"};
	return {EMISSIVE_CLASS_UNKNOWN,600,st->glow?0.55f:0.35f,"conservative fallback"};
}
}

void R_ClassifyEmissiveStage(const shader_t *sh, shaderStage_t *st) {
	if (!st || !st->active) return;
	if (st->emissivePhysical) { st->emissiveProvenance=EMISSIVE_EXPLICIT_PHYSICAL; st->emissiveConfidence=1; st->emissiveReason="authored nits"; return; }
	if (st->emissive) { st->emissiveProvenance=EMISSIVE_LEGACY_CONVERTED; st->emissiveClass=EMISSIVE_CLASS_UNKNOWN; st->emissiveConfidence=1; st->emissiveReason="legacy emissiveScale"; return; }
	const int blend=st->stateBits&(GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS);
	if (!st->glow && blend!=(GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE)) return;
	const inference_t x=Infer(sh,st); st->emissiveClass=x.cls; st->emissiveProvenance=EMISSIVE_AUTO_INFERRED;
	st->emissiveLuminance=x.nits; st->emissiveConfidence=x.confidence; st->emissiveReason=x.reason;
}

void R_EmissiveStageCanonicalScale(const shaderStage_t *st, vec3_t out) {
	VectorClear(out); if (!st) return;
	if (st->emissivePhysical) { VectorScale(st->emissiveColor,Rend2PBR::PhysicalToScene(st->emissiveLuminance,R_PBRPhysicalToScene()),out); return; }
	if (st->emissive) {
		const float s=r_pbrPhysical->integer ? Rend2PBR::PhysicalToScene(Rend2PBR::LegacyEmissiveToNits(st->emissiveIntensity,r_emissiveLegacyNits->value),R_PBRPhysicalToScene()) : st->emissiveIntensity;
		VectorScale(st->emissiveColor,s,out); return;
	}
	if (st->emissiveProvenance==EMISSIVE_AUTO_INFERRED) { const float s=r_pbrPhysical->integer ? Rend2PBR::PhysicalToScene(st->emissiveLuminance,R_PBRPhysicalToScene()) : 1; VectorSet(out,s,s,s); }
}

qboolean R_EmissiveDebugColor(const shaderStage_t *st, vec4_t out) {
	if (!r_emissiveDebug->integer || !st || st->emissiveProvenance==EMISSIVE_NONE) return qfalse;
	if (r_emissiveDebug->integer==1) {
		if (st->emissiveProvenance==EMISSIVE_EXPLICIT_PHYSICAL) VectorSet(out,0.1f,1,0.2f);
		else if (st->emissiveProvenance==EMISSIVE_LEGACY_CONVERTED) VectorSet(out,0.1f,0.8f,1);
		else VectorSet(out,1,0.55f,0.05f);
	} else if (r_emissiveDebug->integer==2) {
		const float n=st->emissivePhysical?st->emissiveLuminance:(st->emissive?st->emissiveIntensity*r_emissiveLegacyNits->value:st->emissiveLuminance);
		if (n<100) VectorSet(out,0.1f,0.1f,0.8f); else if(n<500) VectorSet(out,0.1f,0.8f,1); else if(n<3000) VectorSet(out,0.2f,1,0.2f); else if(n<10000) VectorSet(out,1,0.8f,0.1f); else VectorSet(out,1,0.1f,0.05f);
	} else { const float c=st->emissiveConfidence; VectorSet(out,1-c,c,0.05f); }
	out[3]=1; return qtrue;
}
