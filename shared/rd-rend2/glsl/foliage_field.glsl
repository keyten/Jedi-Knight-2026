/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Grass bend field (r_foliageInteraction, tr_foliagefield.cpp). The
// FoliageInteraction block and the contact functions come from
// foliage_interact.glsl (pasted in front as a library).
//
// Update pass (default): one texel = one world XY patch of grass around the
// player. State A: rg = displacement delta of the grass at the probe height
// (world units), ba = its velocity; state B: rg = trampled displacement. Each
// frame the previous state is read at the texel shift of the scrolled origin
// (freshly exposed texels start at rest). The grass springs (damped) towards
// its trampled pose; a character is then a hard one-sided contact: delta is
// pushed out of the capsule, along the direction away from its axis, nothing
// ahead of the body moves. While a character stands on it, the patch slowly
// takes a trampled set it keeps for a while after (r_grassTrample). A Force
// push / pull front passing the patch kicks its velocity away from / towards
// the caster (FoliageForceWaveKick); the spring brings it back. No blur: the
// state never spreads to other texels.
//
// DEBUG_VIEW: r_foliageBendFieldDebug 1, the current field in a corner of the
// screen: left square = displacement (red / green = +x / +y, gray = rest),
// right square = magnitude heat; the player is the center.

out vec4 out_Color;

#if defined(DEBUG_VIEW)

uniform vec4 u_FoliageFieldDebug;	// x, y of the lower left corner, square size (pixels), displacement of full heat

vec3 Heat(in float x)
{
	x = clamp(x, 0.0, 1.0);
	return clamp(vec3(1.5 - abs(4.0 * x - 3.0), 1.5 - abs(4.0 * x - 2.0), 1.5 - abs(4.0 * x - 1.0)), 0.0, 1.0);
}

void main()
{
	vec2 local = (gl_FragCoord.xy - u_FoliageFieldDebug.xy) / u_FoliageFieldDebug.z;
	if (local.x < 0.0 || local.y < 0.0 || local.x >= 2.0 || local.y >= 1.0)
		discard;

	bool magnitudeView = local.x >= 1.0;
	vec2 uv = vec2(fract(local.x), local.y);
	ivec2 size = textureSize(u_FoliageFieldMap, 0);
	vec4 state = texelFetch(u_FoliageFieldMap, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0);
	vec2 bend = state.xy / u_FoliageFieldDebug.w;

	vec3 color = magnitudeView ? Heat(length(bend)) * step(1e-3, length(bend))
		: vec3(clamp(0.5 + 0.5 * bend, 0.0, 1.0), 0.5);

	// border and player cross
	vec2 border = min(uv, 1.0 - uv) * u_FoliageFieldDebug.z;
	vec2 center = abs(uv - 0.5) * u_FoliageFieldDebug.z;
	if (min(border.x, border.y) < 1.5 || (min(center.x, center.y) < 0.75 && max(center.x, center.y) < 6.0))
		color = vec3(1.0);

	out_Color = vec4(color, 1.0);
}

#else

// the update pass also writes the trampled state (second attachment)
out vec4 out_Glow;
#define out_Trample out_Glow
// the update pass reads the previous trampled state on the second unit
#define u_FoliageTrampleMap u_FoliageFieldPrevMap

// state limits (world units, units / s)
const float FIELD_MAX_DISPLACEMENT = 48.0;
const float FIELD_MAX_SPEED = 600.0;
// kick of a walk through at full contact and full speed (units / s^2 at
// r_foliageBendFieldImpulse 1): the legs drag the grass along a little
const float FIELD_KICK_RATE = 240.0;
// seconds a patch needs under a character to take its trampled set
const float FIELD_TRAMPLE_SET_TIME = 1.5;

float FieldHash(in vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.547);
}

