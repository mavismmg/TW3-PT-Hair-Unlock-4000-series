# Release 1.1.0 — Normal PT Hair build, CPU and resource-pool improvements

Experimental standalone ReShade addon for The Witcher 3 Path Traced Hair on
NVIDIA RTX 40 / Ada, using the DOTS triangle fallback rather than native LSS.

## Changes

- Ships only the approved normal layout: 12 vertices and four triangles per
  hair segment. The indexed experiment performed significantly worse in local
  comparison and is not included in the release assets.
- Reduced repeated memory-region, COM and command-list validation work using
  operation-local snapshots and reference accounting. Current protections,
  resource identity and conservative fallbacks remain checked.
- Conversion buffers are sized for the current hair rather than the largest
  retained owner from a previous save. Best-fit reuse and pressure sweeps reduce
  unnecessary retention/allocation pressure.
- Geometry pool retains its 512 MiB base cap; bounded transition headroom is
  allowed only with sufficient sampled VRAM budget. Unknown budgets keep the
  conservative cap. Resources still in use by the GPU are never released early.
- Expanded tracked queues to 32 and isolated unrelated overlay queue/list
  failures. Unfenced hair submissions still stop new conversions safely.
- Removed the redundant vertex UAV barrier adjacent to the write-to-read
  transition; AS, scratch and recording-reuse dependencies remain intact.
- Added clearer budget/allocation/tracking failure diagnostics and optional
  session-only GPU conversion/BLAS timings without waits or flushes.

Approved converter, closest-hit, prepass and rounded-normal payloads are
unchanged. No reduction in strand count, geometry density or update frequency.
No changes to MFG Unlock, Reflex, FPS caps or game settings.

## Requirements

- NVIDIA RTX 40 / Ada; locally tested on RTX 4070 SUPER.
- Direct3D 12, DXR 1.1, Shader Model 6.5 and NVIDIA driver 617.14 or newer,
  subject to runtime validation.
- ReShade with **full add-on support**; tested with ReShade 6.8.0.
- Exact Steam 5.00c DX12 `witcher3.exe` **5.0.0.1044392**, SHA-256:
  `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Other executable revisions, unknown shaders and failed resource checks are
rejected. DX11, RTX 20/30 and non-NVIDIA GPUs are unsupported by this release.

## Installation

1. Close the game and back up `Documents\The Witcher 3\dx12user.settings`.
2. Install ReShade with full add-on support for the DX12 executable.
3. Replace the old addon with `renodx-witcher3-pthairunlock.addon64` in
   `bin\x64_dx12`, beside `witcher3.exe`. Keep the license notices when redistributing.
4. Enable **Path Tracing**, **DLSS Ray Reconstruction**, **NVIDIA HairWorks**
   and **Path Traced Hair** in the game's settings.
5. Load a HairWorks scene and check for `active (tracing converted hair)` in
   the addon's panel. First activation can take approximately two seconds.

Do not combine this with RTXMFG's Witcher DOTS backend or another PT Hair
unlocker. MFG Unlock is not required. Restart before replacing/removing the
addon. If unstable, remove it and restore settings, or set `PTHairQualityMode=0`.

## Validation and limitations

- Six automated suites passed in MSVC Debug and Release.
- Exact installed-game profile, shader translation and DXIL validation passed.
- Real NVIDIA GPU tests passed for converter output and both BLAS builds/refits.
- No game executable, NVIDIA DLL or provider package is redistributed.
- CPU/GPU profiling is off by default; disable it during FPS/frame-time comparisons.

The normal build was selected following local performance comparison with the
indexed experiment. This does not guarantee an FPS increase, resolve all
portal/save degradation, or fix the game's underlying HairWorks issues.
Conversion, BLAS work and tracing still consume CPU/GPU time and VRAM.
Broader gameplay, visual and save/portal stability testing remains encouraged.

## Credits

**dashdogy / Michael Robles** — original DOTS fallback, geometry conversion,
shader translation and renderer integration from
[RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock), MIT-licensed.
Original port baseline: `49dc07ba00568c4337d7efc79a4b9e6470289d15`;
guarded-read and queue/list isolation reference: upstream v1.4.1,
`866f491f9b899fbe46730c3213d2fe85a8ac8e40`.

**mavismmg** — standalone RenoDX/ReShade port, compatibility/ownership/lifecycle
fixes, validation and resource-pool improvements, UI and local testing.

Thanks to RenoDX, ReShade, Dear ImGui and Microsoft Detours contributors.
The package includes the project license and third-party notices.
Unofficial mod; not endorsed by CD PROJEKT RED or NVIDIA.
