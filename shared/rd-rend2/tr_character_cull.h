// Render-only approximation for distant offscreen character bodies.
// Independent of engine/GL types so the actual policy can be tested on the CPU.
#pragma once
#include <algorithm>
#include <cmath>

namespace CharacterCull {
inline bool RenderHistoryStale(int lastRenderedFrame, int frame, bool enabled) {
    return enabled && lastRenderedFrame >= 0 && lastRenderedFrame != frame && lastRenderedFrame != frame - 1;
}
struct View {
    float origin[3];
    float axis[3][3]; // forward, left, up
    float fovX, fovY;
};
struct Body {
    float origin[3];
    float radius;
    bool character;
    bool protectedBody;
};
inline float DistanceSquared(const float *a, const float *b) {
    float d = 0.0f;
    for (int i = 0; i < 3; ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}
inline bool OutsideView(const Body& body, const View& view) {
    if (!(view.fovX > 0.0f && view.fovX < 179.0f &&
          view.fovY > 0.0f && view.fovY < 179.0f)) return false;
    float p[3] = {};
    for (int a = 0; a < 3; ++a)
        for (int i = 0; i < 3; ++i)
            p[a] += (body.origin[i] - view.origin[i]) * view.axis[a][i];
    // Extra silhouette/shadow guard; test normalized side planes, not an AABB
    // in projected coordinates (which clips spheres at wide FOVs).
    const float radius = body.radius + 32.0f;
    if (p[0] < -radius) return true;
    const float radians = 0.008726646259971648f;
    for (int a = 1; a < 3; ++a) {
        const float half = std::min(178.0f, (a == 1 ? view.fovX : view.fovY) + 10.0f) * radians;
        if (std::fabs(p[a]) * std::cos(half) - p[0] * std::sin(half) > radius) return true;
    }
    return false;
}
inline bool Cull(const Body& body, const View& view, const float *playerOrigin,
                 float keepDistance, bool enabled) {
    if (!enabled || !body.character || body.protectedBody ||
        !(keepDistance > 0.0f) || !std::isfinite(keepDistance) ||
        !(body.radius > 0.0f) || !std::isfinite(body.radius)) return false;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(body.origin[i]) || !std::isfinite(view.origin[i]) ||
            !std::isfinite(playerOrigin[i])) return false;
        for (int a = 0; a < 3; ++a)
            if (!std::isfinite(view.axis[a][i])) return false;
    }
    // Large/scaled bodies and elevated casters retain more shadow reach. The
    // camera ALSO gets the keep zone (third-person, spectator or remote view).
    const float sizeReach = 4.0f * std::max(0.0f, body.radius - 64.0f);
    const float elevation = std::max(std::fabs(body.origin[2] - playerOrigin[2]),
                                     std::fabs(body.origin[2] - view.origin[2]));
    const float keep = keepDistance + sizeReach + 2.0f * std::max(0.0f, elevation - 96.0f);
    if (DistanceSquared(body.origin, playerOrigin) <= keep * keep ||
        DistanceSquared(body.origin, view.origin) <= keep * keep) return false;
    return OutsideView(body, view);
}
} // namespace CharacterCull
