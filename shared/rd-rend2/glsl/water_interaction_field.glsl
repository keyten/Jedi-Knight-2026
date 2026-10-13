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
uniform vec4 u_WaterInteractionSim0; // dt, c, damping, resolve mode
uniform vec4 u_WaterInteractionSim1; // texel world X/Y, flow world X/Y
uniform vec4 u_WaterInteractionDomain; // viewport origin, intersection persistence, apply source
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
	float lap = (left.r - 2.0 * center.r + right.r) / (worldTexel.x * worldTexel.x) +
		(down.r - 2.0 * center.r + up.r) / (worldTexel.y * worldTexel.y);
	float velocity = center.g + dt * (c * c * lap - 2.0 * damping * center.g);
	float dhdx = flow.x >= 0.0 ? (center.r - left.r) / worldTexel.x : (right.r - center.r) / worldTexel.x;
	float dhdy = flow.y >= 0.0 ? (center.r - down.r) / worldTexel.y : (up.r - center.r) / worldTexel.y;
	float height = center.r + dt * (velocity - flow.x * dhdx - flow.y * dhdy);

	float dfdx = flow.x >= 0.0 ? (center.b - left.b) / worldTexel.x : (right.b - center.b) / worldTexel.x;
	float dfdy = flow.y >= 0.0 ? (center.b - down.b) / worldTexel.y : (up.b - center.b) / worldTexel.y;
	float foamDiffusion = 0.3 * ((left.b - 2.0 * center.b + right.b) + (down.b - 2.0 * center.b + up.b));
	float foam = (center.b - dt * dot(flow, vec2(dfdx, dfdy)) + dt * foamDiffusion) * exp(-dt * 0.85);
	bool boundary = p.x == 0 || p.y == 0 || p.x + 1 == size.x || p.y + 1 == size.y ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(-1, 0), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(1, 0), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(0, -1), ivec2(0), size - 1), 0).a < 0.0 ||
		texelFetch(u_WaterInteractionState, clamp(p + ivec2(0, 1), ivec2(0), size - 1), 0).a < 0.0;
	if (boundary && u_WaterInteractionSim0.w < 0.0)
	{
		float agitation = max(abs(velocity) / 96.0, length(flow) / 96.0);
		foam = max(foam, -u_WaterInteractionSim0.w * clamp(agitation - 0.15, 0.0, 1.0));
	}

	float didx = flow.x >= 0.0 ? (center.a - left.a) / worldTexel.x : (right.a - center.a) / worldTexel.x;
	float didy = flow.y >= 0.0 ? (center.a - down.a) / worldTexel.y : (up.a - center.a) / worldTexel.y;
	float intersectionDiffusion = 0.3 * ((left.a - 2.0 * center.a + right.a) + (down.a - 2.0 * center.a + up.a));
	float intersection = (center.a - dt * dot(flow, vec2(didx, didy)) + dt * intersectionDiffusion) *
		exp(-dt / max(u_WaterInteractionDomain.z, 0.001));
	out_Color = vec4(clamp(height, -64.0, 64.0), clamp(velocity, -512.0, 512.0),
		clamp(foam, 0.0, 1.0), clamp(intersection, 0.0, 1.0));
}
