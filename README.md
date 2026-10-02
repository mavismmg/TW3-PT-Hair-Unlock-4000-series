# The Witcher 3 - Path Traced Hair Unlock for RTX 40

Experimental **ReShade addon** enabling Path Traced Hair on NVIDIA RTX 40
(Ada) through a DOTS triangle fallback. This is more than a menu unlock: it
converts HairWorks strand geometry, builds triangle acceleration structures
and adapts the verified game's hair shaders for tracing.

**The fallback implementation comes from dashdogy / Michael Robles.** This
project ports his MIT-licensed work to a separate RenoDX/ReShade addon, with
ReShade device-lifecycle and native-resource ownership fixes. It does not
claim authorship of the original DOTS implementation.

**Release 1.1.0:** targets the exact Steam 5.00c executable `5.0.0.1044392`.
Ships the **normal, approved 12-vertex layout**, with CPU validation, resource
pool and queue/list tracking improvements. The indexed experiment is not
distributed: local comparison found it significantly slower than the normal build.
This remains experimental and build-specific; that result is not a guarantee
of stability or performance on every system.

## Download and installation

[Download release 1.1.0](https://github.com/mavismmg/TW3-PT-Hair-Unlock-4000-series/releases/tag/v1.1.0)
(addon, instructions and license notices).
[Individual addon file](https://github.com/mavismmg/TW3-PT-Hair-Unlock-4000-series/releases/download/v1.1.0/renodx-witcher3-pthairunlock.addon64).

1. Close the game and back up `Documents\The Witcher 3\dx12user.settings`.
2. Install the **ReShade build with full add-on support** for the game's DX12
   executable. The tested ReShade version is 6.8.0.
3. Copy `renodx-witcher3-pthairunlock.addon64` into `bin\x64_dx12`, beside
   `witcher3.exe`. Keep the accompanying [license notices](licenses/) with
   redistributed copies.
4. Start the DX12 game. Enable **Path Tracing**, **DLSS Ray Reconstruction**,
   **NVIDIA HairWorks**, and then **Path Traced Hair** in the game's menu.
5. Load a scene with HairWorks hair, such as Geralt or Roach. Open ReShade's
   **Witcher 3 Path Traced Hair** panel and check for
   `active (tracing converted hair)`. Additional counters and **Copy diagnostics**
   are under the collapsed **Support / Diagnostics** section.

Do not run this alongside RTXMFG's Witcher DOTS backend or another PT Hair
unlocker. Both would hook the same game/renderer paths. This addon does not
require MFG Unlock; it changes neither DLSS-G quality nor FPS caps.

First activation waits approximately two seconds while hair resources are
recreated. The addon stays loaded until process exit; restart the game before
updating or removing it. [Download details and SHA-256](downloads/README.md).

## Requirements and scope

- NVIDIA **RTX 40 / Ada**; tested locally on an RTX 4070 SUPER.
- Direct3D 12, DXR 1.1 and Shader Model 6.5.
- NVIDIA driver **617.14 or newer**, subject to the runtime checks below.
- Exact verified `witcher3.exe` version **5.0.0.1044392 (Steam 5.00c)**:
  - PE timestamp: `0x6ABD8695`.
  - Image size: `0x063F8000`.
  - SHA-256: `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Other executable revisions, unknown shader/runtime hashes, incompatible GPUs,
and failed resource validation are rejected. A matching version number alone
is not sufficient. RTX 20/30, non-NVIDIA GPUs and DX11 are not supported by
this build. RTX 50's native LSS path is outside its scope. Steam is the tested
installation; compatibility with other stores is not established.

RTX 40 does **not** gain native Linear Swept Sphere hardware support. The
fallback reuses HairWorks positions, radii and segment indices, converting
each segment into four triangles and adapting the verified hair shader
libraries. Unsupported LSS-only shader paths remain disabled.

## Experimental status and performance

Runtime counters confirmed converted hair builds and admission into ray
tracing on 5.00c. The local tester reported recovery of the post-save performance
loss after the builder-scoped/current-page validation changes. Visual shaders,
geometry density and update frequency remain unchanged from the tested baseline.
Broader controlled Off/On comparisons and stability testing are still encouraged;
passing offline shader validation alone does not prove gameplay safety.

- Conversion, BLAS updates and tracing add CPU/GPU cost and VRAM usage.
  The reported test with five live hair owners used approximately **938 MiB**
  across converted geometry, hair BLAS and scratch buffers; this is not a
  fixed memory requirement or total addon overhead measurement.
- [CDPR has acknowledged a HairWorks performance issue](https://support.cdprojektred.com/en/witcher-3/pc/sp-technical/issue/3015/performance-issues-related-to-nvidia-hairworks).
  This addon does not claim to fix that game issue.
- Save-load validation overhead was reduced without retaining raw-owner or
  page-permission caches between frames/saves. Unknown page information uses
  the full checked fallback. This does not establish a Steam-specific engine bug.
- Release 1.1.0 avoids sizing each conversion buffer for the largest retained
  hair owner, sweeps completed/stale resources under pressure and allows bounded
  extra transition headroom only when sampled VRAM budget permits it. Overlay-only
  tracking failures no longer stop unrelated hair conversions; unsafe hair
  submissions still fail closed. No universal portal/save-performance fix is claimed.
- Advanced timings are off by default and session-only. They measure elapsed
  time including waits, with overlapping nested rows, not CPU utilization or
  GPU time. Optional GPU timestamps separately sample conversion and BLAS work
  from completed recordings, without waits or flushes; they are not whole-frame
  GPU timings. Disable both types of profiling for FPS comparisons.

Compare Off/On with identical camera, lighting, resolution and settings. Test
gameplay, cutscenes, fast travel, resolution changes and a clean restart.
Include the addon status and `ReShade.log` when reporting a problem; review
logs for personal paths before sharing.

## Recovery

If the game crashes, close it, remove the addon and restore your backed-up
settings. Alternatively, set `PTHairQualityMode=0` under
`[Rendering/RT/PathTracer]` in `dx12user.settings`. Do not keep forcing the
feature after device-removal errors.

## Build from source

Requires Windows x64, Git, CMake 3.24+, Visual Studio 2022's C++ tools (or
clang-cl with the MSVC toolchain) and Windows SDK 10.0.26100.0 or newer
providing `dxcapi.h`. ReShade and Detours revisions are pinned as submodules.
Only the ReShade `deps/imgui` submodule is needed; no NVIDIA binaries or game
files are included. Runtime DXC/validator libraries come from the game.

```powershell
git clone https://github.com/mavismmg/TW3-PT-Hair-Unlock-4000-series.git
cd TW3-PT-Hair-Unlock-4000-series
git submodule update --init external/reshade external/Detours
git -C external/reshade submodule update --init deps/imgui
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DWITCHER_DOTS_INDEXED_GEOMETRY=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build\Release\renodx-witcher3-pthairunlock.addon64`. Close the game
before building/replacing a loaded addon. Automated tests cover geometry,
native/proxy device identity and 32 addon unload/reload cycles; they do not
simulate the actual renderer or prove visual correctness.

The six test suites also cover scoped input/memory validation, save/resource
address reuse, fence and lease retention, bounded caches and compact/expanded
ImGui rendering at 100% and 150% scale. The clean UI is presentation-only; the
release preserves the approved renderer/converter payloads.
The indexed source experiment remains compile-time opt-in for research only;
it is not recommended and is not included in the release package.

The additional `hotfix_compatibility` test rejects altered PE layouts, hook
entries, gates, callers and configuration structures. Optional validation of
the installed executable runs no game entry point and checks all four embedded
shader identities, translation, DXIL finalization, validation and the converter:

```powershell
.\build\Release\hotfix_tests.exe 'C:\path\to\bin\x64_dx12\witcher3.exe'
```

The original RenoDX integration snapshot is
`98934e0f68d1d3ae2027fc6299fb3e8631cc66b4`. This branch adapts that port to
5.00c and optimizes invocation-local validation; visual geometry conversion and
public device ownership handling are unchanged.
No MFG Unlock code or unrelated RenoDX documentation is published here.

The 5.00c compatibility profile was independently checked against the installed
executable and dashdogy's user-provided `RTXMFG.dll` **1.4.0.42** (SHA-256
`0BFC8C2FA07A309026EED461D57566CDAFFB03E28D38E5CA6DB65445CDCA02DE`).
That DLL is a reference, not a dependency or redistributed binary. The guarded
owner-read optimization was also informed by upstream v1.4.1
([commit `866f491`](https://github.com/dashdogy/RTX40MFG-Unlock/tree/866f491f9b899fbe46730c3213d2fe85a8ac8e40)).
Do not load both implementations together: they hook the same renderer.

## Credits and licenses

- **[dashdogy / Michael Robles](https://github.com/dashdogy)** - original
  RTX 40 DOTS hair fallback, geometry conversion, shader translation and
  renderer integration from
  [RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock), specifically
  [commit `49dc07b`](https://github.com/dashdogy/RTX40MFG-Unlock/tree/49dc07ba00568c4337d7efc79a4b9e6470289d15/source/native/witcher_dots).
  Copyright (c) 2026 Michael Robles; [MIT license](licenses/dashdogy-MIT.txt).
  Additional guarded-read and queue/list isolation reference: upstream v1.4.1, `866f491`.
- **mavismmg** - RenoDX/ReShade addon port, boot lifecycle and proxy-device
  ownership fixes, scoped validation/performance improvements, interface and
  local testing.
- **[Carlos Lopez Jr. / RenoDX contributors](https://github.com/clshortfuse/renodx)**
  - original addon development/build environment; [MIT notice](licenses/RenoDX-MIT.txt).
- **[Patrick Mours / ReShade contributors](https://github.com/crosire/reshade)**
  - add-on API and overlay integration; [BSD notice](licenses/ReShade-BSD.txt).
- **[Omar Cornut / Dear ImGui contributors](https://github.com/ocornut/imgui)**
  - overlay UI API; [MIT notice](licenses/Dear-ImGui-MIT.txt).
- **[Microsoft Detours](https://github.com/microsoft/Detours)** - hooking library;
  [MIT notice](licenses/Detours-MIT.txt).

Project integration is MIT-licensed; third-party notices are preserved in
`licenses/` and the original DOTS license also remains under `src/`.
This is an unofficial mod, not endorsed by CD PROJEKT RED or NVIDIA.
