/*[Vertex]*/
#if defined(USE_DROPS)
uniform sampler2D u_TextureMap;  // instance records, 4 rows (tr_lenswater.h)
uniform vec4 u_RainLensParams;   // x = aspect, y = blur multiplier, w = debug view
uniform vec4 u_RainLensParams2;  // zw = target size

out vec2 var_Local;  // position relative to the instance centre, lens units
flat out vec4 var_T0;
flat out vec4 var_T1;
flat out vec4 var_T2;
flat out vec4 var_T3;

void main()
{
	int id = gl_InstanceID;
	var_T0 = texelFetch(u_TextureMap, ivec2(id, 0), 0);
	var_T1 = texelFetch(u_TextureMap, ivec2(id, 1), 0);
	var_T2 = texelFetch(u_TextureMap, ivec2(id, 2), 0);
	var_T3 = texelFetch(u_TextureMap, ivec2(id, 3), 0);

	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	vec2 axis = var_T1.xy;
	vec2 side = vec2(-axis.y, axis.x);
	// one target texel of margin so small drops stay antialiased
	float pixel = 1.5 / u_RainLensParams2.w;

	vec2 local;
	if (var_T1.w > 4.5)
	{
		// sheet: oriented ribbon, wobble margin across
		local = axis * corner.y * (var_T0.z + pixel) + side * corner.x * (var_T3.w * 1.4 + pixel);
	}
	else
	{
		float extent = var_T0.z * max(var_T1.z, 1.0) * (1.0 + var_T3.w * 0.5);
		if (var_T2.z > 0.0)
			extent = max(extent, length(var_T2.xy) + var_T2.z);
		local = corner * (extent * 1.05 + pixel);
	}
	var_Local = local;

	// lens space (screen height = 1, centred) to clip space
	vec2 lens = var_T0.xy + local;
	vec2 uv = vec2(lens.x / u_RainLensParams.x + 0.5, lens.y + 0.5);
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
#else
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}
#endif

/*[Fragment]*/
// Lens water field (r_rainLens), tr_rainlens.cpp / tr_lenswater.cpp.
//
// Writes the shared lower resolution lens field read by the HDR composite
// and the bloom prefilter:
//   RG = unscaled UV refraction offset, B = optical weight, A = blur radius.
//
// USE_FILM: fullscreen pass over the persistent CPU wetness (R) / film (G)
// texture. A thin trail is almost transparent: weak refraction from the
// film gradient, low weight (at most 0.25), no blur.
//
// USE_DROPS: instanced analytic quads from tr_lenswater.cpp, additively
// blended over the film. A drop is a spherical cap in its own frame (motion
// tail, lopsided impact shape) whose analytic slope bends the view; a merge
// adds a relaxing second lobe, so overlapping heights sum their slopes rather
// than switching normals. A sheet is a broad low-weight ribbon with strong
// distortion. USE_DEBUG_AGENTS draws the same instances as flat colours.

uniform sampler2D u_TextureMap;
uniform vec4 u_RainLensParams;
uniform vec4 u_RainLensParams2;

out vec4 out_Color;

#define REFRACTION_SCALE 1.6

#if defined(USE_DROPS)
in vec2 var_Local;
flat in vec4 var_T0;
flat in vec4 var_T1;
flat in vec4 var_T2;
flat in vec4 var_T3;

// Spherical cap of unit radius at q (in radii). x = soft mask, yz = outward
// slope (zero at the centre, about one at the edge).
vec3 Cap(vec2 q, vec2 lopsided)
{
	float qLength = length(q);
	float egg = 1.0 + dot(q, lopsided);
	float r = qLength * egg;
	if (r >= 1.0)
		return vec3(0.0);
	float h = sqrt(1.0 - r * r);
	float edge = 1.0 - smoothstep(0.78, 1.0, r);
	vec2 radiusGradient = q / max(qLength, 1e-4) * egg + qLength * lopsided;
	return vec3(edge, r * radiusGradient / max(h, 0.35));
}

// Sheet thickness at a point relative to its centre (0..1). The wobble
// follows the sheet: it moves rigidly, it does not swim.
float SheetHeight(vec2 local)
{
	vec2 axis = var_T1.xy;
	vec2 side = vec2(-axis.y, axis.x);
	float a = dot(local, axis) / max(var_T0.z, 1e-4);
	float c = dot(local, side) / max(var_T3.w, 1e-4);
	float seed = var_T2.w;
	c += 0.18 * sin(a * 3.1 + seed * 40.0) + 0.08 * sin(a * 7.3 + seed * 17.0);
	float across = cos(clamp(c, -1.0, 1.0) * 1.5707963);
	// thicker, rounded leading edge; thin tail
	float along = mix(0.35, 1.0, smoothstep(-1.0, 0.7, a))
		* (1.0 - smoothstep(0.75, 1.0, a)) * (1.0 - smoothstep(0.8, 1.0, -a));
	return across * across * along;
}

