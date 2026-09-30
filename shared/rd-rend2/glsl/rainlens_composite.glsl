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
//
// Water is a dielectric (n = 1.333, F0 = 0.02): Schlick Fresnel splits the
// light between the refracted scene and a reflection of the environment,
// the nearest cubemap (USE_CUBEMAP) or the light grid ambient at the camera.
// Glints come from the dominant light (the sun outdoors) and the brightest
// nearby dynamic light, never from a fixed screen direction.

uniform sampler2D u_ScreenImageMap; // HDR scene
uniform sampler2D u_TextureMap;     // RG: unscaled offset, B: optical weight, A: physical blur radius
uniform sampler2D u_NormalMap;      // wetness (R) / film (G), debug view 6 only
uniform sampler2D u_SpecularMap;    // r_rainLensMipBlur: mipped half resolution scene
#if defined(USE_CUBEMAP)
uniform samplerCube u_CubeMap;      // nearest environment probe (alpha: probe luma)
#endif
uniform vec4 u_RainLensParams;      // x: r_rainLensBlur, z: refraction strength, w: scene samples (1, 3, 5)
uniform vec4 u_RainLensParams2;     // z: debug view, w: display encoded HDR
// lens space (x right, y up, z toward the viewer):
//  [0] key light direction, w: reflection amount
//  [1] key light radiance, w: cubemap mip
//  [2] nearby light direction, w: light grid luma at the camera
//  [3] nearby light radiance, w: mip blur on
//  [4] ambient (reflection without cubemap), w: mip blur image height
//  [5] transmitted tint (acid rain)
//  [6..8] lens to world: right, up, toward the viewer
uniform vec4 u_RainLensOptics[10];
uniform vec4 u_RainLensDebug[4];    // debug view 9 controller panel
out vec4 out_Color;

// The field stores a drop's offset = -slope * radius * mask * 1.6 and its
// physical blur A = radius * mask * 0.18 (rainlens.glsl REFRACTION_SCALE,
// BLUR_SCALE), so offset / (A * 1.6 / 0.18) is the cap slope again at any
// drop size, mask included. Film writes no A: its offset is turned back into
// a slope with the edge offset of a reference drop (radius 0.02 * 1.6).
#define OFFSET_PER_BLUR (1.6 / 0.18)
#define REFERENCE_OFFSET 0.032
#define WATER_F0 0.02

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// sky above, ground below: the ambient, a little of the key light upward
vec3 AmbientReflection(vec3 r)
{
	float upward = clamp(r.y * 0.5 + 0.5, 0.0, 1.0);
	return (u_RainLensOptics[4].rgb * mix(0.6, 1.2, upward) + u_RainLensOptics[1].rgb * 0.15 * upward)
		* u_RainLensOptics[0].w;
}

// detailed: compact drops and sheets take the cubemap, the nearly flat film
// (Fresnel at about F0) only the ambient
vec3 Reflection(vec3 n, bool detailed)
{
	// reflect the view ray (toward the viewer, +z) about the drop normal
	vec3 r = 2.0 * n.z * n - vec3(0.0, 0.0, 1.0);
#if defined(USE_CUBEMAP)
	if (detailed)
	{
		vec3 world = u_RainLensOptics[6].xyz * r.x + u_RainLensOptics[7].xyz * r.y + u_RainLensOptics[8].xyz * r.z;
		vec4 probe = textureLod(u_CubeMap, world, u_RainLensOptics[1].w);
		// like lightall: the probe never gets brighter than the light here
		probe.rgb *= clamp(u_RainLensOptics[2].w / max(probe.a, 1e-4), 0.0, 1.0);
		return probe.rgb * u_RainLensOptics[0].w;
	}
#endif
	return AmbientReflection(r);
}

// normalised Blinn-Phong glint of a light seen in the drop
vec3 Glint(vec3 n, vec3 l, vec3 radiance, float power)
{
	if (dot(radiance, radiance) <= 0.0)
		return vec3(0.0);
	vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
	float nl = max(dot(n, l), 0.0);
	float spec = pow(max(dot(n, h), 0.0), power) * (power + 8.0) / 25.1327;
	return radiance * spec * nl;
}

