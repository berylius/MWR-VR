"""Archive the current MWR working sources and initialized dependencies locally.

Includes tracked files plus untracked project source/docs, never ignored output
or personal runtime files. Records dirty provenance and per-file hashes rather
than presenting a working snapshot as a committed tag.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tracked = {p for p in git('ls-files', '--recurse-submodules', '-z').decode().split('\0') if p}
    extra = {p for p in git('ls-files', '--others', '--exclude-standard', '-z').decode().split('\0') if p}
    allowed = {'src', 'docs', 'tools', 'tests', 'premake', 'data', 'assets', 'licenses'}
    extra = {p for p in extra if Path(p).parts[0] in allowed}
    names = sorted(tracked | extra)
    entries = {}
    for name in names:
        path = ROOT / name
        if not path.resolve().is_relative_to(ROOT) or path.is_symlink() or not path.is_file():
            raise ValueError('Invalid source entry: '+name)
        if any(part in {'.git', '__pycache__', 'node_modules'} for part in path.relative_to(ROOT).parts):
            raise ValueError('Unexpected generated/private entry: '+name)
        entries[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(version=json.loads((ROOT/'version.json').read_text(encoding='utf-8')),
        revision=git('rev-parse', 'HEAD').decode().strip(),
        working_tree_dirty=bool(git('status', '--porcelain').strip()),
        source_kind='working-source snapshot; not a release tag',
        dependencies=git('submodule', 'status', '--recursive').decode().splitlines(), files=entries)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.tmp')
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, digest in entries.items():
                data = (ROOT/name).read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError('Source changed during packaging: '+name)
                archive.writestr('MWR-VR/'+name, data)
            archive.writestr('MWR-VR/SOURCE-SNAPSHOT.json', json.dumps(manifest, indent=2))
            archive.writestr('MWR-VR/BUILD-SNAPSHOT.txt',
                'Dependencies are included. On Windows, run tools\\premake5.exe vs2022, '
                'then build build\\overlord.sln in Release x64 with the installed Visual Studio toolset.\n'
                'This snapshot has no .git directory; do not run git submodule update.\n'
                'See docs/development.md for prerequisites.\n')
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None:
                raise ValueError('Archive CRC check failed')
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f'{args.output}: {len(entries)} source files; dirty snapshot={manifest["working_tree_dirty"]}')

if __name__ == '__main__':
    main()
