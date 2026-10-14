/*[Vertex]*/
in vec3 attr_Position;
in vec4 attr_TexCoord0;

uniform mat4 u_ModelViewProjectionMatrix;

out vec2 var_TexCoords;

void main()
{
	gl_Position = u_ModelViewProjectionMatrix * vec4(attr_Position, 1.0);
	var_TexCoords = attr_TexCoord0.st;
}

/*[Fragment]*/
uniform sampler2D u_TextureMap;
uniform vec4 u_Color;
uniform vec2 u_InvTexRes;

in vec2 var_TexCoords;

out vec4 out_Color;

const vec3  LUMINANCE_VECTOR = vec3(0.2126, 0.7152, 0.0722);

vec3 GetValues(vec2 offset, vec3 current)
{
	vec2 tc = var_TexCoords + u_InvTexRes * offset;
	vec3 minAvgMax = texture(u_TextureMap, tc).rgb;

#ifdef FIRST_PASS
	float lumi = max(dot(LUMINANCE_VECTOR, minAvgMax), 0.000001);
	// Legacy passes 10 (the historical range). Physical camera metering passes
	// 24 so moonlight through very bright emitters fits the same RGBA16F levels
	// texture without changing legacy exposure behaviour.
	float logRange = max(u_Color.x, 10.0);
	float loglumi = clamp(log2(lumi), -logRange, logRange);
	minAvgMax = vec3(loglumi / (2.0 * logRange) + 0.5);
#endif

	return vec3(
		min(current.x, minAvgMax.x),
		current.y + minAvgMax.y,
		max(current.z, minAvgMax.z));
}

void main()
{
	vec3 current = vec3(1.0, 0.0, 0.0);

#ifdef FIRST_PASS
	current = GetValues(vec2( 0.0,  0.0), current);
#else
	current = GetValues(vec2(-1.5, -1.5), current);
	current = GetValues(vec2(-0.5, -1.5), current);
	current = GetValues(vec2( 0.5, -1.5), current);
	current = GetValues(vec2( 1.5, -1.5), current);

	current = GetValues(vec2(-1.5, -0.5), current);
	current = GetValues(vec2(-0.5, -0.5), current);
	current = GetValues(vec2( 0.5, -0.5), current);
	current = GetValues(vec2( 1.5, -0.5), current);

	current = GetValues(vec2(-1.5,  0.5), current);
	current = GetValues(vec2(-0.5,  0.5), current);
	current = GetValues(vec2( 0.5,  0.5), current);
	current = GetValues(vec2( 1.5,  0.5), current);

	current = GetValues(vec2(-1.5,  1.5), current);
	current = GetValues(vec2(-0.5,  1.5), current);
	current = GetValues(vec2( 0.5,  1.5), current);
	current = GetValues(vec2( 1.5,  1.5), current);

	current.y *= 0.0625;
#endif

	out_Color = vec4(current, 1.0f);
}
