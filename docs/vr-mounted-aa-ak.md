# Mounted cameras, AA and AK attachment

The MWR build uses H1's renderer/frontend and native campaign. Generic Overlord
host code is shared; H2-only providers/compiler/frontend services are excluded
by `premake/mwr_host.lua` and the component admission gate.

Mk19 hand aiming follows the rear-grip-to-pintle line. The previous horizontal
reflection reversed that line. Tracking recovery still reanchors without an aim
jump, and native pitch arc limits retain their incremental command behavior.

Mounted camera seats are acquired near their carrier once. A stale native eye
far from an already moving carrier cannot invalidate that acquired seat. Linked
vehicle passengers, including Charlie Don't Surf's UH-60, also use the rendered
vehicle heading even when an ordinary native weapon keeps them out of the
weaponless Mk19 state. Passenger authored eye motion stays native; the mounted
seat is frozen only for hand-aimed turrets. Vehicle model admission excludes
unrelated linked character/rope rigs and physical ladders.
The player-to-vehicle search follows up to eight intermediate parents. Passenger
boarding and taking the gun have different seat identities even on one helicopter.

Heat (`village_defend`) has a separate ground-minigun case: native player origin
orbits the gun as its aim changes. Its seat now follows the rendered turret base
translation, ignoring aim rotation and that orbiting player origin. Physical head
translation and rotation still apply independently; no new setting is required.
In the native D3D11 mock, turret 553 aimed through 61.93 degrees horizontally and
21.80 degrees vertically with identical submitted head position and orientation.
A separate 10 cm head translation plus 20/10 degree yaw/pitch test moved the view
correctly without moving the gun aim, then returned exactly to the resting view.
This Heat check is separate from the earlier Charlie helicopter verification.
The corrected Release was deployed; original root/private profiles were restored.
Actual headset comfort still needs acceptance.

Taking a hand-aimed minigun or Mk19 now seats the current physical head at the
authored gun seat once, cancelling any old play-space translation. Subsequent
head translation and rotation remain fully tracked. This uses the shared camera
and hand frame; the native mounted body still follows native turret physics.
Passenger boarding alone does not apply this gun-seat centring.

The control rig acquires a neutral handle centre 40 cm forward and 30 cm below
the entry head, using horizontal heading. Initial physical hand displacement is
normalized onto that centre; later hand travel drives the gun about its pintle.
Looking down on entry does not tilt the neutral rig. Head movement never reseeds
it. A new gun, manual recenter, or a hand regaining tracking rebinds appropriately.
No new option is needed. `vr_input_status` reports `mounted_neutral_m` and
`hand_offset_m` in tracking-local metres.

Heat's independent grip hands also take ownership ahead of the personal firearm
retained for dismount. That retained weapon previously suppressed their render
submission or made their hands-only skeleton fail firearm-owner admission. Both
gates now recognize the fitted turret. The final native mock capture shows both
hands on the minigun grips, with 811 empty-hand submissions and no rejections.
The moving Mk19 was checked separately through DXVK/Vulkan, retaining its native
hands path and carrier motion. Both runs passed off-centre seating, hand aiming,
and independent 10 cm/20 degree yaw/10 degree pitch head-motion checks.

T2x projection jitter and each eye's AA ring now select the same VR pair phase.
Previously projection samples retained the native frontend frame parity while
history readers advanced at the headset output rate. Repeated/skipped native
phases could mismatch those histories. H1's native two-sample pattern and viewport
amplitudes are retained. Mono/desktop jitter is unchanged. Lost pairs seed the
needed histories with the current eye rather than mixing stale ring contents.
Unrelated renderer bookkeeping adjacent to the global frame counter is no longer
treated as an AA progress structure. `native_post_aa` diagnostics expose
`clock=projection_pair`, `sample_phase` and `history_reuses`.

No DXVK off/on cycle is required to enable AA. AA None remains None; SMAA T2x and
Filmic SMAA T2x use temporal histories. Fixing sampling does not promise that every
subpixel highlight, fence or texture stops shimmering at all headset resolutions.

Launcher > Gameplay > Weapon interaction > **Original Overlord AK magazine
attachment** (`vr_originalAKMagazine`, default off) selects the upstream swept
insertion rail instead of the MWR hook-and-rock gesture. It retains the manual
latch, real ammunition transactions, MWR assets and the longer charging stroke.
The mode is pinned with each mechanical instance; restart the game after changing
it. All admitted AK skin profiles use the option. Other weapon families retain
their profiles. Korean label: **AK 탄창: 원본 Overlord 결합**.

Focused checks are registered as `vr-mounted-aa-ak-tests`, alongside the mounted
camera/hand aim tests. They include skipped/repeated sample phases, zero-jitter
preservation, immutable AK variants, and actual straight insertion/ammunition
accounting for both hands in both attachment modes.

Native armada testing tracked the Black Hawk through the intermediate body rig
with 4,440 follows and no misses. Native T2x completed 4,432 stereo pairs; final
DXVK Filmic T2x completed 3,170 pairs. Both recorded zero AA failures and zero
projection/history phase mismatches. DXVK hardware optimization was answered Yes
and retained the saved AA choice. Screenshots and native command angles were
inspected. Headset comfort, visual shimmer reduction and AK hand fit still need
in-headset acceptance. Original root/private saves and settings were restored.
