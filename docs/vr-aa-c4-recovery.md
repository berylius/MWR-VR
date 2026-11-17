# AA transitions and scripted C4 ownership

## Antialiasing

DXVK's different adapter identity makes the game run hardware optimization
again. A saved SMAA T2x selection followed by **Yes** in that dialog was observed
to become None at startup. Native optimization runs before the renderer's
`r_postAA` enum is registered. The mod now captures an explicit saved AA choice
from the existing profile and restores it after the byte-verified native
optimization function (`0x1403A5E90`). A live registered enum takes precedence.
This applies to both DXVK and native D3D11, including a switch back from DXVK,
and preserves an explicit None choice as well;
profiles without an AA assignment keep the native recommendation.

Launching with `r_postAA=None` leaves native SMAA targets 14/15 and the AA
history targets unallocated. The earlier transition guard checked only target
64 and temporarily drew SMAA 1x, which still needs 14/15. A None-to-T2x switch
therefore reached the native blend-weight pass with a null texture and raised
`Tried to use '(null)' when it isn't valid` (native return site 0x140072DF3).

The guard now selects FXAA while the spatial targets are absent, or SMAA 1x
while only temporal/Filmic inputs are absent. The same effective mode governs
VR history isolation, display-target validation and AA execution. Otherwise
history validation can reject an eye before the native reset creates its buffers.
The original scene-record mode is restored after dispatch, preserving the
user's requested setting.

Native `vid_resize` recreates these targets without DXGI ResizeBuffers, so the
normal VR resize callback does not run. Once the requested targets exist, a
bounded main-thread handoff invokes the existing `vr_reinit` command once.
It never enters the runtime from the AA dispatcher's GPU-context lock. The
diagnostic adds `vr_reinitializations` and `optimal_preservations` beside the
transition counters.

Validation: `post_aa_fallback_tests.cpp` covers missing spatial targets, missing
temporal input, each history target and all supported selections. Release
build passed. DXVK/MockXR, bog_a: startup None with targets 14/15/64 absent;
switch to SMAA T2x, then Filmic SMAA T2x. Both eyes resumed with the selected
mode, no AA validation failures, and 13,122 completed stereo pairs. Two pairs
were lost during the renderer/runtime reset; that count remained stable.
Headset acceptance of the brief reinitialization is pending.
The final DXVK startup test with saved T2x and Yes on optimization retained
mode 3 and created targets 14/64 before the first rendered frame. It completed
3,989 stereo pairs with zero transport or AA failures (both eyes mode 3).

Follow-up (2026-11-15): returning to native D3D11 reproduced the same startup
reset with the old DXVK-only preservation guard. The guard now applies to both
renderers. Native D3D11 + Yes retained mode 3 and logged one preservation.
Native MockXR map loading also encountered device removal `0x887A0005`, including
with AA None; native headset/world validation is still pending. The updated
DXVK build rendered 2,056 stereo pairs with both eyes mode 3 and zero AA or
transport failures.

## C4 detonator

A held rifle reselects its native weapon, so requiring the currently selected
weapon to be `c4` drops the scripted detonator. The detonator is now retained
when the owned C4 token is empty (zero clip and reserve), as in bog_a's planted
charge. Ammo-bearing, unselected placeable C4 keeps the existing exclusion.
The native ammunition observer remains authoritative; this does not grant ammo.

A trigger press no longer permanently hides the detonator. The mission script
still decides whether detonation is accepted and when C4 is removed.

Validation: the user's bog_a save, DXVK/MockXR. Right-hand M4 selected natively
while holder=0 remained active. At 451 units from the charge, a left-trigger
press sent one notification; entity 1294 changed from `vehicle_zpu4` to
`vehicle_zpu4_burn`, and the `weapon_c4` entity disappeared. Other campaigns'
empty placeable C4 cases and headset hand fit are not validated by this run.
Final captures with a right-hand M4 and two left-wrist orientations also show
the detonator held in the left palm, including after native rifle reselection.
