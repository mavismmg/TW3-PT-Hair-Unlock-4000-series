#pragma once
#include <bit>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace witcher_dots::runtime_policy {
// Retention remains bounded: releasing private-vtable objects needs separate
// reclamation proof for concurrent forwarders and GPU submissions. The former
// 256-list ceiling was reached during normal hair toggles on Steam 5.00c.
inline constexpr size_t kMaxLists=2048, kListSlots=8192, kPublicationSlots=131072;
inline constexpr size_t kTrackedMethods=14;
inline constexpr size_t kMaxQueues=32;
// Failure on an unrelated overlay list is local. An unfenced *hair*
// submission still stops conversions; increasing capacity is not that proof.
inline constexpr uint64_t kSweepIntervalMs=250;
inline constexpr bool SweepDue(uint64_t now,uint64_t last,bool pressure=false) noexcept {
    return pressure||!last||now-last>=kSweepIntervalMs;
}
inline constexpr bool FitsBudget(uint64_t used,uint64_t needed,uint64_t budget) noexcept {
    return used<=budget&&needed&&needed<=budget-used;
}
inline constexpr uint64_t kMiB=1024ull*1024;
// Bounded extra transition buffers, only with at least 1 GiB VRAM reserve.
// Unknown budget data never expands caps.
inline constexpr uint64_t GeometryLimit(uint64_t base,uint64_t localBudget,uint64_t usage,uint64_t allocatedSinceSample,uint64_t needed) noexcept {
    if(localBudget<usage||allocatedSinceSample>localBudget-usage
        ||needed>localBudget-usage-allocatedSinceSample
        ||localBudget-usage-allocatedSinceSample-needed<1024*kMiB)return base;
    return std::max(base,std::min<uint64_t>(1024*kMiB,localBudget/12));
}
inline constexpr uint64_t AsLimit(uint64_t localBudget) noexcept {
    return std::max<uint64_t>(1024*kMiB,std::min<uint64_t>(4096*kMiB,localBudget/4));
}
// Per-operation reference accounting, not a cache of game-owned pointers.
// All keys are interfaces retained by the runtime while its mutex is held.
class ReferenceCounts {
    struct Entry {const void* key{};unsigned count{};};
    std::array<Entry,512> entries_{};
    static size_t Hash(const void* key) noexcept {return (reinterpret_cast<uintptr_t>(key)>>4)*0x9E3779B97F4A7C15ull>>55;}
public:
    void Add(const void* key) noexcept {
        if(!key)return;
        for(size_t n=0,i=Hash(key);n<entries_.size();++n,i=(i+1)&511)
            if(!entries_[i].key||entries_[i].key==key) {entries_[i].key=key;++entries_[i].count;return;}
    }
    unsigned Get(const void* key) const noexcept {
        if(!key)return 0;
        for(size_t n=0,i=Hash(key);n<entries_.size();++n,i=(i+1)&511) {
            if(entries_[i].key==key)return entries_[i].count;
            if(!entries_[i].key)return 0;
        }
        return 0;
    }
    void Remove(const void* key) noexcept {
        if(!key)return;
        for(size_t n=0,i=Hash(key);n<entries_.size();++n,i=(i+1)&511) {
            if(entries_[i].key==key) {if(entries_[i].count)--entries_[i].count;return;}
            if(!entries_[i].key)return;
        }
    }
};
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
