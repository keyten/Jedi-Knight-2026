// Renderer-independent admission policy for exact-depth conditional model draws.
#ifndef REND2_MODEL_OCCLUSION_POLICY_H
#define REND2_MODEL_OCCLUSION_POLICY_H
namespace ModelOcclusionPolicy
{
struct View
{
	bool enabled, prepass, comparison, conditionalSupported, main;
	bool shadow, portal, sky, skyOccluders, refraction, noWorld;
};
inline bool Eligible(const View& view)
{
	return view.enabled && view.prepass && !view.comparison && view.conditionalSupported && view.main &&
		!view.shadow && !view.portal && !view.sky && !view.skyOccluders && !view.refraction && !view.noWorld;
}
struct Surface
{
	bool ghoul, world, post, opaque, simpleDepth, deforms, offset, distortion;
	bool matchingCull, safeFlags, tessellation, gore, buffers, nearCamera;
	unsigned indices;
};
inline bool Eligible(const Surface& surface)
{
	return surface.ghoul && !surface.world && !surface.post && surface.opaque && surface.simpleDepth &&
		!surface.deforms && !surface.offset && !surface.distortion && surface.matchingCull &&
		surface.safeFlags && !surface.tessellation && !surface.gore && surface.buffers && !surface.nearCamera && surface.indices >= 1024;
}
}
#endif
