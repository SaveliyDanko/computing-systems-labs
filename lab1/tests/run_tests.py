import os
from pathlib import Path
import struct
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / '.lab1-tools'))
sys.path.insert(0, str(ROOT / 'tools'))
from build import toolchain
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR

suffix = '.exe' if os.name == 'nt' else ''
nm = subprocess.check_output([str(toolchain()/('arm-none-eabi-nm'+suffix)), '-n', str(ROOT/'build/tests.elf')], text=True)
symbols = {fields[2]: int(fields[0], 16) for line in nm.splitlines() if len(fields := line.split()) == 3}
uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.mem_map(0x08000000, 1024*1024)
uc.mem_map(0x20000000, 192*1024)
uc.mem_map(0x40000000, 0x30000)
uc.mem_write(0x08000000, (ROOT/'build/tests.bin').read_bytes())
uc.reg_write(UC_ARM_REG_SP, 0x2002FFF0)
uc.reg_write(UC_ARM_REG_LR, 0x080FFFF1)

def read32(address):
    return struct.unpack('<I', uc.mem_read(address, 4))[0]

trace = []
completed = False
def on_write(emu, access, address, size, value, user):
    global completed
    if address == symbols['test_done'] and value:
        completed = True
        emu.emu_stop()
        return

    base = 0x40020C00
    if base <= address < base + 0x28:
        trace.append(f'GPIOD +0x{address-base:02X} <- 0x{value:08X}')
        odr = read32(base + 0x14)
        moder = read32(base)
        if address == base + 0x18:
            odr = (odr & ~(value >> 16)) | (value & 0xFFFF)
            emu.mem_write(base + 0x14, struct.pack('<I', odr))
        if address == base:
            moder = value
        high = read32(symbols['guard_high'])
        held = read32(symbols['guard_driven'])
        for pin in range(16):
            output = (moder >> (pin*2)) & 3 == 1
            if held & (1 << pin) and not output:
                raise AssertionError('Output unexpectedly released during reinitialization')
            if high & (1 << pin) and output and not (odr & (1 << pin)):
                raise AssertionError('Output enabled before high latch was loaded')

uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
uc.emu_start(symbols['test_entry'] | 1, 0x080FFFF0, count=10000000)
failure = read32(symbols['test_failure'])
checks = read32(symbols['test_checks'])

if failure:
    raise SystemExit(f'FAIL: test_lab1.c:{failure} (check {checks})')
if not completed or checks < 80:
    raise SystemExit(f'FAIL: execution did not finish, only {checks} checks')
report = f'PASS: {checks} C assertions; MMIO guards passed.\n' + '\n'.join(trace) + '\n'
(ROOT/'build/test-results.txt').write_text(report, encoding='utf-8')
print(report.splitlines()[0])
