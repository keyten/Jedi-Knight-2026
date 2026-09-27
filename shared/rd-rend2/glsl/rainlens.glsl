/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Rain droplets on the camera lens (r_rainLens), tr_rainlens.cpp.
//
// Runs on the HDR scene after the SMAA T2x temporal resolve and motion blur,
// before bloom extraction and tone mapping: refracted lights keep their HDR
// value and bloom where they appear through a drop, and the screen fixed
// drops never enter the temporal history.
//
// Everything is procedural and deterministic in lens (screen) space, sized
// relative to the screen height so resolution, ultrawide and FOV changes
// keep the drop size:
//  - layer A: small beads in a dense hashed grid, each with its own life
//    cycle (grow in, sit, evaporate);
//  - layer B: sparse large drops, one per column and cycle, that stick for a
//    while and then slide down with a stick-slip motion, stretched into a
//    tail and leaving a thin drying trail that wipes the beads it crosses.
// A drop is a spherical cap: its analytic slope bends the view (magnified,
// inverted image of what is behind it) and a small local disc blur defocuses
// it. The rim is slightly darker (Fresnel), the glint scales with the local
// scene luminance, so there are no white spots and no global darkening.
//
// Drops born after the camera last saw the rain (u_RainLensParams2.x) are
// never shown, the ones already on the lens drain quickly: going under a
// roof stops new drops without any persistent state.
//
// USE_DEBUG: r_rainLensDebug views, displayed as they are (no tone map).

uniform sampler2D u_ScreenImageMap; // HDR scene

uniform vec4 u_RainLensParams;  // x = lens time (s), y = density (amount * intensity * wet), z = refraction, w = scale
uniform vec4 u_RainLensParams2; // x = last exposed time (s), y = drain time (s), z = debug view, w = display encoded HDR buffer

out vec4 out_Color;

#define BEAD_CELLS_PER_HEIGHT   13.0
#define SLIDER_CELLS_PER_HEIGHT 4.5
#define REFRACTION_SCALE        1.6

vec3 Hash33(vec3 p)
{
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}

// Drain/presence factor of a drop born at 'birth': zero if the camera was
// already under cover then, fading after the camera left the rain.
float Exposure(float birth, float time)
{
	float lastExposed = u_RainLensParams2.x;
	if (birth > lastExposed)
		return 0.0;
	float since = time - lastExposed;
	return since <= 0.0 ? 1.0 : clamp(1.0 - since / u_RainLensParams2.y, 0.0, 1.0);
}

// Spherical cap of unit radius evaluated at q (in drop radii).
// Returns mask in x, and the outward slope in zw (zero at the center, about
// one at the soft edge).
vec4 Cap(vec2 q, vec2 lopsided)
{
	// egg shaped, not a perfect circle
	float r = length(q) * (1.0 + dot(q, lopsided));
	if (r >= 1.0)
		return vec4(0.0);

	float h = sqrt(1.0 - r * r);
	float edge = smoothstep(1.0, 0.78, r);
	vec2 slope = q / max(h, 0.35);
	return vec4(edge, h, slope);
}

// Layer A: small static beads. p in bead cells.
vec4 Beads(vec2 p, float time, float density, out float radiusCells)
{
	vec2 cell = floor(p);
	vec2 f = fract(p);

	vec3 h0 = Hash33(vec3(cell, 17.0));
	float life = mix(4.0, 10.0, h0.x);
	float t = time + h0.y * life;
	float cycle = floor(t / life);
	float age = fract(t / life);
	float birth = time - age * life;

	vec3 h = Hash33(vec3(cell, cycle + 3.0));
	radiusCells = 0.0;
	if (h.z >= density * 0.55)
		return vec4(0.0);

	float presence = Exposure(birth, time);
	if (presence <= 0.0)
		return vec4(0.0);

	// grow in quickly, evaporate (shrink) at the end of the life
	float grow = smoothstep(0.0, 0.04, age);
	float evaporate = 1.0 - smoothstep(0.75, 1.0, age);
	float radius = mix(0.1, 0.3, h.x * h.x) * mix(0.6, 1.0, evaporate);
	vec2 center = 0.5 + (h.xy - 0.5) * 0.36;

	vec2 q = (f - center) / radius;
	q.x *= mix(0.88, 1.12, h.y);
	vec2 lopsided = (Hash33(vec3(cell, cycle + 7.0)).xy - 0.5) * 0.25;

	vec4 cap = Cap(q, lopsided);
	cap.x *= grow * evaporate * presence;
	radiusCells = radius;
	return cap;
}

