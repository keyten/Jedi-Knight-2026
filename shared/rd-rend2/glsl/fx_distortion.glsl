/*[Vertex]*/
// r_fxDistortion heat haze / shock distortion (tr_fxupgrades.cpp,
// docs/rend2-fx-upgrades.md). Default: instanced camera facing quads of the
// classified FX sources, added into a half resolution offset buffer.
// USE_COMPOSITE: one full screen pass that refracts the scene by it.
#if defined(USE_COMPOSITE)
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}
#else
uniform sampler2D u_TextureMap;	// row 0: centre xyz, radius; row 1: strength, kind, age 0..1, seed
uniform mat4 u_FxMatrix[2];		// [0] world to clip
uniform vec4 u_FxParams[4];		// [0] xyz view right, w time; [1] xyz view up

out vec2 var_Local;
flat out vec4 var_Source;		// strength, kind, age, seed
flat out vec2 var_Geometry;		// radius, view depth of the centre

void main()
{
	int id = gl_InstanceID;
	vec4 t0 = texelFetch(u_TextureMap, ivec2(id, 0), 0);
	vec4 t1 = texelFetch(u_TextureMap, ivec2(id, 1), 0);
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	vec3 position = t0.xyz + (u_FxParams[0].xyz * corner.x + u_FxParams[1].xyz * corner.y) * t0.w;
	var_Local = corner;
	var_Source = t1;
	var_Geometry = vec2(t0.w, (u_FxMatrix[0] * vec4(t0.xyz, 1.0)).w);
	gl_Position = u_FxMatrix[0] * vec4(position, 1.0);
}
#endif

/*[Fragment]*/
#if defined(USE_COMPOSITE)
uniform sampler2D u_ScreenImageMap;	// resolved copy of the HDR scene
uniform sampler2D u_LightMap;		// offset buffer (RG, scene uv units)
uniform vec4 u_FxParams[4];			// [2] xy 1 / target size, z split x (window pixels, -1 none), w max offset

out vec4 out_Color;

void main()
{
	if (gl_FragCoord.x < u_FxParams[2].z)
		discard;
	vec2 uv = gl_FragCoord.xy * u_FxParams[2].xy;
	vec2 offset = texture(u_LightMap, uv).rg;
	if (dot(offset, offset) < 1.0e-10)
		discard;
	offset = clamp(offset, vec2(-u_FxParams[2].w), vec2(u_FxParams[2].w));
	out_Color = texture(u_ScreenImageMap, clamp(uv + offset, vec2(0.0), vec2(1.0)));
}
#else
uniform sampler2D u_ScreenDepthMap;	// copy of the scene depth (full resolution)
uniform vec4 u_FxParams[4];		// [0] w time; [2] xy 1 / offset buffer size, z proj[10], w proj[14];
								// [3] x amplitude (r_fxDistortionStrength), y max offset

in vec2 var_Local;
flat in vec4 var_Source;
flat in vec2 var_Geometry;

out vec4 out_Color;

float Hash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float ValueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1.0, 0.0)), f.x),
		mix(Hash(i + vec2(0.0, 1.0)), Hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main()
{
	float r = length(var_Local);
	if (r >= 1.0)
		discard;

	float radius = var_Geometry.x;
	float viewDepth = var_Geometry.y;
	vec2 uv = gl_FragCoord.xy * u_FxParams[2].xy;
	float depth = texture(u_ScreenDepthMap, uv).r;
	float sceneDepth = u_FxParams[2].w / (depth * 2.0 - 1.0 + u_FxParams[2].z);
	// a source behind a wall refracts nothing; fade over half its radius
	float soft = clamp((sceneDepth - viewDepth + radius * 0.5) / (radius * 0.5 + 1.0), 0.0, 1.0);

	vec2 offset;
	float age = var_Source.z;
	if (var_Source.y < 0.5)
	{
		// heat: two octaves of noise scrolling upward, strongest in the middle
		float t = u_FxParams[0].w;
		vec2 q = var_Local * 2.0 + vec2(var_Source.w * 13.7, -t * 1.6 + var_Source.w * 7.1);
		vec2 n = vec2(ValueNoise(q * 2.5), ValueNoise(q * 2.5 + 17.3)) * 2.0 - 1.0;
		n += (vec2(ValueNoise(q * 6.0 + 3.1), ValueNoise(q * 6.0 + 41.7)) * 2.0 - 1.0) * 0.5;
		float mask = (1.0 - r * r);
		mask *= mask;
		offset = n * mask;
	}
	else
	{
		// shock: a thin ring travelling out with the age, fading as it goes
		const float width = 0.16;
		float x = (r - age) / width;
		float ring = exp(-x * x) * (1.0 - age);
		offset = (r > 1.0e-3 ? var_Local / r : vec2(0.0)) * ring;
	}

	// projected size of the source: distant haze moves the image less
	float projected = clamp(radius / max(viewDepth, 1.0), 0.0, 1.0);
	offset *= var_Source.x * u_FxParams[3].x * projected * soft;
	out_Color = vec4(clamp(offset, vec2(-u_FxParams[3].y), vec2(u_FxParams[3].y)), 0.0, 0.0);
}
#endif
