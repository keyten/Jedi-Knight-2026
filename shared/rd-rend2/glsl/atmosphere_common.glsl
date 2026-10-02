/*[Fragment]*/
// Long range atmosphere (r_atmosphere, tr_atmosphere.cpp, docs/rend2-atmosphere.md).
//
// This file is not a program on its own: its fragment block is inserted into the atmosphere programs
// (LUT passes and the composite), see GLSL_LoadGPUProgramAtmosphere.
//
// Model: Hillaire 2020, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (transmittance,
// multiple scattering and sky-view LUTs) with the Earth parameters of Bruneton 2017
// ("precomputed_atmospheric_scattering"). Lengths in km inside this file. Altitudes are passed explicitly and
// r^2 - R^2 is formed as h (2R + h): with r ~ 6360 km a float r^2 - R^2 would lose the first few hundred metres
// that every JA map lives in.
//
// The aerial perspective of the scene (AtmosphereAerial) is NOT Hillaire's camera volume: JA paths are short
// against the planet (a few km even with r_atmosphereAerialScale), so the planet is flat along the path, the
// optical depth of the exponential profiles is closed form and the source term is taken at the transmittance
// midpoint of 3 segments (0.55 % worst case against a reference ray march, tools/rend2/atmosphere_check.py).
//
// u_Atmosphere (tr_atmosphere.cpp, RB_AtmosphereSetUniforms):
//   [0] Rayleigh density scale, Mie density scale, ozone density scale, Mie g
//   [1] metres per world unit, aerial distance scale, ground z (world units), ground altitude (km)
//   [2] direction towards the sun (world, z up), cos of the sun's angular radius
//   [3] sun illuminance at the top of the atmosphere (buffer units: a white Lambert surface lit at normal
//       incidence shows this value), w: 1 = the atmosphere draws the sun disc
//   [4] camera origin (world units), sky distance (world units, overlay sky)
//   [5] sky mode (0 overlay, 1 blend, 2 analytic), sky blend, sun glow scale (overlay sky), debug view
//   [6] viewport in render target texture coordinates (x, y, w, h)
//   [7] start distance (world units), camera altitude (km, clamped), draw (0 = transmittance, 1 = radiance),
//       froxel lookup valid (debug 6)

#define ATMOSPHERE_UNIFORM_VEC4S 8

uniform vec4 u_Atmosphere[ATMOSPHERE_UNIFORM_VEC4S];
uniform sampler2D u_AtmosphereTransmittanceMap;
uniform sampler2D u_AtmosphereMultiScatterMap;
uniform sampler2D u_AtmosphereSkyViewMap;

#define ATMO_PI 3.14159265358979

const float ATMO_R_GROUND = 6360.0;
const float ATMO_R_TOP = 6460.0;
const float ATMO_H_TOP = ATMO_R_TOP - ATMO_R_GROUND;
const vec3  ATMO_RAYLEIGH_SCATTERING = vec3(5.802e-3, 13.558e-3, 33.1e-3);	// per km
const float ATMO_RAYLEIGH_H = 8.0;
const float ATMO_MIE_SCATTERING = 3.996e-3;
const float ATMO_MIE_EXTINCTION = 4.40e-3;
const float ATMO_MIE_H = 1.2;
const vec3  ATMO_OZONE_ABSORPTION = vec3(0.650e-3, 1.881e-3, 0.085e-3);
const float ATMO_GROUND_ALBEDO = 0.3;
// the sky-view LUT and its lookups keep the camera above the ground (Hillaire's PLANET_RADIUS_OFFSET)
const float ATMO_MIN_ALTITUDE = 0.01;

#define ATMO_TRANSMITTANCE_SIZE vec2(256.0, 64.0)
#define ATMO_MULTISCATTER_SIZE vec2(32.0, 32.0)
#define ATMO_SKYVIEW_SIZE vec2(192.0, 108.0)

#define ATMO_AERIAL_SEGMENTS 3

// the sun disc in a 16 bit float buffer: luminance cap (not an exposure: the real disc is ~1e5 x the sky)
#define ATMO_SUN_DISC_MAX 16384.0

// --- media -------------------------------------------------------------------------------------------------

