/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Atmosphere multiple scattering LUT (Hillaire 2020, section 5.5): the infinite series of isotropic
// scattering orders, Psi = L2 / (1 - f_ms), per altitude and cos sun zenith, for a unit sun illuminance.
// 32 x 32 RGBA16F, rebuilt with the transmittance LUT (which it reads).

out vec4 out_Color;

#define ATMO_MS_DIRECTIONS_SQRT 8
#define ATMO_MS_STEPS 20

void main()
{
	vec2 uv = gl_FragCoord.xy / ATMO_MULTISCATTER_SIZE;
	float muSun = clamp(AtmoTexelToUnit(uv.x, ATMO_MULTISCATTER_SIZE.x), 0.0, 1.0) * 2.0 - 1.0;
	float h = max(clamp(AtmoTexelToUnit(uv.y, ATMO_MULTISCATTER_SIZE.y), 0.0, 1.0) * ATMO_H_TOP, ATMO_MIN_ALTITUDE);
	float r = ATMO_R_GROUND + h;
	vec3 sunDir = vec3(sqrt(max(1.0 - muSun * muSun, 0.0)), 0.0, muSun);
	const float isotropic = 1.0 / (4.0 * ATMO_PI);

	vec3 L2 = vec3(0.0);
	vec3 fms = vec3(0.0);
	const int N = ATMO_MS_DIRECTIONS_SQRT;
	for (int i = 0; i < N; i++)
	{
		for (int j = 0; j < N; j++)
		{
			// uniform directions on the sphere
			float cosTheta = 1.0 - 2.0 * (float(i) + 0.5) / float(N);
			float phi = 2.0 * ATMO_PI * (float(j) + 0.5) / float(N);
			float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
			vec3 dir = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

			float mu = dir.z;
			float dGround = AtmoDistanceToGround(h, mu);
			float d = dGround >= 0.0 ? dGround : AtmoDistanceToTop(h, mu);
			float dt = d / float(ATMO_MS_STEPS);

			vec3 T = vec3(1.0);
			vec3 L = vec3(0.0);
			vec3 f = vec3(0.0);
			for (int s = 0; s < ATMO_MS_STEPS; s++)
			{
				float t = (float(s) + 0.5) * dt;
				vec3 p = vec3(0.0, 0.0, r) + dir * t;
				float rs = length(p);
				float hs = rs - ATMO_R_GROUND;
				float muS = dot(p, sunDir) / rs;
				vec3 scattering = ATMO_RAYLEIGH_SCATTERING * AtmoRayleighDensity(hs) +
					vec3(ATMO_MIE_SCATTERING * AtmoMieDensity(hs));
				vec3 sigmaT = AtmoExtinction(hs);
				vec3 segT = exp(-sigmaT * dt);
				vec3 weight = (vec3(1.0) - segT) / max(sigmaT, vec3(1e-9));

				vec3 S = scattering * isotropic * AtmoTransmittanceToSpace(hs, muS);
				L += T * S * weight;
				f += T * scattering * weight;
				T *= segT;
			}

			// light reflected by the ground (Lambert, albedo 0.3)
			if (dGround >= 0.0)
			{
				vec3 p = vec3(0.0, 0.0, r) + dir * dGround;
				vec3 n = normalize(p);
				float muS = dot(n, sunDir);
				L += T * AtmoTransmittanceToSpace(0.0, muS) * max(muS, 0.0) * ATMO_GROUND_ALBEDO / ATMO_PI;
			}

			// sphere average of radiance x isotropic phase x 4 pi
			L2 += L;
			fms += f * isotropic;
		}
	}
	L2 /= float(N * N);
	fms /= float(N * N);
	// f_ms: the scattered share of a unit isotropic radiance, integrated over the sphere (4 pi x isotropic)
	fms *= 4.0 * ATMO_PI;
	vec3 psi = L2 / max(vec3(1.0) - fms, vec3(1e-3));
	out_Color = vec4(psi, 1.0);
}
