// Dependency-free policy shared by the renderer and its CPU tests.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Rend2Md3 {
constexpr int MaxInstances = 32;
inline bool ConstantInstanceAttribute(int divisor) {
    // MD3's white colour / zero UV records use a large divisor. Every member
    // in this bounded group still reads element zero, exactly as a single draw.
    return divisor == 0 || divisor >= MaxInstances;
}
inline bool UniformPositiveTransform(const float axis[3][3], const float scale[3])
{
    for (int i = 0; i < 3; ++i) if (!std::isfinite(scale[i])) return false;
    const bool implicit = scale[0] == 0 && scale[1] == 0 && scale[2] == 0;
    if (!implicit && (!(scale[0] > 0) || !std::isfinite(scale[0]) ||
        std::fabs(scale[1] - scale[0]) > scale[0] * 1e-5f ||
        std::fabs(scale[2] - scale[0]) > scale[0] * 1e-5f)) return false;
    float lengths[3] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(axis[i][j])) return false;
            lengths[i] += axis[i][j] * axis[i][j];
        }
    if (!(lengths[0] > 1e-10f) || !std::isfinite(lengths[0])) return false;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(lengths[i] - lengths[0]) > lengths[0] * 1e-4f) return false;
        for (int j = i + 1; j < 3; ++j) {
            float dot = 0;
            for (int k = 0; k < 3; ++k) dot += axis[i][k] * axis[j][k];
            if (std::fabs(dot) > lengths[0] * 1e-4f) return false;
        }
    }
    const float determinant = axis[0][0] * (axis[1][1]*axis[2][2]-axis[1][2]*axis[2][1])
        - axis[0][1] * (axis[1][0]*axis[2][2]-axis[1][2]*axis[2][0])
        + axis[0][2] * (axis[1][0]*axis[2][1]-axis[1][1]*axis[2][0]);
    return determinant > 0 && std::isfinite(determinant);
}
struct Eligibility {
    bool rigidSingleFrame, ordinaryRenderFx, uniformTransform, opaqueSingleStage;
    bool stableGeometry, ordinaryMaterial, supportedView, supportedProgram;
};
inline bool Eligible(const Eligibility& e) {
    return e.rigidSingleFrame && e.ordinaryRenderFx && e.uniformTransform &&
        e.opaqueSingleStage && e.stableGeometry && e.ordinaryMaterial &&
        e.supportedView && e.supportedProgram;
}
// Only consecutive compatible runs may merge. This preserves opaque coplanar
// tie ordering, and splits capacity overflow without dropping any member.
class Planner {
public:
    struct Group { std::vector<int> members; uint64_t hash; };
    std::vector<Group> groups;
    template<class Equal> void Add(int index, uint64_t hash, Equal equal) {
        if (!groups.empty()) {
            auto& group = groups.back();
            auto& members = group.members;
            if (group.hash == hash && members.back() + 1 == index &&
                members.size() < MaxInstances && equal(members.front(), index)) {
                members.push_back(index); return;
            }
        }
        groups.push_back(Group{{index}, hash});
    }
};
inline bool HasUploadSpace(size_t offset, size_t capacity, size_t bytes, size_t alignment) {
    if (!alignment || (alignment & (alignment - 1))) return false;
    if (bytes > SIZE_MAX - (alignment - 1)) return false;
    const size_t padded = (bytes + alignment - 1) & ~(alignment - 1);
    return offset <= capacity && padded <= capacity - offset;
}
}
