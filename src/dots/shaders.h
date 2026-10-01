#pragma once
#include <Windows.h>
#include <objbase.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace witcher_dots {
enum class ShaderKind { ClosestHit, Prepass };
bool TranslateIr(std::string_view original, ShaderKind kind, std::string& translated, std::string& error);
std::array<uint8_t,32> Sha256(std::span<const std::byte> bytes);
bool HashEquals(std::span<const std::byte> bytes, const char* expected);
class ShaderCache {
public:
    // Explicit game-side DXC; never distribute or overwrite its compiler.
    bool Initialize(const std::wstring& directory, std::string& error);
    bool Translate(std::span<const std::byte> source, ShaderKind kind, std::vector<std::byte>& output, std::string& error);
    bool CompileConverter(std::vector<std::byte>& output, std::string& error);
    bool CompileProgram(std::string_view source, const wchar_t* target, std::vector<std::byte>& output, std::string& error);
    bool Prepare(HMODULE game, const std::wstring& directory, std::string& error);
    std::span<const std::byte> Replacement(const void* data, size_t size) const noexcept;
    const std::vector<std::byte>& Converter() const noexcept { return converter_; }
    bool Ready() const noexcept { return !closest_.empty()&&!prepass_.empty()&&!converter_.empty(); }
private:
    bool Disassemble(std::span<const std::byte> source, std::string& text, std::string& error);
    bool Validate(IDxcBlob* blob, std::vector<std::byte>& output, std::string& error);
    HMODULE compilerModule_{}, validatorModule_{};
    Microsoft::WRL::ComPtr<IDxcCompiler3> compiler_;
    Microsoft::WRL::ComPtr<IDxcLibrary> library_;
    Microsoft::WRL::ComPtr<IDxcAssembler> assembler_;
    Microsoft::WRL::ComPtr<IDxcOptimizer> optimizer_;
    Microsoft::WRL::ComPtr<IDxcValidator> validator_;
    std::array<const void*,4> originals_{};
    std::vector<std::byte> closest_,prepass_,converter_;
};
inline constexpr char kGameHash[]="9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51";
inline constexpr char kClosestHash[]="d6bdd62b710a5566db95e4277a7ba94a454ba04f15c65bcbff1c8c81974b4436";
inline constexpr char kPrepassHash[]="736d1986356e2bf49c38fea948bbc38101a6fc67cd9c96d9c9380fd51de30acf";
inline constexpr uint32_t kClosestSize=7804, kPrepassSize=62232;
inline constexpr std::array<uint32_t,4> kShaderRvas{0x33a7468,0x354fa18,0x3261428,0x340ad38};
}
