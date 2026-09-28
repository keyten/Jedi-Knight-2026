/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Froxel fog composite (tr_volumetric.cpp): the fog of everything drawn before it (the layers up to
// SS_FOG, the sky included) from the depth buffer, in the HDR scene before tone mapping.
//
// Blend ONE, SRC_ALPHA:  color = color * T + S,  glow = glow * T + bloom
//
// The glow buffer is the source of bloom: the fog attenuates it like the scene (as the legacy fog
// pass). r_volumetricFogBloom adds the bright part of the in-scattering (soft knee), so light beams
// bloom and the dim haze does not.
//
// RGB extinction (r_volumetricFogRGB): scene.rgb * T.rgb + S cannot be one fixed-function blend with a
// scalar source alpha, so the composite is drawn twice (RB_VolumetricComposite):
//   u_FroxelFogMode 3  blend ZERO, SRC_COLOR  out = T.rgb      color = color * T, glow = glow * T
//   u_FroxelFogMode 4  blend ONE, ONE         out = S, bloom   color += S, glow += bloom

uniform sampler2D u_ScreenDepthMap;

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	vec2 tc = gl_FragCoord.xy / r_FBufScale;
	float depth = texture(u_ScreenDepthMap, tc).r;

	vec3 worldPos = FroxelSceneWorldPosition(tc, depth);
#if defined(USE_FROXEL_RGB)
	vec3 T;
	vec4 fog = vec4(FroxelFogRGB(worldPos, T), 1.0);
	if (u_FroxelFogMode == 3)
	{
		out_Color = vec4(T, 1.0);
		out_Glow = vec4(T, 1.0);
		return;
	}
#else
	vec4 fog = FroxelFog(worldPos);
#endif

	vec3 bloom = vec3(0.0);
	float bloomScale = u_FroxelDebugParams.y;
	if (bloomScale > 0.0)
	{
		float luma = dot(fog.rgb, vec3(0.2126, 0.7152, 0.0722));
		float knee = clamp(luma - 0.5, 0.0, 1.0);
		bloom = fog.rgb * knee * knee * bloomScale;
	}

#if defined(USE_FROXEL_RGB)
	out_Color = vec4(fog.rgb, 0.0);
	out_Glow = vec4(bloom, 0.0);
#else
	out_Color = vec4(fog.rgb, fog.a);
	out_Glow = vec4(bloom, fog.a);
#endif
}