// debug view 9: controller state bars in the top left corner
vec3 ControllerPanel(vec2 pixel, vec2 screenSize, vec3 color)
{
	vec2 origin = vec2(16.0, screenSize.y - 16.0);
	vec2 p = vec2(pixel.x - origin.x, origin.y - pixel.y);
	float barHeight = 10.0, gap = 4.0, width = 220.0;
	int bar = int(floor(p.y / (barHeight + gap)));
	if (p.x < -4.0 || p.x > width + 4.0 || p.y < -4.0 || bar > 12)
		return color;
	color *= 0.35;
	if (bar < 0 || mod(p.y, barHeight + gap) > barHeight || p.x < 0.0 || p.x > width)
		return color;

	if (bar == 12)
	{
		// profile swatch, white edge when forced
		bool edge = p.x < 2.0 || p.x > width - 2.0 || mod(p.y, barHeight + gap) < 2.0;
		return edge && u_RainLensDebug[3].w > 0.5 ? vec3(1.0) : u_RainLensDebug[3].rgb;
	}
	float value = clamp(u_RainLensDebug[bar / 4][bar % 4], 0.0, 1.0);
	// intensity, exposed, facing, map spray | micro, normal, large, flow |
	// sheet, sprays, event flash, drops / limit
	vec3 tint = bar < 4 ? vec3(0.2, 0.9, 1.0) : (bar < 8 ? vec3(0.3, 1.0, 0.3) : vec3(1.0, 0.6, 0.1));
	if (bar == 10)
		tint = vec3(1.0, 0.15, 0.1);
	return p.x < value * width ? tint : vec3(0.08);
}

void main()
{
	vec2 screenSize = vec2(textureSize(u_ScreenImageMap, 0));
	vec2 uv = gl_FragCoord.xy / screenSize;
	vec3 scene = texelFetch(u_ScreenImageMap, ivec2(gl_FragCoord.xy), 0).rgb;
	vec4 lens = texture(u_TextureMap, uv);
	// the field is additive: overlaps can exceed one
	float weight = clamp(lens.z, 0.0, 1.0);
	float blur = clamp(lens.w * u_RainLensParams.x, 0.0, 0.02);
	float aspect = screenSize.x / screenSize.y;
	// physical slope scale; film only (A = 0) and the outermost drop edge
	// fall back to the reference drop
	float physicalOffset = lens.w * OFFSET_PER_BLUR;
	float slopeScale = mix(REFERENCE_OFFSET, physicalOffset, smoothstep(0.0, 0.0015, physicalOffset));
	vec2 slope = -lens.xy * vec2(aspect, 1.0) / slopeScale;

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
		if (blur > 1e-5 && u_RainLensOptics[3].w > 0.5)
		{
			// mipped half resolution copy: one fetch at the blur footprint
			float lod = log2(max(blur * u_RainLensOptics[4].w, 1.0));
			refracted = textureLod(u_SpecularMap, refrUV, lod).rgb;
		}
		else if (blur > 1e-5)
		{
			int samples = int(u_RainLensParams.w + 0.5);
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
			else if (samples >= 3)
			{
				vec2 d = (bx + by) * 0.7071;
				refracted += textureLod(u_ScreenImageMap, refrUV + d, 0.0).rgb;
				refracted += textureLod(u_ScreenImageMap, refrUV - d, 0.0).rgb;
				refracted *= 1.0 / 3.0;
			}
		}
		refracted *= u_RainLensOptics[5].rgb;

		// Fresnel: energy split between transmission and reflection. Thin
		// film is nearly flat, so it reflects at F0 and takes no glint.
		vec3 n = normalize(vec3(slope * 0.8, 1.0));
		float fresnel = WATER_F0 + (1.0 - WATER_F0) * pow(1.0 - clamp(n.z, 0.0, 1.0), 5.0);
		float amount = u_RainLensOptics[0].w;
		fresnel *= min(amount, 1.0);
		// compact drops (weight above the 0.25 film cap) and steep sheets
		float drop = smoothstep(0.25, 0.6, weight);
		bool detailed = amount > 0.0 && (drop > 0.001 || n.z < 0.97);
		vec3 water = refracted * (1.0 - fresnel) + Reflection(n, detailed) * fresnel;
		if (drop > 0.001 && amount > 0.0)
		{
			water += drop * amount * (
				Glint(n, u_RainLensOptics[0].xyz, u_RainLensOptics[1].rgb, 220.0) +
				Glint(n, u_RainLensOptics[2].xyz, u_RainLensOptics[3].rgb, 120.0)) * fresnel * 8.0;
		}

		color = mix(scene, water, weight);
	}
	if (debugView == 4)
	{
		vec3 preview = uv.x < 0.5 ? scene : color;
		if (u_RainLensParams2.w < 0.5)
			preview = pow(preview / (1.0 + preview), vec3(1.0 / 2.2));
		color = abs(uv.x - 0.5) < 0.0015 ? vec3(1.0, 0.8, 0.0) : preview;
	}
	if (debugView == 9)
		color = ControllerPanel(gl_FragCoord.xy, screenSize, color);
	out_Color = vec4(color, 1.0);
}
