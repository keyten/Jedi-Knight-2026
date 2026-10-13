/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Low-frequency world wetness state. R/G are physical-ish retained water
// amounts for flat/steep surfaces. B copies the weather depth used by lightall
// for the ordinary roof test. A retains recent world runoff activity. The
// shaded surface's real geometric normal selects between R/G, avoiding a
// weather-depth-gradient slope proxy.
uniform sampler2D u_TextureMap;
uniform sampler2D u_WeatherDepthMap;
uniform vec4 u_WetnessUpdate; // dt, deposition / s, dry / s, extra steep drying
uniform vec4 u_RunoffTrailUpdate; // lifetime seconds, current rain, enabled, unused
out vec4 out_Color;

void main()
{
	ivec2 size = textureSize(u_TextureMap, 0);
	ivec2 p = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
	vec4 oldState = texelFetch(u_TextureMap, p, 0);
	vec2 oldWater = oldState.rg;
	vec2 uv = (vec2(p) + 0.5) / vec2(size);
	float depth = texture(u_WeatherDepthMap, uv).r;

	vec2 drying = u_WetnessUpdate.z * vec2(1.0, max(u_WetnessUpdate.w, 1.0));
	vec2 water = clamp(oldWater + (u_WetnessUpdate.y - drying) * u_WetnessUpdate.x,
		vec2(0.0), vec2(1.0));
	float trail = 0.0;
	if (u_RunoffTrailUpdate.z > 0.5 && u_RunoffTrailUpdate.x > 0.0)
	{
		// A passing stream deposits immediately; the retained envelope then
		// dries linearly. Geometry and stream placement remain in lightall.
		trail = max(oldState.a - u_WetnessUpdate.x / u_RunoffTrailUpdate.x,
			u_RunoffTrailUpdate.y);
	}
	out_Color = vec4(water, depth, clamp(trail, 0.0, 1.0));
}
