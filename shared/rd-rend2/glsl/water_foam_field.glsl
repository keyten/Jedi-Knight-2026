/*[Vertex]*/
void main()
{
	vec2 p = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(p, 0.0, 1.0);
}

/*[Fragment]*/
// GL 3.2 baseline persistent foam update. R is concentration and G is the
// rasterised body mask. Sources are waterfall, intersection/obstacle,
// impact-pool/wake, and shoreline/steepness in RGBA respectively.
uniform sampler2D u_WaterFoamMap;
uniform sampler2D u_WaterFoamSourceMap;
uniform vec4 u_WaterFoamParams; // dt, decay/s, diffusion, advection
uniform vec4 u_WaterFoamFlow;   // body-UV velocity/s, time, debug
out vec4 out_Color;

float Hash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
	ivec2 size = textureSize(u_WaterFoamMap, 0);
	vec2 texel = 1.0 / vec2(size);
	vec2 uv = gl_FragCoord.xy * texel;
	vec4 source = texture(u_WaterFoamSourceMap, uv);
	float mask = texture(u_WaterFoamMap, uv).g;
	if (mask < 0.5)
	{
		out_Color = vec4(0.0);
		return;
	}

	float dt = u_WaterFoamParams.x;
	vec2 previousUV = clamp(uv - u_WaterFoamFlow.xy * dt * u_WaterFoamParams.w,
		0.5 * texel, vec2(1.0) - 0.5 * texel);
	float center = texture(u_WaterFoamMap, previousUV).r;
	float left = texture(u_WaterFoamMap, previousUV - vec2(texel.x, 0.0)).r;
	float right = texture(u_WaterFoamMap, previousUV + vec2(texel.x, 0.0)).r;
	float down = texture(u_WaterFoamMap, previousUV - vec2(0.0, texel.y)).r;
	float up = texture(u_WaterFoamMap, previousUV + vec2(0.0, texel.y)).r;
	float diffusion = clamp(u_WaterFoamParams.z * dt, 0.0, 0.24);
	float concentration = mix(center, 0.25 * (left + right + down + up), diffusion);
	concentration *= exp(-u_WaterFoamParams.y * dt);

	// Analytic breakup avoids an asset and keeps injection from forming solid
	// rectangles. It is stable in body UV and changes slowly enough to advect.
	float breakup = mix(0.72, 1.0,
		Hash(floor(uv * vec2(size) * 0.5) + floor(u_WaterFoamFlow.z * 2.0)));
	float injected = dot(source, vec4(1.0)) * breakup;
	concentration = clamp(concentration + injected * dt, 0.0, 1.0);
	out_Color = vec4(concentration, mask, 0.0, 1.0);
}
