/*[Vertex]*/
// Underwater suspended particles and micro-bubbles (r_waterParticles, tr_waterparticles.cpp,
// docs/rend2-water-particles.md). One point per particle, simulated and lit on the CPU:
//   attr_Position  xyz world position, w radius (negative: micro-bubble)
//   attr_Color     rgb lit color (light grid + dynamic lights, phase, albedo), a opacity (fades)
in vec4 attr_Position;
in vec4 attr_Color;

out vec4 var_PointColor;
out float var_PointRadius;

void main()
{
	gl_Position = vec4(attr_Position.xyz, 1.0);
	var_PointColor = attr_Color;
	var_PointRadius = attr_Position.w;
}

/*[Geometry]*/
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 _u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

// [0] world size of a pixel at distance 1, near fade start, near fade end, volume radius
// [1] far fade band (fraction of the radius), debug view, minimum radius in pixels, 1 = bubble texture
uniform vec4 u_WaterParticles[2];

in vec4 var_PointColor[];
in float var_PointRadius[];

out vec2 var_TexCoord;
flat out vec4 var_ParticleColor;	// rgb lit color, a opacity
flat out vec4 var_ParticleCenter;	// xyz world position (froxel fog lookup), w 1 = bubble

void main()
{
	vec3 P = gl_in[0].gl_Position.xyz;
	float radius = abs(var_PointRadius[0]);
	float distance = length(P - u_ViewOrigin);
	float alpha = var_PointColor[0].a;

	// nothing next to the lens (a speck there would be a screen sized blob),
	// a soft edge where the camera volume wraps
	alpha *= smoothstep(u_WaterParticles[0].y, u_WaterParticles[0].z, distance);
	float outer = u_WaterParticles[0].w;
	alpha *= 1.0 - smoothstep(outer * (1.0 - u_WaterParticles[1].x), outer, distance);

	// sub-pixel specks keep their coverage: grow to the minimum footprint and
	// trade the area for opacity, so far motes thin out instead of sparkling
	float minRadius = u_WaterParticles[1].z * distance * u_WaterParticles[0].x;
	if (radius < minRadius)
	{
		float ratio = radius / minRadius;
		alpha *= ratio * ratio;
		radius = minRadius;
	}
	if (alpha < 1.0 / 1024.0 && u_WaterParticles[1].y < 0.5)
		return;

	var_ParticleColor = vec4(var_PointColor[0].rgb, alpha);
	var_ParticleCenter = vec4(P, var_PointRadius[0] < 0.0 ? 1.0 : 0.0);

	// the camera block stores the view axes scaled to the far plane frustum
	// edge (zFar * tan(fov / 2)), not as unit vectors
	vec3 viewLeft = normalize(u_ViewLeft);
	vec3 viewUp = normalize(u_ViewUp);
	const vec2 corners[] = vec2[](
		vec2(-1.0, -1.0),
		vec2( 1.0, -1.0),
		vec2(-1.0,  1.0),
		vec2( 1.0,  1.0)
	);
	for (int i = 0; i < 4; ++i)
	{
		vec3 worldPos = P + radius * (-corners[i].x * viewLeft + corners[i].y * viewUp);
		gl_Position = u_viewProjectionMatrix * vec4(worldPos, 1.0);
		var_TexCoord = corners[i] * 0.5 + 0.5;
		EmitVertex();
	}
	EndPrimitive();
}

/*[Fragment]*/
uniform sampler2D u_DiffuseMap;		// micro-bubble texture (gfx/effects/bubble)
uniform vec4 u_WaterParticles[2];

in vec2 var_TexCoord;
flat in vec4 var_ParticleColor;
flat in vec4 var_ParticleCenter;

out vec4 out_Color;
out vec4 out_Glow;

void main()
{
	vec2 q = var_TexCoord * 2.0 - 1.0;
	float r2 = dot(q, q);
	if (r2 >= 1.0)
		discard;

	bool bubble = var_ParticleCenter.w > 0.5;
	vec3 color = var_ParticleColor.rgb;
	float coverage;
	if (bubble && u_WaterParticles[1].w > 0.5)
	{
		// stock bubble: light rim, clear middle; its brightness shades the rim
		vec4 tex = texture(u_DiffuseMap, vec2(var_TexCoord.x, 1.0 - var_TexCoord.y));
		coverage = tex.a;
		color *= tex.rgb;
	}
	else if (bubble)
	{
		// no bubble texture: a procedural ring with a faint middle
		float rim = (sqrt(r2) - 0.78) / 0.14;
		coverage = (0.12 + 0.88 * exp(-rim * rim)) * (1.0 - r2 * r2 * r2 * r2);
	}
	else
	{
		// procedural mote: soft Gaussian speck, zero at the quad edge
		coverage = exp(-3.0 * r2) * (1.0 - r2);
	}
	float a = coverage * var_ParticleColor.a;

	int debugView = int(u_WaterParticles[1].y + 0.5);
	if (debugView == 1)
	{
		// placement: opaque, cyan motes, yellow bubbles, no fog
		float mask = step(0.05, coverage);
		out_Color = vec4((bubble ? vec3(1.0, 0.9, 0.1) : vec3(0.1, 0.9, 1.0)) * mask, mask);
		out_Glow = vec4(0.0);
		return;
	}

	// premultiplied over (ONE, ONE_MINUS_SRC_ALPHA). The medium between the
	// camera and the particle (froxel fog, transparent layers look it up
	// themselves): color * T + S * alpha, as the liquid blends of generic.
	vec3 premultiplied = color * a;
#if defined(USE_FROXEL_FOG)
	if (debugView != 2)
	{
#if defined(USE_FROXEL_RGB)
		if (u_FroxelFogMode == 3)
		{
			vec3 T;
			vec3 S = FroxelFogRGB(var_ParticleCenter.xyz, T);
			premultiplied = premultiplied * T + S * a;
		}
		else
#endif
		if (u_FroxelFogMode == 1)
		{
			vec4 fog = FroxelFog(var_ParticleCenter.xyz);
			premultiplied = premultiplied * fog.a + fog.rgb * a;
		}
	}
#endif

	out_Color = vec4(premultiplied, a);
	// glow alpha 0 keeps the bloom buffer behind the particle untouched
	out_Glow = vec4(0.0);
}
