# MWR host and Baseplate comparison adaptations

The shipping target admits MWR support providers through premake/mwr_host.lua.
Generic Overlord loader, hook, lifecycle and scheduler implementations are
retained where already rebound to H1. This separates H2-specific dependencies;
it does not claim all inherited infrastructure was rewritten from scratch or
that the native game's renderer frontend was removed.

- H2 GUI/debug-console, updater, mod-loader, custom GSC compiler/Lua runtime,
  pool-expansion and other unported host components are excluded from the target.
- The installer gates construction at compile time, before a legacy constructor
  can run. Runtime component admission remains a second check.
- Shared LUI exposes admitted MWR/VR bindings, excluding H2 updater, mod/stats,
  language restart and achievement services.
- Asset enumeration and zone retirement have a small MWR provider. H2 pool
  expansion/custom zone loading are excluded.
- Script names/IDs use H1 catalogs. The host no longer links the H2 compiler or
  constructs its GSC context. Native campaign GSC remains native.
- The MW2CR Shepherd-ending provider is explicitly absent from this build; it
  was not an admitted MWR component before this change.
- game/mwr/ owns image constants, symbols and typed entry points. Historical
  game/symbols.hpp and vr/h2/ headers forward to these MWR definitions for shared
  source compatibility. H2 addresses in comments are provenance only.

This does not import Baseplate's AppContainer/Steam ABI replacement. Shared
ancestry and compatibility namespace names do not mean an H2 engine is running.

## OpenXR wait deferral

openxr_image_lifetime.cpp owns wait/release/destruction order for native D3D11
and DXVK/Vulkan wrappers. XR_TIMEOUT_EXPIRED retains the exact acquired index.
Next use retries its wait instead of acquiring again. Resize/destruction settles
the lease first and retains resources if waiting still times out.

A timed-out frame submits no world/menu layer and retires its native prediction.
A later frame transfers both eyes from a fresh family. A released left eye is
never combined with a stale or unwaited right eye. A temporary wait does not
invalidate controller/session state; API errors retain existing recovery.
Diagnostics report compositor wait_deferrals=N.

## Menus and projections

Menu/HUD capture and fullscreen video use the D3D11 texture dimension limit,
actual allocation failures, and existing bounded image pools. The old menu
8,388,608-pixel budget and 8,192-side ceiling are removed. 4096x2160 exceeded that
pixel budget; 3840x2160 already fitted. Admission does not guarantee free video
memory or runtime swapchain capacity.

Bridge admission, eye construction, projection decoding and culling-union
validation now share finite, ordered, nondegenerate checks. Valid off-axis
intervals need not straddle zero, and arbitrary tangent-width limits no longer
reject narrow/wide optics. Native float representability, positive reverse-Z near
distance, canted-eye admission and renderer ownership contracts still apply.

## Private campaign profile

Config, checkpoints and SP progression live in overlord-mwr/profile/players2/,
relative to the installation; the launcher uses that config too. On first use,
an absent/empty private directory receives a staged copy of selected SP files,
published as a complete directory by rename. Existing private progress wins.

Selected files: config.cfg, keys.cfg, savegame.svg, savegame.svg.bak,
settings_c.zip.h1, settings_s.zip.h1 and h1_*.stat. If the root profile has no
config, players2/default is the import source. Import checks file/total size
budgets and rejects nonregular selected entries. Source files are copied.

Mapped-game Win32 opens, attributes, enumeration, moves, directory creation and
deletion redirect native players2 paths into that directory. Other asset paths
retain their original resolution. This adapter covers the supported game's
imports; it is not a process-wide filesystem sandbox. Steam cloud ownership
metadata is not imported, and no new private-profile Steam Cloud sync is added.
Launcher-language sidecar preferences retain their separate existing location.

Keep the private directory when updating. For a new import, back it up and move
it away while the game is closed. Replacing private progress is an explicit
operation; flat-game progress is not repeatedly synchronized over VR progress.

## Grip choice

Weapon interaction exposes vr_weaponGripMode=hold|toggle, default hold, in the
launcher and native VR menu, with nine locale translations.

- Hold retains existing press/release controls.
- Toggle acquires normally, ignores that acquisition's physical release, and
  uses the next Grip press to release the committed weapon.
- That release press cannot also acquire a magazine/control/other weapon.
  Mechanical parts, reload items and abdominal equipment keep normal controls.
- Rejected placements retry for the same weapon/hold revision every 100 ms,
  including after the button becomes neutral. Ownership, mode/reference/input
  changes or gameplay suspension cancel stale intent. Support-distance rules
  remain unchanged.

## Source audit and validation

After changing native declarations, run:

    python tools/audit_mwr_host.py --write
    python tools/audit_mwr_host.py --check

The checker verifies project source admission and absence of the H2 compiler.
docs/generated/mwr-native-addresses.json classifies image VAs/RVAs and contract
offsets/sizes/constants, and lists zero/unported symbols. Every record has
runtime_admitted=false: source indexing cannot prove an ABI or replace native
byte/signature/lifetime checks.

Generate focused checks with tools/premake5.exe vs2022 --with-vr-tests and build
mwr-host-adaptation-tests. Coverage: constructor rejection, complete/failed
one-time imports including Unicode directories, actual native profile wrappers,
native eye/culling construction, toggle and existing release/motion regressions,
WARP 4096x2160 UI allocation, and actual image lease functions across repeated
timeouts and destruction.

Live evidence: D:/overlordmwr/port/tmp/mwr_host_20261116/. The strict MockXR
fixture injects two timeouts per new swapchain and rejects duplicate acquires,
unwaited releases and destruction of acquired images. Native/DXVK bog_a,
optimizer Yes transitions, AA, shader-preload lock and live toggle/rejected-drop
recovery are checked. Real-HMD fit/comfort and the GitHub #8 reporter's entire
5-10-minute loading delay are not established by these checks.
