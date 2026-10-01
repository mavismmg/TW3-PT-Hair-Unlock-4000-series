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
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void**) override {assert(false);return E_NOTIMPL;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*) override {assert(false);}
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
}

int main() {
    using namespace witcher_dots;
    TestResourceMetadata();
    TestScopedReuse();TestCheckedSnapshots();TestScopedMemory();TestLeasesAndSaveRetention();
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
    std::printf("PASS: scoped source proofs, nested/thread-local builders, changed frontends/native identities/devices/descriptors, checked reads, save reuse, Reset/reclaim fences, balanced references, bounded owners/leases; %zu list indices; %zu forwarding slots; exact prebuild cache\n",
        runtime_policy::kMaxLists,slots);
    return 0;
}
