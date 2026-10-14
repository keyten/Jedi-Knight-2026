/*[Vertex]*/
void main()
{
	vec2 p = vec2(2.0 * float(gl_VertexID & 2) - 1.0,
		4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(p, 0.0, 1.0);
}

/*[Fragment]*/
// GL 3.2 shallow-water ping-pong update and render-atlas resolve.
// State is height, velocity, transient foam, intersection foam. Its alpha is
// negative for dry cells, which lets all four physical channels use one image.
uniform sampler2D u_WaterInteractionState;
uniform sampler2D u_WaterInteractionSource;
uniform sampler2D u_WaterBathymetryMap;
uniform vec4 u_WaterInteractionSim0; // dt, c, damping, resolve mode
uniform vec4 u_WaterInteractionSim1; // texel world X/Y, flow world X/Y
uniform vec4 u_WaterInteractionDomain; // viewport origin, intersection persistence, apply source
uniform vec4 u_WaterBathymetryParams; // enabled, fallback depth, effective gravity, unused
uniform sampler2D u_WaterFlowField;     // r_waterFlowField: velocity / body speed (rg), same body UV
uniform vec4 u_WaterFlowFieldParams;    // field enabled, body flow speed (world units/s), unused, unused
out vec4 out_Color;

vec4 State(ivec2 p, ivec2 centerPosition, ivec2 size, vec4 centerValue)
{
	p = clamp(p, ivec2(0), size - 1);
	vec4 value = texelFetch(u_WaterInteractionState, p, 0);
	if (value.a < 0.0)
		return centerValue;
	if (u_WaterInteractionDomain.w > 0.5)
	{
		vec4 source = texelFetch(u_WaterInteractionSource, p, 0);
		value.rgb += source.rgb;
		value.a = max(value.a, source.a);
	}
	return value;
}

// Semi-Lagrangian advection by the resolved flow.  The departure point is
// interpolated with Catmull-Rom over 4x4 wet cells and clamped to the inner
// 2x2 range (no new extrema).  First-order upwind differencing smeared a
// ripple of a few texels within ~0.15 s at stream speeds (numerical
// diffusion |u| dx / 2); this keeps disturbances intact while they drift.
// Returns false when the footprint touches a dry/outside cell, so the caller
// keeps the bounded upwind result next to banks.
vec4 FlowState(ivec2 p, ivec2 size)
{
	if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y)
		return vec4(0.0, 0.0, 0.0, -1.0);
	vec4 value = texelFetch(u_WaterInteractionState, p, 0);
	if (value.a >= 0.0 && u_WaterInteractionDomain.w > 0.5)
	{
		vec4 source = texelFetch(u_WaterInteractionSource, p, 0);
		value.rgb += source.rgb;
		value.a = max(value.a, source.a);
	}
	return value;
}

vec4 CatmullRomWeights(float t)
{
	return vec4(t * (-0.5 + t * (1.0 - 0.5 * t)),
		1.0 + t * t * (-2.5 + 1.5 * t),
		t * (0.5 + t * (2.0 - 1.5 * t)),
		t * t * (-0.5 + 0.5 * t));
}

bool AdvectState(vec2 departure, ivec2 size, out vec4 result)
{
	vec2 base = floor(departure);
	vec2 t = departure - base;
	ivec2 b = ivec2(base);
	vec4 wx = CatmullRomWeights(t.x), wy = CatmullRomWeights(t.y);
	vec4 sum = vec4(0.0);
	vec4 lo = vec4(1.0e30), hi = vec4(-1.0e30);
	for (int j = 0; j < 4; ++j)
	{
		vec4 row = vec4(0.0);
		for (int i = 0; i < 4; ++i)
		{
			vec4 value = FlowState(b + ivec2(i - 1, j - 1), size);
			if (value.a < 0.0)
				return false;
			row += wx[i] * value;
			if (i == 1 || i == 2) if (j == 1 || j == 2) { lo = min(lo, value); hi = max(hi, value); }
		}
		sum += wy[j] * row;
	}
	result = clamp(sum, lo, hi);
	return true;
}

float WaterDepth(ivec2 p, ivec2 size)
{
	p = clamp(p, ivec2(0), size - 1);
	vec2 sampleValue = texelFetch(u_WaterBathymetryMap, p, 0).rg;
	return sampleValue.y >= 0.75 ? max(sampleValue.x, 0.5) : u_WaterBathymetryParams.y;
}

