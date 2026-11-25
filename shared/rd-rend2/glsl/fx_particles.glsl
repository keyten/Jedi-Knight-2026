/*[Vertex]*/
// r_fxGpuParticles draw (tr_fxupgrades.cpp, docs/rend2-fx-upgrades.md): one
// instanced quad per pool slot, state from the simulation (fx_particles_sim).
// Emissive sparks / embers are velocity stretched and added (HDR); dust is a
// round premultiplied puff. Dead slots and the other blend group collapse to a
// point outside the clip volume.
uniform sampler2D u_TextureMap;	// pool x 4 rows: p0 pos, life; p1 vel, max life; p2 rgb, type; p3 size, opacity, bounce, seed
uniform mat4 u_FxMatrix[2];		// [0] world to clip
uniform vec4 u_FxParams[4];		// [0] xyz view origin, w group (0 emissive, 1 dust)
								// [1] x stretch (seconds of travel), y intensity

out vec2 var_Local;
flat out vec4 var_Color;
flat out vec2 var_Shape;		// x: half length / half width (streak), y: soft distance
out float var_ViewDepth;

void main()
{
	int id = gl_InstanceID;
	vec4 p0 = texelFetch(u_TextureMap, ivec2(id, 0), 0);
	vec4 p1 = texelFetch(u_TextureMap, ivec2(id, 1), 0);
	vec4 p2 = texelFetch(u_TextureMap, ivec2(id, 2), 0);
	vec4 p3 = texelFetch(u_TextureMap, ivec2(id, 3), 0);
	bool dust = p2.w > 1.5;
	if (p0.w <= 0.0 || dust != (u_FxParams[0].w > 0.5))
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		var_Local = vec2(0.0);
		var_Color = vec4(0.0);
		var_Shape = vec2(1.0);
		var_ViewDepth = 0.0;
		return;
	}

	float age = clamp(1.0 - p0.w / max(p1.w, 1.0e-3), 0.0, 1.0);
	vec3 toCamera = normalize(u_FxParams[0].xyz - p0.xyz);
	vec3 velocity = p1.xyz - toCamera * dot(p1.xyz, toCamera);
	float speed = length(velocity);
	vec3 axisA;
	if (speed > 1.0e-3)
		axisA = velocity / speed;
	else
	{
		vec3 helper = abs(toCamera.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		axisA = normalize(cross(helper, toCamera));
	}
	vec3 axisB = normalize(cross(toCamera, axisA));

	float size = p3.x * (dust ? 1.0 + age * 1.5 : 1.0);
	float tail = dust ? 0.0 : speed * u_FxParams[1].x;
	float halfLength = size + tail * 0.5;
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	// the head stays at the particle, the tail trails behind it
	vec3 centre = p0.xyz - axisA * tail * 0.5;
	vec3 position = centre + axisA * corner.x * halfLength + axisB * corner.y * size;

	vec4 color;
	if (dust)
	{
		float fade = smoothstep(0.0, 0.08, age) * (1.0 - age) * (1.0 - age);
		color = vec4(p2.rgb, p3.y * fade);
	}
	else
	{
		float fade = min(1.0, age * 30.0) * pow(1.0 - age, 1.5);
		// streak energy spreads over its length: keep the total constant
		color = vec4(p2.rgb * fade * u_FxParams[1].y * size / halfLength, 1.0);
	}

	var_Local = corner;
	var_Color = color;
	var_Shape = vec2(halfLength / size, dust ? size : max(size * 2.0, 2.0));
	vec4 clip = u_FxMatrix[0] * vec4(position, 1.0);
	var_ViewDepth = clip.w;
	gl_Position = clip;
}

/*[Fragment]*/
uniform sampler2D u_ScreenDepthMap;	// copy of the scene depth
uniform vec4 u_FxParams[4];		// [0] w group; [2] xy 1 / depth size, z proj[10], w proj[14]; [3] x split (window px, -1 none)

in vec2 var_Local;
flat in vec4 var_Color;
flat in vec2 var_Shape;
in float var_ViewDepth;

out vec4 out_Color;

void main()
{
	if (gl_FragCoord.x < u_FxParams[3].x)
		discard;
	float mask;
	if (u_FxParams[0].w > 0.5)
	{
		float r2 = dot(var_Local, var_Local);
		if (r2 >= 1.0)
			discard;
		mask = (1.0 - r2) * (1.0 - r2);
	}
	else
	{
		// capsule: round ends over one width, hot core
		float x = max(abs(var_Local.x) * var_Shape.x - (var_Shape.x - 1.0), 0.0);
		float r2 = x * x + var_Local.y * var_Local.y;
		if (r2 >= 1.0)
			discard;
		mask = exp(-r2 * 4.0);
	}

	float depth = texture(u_ScreenDepthMap, gl_FragCoord.xy * u_FxParams[2].xy).r;
	float sceneDepth = u_FxParams[2].w / (depth * 2.0 - 1.0 + u_FxParams[2].z);
	float soft = clamp((sceneDepth - var_ViewDepth) / var_Shape.y, 0.0, 1.0);

	if (u_FxParams[0].w > 0.5)
	{
		float a = var_Color.a * mask * soft;
		out_Color = vec4(var_Color.rgb * a, a);
	}
	else
	{
		out_Color = vec4(var_Color.rgb * (mask * soft), 0.0);
	}
}
