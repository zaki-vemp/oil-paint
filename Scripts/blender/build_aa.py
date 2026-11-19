"""Rebuild and validate the street GLBs with headless Blender 4.2.23 LTS.

Pass --install after Blender's -- to copy verified exports into public/street.
The working set lives under .test so a failed builder leaves the shipped art intact.
"""

import argparse
import importlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import bpy

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
BUILDERS = ('blades', 'landmarks', 'civilians', 'specialists', 'infantry', 'elites', 'vehicles', 'mission_props')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true', help='copy validated exports into public/street')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if bpy.app.version != (4, 2, 23):
        raise RuntimeError(f'Expected Blender 4.2.23 LTS, got {bpy.app.version_string}')

    scratch = ROOT / '.test'
    scratch.mkdir(exist_ok=True)
    sys.path.insert(0, str(TOOLS))
    sys.dont_write_bytecode = True
    with tempfile.TemporaryDirectory(prefix='street-assets-', dir=scratch) as directory:
        output = Path(directory)
        for name in BUILDERS:
            builder = importlib.import_module(f'build_aa_{name}')
            builder.OUT_DIR = output
            bpy.ops.wm.read_factory_settings(use_empty=True)
            builder.main()

        subprocess.run(['node', str(TOOLS / 'assets.mjs'), str(output)], cwd=ROOT, check=True)
        reference = ROOT / 'public' / 'street'
        changed = []
        for asset in sorted(output.glob('*.glb')):
            target = reference / asset.name
            if target.exists() and asset.read_bytes() == target.read_bytes():
                continue
            changed.append(asset.name)
            if args.install:
                shutil.copyfile(asset, target)
        if changed:
            print(f'{"Updated" if args.install else "Rebuilt with binary differences"}: {", ".join(changed)}', flush=True)
        else:
            print('All 26 Blender exports match public/street byte for byte.', flush=True)


if __name__ == '__main__':
    main()
