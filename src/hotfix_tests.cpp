// Synthetic negative/positive checks, plus optional read-only validation of
// an installed executable. DONT_RESOLVE_DLL_REFERENCES never runs game code.
#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "dots/game_profile.h"
#include "dots/shaders.h"
using namespace witcher_dots;

int wmain(int argc,wchar_t** argv) {
    auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,profile::kImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(base);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+0x100);
    nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.TimeDateStamp=profile::kPeTimestamp;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage=profile::kImageSize;
    nt->OptionalHeader.ImageBase=reinterpret_cast<uintptr_t>(base);
    for(auto& p:profile::kEntries)memcpy(base+p.rva,p.before.data(),p.before.size());
    for(auto& p:profile::kGates)memcpy(base+p.rva,p.before.data(),p.before.size());
    memcpy(base+profile::kHairInstanceWriterRva,profile::kHairInstanceWriter.data(),profile::kHairInstanceWriter.size());
    memcpy(base+profile::kOrdinaryInstanceWriterRva,profile::kOrdinaryInstanceWriter.data(),profile::kOrdinaryInstanceWriter.size());
    for(auto ret:profile::kRendererDeviceReturnRvas) {
        base[ret-6]=0xff;base[ret-5]=0x15;
        const int32_t d=static_cast<int32_t>(profile::kDeviceImportRva-ret);memcpy(base+ret-4,&d,4);
    }
    const std::pair<uint32_t,uint32_t> calls[]={{profile::kPrebuildReturnRva,profile::kEntries[1].rva},
        {profile::kBuildReturnRva,profile::kEntries[2].rva},{profile::kCopyReturnRva,profile::kEntries[3].rva}};
    for(auto [ret,target]:calls) {base[ret-5]=0xe8;const int32_t d=static_cast<int32_t>(target-ret);memcpy(base+ret-4,&d,4);}
    size_t stringAt=0x6000;
    for(auto& var:{profile::kPtEnable,profile::kPtHairQuality}) {
        auto* q=reinterpret_cast<uint64_t*>(base+var.object);
        q[1]=reinterpret_cast<uintptr_t>(base+stringAt);strcpy(reinterpret_cast<char*>(base+stringAt),var.group);stringAt+=64;
        q[2]=reinterpret_cast<uintptr_t>(base+stringAt);strcpy(reinterpret_cast<char*>(base+stringAt),var.name);stringAt+=64;
        q[3]=var.flags;q[4]=reinterpret_cast<uintptr_t>(base+var.value);
        base[var.reader]=var.opcode;base[var.reader+1]=0x3d;
        const int32_t d=var.value-var.reader-7;memcpy(base+var.reader+2,&d,4);base[var.reader+6]=0;
    }
    std::string error;
    const auto image=reinterpret_cast<HMODULE>(base);
    assert(profile::ValidateMapped(image,error));
    const auto rejectByte=[&](size_t at) {base[at]^=1;assert(!profile::ValidateMapped(image,error));base[at]^=1;assert(profile::ValidateMapped(image,error));};
    rejectByte(0x100+offsetof(IMAGE_NT_HEADERS64,FileHeader)+offsetof(IMAGE_FILE_HEADER,TimeDateStamp));
    rejectByte(0x100+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER64,SizeOfImage));
    for(auto& p:profile::kEntries)rejectByte(p.rva);
    for(auto& p:profile::kGates)rejectByte(p.rva);
    rejectByte(profile::kHairInstanceWriterRva);rejectByte(profile::kOrdinaryInstanceWriterRva);
    for(auto ret:profile::kRendererDeviceReturnRvas)rejectByte(ret-4);
    for(auto [ret,target]:calls)rejectByte(ret-4);
    for(auto& var:{profile::kPtEnable,profile::kPtHairQuality}) {rejectByte(var.object+0x20);rejectByte(var.reader+2);rejectByte(var.object+0x18);}
    VirtualFree(base,0,MEM_RELEASE);
    assert(!HashEquals({},kGameHash));
    std::string translated;
    assert(!TranslateIr("unknown shader",ShaderKind::ClosestHit,translated,error)&&translated.empty());
    assert(!TranslateIr("unknown shader",ShaderKind::Prepass,translated,error)&&translated.empty());
    std::puts("PASS: hotfix profile, malformed layouts, callers, gates and shader rejection");
    if(argc==1)return 0;
    if(argc!=2)return 2;
    const std::filesystem::path exe=argv[1];
    std::ifstream file(exe,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=profile::kFileSize)return 3;
    std::vector<std::byte> data(profile::kFileSize);file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),data.size());
    if(!file||!HashEquals(data,kGameHash))return 4;
    const HMODULE mapped=LoadLibraryExW(exe.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!mapped||!profile::ValidateMapped(mapped,error)) {std::printf("FAIL profile: %s\n",error.c_str());return 5;}
    ShaderCache cache;
    if(!cache.Prepare(mapped,exe.parent_path().wstring(),error)) {std::printf("FAIL shaders: %s\n",error.c_str());return 6;}
    for(size_t i=0;i<kShaderRvas.size();++i) {
        const auto size=i<2?kClosestSize:kPrepassSize;
        const auto* p=reinterpret_cast<const std::byte*>(mapped)+kShaderRvas[i];
        assert(!cache.Replacement(p,size).empty());
        std::vector<std::byte> copy(p,p+size);
        assert(!cache.Replacement(copy.data(),copy.size()).empty());
        copy[size/2]^=std::byte{1};assert(cache.Replacement(copy.data(),copy.size()).empty());
        assert(cache.Replacement(p,size-1).empty());
    }
    assert(!cache.Converter().empty());
    FreeLibrary(mapped);
    std::puts("PASS: exact installed 5.00c executable, four shader identities, DXIL translation/finalization/validation and converter");
    return 0;
}
