# The Witcher 3 - Path Traced Hair Unlock for RTX 40

Experimental **ReShade addon** enabling Path Traced Hair on NVIDIA RTX 40
(Ada) through a DOTS triangle fallback. This is more than a menu unlock: it
converts HairWorks strand geometry, builds triangle acceleration structures
and adapts the verified game's hair shaders for tracing.

**The fallback implementation comes from dashdogy / Michael Robles.** This
project ports his MIT-licensed work to a separate RenoDX/ReShade addon, with
ReShade device-lifecycle and native-resource ownership fixes. It does not
claim authorship of the original DOTS implementation.

## Download and installation

[Download the experimental package](https://github.com/mavismmg/TW3-PT-Hair-Unlock-4000-series/raw/refs/heads/main/downloads/TW3-PT-Hair-Unlock-RTX40-experimental.zip)
(addon, instructions and license notices).
[Individual addon file](https://github.com/mavismmg/TW3-PT-Hair-Unlock-4000-series/raw/refs/heads/main/downloads/renodx-witcher3-pthairunlock.addon64).

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
   `active (tracing converted hair)`, increasing triangle builds and non-zero
   hair instances.

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
- Exact verified `witcher3.exe` version **5.0.0.1041720**:
  - PE timestamp: `0x6AB937C4`.
  - Image size: `0x06420000`.
  - SHA-256: `C272B2C2E61F84C758E28FAB69AB2915944DD1E539DBB435FAE9FC67494C7E25`.

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

Runtime counters have confirmed converted hair builds and admission into ray
tracing on the tested RTX 4070 SUPER. They are not a substitute for a controlled
Off/On visual comparison or a complete stability/performance assessment.

- Conversion, BLAS updates and tracing add CPU/GPU cost and VRAM usage.
  The reported test with five live hair owners used approximately **938 MiB**
  across converted geometry, hair BLAS and scratch buffers; this is not a
  fixed memory requirement or total addon overhead measurement.
- [CDPR has acknowledged a HairWorks performance issue](https://support.cdprojektred.com/en/witcher-3/pc/sp-technical/issue/3015/performance-issues-related-to-nvidia-hairworks).
  This addon does not claim to fix that game issue.
- A tester reported lower GPU utilization with PT Hair on the Steam version.
  The cause and whether it is store-specific remain unverified. No fix is
  claimed here.
- The overlay's CPU hook time is **cumulative since launch**, not per-frame
  latency. Use controlled captures to assess performance.

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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Output: `build\Release\renodx-witcher3-pthairunlock.addon64`. Close the game
before building/replacing a loaded addon. Automated tests cover geometry,
native/proxy device identity and 32 addon unload/reload cycles; they do not
simulate the actual renderer or prove visual correctness.

Publication checks: the standalone addon and all three automated tests build
and pass in both MSVC Debug and Release. The distributed, previously tested
addon also passes the 32-cycle unload/reload regression. Packaging does not
change the addon source or install a newly built DLL into the game.

The original RenoDX integration snapshot is
`98934e0f68d1d3ae2027fc6299fb3e8631cc66b4`. The `src/` addon sources are
preserved from that snapshot; this repository adds standalone packaging and
build instructions. No MFG Unlock code or unrelated RenoDX documentation is
published here.

## Credits and licenses

- **[dashdogy / Michael Robles](https://github.com/dashdogy)** - original
  RTX 40 DOTS hair fallback, geometry conversion, shader translation and
  renderer integration from
  [RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock), specifically
  [commit `49dc07b`](https://github.com/dashdogy/RTX40MFG-Unlock/tree/49dc07ba00568c4337d7efc79a4b9e6470289d15/source/native/witcher_dots).
  Copyright (c) 2026 Michael Robles; [MIT license](licenses/dashdogy-MIT.txt).
- **mavismmg** - RenoDX/ReShade addon port, boot lifecycle and proxy-device
  ownership fixes, integration and local testing.
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
