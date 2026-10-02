#pragma once
#include "overlay_slots.h"
#include "dots/checked_memory.h"
#include <wrl/client.h>
#include <array>
namespace single_overlay::native {
inline bool InsideLoader() noexcept {
    using Fn = BOOLEAN (NTAPI*)();
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIsThreadWithinLoaderCallout"));
    return !fn || fn();
}
struct ImageObservation {
    uintptr_t begin{},end{};
    HMODULE image{};
    bool code{};
    bool Contains(HMODULE owner,uintptr_t address,size_t size,bool executable) const noexcept {
        return owner&&owner==image&&executable==code&&address>=begin&&address<=end&&size<=end-address;
    }
    bool Observe(HMODULE owner,const void* value,size_t size,bool executable) noexcept {
        const auto address=reinterpret_cast<uintptr_t>(value);
        if(!owner||!address||!size||size>UINTPTR_MAX-address)return false;
        if(Contains(owner,address,size,executable))return true;
        MEMORY_BASIC_INFORMATION memory{};
        if(witcher_dots::cpu_profile::QueryMemory(value,&memory,sizeof(memory))!=sizeof(memory)
            ||memory.Type!=MEM_IMAGE||memory.State!=MEM_COMMIT||memory.AllocationBase!=owner
            ||(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const bool actualCode=(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
        const auto base=reinterpret_cast<uintptr_t>(memory.BaseAddress);
        if(actualCode!=executable||memory.RegionSize>UINTPTR_MAX-base||address<base||size>base+memory.RegionSize-address)return false;
        begin=base;end=base+memory.RegionSize;image=owner;code=actualCode;return true;
    }
};
inline bool PinInterface(IUnknown* object, size_t last) noexcept {
    if (!object || last > 64) return false;
    void** table{};
    if(!witcher_dots::ReadableCurrentPages(object,sizeof(table))||!witcher_dots::CopyGuarded(&table,object,sizeof(table))
        ||!table||reinterpret_cast<uintptr_t>(table)%alignof(void*))return false;
    HMODULE owner = nullptr;
    ImageObservation data,code;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(table), &owner)
        || (!data.Observe(owner,table,(last+1)*sizeof(void*),false)
            &&(!slots::ImageSlot(owner,table)||!slots::ImageSlot(owner,table+last)
                ||!witcher_dots::ReadableCurrentPages(table,(last+1)*sizeof(void*)))))return false;
    std::array<void*,65> methods{};
    if(!witcher_dots::CopyGuarded(methods.data(),table,(last+1)*sizeof(void*)))return false;
    for (size_t i = 0; i <= last; ++i) {
        HMODULE callable = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(methods[i]), &callable) || !code.Observe(callable,methods[i],1,true)) return false;
    }
    return true;
}
inline bool SystemDxgiObject(IUnknown* object, size_t last) noexcept {
    wchar_t path[MAX_PATH]{};
    if (!object || !GetSystemDirectoryW(path, MAX_PATH) || wcscat_s(path, L"\\dxgi.dll")) return false;
    const HMODULE dxgi = GetModuleHandleW(path);
    auto** table = *reinterpret_cast<void***>(object);
    return slots::ImageSlot(dxgi, table) && slots::ImageSlot(dxgi, table + last) && PinInterface(object, last);
}
// NVIDIA and ReShade's public COM unwrapping contracts. No private object offsets,
// no replacement of an application's interface, and bounded cycle detection.
template<class T> bool Unwrap(IUnknown* input, Microsoft::WRL::ComPtr<T>& output) noexcept {
    using Microsoft::WRL::ComPtr;
    constexpr GUID baseGuid = {0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    // ReShade 6.8.0 source/com_utils.hpp: IID_UnwrappedObject. The returned
    // original owns an AddRef, as with any successful QueryInterface call.
    constexpr GUID reshadeGuid = {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    output.Reset();
    // Prove the first AddRef is callable before taking ownership. Reuse only
    // this invocation's proof, not a raw pointer or table approval across calls.
    if(!PinInterface(input,2))return false;
    ComPtr<IUnknown> current = input;
    std::array<ComPtr<IUnknown>, 4> visited;
    for (size_t i = 0; current && i < visited.size(); ++i) {
        if ((i && !PinInterface(current.Get(), 2)) || FAILED(current.As(&visited[i]))) return false;
        for (size_t j = 0; j < i; ++j) if (visited[j].Get() == visited[i].Get()) return false;
        ComPtr<IUnknown> base;
        HRESULT hr = current->QueryInterface(baseGuid, reinterpret_cast<void**>(base.GetAddressOf()));
        if (hr == E_NOINTERFACE) {
            if (base) return false;
            hr = current->QueryInterface(reshadeGuid, reinterpret_cast<void**>(base.GetAddressOf()));
        }
        if (hr == E_NOINTERFACE && base) return false;
        if (hr == E_NOINTERFACE) return SUCCEEDED(current.As(&output)) && output;
        if (hr != S_OK || !base) return false;
        current = base;
    }
    return false;
}

// ReShade hooks native ID3D12Resource::GetDevice and may return its public
// proxy device even when the resource itself is native. Compare canonical
// native identities, retaining the cheap native-device fast path.
inline bool SameNativeIdentity(IUnknown* object,IUnknown* expected) noexcept {
    if(!object||!expected)return false;
    Microsoft::WRL::ComPtr<IUnknown> identity;
    if(FAILED(object->QueryInterface(IID_PPV_ARGS(&identity))))return false;
    if(identity.Get()==expected)return true;
    Microsoft::WRL::ComPtr<IUnknown> native;
    return Unwrap(object,native)&&SUCCEEDED(native.As(&identity))&&identity.Get()==expected;
}
}
