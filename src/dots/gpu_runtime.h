#pragma once
#include "geometry.h"
#include "shaders.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <span>
#include <string>
namespace witcher_dots {
// ABI declarations for the one verified game/NVAPI profile. This translation
// layer implements only array-layout, one opaque LSS geometry, implicit +1
// segment endpoints and the common DXR build/update flags.
struct AddressStride { uint64_t address{},stride{}; };
struct LssGeometry {
    uint32_t type{},flags{},vertexCount{},indexCount{},primitiveCount{},pad0{};
    AddressStride positions;
    uint32_t positionFormat{},pad1{};
    AddressStride radii;
    uint32_t radiusFormat{},pad2{};
    AddressStride indices;
    uint32_t indexFormat{},endcaps{},primitiveFormat{};
};
struct ExtendedInputs {
    uint32_t type{},flags{},count{},layout{},stride{},pad{};
    const void* geometry{};
};
struct PrebuildParams { uint32_t version{},pad{};const ExtendedInputs* inputs{};D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO* info{}; };
struct ExtendedBuild { uint64_t destination{};ExtendedInputs inputs;uint64_t source{},scratch{}; };
struct BuildParams { uint32_t version{},pad{};const ExtendedBuild* desc{};uint32_t postCount{},pad2{};const void* post{}; };
static_assert(sizeof(ExtendedInputs)==32&&sizeof(PrebuildParams)==24&&sizeof(ExtendedBuild)==56&&sizeof(BuildParams)==32);
static_assert(offsetof(LssGeometry,positions)==24&&offsetof(LssGeometry,radii)==48&&offsetof(LssGeometry,indices)==72);
struct HairInput {
    struct Metadata {D3D12_RESOURCE_DESC description{};uint64_t address{};};
    void* owner{};
    Microsoft::WRL::ComPtr<ID3D12Resource> positions,indices,blas,scratch;
    Metadata positionMetadata,indexMetadata,blasMetadata,scratchMetadata;
    LssGeometry geometry{};
    Plan plan{};
    uint32_t segmentsPerStrand{};
};
struct RuntimeStats {
    uint64_t prebuilds{}, builds{}, updates{}, rejected{}, shaderLibraries{}, instanceCopies{}, geometryBytes{}, leaseReuses{};
    uint64_t lastBuildTick{}, lastHairTick{}; // GetTickCount64 of the latest conversion / admitted hair instance
    uint64_t evictions{};
    uint64_t poolAllocations{}, poolReleases{}, fullRebuilds{}, reclaims{};
    uint64_t hairBlasBytes{}, hairScratchBytes{}; // the game's AS/scratch buffers of live converted hair
    uint32_t liveOwners{}, hairInstances{}; // live associations; hair instances in the latest admitting copy
    uint32_t trackedLists{},listLimit{};
    uint64_t listCapacityMisses{},prebuildCacheHits{},prebuildDriverQueries{},fenceDriverQueries{};
    bool lost{};
};
bool InitializeGpu(ID3D12Device5* device,ShaderCache* shaders,std::string& error);
// Only for failed early preparation, before any hair gate has been accepted.
// Forwarding bindings and retained interfaces remain alive for racing callers.
bool AbortGpuPreparation() noexcept;
void StopConversions() noexcept;
bool ReadHairInput(void* owner,const ExtendedInputs& inputs,HairInput& out,std::string& error);
bool PrebuildTriangles(const HairInput& hair,uint32_t flags,D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& info);
bool BuildTriangles(HairInput hair,ID3D12GraphicsCommandList4* list,const ExtendedBuild& desc,std::string& error);
// Modify a private copy of this game's CPU instance data. Suppress unowned
// hair entries, never mutate an unclassified AS or a non-hair instance. With
// hairTraced false (the game's Path Traced Hair is off) all hair is suppressed.
bool PrepareInstances(std::span<D3D12_RAYTRACING_INSTANCE_DESC> instances,bool hairTraced=true);
// The native D3D12 device behind any forwarding wrapper: public COM unwrapping
// (Streamline, ReShade 6.7+), else a natively created child's GetDevice
// (wrappers without a public unwrap, such as ReShade before 6.7).
bool ResolveNativeDevice(IUnknown* object,Microsoft::WRL::ComPtr<ID3D12Device5>& out) noexcept;
RuntimeStats ReadRuntimeStats();
// Device-removal report (DRED breadcrumbs/page fault plus DOTS state), armed
// only for the prepared DOTS device when the opt-in report is enabled.
bool ArmRemovalReport(ID3D12Device5* device,const std::wstring& path) noexcept;
}
