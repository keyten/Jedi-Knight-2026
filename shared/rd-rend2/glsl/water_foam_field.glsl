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
// and A is the current turbulence source for diagnostics. Localized sources
// are waterfall, intersection/obstacle and continuous impact (rates) and a
// one-shot splash deposit, in RGBA respectively.  Breaking-wave, crest and
// wall-impact sources are evaluated here from the interaction render atlas and
// the flow field; nothing feeds foam merely for being a boundary cell.
uniform sampler2D u_WaterFoamMap;
uniform sampler2D u_WaterFoamSourceMap;
uniform sampler2D u_WaterFoamInteraction; // R height, GB world slope, A 0 dry / 1 + foam wet
uniform sampler2D u_WaterFlowField;       // velocity / body speed (rg), shore distance, impingement
uniform vec4 u_WaterFoamParams; // dt, decay/s, diffusion, advection
uniform vec4 u_WaterFoamFlow;   // body-UV velocity/s, time, debug
uniform vec4 u_WaterWhitewaterParams; // source threshold, decay/s, foam injection, static profile source
uniform vec4 u_WaterFoamSources; // waterfall rate, shore breaking scale, crest steepness scale, wall impact scale
uniform vec4 u_WaterFoamInteractionRect; // body UV -> atlas offset (xy) and scale (zw); zero scale = none
uniform vec4 u_WaterFlowFieldParams; // field enabled, body flow speed (world units/s), 1 / body extent xy
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
	// body-UV velocity: the uniform body flow, or the spatial field (slow at
	// banks, turning along walls) when r_waterFlowField built one
	vec2 velocity = u_WaterFoamFlow.xy;
	vec4 field = vec4(0.0);
	if (u_WaterFlowFieldParams.x > 0.5)
	{
		field = texture(u_WaterFlowField, uv);
		velocity = field.rg * u_WaterFlowFieldParams.y * u_WaterFlowFieldParams.zw;
	}
	// A localized waterfall source has no useful authored body flow on many
	// pools.  The negative gradient of its R-channel footprint is a cheap radial
	// outflow approximation; it vanishes smoothly outside the impact region.
	float sourceLeft = texture(u_WaterFoamSourceMap, uv - vec2(texel.x, 0.0)).r;
	float sourceRight = texture(u_WaterFoamSourceMap, uv + vec2(texel.x, 0.0)).r;
	float sourceDown = texture(u_WaterFoamSourceMap, uv - vec2(0.0, texel.y)).r;
	float sourceUp = texture(u_WaterFoamSourceMap, uv + vec2(0.0, texel.y)).r;
	vec2 sourceGradient = vec2(sourceRight - sourceLeft, sourceUp - sourceDown);
	vec2 radialFlow = length(sourceGradient) > 1e-5 ?
		-normalize(sourceGradient) * texel * 6.0 * source.r : vec2(0.0);
	vec2 previousUV = clamp(uv - (velocity * u_WaterFoamParams.w + radialFlow) * dt,
		0.5 * texel, vec2(1.0) - 0.5 * texel);
	float center = texture(u_WaterFoamMap, previousUV).r;
	float left = texture(u_WaterFoamMap, previousUV - vec2(texel.x, 0.0)).r;
	float right = texture(u_WaterFoamMap, previousUV + vec2(texel.x, 0.0)).r;
	float down = texture(u_WaterFoamMap, previousUV - vec2(0.0, texel.y)).r;
	float up = texture(u_WaterFoamMap, previousUV + vec2(0.0, texel.y)).r;
	float diffusion = clamp(u_WaterFoamParams.z * dt, 0.0, 0.24);
	float concentration = mix(center, 0.25 * (left + right + down + up), diffusion);
	// Dense foam collapses quickly (bubbles burst), thin residual lace lingers:
	// dc/dt = -(k0 + k1 c) c instead of a single exponential.
	concentration *= exp(-u_WaterFoamParams.y * (1.0 + 1.6 * concentration) * dt);
	float turbulence = texture(u_WaterFoamMap, previousUV).b;
	float turbulenceNeighbours = 0.25 * (
		texture(u_WaterFoamMap, previousUV - vec2(texel.x, 0.0)).b +
		texture(u_WaterFoamMap, previousUV + vec2(texel.x, 0.0)).b +
		texture(u_WaterFoamMap, previousUV - vec2(0.0, texel.y)).b +
		texture(u_WaterFoamMap, previousUV + vec2(0.0, texel.y)).b);
	turbulence = mix(turbulence, turbulenceNeighbours, min(diffusion * 1.6, 0.24));
	turbulence *= exp(-u_WaterWhitewaterParams.y * dt);

	// Event-driven sources (rates per second).
	float breaking = 0.0;
	if (u_WaterFoamInteractionRect.z > 0.0)
	{
		vec4 disturbance = texture(u_WaterFoamInteraction, u_WaterFoamInteractionRect.xy + uv * u_WaterFoamInteractionRect.zw);
		float slope = disturbance.a > 0.5 ? length(disturbance.gb) : 0.0;
		// waves breaking on a bank: steep disturbance next to the body edge
		float edge = 1.0 - min(min(texture(u_WaterFoamMap, uv + vec2(1.5 * texel.x, 0.0)).g,
			texture(u_WaterFoamMap, uv - vec2(1.5 * texel.x, 0.0)).g),
			min(texture(u_WaterFoamMap, uv + vec2(0.0, 1.5 * texel.y)).g,
			texture(u_WaterFoamMap, uv - vec2(0.0, 1.5 * texel.y)).g));
		breaking += u_WaterFoamSources.y * edge * smoothstep(0.08, 0.35, slope) * 3.0;
		// crests steeper than a wave can hold spill anywhere
		breaking += u_WaterFoamSources.z * smoothstep(0.35, 0.75, slope) * 3.0;
	}
	// flow running into a wall (r_waterFlowImpact)
	breaking += u_WaterFoamSources.w * field.a * field.a * 6.0;

	float rate = u_WaterFoamSources.x + source.r + source.g + source.b * 0.75 + breaking;
	// Analytic breakup keeps steady sources from painting solid rectangles.
	// Stable in body UV, it changes slowly enough to advect.
	float breakup = mix(0.72, 1.0,
		Hash(floor(uv * vec2(size) * 0.5) + floor(u_WaterFoamFlow.z * 2.0)));
	// Static profile flow is deliberately conservative and patchy. Local
	// waterfall, obstacle and impact sources override it; the mask look-ahead
	// adds aeration where resolved flow runs into a body boundary.
	vec2 flowDir = length(velocity) > 1e-6 ? normalize(velocity) : vec2(0.0);
	float aheadMask = texture(u_WaterFoamMap, clamp(uv + flowDir * texel * 1.5,
		0.5 * texel, vec2(1.0) - 0.5 * texel)).g;
	float obstacle = length(flowDir) > 0.0 ? 1.0 - aheadMask : 0.0;
	if (u_WaterFlowFieldParams.x > 0.5)
		obstacle = max(obstacle, field.a);
	float staticSource = u_WaterWhitewaterParams.w * mix(0.62, 1.0, breakup);
	float rawSource = max(staticSource, max(source.r, max(source.g * 0.9, source.b * 0.75)));
	rawSource = max(rawSource, obstacle * u_WaterWhitewaterParams.w * 1.35);
	float sourceWidth = max(0.08, min(0.3, 1.0 - u_WaterWhitewaterParams.x));
	float whitewaterSource = smoothstep(u_WaterWhitewaterParams.x,
		min(u_WaterWhitewaterParams.x + sourceWidth, 1.0), rawSource);
	rate += whitewaterSource * u_WaterWhitewaterParams.z;
	// Saturating accumulation: a steady source reaches an equilibrium below
	// one instead of clamping into a flat white sheet.
	concentration += rate * breakup * dt * (1.0 - concentration);
	// one-shot splash deposit (already a quantity, not a rate)
	concentration += source.a * (1.0 - concentration);
	concentration = clamp(concentration, 0.0, 1.0);
	turbulence = clamp(turbulence + whitewaterSource * dt, 0.0, 1.0);
	out_Color = vec4(concentration, mask, turbulence, whitewaterSource);
}
