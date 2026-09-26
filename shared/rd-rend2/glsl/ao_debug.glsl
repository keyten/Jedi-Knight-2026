/*[Vertex]*/
out vec2 var_ScreenTex;

void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
	var_ScreenTex = position * 0.5 + vec2(0.5);
}

/*[Fragment]*/
// r_debugAO full screen views of the screen-space AO / contact shadow buffers
// (the lightall based views 7-9 and 12 are written by lightall itself).

uniform sampler2D u_ScreenImageMap; // buffer to show
uniform sampler2D u_AODepthMap;     // GTAO linear depth

uniform vec4 u_AOSettings;          // x = r_debugAO, y = zFar

in vec2 var_ScreenTex;

out vec4 out_Color;

vec3 OctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	return normalize(n);
}

void main()
{
	int view = int(u_AOSettings.x);
	vec4 s = texture(u_ScreenImageMap, var_ScreenTex);
	vec3 color;

	if (view == 4)
	{
		// reconstructed view space normals
		color = s.gba;
	}
	else if (view == 5)
	{
		// linear depth, logarithmic; depth hack pixels red, sky blue
		float z = textureLod(u_AODepthMap, var_ScreenTex, 0.0).r;
		if (z < 0.0)
			color = vec3(0.5, 0.0, 0.0);
		else if (z >= u_AOSettings.y * 0.999)
			color = vec3(0.0, 0.0, 0.3);
		else
			color = vec3(clamp(log2(max(z, 1.0)) / log2(u_AOSettings.y), 0.0, 1.0));
	}
	else if (view == 6)
	{
		// contact shadow visibility
		color = vec3(s.g);
	}
	else if (view == 11)
	{
		// world space bent normal * 0.5 + 0.5
		color = OctDecode(s.ba) * 0.5 + 0.5;
	}
	else
	{
		color = vec3(s.r);
	}

	out_Color = vec4(color, 1.0);
}
