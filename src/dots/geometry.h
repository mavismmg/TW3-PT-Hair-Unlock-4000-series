#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace witcher_dots {
struct Vec3 { float x{}, y{}, z{}; };
struct StrandVertex { Vec3 position; float radius{}; };
constexpr uint32_t kTrianglesPerSegment = 4;
inline constexpr bool kIndexedGeometry=
#if WITCHER_DOTS_INDEXED_GEOMETRY
    true;
#else
    false;
#endif
constexpr uint32_t kVerticesPerSegment = kIndexedGeometry?8:12;
constexpr uint64_t kBytesPerSegment = kVerticesPerSegment*sizeof(Vec3);
// Preserve primitive order, winding and barycentrics of the approved layout.
inline constexpr std::array<uint32_t,12> kSegmentIndices{0,1,2,0,3,1,4,5,6,4,7,5};
constexpr uint32_t SegmentIndex(uint32_t segment,uint32_t corner) noexcept {
    return segment*8+kSegmentIndices[corner];
}
constexpr uint64_t kGeometryBudget = 512ull * 1024 * 1024;
constexpr uint32_t kMaxSegments = 65535u * 64u;
struct Plan {
    uint32_t segments{}, vertices{}, groups{};
    uint64_t bytes{};
    explicit operator bool() const noexcept { return segments != 0; }
};
// Bound DXR primitive count, dispatch dimensions and all buffer arithmetic.
Plan MakePlan(uint64_t segments, uint64_t sourceVertices, uint64_t sourceIndices,
    uint64_t budget = kGeometryBudget) noexcept;
std::array<Vec3, 12> Tessellate(StrandVertex p, StrandVertex q) noexcept;
std::array<Vec3, 8> TessellateIndexed(StrandVertex p, StrandVertex q) noexcept;
float StrandU(uint32_t triangle, float baryX, float baryY) noexcept;
uint64_t SourceVertex(uint32_t triangle, uint32_t segmentsPerStrand) noexcept;
// Reconstruct a rounded tapered strand normal from the ray. Finite fallback
// covers parallel rays, missed cones, collapsed strands and extreme inputs.
Vec3 RoundedNormal(StrandVertex p, StrandVertex q, Vec3 origin, Vec3 direction,
    Vec3 fallback) noexcept;
}