void main()
{
	ivec2 size = textureSize(u_FoliageFieldMap, 0);
	ivec2 pix = ivec2(gl_FragCoord.xy);

	// previous state of the same world patch; outside: freshly exposed, at rest
	vec4 state = vec4(0.0);
	vec2 trample = vec2(0.0);
	ivec2 src = pix + ivec2(u_FIFieldShift.xy);
	if (u_FIFieldShift.z < 0.5 && all(greaterThanEqual(src, ivec2(0))) && all(lessThan(src, size)))
	{
		state = texelFetch(u_FoliageFieldMap, src, 0);
		trample = texelFetch(u_FoliageTrampleMap, src, 0).xy;
	}

	// world XY of the texel center
	vec2 xy = u_FIField.xy + (gl_FragCoord.xy / vec2(size) - 0.5) / u_FIField.z;

	float dt = u_FIFieldUpdate.x;
	float k = u_FIFieldUpdate.y;
	float c = u_FIFieldUpdate.z;
	vec2 d = state.xy;
	vec2 v = state.zw;

	// Force push / pull: the front that passed this frame
	v += FoliageForceWaveKick(xy, dt);

	// damped spring towards the trampled pose, semi-implicit Euler in steps of
	// at most 1/60 s
	int steps = clamp(int(ceil(dt * 60.0)), 1, 6);
	float h = dt / float(steps);
	for (int i = 0; i < 6; ++i)
	{
		if (i >= steps)
			break;
		v += (k * (trample - d) - c * v) * h;
		d += v * h;
	}

	// the characters standing on the patch (airborne ones leave it alone): a
	// hard contact at the probe height
	bool contact = false;
	int count = int(u_FIParams.x);
	for (int i = 0; i < FOLIAGE_MAX_INTERACTORS; ++i)
	{
		if (i >= count)
			break;
		vec4 axis = u_FICurrent[2 * i];
		vec4 body = u_FICurrent[2 * i + 1];
		if (body.w > 0.5)
			continue;
		vec2 n;
		float penetration = FoliageColliderPenetration(axis, body,
			vec3(xy, axis.z + FOLIAGE_PROBE_HEIGHT), n) * u_FIParams.z;
		if (penetration <= 0.0)
			continue;
		contact = true;

		vec2 velocity = body.yz;
		float speed = length(velocity);
		vec2 moveDir = speed > 1.0 ? velocity / speed : vec2(0.0);
		if (dot(n, n) < 0.25)
		{
			// right on the axis: the way the grass already leans, else the
			// walk, else a stable random way
			if (dot(d, d) > 0.25)
				n = normalize(d);
			else if (speed > 1.0)
				n = moveDir;
			else
			{
				float a = FieldHash(xy) * 6.2832;
				n = vec2(cos(a), sin(a));
			}
		}

		// one-sided: only what is still inside goes out, the sideways part of
		// the bend stays (no flipping, no splitting)
		float inside = penetration - dot(d, n);
		if (inside > 0.0)
		{
			d += inside * n;
			float vn = dot(v, n);
			if (vn < 0.0)
				v -= vn * n;
		}

		// walking through drags the grass along the walk a little
		float s = clamp(speed * (1.0 / 320.0), 0.0, 1.0);
		v += moveDir * (s * u_FIFieldUpdate.w * FIELD_KICK_RATE * dt);
	}

	// trampled set: taken while standing on the grass, lost slowly after
	if (contact)
		trample += (d * u_FIFieldTrample.x - trample) * (1.0 - exp(-dt / FIELD_TRAMPLE_SET_TIME));
	else
		trample *= exp(-dt * u_FIFieldTrample.y);

	float dLen = length(d);
	if (dLen > FIELD_MAX_DISPLACEMENT)
		d *= FIELD_MAX_DISPLACEMENT / dLen;
	float vLen = length(v);
	if (vLen > FIELD_MAX_SPEED)
		v *= FIELD_MAX_SPEED / vLen;

	// settle exactly to rest (half floats would creep forever)
	if (dot(d - trample, d - trample) < 1e-3 && dot(v, v) < 1e-2)
	{
		d = trample;
		v = vec2(0.0);
	}
	if (dot(trample, trample) < 1e-3)
		trample = vec2(0.0);

	out_Color = vec4(d, v);
	out_Trample = vec4(trample, 0.0, 0.0);
}

#endif