float AtmoRayleighDensity(float h) { return u_Atmosphere[0].x * exp(-h / ATMO_RAYLEIGH_H); }
float AtmoMieDensity(float h)      { return u_Atmosphere[0].y * exp(-h / ATMO_MIE_H); }
// Bruneton's ozone tent: rises from 10 km, peaks at 25 km, gone at 40 km
float AtmoOzoneDensity(float h)    { return u_Atmosphere[0].z * max(0.0, 1.0 - abs(h - 25.0) / 15.0); }

vec3 AtmoExtinction(float h)
{
	return ATMO_RAYLEIGH_SCATTERING * AtmoRayleighDensity(h) + ATMO_MIE_EXTINCTION * AtmoMieDensity(h) +
		ATMO_OZONE_ABSORPTION * AtmoOzoneDensity(h);
}

float AtmoPhaseRayleigh(float nu)
{
	return 3.0 / (16.0 * ATMO_PI) * (1.0 + nu * nu);
}

// Cornette-Shanks
float AtmoPhaseMie(float nu, float g)
{
	float k = 3.0 / (8.0 * ATMO_PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + nu * nu) / pow(max(1.0 + g * g - 2.0 * g * nu, 1e-4), 1.5);
}

// --- geometry (altitude h in km, mu = cos zenith) -------------------------------------------------------------

// r^2 - R^2 without cancellation
float AtmoRho2(float h)
{
	return h * (2.0 * ATMO_R_GROUND + h);
}

float AtmoDistanceToTop(float h, float mu)
{
	float r = ATMO_R_GROUND + h;
	// Rt^2 - r^2 + r^2 mu^2
	float disc = (ATMO_H_TOP - h) * (ATMO_R_TOP + r) + r * r * mu * mu;
	return max(-r * mu + sqrt(max(disc, 0.0)), 0.0);
}

// distance to the ground, < 0 when the ray misses it
float AtmoDistanceToGround(float h, float mu)
{
	if (mu >= 0.0)
		return -1.0;
	float r = ATMO_R_GROUND + h;
	float disc = r * r * mu * mu - AtmoRho2(h);
	if (disc < 0.0)
		return -1.0;
	return max(-r * mu - sqrt(disc), 0.0);
}

// cos zenith of the horizon seen from altitude h
float AtmoHorizonMu(float h)
{
	return -sqrt(AtmoRho2(max(h, 0.0))) / (ATMO_R_GROUND + max(h, 0.0));
}

// --- LUT parameterizations -------------------------------------------------------------------------------------

float AtmoUnitToTexel(float x, float size) { return 0.5 / size + x * (1.0 - 1.0 / size); }
float AtmoTexelToUnit(float u, float size) { return (u - 0.5 / size) / (1.0 - 1.0 / size); }

// Bruneton: x_r = rho / H, x_mu = distance to the top between its minimum and maximum (rays above the horizon)
vec2 AtmoTransmittanceUV(float h, float mu)
{
	h = clamp(h, 0.0, ATMO_H_TOP);
	float H = sqrt(AtmoRho2(ATMO_H_TOP));
	float rho = sqrt(AtmoRho2(h));
	float d = AtmoDistanceToTop(h, mu);
	float dMin = ATMO_H_TOP - h;
	float dMax = rho + H;
	float xMu = (d - dMin) / max(dMax - dMin, 1e-6);
	float xR = rho / H;
	return vec2(AtmoUnitToTexel(clamp(xMu, 0.0, 1.0), ATMO_TRANSMITTANCE_SIZE.x),
		AtmoUnitToTexel(xR, ATMO_TRANSMITTANCE_SIZE.y));
}

void AtmoTransmittanceFromUV(vec2 uv, out float h, out float mu)
{
	float xMu = clamp(AtmoTexelToUnit(uv.x, ATMO_TRANSMITTANCE_SIZE.x), 0.0, 1.0);
	float xR = clamp(AtmoTexelToUnit(uv.y, ATMO_TRANSMITTANCE_SIZE.y), 0.0, 1.0);
	float H = sqrt(AtmoRho2(ATMO_H_TOP));
	float rho = H * xR;
	float r = sqrt(rho * rho + ATMO_R_GROUND * ATMO_R_GROUND);
	h = r - ATMO_R_GROUND;
	float dMin = ATMO_H_TOP - h;
	float dMax = rho + H;
	float d = dMin + xMu * (dMax - dMin);
	mu = d <= 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
	mu = clamp(mu, -1.0, 1.0);
}

