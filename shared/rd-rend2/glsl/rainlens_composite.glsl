/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
uniform sampler2D u_ScreenImageMap; // HDR scene
uniform sampler2D u_TextureMap;     // RG: unscaled offset, B: coverage, A: blur radius
uniform vec4 u_RainLensParams;      // z: refraction strength
uniform vec4 u_RainLensParams2;     // z: debug view, w: display encoded HDR
out vec4 out_Color;

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec2 screenSize = vec2(textureSize(u_ScreenImageMap, 0));
	vec2 uv = gl_FragCoord.xy / screenSize;
	vec3 scene = texelFetch(u_ScreenImageMap, ivec2(gl_FragCoord.xy), 0).rgb;
	vec4 lens = texture(u_TextureMap, uv);
	float coverage = clamp(lens.z, 0.0, 1.0);
	float aspect = screenSize.x / screenSize.y;
	float blur = max(lens.w, 0.0);
	vec2 slope = blur > 1e-5 ?
		-lens.xy * vec2(aspect, 1.0) / max((blur / 0.18) * 1.6, 1e-5) : vec2(0.0);
	int debugView = int(u_RainLensParams2.z);
	if (debugView == 1)
	{
		out_Color = vec4(coverage, clamp(blur * 40.0, 0.0, 1.0), 0.0, 1.0);
		return;
	}
	if (debugView == 2)
	{
		out_Color = vec4(normalize(vec3(slope * 0.8, 1.0)) * 0.5 + 0.5, 1.0);
		return;
	}
	if (debugView == 3)
	{
		out_Color = vec4(abs(lens.xy * u_RainLensParams.z) * 40.0, 0.0, 1.0);
		return;
	}

	vec3 color = scene;
	if (coverage > 0.001)
	{
		vec2 refrUV = clamp(uv + lens.xy * u_RainLensParams.z, vec2(0.0), vec2(1.0));
		vec3 refracted = textureLod(u_ScreenImageMap, refrUV, 0.0).rgb;
		if (blur > 1e-5)
		{
			vec2 bx = vec2(blur / aspect, blur * 0.3);
			vec2 by = vec2(-blur * 0.3 / aspect, blur);
			refracted *= 0.2;
			refracted += textureLod(u_ScreenImageMap, refrUV + bx, 0.0).rgb * 0.2;
			refracted += textureLod(u_ScreenImageMap, refrUV - bx, 0.0).rgb * 0.2;
			refracted += textureLod(u_ScreenImageMap, refrUV + by, 0.0).rgb * 0.2;
			refracted += textureLod(u_ScreenImageMap, refrUV - by, 0.0).rgb * 0.2;
			// Artistic edge attenuation until reflected lighting is available.
			float rim = clamp(dot(slope, slope) * 0.5, 0.0, 1.0);
			refracted *= 0.97 - 0.09 * rim;
			vec3 n = normalize(vec3(slope * 0.8, 1.0));
			vec3 halfVec = normalize(vec3(-0.35, 0.55, 1.0) + vec3(0.0, 0.0, 1.0));
			float spec = pow(max(dot(n, halfVec), 0.0), 80.0);
			refracted += spec * Luminance(refracted) * 0.6;
		}
		color = mix(scene, refracted, coverage);
	}
	if (debugView == 4)
	{
		vec3 preview = uv.x < 0.5 ? scene : color;
		if (u_RainLensParams2.w < 0.5)
			preview = pow(preview / (1.0 + preview), vec3(1.0 / 2.2));
		color = abs(uv.x - 0.5) < 0.0015 ? vec3(1.0, 0.8, 0.0) : preview;
	}
	out_Color = vec4(color, 1.0);
}
