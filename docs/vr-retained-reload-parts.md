# Reload geometry and retained native skeletons

MWR's magazine, live chamber cartridge and M4 receiver partitions are independent
rigid XModels. `physical_reload::present` hides their original DObj groups and
publishes the replacement transforms with that exact object, matrix buffer and
native skeleton epoch. Native visibility retains its hide mask while that same
skeleton is reused. Replacement submission previously rejected a pose older than
150 ms, even when native was still drawing the skeleton with those source groups
hidden. A delay after the solve could consequently leave the hands, stock and
barrel visible while the receiver disappeared.

The retained-pose path is limited to MWR's independently owned weapon DObjs when
the render-record pose service is unavailable. It verifies the owned weapon
instance, resource generation, current DObj matrix pointer and epoch against the
published frame. Only this proof permits replacement geometry to outlive the
sample timeout. Reference, carrier, ammo population, chamber state, grasp, fault,
inactive and backwards-clock checks remain in force. No input or ammunition
transaction accepts an old sample, and no different skeleton is substituted.

A second timeout path affected the ammunition ledger. Carry suspends interaction
when its camera/body publication is older than 150 ms, even with continuous raw
controller tracking. Its generic suspension reached physical reload's lifecycle
interrupt before the gesture's input-stall retention could run. In the final
DXVK regression this naturally returned a held AK magazine's 29 rounds to reserve
(270 to 299), with the same rear lease; the log reports `gameplay suspended`.

The lifecycle now preserves only an already held, still pinched magazine when
the live carrier, input retention/reference generations, tracked supply hand,
HMD availability and unpaused gameplay remain valid. It performs no ammunition
write or acquisition while waiting. A controller suspension flag forces motion
and button rebasing on the next fresh sample, with mandatory withdrawal before
insertion. Actual UI, tracking, focus, reference, ownership, release and stroke
interruptions keep their cleanup behavior.

## Evidence, 2026-11-17

Artifacts are under `port/tmp/warpig_rootcause_20261117` in the parent workspace.
Tests loaded the user's latest `bog_b` checkpoint near (3795, -2294, -66), with
native FL_GODMODE verified before testing. All original root/private save and
configuration files were backed up first.

- Actual H1 surface packing uses frontend pointer `0x15182A680`, cursor
  `+0x540c14`, and a 262144-byte limit. Native builds did not fail in the observed
  scene; cursor peaks stayed below 69 KB during the tests. Existing H2 region-observer offsets are
  not evidence about this MWR arena.
- A temporary, bounded 220 ms delay after pose publication reproduced a missing
  M4 receiver with fresh tracking and retained hands/stock/barrel. Chamber state
  remained 1 and no magazine was inserted. See `solve-delay-220ms.png` and its
  status/console logs. The receiver/chamber submission gates recorded timeout
  rejections; native model packing failures remained zero.
- The same controlled delay with the retained-skeleton check produced the full
  receiver in `retained-220ms.png`, with successful retained replacement draws.
  A subsequent shot changed chamber 1 to 0 with no magazine feeding it.
- An AK's physically detached 29-round magazine survived rotation, controller
  walking, status/capture and the same delayed-pose test. It is visible in the
  left hand in `retained-ak-delayed.png`; reserve stayed 270, chamber stayed 1.
- After the additional lifecycle correction, final DXVK PID47624 passed the
  same rotations/walking and a 5 FPS carry-suspension/resume test. Logs recorded
  retained pose waits with raw input ages 241, 3 and 2 ms; held stayed 29,
  reserve 270 and chamber 1. `final2-dxvk-90fps-resumed.png` shows the magazine
  still in the left hand after returning to 90 FPS. The native M4's real
  chamber-only shot also passed again in the final binary.
- The injected delay and its command were removed before the shipping build.
  CPU coverage includes all counted-magazine profiles, both hands, all four
  attachment kinds and real lifetime/state invalidation, plus the existing
  64 input-interruption cases, 64 pose-suspension cases and button-magazine cases.

The user's desktop recording also shows a missing M4 receiver around 140 s, but
it is not a separately captured OpenXR eye. The controlled mock establishes this
timing fault and its correction; the user's natural headset occurrence still
requires acceptance in actual play. Severe stalls can separately suppress the
entire tracked weapon through input freshness; that is not this partial-geometry
fault and its input checks are unchanged.
