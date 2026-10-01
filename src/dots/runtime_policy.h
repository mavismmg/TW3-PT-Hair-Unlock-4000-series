#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
namespace witcher_dots::runtime_policy {
// Retention remains bounded: releasing private-vtable objects needs separate
// reclamation proof for concurrent forwarders and GPU submissions. The former
// 256-list ceiling was reached during normal hair toggles on Steam 5.00c.
inline constexpr size_t kMaxLists=2048, kListSlots=8192, kPublicationSlots=131072;
inline constexpr size_t kTrackedMethods=14;
static_assert(std::has_single_bit(kListSlots)&&std::has_single_bit(kPublicationSlots));
// Reserve room for image slots and two private table generations per list.
static_assert(kPublicationSlots>=4*kMaxLists*kTrackedMethods+256);
static_assert(kListSlots>=4*kMaxLists);
template<size_t Slots> constexpr size_t PointerHash(uintptr_t value,unsigned alignmentShift) noexcept {
    static_assert(std::has_single_bit(Slots)&&Slots>1);
    return static_cast<size_t>(((value>>alignmentShift)*0x9E3779B97F4A7C15ull)>>(64-std::countr_zero(Slots)));
}
inline constexpr bool CanTrack(size_t count) noexcept {return count<kMaxLists;}
}
