/*[Compute]*/
// Compact one index per surviving plant. Every bin reserves numSprites slots,
// so even the all-visible/all-one-LOD case cannot overflow a list. The CPU
// reserves all six lists atomically, or submits the complete legacy surface.
layout(local_size_x = 64) in;
struct Sprite { vec4 position; vec4 normal; vec4 color; vec4 dimensions; };
struct DrawCommand { uint count; uint instanceCount; uint first; uint baseVertex; uint baseInstance; };
layout(std430, binding = 0) readonly buffer SpriteData { Sprite sprites[]; };
layout(std430, binding = 1) writeonly buffer SpriteLists { uint indices[]; };
layout(std430, binding = 2) buffer SpriteCommands { DrawCommand commands[]; };

uniform vec4 u_SpriteCullPlanes[6]; // inward, normalized; zero disables a plane
uniform vec4 u_SpriteCullRange; // first plant, plant count, output base, command base
uniform vec4 u_SpriteCullLod; // strip distance, segments, cards, third-card fade end
uniform vec4 u_SpriteCullShape; // width factor, extra height, fade width scale, fade end
uniform vec3 u_SpriteViewOrigin; // visible camera, also for light views

void main()
{
    uint id = gl_GlobalInvocationID.x;
    uint count = uint(u_SpriteCullRange.y);
    if (id >= count) return;
    uint spriteIndex = uint(u_SpriteCullRange.x) + id;
    Sprite sp = sprites[spriteIndex];
    float distanceToCamera = length(sp.position.xyz - u_SpriteViewOrigin);
    // Retain the rounding band of smoothstep near its upper endpoint.
    if (distanceToCamera >= u_SpriteCullShape.w) return;

    // FoliageBladeBend integrates a curve whose arc length is exactly the rest
    // stem length, for arbitrary wind and current/previous interaction fields.
    // A camera-facing stem also includes rotated(height) - original(height).
    // Flattened sprites can use height as their horizontal width. The last
    // unit covers matrix/raster rounding (including snapped sun projections).
    float radius = length(sp.dimensions.yzw) + abs(sp.dimensions.y) * u_SpriteCullShape.y
        + abs(sp.dimensions.x) * (1.0 + abs(u_SpriteCullShape.z)) * u_SpriteCullShape.x + 1.0;
    for (int p = 0; p < 6; ++p)
        if (dot(u_SpriteCullPlanes[p], vec4(sp.position.xyz, 1.0)) < -radius) return;

    uint cards = uint(u_SpriteCullLod.z);
    if (cards == 3u && u_SpriteCullLod.w > 0.0 && distanceToCamera >= u_SpriteCullLod.w)
        cards = 2u;
    bool strip = u_SpriteCullLod.y > 0.5 &&
        max(distanceToCamera - radius, 0.0) < u_SpriteCullLod.x;
    uint bin = (strip ? 3u : 0u) + cards - 1u;
    uint command = uint(u_SpriteCullRange.w) + bin;
    uint slot = atomicAdd(commands[command].instanceCount, cards) / cards;
    // slot < count by construction: each of count invocations emits at most
    // one record. Keep the guard so a malformed dispatch cannot write past it.
    if (slot < count)
        indices[uint(u_SpriteCullRange.z) + bin * count + slot] = spriteIndex;
}
