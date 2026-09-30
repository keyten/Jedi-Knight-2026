/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Lens water HDR composite (r_rainLens), tr_rainlens.cpp. Full resolution;
// pixels without water cost one field and one scene fetch, film pixels one
// extra refracted fetch, only drops with a blur radius take the defocus taps.

uniform sampler2D u_ScreenImageMap; // HDR scene
uniform sampler2D u_TextureMap;     // RG: unscaled offset, B: optical weight, A: blur radius
uniform sampler2D u_NormalMap;      // wetness (R) / film (G), debug view 6 only
uniform vec4 u_RainLensParams;      // z: refraction strength, w: scene samples (1, 3, 5)
uniform vec4 u_RainLensParams2;     // z: debug view, w: display encoded HDR
out vec4 out_Color;

// offset of a reference drop edge (radius 0.02 * refraction scale 1.6):
// turns the field offset back into an approximate surface slope
#define REFERENCE_OFFSET 0.032

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
	// the field is additive: overlaps can exceed one
	float weight = clamp(lens.z, 0.0, 1.0);
	float blur = clamp(lens.w, 0.0, 0.02);
	float aspect = screenSize.x / screenSize.y;
	vec2 slope = -lens.xy * vec2(aspect, 1.0) / REFERENCE_OFFSET;

	int debugView = int(u_RainLensParams2.z);
	if (debugView == 1)
	{
		out_Color = vec4(weight, clamp(blur * 40.0, 0.0, 1.0), 0.0, 1.0);
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
	if (debugView == 6)
	{
		// film green, wetness (path affinity) blue
		vec2 water = texture(u_NormalMap, uv).rg;
		out_Color = vec4(0.0, clamp(water.y, 0.0, 1.0), clamp(water.x, 0.0, 1.0) * 0.6, 1.0);
		return;
	}

	vec3 color = scene;
	if (weight > 0.001)
	{
		vec2 refrUV = clamp(uv + lens.xy * u_RainLensParams.z, vec2(0.0), vec2(1.0));
		vec3 refracted = textureLod(u_ScreenImageMap, refrUV, 0.0).rgb;
		int samples = int(u_RainLensParams.w + 0.5);
		if (blur > 1e-5 && samples > 1)
		{
			// defocus: a small rotated disc, only inside drops
			vec2 bx = vec2(blur / aspect, blur * 0.3);
			vec2 by = vec2(-blur * 0.3 / aspect, blur);
			if (samples >= 5)
			{
				refracted *= 0.2;
				refracted += textureLod(u_ScreenImageMap, refrUV + bx, 0.0).rgb * 0.2;
				refracted += textureLod(u_ScreenImageMap, refrUV - bx, 0.0).rgb * 0.2;
				refracted += textureLod(u_ScreenImageMap, refrUV + by, 0.0).rgb * 0.2;
				refracted += textureLod(u_ScreenImageMap, refrUV - by, 0.0).rgb * 0.2;
			}
			else
			{
				vec2 d = (bx + by) * 0.7071;
				refracted += textureLod(u_ScreenImageMap, refrUV + d, 0.0).rgb;
				refracted += textureLod(u_ScreenImageMap, refrUV - d, 0.0).rgb;
				refracted *= 1.0 / 3.0;
			}
		}

		// Compact drops only (thin film stays transparent): artistic edge
		// attenuation and a glint tied to the local luminance until reflected
		// lighting is available.
		float drop = smoothstep(0.25, 0.6, weight);
		float rim = clamp(dot(slope, slope) * 0.5, 0.0, 1.0);
		refracted *= 1.0 - drop * (0.03 + 0.09 * rim);
		vec3 n = normalize(vec3(slope * 0.8, 1.0));
		vec3 halfVec = normalize(vec3(-0.35, 0.55, 1.0) + vec3(0.0, 0.0, 1.0));
		float spec = pow(max(dot(n, halfVec), 0.0), 80.0);
		refracted += spec * Luminance(refracted) * 0.6 * drop;

		color = mix(scene, refracted, weight);
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
