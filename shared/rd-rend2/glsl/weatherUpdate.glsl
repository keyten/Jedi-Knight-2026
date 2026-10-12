/*[Vertex]*/

layout(std140) uniform Scene
{
	vec4 u_PrimaryLightOrigin;
	vec3 u_PrimaryLightAmbient;
	int  u_globalFogIndex;
	vec3 u_PrimaryLightColor;
	float u_PrimaryLightRadius;
	float u_frameTime;
	float u_deltaTime;
};

uniform vec2 u_MapZExtents;
uniform vec3 u_EnvForce;
uniform vec4 u_RandomOffset;

in vec3 attr_Position;
in vec3 attr_Color;

out vec3 var_Position;
out vec3 var_Velocity;

#if defined(USE_RAIN_SPLASHES)
// r_rainSplashes: impact events against the static rain occlusion map
uniform vec2 u_ZoneOffset[9];			// world XY offset of each VBO chunk slot
uniform mat4 u_WeatherMvp;				// world -> weather depth clip space (orthographic)
uniform sampler2D u_ShadowMap;			// weather depth: first rain occluder
uniform sampler2D u_WeatherSurfaceMap;	// world geometry only (maps with weather brushes)
uniform vec4 u_SplashParams;	// 1 / lifetime (1/ms), particles per chunk, depth range (world units), unused
uniform vec4 u_SplashParams2;	// weather map texel (uv), edge height limit, surface tolerance (world units), brush check (0/1)

in vec4 attr_TexCoord0;	// impact: world xyz, state (below)
out vec4 var_Impact;

// impact.w, at most one impact per fall of a drop:
//   2         fresh impact event in this fall (consumed once by weatherRippleImpulse)
//   -3        fresh event whose drop respawned in the same update
//   (0, 1]   life of this fall's splash (1 -> 0)
//   [-1, 0)  -life of a splash from an earlier fall, still running
//   SPENT    this fall's splash is over
//   0        no splash; the drop has not hit anything in this fall
// weatherSplash.glsl draws the fresh markers plus the decaying +/- life states.
const float IMPACT_SPENT = -2.0;
const float IMPACT_FRESH = 2.0;
const float IMPACT_FRESH_OLD_FALL = -3.0;
#endif

const float CHUNK_EXTENDS = 2000.0;
const float HALF_CHUNK_EXTENDS = CHUNK_EXTENDS * 0.5;

vec3 NewParticleZPosition( in vec3 in_position )
{
	vec3 position = in_position;
	position.xy += u_RandomOffset.xy;
	position.z += u_MapZExtents.y - u_MapZExtents.x;

	return position;
}

#if defined(USE_RAIN_SPLASHES)
// The weather projection is orthographic: w = 1 and the depth is affine in
// world z (it grows downward, the map top is 0).
float WeatherDepth( in vec3 position )
{
	return (u_WeatherMvp * vec4(position, 1.0)).z * 0.5 + 0.5;
}

vec2 WeatherUV( in vec3 position )
{
	return (u_WeatherMvp * vec4(position, 1.0)).xy * 0.5 + 0.5;
}

// world z of the weather depth d above (x, y): row 2 of the matrix solved for z
float SurfaceHeight( in vec2 xy, in float d )
{
	return (2.0 * d - 1.0 - u_WeatherMvp[0][2] * xy.x - u_WeatherMvp[1][2] * xy.y -
		u_WeatherMvp[3][2]) / u_WeatherMvp[2][2];
}

