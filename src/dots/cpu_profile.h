#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
namespace witcher_dots::cpu_profile {
enum class Part : size_t { Prebuild, Build, Instances, Input, ResourceUnwrap, DeviceIdentity,
    BuildLockWait, TableValidation, Recording, InstanceLockWait, Count };
inline constexpr size_t kCount=static_cast<size_t>(Part::Count);
inline constexpr std::array<const char*,kCount> kLabels{
    "Prebuild total", "Build total", "TLAS preparation total", "Input validation (nested)",
    "Resource unwrap (nested)", "Resource device identity (nested)", "Build mutex wait (nested)",
    "List validation (nested)", "Driver command recording (nested)", "TLAS mutex wait (nested)"};
struct Value {uint64_t ticks{},calls{};};
using Sample=std::array<Value,kCount>;
struct Counters {std::atomic<uint64_t> ticks{},calls{};};
inline std::array<Counters,kCount> counters{};
inline uint64_t Frequency() noexcept {
    static const uint64_t hz=[] {LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)&&f.QuadPart>0?static_cast<uint64_t>(f.QuadPart):0ull;}();
    return hz;
}
inline Sample Read() noexcept {
    Sample result{};
    for(size_t i=0;i<kCount;++i)result[i]={counters[i].ticks.load(std::memory_order_relaxed),counters[i].calls.load(std::memory_order_relaxed)};
    return result;
}
class Timer {
    Part part_;LARGE_INTEGER start_{};bool stopped_{};
public:
    explicit Timer(Part part) noexcept : part_(part) {QueryPerformanceCounter(&start_);}
    Timer(const Timer&)=delete;
    void Stop() noexcept {
        if(stopped_)return;stopped_=true;
        LARGE_INTEGER end{};QueryPerformanceCounter(&end);
        auto& c=counters[static_cast<size_t>(part_)];
        if(end.QuadPart>=start_.QuadPart)c.ticks.fetch_add(static_cast<uint64_t>(end.QuadPart-start_.QuadPart),std::memory_order_relaxed);
        c.calls.fetch_add(1,std::memory_order_relaxed);
    }
    ~Timer() {Stop();}
};
// UI-only window, serialized by State::lock. Each row is aggregate elapsed
// time, including waits across threads; nested rows must not be added together.
struct Window {
    Sample previous{};uint64_t last{};bool known{};
    std::array<double,kCount> msPerSecond{},callsPerSecond{};
    void Update(uint64_t now,const Sample& sample,uint64_t hz) noexcept {
        if(!last||now<last||now-last>5000||!hz) {
            previous=sample;last=now;known=false;msPerSecond={};callsPerSecond={};return;
        }
        const uint64_t elapsed=now-last;if(elapsed<1000)return;
        for(size_t i=0;i<kCount;++i) {
            const auto dt=sample[i].ticks>=previous[i].ticks?sample[i].ticks-previous[i].ticks:0;
            const auto dc=sample[i].calls>=previous[i].calls?sample[i].calls-previous[i].calls:0;
            msPerSecond[i]=static_cast<double>(dt)/hz*1000000.0/elapsed;
            callsPerSecond[i]=static_cast<double>(dc)*1000.0/elapsed;
        }
        previous=sample;last=now;known=true;
    }
};
}
