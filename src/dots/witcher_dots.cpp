#include "witcher_dots.h"
#include "gpu_runtime.h"
#include "input_scope.h"
#include "game_profile.h"
#include "checked_memory.h"
#include "../protected_pointer.h"
#include "../overlay_native.h"
#include "../single_module.h"
#include <detours.h>
#include <dxgi1_4.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace witcher_dots {
using Microsoft::WRL::ComPtr;
namespace {
struct State {
    std::mutex lock;
    std::wstring executable,directory,control;
    Snapshot snapshot{};
    HMODULE game{};
    ComPtr<ID3D12Device5> device;
    ComPtr<IUnknown> identity;
    ShaderCache shaders;
    std::mutex instanceLock;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceWorkspace;
    std::vector<ComPtr<IUnknown>> deviceAliasRefs;
    ComPtr<IDXGIAdapter3> memoryAdapter;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};
    uint64_t memorySampled{};
    uint64_t hookSampleTick{},hookSampleMicroseconds{};
    double recentHookMsPerSecond{};
    bool recentHookTimeKnown{};
    cpu_profile::Window cpuWindow;
    uint64_t cpuEpoch{};
    bool preferencesRead{},attempted{},dredEnabled{};
};
State& S() {static auto* const state=new State;return *state;}
std::atomic<bool> active{};
std::atomic<uint32_t> rejectionLogs{};
std::atomic<bool> fallbackShown{};
std::atomic<bool> converted{};
// Wrapper pointers proven to front the prepared device (kept alive in State).
std::array<std::atomic<void*>,4> deviceAliases{};
std::atomic<bool> settingKnown{};
std::atomic<uint64_t> declinedWhileOff{},hookTicks{};
std::atomic<int> loggedSetting{-1};
std::atomic<uint32_t> settingLogs{};
// When the setting returns mid-game the game recreates all of its hair, in
// more than one burst; hair re-enters ray tracing once that has settled.
constexpr uint64_t kSettleMs=2000;
std::atomic<uint64_t> traceAfterTick{};
struct HookTimer {
    LARGE_INTEGER start{};
    HookTimer() noexcept {QueryPerformanceCounter(&start);}
    ~HookTimer() {LARGE_INTEGER end{};QueryPerformanceCounter(&end);hookTicks.fetch_add(static_cast<uint64_t>(end.QuadPart-start.QuadPart),std::memory_order_relaxed);}
};
// The game's own Path Traced Hair state (profile-validated config variables).
// Off: DOTS declines hair builds and keeps hair out of ray tracing, so
// HairWorks behaves as on a stock RTX 40 series GPU.
bool GameHairTraced() noexcept {
    if(!settingKnown.load(std::memory_order_acquire))return false;
    const auto* base=reinterpret_cast<const std::byte*>(S().game);
    return *reinterpret_cast<const volatile uint8_t*>(base+profile::kPtEnable.value)!=0
        &&*reinterpret_cast<const volatile int32_t*>(base+profile::kPtHairQuality.value)>0;
}
bool HairTracedNow() noexcept {
    const bool on=GameHairTraced();const int value=on?1:0;
    const int previous=loggedSetting.exchange(value,std::memory_order_relaxed);
    if(previous==value)return on;
    if(on&&previous==0)traceAfterTick.store(GetTickCount64()+kSettleMs,std::memory_order_relaxed);
    if(settingLogs.fetch_add(1,std::memory_order_relaxed)<32)
        single_module::Log(on?(previous==0?L"WITCHER_DOTS game Path Traced Hair on: converting hair; ray tracing hair after 2 s"
                :L"WITCHER_DOTS game Path Traced Hair on: converting hair")
            :L"WITCHER_DOTS game Path Traced Hair off: hair builds declined, HairWorks stays raster");
    return on;
}
using BuilderFn=int32_t(WINAPI*)(void*,const void*);
using PrebuildFn=int32_t(WINAPI*)(ID3D12Device5*,const PrebuildParams*);
using BuildFn=int32_t(WINAPI*)(ID3D12GraphicsCommandList4*,const BuildParams*);
using CopyFn=uintptr_t(WINAPI*)(void*,const void*,size_t);
BuilderFn originalBuilder{};PrebuildFn originalPrebuild{};BuildFn originalBuild{};CopyFn originalCopy{};
std::array<void*,4> gameTargets{};
std::array<bool,4> gameEnabled{};
void Preferences() {
    auto& state=S();if(state.preferencesRead)return;state.preferencesRead=true;
    std::wstring path(32768,L'\0');
    const DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!n||n>=path.size())return;path.resize(n);
    state.executable=path;state.directory=std::filesystem::path(path).parent_path().wstring();
    state.control=(std::filesystem::path(state.directory)/L"RTXMFG.WitcherDOTS.ini").wstring();
    state.snapshot.applicable=_wcsicmp(std::filesystem::path(path).filename().c_str(),L"witcher3.exe")==0;
    if(!state.snapshot.applicable)return;
    state.game=GetModuleHandleW(nullptr);state.snapshot.processId=GetCurrentProcessId();
    // Always prepared in Witcher 3: the game's own Path Traced Hair setting
    // decides whether hair is traced. RTXMFG_WITCHER_DOTS=0 is a
    // troubleshooting off switch.
    state.snapshot.requested=true;
    state.snapshot.crashReportRequested=GetPrivateProfileIntW(L"WitcherDOTS",L"CrashReport",0,state.control.c_str())==1;
    wchar_t env[8]{};const DWORD count=GetEnvironmentVariableW(L"RTXMFG_WITCHER_DOTS",env,_countof(env));
    if(count==1&&env[0]==L'0') {state.snapshot.environmentOverride=true;state.snapshot.requested=false;}
    state.snapshot.stage=state.snapshot.requested?Stage::WaitingForDevice:Stage::Disabled;
}
void Reason(Stage stage,std::string_view reason) {
    auto& state=S();std::lock_guard lock(state.lock);state.snapshot.stage=stage;
    const auto size=std::min(reason.size(),sizeof(state.snapshot.reason)-1);
    memcpy(state.snapshot.reason,reason.data(),size);state.snapshot.reason[size]=0;
    wchar_t line[512]{};swprintf_s(line,L"WITCHER_DOTS stage=%S reason=%S",StageText(stage),state.snapshot.reason);
    single_module::Log(line);
}
void RejectReason(std::string_view error) {
    auto& state=S();std::lock_guard lock(state.lock);
    // Log each change of reason (bounded) so a later blocker is not hidden
    // behind repeats of the first one.
    if(error.substr(0,sizeof(state.snapshot.reason)-1)!=std::string_view(state.snapshot.reason)
        &&rejectionLogs.fetch_add(1,std::memory_order_relaxed)<24) {
        wchar_t line[512]{};const std::string text(error.substr(0,256));
        swprintf_s(line,L"WITCHER_DOTS raster-fallback reason=%S",text.c_str());single_module::Log(line);
    }
    const size_t size=std::min(error.size(),sizeof(state.snapshot.reason)-1);
    memcpy(state.snapshot.reason,error.data(),size);state.snapshot.reason[size]=0;
    fallbackShown.store(true,std::memory_order_release);
}
// A rejection stays on screen until the next successful conversion, which
// keeps it only as the last fallback instead of the current state. The first
// conversion also retires the "waiting for game hair" preparation text.
void ConversionSucceeded() {
    const bool first=!converted.exchange(true,std::memory_order_acq_rel);
    if(!fallbackShown.exchange(false,std::memory_order_acq_rel)) {
        if(first) {auto& state=S();std::lock_guard lock(state.lock);state.snapshot.reason[0]=0;}
        return;
    }
    auto& state=S();std::lock_guard lock(state.lock);
    char text[sizeof(state.snapshot.reason)]{};
    _snprintf_s(text,_TRUNCATE,"last fallback: %s",state.snapshot.reason);
    strcpy_s(state.snapshot.reason,text);
}
bool At(const void* caller,uint32_t rva) {return caller==reinterpret_cast<const std::byte*>(S().game)+rva;}
bool DeviceMatches(ID3D12Device5* device) {
    if(!device)return false;
    for(const auto& alias:deviceAliases)if(alias.load(std::memory_order_acquire)==device)return true;
    ComPtr<ID3D12Device5> native;ComPtr<IUnknown> identity;
    if(!ResolveNativeDevice(device,native)||FAILED(native.As(&identity))||identity.Get()!=S().identity.Get())return false;
    // Resolution through a wrapper may create a fence: do it once per front.
    auto& state=S();std::lock_guard lock(state.lock);
    if(state.deviceAliasRefs.size()<deviceAliases.size()) {
        const size_t slot=state.deviceAliasRefs.size();state.deviceAliasRefs.emplace_back(device);
        deviceAliases[slot].store(device,std::memory_order_release);
    }
    return true;
}
int32_t WINAPI Builder(void* owner,const void* context) {
    if(!active.load(std::memory_order_acquire))return originalBuilder(owner,context);
    struct Header {uint32_t version{},pad{};ID3D12GraphicsCommandList4* list{};} header;
    OwnerScope scope{owner};bool accepted{};
    {
        cpu_profile::Timer gate(cpu_profile::Part::OwnerGate);
        {cpu_profile::Timer range(cpu_profile::Part::OwnerRange);
            accepted=scope.memory.ValidateCurrentPages(owner,profile::kOwnerSize);}
        if(accepted) {
            cpu_profile::Timer read(cpu_profile::Part::BuilderContext);
            accepted=ReadableCurrentPages(context,sizeof(header))&&CopyGuarded(&header,context,sizeof(header))
                &&header.version==0x201&&header.list;
        }
    }
    if(!accepted)return originalBuilder(owner,context);
    scope.list=header.list;OwnerScopeBinding binding(scope);
    return originalBuilder(owner,context);
}
int32_t WINAPI Prebuild(ID3D12Device5* device,const PrebuildParams* supplied) try {
    if(!active.load(std::memory_order_acquire)||!At(_ReturnAddress(),profile::kPrebuildReturnRva))return originalPrebuild(device,supplied);
    HookTimer timer;
    cpu_profile::Timer profile(cpu_profile::Part::Prebuild);
    if(!HairTracedNow()) {declinedWhileOff.fetch_add(1,std::memory_order_relaxed);return -1;}
    PrebuildParams params{};ExtendedInputs inputs{};HairInput hair;std::string error;
    if(!ownerScope||!DeviceMatches(device)||!CopyChecked(&params,supplied,sizeof(params))||params.version!=0x10018
        ||!CopyChecked(&inputs,params.inputs,sizeof(inputs))||!params.info
        ||!ReadHairInput(ownerScope->owner,inputs,hair,error)) {
        RejectReason(error.empty()?"prebuild caller/device/layout not established":error);return -1;
    }
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    if(!PrebuildTriangles(hair,inputs.flags,info)||!CopyChecked(params.info,&info,sizeof(info))) {
        RejectReason("triangle prebuild capacity unavailable");return -1;
    }
    ownerScope->reuse.input=std::move(hair);ownerScope->reuse.valid=true;
    ownerScope->havePrebuild=true;return 0;
} catch(...) {StopConversions();return -1;
}
int32_t WINAPI Build(ID3D12GraphicsCommandList4* list,const BuildParams* supplied) try {
    if(!active.load(std::memory_order_acquire)||!At(_ReturnAddress(),profile::kBuildReturnRva))return originalBuild(list,supplied);
    HookTimer timer;
    cpu_profile::Timer profile(cpu_profile::Part::Build);
    if(!HairTracedNow()) {declinedWhileOff.fetch_add(1,std::memory_order_relaxed);return -1;}
    BuildParams params{};ExtendedBuild desc{};HairInput hair;std::string error;
    if(!ownerScope||list!=ownerScope->list||!CopyChecked(&params,supplied,sizeof(params))
        ||params.version!=0x10020||params.postCount||params.post||!CopyChecked(&desc,params.desc,sizeof(desc))
        ||!ReadHairInput(ownerScope->owner,desc.inputs,hair,error,&ownerScope->reuse)) {
        RejectReason(error.empty()?"build owner/prebuild/layout not established":error);return -1;
    }
    // An unchanged topology may skip the engine's prebuild query on an update.
    // BuildTriangles still requires the exact owned AS generation, fixed
    // topology/flags and independently sufficient AS/scratch allocations.
    if(!ownerScope->havePrebuild&&desc.inputs.flags!=0x27) {RejectReason("new hair AS has no triangle prebuild");return -1;}
    if(!BuildTriangles(std::move(hair),list,desc,error)) {RejectReason(error);return -1;}
    ConversionSucceeded();
    static std::atomic_flag first=ATOMIC_FLAG_INIT;
    if(!first.test_and_set())single_module::Log(L"WITCHER_DOTS first triangle BLAS recorded (GPU completion and game image not yet verified)");
    return 0;
} catch(...) {StopConversions();return -1;
}
uintptr_t WINAPI Copy(void* destination,const void* source,size_t bytes) {
    if(!active.load(std::memory_order_acquire)||!At(_ReturnAddress(),profile::kCopyReturnRva))return originalCopy(destination,source,bytes);
    if(!bytes)return originalCopy(destination,source,bytes);
    constexpr size_t stride=sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    if(bytes%stride||bytes>1024ull*1024*stride||!Readable(source,bytes)) {
        StopConversions();RejectReason("hair instance copy bounds not established");return originalCopy(destination,source,bytes);
    }
    try {
        auto& state=S();std::lock_guard lock(state.instanceLock);
        const bool traced=HairTracedNow()&&GetTickCount64()>=traceAfterTick.load(std::memory_order_relaxed);
        // Allocated before publishing any game gate. Chunking bounds CPU storage
        // even for a large scene and requires no allocation on the render path.
        for(size_t at=0;at<bytes;) {
            const size_t part=std::min(bytes-at,state.instanceWorkspace.size()*stride);
            auto instances=std::span(state.instanceWorkspace.data(),part/stride);
            {
                HookTimer timer; // DOTS's own work only, not the game's copy.
                cpu_profile::Timer profile(cpu_profile::Part::Instances);
                if(!part||!CopyChecked(state.instanceWorkspace.data(),static_cast<const std::byte*>(source)+at,part)) {
                    StopConversions();RejectReason("hair instance source changed during copy");
                    return originalCopy(destination,source,bytes);
                }
                if(!PrepareInstances(instances,traced)) {
                    StopConversions();for(auto& entry:instances)if(entry.InstanceMask&0x80)entry.InstanceMask=0;
                }
            }
            originalCopy(static_cast<std::byte*>(destination)+at,instances.data(),part);at+=part;
        }
        // This verified caller ignores memcpy's return; preserve its usual value.
        return reinterpret_cast<uintptr_t>(destination);
    } catch(...) {StopConversions();RejectReason("hair instance copy failed");return originalCopy(destination,source,bytes);}
}
bool VerifyProfile(std::string& error) {
    auto& state=S();
    std::ifstream file(std::filesystem::path(state.executable),std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=profile::kFileSize) {error="unsupported game executable size";return false;}
    std::vector<std::byte> data(profile::kFileSize);file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()))||!HashEquals(data,kGameHash)) {
        error="unsupported game executable SHA-256";return false;
    }
    return profile::ValidateMapped(state.game,error);
}
bool InstallGameHooks(std::string& error) {
    const auto* base=reinterpret_cast<const std::byte*>(S().game);
    const std::array<void*,4> hooks{reinterpret_cast<void*>(&Builder),reinterpret_cast<void*>(&Prebuild),reinterpret_cast<void*>(&Build),reinterpret_cast<void*>(&Copy)};
    auto& targets=gameTargets;auto& enabled=gameEnabled;
    for(size_t i=0;i<targets.size();++i)targets[i]=const_cast<std::byte*>(base+profile::kEntries[i].rva);
    originalBuilder=reinterpret_cast<BuilderFn>(targets[0]);originalPrebuild=reinterpret_cast<PrebuildFn>(targets[1]);
    originalBuild=reinterpret_cast<BuildFn>(targets[2]);originalCopy=reinterpret_cast<CopyFn>(targets[3]);
    if(DetourTransactionBegin()!=NO_ERROR||DetourUpdateThread(GetCurrentThread())!=NO_ERROR
        ||DetourAttach(reinterpret_cast<PVOID*>(&originalBuilder),hooks[0])!=NO_ERROR
        ||DetourAttach(reinterpret_cast<PVOID*>(&originalPrebuild),hooks[1])!=NO_ERROR
        ||DetourAttach(reinterpret_cast<PVOID*>(&originalBuild),hooks[2])!=NO_ERROR
        ||DetourAttach(reinterpret_cast<PVOID*>(&originalCopy),hooks[3])!=NO_ERROR
        ||DetourTransactionCommit()!=NO_ERROR) {
        DetourTransactionAbort();error="game hook relocation/publication failed";return false;
    }
    enabled.fill(true);return true;
}
void AbortPreparation() noexcept {
    active.store(false,std::memory_order_release);
    if(DetourTransactionBegin()==NO_ERROR&&DetourUpdateThread(GetCurrentThread())==NO_ERROR) {
        if(gameEnabled[0])DetourDetach(reinterpret_cast<PVOID*>(&originalBuilder),reinterpret_cast<PVOID>(&Builder));
        if(gameEnabled[1])DetourDetach(reinterpret_cast<PVOID*>(&originalPrebuild),reinterpret_cast<PVOID>(&Prebuild));
        if(gameEnabled[2])DetourDetach(reinterpret_cast<PVOID*>(&originalBuild),reinterpret_cast<PVOID>(&Build));
        if(gameEnabled[3])DetourDetach(reinterpret_cast<PVOID*>(&originalCopy),reinterpret_cast<PVOID>(&Copy));
        if(DetourTransactionCommit()==NO_ERROR)gameEnabled.fill(false);
        else DetourTransactionAbort();
    }
    if(!AbortGpuPreparation())single_module::Log(L"WITCHER_DOTS preparation rollback indeterminate; forwarding bindings retained");
}
bool PublishGates(std::string& error) {
    return profile::PublishGates(reinterpret_cast<uintptr_t>(S().game),profile::kGates,error);
}
// RTX 40 series is Ada: CUDA compute capability 8.9 (desktop, laptop and
// workstation parts). The CUDA device must match the game's D3D12 adapter LUID
// exactly once; any incomplete or ambiguous enumeration is not admitted.
bool AdaAdapter(const LUID& luid,int& major,int& minor) noexcept {
    using InitFn=int(WINAPI*)(unsigned);using CountFn=int(WINAPI*)(int*);using DeviceFn=int(WINAPI*)(int*,int);
    using CapabilityFn=int(WINAPI*)(int*,int*,int);using LuidFn=int(WINAPI*)(char*,unsigned*,int);
    const HMODULE cuda=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!cuda)return false;
    const auto init=reinterpret_cast<InitFn>(GetProcAddress(cuda,"cuInit"));
    const auto getCount=reinterpret_cast<CountFn>(GetProcAddress(cuda,"cuDeviceGetCount"));
    const auto getDevice=reinterpret_cast<DeviceFn>(GetProcAddress(cuda,"cuDeviceGet"));
    const auto getCapability=reinterpret_cast<CapabilityFn>(GetProcAddress(cuda,"cuDeviceComputeCapability"));
    const auto getLuid=reinterpret_cast<LuidFn>(GetProcAddress(cuda,"cuDeviceGetLuid"));
    int count=0;uint32_t matches=0;
    bool complete=init&&getCount&&getDevice&&getCapability&&getLuid&&init(0)==0&&getCount(&count)==0&&count>0;
    for(int ordinal=0;complete&&ordinal<count;++ordinal) {
        int device=0,candidateMajor=0,candidateMinor=0;unsigned nodeMask=0;LUID candidate{};
        std::array<char,sizeof(LUID)> raw{};
        if(getDevice(&device,ordinal)!=0||getCapability(&candidateMajor,&candidateMinor,device)!=0
            ||getLuid(raw.data(),&nodeMask,device)!=0) {complete=false;break;}
        memcpy(&candidate,raw.data(),sizeof(candidate));
        if(candidate.LowPart==luid.LowPart&&candidate.HighPart==luid.HighPart) {++matches;major=candidateMajor;minor=candidateMinor;}
    }
    FreeLibrary(cuda);
    return complete&&matches==1&&major==8&&minor==9;
}
bool Adapter(ID3D12Device5* device,DXGI_ADAPTER_DESC1& desc,ComPtr<IDXGIAdapter3>& memory) {
    const HMODULE module=single_module::LoadSystemModule(L"dxgi.dll");
    using Fn=HRESULT(WINAPI*)(REFIID,void**);
    const auto create=reinterpret_cast<Fn>(GetProcAddress(module,"CreateDXGIFactory1"));
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;
    if(!create||FAILED(create(IID_PPV_ARGS(&factory)))
        ||FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter)))||FAILED(adapter->GetDesc1(&desc)))return false;
    adapter.As(&memory); // Optional: VRAM figures for the overlay.
    return true;
}
}
void BeforeDeviceCreate(const void* caller) noexcept {
    try {
        auto& state=S();
        {
            std::lock_guard lock(state.lock);Preferences();
            if(!state.snapshot.applicable||!state.snapshot.requested||!state.snapshot.crashReportRequested||state.dredEnabled
                ||state.attempted||single_overlay::native::InsideLoader()||(!At(caller,profile::kRendererDeviceReturnRvas[0])&&!At(caller,profile::kRendererDeviceReturnRvas[1])))return;
            state.dredEnabled=true;
        }
        // DRED applies only to devices created after it is configured.
        using DebugFn=HRESULT(WINAPI*)(REFIID,void**);
        const auto debug=reinterpret_cast<DebugFn>(GetProcAddress(single_module::LoadSystemModule(L"d3d12.dll"),"D3D12GetDebugInterface"));
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
        if(!debug||FAILED(debug(IID_PPV_ARGS(&dred)))) {
            {std::lock_guard lock(state.lock);state.dredEnabled=false;}
            single_module::Log(L"WITCHER_DOTS crash report unavailable: DRED settings interface missing");return;
        }
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        single_module::Log(L"WITCHER_DOTS crash report requested: DRED breadcrumbs, contexts and page faults enabled");
    } catch(...) {}
}
void ObserveDevice(IUnknown* object,const void* caller) noexcept {
    try {
        auto& state=S();
      {
        std::lock_guard lock(state.lock);Preferences();
        if(!state.snapshot.applicable||!state.snapshot.requested||state.attempted||single_overlay::native::InsideLoader()
            ||(!At(caller,profile::kRendererDeviceReturnRvas[0])&&!At(caller,profile::kRendererDeviceReturnRvas[1])))return;
        state.attempted=true;state.snapshot.stage=Stage::Preparing;
      }
        std::string error;
        if(!VerifyProfile(error)) {Reason(Stage::UnsupportedGame,error);return;}
        settingKnown.store(true,std::memory_order_release);
        if(!ResolveNativeDevice(object,state.device)||FAILED(state.device.As(&state.identity))) {
            Reason(Stage::Failed,"native D3D12 device ownership unavailable");return;
        }
        {
            ComPtr<IUnknown> outer;object->QueryInterface(IID_PPV_ARGS(&outer));
            if(outer.Get()!=state.identity.Get())single_module::Log(L"WITCHER_DOTS device reached through a wrapper; native device resolved");
        }
        if(state.dredEnabled) {
            wchar_t temp[MAX_PATH]{};const DWORD length=GetTempPathW(MAX_PATH,temp);
            std::wstring path;
            if(length&&length<MAX_PATH)path=std::wstring(temp)+L"RTXMFG-witcher3-device-removed-"+std::to_wstring(GetCurrentProcessId())+L".txt";
            const bool armed=ArmRemovalReport(state.device.Get(),path);
            {std::lock_guard lock(state.lock);state.snapshot.crashReportArmed=armed;}
            wchar_t line[600]{};
            swprintf_s(line,L"WITCHER_DOTS crash report %s path=%s",armed?L"armed":L"unavailable",path.c_str());single_module::Log(line);
        }
        DXGI_ADAPTER_DESC1 adapter{};ComPtr<IDXGIAdapter3> memoryAdapter;
        if(!Adapter(state.device.Get(),adapter,memoryAdapter)) {Reason(Stage::Failed,"authoritative DXGI adapter lookup failed");return;}
        {std::lock_guard lock(state.lock);state.memoryAdapter=memoryAdapter;}
        {
            std::lock_guard lock(state.lock);state.snapshot.deviceId=adapter.DeviceId;state.snapshot.luid=state.device->GetAdapterLuid();
        }
        using CapsFn=int32_t(WINAPI*)(ID3D12Device*,uint32_t,void*,uint32_t);
        uint32_t lss{};
        const auto caps=reinterpret_cast<CapsFn>(reinterpret_cast<std::byte*>(state.game)+profile::kEntries[4].rva);
        int32_t capsStatus=caps(state.device.Get(),6,&lss,sizeof(lss));
        if(capsStatus==-4) { // NVAPI_API_NOT_INITIALIZED on an early loader route.
            using QueryFn=void*(__cdecl*)(uint32_t);using InitializeFn=int32_t(__cdecl*)();
            const auto module=single_module::LoadSystemModule(L"nvapi64.dll");
            const auto query=reinterpret_cast<QueryFn>(GetProcAddress(module,"nvapi_QueryInterface"));
            const auto initialize=query?reinterpret_cast<InitializeFn>(query(0x0150e828)):nullptr;
            if(initialize&&initialize()==0) {lss=0;capsStatus=caps(state.device.Get(),6,&lss,sizeof(lss));}
        }
        if(capsStatus!=0) {Reason(Stage::Failed,"native LSS capability query failed");return;}
        if(lss&1) {Reason(Stage::NativeLss,"native LSS supported; game path retained");return;}
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_5};
        int major=0,minor=0;
        if(adapter.VendorId!=0x10de||!AdaAdapter(state.device->GetAdapterLuid(),major,minor)) {
            char text[128]{};
            _snprintf_s(text,_TRUNCATE,"requires an RTX 40 series (Ada) GPU; adapter compute capability %d.%d",major,minor);
            Reason(Stage::UnsupportedGpu,text);return;
        }
        if(FAILED(state.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options)))
            ||options.RaytracingTier<D3D12_RAYTRACING_TIER_1_1
            ||FAILED(state.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model)))||model.HighestShaderModel<D3D_SHADER_MODEL_6_5) {
            Reason(Stage::UnsupportedGpu,"requires DXR 1.1 and shader model 6.5");return;
        }
        const HMODULE nvapi=GetModuleHandleW(L"nvapi64.dll");
        using QueryFn=void*(__cdecl*)(uint32_t);using DriverFn=int32_t(__cdecl*)(uint32_t*,char*);
        const auto query=reinterpret_cast<QueryFn>(GetProcAddress(nvapi,"nvapi_QueryInterface"));
        uint32_t driver{};char branch[64]{};
        const auto driverFn=query?reinterpret_cast<DriverFn>(query(0x2926aaad)):nullptr;
        if(driverFn&&driverFn(&driver,branch)==0) {std::lock_guard lock(state.lock);state.snapshot.driverVersion=driver;}
        // Validated from 617.14 onward; any later release is admitted. The UMD
        // image itself is identified independently of release in gpu_runtime.
        if(driver<61714) {
            char text[128]{};
            _snprintf_s(text,_TRUNCATE,"requires NVIDIA driver 617.14 or later (found %u.%02u)",driver/100,driver%100);
            Reason(Stage::UnsupportedDriver,text);return;
        }
        if(!state.shaders.Prepare(state.game,state.directory,error)) {Reason(Stage::Failed,error);return;}
        {std::lock_guard lock(state.lock);state.snapshot.shaderReady=true;}
        state.instanceWorkspace.resize(4096);
        if(!InitializeGpu(state.device.Get(),&state.shaders,error)||!InstallGameHooks(error)) {AbortPreparation();Reason(Stage::Failed,error);return;}
        // All shader translations, hooks, converter state and budgets are ready
        // before the first game gate is published. The logical getter is last.
        active.store(true,std::memory_order_release);
        if(!PublishGates(error)) {AbortPreparation();Reason(Stage::Failed,error);return;}
        wchar_t line[512]{};
        swprintf_s(line,L"WITCHER_DOTS prepared pid=%lu gameSHA256=%S gpu=%s vendor=%04x device=%04x capability=%d.%d luid=%08x:%08x driver=%u nativeLSS=0 fourTrianglesPerSegment=1 geometryBudgetMiB=512",
            GetCurrentProcessId(),kGameHash,adapter.Description,adapter.VendorId,adapter.DeviceId,major,minor,
            static_cast<uint32_t>(state.device->GetAdapterLuid().HighPart),state.device->GetAdapterLuid().LowPart,driver);
        single_module::Log(line);
        single_module::Log(L"WITCHER_DOTS stage=prepared; waiting for game hair (game Path Traced Hair setting)");
        {std::lock_guard lock(state.lock);state.snapshot.stage=Stage::Active;}
    } catch(const std::exception& e) {AbortPreparation();Reason(Stage::Failed,e.what());}
    catch(...) {AbortPreparation();Reason(Stage::Failed,"DOTS preparation failed");}
}
Snapshot ReadSnapshot() noexcept {
    Snapshot out{};
    try {
        {std::lock_guard lock(S().lock);Preferences();out=S().snapshot;}
        if(!out.applicable)return out;
        const auto stats=ReadRuntimeStats();out.prebuilds=stats.prebuilds;out.builds=stats.builds;out.updates=stats.updates;
        out.rejected=stats.rejected;out.shaderLibraries=stats.shaderLibraries;out.instanceCopies=stats.instanceCopies;out.geometryBytes=stats.geometryBytes;
        out.trackedLists=stats.trackedLists;out.listLimit=stats.listLimit;out.listCapacityMisses=stats.listCapacityMisses;
        out.prebuildCacheHits=stats.prebuildCacheHits;out.prebuildDriverQueries=stats.prebuildDriverQueries;
        out.fenceDriverQueries=stats.fenceDriverQueries;
        out.inputReuseHits=stats.inputReuseHits;out.inputReuseMisses=stats.inputReuseMisses;
        out.leasesRecording=stats.leasesRecording;out.leasesRecorded=stats.leasesRecorded;
        out.leasesPending=stats.leasesPending;out.leasesAvailable=stats.leasesAvailable;out.leasesUnsafe=stats.leasesUnsafe;
        const uint64_t now=GetTickCount64();
        if(stats.lastBuildTick)out.lastBuildAgeMs=now-stats.lastBuildTick;
        if(stats.lastHairTick)out.lastHairAgeMs=now-stats.lastHairTick;
        out.evictions=stats.evictions;out.liveOwners=stats.liveOwners;out.hairInstances=stats.hairInstances;
        out.poolAllocations=stats.poolAllocations;out.poolReleases=stats.poolReleases;out.poolReturns=stats.reclaims;out.fullRebuilds=stats.fullRebuilds;
        out.hairBlasBytes=stats.hairBlasBytes;out.hairScratchBytes=stats.hairScratchBytes;
        out.declinedWhileOff=declinedWhileOff.load(std::memory_order_relaxed);
        static const uint64_t hz=[] {LARGE_INTEGER f{};return QueryPerformanceFrequency(&f)&&f.QuadPart>0?static_cast<uint64_t>(f.QuadPart):0ull;}();
        if(hz) {
            const uint64_t ticks=hookTicks.load(std::memory_order_relaxed);
            out.hookMicroseconds=ticks/hz*1000000+ticks%hz*1000000/hz;
        }
        out.gameHairTraced=out.stage==Stage::Active&&GameHairTraced();
        {
            auto& state=S();std::lock_guard lock(state.lock);
            const uint64_t elapsed=now-state.hookSampleTick;
            const uint64_t epoch=cpu_profile::epoch.load(std::memory_order_acquire);
            if(epoch!=state.cpuEpoch) {state.cpuEpoch=epoch;state.cpuWindow={};}
            out.cpuProfileEnabled=(epoch&1)!=0;
            if(out.cpuProfileEnabled)state.cpuWindow.Update(now,cpu_profile::Read(),cpu_profile::Frequency());
            out.cpuProfileKnown=out.cpuProfileEnabled&&state.cpuWindow.known;
            out.cpuMsPerSecond=state.cpuWindow.msPerSecond;out.cpuCallsPerSecond=state.cpuWindow.callsPerSecond;
            if(!state.hookSampleTick||elapsed>5000) {
                state.hookSampleTick=now;state.hookSampleMicroseconds=out.hookMicroseconds;state.recentHookTimeKnown=false;
            } else if(elapsed>=1000) {
                // Sum of hook elapsed times across threads, including lock
                // waits: not GPU time, CPU occupancy or a per-frame latency.
                state.recentHookMsPerSecond=static_cast<double>(out.hookMicroseconds-state.hookSampleMicroseconds)/elapsed;
                state.recentHookTimeKnown=true;state.hookSampleTick=now;state.hookSampleMicroseconds=out.hookMicroseconds;
            }
            out.recentHookMsPerSecond=state.recentHookMsPerSecond;out.recentHookTimeKnown=state.recentHookTimeKnown;
            if(state.memoryAdapter&&(!state.memorySampled||now-state.memorySampled>=500)) {
                state.memorySampled=now;
                if(FAILED(state.memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&state.local))
                    ||FAILED(state.memoryAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&state.shared))) {
                    state.local={};state.shared={};
                }
            }
            out.memoryKnown=state.local.Budget!=0;
            out.vramUsage=state.local.CurrentUsage;out.vramBudget=state.local.Budget;out.sharedUsage=state.shared.CurrentUsage;
        }
        if(stats.lost) {out.stage=Stage::Failed;strcpy_s(out.reason,"GPU tracking/fence lost; new conversions stopped");}
    } catch(...) {out.stage=Stage::Failed;}
    return out;
}
bool SetCrashReportRequested(bool requested) noexcept {
    try {
        auto& state=S();std::lock_guard lock(state.lock);Preferences();
        if(!state.snapshot.applicable)return false;
        if(!WritePrivateProfileStringW(L"WitcherDOTS",L"CrashReport",requested?L"1":L"0",state.control.c_str()))return false;
        state.snapshot.crashReportRequested=requested;return true;
    } catch(...) {return false;}
}
const char* StageText(Stage stage) noexcept {
    switch(stage) {
    case Stage::Disabled:return "disabled";
    case Stage::WaitingForDevice:return "waiting for early device";
    case Stage::Preparing:return "preparing";
    case Stage::UnsupportedGame:return "unsupported game build";
    case Stage::UnsupportedGpu:return "unsupported GPU";
    case Stage::NativeLss:return "native LSS";
    case Stage::Failed:return "unavailable";
    case Stage::Active:return "prepared";
    case Stage::UnsupportedDriver:return "unsupported driver";
    }
    return "unknown";
}
const char* ActivityText(const Snapshot& snapshot) noexcept {
    if(snapshot.stage!=Stage::Active)return StageText(snapshot.stage);
    if(!snapshot.gameHairTraced)return "off (game Path Traced Hair setting)";
    if(fallbackShown.load(std::memory_order_acquire))
        return snapshot.lastHairAgeMs<2000?"active (some hair conversions rejected)":"raster fallback (hair conversion rejected)";
    // Admitted hair instances prove the game traced converted hair recently;
    // the TLAS copy may pause in menus.
    if(snapshot.lastHairAgeMs<2000)return "active (tracing converted hair)";
    if(snapshot.builds)return "idle (no hair traced in the last 2 s)";
    return "ready (waiting for game hair)";
}
bool GameProcess() noexcept {
    wchar_t path[MAX_PATH]{};
    const DWORD n=GetModuleFileNameW(nullptr,path,_countof(path));
    if(!n||n>=_countof(path))return false;
    const auto* leaf=wcsrchr(path,L'\\');leaf=leaf?leaf+1:path;
    return !_wcsicmp(leaf,L"witcher3.exe");
}
}
