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
- Version: `5.0.0.1044392` (Steam 5.00c, local hotfix candidate)
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
5. Open the ReShade overlay and verify:
   - `Backend stage: prepared`;
   - `Game Path Traced Hair: Yes`;
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

## Recovery

If the game crashes at startup or removes the D3D12 device, remove the addon
and set `PTHairQualityMode=0` under `[Rendering/RT/PathTracer]` in
`dx12user.settings`.

This build does not target RTX 20/30, non-NVIDIA hardware, RTX 50 with native
LSS, or any other Witcher 3 executable revision.