void main()
{
	ivec2 size = textureSize(u_WaterInteractionState, 0);
	ivec2 p = ivec2(gl_FragCoord.xy - u_WaterInteractionDomain.xy);
	p = clamp(p, ivec2(0), size - 1);
	vec4 center = texelFetch(u_WaterInteractionState, p, 0);
	if (center.a < 0.0)
	{
		out_Color = u_WaterInteractionSim0.w > 0.5 ? vec4(0.0) : center;
		return;
	}
	vec4 left = State(p + ivec2(-1, 0), p, size, center);
	vec4 right = State(p + ivec2(1, 0), p, size, center);
	vec4 down = State(p + ivec2(0, -1), p, size, center);
	vec4 up = State(p + ivec2(0, 1), p, size, center);

	if (u_WaterInteractionSim0.w > 0.5)
	{
		float sx = (right.r - left.r) / (2.0 * u_WaterInteractionSim1.x);
		float sy = (up.r - down.r) / (2.0 * u_WaterInteractionSim1.y);
		out_Color = vec4(center.r, sx, sy, 1.0 + clamp(center.b + center.a, 0.0, 1.0));
		return;
	}

	vec4 source = u_WaterInteractionDomain.w > 0.5 ?
		texelFetch(u_WaterInteractionSource, p, 0) : vec4(0.0);
	center.rgb += source.rgb;
	center.a = max(center.a, source.a);
	float dt = u_WaterInteractionSim0.x;
	float c = u_WaterInteractionSim0.y;
	float damping = u_WaterInteractionSim0.z;
	vec2 worldTexel = u_WaterInteractionSim1.xy;
	vec2 flow = u_WaterInteractionSim1.zw;
	if (u_WaterFlowFieldParams.x > 0.5)
	{
		// spatial flow: slow at banks, turning along walls; per-cell CFL guard
		flow = texture(u_WaterFlowField, (vec2(p) + 0.5) / vec2(size)).rg * u_WaterFlowFieldParams.y;
		float courant = dt * (abs(flow.x) / worldTexel.x + abs(flow.y) / worldTexel.y);
		if (courant > 0.95)
			flow *= 0.95 / courant;
	}
	float acceleration;
	if (u_WaterBathymetryParams.x > 0.5)
	{
		float centerDepth = WaterDepth(p, size);
		float depthLeft = 0.5 * (centerDepth + WaterDepth(p + ivec2(-1, 0), size));
		float depthRight = 0.5 * (centerDepth + WaterDepth(p + ivec2(1, 0), size));
		float depthDown = 0.5 * (centerDepth + WaterDepth(p + ivec2(0, -1), size));
		float depthUp = 0.5 * (centerDepth + WaterDepth(p + ivec2(0, 1), size));
		acceleration = u_WaterBathymetryParams.z *
			((depthRight * (right.r - center.r) - depthLeft * (center.r - left.r)) /
				(worldTexel.x * worldTexel.x) +
			 (depthUp * (up.r - center.r) - depthDown * (center.r - down.r)) /
				(worldTexel.y * worldTexel.y));
	}
	else
	{
		float lap = (left.r - 2.0 * center.r + right.r) / (worldTexel.x * worldTexel.x) +
			(down.r - 2.0 * center.r + up.r) / (worldTexel.y * worldTexel.y);
		acceleration = c * c * lap;
	}
	// Transport by the flow first (semi-Lagrangian, upwind next to banks),
	// then integrate the local wave equation on the transported cell.
	vec4 transported;
	bool flowing = abs(flow.x) + abs(flow.y) > 1.0e-4;
	if (!flowing || !AdvectState(vec2(p) - dt * flow / worldTexel, size, transported))
	{
		transported = center;
		if (flowing)
		{
			vec4 gradX = flow.x >= 0.0 ? (center - left) / worldTexel.x : (right - center) / worldTexel.x;
			vec4 gradY = flow.y >= 0.0 ? (center - down) / worldTexel.y : (up - center) / worldTexel.y;
			transported = center - dt * (flow.x * gradX + flow.y * gradY);
		}
	}
	float velocity = transported.g + dt * (acceleration - 2.0 * damping * transported.g);
	float height = transported.r + dt * velocity;

	float foamDiffusion = 0.3 * ((left.b - 2.0 * center.b + right.b) + (down.b - 2.0 * center.b + up.b));
	float foam = (transported.b + dt * foamDiffusion) * exp(-dt * 0.85);
	bool boundary = p.x == 0 || p.y == 0 || p.x + 1 == size.x || p.y + 1 == size.y ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(-1, 0), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(1, 0), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(0, -1), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(0, 1), ivec2(0), size - 1), 0).a < 0.0;
	if (boundary && u_WaterInteractionSim0.w < 0.0)
	{
		// Only a disturbance that actually breaks on the edge foams it.  Steady
		// flow along a bank is not agitation (it used to foam every edge cell
		// as soon as a stream exceeded ~15 u/s).  Foam accumulates as a
		// saturating rate instead of being forced to a constant.
		float agitation = clamp((abs(velocity) - 40.0) / 120.0, 0.0, 1.0);
		foam += -u_WaterInteractionSim0.w * agitation * 4.0 * dt * (1.0 - foam);
	}

	float intersectionDiffusion = 0.3 * ((left.a - 2.0 * center.a + right.a) + (down.a - 2.0 * center.a + up.a));
	float intersection = (transported.a + dt * intersectionDiffusion) *
		exp(-dt / max(u_WaterInteractionDomain.z, 0.001));
	out_Color = vec4(clamp(height, -64.0, 64.0), clamp(velocity, -512.0, 512.0),
		clamp(foam, 0.0, 1.0), clamp(intersection, 0.0, 1.0));
}
