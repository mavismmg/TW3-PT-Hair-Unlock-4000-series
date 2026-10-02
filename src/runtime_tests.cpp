// Compile the real internal indexing/cache implementation, not a second model.
// This host-side test publishes no COM hooks and submits no GPU commands.
#define WITCHER_DOTS_HARNESS 1
#include "dots/gpu_runtime.cpp"
#include "dots/geometry.cpp"
#include "dots/shader_cache.cpp"
#include "dots/shader_ir.cpp"
#include "dots/input_scope.h"
#include <cassert>
#include <thread>
namespace single_module {
HMODULE LoadSystemModule(const wchar_t*) noexcept {
    assert(false&&"host-side runtime test must not load a driver");return nullptr;
}
void Log(const wchar_t* text) noexcept {if(text)std::wprintf(L"%ls\n",text);}
}
namespace {
unsigned forwardedSubmissions{};
void STDMETHODCALLTYPE ForwardSubmission(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*) {++forwardedSubmissions;}
void TestQueueIsolationAndBudgets() {
    using namespace witcher_dots;
    using namespace runtime_policy;
    static_assert(runtime_policy::kMaxQueues==32);
    assert(!SweepDue(110,100)&&SweepDue(350,100)&&SweepDue(110,100,true));
    assert(FitsBudget(397*kMiB,128*kMiB,1024*kMiB)&&!FitsBudget(397*kMiB,128*kMiB,512*kMiB));
    assert(!FitsBudget(UINT64_MAX,10,512*kMiB)&&!FitsBudget(0,0,512*kMiB));
    assert(GeometryLimit(512*kMiB,24*1024*kMiB,20*1024*kMiB,0,128*kMiB)==1024*kMiB);
    assert(GeometryLimit(512*kMiB,12*1024*kMiB,11*1024*kMiB,0,128*kMiB)==512*kMiB);
    assert(GeometryLimit(512*kMiB,0,0,0,128*kMiB)==512*kMiB);
    assert(GeometryLimit(512*kMiB,1024*kMiB,2048*kMiB,0,1)==512*kMiB);
    assert(GeometryLimit(512*kMiB,24*1024*kMiB,20*1024*kMiB,4*1024*kMiB,1)==512*kMiB);
    assert(AsLimit(0)==1024*kMiB&&AsLimit(12*1024*kMiB)==3*1024*kMiB&&AsLimit(UINT64_MAX)==4*1024*kMiB);
    ReferenceCounts references;const void* a=reinterpret_cast<void*>(0x1000),*b=reinterpret_cast<void*>(0x3000);
    references.Add(a);references.Add(a);references.Add(b);references.Add(nullptr);
    assert(references.Get(a)==2&&references.Get(b)==1&&references.Get(nullptr)==0);
    references.Remove(a);references.Remove(b);assert(references.Get(a)==1&&!references.Get(b));
    references.Add(b);assert(references.Get(b)==1);
    // Execute the actual forwarder: an untracked overlay queue is harmless
    // until a recording with hair leases is submitted on it.
    std::array<void*,80> table{};struct Fake {void** table;};Fake queue{table.data()},object{table.data()};
    auto* q=reinterpret_cast<ID3D12CommandQueue*>(&queue);
    auto* key=reinterpret_cast<ID3D12GraphicsCommandList4*>(&object);
    ListState state;state.keep.Attach(key);state.generation=42;state.open=false;
    assert(IndexList(key,&state));
    canonical[static_cast<size_t>(Kind::Queue)][10]=reinterpret_cast<void*>(&ForwardSubmission);
    ID3D12CommandList* list=reinterpret_cast<ID3D12CommandList*>(key);
    Execute(q,1,&list);assert(forwardedSubmissions==1&&!C().stats.lost);
    state.hasLeases=true;state.leases={0};C().leases[0].list=key;C().leases[0].generation=42;
    Execute(q,1,&list);
    assert(forwardedSubmissions==2&&C().stats.lost&&C().leases[0].poisoned);
    assert(strstr(C().stats.lostReason,"untracked queue"));
    std::string error;HairInput hair;D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    assert(!PrebuildTriangles(hair,7,info,error)&&error.find("untracked queue")!=std::string::npos);
    Lost("another failure");assert(strstr(C().stats.lostReason,"untracked queue")); // first reason retained
    C().stats={};C().leases[0]={};state.keep.Detach();
    for(size_t i=0;i<listKeys.size();++i)if(listKeys[i].load()==key){listKeys[i]=nullptr;listValues[i]=nullptr;}
    std::array<uint64_t,4> ticks{100,200,210,400};
    assert(gpu_profile::Valid(ticks.data(),1000)&&!gpu_profile::Valid(ticks.data(),0));
    ticks[2]=150;assert(!gpu_profile::Valid(ticks.data(),1000));
    gpu_profile::SetEnabled(true);const auto epoch=gpu_profile::epoch.load();
    gpu_profile::SetEnabled(true);assert(gpu_profile::epoch.load()==epoch);
    gpu_profile::SetEnabled(false);assert(!gpu_profile::Enabled()&&gpu_profile::epoch.load()>epoch);
    Lease timing;assert(!PrepareTiming(timing)&&!timing.timingHeap&&!timing.timingReadback);
    std::puts("PASS: real Execute overlay-only forwarding vs unfenced hair; first failure reason; bounded transition headroom; overflow guards; reference counts; GPU timings off without allocation");
}
struct HostIdentity final : IUnknown {
    ULONG references=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>0);return --references;}
};
struct HostResource final : ID3D12Resource {
    ULONG references=1;HostIdentity* device{};
    D3D12_RESOURCE_DESC desc{};uint64_t address{};unsigned descriptions{},addresses{},devices{};
    explicit HostResource(HostIdentity* d,uint64_t a) :device(d),address(a) {desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4096;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(ID3D12Resource))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>0);return --references;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** out) override {
        ++devices;
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(ID3D12Device))return E_NOINTERFACE;
        *out=reinterpret_cast<ID3D12Device*>(device);device->AddRef();return S_OK;
    }
    void* mappedMemory{};unsigned maps{},unmaps{};
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void** output) override {assert(mappedMemory&&output);++maps;*output=mappedMemory;return S_OK;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*) override {assert(mappedMemory);++unmaps;}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc() override {++descriptions;return desc;}
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress() override {++addresses;return address;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT,const D3D12_BOX*,const void*,UINT,UINT) override {assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,UINT,const D3D12_BOX*) override {assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS*) override {return E_NOTIMPL;}
};
struct HostFrontend final : IUnknown {
    ULONG references=1;HostResource* native{};
    explicit HostFrontend(HostResource& resource):native(&resource) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        constexpr GUID base={0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
        if(iid==base) {*out=native;native->AddRef();return S_OK;}
        if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>0);return --references;}
};
struct HostFence final : ID3D12Fence {
    ULONG references=1;uint64_t complete{};unsigned queries{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(iid!=__uuidof(IUnknown)&&iid!=__uuidof(ID3D12Fence))return E_NOINTERFACE;
        *out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
    ULONG STDMETHODCALLTYPE Release() override {assert(references>0);return --references;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**) override {return E_NOTIMPL;}
    UINT64 STDMETHODCALLTYPE GetCompletedValue() override {++queries;return complete;}
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64,HANDLE) override {assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Signal(UINT64) override {assert(false);return E_NOTIMPL;}
};
void TestGpuTimingReadbackSafety() {
    using namespace witcher_dots;
    HostIdentity device,queueIdentity;HostFence fence;HostResource readback(&device,0x100000);
    std::array<uint64_t,4> ticks{100,102,103,106};readback.mappedMemory=ticks.data();
    auto* queue=reinterpret_cast<ID3D12CommandQueue*>(&queueIdentity);auto& ctx=C();
    auto& q=ctx.queues[queue];q.keep=queue;q.fence=&fence;q.timestampFrequency=1000;
    ListState state;state.generation=7;state.open=true;
    const auto key=reinterpret_cast<ID3D12GraphicsCommandList4*>(0xcafe1230ull);assert(IndexList(key,&state));
    auto& lease=ctx.leases[0];lease.list=key;lease.generation=7;lease.queue=queue;lease.fenceValue=10;
    lease.timingReadback=&readback;lease.timingSerial=1;
    gpu_profile::SetEnabled(true);lease.timingEpoch=gpu_profile::epoch.load();
    fence.complete=10;QueueSamples recording;CollectGpuTimings(1000,recording);assert(!readback.maps);
    state.open=false;fence.complete=9;QueueSamples pending;CollectGpuTimings(1000,pending);assert(!readback.maps);
    fence.complete=10;QueueSamples done;CollectGpuTimings(1000,done);
    assert(readback.maps==1&&readback.unmaps==1&&ctx.stats.gpuTimingSamples==1);
    assert(ctx.stats.gpuConverterMs==2&&ctx.stats.gpuBlasMs==3);
    QueueSamples duplicate;CollectGpuTimings(1001,duplicate);assert(readback.maps==1);
    ++state.generation;++lease.timingSerial;QueueSamples stale;CollectGpuTimings(1002,stale);assert(readback.maps==1);
    state.generation=7;lease.poisoned=true;QueueSamples poisoned;CollectGpuTimings(1003,poisoned);assert(readback.maps==1);
    lease.poisoned=false;fence.complete=UINT64_MAX;QueueSamples removed;CollectGpuTimings(1004,removed);assert(readback.maps==1);
    fence.complete=10;gpu_profile::SetEnabled(false);QueueSamples off;CollectGpuTimings(1005,off);assert(readback.maps==1);
    gpu_profile::SetEnabled(true);QueueSamples epoch;CollectGpuTimings(1006,epoch);assert(readback.maps==1); // old session ignored
    lease.timingEpoch=gpu_profile::epoch.load();ticks[2]=101;
    QueueSamples invalid;CollectGpuTimings(1007,invalid);assert(readback.maps==2&&ctx.stats.gpuTimingFailures==1);
    gpu_profile::SetEnabled(false);lease={};ctx.queues.clear();ctx.stats={};
    for(size_t i=0;i<listKeys.size();++i)if(listKeys[i].load()==key){listKeys[i]=nullptr;listValues[i]=nullptr;}
    assert(readback.references==1&&queueIdentity.references==1&&fence.references==1);
    std::puts("PASS: GPU timing readback waits for closed/current/completed recording without blocking; removal/poison/epoch/invalid data rejected; exact timing units; duplicate samples ignored");
}
void TestScopedReuse() {
    using namespace witcher_dots;
    HostIdentity device,other;
    HostResource positions(&device,0x100000),indices(&device,0x200000),replacement(&device,0x300000),foreign(&other,0x400000);
    HostFrontend front(positions),secondFront(positions);
    C().identity=&device;
    std::array<std::byte,profile::kOwnerSize> owner{},otherOwner{};
    const auto set=[&](auto& memory,size_t offset,IUnknown* resource) {memcpy(memory.data()+offset,&resource,sizeof(resource));};
    set(owner,profile::kPositionOffset,&front);set(owner,profile::kIndexOffset,&indices);
    LssGeometry geometry{};geometry.type=5;geometry.flags=1;geometry.vertexCount=6;geometry.indexCount=geometry.primitiveCount=4;
    geometry.positions={positions.address,16};geometry.positionFormat=DXGI_FORMAT_R32G32B32_FLOAT;
    geometry.radii={positions.address+12,16};geometry.radiusFormat=DXGI_FORMAT_R32_FLOAT;
    geometry.indices={indices.address,4};geometry.indexFormat=DXGI_FORMAT_R32_UINT;geometry.primitiveFormat=1;
    ExtendedInputs inputs{1,7,1,0,168,0,&geometry};std::string error;
    const auto hits=inputReuseHits.load(),misses=inputReuseMisses.load();
    {
        OwnerScope scope{owner.data()};assert(scope.memory.Validate(owner.data(),owner.size()));OwnerScopeBinding binding(scope);
        assert(ownerScope==&scope&&!scope.reuse.valid);
        assert(ReadHairInput(owner.data(),inputs,scope.reuse.input,error));scope.reuse.valid=true;
        assert(front.references>1&&positions.references>1);
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        assert(inputReuseHits.load()==hits+1&&positions.devices==1&&indices.devices==1);
        // Reentrant builders cannot see the outer validation proof.
        {
            OwnerScope nested{owner.data()};assert(nested.memory.Validate(owner.data(),owner.size()));OwnerScopeBinding inner(nested);
            assert(ownerScope==&nested&&!nested.reuse.valid);
            HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&nested.reuse));
        }
        assert(ownerScope==&scope&&positions.devices==2);
        try {OwnerScope nested;OwnerScopeBinding inner(nested);throw 1;}catch(int) {}
        assert(ownerScope==&scope);
        std::thread isolated([] {assert(ownerScope==nullptr);});isolated.join();
        // Padding is not geometry; changing any meaningful field is.
        ++geometry.pad0;
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        auto changed=geometry;changed.vertexCount+=2;changed.primitiveCount=changed.indexCount=6;
        inputs.geometry=&changed;
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));assert(hair.plan.segments==6);}
        assert(positions.devices==3&&indices.devices==3);inputs.geometry=&geometry;
        const auto different=[&](auto alter) {auto copy=geometry;alter(copy);assert(!SameSourceGeometry(geometry,copy));};
        different([](auto& g){++g.type;});different([](auto& g){++g.flags;});
        different([](auto& g){++g.vertexCount;});different([](auto& g){++g.indexCount;});different([](auto& g){++g.primitiveCount;});
        different([](auto& g){++g.positions.address;});different([](auto& g){++g.positions.stride;});different([](auto& g){++g.positionFormat;});
        different([](auto& g){++g.radii.address;});different([](auto& g){++g.radii.stride;});different([](auto& g){++g.radiusFormat;});
        different([](auto& g){++g.indices.address;});different([](auto& g){++g.indices.stride;});different([](auto& g){++g.indexFormat;});
        different([](auto& g){++g.endcaps;});different([](auto& g){++g.primitiveFormat;});
        // Different frontend, same native object: full validation is required.
        set(owner,profile::kPositionOffset,&secondFront);
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        assert(positions.devices==4);set(owner,profile::kPositionOffset,&front);
        // Same retained frontend, different native object/address: never reuse.
        front.native=&replacement;
        {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        assert(replacement.devices==1);
        geometry.positions.address=replacement.address;geometry.radii.address=replacement.address+12;
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        front.native=&foreign;geometry.positions.address=foreign.address;geometry.radii.address=foreign.address+12;
        {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        assert(foreign.devices==1&&foreign.descriptions==0);
        front.native=&positions;geometry.positions.address=positions.address;geometry.radii.address=positions.address+12;
        otherOwner=owner;
        {HairInput hair;assert(ReadHairInput(otherOwner.data(),inputs,hair,error,&scope.reuse));}
        assert(positions.devices==5);
        set(owner,profile::kIndexOffset,&replacement);geometry.indices.address=replacement.address;
        {HairInput hair;assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        assert(positions.devices==6&&replacement.devices==3);
        set(owner,profile::kIndexOffset,&indices);geometry.indices.address=indices.address;
        // AS pointers are reread separately, even when source reuse is valid.
        set(owner,profile::kBlasOffset,&replacement);set(owner,profile::kScratchOffset,&indices);
        OwnerAs as;assert(ReadOwnerAs(owner.data(),as)&&as.blas==&replacement&&as.scratch==&indices);
        set(owner,profile::kBlasOffset,&foreign);assert(ReadOwnerAs(owner.data(),as)&&as.blas==&foreign);
        // A prepared-device change invalidates the proof, even if the owner,
        // frontend, native object and descriptor are all unchanged.
        C().identity=&other;
        {HairInput hair;assert(!ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse));}
        C().identity=&device;
    }
    assert(ownerScope==nullptr&&inputReuseHits.load()==hits+2&&inputReuseMisses.load()>=misses+7);
    // A new save can reuse the exact owner address, but not the previous proof.
    for(int save=0;save<15;++save) {
        OwnerScope scope{owner.data()};assert(scope.memory.Validate(owner.data(),owner.size()));OwnerScopeBinding binding(scope);HairInput hair;
        const auto before=positions.devices;
        assert(ReadHairInput(owner.data(),inputs,hair,error,&scope.reuse)&&positions.devices==before+1);
    }
    C().identity.Reset();
    assert(device.references==1&&other.references==1&&front.references==1&&secondFront.references==1);
    assert(positions.references==1&&indices.references==1&&replacement.references==1&&foreign.references==1);
}
void TestCheckedSnapshots() {
    using namespace witcher_dots;
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=system.dwPageSize;
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    OwnerSources sources;OwnerAs as;LssGeometry geometry{};DWORD previous{};
    assert(ReadOwnerSources(memory,sources)&&ReadOwnerAs(memory,as)&&ReadGeometry(memory,geometry));
    assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous));
    assert(!ReadOwnerSources(memory+page-profile::kPositionOffset-8,sources));
    assert(!ReadOwnerAs(memory+page-profile::kScratchOffset-8,as));
    assert(!ReadGeometry(memory+page-8,geometry));
    assert(VirtualProtect(memory+page,page,PAGE_READWRITE|PAGE_GUARD,&previous));
    assert(!ReadGeometry(memory+page,geometry));
    assert(VirtualFree(memory+page,page,MEM_DECOMMIT));assert(!ReadGeometry(memory+page,geometry));
    assert(!ReadOwnerSources(nullptr,sources)&&!ReadOwnerAs(nullptr,as)&&!ReadGeometry(nullptr,geometry));
    assert(!ReadOwnerSources(reinterpret_cast<void*>(UINTPTR_MAX-8),sources));
    assert(!ReadOwnerAs(reinterpret_cast<void*>(UINTPTR_MAX-8),as));
    assert(VirtualProtect(memory,page,PAGE_READONLY,&previous));
    assert(ReadGeometry(memory,geometry));
    // A protected destination must fail under SEH, not write through it.
    assert(!CopyChecked(memory,&geometry,sizeof(geometry)));
    assert(VirtualFree(memory,0,MEM_RELEASE));
}
void TestScopedMemory() {
    using namespace witcher_dots;
    cpu_profile::SetEnabled(true);
    const auto queries=[] {return cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls;};
    std::array<std::byte,profile::kOwnerSize> owner{},other{};
    OwnerSources sources{};OwnerAs as{};
    const auto before=queries();
    {
        OwnerScope scope{owner.data()};assert(scope.memory.Validate(owner.data(),owner.size()));OwnerScopeBinding binding(scope);
        const auto afterValidation=queries();assert(afterValidation>before);
        for(int frameRead=0;frameRead<100;++frameRead) {
            assert(ReadOwnerSources(owner.data(),sources)); // prebuild
            assert(ReadOwnerSources(owner.data(),sources)); // build: current pointers
            assert(ReadOwnerAs(owner.data(),as)); // current BLAS/scratch, not cached values
        }
        assert(queries()==afterValidation);
        auto* current=reinterpret_cast<IUnknown*>(0x12340);
        memcpy(owner.data()+profile::kPositionOffset,&current,sizeof(current));
        assert(ReadOwnerSources(owner.data(),sources)&&sources.positions==current&&queries()==afterValidation);
        // An inner scope cannot borrow the outer range, even for the same owner.
        {
            OwnerScope inner{owner.data()};OwnerScopeBinding nested(inner);
            const auto start=queries();assert(ReadOwnerSources(owner.data(),sources)&&queries()>start);
        }
        const auto restored=queries();assert(ReadOwnerSources(owner.data(),sources)&&queries()==restored);
        assert(ReadOwnerSources(other.data(),sources)&&queries()>restored);
        const auto unknown=queries();uint64_t word{};
        assert(!scope.memory.Read(owner.data(),owner.size(),word));
        assert(!scope.memory.Read(other.data(),0,word));
        assert(!scope.memory.Read(owner.data(),SIZE_MAX,word)&&queries()==unknown);
        std::thread isolated([&] {assert(!ownerScope);assert(ReadOwnerSources(owner.data(),sources));});isolated.join();
    }
    const auto ended=queries();assert(ReadOwnerSources(owner.data(),sources)&&queries()>ended);
    // A partial proof never authorizes a wider snapshot.
    {
        OwnerScope scope{owner.data()};assert(scope.memory.Validate(owner.data(),8));OwnerScopeBinding binding(scope);
        const auto start=queries();assert(ReadOwnerSources(owner.data(),sources)&&queries()>start);
    }
    ScopedReadRange overflow;
    assert(!overflow.Validate(reinterpret_cast<void*>(UINTPTR_MAX-8),16));
    assert(!overflow.Validate(nullptr,8)&&!overflow.Validate(owner.data(),0));
    // Validate all pages once, then catch a later protection/unmap fault under
    // SEH. Failed revalidation must erase the previous proof completely.
    SYSTEM_INFO sys{};GetSystemInfo(&sys);const auto page=sys.dwPageSize;
    auto* pages=static_cast<std::byte*>(VirtualAlloc(nullptr,2*page,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(pages);
    auto* crossing=pages+page-16;DWORD previous{};
    {
        OwnerScope scope{crossing};assert(scope.memory.Validate(crossing,profile::kOwnerSize));OwnerScopeBinding binding(scope);
        assert(ReadOwnerSources(crossing,sources));
        assert(VirtualProtect(pages+page,page,PAGE_NOACCESS,&previous));
        assert(!ReadOwnerSources(crossing,sources)&&!ReadOwnerAs(crossing,as));
        assert(!scope.memory.Validate(crossing,profile::kOwnerSize));
        assert(!scope.memory.Contains(crossing,0,8));
        assert(VirtualProtect(pages+page,page,PAGE_READWRITE|PAGE_GUARD,&previous));
        assert(!scope.memory.Validate(crossing,profile::kOwnerSize));
        MEMORY_BASIC_INFORMATION region{};assert(VirtualQuery(pages+page,&region,sizeof(region))&&region.Protect&PAGE_GUARD);
        assert(VirtualProtect(pages+page,page,PAGE_READWRITE,&previous));
        assert(scope.memory.Validate(crossing,profile::kOwnerSize));
        assert(VirtualFree(pages+page,page,MEM_DECOMMIT));
        assert(!ReadOwnerSources(crossing,sources));
    }
    assert(VirtualFree(pages,0,MEM_RELEASE));
    uint64_t word{};assert(!CopyGuarded(nullptr,&word,sizeof(word))&&!CopyGuarded(&word,nullptr,sizeof(word)));
    assert(!CopyGuarded(&word,reinterpret_cast<void*>(UINTPTR_MAX-4),sizeof(word)));
    // Deterministic query-count regression: three snapshots formerly queried
    // the owner three times, on top of the builder gate. Scoped snapshots do 0.
    std::printf("PASS: scoped memory proof: 300 owner snapshots, zero additional VirtualQuery; nested/unknown/out-of-range reads use checked fallback; protection/unmap faults contained\n");
    cpu_profile::SetEnabled(false);
}
void TestCurrentPageValidation() {
    using namespace witcher_dots;
    cpu_profile::SetEnabled(true);
    const auto queries=[] {return cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls;};
    PSAPI_WORKING_SET_EX_BLOCK block{};
    block.Win32Protection=PAGE_READWRITE;assert(CurrentPageStatus(block)==PageReadStatus::Unknown);
    block.Valid=1;assert(CurrentPageStatus(block)==PageReadStatus::Readable);
    for(const DWORD protection:std::array<DWORD,4>{PAGE_NOACCESS,PAGE_EXECUTE,PAGE_READWRITE|PAGE_GUARD,0}) {
        block.Win32Protection=protection;assert(CurrentPageStatus(block)==PageReadStatus::Rejected);
    }
    block.Win32Protection=PAGE_READONLY;block.Bad=1;assert(CurrentPageStatus(block)==PageReadStatus::Rejected);
    block.Valid=0;assert(CurrentPageStatus(block)==PageReadStatus::Rejected);
    SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
    auto* memory=static_cast<std::byte*>(VirtualAlloc(nullptr,10*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    // Touch each page before observing residency. The helper must never fault
    // pages in merely to discover whether it can safely read them.
    for(size_t i=0;i<10;++i)memory[i*page]=std::byte{1};
    unsigned probes{};
    auto countReal=[&](void* pages,DWORD bytes) noexcept {
        ++probes;assert(bytes==2*sizeof(PSAPI_WORKING_SET_EX_INFORMATION));
        const auto* entries=static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(pages);
        assert(entries[0].VirtualAddress==memory&&entries[1].VirtualAddress==memory+page);
        return QueryWorkingSetEx(GetCurrentProcess(),pages,bytes);
    };
    auto* crossing=memory+page-16;
    const auto before=queries();assert(ReadableCurrentPagesWith(crossing,profile::kOwnerSize,countReal));
    // Confirm actual resident-page metadata can replace the VAD query, without
    // relying on timing or a simulated authorization in the production helper.
    assert(probes==1&&queries()==before);
    {
        OwnerScope scope{crossing};assert(scope.memory.ValidateCurrentPages(crossing,profile::kOwnerSize));OwnerScopeBinding binding(scope);
        OwnerSources sources{};OwnerAs as{};const auto validated=queries();
        for(unsigned i=0;i<100;++i)assert(ReadOwnerSources(crossing,sources)&&ReadOwnerAs(crossing,as));
        assert(queries()==validated);
        DWORD old{};assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old));
        assert(!ReadOwnerSources(crossing,sources));
        assert(!scope.memory.ValidateCurrentPages(crossing,profile::kOwnerSize)&&!scope.memory.Contains(crossing,0,1));
        assert(VirtualProtect(memory+page,page,PAGE_READWRITE,&old));
    }
    auto failing=[](void*,DWORD) noexcept {return FALSE;};
    auto unknown=[](void* p,DWORD bytes) noexcept {
        auto* entries=static_cast<PSAPI_WORKING_SET_EX_INFORMATION*>(p);
        for(size_t i=0;i<bytes/sizeof(entries[0]);++i) {
            entries[i].VirtualAttributes.Flags=0;
            // Undefined protection bits must be ignored for Valid=0.
            entries[i].VirtualAttributes.Win32Protection=PAGE_GUARD;
        }
        return TRUE;
    };
    const auto fallback=queries();assert(ReadableCurrentPagesWith(memory,page,failing)&&queries()>fallback);
    const auto absent=queries();assert(ReadableCurrentPagesWith(memory,page,unknown)&&queries()>absent);
    const auto large=queries();
    assert(ReadableCurrentPagesWith(memory,10*page,[](void*,DWORD) noexcept {assert(false);return FALSE;})&&queries()>large);
    assert(!ReadableCurrentPagesWith(nullptr,8,failing)&&!ReadableCurrentPagesWith(memory,0,failing));
    assert(!ReadableCurrentPagesWith(reinterpret_cast<void*>(UINTPTR_MAX-8),16,failing));
    DWORD old{};
    for(const DWORD protection:{PAGE_READONLY,PAGE_READWRITE,PAGE_EXECUTE_READ,PAGE_NOACCESS,PAGE_READWRITE|PAGE_GUARD}) {
        assert(VirtualProtect(memory+page,page,protection,&old));
        assert(ReadableCurrentPages(crossing,profile::kOwnerSize)==Readable(crossing,profile::kOwnerSize));
        if(protection&PAGE_GUARD) {
            MEMORY_BASIC_INFORMATION region{};assert(VirtualQuery(memory+page,&region,sizeof(region))&&region.Protect&PAGE_GUARD);
            assert(!ReadableCurrentPagesWith(crossing,profile::kOwnerSize,unknown));
            assert(VirtualQuery(memory+page,&region,sizeof(region))&&region.Protect&PAGE_GUARD);
        }
    }
    assert(VirtualProtect(memory+page,page,PAGE_READWRITE,&old));
    assert(VirtualFree(memory+page,page,MEM_DECOMMIT));
    assert(!ReadableCurrentPages(crossing,profile::kOwnerSize));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    assert(!ReadableCurrentPages(crossing,profile::kOwnerSize));
    // A reused address is validated again; no old protection proof survives.
    memory=static_cast<std::byte*>(VirtualAlloc(memory,10*page,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));assert(memory);
    assert(!ReadableCurrentPages(memory,profile::kOwnerSize));
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&old));memory[0]=std::byte{1};
    assert(ReadableCurrentPages(memory,profile::kOwnerSize));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    cpu_profile::SetEnabled(false);
    std::printf("PASS: current page protection: resident owner validation without VirtualQuery; every page checked; unknown/API failure/large range use checked fallback; guard preserved; protection/decommit/address reuse validated\n");
}
void TestLeasesAndSaveRetention() {
    using namespace witcher_dots;
    HostIdentity device,queueIdentity;HostFence fence;
    HostResource vertices(&device,0x100000),source(&device,0x200000),blas(&device,0x300000),newBlas(&device,0x400000);
    auto* queue=reinterpret_cast<ID3D12CommandQueue*>(&queueIdentity);
    auto& ctx=C();auto& q=ctx.queues[queue];q.keep=queue;q.fence=&fence;
    auto& first=ctx.leases[0];auto& second=ctx.leases[1];
    first.vertices=&vertices;first.positions=&source;first.blas=&blas;first.queue=queue;first.fenceValue=10;first.lastUsed=GetTickCount64();
    second.positions=&source;second.blas=&blas;second.queue=queue;second.fenceValue=20;second.lastUsed=first.lastUsed;
    auto& association=ctx.associations[0];association.owner=reinterpret_cast<void*>(0x1234);association.blas=&blas;association.positions=&source;
    // Save unload releases the game's reference. Pending leases still own data.
    blas.Release();assert(!GameHolds(association));
    QueueSamples pending;assert(!Available(first,&pending)&&!Available(second,&pending));
    assert(ClassifyLease(first,pending)==LeasePhase::Pending&&fence.queries==1);
    EvictReleased(pending);assert(first.blas&&second.blas&&source.references==3&&!association.owner&&fence.queries==1);
    // Address reused by a new save: old leases cannot become its generation.
    association.owner=reinterpret_cast<void*>(0x1234);association.blas=&newBlas;association.positions=&source;
    HairInput hair;hair.blas=&blas;hair.plan.segments=4;hair.geometry.vertexCount=6;
    association.segments=4;association.vertices=6;association.flags=7;
    ExtendedBuild desc{};desc.inputs.flags=0x27;desc.source=desc.destination=0x400000;
    assert(!UpdateMatches(association,hair,desc));hair.blas=&newBlas;assert(UpdateMatches(association,hair,desc));
    ++hair.geometry.vertexCount;assert(!UpdateMatches(association,hair,desc));--hair.geometry.vertexCount;
    ++desc.source;assert(!UpdateMatches(association,hair,desc));--desc.source;
    // One operation keeps the conservative older fence value after progression.
    fence.complete=10;QueueSamples completed;EvictReleased(completed);
    assert(!first.blas&&second.blas&&GameHolds(association));
    assert(ClassifyLease(first,completed)==LeasePhase::Available);
    fence.complete=20;assert(!Available(second,&completed));
    QueueSamples fresh;EvictReleased(fresh);assert(!second.blas&&blas.references==0&&association.owner);
    assert(source.references==2); // game + new association, not stale leases
    // Missing queues, device removal, and poisoned leases fail closed.
    second.positions=&source;second.queue=queue;second.fenceValue=30;
    fence.complete=UINT64_MAX;QueueSamples removed;
    assert(!Available(second,&removed)&&ClassifyLease(second,removed)==LeasePhase::Unsafe);
    second.queue=reinterpret_cast<ID3D12CommandQueue*>(0x4567);QueueSamples missing;
    assert(!Available(second,&missing)&&ClassifyLease(second,missing)==LeasePhase::Unsafe);
    second.queue=nullptr;second.poisoned=true;assert(!Available(second)&&ClassifyLease(second,missing)==LeasePhase::Unsafe);
    // Recorded leases are not reusable before reset/reclamation; mismatched
    // list generations may never be reclaimed by a completed fence.
    ListState state;state.generation=3;auto* key=reinterpret_cast<ID3D12GraphicsCommandList4*>(0xabcdef0);
    assert(IndexList(key,&state));second.poisoned=false;second.list=key;second.generation=3;
    state.open=true;assert(ClassifyLease(second,missing)==LeasePhase::Recording&&!Available(second));
    state.open=false;assert(ClassifyLease(second,missing)==LeasePhase::Recorded&&!Available(second));
    second.generation=2;assert(ClassifyLease(second,missing)==LeasePhase::Unsafe);
    second.queue=queue;fence.complete=30;second.lastUsed=0;
    QueueSamples stale;ReclaimFinished(GetTickCount64()+kReclaimMs,stale);assert(second.list==key);
    second.generation=3;QueueSamples reclaim;ReclaimFinished(GetTickCount64()+kReclaimMs,reclaim);
    assert(!second.list&&state.reclaimed&&Available(second,&reclaim));DropLeaseReferences(second);
    // Reset the test index, without ever publishing/calling a fake COM list.
    for(size_t i=0;i<listKeys.size();++i)if(listKeys[i].load()==key){listKeys[i]=nullptr;listValues[i]=nullptr;}
    hair=HairInput{};association=Association{};first=Lease{};second=Lease{};ctx.queues.clear();
    assert(device.references==1&&queueIdentity.references==1&&fence.references==1&&vertices.references==1&&source.references==1&&newBlas.references==1);
    // Real Reset release helper: one completion observation for the whole
    // operation, without discarding an incomplete or wrong-generation lease.
    ctx.queues[queue].keep=queue;ctx.queues[queue].fence=&fence;fence.complete=10;
    state.keep.Attach(key);state.generation=4;state.leases={0,1,2};state.hasLeases=true;
    for(size_t i=0;i<3;++i) {
        auto& lease=ctx.leases[i];lease.list=key;lease.generation=i==2?3:4;
        lease.positions=&source;lease.blas=&newBlas;lease.queue=queue;lease.fenceValue=i==0?10:20;
    }
    const auto before=fence.queries;QueueSamples reset;ReleaseRecordingLeases(state,reset);
    assert(fence.queries==before+1&&!first.list&&!first.blas&&!second.list&&second.blas);
    assert(ctx.leases[2].list==key&&ctx.leases[2].blas&&state.leases.empty()&&!state.hasLeases);
    state.keep.Detach(); // fake list is never invoked
    for(size_t i=0;i<3;++i)ctx.leases[i]=Lease{};
    ctx.queues.clear();
    // Bounded owner table: off-camera game-owned data is not evicted. A
    // completed save unload frees all associations, even with shared sources.
    for(size_t i=0;i<ctx.associations.size();++i) {
        auto* owner=reinterpret_cast<void*>(0x10000+i*16);auto* slot=FindAssociation(owner);assert(slot);
        slot->owner=owner;slot->blas=&newBlas;slot->positions=&source;assert(FindAssociation(owner)==slot);
    }
    assert(!FindAssociation(reinterpret_cast<void*>(0x98765))&&!FindAssociation(nullptr));
    QueueSamples retained;EvictReleased(retained);assert(ctx.associations.back().owner);
    newBlas.Release();QueueSamples unloaded;EvictReleased(unloaded);
    for(const auto& slot:ctx.associations)assert(!slot.owner&&!slot.blas&&!slot.positions);
    assert(FindAssociation(reinterpret_cast<void*>(0x10000))==&ctx.associations[0]);
    assert(newBlas.references==0&&source.references==1&&queueIdentity.references==1&&fence.references==1);
    // A pool full of in-flight leases never becomes available by guessing
    // that a save changed; completion is shared but not retained across calls.
    ctx.queues[queue].keep=queue;ctx.queues[queue].fence=&fence;fence.complete=10;
    for(auto& lease:ctx.leases){lease.positions=&source;lease.queue=queue;lease.fenceValue=11;}
    QueueSamples full;const auto polls=fence.queries;
    for(const auto& lease:ctx.leases)assert(!Available(lease,&full));
    assert(fence.queries==polls+1);fence.complete=11;
    for(const auto& lease:ctx.leases)assert(!Available(lease,&full));
    QueueSamples done;for(auto& lease:ctx.leases){assert(Available(lease,&done));lease=Lease{};}
    ctx.queues.clear();assert(source.references==1&&queueIdentity.references==1&&fence.references==1);
}
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
void TestIndexedLeaseLifetime() {
    using namespace witcher_dots;
    if constexpr(!kIndexedGeometry)return;
    HostIdentity device;HostResource old(&device,0x100000),next(&device,0x200000);
    Lease lease;C().convertedIndices=&old;
    assert(RetainConvertedIndices(lease)&&old.references==3);
    assert(RetainConvertedIndices(lease)&&old.references==3); // no duplicate retention
    C().convertedIndices=&next;
    assert(old.references==2&&RetainConvertedIndices(lease)&&next.references==3);
    assert(ConvertedIndexBytes()==next.desc.Width); // local lease not in context yet
    // Account actual context leases, not arbitrary host stack references.
    C().leases[0]=std::move(lease);
    assert(ConvertedIndexBytes()==old.desc.Width+next.desc.Width);
    C().convertedIndices.Reset();
    assert(old.references==2&&next.references==2); // old recording owns both
    DropLeaseReferences(C().leases[0]);C().leases[0]={};
    assert(old.references==1&&next.references==1&&ConvertedIndexBytes()==0);
    // Every slot occupied by different retained prefixes -> fail closed.
    std::array<std::unique_ptr<HostResource>,kMaxOwners> versions;
    for(size_t i=0;i<versions.size();++i) {
        versions[i]=std::make_unique<HostResource>(&device,0x300000+i*0x1000);
        C().convertedIndices=versions[i].get();assert(RetainConvertedIndices(lease));
    }
    C().convertedIndices=&old;assert(!RetainConvertedIndices(lease));
    C().convertedIndices.Reset();DropLeaseReferences(lease);
    for(const auto& version:versions)assert(version->references==1);
    std::puts("PASS: immutable index prefixes shared, recording retains old/grown buffers, balanced references, bounded generations");
}
}

