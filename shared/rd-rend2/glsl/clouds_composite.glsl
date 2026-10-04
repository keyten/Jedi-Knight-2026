/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Cloud composite (r_clouds, tr_clouds.cpp). Runs after the atmosphere composite (the sky already carries the
// aerial perspective of the whole path) and the early sun, before the froxel composite (local fog in front of
// the clouds then attenuates them with its own T). On sky pixels only (full resolution depth 1); the resolved
// clouds are upsampled from the march resolution over the low resolution pixels that saw sky.
//
// With the aerial perspective S_ap, T_ap of the air between the camera and the cloud (distance Dc), the sky
// behind the cloud as seen from the cloud is (dst - S_ap) / T_ap, so
//   out = S_ap + T_ap (S_c + T_c (dst - S_ap) / T_ap) = T_ap S_c + (1 - T_c) S_ap + T_c dst
// one draw, blend ONE, SRC_ALPHA: color = (T_ap S_c + (1 - T_c) S_ap, T_c), glow = (0, T_c).
//
// u_Cloud[18].x = 1: sun ray mask, into the sun flare target (blend ZERO, SRC_COLOR): T_c of the clouds.
// Debug views replace the sky pixels (debug 11: the cloud shadow map on every pixel).

uniform sampler2D u_ScreenDepthMap;
uniform sampler2D u_CloudCurrentMap;
uniform sampler2D u_CloudCurrentDepthMap;
uniform sampler2D u_CloudShadowMap;
uniform mat4 u_CloudInvViewProjection;	// clip -> world offset from the camera (no translation)
uniform vec4 u_CloudShadow[2];

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	int mode = int(u_Cloud[18].x);
	int debugView = int(u_Cloud[16].x);
	vec2 tc = gl_FragCoord.xy / r_FBufScale;
	vec2 lowPos = u_Cloud[15].xy + (tc - u_Cloud[14].xy) / u_Cloud[14].zw * u_Cloud[15].zw;

	vec4 cloud;
	float cloudDistance;
	if (mode == 1)
	{
		// the sun flare target has the scene depth attached: no depth read here, the flare is depth tested
		bool hasCloud = CloudFetchBilinear(u_CloudCurrentMap, u_CloudCurrentDepthMap, lowPos, cloud, cloudDistance);
		float T = hasCloud ? clamp(cloud.a, 0.0, 1.0) : 1.0;
		out_Color = vec4(T, T, T, 1.0);
		out_Glow = vec4(T);
		return;
	}

	float depth = texture(u_ScreenDepthMap, tc).r;
	if (debugView == 11)
	{
		// the cloud shadow map projected on the scene (r_cloudShadows), sky = blue
		vec3 c = vec3(0.0, 0.0, 0.3);
		if (depth < 1.0)
		{
			vec2 ndc = (tc - u_Cloud[14].xy) / u_Cloud[14].zw * 2.0 - 1.0;
			vec4 p = u_CloudInvViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
			vec3 world = u_Cloud[19].xyz + p.xyz / p.w;
			vec2 q = world.xy - u_CloudShadow[1].xy * (world.z - u_CloudShadow[0].w);
			vec2 uv = (q - u_CloudShadow[0].xy) * u_CloudShadow[0].z + 0.5;
			bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
			c = inside ? vec3(textureLod(u_CloudShadowMap, uv, 0.0).r) : vec3(0.5, 0.0, 0.0);
		}
		out_Color = vec4(c, 0.0);
		out_Glow = vec4(0.0);
		return;
	}

	if (depth < 1.0)
		discard;
	if (!CloudFetchBilinear(u_CloudCurrentMap, u_CloudCurrentDepthMap, lowPos, cloud, cloudDistance))
		discard;

	if (debugView > 0)
	{
		out_Color = vec4(cloud.rgb, 0.0);
		out_Glow = vec4(0.0);
		return;
	}

	float Tc = clamp(cloud.a, 0.0, 1.0);
	vec3 color = cloud.rgb;
	if (u_Cloud[18].y > 0.5)
	{
		// aerial perspective to the cloud (r_atmosphere, physical distance)
		vec2 ndc = (tc - u_Cloud[14].xy) / u_Cloud[14].zw * 2.0 - 1.0;
		vec4 nearPoint = u_CloudInvViewProjection * vec4(ndc, -1.0, 1.0);
		vec4 farPoint = u_CloudInvViewProjection * vec4(ndc, 1.0, 1.0);
		vec3 dir = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
		float h0 = max(AtmoAltitude(u_Cloud[19].z), 0.0);
		vec3 sR, sM, Tap;
		AtmosphereAerial(h0, dir.z, cloudDistance, dot(dir, u_Atmosphere[2].xyz), 1.0, sR, sM, Tap);
		vec3 Sap = (sR + sM) * u_Atmosphere[3].rgb * CLOUD_PI;
		color = Tap * cloud.rgb + (1.0 - Tc) * Sap;
	}
	out_Color = vec4(color, Tc);
	out_Glow = vec4(0.0, 0.0, 0.0, Tc);
}
