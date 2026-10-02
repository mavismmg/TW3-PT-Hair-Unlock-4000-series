#include "geometry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstring>
#include "indexed_converter_source.h"

namespace {
constexpr uint64_t ConverterFingerprint(std::string_view source) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : source) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}
// Freeze the approved converter; indexed mode may change stores, not math.
static_assert(witcher_dots::kConverterHlsl.size() == 2523);
static_assert(ConverterFingerprint(witcher_dots::kConverterHlsl) ==
              0x232f1aac55d74440ull);

bool Finite(witcher_dots::Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
}  // namespace

int main() {
  using namespace witcher_dots;

  const auto plan = MakePlan(64, 65, 64);
  assert(plan);
  assert(plan.segments == 64);
  assert(plan.vertices == 64 * kVerticesPerSegment);
  assert(plan.groups == 1);
  assert(plan.bytes == 64 * kBytesPerSegment);
  assert(!MakePlan(0, 2, 1));
  assert(!MakePlan(1, 1, 1));
  assert(!MakePlan(2, 3, 1));
  assert(!MakePlan(2, 3, 2, kBytesPerSegment));
  assert(!MakePlan(static_cast<uint64_t>(kMaxSegments) + 1, UINT32_MAX,
                   UINT32_MAX));

  const StrandVertex p{{0.0f, 0.0f, 0.0f}, 0.25f};
  const StrandVertex q{{0.0f, 1.0f, 0.0f}, 0.125f};
  const auto triangles = Tessellate(p, q);
  for (const auto vertex : triangles) assert(Finite(vertex));
  assert(triangles[0].x != triangles[4].x ||
         triangles[0].z != triangles[4].z);

  const auto collapsed = Tessellate(
      {{std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, 1.0f}, q);
  for (const auto vertex : collapsed) {
    assert(vertex.x == 0.0f && vertex.y == 0.0f && vertex.z == 0.0f);
  }
  // The indexed stream reconstructs the exact approved triangle list,
  // including winding, primitive IDs and degenerate-segment behavior.
  const auto equivalent=[](StrandVertex a,StrandVertex b) {
    const auto original=Tessellate(a,b);const auto indexed=TessellateIndexed(a,b);
    for(size_t corner=0;corner<12;++corner)
      assert(std::memcmp(&original[corner],&indexed[kSegmentIndices[corner]],sizeof(Vec3))==0);
  };
  for(unsigned i=0;i<4096;++i) {
    const float f=float(i)/37;
    equivalent({{f,-f,f/3},0.01f+f/1000},{{f+1,f/2,1-f},0.005f});
  }
  equivalent(p,p);equivalent({{},-1},q);
  equivalent({{std::numeric_limits<float>::quiet_NaN(),0,0},1},q);
  equivalent({{std::numeric_limits<float>::infinity(),0,0},1},q);
  equivalent({{1e30f,0,0},1e30f},{{-1e30f,0,0},1e30f});
  for(uint32_t s:{0u,64u,kMaxSegments-1})for(uint32_t i=0;i<12;++i)
    assert(SegmentIndex(s,i)>=s*8&&SegmentIndex(s,i)<(s+1)*8);
  const auto source=IndexedConverterSource();assert(!source.empty());
  assert(source.find("j < 8")!=std::string::npos&&source.find("DotsVertices[f+5]")==std::string::npos);
  assert(kConverterHlsl.find("DotsVertices[f+5] = B;")!=std::string::npos);

  assert(StrandU(0, 0.25f, 0.5f) == 0.75f);
  assert(StrandU(1, 0.25f, 0.5f) == 0.5f);
  assert(SourceVertex(0, 4) == 0);
  assert(SourceVertex(4, 4) == 1);
  assert(SourceVertex(16, 4) == 5);

  const auto normal = RoundedNormal(p, q, {1.0f, 0.5f, 0.0f},
                                    {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
  assert(Finite(normal));
  const auto fallback = RoundedNormal(p, p, {}, {}, {0.0f, 0.0f, 2.0f});
  assert(Finite(fallback));
  assert(std::abs(fallback.z - 1.0f) < 0.0001f);
  return 0;
}