// Layer B: sliding drops, one per column. p in slider cells, y up.
// trail returns the wiping mask of the drop's path.
vec4 Slider(vec2 p, float time, float density, float heightCells, out float radiusCells, out float trail)
{
	radiusCells = 0.0;
	trail = 0.0;

	float column = floor(p.x);
	vec3 h0 = Hash33(vec3(column, 0.0, 41.0));
	float life = mix(7.0, 13.0, h0.x);
	float t = time + h0.y * life;
	float cycle = floor(t / life);
	float age = fract(t / life) * life; // seconds
	float birth = time - age;

	vec3 h = Hash33(vec3(column, cycle, 53.0));
	if (h.z >= density * 0.8)
		return vec4(0.0);

	float presence = Exposure(birth, time);
	if (presence <= 0.0)
		return vec4(0.0);

	float radius = mix(0.2, 0.34, h.x);
	float x0 = column + 0.5 + (h.y - 0.5) * 0.3;
	float y0 = heightCells * mix(0.35, 1.05, Hash33(vec3(column, cycle, 59.0)).x);

	// stick, then slide with a stick-slip motion; once the camera is under
	// cover the drop lets go at once and runs off faster
	float stick = mix(0.8, 3.5, h.y);
	float drained = max(time - u_RainLensParams2.x, 0.0);
	float s = max(age - stick, 0.0) + drained * 2.0;
	float speed = mix(0.6, 1.4, h.x); // cells per second
	float n = 1.3;
	float travel = speed * (s - 0.9 * sin(6.2831853 * n * s) / (6.2831853 * n));
	float velocity = speed * (1.0 - 0.9 * cos(6.2831853 * n * s)) * step(0.0, s - 1e-4);

	float wobble = sin(s * 2.3 + h.x * 6.0) * 0.06;
	vec2 center = vec2(x0 + wobble, y0 - travel);
	// the drop shrinks a little while it feeds the trail
	radius *= mix(1.0, 0.75, clamp(travel / heightCells, 0.0, 1.0));

	vec2 d = p - center;

	// trail: thin film from the start point down to the drop, drying from the top
	if (d.y > 0.0 && d.y < y0 - center.y)
	{
		float dry = clamp(1.0 - d.y / max(velocity + 0.5, 0.5) / 2.2, 0.0, 1.0);
		float width = radius * 0.8;
		trail = (1.0 - smoothstep(width * 0.6, width, abs(d.x))) * dry * presence;
	}

	// tail: stretched upwards while moving
	vec2 q = d / radius;
	if (q.y > 0.0)
		q.y /= 1.0 + clamp(velocity * 0.6, 0.0, 1.5);

	vec2 lopsided = vec2((h.y - 0.5) * 0.2, -0.12);
	vec4 cap = Cap(q, lopsided);
	cap.x *= presence;

	// small beads left behind along the trail
	if (trail > 0.0 && cap.x <= 0.0)
	{
		float seg = floor(d.y * 3.0);
		vec3 hb = Hash33(vec3(column, cycle, seg + 71.0));
		if (hb.x < 0.5)
		{
			vec2 bc = vec2(x0 + (hb.y - 0.5) * radius, center.y + (seg + 0.5) / 3.0);
			float br = radius * mix(0.18, 0.32, hb.z);
			vec4 bead = Cap((p - bc) / br, vec2(0.0));
			bead.x *= trail;
			radiusCells = br;
			return bead;
		}
	}

	radiusCells = radius;
	return cap;
}

