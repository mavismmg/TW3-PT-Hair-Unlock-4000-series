#include "geometry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
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
