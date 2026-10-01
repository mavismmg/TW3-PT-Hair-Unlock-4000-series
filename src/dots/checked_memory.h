#pragma once
#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "cpu_profile.h"
namespace witcher_dots {
inline bool Readable(const void* p,size_t size) noexcept {
    const auto start=reinterpret_cast<uintptr_t>(p);
    if(!start||size>UINTPTR_MAX-start)return false;
    uintptr_t at=start;const uintptr_t end=start+size;
    while(at<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!cpu_profile::QueryMemory(reinterpret_cast<void*>(at),&m,sizeof(m))||m.State!=MEM_COMMIT
            ||(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        const DWORD access=m.Protect&0xff;
        if(access!=PAGE_READONLY&&access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY
            &&access!=PAGE_EXECUTE_READ&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return false;
        const uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(next<=at)return false;at=std::min(next,end);
    }
    return true;
}
// Invocation-local adaptation of dashdogy / Michael Robles' MIT-licensed
// v1.4.1 guarded-read optimization. This primitive grants no memory proof:
// callers must either query the range or have a current ScopedReadRange.
inline bool CopyGuarded(void* dst,const void* src,size_t size) noexcept {
    const auto from=reinterpret_cast<uintptr_t>(src),to=reinterpret_cast<uintptr_t>(dst);
    if(!from||!to||size>UINTPTR_MAX-from||size>UINTPTR_MAX-to)return false;
    __try { std::memcpy(dst,src,size);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool CopyChecked(void* dst,const void* src,size_t size) noexcept {
    return Readable(src,size)&&CopyGuarded(dst,src,size);
}
// A bounded proof for one builder invocation, not an address-space cache.
// All pages are queried initially, including guard/protection checks. Each
// actual copy still catches faults if memory changes after that observation.
class ScopedReadRange {
    uintptr_t start_{};size_t size_{};
public:
    ScopedReadRange()=default;
    ScopedReadRange(const ScopedReadRange&)=delete;
    ScopedReadRange& operator=(const ScopedReadRange&)=delete;
    bool Validate(const void* base,size_t size) noexcept {
        start_=0;size_=0;
        if(!size||!Readable(base,size))return false;
        start_=reinterpret_cast<uintptr_t>(base);size_=size;return true;
    }
    bool Contains(const void* base,size_t offset,size_t bytes) const noexcept {
        return start_&&reinterpret_cast<uintptr_t>(base)==start_
            &&offset<=size_&&bytes<=size_-offset;
    }
    template<class T> bool Read(const void* base,size_t offset,T& out) const noexcept {
        return Contains(base,offset,sizeof(T))
            &&CopyGuarded(&out,reinterpret_cast<const void*>(start_+offset),sizeof(T));
    }
};
template<class T> bool Read(const void* base,size_t offset,T& out) noexcept {
    const auto address=reinterpret_cast<uintptr_t>(base);
    return offset<=UINTPTR_MAX-address&&CopyChecked(&out,reinterpret_cast<void*>(address+offset),sizeof(T));
}
}