int main() {
    TestQueueIsolationAndBudgets();
    using namespace witcher_dots;
    TestResourceMetadata();
    TestGpuTimingReadbackSafety();
    TestIndexedLeaseLifetime();
    TestScopedReuse();TestCheckedSnapshots();TestScopedMemory();TestCurrentPageValidation();TestLeasesAndSaveRetention();
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
    assert(!cpu_profile::Enabled());
    {cpu_profile::Timer timer(cpu_profile::Part::Build);}
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::Build)].calls==beforeTimer);
    cpu_profile::SetEnabled(true);
    {cpu_profile::Timer timer(cpu_profile::Part::Build);timer.Stop();timer.Stop();}
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::Build)].calls==beforeTimer+1);
    {cpu_profile::Timer timer(cpu_profile::Part::Build);cpu_profile::SetEnabled(false);cpu_profile::SetEnabled(true);}
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::Build)].calls==beforeTimer+1);
    const auto memoryQueries=cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls;
    {OwnerSources source;std::array<std::byte,profile::kOwnerSize> owner{};assert(ReadOwnerSources(owner.data(),source));}
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls>memoryQueries);
    cpu_profile::SetEnabled(false);
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
    cpu_profile::SetEnabled(true);
    const auto tableQueries=cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls;
    assert(CurrentListTable(list));
    assert(cpu_profile::Read()[static_cast<size_t>(cpu_profile::Part::MemoryQuery)].calls-tableQueries<=3);
    cpu_profile::SetEnabled(false);
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
    std::printf("PASS: scoped source proofs, nested/thread-local builders, changed frontends/native identities/devices/descriptors, checked reads, save reuse, Reset/reclaim fences, balanced references, bounded owners/leases; %zu list indices; %zu forwarding slots; exact prebuild cache\n",
        runtime_policy::kMaxLists,slots);
    return 0;
}
