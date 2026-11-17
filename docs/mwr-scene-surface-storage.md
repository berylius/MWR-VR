# MWR scene surface storage

War Pig exterior gameplay exhausted H1's 256 KiB scene surface arena. The
preserved user console contained over 16,000 failed runtime model builds, with
cursors from 262172 through 302672. Replacement magazines, chamber rounds and
receivers use this same native allocator. Earlier short mocks stayed below its
limit and therefore did not exclude exhaustion during real gameplay.

The H2 storage component existed but was excluded from the MWR shipping sources
and component gate. Its addresses and packet layout cannot be enabled unchanged.
The H1 port retains the native atomic cursor/reset and two separate frontend
banks, with 512 KiB per bank and eight-byte surface ID units. A capacity-only
change would wrap the native 16-bit IDs above 256 KiB.

The complete checked transaction includes 230 instruction sites: 86 arena
references including compound field loads, 55 packed decoder pairs, four scaled
address calculations, five encoders/advances, the fused shadow encoder, three
reservation bounds, both brush advances, hidden writers/walkers, brush prefix
padding and unchanged frontend identity guards. Motion-history indices remain
in four-byte units. H1 rigid records remain `bones*32+56`, skinned records 56;
hidden records grow from four to eight bytes. The C++ skin packet parser uses
the same layout.

H1 bindings:

| Field | Address or value |
|---|---|
| Frontend pointer | `0x15182A680` |
| First frontend | `0x14F877580` |
| Second frontend | `0x150850E00` |
| Bank separation | `0xFD9880` |
| Atomic cursor | frontend `+0x540C14` |
| Original arena | frontend `+0x62B480` |
| Warning text | `0x1407FEC20` |

Generate the contract offline from the supported clean owned executable:

```text
python tools/generate_mwr_scene_surface_contract.py --image h1_sp64_clean.exe --output src/client/component/mwr_scene_surface_storage_contract.hpp --audit contract.json
```

The generator rejects unexpected reader/member counts and instruction operands.
At startup every original instruction and the complete warning text must match
before any write. Installation verifies the written bytes and rolls back on
failure. Arena relocation preserves each frontend's original separation.

`mwr_scene_surface_storage_tests.cpp` checks all 65,536 IDs, packed draw keys,
mixed streams across 256 KiB, H1 rigid/hidden/skinned packets, the C++ skin parser,
compound operand relocation, the shadow encoder and independent frontend banks.

For an owned offline mock only, environment variable
`OVERLORD_MWR_MOCK_SURFACE_PRESSURE=1` reserves a 256 KiB prefix through the native
atomic allocator before model builds. It preserves all existing reservations
and exercises actual rendering above the old ID boundary. The normal frontend
reset clears the prefix each frame. It requires loaded `mockxr.dll`, is inert in
real headset gameplay, and is not a saved option or console command. God mode
must be verified before gameplay testing. This fixture tests address capacity;
it does not substitute for real headset acceptance of the reported incident.

Observed validation (2026-11-18): Release build succeeded. On the user's latest
`bog_b` exterior checkpoint, native D3D11 completed 23,972 replacement model
builds with zero failures and a 289,968-byte cursor peak. DXVK completed 26,175
with zero failures and a 293,136-byte peak. Both exceeded the original limit.
Eye captures show the complete M4 receiver and detached AK magazine. In both
paths, magazine removal retained a real chamber round, a single shot consumed
it without refilling, and an AK magazine with 29 rounds survived rotation,
walking and a 5 FPS pause/resume. Native god/notarget flags were `0xc05`.
All 22 original save/config/input files were restored and hash checked; 23
installed files match the build and the native executable is unchanged. Evidence
is under `port/tmp/warpig_arena_20261117` in the shared workspace. Natural
headset acceptance remains pending.
