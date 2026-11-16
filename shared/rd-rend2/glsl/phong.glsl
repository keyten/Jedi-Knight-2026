/*[Vertex]*/
// The ordinary VS supplies its already skinned control points. Reusing its
// position/normal avoids skinning the character a second time for tessellation.
in float attr_Position2; // G2 geometry crease weight (separate static stream)
invariant gl_Position;
out vec3 phongVertex_phongPosition;
out vec3 phongVertex_phongNormal;
#if defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
out vec3 phongVertex_phongPreviousPosition;
out vec3 phongVertex_phongPreviousNormal;
#endif

void PhongControlPoint(vec3 worldPosition, vec3 worldNormal)
{
    phongVertex_phongPosition = worldPosition;
    phongVertex_phongNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-12)) * attr_Position2;
}
#if defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
void PhongPreviousControlPoint(vec3 worldPosition, vec3 worldNormal)
{
    phongVertex_phongPreviousPosition = worldPosition;
    phongVertex_phongPreviousNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-12)) * attr_Position2;
}
#endif

/*[TessControl]*/
layout(vertices = 3) out;
uniform vec4 u_PhongParams; // level, smoothing, maximum displacement, debug
void main()
{
#define V(type, name) phongPatch_##name[gl_InvocationID] = phongVertex_##name[gl_InvocationID];
    PHONG_VARYINGS
#undef V
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
        // One level for the entire character keeps shared edges identical,
        // including edges crossing material and Ghoul2 surface boundaries.
        gl_TessLevelOuter[0] = u_PhongParams.x;
        gl_TessLevelOuter[1] = u_PhongParams.x;
        gl_TessLevelOuter[2] = u_PhongParams.x;
        // A patch with three preserved crease vertices cannot move at all.
        // Keep boundary subdivisions identical to its neighbors, but avoid
        // unnecessary interior triangles on these planar patches.
        float curved = dot(phongVertex_phongNormal[0], phongVertex_phongNormal[0])
                     + dot(phongVertex_phongNormal[1], phongVertex_phongNormal[1])
                     + dot(phongVertex_phongNormal[2], phongVertex_phongNormal[2]);
        gl_TessLevelInner[0] = curved == 0.0 ? 1.0 : u_PhongParams.x;
    }
}

/*[TessEvaluation]*/
layout(triangles, fractional_even_spacing, ccw) in;
invariant gl_Position;
uniform vec4 u_PhongParams;
uniform mat4 u_PhongViewProjection;
#if defined(PHONG_LIGHTALL) || defined(PHONG_GENERIC)
out float var_PhongDisplacement;
#endif
#if defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
layout(std140) uniform TemporalInfo
{
    mat4 u_previousViewProjectionMatrix;
    vec2 u_currentJitter;
    vec2 u_previousJitter;
    float u_previousFrameTime;
};
#endif

vec3 PhongOffset(vec3 p, vec3 p0, vec3 p1, vec3 p2, vec3 n0, vec3 n1, vec3 n2)
{
    vec3 b = gl_TessCoord;
    // Project the linear point onto each control vertex tangent plane.
    vec3 d = -b.x * dot(p - p0, n0) * n0
             -b.y * dot(p - p1, n1) * n1
             -b.z * dot(p - p2, n2) * n2;
    d *= u_PhongParams.y;
    return d * min(1.0, u_PhongParams.z / max(length(d), 0.000001));
}

void main()
{
    vec3 b = gl_TessCoord;
#define V(type, name) name = phongPatch_##name[0] * b.x + phongPatch_##name[1] * b.y + phongPatch_##name[2] * b.z;
    PHONG_VARYINGS
#undef V
    vec3 delta = PhongOffset(phongPosition,
        phongPatch_phongPosition[0], phongPatch_phongPosition[1], phongPatch_phongPosition[2],
        phongPatch_phongNormal[0], phongPatch_phongNormal[1], phongPatch_phongNormal[2]);
    // Preserve the ordinary VS depth/clip convention and displace in world space.
    gl_Position = gl_in[0].gl_Position * b.x + gl_in[1].gl_Position * b.y + gl_in[2].gl_Position * b.z
                + u_PhongViewProjection * vec4(delta, 0.0);
#if defined(PHONG_LIGHTALL) || defined(PHONG_GENERIC)
    var_PhongDisplacement = length(delta) / max(u_PhongParams.z, 0.000001);
#endif
#if defined(PHONG_LIGHTALL)
#if defined(PER_PIXEL_LIGHTING)
    var_ViewDir.xyz -= delta;
#else
    var_Position += delta;
#endif
#elif defined(PHONG_FOG) || (defined(PHONG_GENERIC) && defined(USE_FOG))
    var_WSPosition += delta;
#elif defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
    var_Position = gl_Position;
    vec3 previousDelta = PhongOffset(phongPreviousPosition,
        phongPatch_phongPreviousPosition[0], phongPatch_phongPreviousPosition[1], phongPatch_phongPreviousPosition[2],
        phongPatch_phongPreviousNormal[0], phongPatch_phongPreviousNormal[1], phongPatch_phongPreviousNormal[2]);
    var_prevPosition += u_previousViewProjectionMatrix * vec4(previousDelta, 0.0);
#endif
}

/*[Fragment]*/
in float var_PhongDisplacement;
uniform vec4 u_PhongParams;
#undef main
void main()
{
    PhongOriginalFragment();
    if (u_PhongParams.w > 2.5)
        out_Color.rgb = mix(vec3(0.0, 0.1, 0.8), vec3(1.0, 0.15, 0.0),
            clamp(var_PhongDisplacement * 4.0, 0.0, 1.0));
    else if (u_PhongParams.w > 1.5)
        out_Color.rgb = mix(out_Color.rgb, vec3(0.05, 1.0, 0.15), 0.7);
}
