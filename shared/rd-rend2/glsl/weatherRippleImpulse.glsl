/*[Vertex]*/
// Raster injection of fresh transform-feedback rain impacts.  The impact is
// already in world space, so this pass uses exactly the weather projection
// used by DetectImpact and by lightall.
uniform mat4 u_WeatherMvp;
uniform sampler2D u_WeatherImpactMap;
uniform vec4 u_PuddleRippleField; // diameter, impulse, coverage, 0 puddle / 1+density real water

in vec4 attr_TexCoord0; // impact.xyz, state (2 = fresh event)

out float var_Strength;

uint HashUint(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}

void main()
{
	vec4 impact = attr_TexCoord0;
	vec2 uv = (u_WeatherMvp * vec4(impact.xyz, 1.0)).xy * 0.5 + 0.5;
	uv = (uv - 0.5) / max(u_PuddleRippleField.z, 0.125) + 0.5;

	// The top-down material attachment removes entities/cloth/dry authored
	// surfaces before an impulse exists.  lightall's final submerged mask is
	// still authoritative at consumption time.
	ivec2 classSize = textureSize(u_WeatherImpactMap, 0);
	ivec2 classTexel = clamp(ivec2(((uv - 0.5) * u_PuddleRippleField.z + 0.5) *
		vec2(classSize)), ivec2(0), classSize - ivec2(1));
	int surfaceClass = int(floor(texelFetch(u_WeatherImpactMap, classTexel, 0).r * 6.0 + 0.5));
	bool waterPass = u_PuddleRippleField.w >= 1.0;
	bool acceptsSurface = waterPass ? surfaceClass == 3 : (surfaceClass == 1 || surfaceClass == 2);
	bool fresh = (impact.w > 1.5 && impact.w < 2.5) ||
		(impact.w < -2.5 && impact.w > -3.5);
	float density = clamp(u_PuddleRippleField.w - 1.0, 0.0, 1.0);
	uint eventHash = HashUint(uint(gl_VertexID) ^ uint(floor(impact.x * 8.0)) ^
		(uint(floor(impact.y * 8.0)) * 2891336453u));
	bool selected = !waterPass || float(eventHash & 65535u) < density * 65535.0;
	bool valid = fresh && acceptsSurface &&
		all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));

	gl_Position = valid ? vec4(uv * 2.0 - 1.0, 0.0, 1.0) : vec4(2.0, 2.0, 0.0, 1.0);
	gl_PointSize = u_PuddleRippleField.x;
	// Real water retains every hit as low-amplitude aggregate energy while a
	// stable subset receives the explicit solver impulse.
	var_Strength = valid ? u_PuddleRippleField.y *
		(waterPass && !selected ? 0.025 : 1.0) : 0.0;
}

/*[Fragment]*/
in float var_Strength;
out float out_Color;

void main()
{
	vec2 p = gl_PointCoord * 2.0 - 1.0;
	float r2 = dot(p, p);
	if (r2 >= 1.0)
		discard;
	// Smooth compact velocity impulse. Additive blending accumulates heavy rain.
	out_Color = var_Strength * (1.0 - r2) * (1.0 - r2);
}
