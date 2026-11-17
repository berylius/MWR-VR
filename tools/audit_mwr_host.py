"""Source catalog and build boundary for the MWR host; never an ABI admission."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/generated/mwr-native-addresses.json"
BASE, SIZE = 0x140000000, 0x12654800

def support_names():
    text = (ROOT / "premake/mwr_host.lua").read_text(encoding="utf-8")
    return set(re.findall(r'"([\w]+)"', text.split("local support = {", 1)[1].split("}", 1)[0]))

def admitted(path: Path):
    rel = path.relative_to(ROOT / "src/client")
    if rel.parts[:2] == ("component", "gsc") or rel.parts[:3] == ("game", "scripting", "lua"):
        return False
    if rel.parts[:2] == ("component", "gui"):
        return False
    if rel.parts[0] == "component" and len(rel.parts) == 2:
        return rel.stem in support_names()
    if rel.as_posix() == "component/vr/gameplay/campaign/ending/runtime.cpp":
        return False
    return True

def mask(text):
    # Preserve line locations while excluding provenance comments and UI strings.
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/',
                  lambda m: re.sub(r"[^\n]", " ", m[0]), text, flags=re.S)

def generate():
    entries, unported = [], []
    for path in sorted((ROOT / "src/client").rglob("*")):
        if path.suffix not in (".cpp", ".hpp") or not admitted(path):
            continue
        source = path.relative_to(ROOT).as_posix()
        text = path.read_text(encoding="utf-8-sig")
        for line, code in enumerate(mask(text).splitlines(), 1):
            for token in re.finditer(r"\b0x[0-9A-Fa-f]+\b", code):
                value = int(token[0], 16)
                if BASE <= value < BASE + SIZE:
                    kind = "image_va"
                    rva = f"0x{value - BASE:X}"
                elif "_contract" in path.stem or path.parent.name == "mwr":
                    rva = None
                    kind = "integer_constant"
                    if re.search(r"\w*_rva\b", code): kind = "rva"
                    elif re.search(r"\w*(?:offset|stride)\w*\b", code): kind = "offset_or_stride"
                    elif re.search(r"\w*(?:size|capacity)\w*\b", code): kind = "size_or_capacity"
                else:
                    continue
                entries.append(dict(source=source, line=line, column=token.start()+1,
                                    value=token[0], kind=kind, rva=rva, runtime_admitted=False))
        if source == "src/client/game/mwr/symbols.hpp":
            for line, code in enumerate(text.splitlines(), 1):
                if "TODO(mwr)" in code:
                    name = re.search(r"(\w+)\s*\{\s*0x0\b", code)
                    if name: unported.append(dict(name=name[1], source=source, line=line))
    return dict(schema=1, target="h1_sp64_ship.exe", image_base=f"0x{BASE:X}",
                image_size=f"0x{SIZE:X}", evidence="source_index_only",
                runtime_admitted=False, unported_symbols=unported, entries=entries)

def check_project():
    project = ROOT / "build/client.vcxproj"
    if not project.exists(): return
    namespace = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
    tree = ET.parse(project)
    sources = [(project.parent / p.attrib["Include"].replace("\\", "/")).resolve()
               for p in tree.findall(".//m:ClCompile", namespace) if "Include" in p.attrib]
    rejected = [p.relative_to(ROOT).as_posix() for p in sources
                if p.is_relative_to(ROOT / "src/client") and not admitted(p)]
    if rejected: raise RuntimeError("Legacy sources entered the MWR target: " + ", ".join(rejected))
    missing = support_names() - {p.stem for p in sources if p.parent == ROOT / "src/client/component"}
    if missing: raise RuntimeError("MWR support providers missing from project: " + ", ".join(sorted(missing)))
    dependencies = " ".join(p.text or "" for p in tree.findall(".//m:AdditionalDependencies", namespace))
    references = " ".join(p.attrib.get("Include", "") for p in tree.findall(".//m:ProjectReference", namespace))
    if "xsk-gsc-h2" in (dependencies + references).lower():
        raise RuntimeError("The H2 script compiler entered the MWR host")
    if "xsk-gsc-h1-catalog" not in (dependencies + references).lower():
        raise RuntimeError("The MWR host is missing its H1 name catalogs")
    return len(sources)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    payload = json.dumps(generate(), ensure_ascii=False, indent=2) + "\n"
    if args.write:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(payload, encoding="utf-8", newline="\n")
    elif not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != payload:
        raise SystemExit("MWR source catalog is stale; run python tools/audit_mwr_host.py --write")
    count = check_project()
    print(f"MWR_HOST_AUDIT_OK sources={count} addresses={len(json.loads(payload)['entries'])} "
          f"sha256={hashlib.sha256(payload.encode()).hexdigest()[:16]} evidence=source_only")

if __name__ == "__main__": main()
