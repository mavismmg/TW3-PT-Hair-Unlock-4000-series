#pragma once
#include <Windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include "cpu_profile.h"
namespace witcher_dots {
inline bool ReadableProtection(DWORD protection) noexcept {
    if(protection&(PAGE_NOACCESS|PAGE_GUARD))return false;
    const DWORD access=protection&0xff;
    return access==PAGE_READONLY||access==PAGE_READWRITE||access==PAGE_WRITECOPY
        ||access==PAGE_EXECUTE_READ||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY;
}
inline bool Readable(const void* p,size_t size) noexcept {
    const auto start=reinterpret_cast<uintptr_t>(p);
    if(!start||size>UINTPTR_MAX-start)return false;
    uintptr_t at=start;const uintptr_t end=start+size;
    while(at<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!cpu_profile::QueryMemory(reinterpret_cast<void*>(at),&m,sizeof(m))||m.State!=MEM_COMMIT
            ||!ReadableProtection(m.Protect))return false;
        const uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(next<=at)return false;at=std::min(next,end);
    }
    return true;
}
enum class PageReadStatus {Unknown,Readable,Rejected};
inline PageReadStatus CurrentPageStatus(const PSAPI_WORKING_SET_EX_BLOCK& page) noexcept {
    if(page.Invalid.Bad)return PageReadStatus::Rejected;
    // Protection bits are undefined when Valid is zero. Never authorize a
    // nonresident/unmapped page from them: ask VirtualQuery instead.
    if(!page.Valid)return PageReadStatus::Unknown;
    return ReadableProtection(static_cast<DWORD>(page.Win32Protection))
        ?PageReadStatus::Readable:PageReadStatus::Rejected;
}
// No address/protection cache: query every covered page on this invocation.
// Small resident ranges can avoid an expensive heap-region VirtualQuery walk.
// Missing working-set information is not a failure or permission to read:
// retain the complete committed/protection/guard validation as fallback.
template<class Query> bool ReadableCurrentPagesWith(const void* p,size_t size,Query query) noexcept {
    const auto start=reinterpret_cast<uintptr_t>(p);
    if(!start||!size||size>UINTPTR_MAX-start)return false;
    static const size_t pageSize=[] {SYSTEM_INFO info{};GetSystemInfo(&info);return size_t(info.dwPageSize);}();
    if(!pageSize)return Readable(p,size);
    const uintptr_t first=start-start%pageSize,last=(start+size-1)-(start+size-1)%pageSize;
    const size_t count=(last-first)/pageSize+1;
    std::array<PSAPI_WORKING_SET_EX_INFORMATION,8> pages{};
    if(count>pages.size())return Readable(p,size);
    for(size_t i=0;i<count;++i)pages[i].VirtualAddress=reinterpret_cast<void*>(first+i*pageSize);
    bool queried{};
    {cpu_profile::Timer timer(cpu_profile::Part::ResidentQuery);
        queried=query(pages.data(),static_cast<DWORD>(count*sizeof(pages[0])))!=FALSE;}
    if(!queried)return Readable(p,size);
    bool unknown{};
    for(size_t i=0;i<count;++i) {
        const auto status=CurrentPageStatus(pages[i].VirtualAttributes);
        if(status==PageReadStatus::Rejected)return false;
        unknown|=status==PageReadStatus::Unknown;
    }
    return !unknown||Readable(p,size);
}
inline bool ReadableCurrentPages(const void* p,size_t size) noexcept {
    return ReadableCurrentPagesWith(p,size,[](void* pages,DWORD bytes) noexcept {
        return QueryWorkingSetEx(GetCurrentProcess(),pages,bytes);
    });
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
// All pages are checked initially, including guard/protection checks. Each
// actual copy still catches faults if memory changes after that observation.
class ScopedReadRange {
    uintptr_t start_{};size_t size_{};
    bool Store(const void* base,size_t size,bool currentPages) noexcept {
        start_=0;size_=0;
        if(!size||!(currentPages?ReadableCurrentPages(base,size):Readable(base,size)))return false;
        start_=reinterpret_cast<uintptr_t>(base);size_=size;return true;
    }
public:
    ScopedReadRange()=default;
    ScopedReadRange(const ScopedReadRange&)=delete;
    ScopedReadRange& operator=(const ScopedReadRange&)=delete;
    bool Validate(const void* base,size_t size) noexcept {
        return Store(base,size,false);
    }
    bool ValidateCurrentPages(const void* base,size_t size) noexcept {
        return Store(base,size,true);
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
