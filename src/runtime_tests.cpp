// Compile the real internal indexing/cache implementation, not a second model.
// This host-side test publishes no COM hooks and submits no GPU commands.
#define WITCHER_DOTS_HARNESS 1
#include "dots/gpu_runtime.cpp"
#include "dots/geometry.cpp"
#include "dots/shader_cache.cpp"
#include "dots/shader_ir.cpp"
#include <cassert>
#include <thread>
namespace single_module {
HMODULE LoadSystemModule(const wchar_t*) noexcept {
    assert(false&&"host-side runtime test must not load a driver");return nullptr;
}
void Log(const wchar_t* text) noexcept {if(text)std::wprintf(L"%ls\n",text);}
}
namespace {
struct HostIdentity final : IUnknown {
    ULONG references=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>1);return --references;}
};
struct HostResource final : ID3D12Resource {
    ULONG references=1;HostIdentity* device{};
    D3D12_RESOURCE_DESC desc{};uint64_t address{};unsigned descriptions{},addresses{};
    explicit HostResource(HostIdentity* d,uint64_t a) :device(d),address(a) {desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4096;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(ID3D12Resource))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>1);return --references;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(ID3D12Device))return E_NOINTERFACE;
        *out=reinterpret_cast<ID3D12Device*>(device);device->AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void**) override {assert(false);return E_NOTIMPL;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*) override {assert(false);}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override {++descriptions;return desc;}
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override {++addresses;return address;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT,const D3D12_BOX*,const void*,UINT,UINT) override {assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,UINT,const D3D12_BOX*) override {assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS*) override {return E_NOTIMPL;}
};
void TestResourceMetadata() {
    using namespace witcher_dots;
    HostIdentity device,other;HostResource positions(&device,0x100000),indices(&device,0x200000),replacement(&device,0x300000),foreign(&other,0x400000);
    C().identity=&device;
    std::array<std::byte,profile::kOwnerSize> owner{};
    const auto set=[&](size_t offset,HostResource& resource) {IUnknown* p=&resource;memcpy(owner.data()+offset,&p,sizeof(p));};
    set(profile::kPositionOffset,positions);set(profile::kIndexOffset,indices);
    LssGeometry geometry{};geometry.type=5;geometry.flags=1;geometry.vertexCount=6;geometry.indexCount=geometry.primitiveCount=4;
    geometry.positions={positions.address,16};geometry.positionFormat=DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.radii={positions.address+12,16};geometry.radiusFormat=DXGI_FORMAT_R32_FLOAT;
    geometry.indices={indices.address,4};geometry.indexFormat=DXGI_FORMAT_R32_UINT;geometry.primitiveFormat=1;
    ExtendedInputs inputs{1,7,1,0,168,0,&geometry};std::string error;
    {
        HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error));
        assert(positions.descriptions==1&&positions.addresses==1&&indices.descriptions==1&&indices.addresses==1);
        assert(hair.positionMetadata.address==positions.address&&hair.indexMetadata.description.Width==4096);
        assert(hair.plan.segments==4&&hair.segmentsPerStrand==2);
    }
    // A new invocation revalidates resource/device ownership, no persistent
    // owner-pointer cache or stale generation/address is admitted.
    set(profile::kPositionOffset,replacement);
    {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error));}
    geometry.positions.address=replacement.address;geometry.radii.address=replacement.address+12;
    {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error));assert(hair.positions.Get()==&replacement);}
    set(profile::kPositionOffset,foreign);geometry.positions.address=foreign.address;geometry.radii.address=foreign.address+12;
    {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error));assert(foreign.descriptions==0&&foreign.addresses==0);}
    set(profile::kPositionOffset,positions);positions.desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error));assert(positions.descriptions==2&&positions.addresses==1);}
    C().identity.Reset();
    assert(device.references==1&&other.references==1&&positions.references==1&&indices.references==1&&replacement.references==1&&foreign.references==1);
}
}

