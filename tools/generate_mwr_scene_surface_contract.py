"""Generate the reviewed H1 SP surface ABI transaction from an owned clean PE.

Offline only. Runtime installation checks every exact instruction before writing.
The H2 generator is retained separately; its addresses do not apply to MWR.
"""
import argparse
import json
import struct
from pathlib import Path
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--audit', type=Path, required=True)
args = parser.parse_args()
pe = pefile.PE(str(args.image))
base = pe.OPTIONAL_HEADER.ImageBase
image = pe.get_memory_mapped_image()
pdata = next(s for s in pe.sections if s.Name.startswith(b'.pdata')).get_data()
cs = Cs(CS_ARCH_X86, CS_MODE_64)
cs.skipdata = True
decoded = {}
for a, b, _ in struct.iter_unpack('<III', pdata[:len(pdata)//12*12]):
    if not 0x1000 <= a < b <= 0x766000 or b-a > 0x20000:
        continue
    for at, size, mnemonic, operands in cs.disasm_lite(image[a:b], base+a):
        if 0x140530000 <= at < 0x1405d2000:
            decoded[at] = (at, size)
        elif '0x62b4' in operands or '0x3fffc' in operands:
            # Check candidates outside the renderer without retaining the
            # entire multi-million instruction image as Capstone objects.
            decoded[at] = (at, size)
cs.detail = True
decoded = {at: next(cs.disasm(image[at-base:at-base+size], at)) for at, size in decoded.values()}
ordered = sorted(decoded.values(), key=lambda x: x.address)
patches = {}

def add(inst, field, size, value, reason, dynamic=0):
    old = bytes(inst.bytes)
    new = bytearray(old)
    new[field:field+size] = value.to_bytes(size, 'little')
    assert len(new) == len(old) and inst.address not in patches
    patches[inst.address] = dict(address=inst.address, old=old.hex(), new=new.hex(),
        field=field, dynamic=dynamic, reason=reason, asm=inst.mnemonic+' '+inst.op_str)

def imm(at, expected, new, reason):
    inst = decoded[at]
    assert any(o.type == X86_OP_IMM and o.imm & 0xffffffff == expected for o in inst.operands), hex(at)
    add(inst, inst.imm_offset, inst.imm_size, new, reason)

def disp(at, expected, new, reason):
    inst = decoded[at]
    assert inst.disp == expected, hex(at)
    add(inst, inst.disp_offset, inst.disp_size, new, reason)

fields = {}
decoders = []
for i, inst in enumerate(ordered):
    if not inst.id:
        continue
    operands = [(o.imm, inst.imm_offset, inst.imm_size) if o.type == X86_OP_IMM else
        (o.mem.disp, inst.disp_offset, inst.disp_size) for o in inst.operands
        if o.type == X86_OP_IMM or (o.type == X86_OP_MEM and o.mem.base != X86_REG_RIP)]
    arena = [v for v in operands if 0x62b480 <= v[0] < 0x66b480]
    renderer = 0x140530000 <= inst.address < 0x1405d2000
    if not renderer:
        assert not any(0x62b480 <= v[0] < 0x62b580 for v in operands), hex(inst.address)
    if arena and renderer:
        assert len(arena) == 1
        value, field, size = arena[0]
        assert size == 4
        add(inst, field, size, value, f'relocated arena + {value-0x62b480:#x}', 1)
        fields[value-0x62b480] = fields.get(value-0x62b480, 0)+1
    if inst.mnemonic == 'and' and any(o.type == X86_OP_IMM and o.imm == 0x3fffc for o in inst.operands):
        assert renderer
        imm(inst.address, 0x3fffc, 0x7fff8, 'packed surface ID mask')
        shifts = [x for x in ordered[max(0, i-5):i] if x.mnemonic == 'shr' and x.address >= inst.address-30]
        assert shifts
        imm(shifts[-1].address, 0x12, 0x11, 'packed surface ID decode')
        decoders.append(inst.address)
assert fields == {0:62, 4:7, 6:2, 8:2, 16:1, 24:2, 40:6, 48:2, 84:2}, fields
assert len(decoders) == 55, len(decoders)
# Independent raw-byte census catches operands missed by unwind disassembly.
# A similarly valued RIP displacement in unrelated code is not an arena field.
text = next(s for s in pe.sections if s.Name.startswith(b'.text'))
raw_text = text.get_data()
text_base = base+text.VirtualAddress
covered = {r['address']+i for r in patches.values() for i in range(len(r['old'])//2)}
rip_fields = {inst.address+inst.disp_offset for inst in ordered if inst.id and
    any(o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP for o in inst.operands)}
for field in fields:
    needle = struct.pack('<I', 0x62b480+field)
    start = 0
    while (at := raw_text.find(needle, start)) >= 0:
        start = at+1
        assert text_base+at in covered or text_base+at in rip_fields, ('unaccounted raw arena operand', hex(text_base+at))

# Four explicit uint16-ID address calculations. H1 has no H2 folded base/4 site.
for at in [0x140587dc1, 0x140588152, 0x14058ad9c, 0x14058afce]:
    inst = decoded[at]
    assert inst.mnemonic == 'lea' and inst.operands[1].mem.scale == 4
    field = inst.modrm_offset+1
    assert inst.bytes[field] >> 6 == 2
    add(inst, field, 1, inst.bytes[field] | 0xc0, 'surface ID address scale 4 -> 8')
for at in [0x14053b0d8, 0x140571846, 0x140589489, 0x14058aeed, 0x14058b516]:
    imm(at, 2, 3, 'surface ID encode or advance')
inst = decoded[0x140588d64]
assert inst.operands[1].imm == 0x14b80
add(inst, inst.imm_offset, inst.imm_size, 0x14b80, 'fused shadow encoder relocated modulus', 3)
imm(0x140588d72, 0x12, 0x11, 'fused shadow encoder byte offset -> packed ID')
for at in [0x1405880a8, 0x140588583]:
    imm(at, 6, 3, '24-byte brush surface ID advance')
for at in [0x140536b29, 0x14053b06a, 0x14057182c]:
    imm(at, 0x40000, 0x80000, '512 KiB reservation bound')

# Hidden records are the only non-8-aligned H1 record. Rigid packets retain
# bones*32+56; skinned packets retain 56. Both writers and all walkers change.
for at in [0x1405352c6, 0x1405354ed, 0x14053571e, 0x140535760,
           0x14053661c, 0x14053681b, 0x1405714fc, 0x14057165f,
           0x140573f6d, 0x140574fe0]:
    imm(at, 4, 8, 'hidden surface byte stride')
disp(0x140588c44, -0x11, 0xf3, 'hidden byte stride: 21 - 13 = 8')
for at in [0x1405892eb, 0x14058adda, 0x14058b18c]:
    disp(at, 7, 11, 'hidden byte stride: -3 + 11 = 8; logical ID advance remains 1')
# Only the scene brush prefix changes. Motion-history storage at +0xf41880
# deliberately retains its original 28-byte prefix and 4-byte index units.
for at in [0x14053b053, 0x14053b0b5]:
    disp(at, 0x1c, 0x20, 'align scene brush transform prefix')
for at in [0x14058304f, 0x140583056]:
    inst = decoded[at]
    patches[at] = dict(address=at, old=inst.bytes.hex(), new=inst.bytes.hex(),
        field=0, dynamic=0, reason='native frontend bank identity guard', asm=inst.mnemonic+' '+inst.op_str)
rows = sorted(patches.values(), key=lambda x: x['address'])
args.audit.write_text(json.dumps(rows, indent=2))
def array(s):
    return '{'+','.join('0x'+s[i:i+2] for i in range(0, len(s), 2))+'}'
header = ['#pragma once', '#include <array>', '#include <cstdint>',
    'namespace scene_surface_storage::contract {',
    'struct patch { std::uintptr_t address; std::array<std::uint8_t,15> before,after; unsigned size,field,dynamic; };',
    '// Exact MWR H1 SP ABI. Generated offline; install the whole transaction.',
    f'inline constexpr std::array<patch,{len(rows)}> patches{{{{']
for row in rows:
    header += [f'    // {row["reason"]}; {row["asm"]}',
        f'    {{0x{row["address"]:x}, {array(row["old"])}, {array(row["new"])}, {len(row["old"])//2}, {row["field"]}, {row["dynamic"]}}},']
header += ['}};', '}']
args.output.write_text('\n'.join(header)+'\n', encoding='utf-8')
needle = b'MAX_SCENE_SURFS_SIZE(( 1 << ( 16 + 2 ) )) exceeded - not drawing surface\n\0'
warning = image.find(needle)
assert warning >= 0 and image.find(needle, warning+1) < 0
print(f'Validated {len(rows)} sites: {sum(fields.values())} arena references, {len(decoders)} packed decoders; warning={base+warning:#x}')