void Drop(out vec2 offset, out float weight, out float blur, out float mask)
{
	float aspect = u_RainLensParams.x;
	float radius = var_T0.z;
	vec2 axis = var_T1.xy;
	vec2 side = vec2(-axis.y, axis.x);

	// drop frame: x across, y along the motion; the tail stretches behind
	vec2 f = vec2(dot(var_Local, side), dot(var_Local, axis));
	float stretch = f.y < 0.0 ? max(var_T1.z, 1.0) : 1.0;
	f.y /= stretch;
	float seed = var_T2.w;
	vec2 lopsided = (vec2(fract(seed * 7.13), fract(seed * 3.71)) - 0.5) * (0.25 + var_T3.w);
	vec3 cap = Cap(f / radius, lopsided);
	// slope back to lens space: q = M p / r, d/dp = M^T d/dq
	vec2 slope = side * cap.y + axis * (cap.z / stretch);
	offset = -slope * radius * REFRACTION_SCALE * cap.x;
	mask = cap.x;
	blur = radius * 0.18 * cap.x;

	// merge: the absorbed drop relaxes into the survivor over ~180 ms
	if (var_T2.z > 0.0)
	{
		vec3 lobe = Cap((var_Local - var_T2.xy) / var_T2.z, vec2(0.0));
		offset -= lobe.yz * var_T2.z * REFRACTION_SCALE * lobe.x;
		mask = 1.0 - (1.0 - mask) * (1.0 - lobe.x);
		blur = max(blur, var_T2.z * 0.18 * lobe.x);
	}
	weight = mask * var_T0.w;
	offset.x /= aspect;
}

void Sheet(out vec2 offset, out float weight, out float blur, out float mask)
{
	float aspect = u_RainLensParams.x;
	float strength = var_T0.w;
	float e = 0.25 * var_T3.w;
	float h = SheetHeight(var_Local);
	vec2 gradient = vec2(
		SheetHeight(var_Local + vec2(e, 0.0)) - SheetHeight(var_Local - vec2(e, 0.0)),
		SheetHeight(var_Local + vec2(0.0, e)) - SheetHeight(var_Local - vec2(0.0, e))) / (2.0 * e);
	// strong distortion, low optical weight
	offset = gradient * var_T3.w * 0.02 * strength;
	offset.x /= aspect;
	mask = smoothstep(0.02, 0.3, h);
	weight = mask * 0.2 * strength;
	blur = 0.002 * h * strength;
}
#endif

void main()
{
#if defined(USE_FILM)
	float aspect = u_RainLensParams.x;
	float filmAmount = u_RainLensParams.y;
	vec2 uv = gl_FragCoord.xy / u_RainLensParams2.zw;
	vec2 texel = 1.0 / vec2(textureSize(u_TextureMap, 0));
	float film = texture(u_TextureMap, uv).g;
	// gradient per film cell (square in lens space)
	vec2 gradient = 0.5 * vec2(
		texture(u_TextureMap, uv + vec2(texel.x, 0.0)).g - texture(u_TextureMap, uv - vec2(texel.x, 0.0)).g,
		texture(u_TextureMap, uv + vec2(0.0, texel.y)).g - texture(u_TextureMap, uv - vec2(0.0, texel.y)).g);
	vec2 offset = gradient * 0.035 * filmAmount;
	offset.x /= aspect;
	// Never above 0.25: the composite takes weight > 0.25 for a compact drop
	// (rim, glint). r_rainLensFilm above one only strengthens the refraction.
	float weight = min(film * 0.45, 0.25) * min(filmAmount, 1.0);
	out_Color = vec4(offset, weight, 0.0);
#elif defined(USE_DROPS)
	vec2 offset;
	float weight, blur, mask;
	bool sheet = var_T1.w > 4.5;
	if (sheet)
		Sheet(offset, weight, blur, mask);
	else
		Drop(offset, weight, blur, mask);
	if (mask <= 0.0)
		discard;

#if defined(USE_DEBUG_AGENTS)
	int view = int(u_RainLensParams.w);
	int type = int(var_T1.w + 0.5);
	vec3 color;
	float alpha = 0.6;
	if (view == 7)
	{
		// Fdrive / Fpin: blue pinned, red beyond the depinning threshold
		color = sheet || type == 0 ? vec3(0.4) : mix(vec3(0.1, 0.3, 1.0), vec3(1.0, 0.15, 0.1),
			smoothstep(0.6, 1.2, var_T3.x));
	}
	else if (view == 8)
	{
		// transient state: impact yellow, settling orange, merge lobe cyan, sheet magenta
		int state = int(var_T3.y + 0.5);
		color = vec3(0.35);
		alpha = 0.2;
		if (sheet) { color = vec3(1.0, 0.2, 1.0); alpha = 0.6; }
		else if (state == 0) { color = vec3(1.0, 1.0, 0.1); alpha = 0.8; }
		else if (state == 1) { color = vec3(1.0, 0.55, 0.1); alpha = 0.8; }
		if (!sheet && var_T2.z > 0.0 && length(var_Local - var_T2.xy) < var_T2.z)
		{
			color = vec3(0.1, 1.0, 1.0);
			alpha = 0.8;
		}
	}
	else
	{
		// agents: pinned blue, moving green, flow red, residual yellow,
		// micro grey, sheet magenta
		if (sheet) color = vec3(1.0, 0.2, 1.0);
		else if (type == 0) color = vec3(0.6);
		else if (type == 3) color = vec3(1.0, 0.15, 0.1);
		else if (type == 4) color = vec3(1.0, 0.9, 0.1);
		else if (var_T3.z > 0.5) color = vec3(0.1, 1.0, 0.2);
		else color = vec3(0.1, 0.35, 1.0);
	}
	// solid outline, translucent fill
	float outline = 1.0 - smoothstep(0.0, 0.25, mask);
	out_Color = vec4(color, max(alpha * mask, outline * 0.9) * step(0.001, mask));
#else
	out_Color = vec4(offset, weight, blur * u_RainLensParams.y);
#endif
#else
	out_Color = vec4(0.0);
#endif
}
