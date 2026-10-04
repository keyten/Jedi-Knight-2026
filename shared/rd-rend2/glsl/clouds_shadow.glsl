/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Cloud shadow map (r_cloudShadows, tr_clouds.cpp): the transmittance of the clouds along the sun for points of
// the ground plane (cloud space z = 0) in a square around the camera (u_Cloud[17]: centre, extent in km).
// Everything the map lights lies below the cloud base, where the transmittance is constant along the sun
// direction, so a world point is looked up by sliding it along the sun onto the plane (lightall,
// volumetric_inject: CloudShadow). No erosion (the shadow is soft at this resolution).

// texels of tr.cloudShadowImage (CLOUD_SHADOW_SIZE in tr_clouds.cpp)
#define CLOUD_SHADOW_SIZE 512.0

out vec4 out_Color;

void main()
{
	vec2 p = u_Cloud[17].xy + (gl_FragCoord.xy / CLOUD_SHADOW_SIZE - 0.5) * u_Cloud[17].z;
	vec3 sunDir = u_Cloud[6].xyz;
	float sunZ = max(sunDir.z, 0.05);

	// flat layer over the map (the curvature is negligible over the extent)
	float base = u_Cloud[0].x;
	float top = u_Cloud[0].y;
	float tStart = base / sunZ;
	float tEnd = top / sunZ;
	int n = int(u_Cloud[17].w);
	float dt = (tEnd - tStart) / float(max(n, 1));
	vec3 lods = CloudLods(max(dt, u_Cloud[17].z / 512.0));
	vec3 origin = vec3(p, 0.0);
	float tau = 0.0;
	for (int i = 0; i < 32; i++)
	{
		if (i >= n)
			break;
		vec3 q = origin + sunDir * (tStart + (float(i) + 0.5) * dt);
		tau += CloudDensity(q, CloudAltitude(q), lods, false).extinction * dt;
	}
	out_Color = vec4(exp(-tau), 0.0, 0.0, 1.0);
}
