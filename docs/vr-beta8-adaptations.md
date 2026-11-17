# Beta 8 features adapted for MWR

Source: Overlord v0.8.0-beta, tag `3df27c1b67dc37e0a910d9f569d2a0a2d64b6289`.
This is an adaptation to the MWR host and its assets, not an installation of the H2 executable.

| Feature | Behavior and controls |
| --- | --- |
| Back holster readiness | A short, gentle pulse when a hand first enters a usable back slot. Holding the hand there does not repeat it. Existing draw/stow confirmation remains stronger. Uses `vr_weaponHapticScale`. |
| Separate hand angles | Enable **Adjust hands separately** in Calibration. Left/right refer to physical controllers. First use copies shared angles; later toggles preserve each hand's settings. Standard and legacy controller modes keep separate banks. |
| M203 handling | Trigger can pinch the tube without taking the rifle's support grip. A fired case remains at the breech until the moving tube no longer supports it, then drops once. Uses the loaded M4/M16 attachment's own geometry. Loaded rounds follow the tube; firing and ammunition still require native authorization. |
| First-person size | `vr_firstPersonScale`: 20–100%, default 100%, **restart required**. Saved size is frozen before native rigs are built. Scales hands, weapons and associated magazines/reload parts together, keeping controller contact and anatomical arm placement. Does not change world size or eye height. |
| Tracer style | `vr_tracerStyle`: Native, Blaze, Cerulean, Royal, Anime, 1776, Rainbow. Native preserves game cadence; other styles request tracers for each shot. Pellet/penetration segments share that shot's appearance. Changes appearance, not damage, spread, ammunition or firing sounds. |
| Manual shotgun cases | Physical W1200 pump extraction uses its native world casing effect at the committed extraction. H1's `boltAction=false` no longer makes it eject early or miss physical extraction. Explicit notetrack FX lookup is available for admitted manual feeds without a direct casing effect. |

The native controller settings menu and launcher expose relevant live settings;
first-person size is launcher-only because it changes cached geometry for the whole process.
The launcher includes all nine existing translations.

## MWR implementation boundaries

H1 tracer event layouts, entry points, model identities and WeaponDef offsets are explicitly rebound.
Colored styles use private HDR-preserving neutral texture copies made from the loaded H1 material;
shared game materials and textures are not modified. GPU preparation runs under the interop owner
and the native context mutex. A pending texture retains stock appearance until preparation finishes.

M203 geometry is measured at H1's seated round position, not its animation-start bind position
16.5 inches ahead of the mount. Both M4 and M16 providers measure approximately 1.1041 native units
of axial support. The gameplay ledger commits extraction before publishing the dropped case.

H1 shotgun world effects remain native assets, preserving checkpoint compatibility.
H2-only private FX checkpoint addresses and H2-only shotgun assets are not activated.

Sun shadow sharing already exists in this MWR branch. Inspection of upstream `445de06` found
additional visibility/resource/light guards tied to its H2 renderer. Those guards were not
transplanted without H1 layout evidence. Existing MWR same-pair/index-order/auxiliary-view guards
remain in place; this feature set does not claim additional shadow performance gains.

## Validation

Focused CPU tests cover holster visits, independent calibration, tracer shot ownership,
M203 ammunition and manipulation, and H1 seated-case geometry. WARP readback tests exercise
HDR neutral material conversion and first-person meshes at 20%, 80% and 100%, including mipmaps,
original resource preservation, wrist contacts, buffer subviews and native epoch retirement.

Native D3D11 and DXVK MockXR runs loaded `bog_a`. Saved 80% applied from startup, with mesh
packets admitted and zero mesh rejections. Native tracer samples contained all seven Rainbow
colors. DXVK accepted the optimization dialog's **Yes**, retained stereo Filmic T2x, and admitted
M203 firing and Trigger pinch with one unsupported-case release and no duplicate drop.
The H1 W1200 was also exercised in DXVK: one shot produced no early shell, committed
pump extraction produced one `vfx/shelleject/shotgun` request, and closing the pump
changed stored/chamber ammunition from 6/0 to 5/1 without a second case request.

MockXR cannot establish real controller vibration strength, comfort or precise headset hand fit.
Those require a headset check. Evidence and logs: `port/tmp/beta8_20261116` in the workspace.
