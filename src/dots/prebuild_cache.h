#pragma once
#include <d3d12.h>
#include <array>
#include <cstdint>
namespace witcher_dots {
// Per-native-device cache; caller serializes access. All admitted descriptors
// have one opaque non-indexed triangle geometry, FLOAT3/stride12, ARRAY layout,
// no transform and BOTTOM_LEVEL type. Only vertex count and flags can vary.
// Keep exact flags (including PERFORM_UPDATE) distinct; never cache failure.
class PrebuildCache {
    struct Entry {uint32_t vertices{},flags{};D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};};
    std::array<Entry,64> entries_{};
    size_t next_{};
public:
    template<class Query> bool Get(uint32_t vertices,uint32_t flags,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& out,bool& hit,Query&& query) {
        out={};hit=false;
        if(!vertices||(flags!=7&&flags!=0x27))return false;
        for(const auto& entry:entries_)if(entry.vertices==vertices&&entry.flags==flags) {out=entry.info;hit=true;return true;}
        query(out);
        if(!out.ResultDataMaxSizeInBytes||!out.ScratchDataSizeInBytes)return false;
        entries_[next_]={vertices,flags,out};next_=(next_+1)%entries_.size();return true;
    }
};
}
