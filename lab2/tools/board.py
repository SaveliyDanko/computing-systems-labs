"""Probe, program or serve GDB for the SDK FT2232/JTAG adapter."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['probe', 'flash', 'debug'])
parser.add_argument('--elf', type=Path, default=ROOT/'Debug/lab2.elf')
args = parser.parse_args()
plugins = sorted(Path('C:/ST').glob('STM32CubeIDE*/STM32CubeIDE/plugins'))
exes = [x for p in plugins for x in p.glob('*externaltools.openocd*/tools/bin/openocd.exe')]
scripts = [x.parent.parent for p in plugins for x in p.glob('*debug.openocd*/resources/openocd/st_scripts/target/stm32f4x.cfg')]
exe = os.environ.get('OPENOCD_EXE') or (str(exes[-1]) if exes else None)
search = os.environ.get('OPENOCD_SCRIPTS') or (str(scripts[-1]) if scripts else None)
if not exe or not search:
    raise SystemExit('Set OPENOCD_EXE and OPENOCD_SCRIPTS (folder containing target/stm32f4x.cfg).')
if args.action == 'flash' and not args.elf.is_file():
    raise SystemExit(f'Build first: missing {args.elf}')
# Tcl braces safely quote a Windows path; reject Tcl delimiters in user input.
elf = args.elf.resolve().as_posix()
if any(c in elf for c in '{}\n\r'):
    raise SystemExit('ELF path must not contain braces or newlines.')
commands = {'probe': 'init; halt; flash probe 0; mdw 0xE0042000 1; mdh 0x1FFF7A22 1; resume; shutdown',
            'flash': 'program {' + elf + '} verify reset exit',
            'debug': 'init; halt'}
raise SystemExit(subprocess.call([exe, '-s', search, '-f', str(ROOT/'SDK11M.cfg'), '-c', commands[args.action]], cwd=ROOT))
