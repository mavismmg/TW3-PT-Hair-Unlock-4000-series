// Explicit hardware check, not a game benchmark. No game hooks or game code
// execute. Use the installed game's DXC/validator through an explicit path.
#include "shaders.h"
#include "geometry.h"
#include "converter_source.h"
#include "indexed_converter_source.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace witcher_dots;
namespace {
void Check(HRESULT hr,const char* stage) {
    if(FAILED(hr)) {char text[160]{};sprintf_s(text,"%s: 0x%08X",stage,unsigned(hr));throw std::runtime_error(text);}
}
ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(uav)desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"buffer");return resource;
}
void Upload(ID3D12Resource* resource,const void* bytes,size_t size) {
    void* mapped{};const D3D12_RANGE read{0,0};Check(resource->Map(0,&read,&mapped),"upload Map");memcpy(mapped,bytes,size);
    const D3D12_RANGE written{0,size};resource->Unmap(0,&written);
}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&barrier);
}
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=2) {std::puts("Usage: gpu_geometry_tests <game DX12 binary directory>");return 2;}
    ShaderCache shaders;std::string error;
    if(!shaders.Initialize(std::filesystem::path(argv[1]).wstring(),error))throw std::runtime_error(error);
    std::array<std::vector<std::byte>,2> code;
    if(!shaders.CompileProgram(kConverterHlsl,L"cs_6_5",code[0],error)
        ||!shaders.CompileProgram(IndexedConverterSource(),L"cs_6_5",code[1],error))throw std::runtime_error(error);
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device5> device;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i) {
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
        if(desc.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_1,IID_PPV_ARGS(&device))))break;
        adapter.Reset();
    }
    if(!device)throw std::runtime_error("no NVIDIA DXR device");
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,5};
    parameters[1].ParameterType=parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor={0,0};parameters[2].Descriptor={1,0};parameters[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[3].Descriptor={0,0};
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=parameters;
    ComPtr<ID3DBlob> blob,messages;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&messages),"root serialization");
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"root");
    std::array<ComPtr<ID3D12PipelineState>,2> pipelines;
    for(unsigned i=0;i<2;++i) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.CS={code[i].data(),code[i].size()};
        Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipelines[i])),"converter PSO");
    }
    constexpr uint32_t strands=128,perStrand=12,segments=strands*perStrand,sourceCount=strands*(perStrand+1);
    std::vector<StrandVertex> positions(sourceCount);std::vector<uint32_t> starts(segments),indices(segments*12);
    for(uint32_t i=0;i<sourceCount;++i)positions[i]={{float(i%17)/7,float(i/13)/11,float(i%31)/13},0.005f+float(i%5)/1000};
    for(uint32_t s=0;s<segments;++s) {
        starts[s]=(s/perStrand)*(perStrand+1)+s%perStrand;
        for(uint32_t j=0;j<12;++j)indices[size_t(s)*12+j]=SegmentIndex(s,j);
    }
    positions[0].position.x=std::numeric_limits<float>::quiet_NaN();
    positions[31].radius=std::numeric_limits<float>::infinity();positions[60].radius=-1;
    positions[83]=positions[84];positions[120].position={1e30f,1e30f,1e30f};starts[200]=UINT32_MAX;
    auto source=Buffer(device.Get(),positions.size()*sizeof(positions[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto sourceIndices=Buffer(device.Get(),starts.size()*sizeof(starts[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto triangleIndices=Buffer(device.Get(),indices.size()*sizeof(indices[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto grownIndices=Buffer(device.Get(),indices.size()*sizeof(indices[0]),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Upload(source.Get(),positions.data(),positions.size()*sizeof(positions[0]));
    Upload(sourceIndices.Get(),starts.data(),starts.size()*sizeof(starts[0]));Upload(triangleIndices.Get(),indices.data(),indices.size()*sizeof(indices[0]));
    Upload(grownIndices.Get(),indices.data(),indices.size()*sizeof(indices[0]));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList4> list;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    D3D12_QUERY_HEAP_DESC queryDesc{};queryDesc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;queryDesc.Count=8;
    ComPtr<ID3D12QueryHeap> queries;Check(device->CreateQueryHeap(&queryDesc,IID_PPV_ARGS(&queries)),"queries");
    auto timings=Buffer(device.Get(),8*sizeof(uint64_t),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<ComPtr<ID3D12Resource>,2> output,readback,blas,scratch;
    for(unsigned mode=0;mode<2;++mode) {
        const uint32_t vertices=segments*(mode?8:12);const uint64_t bytes=uint64_t(vertices)*sizeof(Vec3);
        output[mode]=Buffer(device.Get(),bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        readback[mode]=Buffer(device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,mode*4);
        list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipelines[mode].Get());
        const uint32_t constants[]{segments,sourceCount,segments,vertices,perStrand};
        list->SetComputeRoot32BitConstants(0,5,constants,0);list->SetComputeRootShaderResourceView(1,source->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(2,sourceIndices->GetGPUVirtualAddress());list->SetComputeRootUnorderedAccessView(3,output[mode]->GetGPUVirtualAddress());
        list->Dispatch((segments+63)/64,1,1);
        Transition(list.Get(),output[mode].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,mode*4+1);
        D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;geometry.Triangles.VertexCount=vertices;geometry.Triangles.VertexBuffer={output[mode]->GetGPUVirtualAddress(),sizeof(Vec3)};
        if(mode) {geometry.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT;geometry.Triangles.IndexCount=segments*12;geometry.Triangles.IndexBuffer=triangleIndices->GetGPUVirtualAddress();}
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS input{};
        input.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;input.Flags=static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(7);
        input.NumDescs=1;input.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;input.pGeometryDescs=&geometry;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size{};device->GetRaytracingAccelerationStructurePrebuildInfo(&input,&size);
        assert(size.ResultDataMaxSizeInBytes&&size.ScratchDataSizeInBytes);
        blas[mode]=Buffer(device.Get(),size.ResultDataMaxSizeInBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,true);
        scratch[mode]=Buffer(device.Get(),std::max(size.ScratchDataSizeInBytes,size.UpdateScratchDataSizeInBytes),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs=input;
        build.DestAccelerationStructureData=blas[mode]->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch[mode]->GetGPUVirtualAddress();
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,mode*4+2);list->BuildRaytracingAccelerationStructure(&build,0,nullptr);
        D3D12_RESOURCE_BARRIER as{};as.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;as.UAV.pResource=blas[mode].Get();list->ResourceBarrier(1,&as);
        // Same topology at a new index-buffer address is legal for refits.
        // Exercise both full builds and updates, including degenerate strands.
        build.SourceAccelerationStructureData=build.DestAccelerationStructureData;
        build.Inputs.Flags=static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(0x27);
        if(mode)geometry.Triangles.IndexBuffer=grownIndices->GetGPUVirtualAddress();
        D3D12_RESOURCE_BARRIER scratchBarrier{};scratchBarrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;scratchBarrier.UAV.pResource=scratch[mode].Get();
        list->ResourceBarrier(1,&scratchBarrier);
        list->BuildRaytracingAccelerationStructure(&build,0,nullptr);list->ResourceBarrier(1,&as);
        list->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,mode*4+3);
        Transition(list.Get(),output[mode].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyResource(readback[mode].Get(),output[mode].Get());
    }
    list->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,8,timings.Get(),0);Check(list->Close(),"Close");
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    ID3D12CommandList* submitted=list.Get();queue->ExecuteCommandLists(1,&submitted);
    // On timeout/Signal failure exit without releasing resources still in use.
    if(FAILED(queue->Signal(fence.Get(),1))||FAILED(fence->SetEventOnCompletion(1,event))||WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)std::exit(5);
    CloseHandle(event);Check(device->GetDeviceRemovedReason(),"device status");
    std::array<Vec3*,2> mapped{};
    for(unsigned mode=0;mode<2;++mode) {const D3D12_RANGE range{0,size_t(segments)*(mode?8:12)*sizeof(Vec3)};Check(readback[mode]->Map(0,&range,reinterpret_cast<void**>(&mapped[mode])),"output readback");}
    for(uint32_t segment=0;segment<segments;++segment)for(uint32_t corner=0;corner<12;++corner) {
        const auto& original=mapped[0][size_t(segment)*12+corner];const auto& indexed=mapped[1][SegmentIndex(segment,corner)];
        if(memcmp(&original,&indexed,sizeof(Vec3))) {std::printf("FAIL GPU exact position at segment %u corner %u\n",segment,corner);return 7;}
    }
    const D3D12_RANGE written{0,0};for(auto& resource:readback)resource->Unmap(0,&written);
    std::printf("PASS: actual NVIDIA GPU DXIL/PSOs, %u segments, all %u triangle vertices bit-equivalent; invalid/collapsed cases; both BLAS builds/refits; single transition write/read dependency\n",segments,segments*12);
    return 0;
} catch(const std::exception& e) {std::printf("FAIL: %s\n",e.what());return 1;}
