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
	if (coverage <= 0.001)
	{
		out_Color = vec4(scene, 1.0);
		return;
	}

	float aspect = screenSize.x / screenSize.y;
	vec2 refrUV = clamp(uv + lens.xy * u_RainLensParams.z, vec2(0.0), vec2(1.0));
	float blur = max(lens.w, 0.0);
	vec3 refracted = textureLod(u_ScreenImageMap, refrUV, 0.0).rgb;
	vec2 slope = vec2(0.0);
	if (blur > 1e-5)
	{
		vec2 bx = vec2(blur / aspect, blur * 0.3);
		vec2 by = vec2(-blur * 0.3 / aspect, blur);
		refracted *= 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV + bx, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV - bx, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV + by, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV - by, 0.0).rgb * 0.2;

		// blur = radiusUV * 0.18 * drop mask. Recover the cap slope
		// from the unscaled offset; the tiny film offset is negligible here.
		slope = -lens.xy * vec2(aspect, 1.0) / max((blur / 0.18) * 1.6, 1e-5);
		// Artistic edge attenuation until a reflected environment is available.
		float rim = clamp(dot(slope, slope) * 0.5, 0.0, 1.0);
		refracted *= 0.97 - 0.09 * rim;
		vec3 n = normalize(vec3(slope * 0.8, 1.0));
		vec3 halfVec = normalize(vec3(-0.35, 0.55, 1.0) + vec3(0.0, 0.0, 1.0));
		float spec = pow(max(dot(n, halfVec), 0.0), 80.0);
		refracted += spec * Luminance(refracted) * 0.6;
	}
	out_Color = vec4(mix(scene, refracted, coverage), 1.0);
}
