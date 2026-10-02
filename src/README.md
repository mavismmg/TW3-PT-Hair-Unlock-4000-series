# The Witcher 3 Path Traced Hair — RTX 40 DOTS

Experimental, build-specific ReShade addon that restores the game's official
triangle fallback for Path Traced Hair on NVIDIA Ada / RTX 40 GPUs.

The implementation converts unsupported Linear Swept Sphere (LSS) hair into
four triangles per segment, translates the verified hair shaders and replaces
only the exact renderer gates required by that fallback. It never reports
native LSS hardware support on RTX 40.

The DOTS implementation is adapted from
[`dashdogy/RTX40MFG-Unlock`](https://github.com/dashdogy/RTX40MFG-Unlock),
commit `49dc07ba00568c4337d7efc79a4b9e6470289d15`, Copyright (c) 2026
Michael Robles, under the MIT License. See `LICENSE.dashdogy-MIT.txt`.

## Supported test configuration

- Executable: `bin/x64_dx12/witcher3.exe`
- Version: `5.0.0.1044392` (Steam 5.00c, release 1.1.0)
- SHA-256: `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`
- Renderer: Direct3D 12
- GPU: NVIDIA Ada / RTX 40
- NVIDIA driver: 617.14 or newer
- Required GPU features: DXR 1.1 and Shader Model 6.5

The addon fails closed if the executable, GPU, driver, runtime, game entry
points or shader hashes differ from the verified profile.

## Manual test

1. Back up `Documents/The Witcher 3/dx12user.settings`.
2. Install `renodx-witcher3-pthairunlock.addon64` beside the DX12 executable.
3. Start the game with Path Tracing, DLSS Ray Reconstruction and HairWorks on.
4. Set **Path Traced Hair** to **On** and load a HairWorks character or animal.
5. Open the ReShade overlay; expand **Support / Diagnostics** for counters:
   - `Backend stage: prepared`;
   - `Game Path Traced Hair: On`;
   - triangle `prebuilds` and `builds` increase;
   - `hair instances` becomes non-zero while hair is visible;
   - the status becomes `active (tracing converted hair)`.
6. Compare the same scene with Path Traced Hair Off and On. Record GPU time,
   VRAM, visual differences and stability through gameplay, cutscenes, fast
   travel, resolution changes and a clean restart.

The first activation deliberately waits about two seconds before admitting
converted hair into ray tracing while the game recreates its hair resources.

The addon remains loaded until process exit because its game hooks and native
D3D12 forwarders must outlive ReShade's temporary-device unload/reload cycles.
Temporary devices are ignored; initialization requires one of the two verified
renderer creation call sites. Restart the game to replace or remove this addon.

Resource ownership is checked against the native device identity after public
ReShade/Streamline unwrapping. This also handles native resources whose
`GetDevice()` was redirected to a ReShade device proxy. Mismatched devices and
unreadable/unwrappable resources remain rejected, with the failing stage shown
in the status detail.

## Release 1.1.0: normal layout, portal/save robustness and performance

Developed on `feature/witcher3-portal-performance`, baseline `c1709db` / release 1.0.0.
Only the normal layout is shipped. Local testing found the indexed experiment
significantly slower; it remains research-only and is excluded from release assets.
The standalone toolchain is MSVC x64 (Clang is not installed here).

- Default candidate keeps the approved converter, closest-hit, prepass and
  rounded-normal source payloads unchanged. Strand count and update frequency
  are unchanged. The duplicate vertex UAV barrier immediately before its
  write-to-read transition is removed; AS/scratch/replay dependencies remain.
- Adapted queue/list isolation from dashdogy / Michael Robles' MIT v1.4.1
  (`866f491f9b899fbe46730c3213d2fe85a8ac8e40`). Extra overlay queues/lists do not
  stop unrelated conversions; unfenced hair submissions still fail closed.
- Operation-local COM/table validation and reference accounting avoid repeated
  region queries/scans. No raw-owner validation cache survives a builder.
- Exact-sized pool allocations, pressure sweeps and optional transition
  headroom: 512 MiB base, at most 1 GiB and budget/12, with at least 1 GiB of
  sampled headroom reserved. DXGI sampling is allocation-pressure-driven,
  throttled to once/second, never permanent frame polling. Unknown budgets keep
  the base cap. Retained AS cap is 1 GiB by default, at most 4 GiB / budget/4.
- Optional session-only GPU timestamps separate sampled conversion and BLAS
  work without waits/flushes. They sample the last conversion in completed
  recordings, not whole-frame GPU cost. Disable CPU/GPU profiling for FPS A/B.
- `-DWITCHER_DOTS_INDEXED_GEOMETRY=ON` builds a separate experimental layout.
  It uses eight unique positions and immutable shared 32-bit index prefixes,
  retaining old versions until recordings/submissions are safe to release.
  Four triangles, order, winding and shading mappings are preserved. Dynamic
  position writes fall from 144 to 96 bytes/segment; static indices add memory,
  so this is not a claim of 33% less total VRAM or 33% more FPS.

Build/test both configurations with `cmake --build build --config Debug` /
`Release` and `ctest --test-dir build -C Debug` / `Release`. The separate
`build-indexed` directory must never overwrite the default candidate silently.
Run `hotfix_tests.exe <witcher3.exe>` to verify the installed profile/DXIL and
`gpu_geometry_tests.exe <DX12 binary directory>` to dispatch both converters
and build both BLASes on real hardware. Host tests cover retention/generation,
unfenced submissions, headroom/overflow and exact indexed triangle mapping.

Acceptance remains manual: same scene/camera/settings, warm 30 s then capture
60 s; cross the portal or load B, return to A, repeat five times. Compare source
FPS and median/p95 frame time plus external displayed-FPS telemetry, retained
resources, VRAM and visual hair/shadows. Copy diagnostics before and after the
transition. No repeatable >5% source-FPS loss or >10% p95 regression on return
to A; no visual regression or crash. GPU utilization alone is not acceptance.

Inter-object batching/async compute and a more aggressive large-hair rebuild
policy are not enabled: dependencies and scene GPU measurements must be proven
first. The current periodic refit/rebuild policy is unchanged. Offline tests
alone do not establish a gameplay FPS gain or a universal portal/save fix.

## Recovery steps

If the game crashes at startup or removes the D3D12 device, remove the addon
and set `PTHairQualityMode=0` under `[Rendering/RT/PathTracer]` in
`dx12user.settings`.

This build does not target RTX 20/30, non-NVIDIA hardware, RTX 50 with native
LSS, or any other Witcher 3 executable revision.
