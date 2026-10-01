# Release 1.0.0 — RTX 40 Path Traced Hair, Steam 5.00c

Experimental standalone ReShade addon for The Witcher 3's Path Traced Hair on
NVIDIA RTX 40 / Ada. Uses a DOTS triangle fallback, not native RTX 50 LSS hardware.

## Changes

- Updated compatibility for the exact Steam 5.00c DX12 executable.
- Reduced CPU validation overhead when loading/reloading saves. The local
  RTX 4070 SUPER tester confirmed the latest fix worked; results on other
  systems may differ. No GPU-utilization or FPS percentage is guaranteed.
- Reuses validated inputs only within one builder, reads current source/AS
  pointers and validates current page protections with a conservative fallback.
- Preserves shader payloads, hair density, geometry and update frequency from
  the approved performance build. No MFG, Reflex, FPS-cap or game-setting changes.
- Simplified the overlay: essential status up front, technical counters and
  session-only CPU timings under **Support / Diagnostics**, plus **Copy diagnostics**.

## Requirements

- NVIDIA RTX 40 / Ada; locally tested on RTX 4070 SUPER.
- Direct3D 12, DXR 1.1, Shader Model 6.5 and NVIDIA driver 617.14 or newer,
  subject to the addon's runtime validation.
- ReShade with **full add-on support**; tested with ReShade 6.8.0.
- Exact `witcher3.exe` **5.0.0.1044392 (Steam 5.00c)**, SHA-256:
  `9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Other executable revisions or failed resource/runtime/shader checks are rejected.
DX11, RTX 20/30 and non-NVIDIA GPUs are not supported by this release.

## Installation

1. Close the game and back up `Documents\The Witcher 3\dx12user.settings`.
2. Install ReShade with full add-on support for the DX12 executable.
3. Place `renodx-witcher3-pthairunlock.addon64` in `bin\x64_dx12`, beside `witcher3.exe`.
4. Enable **Path Tracing**, **DLSS Ray Reconstruction**, **NVIDIA HairWorks** and
   **Path Traced Hair** in the game's graphics settings.
5. Load a HairWorks character/animal scene. The addon should show
   `active (tracing converted hair)`; first activation can take about two seconds.

Do not combine it with RTXMFG's Witcher DOTS backend or another PT Hair unlocker.
MFG Unlock is not required. Restart the game before replacing/removing the addon.
If unstable, remove it and restore settings, or set `PTHairQualityMode=0`.

## Validation and limitations

- Six automated suites passed in MSVC Debug and Release, including memory
  protections, save/address reuse, fence/lease lifetime and ImGui UI rendering.
- Exact installed-game profile and DXIL translation/finalization/validation passed.
- Visual backend sources are unchanged from the tester-approved build.
- Broader visual/stability testing is encouraged. Hair conversion/tracing still
  costs CPU/GPU time and VRAM; this is not a universal HairWorks engine fix.
- Detailed timers default to Off. Disable them for FPS/frame-time comparisons.
- No game executable or NVIDIA DLL/provider package is redistributed.

## Credits

**dashdogy / Michael Robles** — original RTX 40 DOTS fallback, geometry conversion,
shader translation and renderer integration from
[RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock), MIT-licensed.
Original port baseline: `49dc07ba00568c4337d7efc79a4b9e6470289d15`;
guarded-read performance reference: upstream v1.4.1, `866f491f9b899fbe46730c3213d2fe85a8ac8e40`.

**mavismmg** — standalone RenoDX/ReShade port, compatibility/ownership/lifecycle
fixes, scoped validation improvements, UI and local testing.

Thanks to RenoDX, ReShade, Dear ImGui and Microsoft Detours contributors.
The package includes the project license and all third-party notices.
Unofficial mod; not endorsed by CD PROJEKT RED or NVIDIA.
