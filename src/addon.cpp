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

const char* YesNo(bool value) { return value ? "Yes" : "No"; }

double MiB(uint64_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void OnOverlay(reshade::api::effect_runtime*) {
  const auto snapshot = witcher_dots::ReadSnapshot();
  const bool active = snapshot.stage == witcher_dots::Stage::Active;
  ImGui::TextColored(active ? ImVec4(0.35f, 1.0f, 0.55f, 1.0f)
                            : ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                     "%s", witcher_dots::ActivityText(snapshot));

  ImGui::SeparatorText("Compatibility");
  ImGui::Text("Witcher 3 process: %s", YesNo(snapshot.applicable));
  ImGui::Text("Fallback requested: %s", YesNo(snapshot.requested));
  ImGui::Text("Backend stage: %s", witcher_dots::StageText(snapshot.stage));
  ImGui::Text("GPU device ID: 0x%04X", snapshot.deviceId);
  if (snapshot.driverVersion != 0) {
    ImGui::Text("NVIDIA driver: %u.%02u", snapshot.driverVersion / 100,
                snapshot.driverVersion % 100);
  }
  ImGui::Text("Converter shader ready: %s", YesNo(snapshot.shaderReady));
  if (snapshot.reason[0] != '\0') ImGui::TextWrapped("Status detail: %s", snapshot.reason);

  ImGui::SeparatorText("Converted Hair");
  ImGui::Text("Game Path Traced Hair: %s", YesNo(snapshot.gameHairTraced));
  ImGui::Text("Triangle prebuilds / builds / updates: %llu / %llu / %llu",
              snapshot.prebuilds, snapshot.builds, snapshot.updates);
  ImGui::Text("Shader libraries translated: %llu", snapshot.shaderLibraries);
  ImGui::Text("TLAS instance copies: %llu", snapshot.instanceCopies);
  ImGui::Text("Retained hair associations / admitted TLAS instances: %u / %u",
              snapshot.liveOwners, snapshot.hairInstances);
  ImGui::Text("Converted geometry: %.1f MiB", MiB(snapshot.geometryBytes));
  ImGui::Text("Hair BLAS / scratch: %.1f / %.1f MiB",
              MiB(snapshot.hairBlasBytes), MiB(snapshot.hairScratchBytes));
  ImGui::Text("Rejected conversions: %llu", snapshot.rejected);
  ImGui::Text("Declined while game setting was Off: %llu",
              snapshot.declinedWhileOff);
  if (snapshot.lastBuildAgeMs != UINT64_MAX)
    ImGui::Text("Last converted build: %llu ms ago", snapshot.lastBuildAgeMs);
  if (snapshot.lastHairAgeMs != UINT64_MAX)
    ImGui::Text("Last traced hair instance: %llu ms ago", snapshot.lastHairAgeMs);

  ImGui::SeparatorText("Performance / Memory");
  if (ImGui::CollapsingHeader("CPU diagnostics (experimental)")) {
    bool profiling = witcher_dots::cpu_profile::Enabled();
    if (ImGui::Checkbox("Enable detailed CPU timings (this session only)", &profiling))
      witcher_dots::cpu_profile::SetEnabled(profiling);
    ImGui::TextWrapped("Elapsed time including waits, summed across threads. Nested rows overlap; do not add them. Not GPU time or CPU utilization.");
    if (profiling) ImGui::TextWrapped("Reference rows count instrumented COM blocks, not individual AddRef/Release calls. Unwrap includes its own COM work.");
    if (!profiling) ImGui::TextUnformatted("Detailed timers are off. Enable only while diagnosing; disable for performance comparisons.");
    else if (!snapshot.cpuProfileKnown) ImGui::TextUnformatted("Collecting: keep this panel open for at least one second.");
    else for (size_t i = 0; i < witcher_dots::cpu_profile::kCount; ++i)
      ImGui::Text("%s: %.2f ms/s; %.0f calls/s; %.2f us/call", witcher_dots::cpu_profile::kLabels[i],
                  snapshot.cpuMsPerSecond[i], snapshot.cpuCallsPerSecond[i],
                  snapshot.cpuCallsPerSecond[i] > 0 ? snapshot.cpuMsPerSecond[i] * 1000.0 / snapshot.cpuCallsPerSecond[i] : 0.0);
  }
  ImGui::Text("Tracked command lists: %u / %u; capacity misses: %llu",
              snapshot.trackedLists, snapshot.listLimit, snapshot.listCapacityMisses);
  ImGui::Text("BLAS size cache hits / driver queries: %llu / %llu",
              snapshot.prebuildCacheHits, snapshot.prebuildDriverQueries);
  ImGui::Text("Queue completion driver queries: %llu", snapshot.fenceDriverQueries);
  ImGui::Text("Scoped input reuse / full validations: %llu / %llu", snapshot.inputReuseHits, snapshot.inputReuseMisses);
  ImGui::Text("Leases recording / recorded / awaiting GPU / reusable / unsafe: %u / %u / %u / %u / %u",
              snapshot.leasesRecording, snapshot.leasesRecorded, snapshot.leasesPending, snapshot.leasesAvailable, snapshot.leasesUnsafe);
  if (snapshot.recentHookTimeKnown)
    ImGui::Text("Recent hair-hook elapsed time: %.2f ms per second (includes waits)",
                snapshot.recentHookMsPerSecond);
  ImGui::Text("Hair-hook elapsed time since launch: %.3f ms",
              static_cast<double>(snapshot.hookMicroseconds) / 1000.0);
  ImGui::Text("Pool allocations / reuses / releases: %llu / %llu / %llu",
              snapshot.poolAllocations, snapshot.poolReturns,
              snapshot.poolReleases);
  ImGui::Text("Full rebuilds / evictions: %llu / %llu",
              snapshot.fullRebuilds, snapshot.evictions);
  if (snapshot.memoryKnown) {
    ImGui::Text("Process VRAM / budget: %.0f / %.0f MiB",
                MiB(snapshot.vramUsage), MiB(snapshot.vramBudget));
  }

  ImGui::SeparatorText("Experimental warning");
  ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f),
                     "RTX 40 uses a triangle fallback, not native LSS hardware.");
  ImGui::TextWrapped(
      "This is restricted to the exact verified executable, Ada GPU, supported "
      "driver and known shader hashes. If validation fails, the addon keeps "
      "the native raster HairWorks path. Remove the addon if the renderer "
      "becomes unstable.");
  ImGui::TextWrapped(
      "Ported from dashdogy/Michael Robles' MIT-licensed RTX40MFG-Unlock DOTS "
      "implementation (commit 49dc07ba00568c4337d7efc79a4b9e6470289d15).");
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