float Luminance(vec3 c)
{
	return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec2 screenSize = vec2(textureSize(u_ScreenImageMap, 0));
	vec2 uv = gl_FragCoord.xy / screenSize;
	vec3 scene = texelFetch(u_ScreenImageMap, ivec2(gl_FragCoord.xy), 0).rgb;

	float time = u_RainLensParams.x;
	float density = u_RainLensParams.y;
	float scale = max(u_RainLensParams.w, 0.1);
	float aspect = screenSize.x / screenSize.y;

	// lens space: y up, normalised by the screen height
	vec2 lens = vec2((uv.x - 0.5) * aspect, uv.y);

	float beadCells = BEAD_CELLS_PER_HEIGHT / scale;
	float sliderCells = SLIDER_CELLS_PER_HEIGHT / scale;

	float beadRadius, sliderRadius, trail;
	vec4 bead = Beads(lens * beadCells + vec2(37.0, 0.0), time, density, beadRadius);
	vec4 slider = Slider(lens * sliderCells + vec2(11.0, 0.0), time, density, sliderCells, sliderRadius, trail);

	// the sliding drops' trails wipe the beads ("merge")
	bead.x *= 1.0 - clamp(trail * 1.5, 0.0, 1.0);

	// dominant drop; radius in texture coordinates (y)
	float mask;
	vec2 slope;
	float radiusUV;
	if (slider.x >= bead.x)
	{
		mask = slider.x;
		slope = slider.zw;
		radiusUV = sliderRadius / sliderCells;
	}
	else
	{
		mask = bead.x;
		slope = bead.zw;
		radiusUV = beadRadius / beadCells;
	}

	// thin water film of the trail: weak refraction only
	float film = trail * (1.0 - mask) * 0.35;

	vec2 offset = vec2(0.0);
	vec3 color = scene;
	if (mask > 0.001 || film > 0.001)
	{
		// magnified, inverted view of what is behind the drop
		offset = -slope * radiusUV * u_RainLensParams.z * REFRACTION_SCALE * mask;
		offset.x /= aspect;
		offset += vec2(0.0, 0.002) * film * u_RainLensParams.z;

		vec2 refrUV = clamp(uv + offset, vec2(0.0), vec2(1.0));

		// defocus: a small rotated 4 tap disc, only inside drops
		float blur = radiusUV * 0.18 * mask;
		vec2 bx = vec2(blur / aspect, blur * 0.3);
		vec2 by = vec2(-blur * 0.3 / aspect, blur);
		vec3 refracted = textureLod(u_ScreenImageMap, refrUV, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV + bx, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV - bx, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV + by, 0.0).rgb * 0.2;
		refracted += textureLod(u_ScreenImageMap, refrUV - by, 0.0).rgb * 0.2;

		// Fresnel: slightly darker rim, the transmission of the water
		float rim = clamp(dot(slope, slope) * 0.5, 0.0, 1.0);
		refracted *= 0.97 - 0.09 * rim;

		// glint towards a light above the camera, tied to the local luminance
		vec3 n = normalize(vec3(slope * 0.8, 1.0));
		vec3 halfVec = normalize(vec3(-0.35, 0.55, 1.0) + vec3(0.0, 0.0, 1.0));
		float spec = pow(max(dot(n, halfVec), 0.0), 80.0);
		refracted += spec * Luminance(refracted) * 0.6;

		color = mix(scene, refracted, clamp(max(mask, film), 0.0, 1.0));
	}

#if defined(USE_DEBUG)
	int view = int(u_RainLensParams2.z);
	if (view == 1)
		color = vec3(mask, film, 0.0);
	else if (view == 2)
		color = mask > 0.0 ? normalize(vec3(slope * 0.8, 1.0)) * 0.5 + 0.5 : vec3(0.5, 0.5, 1.0);
	else if (view == 3)
		color = vec3(abs(offset) * 40.0, 0.0);
	else if (view == 4)
	{
		// left: scene, right: composition (display range preview)
		vec3 c = uv.x < 0.5 ? scene : color;
		if (u_RainLensParams2.w < 0.5)
			c = pow(c / (1.0 + c), vec3(1.0 / 2.2));
		color = abs(uv.x - 0.5) < 0.0015 ? vec3(1.0, 0.8, 0.0) : c;
	}
#endif

	out_Color = vec4(color, 1.0);
}
