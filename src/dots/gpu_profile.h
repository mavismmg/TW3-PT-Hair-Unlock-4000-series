#pragma once
#include <atomic>
#include <cstdint>
namespace witcher_dots::gpu_profile {
// Session-only diagnostic. No queries, readbacks or waits when disabled.
inline std::atomic<uint64_t> epoch{};
inline bool Enabled() noexcept {return (epoch.load(std::memory_order_acquire)&1)!=0;}
inline void SetEnabled(bool enabled) noexcept {
    auto value=epoch.load(std::memory_order_relaxed);
    while((value&1)!=unsigned(enabled)&&!epoch.compare_exchange_weak(value,value+1,std::memory_order_release,std::memory_order_relaxed)) {}
}
inline bool Valid(const uint64_t* ticks,uint64_t hz) noexcept {
    return hz&&ticks[0]<=ticks[1]&&ticks[1]<=ticks[2]&&ticks[2]<=ticks[3];
}
}
