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
inline constexpr char kGameHash[]="C272B2C2E61F84C758E28FAB69AB2915944DD1E539DBB435FAE9FC67494C7E25";
inline constexpr char kClosestHash[]="4f2063aca18fdac330cdf5d54d52dfd22b620fc50f84184ef3adaefbcc206e3f";
inline constexpr char kPrepassHash[]="3406beddeabcebcc372e10df365eac5ee03b7aa6e99aa4b1f5d4d252aae7ea6e";
}
