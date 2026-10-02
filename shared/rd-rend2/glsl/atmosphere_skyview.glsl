/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Atmosphere sky-view LUT (Hillaire 2020, section 5.3): sky radiance per unit sun illuminance from the
// camera altitude, by view zenith and azimuth from the sun. 192 x 108 RGBA16F, rebuilt when the sun direction,
// the camera altitude bucket or the medium changes (tr_atmosphere.cpp).

out vec4 out_Color;

#define ATMO_SKYVIEW_STEPS 32

void main()
{
	float h = max(u_Atmosphere[7].y, ATMO_MIN_ALTITUDE);
	float cosZenith, cosLight;
	AtmoSkyViewFromUV(gl_FragCoord.xy / ATMO_SKYVIEW_SIZE, h, cosZenith, cosLight);

	float muSun = u_Atmosphere[2].z;
	float sinSun = sqrt(max(1.0 - muSun * muSun, 0.0));
	vec3 sunDir = vec3(sinSun, 0.0, muSun);
	float sinZenith = sqrt(max(1.0 - cosZenith * cosZenith, 0.0));
	float sinLight = sqrt(max(1.0 - cosLight * cosLight, 0.0));
	vec3 dir = vec3(sinZenith * cosLight, sinZenith * sinLight, cosZenith);

	float r = ATMO_R_GROUND + h;
	float dGround = AtmoDistanceToGround(h, cosZenith);
	float d = dGround >= 0.0 ? dGround : AtmoDistanceToTop(h, cosZenith);

	float nu = dot(dir, sunDir);
	float pr = AtmoPhaseRayleigh(nu);
	float pm = AtmoPhaseMie(nu, u_Atmosphere[0].w);

	vec3 T = vec3(1.0);
	vec3 L = vec3(0.0);
	// quadratic step distribution: short steps in the dense air near the camera
	float tPrev = 0.0;
	for (int i = 0; i < ATMO_SKYVIEW_STEPS; i++)
	{
		float x = float(i + 1) / float(ATMO_SKYVIEW_STEPS);
		float tNext = d * x * x;
		float dt = tNext - tPrev;
		float t = tPrev + 0.5 * dt;
		tPrev = tNext;

		vec3 p = vec3(0.0, 0.0, r) + dir * t;
		float rs = length(p);
		float hs = rs - ATMO_R_GROUND;
		float muS = dot(p, sunDir) / rs;

		vec3 scatterR = ATMO_RAYLEIGH_SCATTERING * AtmoRayleighDensity(hs);
		float scatterM = ATMO_MIE_SCATTERING * AtmoMieDensity(hs);
		vec3 sigmaT = AtmoExtinction(hs);
		vec3 segT = exp(-sigmaT * dt);

		vec3 sunT = AtmoTransmittanceToSpace(hs, muS);
		vec3 ms = AtmoMultiScatter(hs, muS);
		vec3 S = scatterR * (pr * sunT + ms) + scatterM * (pm * sunT + ms);
		L += T * S * (vec3(1.0) - segT) / max(sigmaT, vec3(1e-9));
		T *= segT;
	}
	out_Color = vec4(L, 1.0);
}
