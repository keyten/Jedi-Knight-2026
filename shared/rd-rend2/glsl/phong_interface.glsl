/*[Vertex]*/
// Shared interface of the post-skinning Phong stages. Keep the ordinary
// material varyings: alpha tests, lighting, fog and velocity use the same mesh.
#if defined(PHONG_LIGHTALL)
#if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
#define PER_PIXEL_LIGHTING
#endif
#define PHONG_BASE V(vec4, var_TexCoords) V(vec4, var_Color) V(float, var_LeafFlutter)
#if defined(PER_PIXEL_LIGHTING)
#define PHONG_MATERIAL V(vec4, var_Normal) V(vec4, var_Tangent) V(vec4, var_ViewDir) V(vec4, var_LightDir)
#else
#define PHONG_MATERIAL V(vec3, var_Position) V(vec3, var_Normal)
#endif
#elif defined(PHONG_GENERIC)
#define PHONG_BASE V(vec2, var_DiffuseTex) V(vec4, var_Color) V(float, var_LeafFlutter)
#if defined(USE_FOG)
#define PHONG_MATERIAL V(vec3, var_WSPosition)
#endif
#elif defined(PHONG_FOG)
#define PHONG_BASE V(vec3, var_WSPosition)
#if defined(USE_ALPHA_TEST)
#define PHONG_MATERIAL V(vec2, var_TexCoords)
#endif
#elif defined(PHONG_VELOCITY)
#define PHONG_BASE V(vec4, var_Color)
#if !defined(USE_DEPTH_ONLY)
#define PHONG_MOTION V(vec4, var_Position) V(vec4, var_prevPosition) V(vec3, phongPreviousPosition) V(vec3, phongPreviousNormal)
#endif
#if defined(USE_ALPHA_TEST)
#define PHONG_MATERIAL V(vec2, var_TexCoords)
#if defined(USE_PARALLAXMAP)
#define PHONG_PARALLAX V(vec3, var_TangentViewDir)
#endif
#endif
#elif defined(PHONG_REFRACTION)
#define PHONG_BASE V(vec2, var_DiffuseTex) V(vec4, var_Color) V(vec4, var_RefractPosR) V(vec4, var_RefractPosG) V(vec4, var_RefractPosB)
#endif
#ifndef PHONG_MATERIAL
#define PHONG_MATERIAL
#endif
#ifndef PHONG_MOTION
#define PHONG_MOTION
#endif
#ifndef PHONG_PARALLAX
#define PHONG_PARALLAX
#endif
#define PHONG_VARYINGS PHONG_BASE PHONG_MATERIAL PHONG_MOTION PHONG_PARALLAX V(vec3, phongPosition) V(vec3, phongNormal)

#if defined(PHONG_CONTROL)
#define V(type, name) in type phongVertex_##name[]; out type phongPatch_##name[];
#else
#define V(type, name) in type phongPatch_##name[]; out type name;
#endif
PHONG_VARYINGS
#undef V
