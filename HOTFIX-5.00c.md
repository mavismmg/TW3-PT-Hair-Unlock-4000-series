# Steam 5.00c local compatibility candidate

Branch: `feature/witcher3-save-stable-performance`, from `79eeb54`.
Experimental local candidate. No push or release.

Target: `5.0.0.1044392`, executable SHA-256
`9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.

Release addon SHA-256:
`D268C5B4846DD139FF95998424DDD99BFEFA07BAFBF110BB48079265BC402B37`.

Previous installed baseline (`79eeb54`) and verified backup SHA-256:
`E81F6B3FF27299281E566BDCD044718A440BB7B633B380101633B4E2B0D04A1D`.
Backup: `build/hotfix-backups/E81F6B3FF27299281E566BDCD044718A440BB7B633B380101633B4E2B0D04A1D.addon64.bak`.

## Save-stable performance candidate

The latest capture shows recent conversion, no rejection, and 11 retained
associations versus 5 admitted instances. These are different populations:
this alone establishes neither a failed hair path nor a leak. Input validation
is about 1.56 ms/call (437.18 ms/s, 281 calls/s), compared with about 2.31 ms/call
in the earlier capture. Neither capture is a controlled same-scene benchmark.

- Batch adjacent source and AS fields into separate checked owner snapshots.
  Overflow, page protections and SEH-protected copies remain enforced.
- Retain source frontends, native resources, canonical identities and prepared
  device identity only inside the builder's `OwnerScope`. Prebuild/build may
  share immutable source metadata only after rereading the current descriptor,
  frontend pointers and native identities. Any mismatch takes full validation.
  Nested builders and other threads have independent scopes; no owner or raw
  pointer cache survives the builder, frame or save.
- BLAS/scratch are always reread and fully approved at build time. AS generation,
  capacity, native command list, binding and buffer-state checks are unchanged.
  A matching prebuild/build pair now makes four resource metadata/device queries
  instead of six; an update without a prebuild still takes full source validation
  without adding proof references for a nonexistent reuse.
- Share a conservative fence observation per queue **within** build, Reset,
  reclamation and TLAS operations. Release completed leases' source references
  before evaluating obsolete associations. No cross-operation fence cache,
  camera-based eviction, global save reset or command-list destruction was added.
- Distinguish retained associations, admitted TLAS instances, recording/recorded
  leases, leases waiting for GPU, reusable buffers and unsafe leases in the UI.
- Detailed timers are off by default, session-only and restart their measurement
  window when enabled. Added VirtualQuery, owner snapshot, geometry descriptor
  and resource-reference timing, with ms/s, calls/s and mean us/call. Reference
  rows count instrumented COM blocks (some contain multiple operations); unwrap
  includes its own COM work. Nested rows must not be added together. Basic hook
  elapsed time and resource/capacity counters remain available with timers off.

Shaders, shader translation, converter, geometry and exact provider/game profile
files are byte-for-byte identical to `79eeb54`. Strand counts, four triangles per
segment, rounded normals, dispatches, BLAS update/rebuild frequency and barriers
are unchanged. MFG Unlock, game configuration and `docs/` were not modified.
Original dashdogy / Michael Robles MIT attribution remains intact.

Clean MSVC Debug/Release builds and all five CTest suites passed, followed by
final rebuilds/tests of the candidate. Installed-game exact profile, four shader
identities and DXIL translation/finalization/validation passed in both configs.
Expanded host tests execute real helpers for scoped proofs, descriptor/frontend/
native/device changes, nested/thread-local builders, read failures, protected
copies, balanced references, owner-address reuse, shared resources, Reset,
completed/incomplete/removed fences and bounded owner/lease exhaustion. They do
not submit GPU work or execute gameplay. Clang-cl was unavailable; MSVC was used.

### Required manual acceptance — NOT YET RUN

For each of three fresh sessions, perform five A -> B -> A cycles:

1. Load A, return to the same scene/camera, warm up 30 seconds, capture 60 seconds.
2. Load B, warm up and capture with the same durations.
3. Return to the exact A scene/camera and repeat the measurement.
4. Keep resolution, HairWorks AA, PT, DLSS/MFG, caps and sync identical. Use
   FrameView/PresentMon externally for displayed FPS/pacing; record source FPS,
   median/p95 frame time, VRAM, associations, admitted instances and lease states.
5. Keep detailed timers **off** for FPS comparisons. Collect a separate timed
   diagnostic window (at least a second after enabling) for per-call costs.
6. Compare against the backed-up baseline after a full game restart. Accept
   only with no repeatable source-FPS loss above 5%, p95 regression above 10%,
   visual regression, crash or unjustified accumulating resource retention.

GPU utilization is auxiliary evidence, not the acceptance criterion. Runtime
save stability and performance gains remain unverified; automated passes alone
do not mean the reported problem is fixed. The following sections describe
earlier compatibility candidates, not additional changes to this candidate.

## Follow-up: invocation-local immutable resource metadata

The stage-timing screenshot reports 228.20 prebuild ms/s, 456.21 build ms/s,
448.72 nested input-validation ms/s, and almost zero build/TLAS mutex wait.
Resource unwrap (51.61 ms/s) and identity (43.50 ms/s) do not explain the whole
input cost. Driver command recording is only 2.14 ms/s in this sample.
The measurements locate a costly validation path but do not yet identify its
entire cost or measure GPU execution. In particular, the original identity
subtimer excluded the final resource-device COM Release, which may itself wait.

This candidate captures each resource's immutable description/virtual address
once in the current HairInput and reuses them for its bounds/address checks.
Each retained native resource is still unwrapped and ownership-validated on
every invocation; nothing is cached by raw owner across frames or generations.
A successful prebuild/build pair performs six GetDesc calls instead of twelve,
with the same six virtual-address queries. GPU commands, shaders, geometry,
budgets, synchronization, generation and ownership validation are unchanged.

Two more nested diagnostic rows isolate description/address queries and the
final device Release. Host tests execute the real resource/input helpers and
check query counts, changed resources/addresses, foreign devices, texture
rejection and balanced references. No live FPS/utilization gain is claimed.

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
