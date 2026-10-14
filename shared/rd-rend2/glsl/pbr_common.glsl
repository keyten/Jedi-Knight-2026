/*[Fragment]*/
// Common scene/PBR math. Inserted by Rend2's shader-library combiner; GLSL
// source files do not rely on a non-portable #include implementation.
const vec3 PBR_LUMINANCE = vec3(0.2126, 0.7152, 0.0722);
const float PBR_DIELECTRIC_F0 = 0.04;
const float PBR_PI = 3.14159265358979323846;
// Keeps the normal-incidence GGX peak below 50,930, finite even when the
// implementation evaluates intermediates at fp16 precision.
const float PBR_MIN_GGX_ALPHA = 0.0025;

float PBRLuminance(vec3 rgb)
{
	return max(dot(max(rgb, vec3(0.0)), PBR_LUMINANCE), 0.0);
}

float PerceptualRoughnessToAlpha(float perceptualRoughness)
{
	float r = clamp(perceptualRoughness, 0.0, 1.0);
	return max(r * r, PBR_MIN_GGX_ALPHA);
}

float LegacyAlphaToPerceptualRoughness(float legacyAlpha)
{
	return sqrt(clamp(legacyAlpha, 0.0, 1.0));
}

// Canonical isotropic Trowbridge-Reitz / GGX NDF. `alpha` is the microfacet
// slope parameter, never material-facing perceptual roughness.
float D_GGX(float NoH, float alpha)
{
	NoH = clamp(NoH, 0.0, 1.0);
	alpha = clamp(alpha, PBR_MIN_GGX_ALPHA, 1.0);
	float alpha2 = alpha * alpha;
	float d = NoH * NoH * (alpha2 - 1.0) + 1.0;
	return alpha2 / max(PBR_PI * d * d, 1e-12);
}

// Height-correlated Smith-GGX visibility, i.e. G2 / (4 NoV NoL).
// Heitz 2014. `alpha` has the same slope-parameter domain as D_GGX.
float V_SmithGGXCorrelated(float alpha, float NoV, float NoL)
{
	alpha = clamp(alpha, PBR_MIN_GGX_ALPHA, 1.0);
	NoV = clamp(NoV, 0.0, 1.0);
	NoL = clamp(NoL, 0.0, 1.0);
	float alpha2 = alpha * alpha;
	float lambdaV = NoL * sqrt(NoV * NoV * (1.0 - alpha2) + alpha2);
	float lambdaL = NoV * sqrt(NoL * NoL * (1.0 - alpha2) + alpha2);
	return 0.5 / max(lambdaV + lambdaL, 1e-6);
}

vec3 F_Schlick(vec3 F0, vec3 F90, float VoH)
{
	float Fc = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
	return F0 + (F90 - F0) * Fc;
}

float PBRPointAttenuation(float rangeWorld, float distanceSquaredWorld,
	float metersPerWorldUnitSquared)
{
	float safeDistanceSquared = max(distanceSquaredWorld, 1e-8);
	float x2 = safeDistanceSquared / max(rangeWorld * rangeWorld, 1e-8);
	float window = max(1.0 - x2 * x2, 0.0);
	window *= window;
	return window / max(safeDistanceSquared * metersPerWorldUnitSquared, 1e-4);
}
