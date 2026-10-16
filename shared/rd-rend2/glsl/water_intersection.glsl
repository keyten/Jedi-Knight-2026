/*[Compute]*/
// GL 4.3 object intersection source of the shallow-water interaction field
// (r_waterIntersectionFoam, tr_watersurface.cpp RB_WaterIntersectionCompute).
// The same test as the GL 3.2 CPU path (RB_WaterIntersectionInject), but on
// the depth copy itself: no readback. One invocation per simulation cell of
// one body: the cell's water point is projected into the opaque depth copy;
// only a narrow, locally coherent, near-vertical contact survives.
//
// Output: the contact source of every cell (0 = none) and the body's largest
// source in the hit buffer (non-negative float bits order like uints), which
// the CPU reads back without waiting to wake a sleeping body.
layout(local_size_x = 8, local_size_y = 8) in;

uniform sampler2D u_WaterDepthMap;			// opaque depth copy (waterCopyFbo)
uniform sampler2D u_WaterInteractionState;	// body state: alpha < 0 = dry cell
uniform mat4 u_ModelViewProjectionMatrix;	// view projection of the depth copy
// 0 body min xy, texel xy | 1 plane normal, dist | 2 copy rect x, y, w, h |
// 3 viewport x, y, w, h | 4 target w, h, band, strength | 5 P[0], P[5], P[8], P[9] |
// 6 P[10], P[14], hit slot, unused | 7 view origin | 8 forward | 9 left | 10 up
uniform vec4 u_WaterIntersection[11];

layout(binding = 0, r16f) writeonly uniform image2D u_WaterIntersectionOut;
layout(std430, binding = 0) buffer WaterIntersectionHits
{
	uint hits[];
};

float SceneDepth(ivec2 p)
{
	return texelFetch(u_WaterDepthMap, p, 0).r;
}

vec3 WorldPosition(ivec2 p, float depth)
{
	vec2 target = u_WaterIntersection[4].xy;
	vec4 viewport = u_WaterIntersection[3] / target.xyxy;
	vec2 uv = (vec2(p) + 0.5) / target;
	vec2 ndc = (uv - viewport.xy) / viewport.zw * 2.0 - 1.0;
	float z = u_WaterIntersection[6].y / (depth * 2.0 - 1.0 + u_WaterIntersection[6].x);
	float side = (ndc.x + u_WaterIntersection[5].z) * z / u_WaterIntersection[5].x;
	float up = (ndc.y + u_WaterIntersection[5].w) * z / u_WaterIntersection[5].y;
	return u_WaterIntersection[7].xyz - side * u_WaterIntersection[9].xyz +
		up * u_WaterIntersection[10].xyz + z * u_WaterIntersection[8].xyz;
}

float ContactSource(ivec2 cell)
{
	if (texelFetch(u_WaterInteractionState, cell, 0).a < 0.0)
		return 0.0;
	vec3 n = u_WaterIntersection[1].xyz;
	float planeDist = u_WaterIntersection[1].w;
	vec3 waterPoint;
	waterPoint.xy = u_WaterIntersection[0].xy + (vec2(cell) + 0.5) * u_WaterIntersection[0].zw;
	waterPoint.z = (planeDist - n.x * waterPoint.x - n.y * waterPoint.y) / n.z;
	vec4 clip = u_ModelViewProjectionMatrix * vec4(waterPoint, 1.0);
	if (clip.w <= 0.001)
		return 0.0;
	vec4 viewport = u_WaterIntersection[3];
	ivec2 s = ivec2(floor(viewport.xy + (clip.xy / clip.w * 0.5 + 0.5) * viewport.zw));
	ivec4 rect = ivec4(u_WaterIntersection[2]);
	if (s.x <= rect.x || s.y <= rect.y || s.x >= rect.x + rect.z - 1 || s.y >= rect.y + rect.w - 1)
		return 0.0;
	float dc = SceneDepth(s);
	if (dc >= 0.999999)
		return 0.0;
	float band = u_WaterIntersection[4].z;
	vec3 scene = WorldPosition(s, dc);
	float signedDistance = dot(scene, n) - planeDist;
	if (abs(signedDistance) >= band)
		return 0.0;
	float dl = SceneDepth(s + ivec2(-1, 0)), dr = SceneDepth(s + ivec2(1, 0));
	float dd = SceneDepth(s + ivec2(0, -1)), du = SceneDepth(s + ivec2(0, 1));
	if (max(max(dl, dr), max(dd, du)) >= 0.999999)
		return 0.0;
	vec3 dx = WorldPosition(s + ivec2(1, 0), dr) - WorldPosition(s + ivec2(-1, 0), dl);
	vec3 dy = WorldPosition(s + ivec2(0, 1), du) - WorldPosition(s + ivec2(0, -1), dd);
	vec3 sceneNormal = cross(dx, dy);
	float normalLength = length(sceneNormal);
	if (normalLength < 1.0e-4)
		return 0.0;
	sceneNormal /= normalLength;
	float vertical = 1.0 - abs(dot(sceneNormal, n));
	float normalConfidence = clamp((vertical - 0.15) / 0.55, 0.0, 1.0);
	float derivative = max(length(dx), length(dy));
	float depthConfidence = 1.0 - clamp(derivative / (band * 8.0), 0.0, 1.0);
	float bandConfidence = 1.0 - clamp(abs(signedDistance) / band, 0.0, 1.0);
	float source = bandConfidence * normalConfidence * depthConfidence * u_WaterIntersection[4].w;
	return source <= 0.01 ? 0.0 : clamp(source, 0.0, 1.0);
}

void main()
{
	ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = textureSize(u_WaterInteractionState, 0);
	if (cell.x >= size.x || cell.y >= size.y)
		return;
	float source = ContactSource(cell);
	imageStore(u_WaterIntersectionOut, cell, vec4(source));
	if (source > 0.0)
		atomicMax(hits[int(u_WaterIntersection[6].z)], floatBitsToUint(source));
}
