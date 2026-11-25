#!/usr/bin/env python3
"""Check ARM syscall metadata needed by Thumb callers in the linked kernel."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path,
                        default=ROOT / 'kernel/kernel.elf')
    sdk_tool = Path(os.environ.get('DEVKITARM', '')) / 'bin/arm-none-eabi-readelf'
    if os.name == 'nt':
        sdk_tool = sdk_tool.with_suffix('.exe')
    parser.add_argument('--readelf', default=shutil.which('arm-none-eabi-readelf')
                        or str(sdk_tool))
    args = parser.parse_args()
    source = (ROOT / 'kernel/syscalls.s').read_text()
    exports = set(re.findall(r'^\s*\.global\s+(\w+)', source, re.M))
    functions = set(re.findall(r'^\s*\.type\s+(\w+)\s+STT_FUNC', source, re.M))
    errors = [f'{name}: exported syscall has no STT_FUNC declaration'
              for name in sorted(exports - functions)]
    symbols = subprocess.check_output([args.readelf, '-sW', str(args.elf)], text=True)
    # Unused stubs can be discarded by --gc-sections. Check every surviving one.
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[-1] in exports:
            if fields[3] != 'FUNC' or int(fields[1], 16) & 3:
                errors.append(f'{fields[-1]}: linked syscall is not an aligned ARM FUNC')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'{args.elf.name}: exported syscall metadata supports ARM/Thumb interworking')


if __name__ == '__main__':
    main()
