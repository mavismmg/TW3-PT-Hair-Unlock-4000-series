# Steam 5.00c local compatibility candidate

Branch: `fix/witcher3-5.00c-compatibility`. No push or release.

Target: `5.0.0.1044392`, executable SHA-256
`9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Release addon SHA-256:
`CB8F6B3CD1E0C55F6BFB18C71C004E9716C97668B042CF4BA5935F0D0F245E74`.

## Follow-up: diagnostic-only CPU stage timings

The tester reports lower GPU utilization with `d4fd4c0`. Its screenshot shows
active converted hair, five live owners/instances, no rejection/capacity miss,
1330 size-cache hits versus 10 queries, and 690.95 aggregate hook ms/s.
This does not establish CPU occupancy, a particular bottleneck, or a quantified
performance regression without same-scene FPS/frame-time comparison.

This candidate adds elapsed-time/call-rate rows under `CPU diagnostics
(experimental)`: prebuild, build, TLAS preparation, input validation, resource
unwrapping, resource-device identity, build/TLAS mutex wait, list validation and
driver command recording. Nested rows overlap and must not be summed. Timers
include waits and aggregate across threads; they do not measure GPU execution.
The instrumentation itself has a small CPU cost and is diagnostic, not a fix.
No shader, geometry, GPU command, synchronization or lifetime policy changed.

Keep the panel open for at least a second, then capture its rows and FPS/GPU
utilization in a static scene with hair On and Off. Repeat with the previous
backed-up candidate after a complete restart to quantify the alleged regression.
No utilization improvement is claimed before these measurements.

## Follow-up: tracking exhaustion and CPU-side optimization

The runtime log after the first candidate recorded `list tracking capacity
reached` immediately before `native command list unwrapping unavailable`.
Hair builds had succeeded before this event. This explains the later raster
fallback; menu activation alone did not establish ongoing traced hair.

- Increased retained-list capacity from 256 to 2048; expanded the lock-free
  index and original-method publication tables together. Capacity misses and
  occupancy are now visible. Retention remains bounded and is not a general
  reclamation solution; long sessions can still exhaust it. Freeing privately
  instrumented lists requires proof that CPU forwarders and GPU leases no
  longer reference them, and is intentionally not forced by this patch.
- Added a 64-entry, per-device exact prebuild-size cache keyed by triangle
  vertex count and build flags. Invalid/failed queries are not cached. This
  only avoids repeated CPU driver queries; geometry, allocations' minimum
  sizes, GPU dispatches and BLAS build/refit policy are unchanged.
- Reused BLAS/scratch descriptions within one conversion instead of querying
  the same immutable description repeatedly.
- Kept the full command-list vtable/protection validation before injection,
  removing its duplicate scan during wrapper resolution. Retained list origin,
  identity, lifetime and device ownership remain established by native hooks.
- Added operation-local fence snapshots. Older completed values can delay
  reuse, never authorize incomplete work. No CPU waits, GPU ordering changes,
  submission changes or cross-operation completion cache were added.
- Fixed missing rejection counts and stale displayed hair-instance counts.
  Added recent aggregate hook elapsed time (including lock waits), prebuild
  cache/query counts and completion-query counts. These are not GPU timings
  or per-frame/end-to-end latency measurements.

No shader, rounded-normal, converter or geometry payload changed from
`c846a05`. The low GPU utilization itself is **not yet confirmed fixed**.

Five CTest suites passed in Debug and Release. The new test exercises the real
internal indices with 2048 entries, two full publication generations per list,
concurrent reads, altered identities/hooks, exact cache keys, failed queries,
bounded replacement, per-device separation and conservative fence snapshots.
The optional installed-game DXIL/profile validation also passed.

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
- All five CTest suites passed in both configurations: geometry, device
  identity, 32-cycle addon lifetime, hotfix compatibility and runtime capacity/cache.
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
6. Toggle hair Off/On and reload a save repeatedly: `capacity misses` must stay
   zero, builds must resume, and `active (tracing converted hair)` must persist
   while hair is visible. Capture list occupancy, recent hook elapsed time and
   prebuild cache hits/queries along with the same-scene FPS/GPU utilization.

If unstable, close the game and remove the candidate addon. Restore the backed
up settings or set `PTHairQualityMode=0`. Do not keep forcing device-removal
errors. Publication awaits a successful runtime test and approval.
