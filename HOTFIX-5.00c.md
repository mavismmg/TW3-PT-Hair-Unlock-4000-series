# Steam 5.00c local compatibility candidate

Branch: `fix/witcher3-5.00c-compatibility`. No push or release.

Target: `5.0.0.1044392`, executable SHA-256
`9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Release addon SHA-256:
`2C5F27BA1FB51E89A28A1AD5C380EC88728DC93EC034B394FDBB5078AF72D324`.

## Changes

- Revalidated renderer creation, hair builder, NVAPI wrappers, instance copy,
  instance masks and path-tracing configuration structures.
- Updated hair buffer ownership offsets to the new engine layout.
- Replaced the old ten-gate profile with the two exact gates validated for
  5.00c; native NVAPI LSS support remains untouched.
- Updated exact shader hashes, sizes, locations and the SSA-specific shader
  translation. The native hotfix removed two extended geometry-query sequences
  and changed its normal reconstruction; the port now follows that layout.
- Rounded-normal math is retained, bound explicitly to this shader's endpoint,
  object-space ray-origin/direction and normal-output values. Resource bindings
  and previous-position buffers are verified after DXIL finalization.
- Geometry conversion, four triangles per segment, device identity handling,
  lifetime protection and game/user configuration behavior are unchanged.

The original DOTS implementation remains credited to **dashdogy / Michael
Robles** under MIT. His user-provided `RTXMFG.dll` 1.4.0.42 supplied a useful
independent reference for the new compatibility profile. No reference DLL,
game binary or game shader payload is redistributed.

## Completed checks

- MSVC Debug and Release addon builds.
- All four CTest suites passed in both configurations: geometry, device
  identity, 32-cycle addon lifetime and hotfix compatibility.
- Optional installed-game checks passed in both configurations: exact file
  hash, PE timestamp/image layout, five hook signatures, two gates, device/hair
  callers, configuration layout and both instance-mask writers.
- Four embedded shader identities checked, both shaders translated/finalized
  and validated using the game's DXC/validator, converter compiled/validated.
- Replacement accepts verified copies and rejects modified/incorrect-size
  shaders. Altered layout/caller/configuration fixtures are rejected.

These checks do not execute the game's renderer or establish GPU/pacing gains.
The utilization corrections reported for the reference DLL are not claimed
as verified by this port.

## Runtime test still required

1. Load only this PT Hair implementation, not RTXMFG DOTS alongside it.
2. Start DX12 with the same Path Tracing/Ray Reconstruction/HairWorks settings
   used in the previous successful test. Enable Path Traced Hair manually.
3. Check `prepared`, increasing triangle builds/updates, nonzero hair instances
   and `active (tracing converted hair)` in the addon overlay.
4. Compare Off/On in the same scene. Record FPS, GPU time/utilization, VRAM and
   pacing, along with a close-up of hair and shadows.
5. Test gameplay, a cutscene, fast travel and a clean restart. Report crashes,
   visual changes or a persistent zero conversion counter with `ReShade.log`.

If unstable, close the game and remove the candidate addon. Restore the backed
up settings or set `PTHairQualityMode=0`. Do not keep forcing device-removal
errors. Publication awaits a successful runtime test and approval.
