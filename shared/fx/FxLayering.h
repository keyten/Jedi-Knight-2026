// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
//
// FX event classification for the rend2 FX upgrades (docs/rend2-fx-upgrades.md):
// r_fxGpuParticles (secondary sparks / embers / dust) and r_fxLayering (missing
// layers of stock impacts and explosions). Shared by the SP cgame and the MP
// client FX schedulers; the renderer receives a refFxEvent_t (tr_types.h).
//
// Classification is by effect path (stock names are stable and descriptive),
// refined by what the template already contains, so a layer is added only when
// the effect lacks it. Pure functions, no engine dependencies.
#ifndef FX_LAYERING_H
#define FX_LAYERING_H

#include <string.h>
#include <ctype.h>

namespace FxLayering {

// same values as FXEV_CLASS_* in tr_types.h
enum Class {
	ClassNone = 0,
	ClassImpactMetal,	// blaster / bullet on metal: sparks
	ClassImpactStone,	// impact on stone / dirt: dust kick-up
	ClassImpactEnergy,	// energy weapon impact without a material
	ClassExplosion,		// grenades, rockets, thermal, mines, barrels
	ClassMuzzle,		// muzzle flashes
	ClassFire,			// fires, torches, embers
	ClassSparks,		// standalone spark showers
	ClassCount
};

// same bits as FXEV_HAS_* in tr_types.h: layers the template already has
enum Has {
	HasLight = 1,		// a Light primitive
	HasDecal = 2,		// a Decal primitive (scorch / mark)
	HasSparks = 4,		// a Line / Tail / Electricity primitive (spark streaks)
	HasDust = 8,		// a non-additive Particle primitive (smoke / dust)
	HasDistortion = 16	// a Cylinder / shockwave primitive
};

inline bool Contains(const char *haystack, const char *needle) {
	if (!haystack || !needle) return false;
	const size_t n = strlen(needle);
	for (const char *h = haystack; *h; ++h) {
		size_t i = 0;
		while (i < n && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) ++i;
		if (i == n) return true;
	}
	return false;
}

// Class of an effect file (e.g. "effects/blaster/wall_impact.efx").
inline Class Classify(const char *effect) {
	if (!effect || !*effect) return ClassNone;
	// never touch screen / view / environment ambience effects
	if (Contains(effect, "screen") || Contains(effect, "rain") || Contains(effect, "snow") ||
		Contains(effect, "water") || Contains(effect, "splash") || Contains(effect, "steam") ||
		Contains(effect, "saber") || Contains(effect, "force/") || Contains(effect, "blood"))
		return ClassNone;
	if (Contains(effect, "muzzle") || Contains(effect, "flash.efx")) return ClassMuzzle;
	if (Contains(effect, "explosion") || Contains(effect, "explode") || Contains(effect, "detpack") ||
		Contains(effect, "thermal") || Contains(effect, "mine") || Contains(effect, "rocket/") ||
		Contains(effect, "barrel"))
		return ClassExplosion;
	if (Contains(effect, "fire") || Contains(effect, "flame") || Contains(effect, "torch") ||
		Contains(effect, "ember") || Contains(effect, "burn"))
		return ClassFire;
	if (Contains(effect, "spark")) return ClassSparks;
	if (Contains(effect, "impact") || Contains(effect, "_hit") || Contains(effect, "hit_") ||
		Contains(effect, "/hit") || Contains(effect, "wall_")) {
		if (Contains(effect, "stone") || Contains(effect, "dirt") || Contains(effect, "sand") ||
			Contains(effect, "rock") || Contains(effect, "dust") || Contains(effect, "concrete"))
			return ClassImpactStone;
		if (Contains(effect, "metal") || Contains(effect, "droid") || Contains(effect, "blaster") ||
			Contains(effect, "bowcaster") || Contains(effect, "repeater") || Contains(effect, "bryar") ||
			Contains(effect, "disruptor") || Contains(effect, "demp") || Contains(effect, "flechette") ||
			Contains(effect, "concussion"))
			return ClassImpactMetal;
		return ClassImpactEnergy;
	}
	return ClassNone;
}

// What the layering adds for a class, given what the effect has.
enum Layer { LayerSparks = 1, LayerDust = 2, LayerLight = 4, LayerDistortion = 8, LayerScorch = 16 };

inline int MissingLayers(int cls, int has) {
	int want = 0;
	switch (cls) {
	case ClassImpactMetal:  want = LayerSparks | LayerLight | LayerScorch; break;
	case ClassImpactStone:  want = LayerDust | LayerLight | LayerScorch; break;
	case ClassImpactEnergy: want = LayerSparks | LayerDust | LayerLight | LayerScorch; break;
	case ClassExplosion:    want = LayerSparks | LayerDust | LayerLight | LayerDistortion | LayerScorch; break;
	case ClassMuzzle:       want = LayerLight | LayerDistortion; break;
	default: return 0;
	}
	if (has & HasLight) want &= ~LayerLight;
	if (has & HasDecal) want &= ~LayerScorch;
	if (has & HasSparks) want &= ~LayerSparks;
	if (has & HasDust) want &= ~LayerDust;
	if (has & HasDistortion) want &= ~LayerDistortion;
	return want;
}

}

#endif
