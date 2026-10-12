/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Low-frequency world wetness state. R is physical-ish water amount, G is a
// copy of the weather depth used by lightall for the ordinary roof test.
uniform sampler2D u_TextureMap;
uniform sampler2D u_WeatherDepthMap;
uniform vec4 u_WetnessUpdate; // dt, deposition / s, dry / s, extra steep drying
out vec4 out_Color;

void main()
{
	ivec2 size = textureSize(u_TextureMap, 0);
	ivec2 p = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
	float oldWater = texelFetch(u_TextureMap, p, 0).r;
	vec2 uv = (vec2(p) + 0.5) / vec2(size);
	vec2 duv = 1.0 / vec2(size);
	float depth = texture(u_WeatherDepthMap, uv).r;

	// Depth gradient is a cheap projection-space proxy for drainage: flat
	// roofs/courtyards retain water, steep first-hit surfaces dry faster.
	float dx = abs(texture(u_WeatherDepthMap, uv + vec2(duv.x, 0.0)).r -
		texture(u_WeatherDepthMap, uv - vec2(duv.x, 0.0)).r);
	float dy = abs(texture(u_WeatherDepthMap, uv + vec2(0.0, duv.y)).r -
		texture(u_WeatherDepthMap, uv - vec2(0.0, duv.y)).r);
	float steep = smoothstep(0.001, 0.02, max(dx, dy));
	float drying = u_WetnessUpdate.z * mix(1.0, max(u_WetnessUpdate.w, 1.0), steep);
	float water = clamp(oldWater + (u_WetnessUpdate.y - drying) * u_WetnessUpdate.x, 0.0, 1.0);
	out_Color = vec4(water, depth, 0.0, 1.0);
}
