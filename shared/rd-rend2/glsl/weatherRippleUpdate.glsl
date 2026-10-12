/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
uniform sampler2D u_TextureMap; // previous RG: height, velocity
uniform sampler2D u_NormalMap;  // this frame's additive velocity impulses
uniform sampler2D u_WeatherDepthMap; // static top-down surface depth
uniform vec4 u_PuddleRippleField; // dt, damping / s, propagation (texels / s), max height
uniform vec4 u_SplashParams; // depth range, max propagation height step, field coverage scale, unused

out vec2 out_Color;

void main()
{
	ivec2 size = textureSize(u_TextureMap, 0);
	ivec2 p = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - 1);
	ivec2 lo = ivec2(0), hi = size - 1;
	vec2 state = texelFetch(u_TextureMap, p, 0).rg;
	vec2 uv = (vec2(p) + 0.5) / vec2(size);
	vec2 weatherUV = (uv - 0.5) * u_SplashParams.z + 0.5;
	float centerDepth = texture(u_WeatherDepthMap, weatherUV).r;
	float laplacian = 0.0;
	const ivec2 offsets[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0),
		ivec2(0, 1), ivec2(0, -1));
	for (int i = 0; i < 4; ++i)
	{
		ivec2 neighbour = clamp(p + offsets[i], lo, hi);
		vec2 neighbourUV = (vec2(neighbour) + 0.5) / vec2(size);
		neighbourUV = (neighbourUV - 0.5) * u_SplashParams.z + 0.5;
		float depth = texture(u_WeatherDepthMap, neighbourUV).r;
		float connected = step(abs(depth - centerDepth) * u_SplashParams.x,
			u_SplashParams.y);
		laplacian += (texelFetch(u_TextureMap, neighbour, 0).r - state.r) * connected;
	}
	float dt = u_PuddleRippleField.x;
	float velocity = state.g + laplacian * u_PuddleRippleField.z * u_PuddleRippleField.z * dt;
	velocity += texelFetch(u_NormalMap, p, 0).r;
	velocity *= exp(-u_PuddleRippleField.y * dt);
	float height = clamp(state.r + velocity * dt,
		-u_PuddleRippleField.w, u_PuddleRippleField.w);
	out_Color = vec2(height, velocity);
}
