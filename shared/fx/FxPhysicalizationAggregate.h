// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
#ifndef FX_PHYSICALIZATION_AGGREGATE_H
#define FX_PHYSICALIZATION_AGGREGATE_H
#include <algorithm>
#include <cmath>
namespace FxPhysical {
// Exact same support and optical response only. Never bridge gaps, average
// materials, or merge authored/emissive media. T is a renderer evaluation.
template<class T> inline bool ExactEligible(const T& p) {
    if (!p.automaticDensity || !std::isfinite(p.extinction) || p.extinction <= 0 ||
        !std::isfinite(p.radius) || p.radius <= 0 || !std::isfinite(p.inner) ||
        !std::isfinite(p.anisotropy)) return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(p.center[i]) || !std::isfinite(p.invExtent[i]) || p.invExtent[i] <= 0 ||
            !std::isfinite(p.albedo[i]) || p.emission[i] != 0) return false;
    return true;
}
template<class T> inline int ExactKeyCompare(const T& a, const T& b) {
    const float ak[] = {a.center[0], a.center[1], a.center[2], a.invExtent[0], a.invExtent[1], a.invExtent[2],
        a.radius, a.inner, a.anisotropy, a.albedo[0], a.albedo[1], a.albedo[2]};
    const float bk[] = {b.center[0], b.center[1], b.center[2], b.invExtent[0], b.invExtent[1], b.invExtent[2],
        b.radius, b.inner, b.anisotropy, b.albedo[0], b.albedo[1], b.albedo[2]};
    for (unsigned i = 0; i < sizeof(ak)/sizeof(ak[0]); ++i) {
        if (ak[i] < bk[i]) return -1;
        if (ak[i] > bk[i]) return 1;
    }
    return 0;
}
template<class T> inline int CoalesceExact(T* ps, int count) {
    if (count < 2) return count;
    std::stable_sort(ps, ps + count, [](const T& a, const T& b) {
        const bool ae = ExactEligible(a), be = ExactEligible(b);
        if (ae != be) return ae;
        if (ae) { const int key = ExactKeyCompare(a, b); if (key) return key < 0; }
        return a.id < b.id;
    });
    int out = 0;
    for (int i = 0; i < count; ++i) {
        if (out && ExactEligible(ps[out-1]) && ExactEligible(ps[i]) &&
            !ExactKeyCompare(ps[out-1], ps[i])) {
            const float sigma = ps[out-1].extinction + ps[i].extinction;
            if (std::isfinite(sigma) && sigma <= 1e6f) {
                ps[out-1].extinction = sigma;
                ps[out-1].id = std::min(ps[out-1].id, ps[i].id);
                continue;
            }
        }
        if (out != i) ps[out] = ps[i];
        ++out;
    }
    return out;
}
}
#endif
