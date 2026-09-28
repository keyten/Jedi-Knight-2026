/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Persistent foliage bend field (r_foliageBendField, tr_foliagefield.cpp). The
// FoliageInteraction block and the collider functions come from
// foliage_interact.glsl (pasted in front as a library).
//
// Update pass (default): one texel = one world XY patch around the player.
// State rg = bend d, ba = its velocity v. Each frame the previous state is
// read at the texel shift of the scrolled origin (freshly exposed texels
// start at rest), then a damped spring pulls d towards the push of the
// colliders standing on the patch (none: towards 0); walking adds a kick
// along the walk, so the plants swing back past rest a little after the
// character left. No blur: the state never spreads to other texels.
//
// DEBUG_VIEW: r_foliageBendFieldDebug 1, the current field in a corner of the
// screen: left square = bend vector (red / green = +x / +y, gray = rest),
// right square = bend magnitude heat; the player is the center.

out vec4 out_Color;

#if defined(DEBUG_VIEW)

uniform vec4 u_FoliageFieldDebug;	// x, y of the lower left corner, square size (pixels), bend of full heat

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

// the colliders touch the patch at this height above their feet (grass tips)
const float FIELD_PROBE_HEIGHT = 10.0;
// state limits: FoliageApplyBend caps the stem at 65 degrees anyway
const float FIELD_MAX_BEND = 1.5;
const float FIELD_MAX_SPEED = 24.0;
// kick rate of a walk through at full contact and full speed (bend / s^2 at
// r_foliageBendFieldImpulse 1)
const float FIELD_KICK_RATE = 12.0;

void main()
{
	ivec2 size = textureSize(u_FoliageFieldPrevMap, 0);
	ivec2 pix = ivec2(gl_FragCoord.xy);

	// previous state of the same world patch; outside: freshly exposed, at rest
	vec4 state = vec4(0.0);
	ivec2 src = pix + ivec2(u_FIFieldShift.xy);
	if (u_FIFieldShift.z < 0.5 && all(greaterThanEqual(src, ivec2(0))) && all(lessThan(src, size)))
		state = texelFetch(u_FoliageFieldPrevMap, src, 0);

	// world XY of the texel center
	vec2 xy = u_FIField.xy + (gl_FragCoord.xy / vec2(size) - 0.5) / u_FIField.z;

	// push of the characters standing on the patch (airborne ones leave it)
	vec2 target = vec2(0.0);
	vec2 kick = vec2(0.0);
	int count = int(u_FIParams.x);
	for (int i = 0; i < FOLIAGE_MAX_INTERACTORS; ++i)
	{
		if (i >= count)
			break;
		vec4 axis = u_FICurrent[2 * i];
		vec4 body = u_FICurrent[2 * i + 1];
		if (body.w > 0.5)
			continue;
		float contact;
		vec2 drive;
		target += FoliageColliderPush(axis, body, vec3(xy, axis.z + FIELD_PROBE_HEIGHT), contact, drive);
		kick += drive * contact;
	}
	target *= u_FIParams.z;

	// damped spring, semi-implicit Euler in steps of at most 1/60 s
	float dt = u_FIFieldUpdate.x;
	float k = u_FIFieldUpdate.y;
	float c = u_FIFieldUpdate.z;
	int steps = clamp(int(ceil(dt * 60.0)), 1, 6);
	float h = dt / float(steps);
	vec2 d = state.xy;
	vec2 v = state.zw + kick * (u_FIFieldUpdate.w * FIELD_KICK_RATE * u_FIParams.z * dt);
	for (int i = 0; i < 6; ++i)
	{
		if (i >= steps)
			break;
		v += (k * (target - d) - c * v) * h;
		d += v * h;
	}

	float dLen = length(d);
	if (dLen > FIELD_MAX_BEND)
		d *= FIELD_MAX_BEND / dLen;
	float vLen = length(v);
	if (vLen > FIELD_MAX_SPEED)
		v *= FIELD_MAX_SPEED / vLen;

	// settle exactly to rest (half floats would creep forever)
	if (dot(d, d) < 1e-6 && dot(v, v) < 1e-4)
	{
		d = vec2(0.0);
		v = vec2(0.0);
	}

	out_Color = vec4(d, v);
}

#endif
