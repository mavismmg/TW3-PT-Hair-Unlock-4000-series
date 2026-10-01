// Host-side regression for the real addon DLL: temporary-device unload/reload
// must leave published callback addresses valid and register them only once.
#include <Windows.h>

#include <cassert>
#include <cstdio>

#include "dots/game_profile.h"

namespace {
void* callback{};
unsigned registrations{},event_registrations{},unregistrations{};
}

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t api) {
  ++registrations;
  return api == 18;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*) {
  ++unregistrations;
}
extern "C" __declspec(dllexport) const void* ReShadeGetImGuiFunctionTable(uint32_t) {
  // The harness never draws; registration only needs a non-null table.
  static void* table[2048]{};
  return table;
}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*, int, const char* text) {
  std::puts(text);
}
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(uint32_t, void* function) {
  callback = function;
  ++event_registrations;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(uint32_t, void*) {}
extern "C" __declspec(dllexport) void ReShadeRegisterOverlay(const char*, void*) {}
extern "C" __declspec(dllexport) void ReShadeUnregisterOverlay(const char*, void*) {}

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 1;
  constexpr uintptr_t game_base = 0x140000000;
  void* temporary_frames[]{reinterpret_cast<void*>(game_base + 0x1abe742),
                           reinterpret_cast<void*>(game_base + 0x7d1a3)};
  assert(witcher_dots::profile::RendererDeviceCaller(game_base, temporary_frames) == nullptr);
  assert(witcher_dots::profile::RendererDeviceCaller(0, temporary_frames) == nullptr);
  for (auto rva : witcher_dots::profile::kRendererDeviceReturnRvas) {
    void* renderer_frames[]{temporary_frames[0], reinterpret_cast<void*>(game_base + rva), temporary_frames[1]};
    assert(witcher_dots::profile::RendererDeviceCaller(game_base, renderer_frames) == renderer_frames[1]);
  }

  HMODULE first{};
  for (unsigned cycle = 0; cycle < 32; ++cycle) {
    const auto module = LoadLibraryW(argv[1]);
    if (!module || !callback) {
      std::printf("load/registration failed: %lu\n", GetLastError());
      return 2;
    }
    if (!first) first = module;
    assert(first == module);
    assert(FreeLibrary(module));

    HMODULE callback_owner{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(callback), &callback_owner) ||
        callback_owner != first) {
      std::puts("FAIL: callback owner unloaded after temporary-device release");
      return 3;
    }
    // Mimics a callback dispatch after ReShade releases its loader reference.
    // No graphics device is created by this test.
    reinterpret_cast<void(*)(void*)>(callback)(nullptr);
  }
  assert(registrations == 1 && event_registrations == 1 && unregistrations == 0);
  std::puts("PASS: 32 unload/reload cycles; callback remains callable; one registration; both renderer callers admitted, probes rejected");
  return 0;
}
