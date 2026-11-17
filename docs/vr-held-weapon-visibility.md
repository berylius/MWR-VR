# Held weapon visibility

Held weapons always receive the visibility exemption. There is no setting or
console toggle.

The exemption selects the exact independently owned DObjs for guns currently
held in either hand, including a gun held by its support grip. Both native hand
scene identities (3999/3998) are supported. Stored guns, world guns, NPCs,
scripted native viewmodels and empty hands keep their existing visibility rules.

At the existing native scene preparation hook (`0x14053A4E0`), after native
admission, each selected record runs the native skin preparation state machine
(`0x140571320`). Completed work is reused. Only records with successfully packed
surfaces (state 5 or higher) receive camera visibility 1 in the primary DObj
visibility bank. Failed skinning (state 4), unavailable models, rejected tracking,
and intentionally hidden parts remain excluded. The surface generator consumes
this shared camera visibility for stereo rendering.

MWR scene records start at `0x151C55978`, have stride `0xB0`, and contain state
at `+0x48`, packed entity identity at `+0x90`, and DObj at `+0x98`. The primary
visibility bank is `0x151C6B978` (512 entries). The bridge only visits the native
job's `[+0xC,+0x10)` range. Hook and skin entry bytes are checked at startup.
Native `R_AddViewmodel` pre-skins viewmodels before this hook; the explicit
state-machine call also handles a culled selected record without manufacturing
surfaces or exposing failed work.

This bypasses CPU visibility admission for those held guns. It preserves mesh
backface culling, near-plane clipping, depth occlusion, LODs, surface limits,
model transforms, hide bits, and shadow visibility. It cannot fix a disappearing
gun caused by missing geometry, a driver fault, or exhausted native surface
storage. There is no claim that the reported AMD disappearance has been
reproduced or that culling was its cause.

The existing hand diagnostic status (also included in `vr_status`) adds
`held_weapon_visibility` counters: selected records,
admitted records, previously culled records recovered, and packing failures.
Counters are cumulative.

Offline regression: `tests/vr/held_weapon_visibility_tests.cpp` covers both hand
identities, a support-grip-equivalent owned object, foreign/storage/null objects,
unowned selection, failed/incomplete skinning, bounded worker ranges, and the
last scene slot. Real-headset acceptance at view edges and in the reported
disappearance scene remains necessary.

## Held spare magazines

Magazines drawn from the supply area, removed from a gun, or recovered from the
world receive the same always-on camera admission while held. This includes
their counted rounds. The same policy covers seated counted magazines, live
chamber cartridges, receiver liners and the M4/MP5 split receiver pieces. Released
magazines and loose world cartridges retain native visibility.

These parts use rigid XModels rather than the gun's DObj. Each held submission
requests `scene_models::visibility::held_item`; a scoped thread-local flag covers
only its synchronous native filter call. At `0x14053BBFB`, after the four native
frustum tests, the bridge changes a rejected primary view (2) to visible (1) and
decrements the rejected-view count. It leaves the three shadow views unchanged.
Native LOD selection, surface limits, transforms, depth testing and clipping
continue normally. The original eight-byte MOVSS is replayed; the bridge preserves
flags, volatile registers and SIMD state around the helper call. Startup byte
validation rejects incompatible engine code.

`vr_status` includes cumulative `rigid_held_visibility selected/recovered`
counters. `tests/vr/held_magazine_visibility_tests.cpp` checks camera-only policy,
native behavior without the request, invalid input, and executes the actual JIT
bridge against the native stack layout to verify register/flag/SIMD preservation.

## Generated receiver and reload parts

The early frustum exception alone is insufficient: native scene preparation can
replace an XModel's primary visibility with its entity/PVS classification. The
existing preparation hook now also visits the job's rigid range (`+0x14/+0x18`).
Records start at `0x151C6C990`, stride `0x88`, with eight 1,536-byte visibility
banks at `0x151C9F990`. Selection uses the reload presenter's retained lighting
handle addresses, including counted-round aliases, excluding its falling slots.

If another native view already packed the selected model, only primary visibility
is restored. Otherwise the original rigid builder (`0x1405713A0`) must successfully
pack the registered runtime model before it can be admitted. Failed packing,
foreign props, released parts and shadow banks keep their native behavior. This
does not disable material face culling or depth tests.

`held_rigid_visibility selected/admitted/recovered/failed/repacked` appears in the
hand report and `vr_status`. The rigid regression cases in
`held_weapon_visibility_tests.cpp` cover hidden/visible records, shared shadow
packing, rejected packing, foreign records and range bounds.

2026-11-17 checks: native War Pig (`bog_b`) with M4 grenadier stored on the back,
AK's original 29-round magazine physically unlatched at 15 degrees and held in
the left hand; four rotation captures show the gun and detached magazine.
An explicitly forced visibility rejection in the owned mock produced 81 primary
recoveries, including 50 native repacks, with zero failures. This forced check
validates the recovery path; the user's intermittent city disappearance has not
been reproduced naturally. Artifacts: `port/tmp/weapon_visibility_20261117`.
