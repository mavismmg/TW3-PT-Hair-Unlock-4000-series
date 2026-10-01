#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace witcher_dots {
// One operation, no allocations or waits. Completed fence values are monotonic
// on our exclusively signalled queue fences: an older snapshot may delay reuse
// until the next operation but cannot admit unfinished work. UINT64_MAX (device
// removal) remains invalid in Available/ReclaimFinished. Never share snapshots
// between operations or submit using cached completion data.
template<size_t Capacity> class FenceSamples {
    struct Entry {const void* queue{};uint64_t completed{};};
    std::array<Entry,Capacity> entries_{};
    size_t count_{};
public:
    template<class Query> uint64_t Get(const void* queue,Query&& query) {
        for(size_t i=0;i<count_;++i)if(entries_[i].queue==queue)return entries_[i].completed;
        const uint64_t complete=query();
        if(count_<Capacity)entries_[count_++]={queue,complete};
        return complete;
    }
};
}
