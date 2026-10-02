// Reproduces GetDevice returning a proxy for a native D3D12 resource, plus
// stacked public wrappers, different owners and cyclic wrappers.
#include <Windows.h>
#include <cassert>

#include "overlay_native.h"

namespace {
constexpr GUID kNvidiaUnwrap{0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
constexpr GUID kReshadeUnwrap{0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
struct DeviceIdentity final : IUnknown {
  ULONG references=1;
  IUnknown* underlying{};
  GUID unwrap{};
  DeviceIdentity(IUnknown* native=nullptr,GUID protocol={}) : underlying(native),unwrap(protocol) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
    if(!output)return E_POINTER;
    *output=nullptr;
    if(iid==__uuidof(IUnknown)) {*output=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    if(underlying&&iid==unwrap) {*output=underlying;underlying->AddRef();return S_OK;}
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
  ULONG STDMETHODCALLTYPE Release() override {assert(references>1);return --references;}
};
}

int main() {
  using single_overlay::native::SameNativeIdentity;
  DeviceIdentity native,other;
  DeviceIdentity reshade(&native,kReshadeUnwrap);
  DeviceIdentity streamline(&reshade,kNvidiaUnwrap);
  DeviceIdentity other_proxy(&other,kReshadeUnwrap);
  DeviceIdentity cycle(nullptr,kReshadeUnwrap);
  cycle.underlying=&cycle;
  // Invocation-local image observations reduce repeated region queries but
  // never approve a private/unreadable replacement table or arbitrary code.
  witcher_dots::cpu_profile::SetEnabled(true);
  const auto queries=witcher_dots::cpu_profile::Read()[static_cast<size_t>(witcher_dots::cpu_profile::Part::MemoryQuery)].calls;
  assert(single_overlay::native::PinInterface(&native,2));
  assert(witcher_dots::cpu_profile::Read()[static_cast<size_t>(witcher_dots::cpu_profile::Part::MemoryQuery)].calls-queries<=3);
  witcher_dots::cpu_profile::SetEnabled(false);
  struct Fake {void** table;};std::array<void*,3> privateTable{};Fake fake{privateTable.data()};
  assert(!single_overlay::native::PinInterface(reinterpret_cast<IUnknown*>(&fake),2));
  fake.table=nullptr;assert(!single_overlay::native::PinInterface(reinterpret_cast<IUnknown*>(&fake),2));
  assert(!single_overlay::native::PinInterface(nullptr,2)&&!single_overlay::native::PinInterface(&native,65));
  // This is why the old ownership check rejected native resources: the
  // public GetDevice return value has the wrapper's IUnknown identity.
  Microsoft::WRL::ComPtr<IUnknown> returned;
  assert(SUCCEEDED(reshade.QueryInterface(IID_PPV_ARGS(&returned))));
  assert(returned.Get()!=static_cast<IUnknown*>(&native));
  returned.Reset();
  assert(SameNativeIdentity(&native,&native));
  assert(SameNativeIdentity(&reshade,&native));
  assert(SameNativeIdentity(&streamline,&native));
  assert(!SameNativeIdentity(&other,&native));
  assert(!SameNativeIdentity(&other_proxy,&native));
  assert(!SameNativeIdentity(&cycle,&native));
  assert(!SameNativeIdentity(nullptr,&native));
  assert(!SameNativeIdentity(&native,nullptr));
  assert(native.references==1&&other.references==1&&reshade.references==1&&streamline.references==1&&other_proxy.references==1&&cycle.references==1);
  return 0;
}
