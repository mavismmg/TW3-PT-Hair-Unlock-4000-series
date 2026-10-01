/*
 * The Witcher 3 Remastered Path Traced Hair fallback for RTX 40.
 * RenoDX integration Copyright (C) 2026.
 * DOTS fallback adapted from RTX40MFG-Unlock, Copyright (c) 2026 Michael Robles.
 * SPDX-License-Identifier: MIT
 */

#define ImTextureID ImU64

#include <Windows.h>

#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "dots/witcher_dots.h"
#include "dots/game_profile.h"
#include "overlay.h"
#include "single_module.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "version.lib")

namespace single_module {

HMODULE LoadSystemModule(const wchar_t* basename) noexcept {
  if (basename == nullptr || *basename == L'\0') return nullptr;
  wchar_t system_path[MAX_PATH]{};
  const UINT length = GetSystemDirectoryW(system_path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return nullptr;
  if (wcscat_s(system_path, L"\\") != 0 ||
      wcscat_s(system_path, basename) != 0) {
    return nullptr;
  }
  if (HMODULE loaded = GetModuleHandleW(system_path); loaded != nullptr) {
    return loaded;
  }
  return LoadLibraryExW(system_path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

void Log(const wchar_t* text) noexcept {
  if (text == nullptr) return;
  char utf8[2048]{};
  const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8,
                                        static_cast<int>(sizeof(utf8)), nullptr,
                                        nullptr);
  if (count > 0) reshade::log::message(reshade::log::level::info, utf8);
}

}  // namespace single_module

namespace {

std::atomic<bool> g_probe_device_logged = false;

void OnInitDevice(reshade::api::device* device) {
  if (device == nullptr ||
      device->get_api() != reshade::api::device_api::d3d12 ||
      !witcher_dots::GameProcess()) {
    return;
  }
  // ReShade also initializes short-lived feature-probe devices. Only admit
  // the same two renderer creation call sites as the upstream device gateway.
  // The callback is deeper in the stack than D3D12CreateDevice, so recover
  // the verified game return address without reading private wrapper fields.
  void* frames[64]{};
  const auto count = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
  const auto* caller = witcher_dots::profile::RendererDeviceCaller(
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
      std::span<void* const>(frames, count));
  if (caller == nullptr) {
    if (!g_probe_device_logged.exchange(true)) {
      single_module::Log(L"WITCHER_DOTS deferred temporary/non-renderer device; waiting for verified renderer creation");
    }
    return;
  }

  auto* native = reinterpret_cast<IUnknown*>(
      static_cast<uintptr_t>(device->get_native()));
  witcher_dots::ObserveDevice(native, caller);
}

void OnOverlay(reshade::api::effect_runtime*) {
  const auto snapshot = witcher_dots::ReadSnapshot();
  hair_overlay::Draw(snapshot, witcher_dots::ActivityText(snapshot),
                     witcher_dots::StageText(snapshot.stage));
}

}  // namespace

extern "C" __declspec(dllexport) constexpr const char* NAME =
    "The Witcher 3 Path Traced Hair - RTX 40 DOTS";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "Experimental build-specific triangle fallback for Path Traced Hair on RTX 40";

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    if (!reshade::register_addon(module)) return FALSE;
    // Game detours and native D3D12 vtable forwarders have process lifetime.
    // ReShade unloads addons after temporary devices, even during boot. Keep
    // this image and its callbacks at the same address through those cycles,
    // matching the lifetime guarantee of the upstream proxy module.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(module), &pinned) ||
        pinned != module) {
      reshade::unregister_addon(module);
      return FALSE;
    }
    single_module::Log(L"WITCHER_DOTS addon pinned until process exit; temporary-device unload/reload cannot invalidate callbacks or hooks");
    reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::register_overlay("Witcher 3 Path Traced Hair", OnOverlay);
  } else if (reason == DLL_PROCESS_DETACH) {
    reshade::unregister_overlay("Witcher 3 Path Traced Hair", OnOverlay);
    reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
    reshade::unregister_addon(module);
  }
  return TRUE;
}

// RenoDX currently compiles one addon.cpp per game target. Keep the imported
// backend as separate, reviewable sources without changing the global build.
#include "dots/geometry.cpp"
#include "dots/shader_ir.cpp"
#include "dots/shader_cache.cpp"
#include "dots/game_profile.cpp"
#include "dots/gpu_runtime.cpp"
#include "dots/witcher_dots.cpp"
