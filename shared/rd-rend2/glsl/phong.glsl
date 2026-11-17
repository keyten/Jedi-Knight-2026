/*[Vertex]*/
// The ordinary VS supplies its already skinned control points. Reusing its
// position/normal avoids skinning the character a second time for tessellation.
#if defined(USE_VORONOI_FRACTURING)
out vec2 phongVertex_voronoiUV;
#else
in float attr_Position2; // G2 geometry crease weight (separate static stream)
#endif
invariant gl_Position;
out vec3 phongVertex_phongPosition;
out vec3 phongVertex_phongNormal;
#if defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
out vec3 phongVertex_phongPreviousPosition;
out vec3 phongVertex_phongPreviousNormal;
#endif

void PhongControlPoint(vec3 worldPosition, vec3 worldNormal, vec2 uv)
{
    phongVertex_phongPosition = worldPosition;
    vec3 normal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-12));
#if defined(USE_VORONOI_FRACTURING)
    phongVertex_voronoiUV = uv;
    phongVertex_phongNormal = normal;
#else
    phongVertex_phongNormal = normal * attr_Position2;
#endif
}
#if defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
void PhongPreviousControlPoint(vec3 worldPosition, vec3 worldNormal)
{
    phongVertex_phongPreviousPosition = worldPosition;
#if defined(USE_VORONOI_FRACTURING)
    phongVertex_phongPreviousNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-12));
#else
    phongVertex_phongPreviousNormal = worldNormal * inversesqrt(max(dot(worldNormal, worldNormal), 1e-12)) * attr_Position2;
#endif
}
#endif

/*[TessControl]*/
layout(vertices = 3) out;
uniform vec4 u_PhongParams; // level, smoothing, maximum displacement, debug
#if defined(USE_VORONOI_FRACTURING)
uniform vec3 u_VoronoiViewOrigin;
float VoronoiEdgeLevel(vec3 a, vec3 b)
{
    vec3 edge = b - a;
    float lengthSq = max(dot(edge, edge), 1e-6);
    float t = clamp(dot(u_VoronoiViewOrigin - a, edge) / lengthSq, 0.0, 1.0);
    float d = distance(u_VoronoiViewOrigin, a + t * edge);
    return mix(1.0, clamp(sqrt(lengthSq) / 10.0, 1.0, u_PhongParams.x),
        1.0 - smoothstep(200.0, 400.0, d));
}
#endif
void main()
{
#define V(type, name) phongPatch_##name[gl_InvocationID] = phongVertex_##name[gl_InvocationID];
    PHONG_VARYINGS
#undef V
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    if (gl_InvocationID == 0)
    {
#if defined(USE_VORONOI_FRACTURING)
        vec3 a = phongVertex_phongPosition[0];
        vec3 b = phongVertex_phongPosition[1];
        vec3 c = phongVertex_phongPosition[2];
        gl_TessLevelOuter[0] = VoronoiEdgeLevel(b, c);
        gl_TessLevelOuter[1] = VoronoiEdgeLevel(c, a);
        gl_TessLevelOuter[2] = VoronoiEdgeLevel(a, b);
        vec3 nearest = clamp(u_VoronoiViewOrigin, min(a, min(b, c)), max(a, max(b, c)));
        float inside = 1.0 - smoothstep(200.0, 400.0, distance(nearest, u_VoronoiViewOrigin));
        float edge = max(length(b - a), max(length(c - b), length(a - c)));
        gl_TessLevelInner[0] = mix(1.0, clamp(edge / 10.0, 1.0, u_PhongParams.x), inside);
#else
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
#endif
    }
}

/*[TessEvaluation]*/
#if defined(USE_VORONOI_FRACTURING)
layout(triangles, fractional_odd_spacing, ccw) in;
uniform sampler2D u_VoronoiHeight;
uniform vec3 u_VoronoiViewOrigin;
#else
layout(triangles, fractional_even_spacing, ccw) in;
#endif
invariant gl_Position;
uniform vec4 u_PhongParams;
uniform mat4 u_PhongViewProjection;
#if (defined(PHONG_LIGHTALL) || defined(PHONG_GENERIC)) && !defined(USE_VORONOI_FRACTURING)
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

#if defined(USE_VORONOI_FRACTURING)
vec3 VoronoiOffset()
{
    vec3 a = phongPatch_phongPosition[0];
    vec3 b = phongPatch_phongPosition[1];
    vec3 c = phongPatch_phongPosition[2];
    vec3 edgeLengths = vec3(length(c - b), length(a - c), length(b - a));
    float area2 = length(cross(b - a, c - a));
    vec3 sideDistance = gl_TessCoord * area2 / max(edgeLengths, vec3(1e-5));
    // Preserve the inputs of any neighboring patch, including T-junctions and
    // transitions to a different material. Fragment relief crosses these edges.
    float edge = min(sideDistance.x, min(sideDistance.y, sideDistance.z));
    float fade = (1.0 - smoothstep(200.0, 390.0, distance(phongPosition, u_VoronoiViewOrigin)))
                 * smoothstep(0.0, 5.0, edge);
    if (fade <= 0.0)
        return vec3(0.0);
    float h = textureLod(u_VoronoiHeight, voronoiUV, 2.0).r;
    return phongNormal * (-u_PhongParams.z * (1.0 - h) * fade);
}
#else
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
#endif

void main()
{
    vec3 b = gl_TessCoord;
#define V(type, name) name = phongPatch_##name[0] * b.x + phongPatch_##name[1] * b.y + phongPatch_##name[2] * b.z;
    PHONG_VARYINGS
#undef V
#if defined(USE_VORONOI_FRACTURING)
    vec3 delta = VoronoiOffset();
#else
    vec3 delta = PhongOffset(phongPosition,
        phongPatch_phongPosition[0], phongPatch_phongPosition[1], phongPatch_phongPosition[2],
        phongPatch_phongNormal[0], phongPatch_phongNormal[1], phongPatch_phongNormal[2]);
#endif
    // Preserve the ordinary VS depth/clip convention and displace in world space.
    gl_Position = gl_in[0].gl_Position * b.x + gl_in[1].gl_Position * b.y + gl_in[2].gl_Position * b.z
                + u_PhongViewProjection * vec4(delta, 0.0);
#if (defined(PHONG_LIGHTALL) || defined(PHONG_GENERIC)) && !defined(USE_VORONOI_FRACTURING)
    var_PhongDisplacement = length(delta) / max(u_PhongParams.z, 0.000001);
#endif
#if defined(PHONG_LIGHTALL)
    // Stone lighting samples the BSP surface shared with the baked lightmaps
    // and shadow casters. Sampling behind the caster can self-shadow the relief.
#if !defined(USE_VORONOI_FRACTURING)
#if defined(PER_PIXEL_LIGHTING)
    var_ViewDir.xyz -= delta;
#else
    var_Position += delta;
#endif
#endif
#elif defined(PHONG_FOG) || (defined(PHONG_GENERIC) && defined(USE_FOG))
    var_WSPosition += delta;
#elif defined(PHONG_VELOCITY) && !defined(USE_DEPTH_ONLY)
    var_Position = gl_Position;
#if defined(USE_VORONOI_FRACTURING)
    vec3 previousDelta = delta; // The BSP surface is static.
#else
    vec3 previousDelta = PhongOffset(phongPreviousPosition,
        phongPatch_phongPreviousPosition[0], phongPatch_phongPreviousPosition[1], phongPatch_phongPreviousPosition[2],
        phongPatch_phongPreviousNormal[0], phongPatch_phongPreviousNormal[1], phongPatch_phongPreviousNormal[2]);
#endif
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