// Transmittance from altitude h towards cos zenith mu to space, 0 below the horizon (planet shadow, softened
// over the sun's angular radius)
vec3 AtmoTransmittanceToSpace(float h, float mu)
{
	h = max(h, 0.0);
	vec3 T = texture(u_AtmosphereTransmittanceMap, AtmoTransmittanceUV(h, mu)).rgb;
	float horizon = AtmoHorizonMu(h);
	float soft = max(1.0 - u_Atmosphere[2].w, 1e-5) * 2.0;
	return T * smoothstep(horizon - soft, horizon + soft, mu);
}

// multiple scattering LUT: u = cos sun zenith, v = altitude
vec2 AtmoMultiScatterUV(float h, float muSun)
{
	return vec2(AtmoUnitToTexel(clamp(muSun * 0.5 + 0.5, 0.0, 1.0), ATMO_MULTISCATTER_SIZE.x),
		AtmoUnitToTexel(clamp(h / ATMO_H_TOP, 0.0, 1.0), ATMO_MULTISCATTER_SIZE.y));
}

vec3 AtmoMultiScatter(float h, float muSun)
{
	return texture(u_AtmosphereMultiScatterMap, AtmoMultiScatterUV(max(h, 0.0), muSun)).rgb;
}

// sky-view LUT (Hillaire): u = sqrt of the azimuth from the sun ((1 - cos) / 2), v = view zenith, non linear
// around the horizon (v = 0.5)
//
// Both directions work with the signed angle delta between the view and the horizon (zenith = zenithHorizon +
// delta) through exact identities, never acos(cos(zenith)): just below the horizon of a camera a few metres
// above the ground the path to the ground changes by ~1e4 km per unit of cos zenith, and the GPUs' acos / cos
// error (~2e-5 rad) moved it by 7 % (tools/rend2/test_atmosphere_gl.py).
//   cos(zenithHorizon) = -sqrt(r^2 - R^2) / r, sin(zenithHorizon) = R / r
void AtmoSkyViewHorizon(float h, out float beta, out float cosHorizon, out float sinHorizon)
{
	float r = ATMO_R_GROUND + h;
	float c = sqrt(AtmoRho2(h)) / r;
	beta = acos(clamp(c, 0.0, 1.0));
	cosHorizon = -c;
	sinHorizon = ATMO_R_GROUND / r;
}

vec2 AtmoSkyViewUV(float h, float cosZenith, float cosLight)
{
	h = max(h, ATMO_MIN_ALTITUDE);
	float beta, ch, sh;
	AtmoSkyViewHorizon(h, beta, ch, sh);
	float zenithHorizon = ATMO_PI - beta;
	float cz = clamp(cosZenith, -1.0, 1.0);
	float sz = sqrt(max(1.0 - cz * cz, 0.0));
	// zenith - zenithHorizon
	float delta = atan(sz * ch - cz * sh, cz * ch + sz * sh);
	float v;
	if (delta < 0.0)
		v = (1.0 - sqrt(clamp(-delta / zenithHorizon, 0.0, 1.0))) * 0.5;
	else
		v = sqrt(clamp(delta / beta, 0.0, 1.0)) * 0.5 + 0.5;
	float u = sqrt(clamp((1.0 - cosLight) * 0.5, 0.0, 1.0));
	return vec2(AtmoUnitToTexel(u, ATMO_SKYVIEW_SIZE.x), AtmoUnitToTexel(v, ATMO_SKYVIEW_SIZE.y));
}

void AtmoSkyViewFromUV(vec2 uv, float h, out float cosZenith, out float cosLight)
{
	float u = clamp(AtmoTexelToUnit(uv.x, ATMO_SKYVIEW_SIZE.x), 0.0, 1.0);
	float v = clamp(AtmoTexelToUnit(uv.y, ATMO_SKYVIEW_SIZE.y), 0.0, 1.0);
	h = max(h, ATMO_MIN_ALTITUDE);
	float beta, ch, sh;
	AtmoSkyViewHorizon(h, beta, ch, sh);
	float zenithHorizon = ATMO_PI - beta;
	float delta;
	if (v < 0.5)
	{
		float c = 1.0 - 2.0 * v;
		delta = -zenithHorizon * c * c;
	}
	else
	{
		float c = 2.0 * v - 1.0;
		delta = beta * c * c;
	}
	cosZenith = ch * cos(delta) - sh * sin(delta);
	cosLight = 1.0 - 2.0 * u * u;
}

