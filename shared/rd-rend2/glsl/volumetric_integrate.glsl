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
//   x  = extinction * length
//   Ts = exp(-x)
//   S += T * j * length * phi(x),  phi(x) = (1 - exp(-x)) / x = the mean transmittance in the slice
//   T *= Ts
//
// j = j_scatter + j_emissive (radiance per world unit): the baked + sun source, the dynamic volume
// (dynamic light scattering + emission of local volumes and FX particles). phi -> 1 as x -> 0 (series
// below 0.05, no division by 0): a glowing medium without extinction adds j * length, it neither
// vanishes nor explodes; phi <= 1, so S stays bounded by j * length per slice.
//
// out_Color  integrated volume, slice u_FroxelSlice: (S, T) at the far side of the slice
// out_Carry  the same, for the next slice
// (beyond far the media are integrated analytically by FroxelLookup, lit by the tail pass of the
// injection)
//
// RGB extinction (r_volumetricFogRGBExtinction, USE_FROXEL_RGB): the same per channel with sigma_t.rgb of
// u_FroxelExtinction (the source already holds sigma_s.rgb * L = sigma_t.rgb * albedo.rgb * L):
//   x.rgb  = sigma_t.rgb * length
//   S.rgb += T.rgb * j.rgb * length * phi(x.rgb)
//   T.rgb *= exp(-x.rgb)
// out_Color.rgb = S, out_Transmittance.rgb = T (draw buffers 2 and 3: the integrated T and its
// carry). The alpha channels keep the scalar transmittance exp(-integral of sigma), sigma = mean of
// sigma_t.rgb (source.a): what the scalar mode computes, the reference of the debug views 53 / 54
// and the transmittance of the scalar lookups (debug views 1, 7).

uniform sampler3D u_FroxelSource;	// baked + sun emission (rgb), extinction (a)
uniform sampler3D u_FroxelDynamic;	// dynamic light scattering + emission (rgb)
uniform sampler2D u_FroxelCarry;
uniform int u_FroxelSlice;
#if defined(USE_FROXEL_RGB)
uniform sampler3D u_FroxelExtinction;	// sigma_t.rgb
uniform sampler2D u_FroxelCarryT;		// T.rgb after the previous slice
#endif

// fragment outputs are bound to draw buffers by name (shaderOutputNames, tr_glsl.cpp):
// 0 = out_Color, 1 = out_Glow
out vec4 out_Color;		// integrated volume
out vec4 out_Glow;		// carry
#define out_Carry out_Glow
#if defined(USE_FROXEL_RGB)
// 2 = out_SSRNormal, 3 = out_SSRSpecular
out vec4 out_SSRNormal;
out vec4 out_SSRSpecular;
#define out_Transmittance out_SSRNormal
#define out_CarryT out_SSRSpecular
#endif

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

	// debug views 30-32 (volumetric_inject.glsl): the source alone, without extinction
	int debugView = int(u_FroxelDebugParams.x);
	if (debugView >= 30 && debugView <= 32)
		extinction = 0.0;

#if defined(USE_FROXEL_RGB)
	vec3 extinctionRGB = texelFetch(u_FroxelExtinction, ivec3(cell, slice), 0).rgb;
	if (debugView >= 30 && debugView <= 32)
		extinctionRGB = vec3(0.0);
	vec3 T = vec3(1.0);
	if (slice > 0)
		T = texelFetch(u_FroxelCarryT, cell, 0).rgb;

	vec3 xRGB = max(extinctionRGB, vec3(0.0)) * pathLength;
	vec3 sliceT = exp(-xRGB);
	state.rgb += T * emission * pathLength * FroxelPhi(xRGB, sliceT);
	T *= sliceT;
	state.a *= exp(-max(extinction, 0.0) * pathLength);

	out_Color = state;
	out_Carry = state;
	out_Transmittance = vec4(T, state.a);
	out_CarryT = vec4(T, state.a);
#else
	float x = max(extinction, 0.0) * pathLength;
	float sliceTransmittance = exp(-x);
	// below 0.05 the series (1 - exp(-x) cancels in fp32 there; truncation < 1e-9)
	float phi = (x < 0.05) ?
		1.0 - x * (1.0 / 2.0 - x * (1.0 / 6.0 - x * (1.0 / 24.0 - x * (1.0 / 120.0)))) :
		(1.0 - sliceTransmittance) / x;
	vec3 scattered = emission * (pathLength * phi);

	state.rgb += state.a * scattered;
	state.a *= sliceTransmittance;

	out_Color = state;
	out_Carry = state;
#endif
}