int main() {
    using namespace witcher_dots;
    TestResourceMetadata();
    cpu_profile::Window window;cpu_profile::Sample sample{};
    window.Update(100,sample,1000000);assert(!window.known);
    sample[0]={100000,50};window.Update(600,sample,1000000);assert(!window.known);
    window.Update(1100,sample,1000000);
    assert(window.known&&window.msPerSecond[0]==100.0&&window.callsPerSecond[0]==50.0);
    sample[0]={300000,100};window.Update(3100,sample,1000000);
    assert(window.known&&window.msPerSecond[0]==100.0&&window.callsPerSecond[0]==25.0);
    window.Update(9100,sample,1000000);assert(!window.known&&window.msPerSecond[0]==0);
    window.Update(10100,sample,1000000);assert(window.known&&window.msPerSecond[0]==0);
    window.Update(10200,sample,0);assert(!window.known);
    const auto beforeTimer=cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::Build)].calls;
    {cpu_profile::Timer timer(cpu_profile::Part::Build);timer.Stop();timer.Stop();}
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::Build)].calls==beforeTimer+1);
    for(size_t i=0;i<runtime_policy::kMaxLists;++i) {
        assert(runtime_policy::CanTrack(i));
        const auto key=reinterpret_cast<void*>(0x10000000ull+i*0x800);
        const auto value=reinterpret_cast<ListState*>(i+1);
        assert(IndexList(key,value));assert(List(key)==value);
    }
    assert(!runtime_policy::CanTrack(runtime_policy::kMaxLists));
    assert(!List(reinterpret_cast<void*>(0x10)));
    const auto read=[] {for(size_t i=0;i<runtime_policy::kMaxLists;++i)
        assert(List(reinterpret_cast<void*>(0x10000000ull+i*0x800))==reinterpret_cast<ListState*>(i+1));};
    std::thread a(read),b(read);a.join();b.join();
    // Two complete table generations for every admitted command list, plus
    // device/image slots. Validate the actual slot table at realistic load.
    constexpr size_t slots=2*runtime_policy::kMaxLists*runtime_policy::kTrackedMethods+256;
    for(size_t i=0;i<slots;++i) {
        const uintptr_t key=0x20000000ull+i*8;
        assert(!FindPublication(key));auto* p=NewPublication(key);assert(p);
        p->original.store(reinterpret_cast<void*>(i+1),std::memory_order_relaxed);
        p->slot.store(key,std::memory_order_release);assert(FindPublication(key)==p);
    }
    for(size_t i=0;i<slots;++i) {
        auto* p=FindPublication(0x20000000ull+i*8);assert(p);
        assert(p->original.load(std::memory_order_acquire)==reinterpret_cast<void*>(i+1));
    }
    assert(!FindPublication(0x10));
    // Identity-only unwrapping is not permission to record. The full table
    // check must still reject any changed hook or protection before injection.
    std::array<void*,kList4Methods> table{};
    for(size_t i=0;i<table.size();++i)table[i]=reinterpret_cast<void*>(0x1000+i);
    struct FakeList {void** table;} fake{table.data()};
    ListState list;
    list.keep.Attach(reinterpret_cast<ID3D12GraphicsCommandList4*>(&fake));
    assert(RememberListTable(list));assert(CurrentListIdentity(list));
    for(auto i:kTrackedListMethods) {
        const auto slot=reinterpret_cast<uintptr_t>(table.data()+i);
        auto* p=NewPublication(slot);assert(p);p->hook=table[i];
        assert(protected_pointer::QueryProtection(slot,p->protection,sizeof(void*)));
        p->slot.store(slot,std::memory_order_release);
    }
    assert(CurrentListTable(list));
    const auto before=table[26];table[26]=reinterpret_cast<void*>(0x9999);
    assert(CurrentListIdentity(list)&&!CurrentListTable(list));table[26]=before;
    const auto identity=table[0];table[0]=reinterpret_cast<void*>(0x9999);
    assert(!CurrentListIdentity(list)&&!CurrentListTable(list));table[0]=identity;
    fake.table=nullptr;assert(!CurrentListIdentity(list));fake.table=table.data();
    list.keep.Detach(); // Fake table is never invoked, including Release.
    PrebuildCache cache;unsigned queries=0;bool hit{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO out{};
    auto query=[&](auto& info) {++queries;info={1024,2048,512};};
    assert(cache.Get(120,7,out,hit,query)&&!hit&&queries==1);
    assert(cache.Get(120,7,out,hit,query)&&hit&&queries==1&&out.ResultDataMaxSizeInBytes==1024);
    assert(cache.Get(120,0x27,out,hit,query)&&!hit&&queries==2);
    assert(cache.Get(240,7,out,hit,query)&&!hit&&queries==3);
    for(unsigned i=0;i<10000;++i)assert(cache.Get(120,7,out,hit,query)&&hit);
    assert(queries==3);
    assert(!cache.Get(0,7,out,hit,query)&&!hit&&queries==3);
    assert(!cache.Get(120,8,out,hit,query)&&!hit&&queries==3);
    auto fail=[&](auto&) {++queries;};
    assert(!cache.Get(999,7,out,hit,fail)&&!hit);
    assert(!cache.Get(999,7,out,hit,fail)&&!hit&&queries==5);
    assert(cache.Get(999,7,out,hit,query)&&!hit);
    assert(cache.Get(999,7,out,hit,query)&&hit);
    for(unsigned i=0;i<128;++i)assert(cache.Get(1000+i,7,out,hit,query)&&!hit);
    assert(cache.Get(120,7,out,hit,query)&&!hit); // bounded replacement, no stale hit
    PrebuildCache otherDevice;
    assert(otherDevice.Get(120,7,out,hit,query)&&!hit); // per-device isolation
    unsigned fenceQueries=0;uint64_t complete=10;
    FenceSamples<2> fences;
    auto readFence=[&] {++fenceQueries;return complete;};
    const auto queueA=reinterpret_cast<void*>(1),queueB=reinterpret_cast<void*>(2),queueC=reinterpret_cast<void*>(3);
    assert(fences.Get(queueA,readFence)==10&&fenceQueries==1);
    complete=20;
    assert(fences.Get(queueA,readFence)==10&&fenceQueries==1); // cannot admit fence 11 before a fresh observation
    assert(fences.Get(queueB,readFence)==20&&fenceQueries==2);
    assert(fences.Get(queueC,readFence)==20&&fenceQueries==3); // full cache queries conservatively
    assert(fences.Get(queueC,readFence)==20&&fenceQueries==4);
    FenceSamples<2> fresh;
    assert(fresh.Get(queueA,readFence)==20&&fenceQueries==5);
    complete=UINT64_MAX;
    assert(fresh.Get(queueB,readFence)==UINT64_MAX);
    assert(fresh.Get(queueB,readFence)==UINT64_MAX&&fenceQueries==6); // removal is not converted into success
    std::printf("PASS: %zu command-list indices; %zu forwarding slots; concurrent lookup; bounded exact prebuild cache\n",
        runtime_policy::kMaxLists,slots);
    return 0;
}
