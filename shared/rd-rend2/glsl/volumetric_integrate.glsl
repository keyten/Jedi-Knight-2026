/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// Froxel fog integration (tr_volumetric.cpp): slice u_FroxelSlice of every froxel column, front to
// back. u_FroxelCarry holds the state after the previous slice.
//
// Beer-Lambert with the medium constant inside a slice (the discretisation of the legacy volumetric
// ray march, color += light * T * (1 - exp(-z))):
//
//   Ts = exp(-extinction * length)
//   S += T * (emission / extinction) * (1 - Ts)
//   T *= Ts
//
// out_Color  integrated volume, slice u_FroxelSlice: (S, T) at the far side of the slice
// out_Carry  the same, for the next slice
// (beyond far the media are integrated analytically by FroxelLookup, lit by the tail pass of the
// injection)

uniform sampler3D u_FroxelSource;	// baked + sun emission (rgb), extinction (a)
uniform sampler3D u_FroxelDynamic;	// dynamic light emission (rgb)
uniform sampler2D u_FroxelCarry;
uniform int u_FroxelSlice;

// fragment outputs are bound to draw buffers by name (shaderOutputNames, tr_glsl.cpp):
// 0 = out_Color, 1 = out_Glow
out vec4 out_Color;		// integrated volume
out vec4 out_Glow;		// carry
#define out_Carry out_Glow

void main()
{
	ivec2 cell = ivec2(gl_FragCoord.xy);
	int slice = u_FroxelSlice;
	float numSlices = u_FroxelGridSize.z;

	vec4 source = texelFetch(u_FroxelSource, ivec3(cell, slice), 0);
	vec3 emission = source.rgb + texelFetch(u_FroxelDynamic, ivec3(cell, slice), 0).rgb;
	float extinction = source.a;

	vec4 state = vec4(0.0, 0.0, 0.0, 1.0);
	if (slice > 0)
		state = texelFetch(u_FroxelCarry, cell, 0);

	// path length through the slice along the ray of the froxel center
	vec2 ndc = (vec2(cell) + 0.5) / u_FroxelGridSize.xy * 2.0 - 1.0;
	vec3 ray = u_FroxelRayForward.xyz + ndc.x * u_FroxelRayRight.xyz + ndc.y * u_FroxelRayUp.xyz;
	float sliceNear = FroxelWToDepth(float(slice) / numSlices);
	float sliceFar = FroxelWToDepth(float(slice + 1) / numSlices);
	float pathLength = (sliceFar - sliceNear) * length(ray);

	vec3 scattered;
	float sliceTransmittance = exp(-extinction * pathLength);
	if (extinction > 1e-7)
	{
		scattered = emission / extinction * (1.0 - sliceTransmittance);
	}
	else
	{
		scattered = emission * pathLength;
	}

	state.rgb += state.a * scattered;
	state.a *= sliceTransmittance;

	out_Color = state;
	out_Carry = state;
}