// The rain simulation never collides: the drop passes through the first
// exposed surface and weather.glsl hides it from there on. An impact is the
// frame in which the drop crosses that surface. Both ends are compared with
// the surface of the column the drop lands in, so a drop that is already
// under a roof (the floor below it, a drop blown in under an overhang) never
// hits. A drop that went under a slope can drift into a lower column and
// come out above it again: the impact state allows one impact per fall.
bool DetectImpact( in vec3 localStart, in vec3 localEnd, out vec3 impact )
{
	int slot = clamp(gl_VertexID / max(int(u_SplashParams.y + 0.5), 1), 0, 8);
	vec3 zone = vec3(u_ZoneOffset[slot], 0.0);
	vec3 p0 = localStart + zone;
	vec3 p1 = localEnd + zone;
	impact = p1;

	vec2 uv1 = WeatherUV(p1);
	if (any(lessThan(uv1, vec2(0.0))) || any(greaterThan(uv1, vec2(1.0))))
		return false;

	float surface = textureLod(u_ShadowMap, uv1, 0.0).r;
	float d0 = WeatherDepth(p0) - surface;
	float d1 = WeatherDepth(p1) - surface;
	if (d0 > 0.0 || d1 <= 0.0)
		return false;

	// sub-frame crossing, then snapped onto the surface under it
	vec3 hit = mix(p0, p1, d0 / (d0 - d1));
	vec2 uv = WeatherUV(hit);
	float hitDepth = textureLod(u_ShadowMap, uv, 0.0).r;
	float height = SurfaceHeight(hit.xy, hitDepth);
	float tolerance = u_SplashParams2.z;
	if (abs(height - hit.z) > abs(p1.z - p0.z) + tolerance)
		return false;

	// roof edges and texel quantisation: the neighbours of a real landing
	// spot are at about the same height (slopes and stairs stay below the
	// limit, a roof edge above a courtyard does not)
	vec2 texel = vec2(u_SplashParams2.x, 0.0);
	vec4 neighbours = vec4(
		textureLod(u_ShadowMap, uv + texel.xy, 0.0).r,
		textureLod(u_ShadowMap, uv - texel.xy, 0.0).r,
		textureLod(u_ShadowMap, uv + texel.yx, 0.0).r,
		textureLod(u_ShadowMap, uv - texel.yx, 0.0).r);
	vec4 rise = abs(neighbours - vec4(hitDepth)) * u_SplashParams.z;
	if (max(max(rise.x, rise.y), max(rise.z, rise.w)) > u_SplashParams2.y)
		return false;

	// weather brushes are baked into the occlusion map: only an occluder that
	// is real world geometry makes a splash
	if (u_SplashParams2.w > 0.5)
	{
		float world = textureLod(u_WeatherSurfaceMap, uv, 0.0).r;
		if (abs(world - hitDepth) * u_SplashParams.z > tolerance)
			return false;
	}

	impact = vec3(hit.xy, height);
	return true;
}
#endif

void main()
{
	var_Velocity = attr_Color;
	var_Velocity = mix(var_Velocity, u_EnvForce, u_deltaTime * 0.002);
	var_Position = attr_Position;
	var_Position += var_Velocity * u_deltaTime;

#if defined(USE_RAIN_SPLASHES)
	float state = attr_TexCoord0.w;
	float decay = u_deltaTime * u_SplashParams.x;
	if (state > 1.5)
		state = max(1.0 - decay, 0.0);
	else if (state < -2.5)
		state = -max(1.0 - decay, 0.0);
	else if (state > 0.0)
		state = state - decay > 0.0 ? state - decay : IMPACT_SPENT;
	else if (state < 0.0 && state > -1.5)
		state = min(state + decay, 0.0);
	var_Impact = vec4(attr_TexCoord0.xyz, state);

	// before the respawn and the XY wrap, which both teleport the drop
	vec3 impact = vec3(0.0);
	if (state <= 0.0 && state > -1.5 && DetectImpact(attr_Position, var_Position, impact))
		var_Impact = vec4(impact, IMPACT_FRESH);
#endif

	if (var_Position.z < u_MapZExtents.x)
	{
		var_Position = NewParticleZPosition(var_Position);
		var_Velocity.xy = u_EnvForce.xy;
#if defined(USE_RAIN_SPLASHES)
		// a new fall: a running splash keeps its life, the drop may hit again
		var_Impact.w = var_Impact.w > 1.5 ? IMPACT_FRESH_OLD_FALL :
			var_Impact.w > 0.0 ? -var_Impact.w :
			var_Impact.w < -1.5 ? 0.0 : var_Impact.w;
#endif
	}

	// Keep each physical VBO slot within its own chunk. The CPU can then
	// frustum-cull that slot without changing its particle identity or update.
	var_Position.xy = mod(var_Position.xy + vec2(HALF_CHUNK_EXTENDS),
		vec2(CHUNK_EXTENDS)) - vec2(HALF_CHUNK_EXTENDS);
}