// cos of the azimuth between a world direction and the sun (z up); 1 when either is vertical
float AtmoCosLight(vec3 dir, vec3 sunDir)
{
	vec2 a = dir.xy;
	vec2 b = sunDir.xy;
	float la = dot(a, a);
	float lb = dot(b, b);
	if (la < 1e-8 || lb < 1e-8)
		return 1.0;
	return clamp(dot(a, b) * inversesqrt(la * lb), -1.0, 1.0);
}

// sky radiance per unit sun illuminance from the camera altitude along dir (world, z up)
vec3 AtmoSkyView(vec3 dir)
{
	float h = u_Atmosphere[7].y;
	return texture(u_AtmosphereSkyViewMap,
		AtmoSkyViewUV(h, dir.z, AtmoCosLight(dir, u_Atmosphere[2].xyz))).rgb;
}

// --- aerial perspective ------------------------------------------------------------------------------------------

// Integral of exp(-(h0 + a t) / H) dt over [t0, t1]
float AtmoExpIntegral(float h0, float a, float t0, float t1, float H)
{
	float L = t1 - t0;
	float x = a * L / H;
	float e0 = exp(-(h0 + a * t0) / H);
	if (abs(x) < 1e-3)
		return L * e0 * (1.0 - 0.5 * x + x * x / 6.0);
	return e0 * H / a * (1.0 - exp(-x));
}

// In-scattering (Rayleigh, Mie separately, per unit sun illuminance) and transmittance along a straight path
// from the camera: h0 = camera altitude (km), dirZ = vertical component of the world direction, len = path
// length in km (already scaled by r_atmosphereAerialScale, which does not scale altitudes), nu = cos of the
// angle between the path and the sun, mieGlow scales the Mie phase. Flat planet along the path; ozone is not
// evaluated (its density is 0 below 10 km).
void AtmosphereAerial(float h0, float dirZ, float len, float nu, float mieGlow,
	out vec3 inscatterR, out vec3 inscatterM, out vec3 T)
{
	float aerialScale = max(u_Atmosphere[1].y, 1e-3);
	float a = dirZ / aerialScale;
	float muSun = u_Atmosphere[2].z;
	float g = u_Atmosphere[0].w;
	float pr = AtmoPhaseRayleigh(nu);
	float pm = AtmoPhaseMie(nu, g) * mieGlow;
	float rayleighScale = u_Atmosphere[0].x;
	float mieScale = u_Atmosphere[0].y;

	inscatterR = vec3(0.0);
	inscatterM = vec3(0.0);
	T = vec3(1.0);
	for (int s = 0; s < ATMO_AERIAL_SEGMENTS; s++)
	{
		float t0 = len * float(s) / float(ATMO_AERIAL_SEGMENTS);
		float t1 = len * float(s + 1) / float(ATMO_AERIAL_SEGMENTS);
		float ir = rayleighScale * AtmoExpIntegral(h0, a, t0, t1, ATMO_RAYLEIGH_H);
		float im = mieScale * AtmoExpIntegral(h0, a, t0, t1, ATMO_MIE_H);
		vec3 tauR = ATMO_RAYLEIGH_SCATTERING * ir;
		float tauMs = ATMO_MIE_SCATTERING * im;
		vec3 tau = tauR + vec3(ATMO_MIE_EXTINCTION * im);
		vec3 segT = exp(-tau);

		// the source where half of the segment's opacity is reached (green, homogeneous approximation)
		float f = tau.g < 1e-4 ? 0.5 : -log(0.5 * (1.0 + segT.g)) / tau.g;
		float h = max(h0 + a * (t0 + f * (t1 - t0)), 0.0);
		vec3 sunT = AtmoTransmittanceToSpace(h, muSun);
		vec3 ms = AtmoMultiScatter(h, muSun);

		vec3 oneMinus = mix(vec3(1.0) - 0.5 * tau, (vec3(1.0) - segT) / max(tau, vec3(1e-30)),
			greaterThan(tau, vec3(1e-6)));
		inscatterR += T * tauR * (pr * sunT + ms) * oneMinus;
		inscatterM += T * tauMs * (pm * sunT + ms) * oneMinus;
		T *= segT;
	}
}

// camera altitude in km for a world position (r_atmosphereUnitScale, r_atmosphereGroundZ, r_atmosphereAltitude)
float AtmoAltitude(float worldZ)
{
	return u_Atmosphere[1].w + (worldZ - u_Atmosphere[1].z) * u_Atmosphere[1].x * 0.001;
}
