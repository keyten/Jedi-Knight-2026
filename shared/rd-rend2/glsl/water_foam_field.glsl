/*[Vertex]*/
void main()
{
	vec2 p = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(p, 0.0, 1.0);
}

/*[Fragment]*/
// GL 3.2 baseline persistent foam/whitewater update. R is persistent surface
// foam, G is the rasterised body mask, B is advected bulk aeration/turbulence,
// and A is the current turbulence source for diagnostics. Sources are waterfall, intersection/obstacle,
// impact-pool/wake, and shoreline/steepness in RGBA respectively.
uniform sampler2D u_WaterFoamMap;
uniform sampler2D u_WaterFoamSourceMap;
uniform vec4 u_WaterFoamParams; // dt, decay/s, diffusion, advection
uniform vec4 u_WaterFoamFlow;   // body-UV velocity/s, time, debug
uniform vec4 u_WaterWhitewaterParams; // source threshold, decay/s, foam injection, static profile source
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
	float turbulence = texture(u_WaterFoamMap, previousUV).b;
	float turbulenceNeighbours = 0.25 * (
		texture(u_WaterFoamMap, previousUV - vec2(texel.x, 0.0)).b +
		texture(u_WaterFoamMap, previousUV + vec2(texel.x, 0.0)).b +
		texture(u_WaterFoamMap, previousUV - vec2(0.0, texel.y)).b +
		texture(u_WaterFoamMap, previousUV + vec2(0.0, texel.y)).b);
	turbulence = mix(turbulence, turbulenceNeighbours, min(diffusion * 1.6, 0.24));
	turbulence *= exp(-u_WaterWhitewaterParams.y * dt);

	// Analytic breakup avoids an asset and keeps injection from forming solid
	// rectangles. It is stable in body UV and changes slowly enough to advect.
	float breakup = mix(0.72, 1.0,
		Hash(floor(uv * vec2(size) * 0.5) + floor(u_WaterFoamFlow.z * 2.0)));
	float injected = dot(source, vec4(1.0)) * breakup;
	// Static profile flow is deliberately conservative and patchy. Local
	// waterfall, obstacle and impact sources override it; the mask look-ahead
	// adds aeration where resolved flow runs into a body boundary.
	vec2 flowDir = length(u_WaterFoamFlow.xy) > 1e-6 ? normalize(u_WaterFoamFlow.xy) : vec2(0.0);
	float aheadMask = texture(u_WaterFoamMap, clamp(uv + flowDir * texel * 1.5,
		0.5 * texel, vec2(1.0) - 0.5 * texel)).g;
	float obstacle = length(flowDir) > 0.0 ? 1.0 - aheadMask : 0.0;
	float staticSource = u_WaterWhitewaterParams.w * mix(0.62, 1.0, breakup);
	float rawSource = max(staticSource, max(source.r, max(source.g * 0.9, source.b * 0.75)));
	rawSource = max(rawSource, obstacle * u_WaterWhitewaterParams.w * 1.35);
	float sourceWidth = max(0.08, min(0.3, 1.0 - u_WaterWhitewaterParams.x));
	float whitewaterSource = smoothstep(u_WaterWhitewaterParams.x,
		min(u_WaterWhitewaterParams.x + sourceWidth, 1.0), rawSource);
	concentration = clamp(concentration + (injected + whitewaterSource *
		u_WaterWhitewaterParams.z) * dt, 0.0, 1.0);
	turbulence = clamp(turbulence + whitewaterSource * dt, 0.0, 1.0);
	out_Color = vec4(concentration, mask, turbulence, whitewaterSource);
}
