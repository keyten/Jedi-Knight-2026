/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Atmosphere transmittance LUT (r_atmosphere, tr_atmosphere.cpp): T from an altitude along a direction above
// the horizon to the top of the atmosphere. 256 x 64 RGBA16F, rebuilt when the medium changes.

out vec4 out_Color;

#define ATMO_TRANSMITTANCE_STEPS 40

void main()
{
	float h, mu;
	AtmoTransmittanceFromUV(gl_FragCoord.xy / ATMO_TRANSMITTANCE_SIZE, h, mu);

	float d = AtmoDistanceToTop(h, mu);
	float r = ATMO_R_GROUND + h;
	float dt = d / float(ATMO_TRANSMITTANCE_STEPS);
	vec3 tau = vec3(0.0);
	for (int i = 0; i < ATMO_TRANSMITTANCE_STEPS; i++)
	{
		float t = (float(i) + 0.5) * dt;
		float hs = sqrt(r * r + t * t + 2.0 * r * mu * t) - ATMO_R_GROUND;
		tau += AtmoExtinction(hs) * dt;
	}
	out_Color = vec4(exp(-tau), 1.0);
}
