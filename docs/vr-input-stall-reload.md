# Reload parts across input stalls

War Pig (`bog_b`) city exterior could lose an AK magazine without a physical
release. The protected DXVK diagnostic run recorded `held=29/reserve=270`
becoming `held=0/reserve=299`, with unchanged rear ownership. Input publication
paused for 3200 ms while focus, reference generation and both tracked grips/aims
remained valid. The subsequent cancellation came from the reload controller's
continuity interruption, not camera visibility or a rejected native ammo write.
The preceding city control also lost escrow before screenshots/full status;
diagnostic capture is therefore one possible stall trigger, not a required one.

`controller_input::frame::retention_generation` now distinguishes physical
ownership/tracking boundaries from elapsed sampling time. Motion continuity,
digital generations, fire freshness and neutral rearming retain their existing
guards. Only an already detached magazine in the same still-pinching tracked
hand can survive a timing-only continuity boundary. The resume sample resets
motion/contact history and requires withdrawal before insertion. It performs no
ammunition transaction and cannot replay a catch, insertion or trigger edge.
Focus/tracking loss, recentering, weapon/rear-hand changes, UI transitions and
physical release continue to interrupt and settle the existing escrow.

The reload presenter previously timestamped a newly solved native skeleton with
the older tracking sample's time. After a slow frame, its generated magazine,
live cartridge and M4 receiver partitions could fail the 150 ms presentation
gate even though the matching native source bones had just been hidden. The
presentation timestamp now records skeleton publication time. Mechanical scene
input keeps its original timestamp, and presentation lifetime/ownership/state
checks keep their existing limits.

Focused regression: `tests/vr/input_stall_reload_tests.cpp`, using real ammo
transactions for M4/AK and both controlling hands, including the MWR rebased
profiles. It also runs the existing attachment eligibility and button-magazine
regressions. Protected game captures and build/test/install manifests are under
`D:/overlordmwr/port/tmp/warpig_exterior_fix_20261117`.

Mock captures cannot establish real-headset acceptance of the original
intermittent receiver/chamber visibility report.
