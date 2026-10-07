"""Run compiled Cortex-M4 C against a small USART1/GPIO MMIO test model.

The model drives RXNE, TXE, TC, SR->DR clearing and NVIC enable bits.
It calls the real USART1 ISR at application boundaries; it is not a hardware
timing model, and cannot confirm the physical FT2232/COM connection.
"""
import argparse
from pathlib import Path
import struct
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                              UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                              UC_ARM_REG_PRIMASK)

ROOT = Path(__file__).resolve().parents[1]
UART = 0x40011000
RXNE, TC, TXE = 0x20, 0x40, 0x80
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


class Board:
    def __init__(self, backend, firmware=False):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in ((0x08000000, 1024 * 1024), (0x20000000, 192 * 1024),
                              (0x40000000, 0x30000), (0x42000000, 0x2000000),
                              (0xE0000000, 0x100000)):
            self.uc.mem_map(address, size)
        target = 'lab2' if firmware else 'tests'
        with (ROOT / f'build/{target}_{backend}.elf').open('rb') as stream:
            elf = ELFFile(stream)
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    self.uc.mem_write(segment['p_vaddr'], segment.data())
                    if segment['p_paddr'] != segment['p_vaddr']:
                        self.uc.mem_write(segment['p_paddr'], segment.data())
        self.sr = TXE | TC
        self.rx = 0
        self.nvic = False
        self.output = bytearray()
        self.hold_tx = False
        self.tx_reads = 0
        self.completed = False
        self.write32(0x40023800, 0x83)  # RCC reset HSI state.
        self.uc.hook_add(UC_HOOK_MEM_READ, self.on_read)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)
        self.uc.hook_add(UC_HOOK_CODE, self.on_tick, begin=self.symbols['HAL_GetTick'] & ~1,
                         end=self.symbols['HAL_GetTick'] & ~1)

    def read32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def write32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def on_tick(self, uc, address, size, user):
        # Let bounded polling timeouts expire even without a hardware SysTick.
        tick = self.symbols['uwTick']
        self.write32(tick, self.read32(tick) + 1)

    def on_read(self, uc, access, address, size, value, user):
        if address == UART:
            if not self.hold_tx and not self.sr & TC:
                self.tx_reads += 1
                if self.tx_reads >= 2:
                    self.sr |= TXE | TC
            self.write32(UART, self.sr)
        elif address == UART + 4:
            self.write32(UART + 4, self.rx)
            self.sr &= ~(RXNE | 0x0F)

    def on_write(self, uc, access, address, size, value, user):
        if 0x42000000 <= address < 0x44000000:
            offset = address - 0x42000000
            byte = 0x40000000 + offset // 32
            word = byte & ~3
            bit = (byte % 4) * 8 + (offset % 32) // 4
            previous = self.read32(word)
            self.write32(word, (previous & ~(1 << bit)) | ((value & 1) << bit))
        elif address == UART:
            # Software can clear status; it cannot set TXE/TC by writing one.
            self.sr &= value | ~0x7F
        elif address == UART + 4:
            self.output.append(value & 0xFF)
            self.sr &= ~(TXE | TC)
            self.tx_reads = 0
        elif address == 0x40023824 and value & (1 << 4):
            self.sr = TXE | TC
            self.rx = 0
        elif address == 0xE000E104 and value & (1 << 5):
            self.nvic = True
        elif address == 0xE000E184 and value & (1 << 5):
            self.nvic = False
        elif address == self.symbols.get('test_done') and value:
            self.completed = True
            uc.emu_stop()
        elif 0x40020000 <= address < 0x40022000 and address % 0x400 == 0x18:
            base = address - 0x18
            odr = self.read32(base + 0x14)
            self.write32(base + 0x14, (odr & ~(value >> 16)) | (value & 0xFFFF))

    def call(self, name, *args):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
        self.uc.reg_write(UC_ARM_REG_LR, 0x080FFFF1)
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.uc.reg_write(reg, value)
        self.uc.emu_start(self.symbols[name] | 1, 0x080FFFF0, count=2000000)
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        if not self.completed and pc != 0x080FFFF0:
            raise AssertionError(f'{name} failed to return (PC={pc:#x})')
        return self.uc.reg_read(UC_ARM_REG_R0)

    def irq(self):
        cr1, cr3 = self.read32(UART + 12), self.read32(UART + 20)
        pending = ((self.sr & RXNE and cr1 & RXNE) or (self.sr & TC and cr1 & TC)
                   or (self.sr & TXE and cr1 & TXE) or (self.sr & 0x0F and cr3 & 1))
        if self.nvic and pending and not self.uc.reg_read(UC_ARM_REG_PRIMASK):
            self.call('USART1_IRQHandler')

    def step(self, now=0):
        if not self.hold_tx:
            self.sr |= TC | TXE
        self.irq()
        self.call('test_step', now)
        self.irq()

    def inject(self, byte, errors=0):
        if self.sr & RXNE:
            self.sr |= 8  # Overrun preserves the previous DR byte.
        else:
            self.rx = byte
        self.sr |= RXNE | errors
        self.irq()

    def settle(self):
        for _ in range(3000):
            self.step()
            if self.call('serial_idle') and not self.call('serial_switch_pending'):
                # One more step lets the console enqueue a pending switch reply.
                self.step()
                if self.call('serial_idle'):
                    return
        raise AssertionError('TX failed to drain')

    def send(self, data):
        self.output.clear()
        for byte in data:
            self.inject(byte)
            self.step()
        self.settle()
        return bytes(self.output)


def exercise(backend, interrupts):
    board = Board(backend)
    check(board.call('test_boot', int(interrupts)), 'serial init')
    check(board.read32(UART + 8) == 278, '57600 baud divider')
    check(board.nvic == interrupts, 'NVIC mode')
    check(bool(board.read32(UART + 12) & RXNE) == interrupts, 'RX interrupt mode')
    check(board.read32(0x40020024) & 0xFF0 == 0x770, 'PA9/PA10 AF7')
    tag = b'I' if interrupts else b'P'
    output = board.send(b'?\r\n')
    check(b'\r\nred, mode 1, timeout 12, ' + tag + b'\r\n' in output, 'status response')
    check(output.count(b'timeout') == 1 and output.startswith(b'?\r'), 'echo and CRLF framing')
    for command, value in ((b'set mode 2\n', 2), (b'set mode 1\r', 1)):
        check(b'\r\nOK\r\n' in board.send(command), 'mode response')
        check(board.call('test_mode') == value, 'mode setting')
    for value in (1, 86400, 12):
        check(b'OK' in board.send(f'set timeout {value}\r'.encode()), 'timeout response')
        check(board.call('test_red_ms') == value * 1000, 'timeout seconds conversion')
    for command in (b'set timeout 0\r', b'set timeout -1\n', b'set timeout 86401\r',
                    b'set timeout 4294967296\r', b'set mode 3\r', b'garbage\r'):
        check(b'ERROR' in board.send(command), 'invalid command')
        check(board.call('test_red_ms') == 12000, 'invalid command cannot alter timeout')
    check(b'line too long' in board.send(b'x' * 80 + b'\r'), 'long line rejection')
    check(b'ERROR' in board.send(b'?\x00\r'), 'embedded NUL rejection')
    check(b'timeout 12' in board.send(b'?x\x08\r'), 'backspace support')
    target = not interrupts
    command = b'set interrupts on\r' if target else b'set interrupts off\r'
    output = board.send(command + b'?\r')
    check(b'OK' in output and (b', I\r\n' if target else b', P\r\n') in output, 'switch plus queued status')
    check(board.nvic == target and board.call('serial_interrupts') == target, 'actual switched mode')
    if not target:
        check(board.read32(UART + 12) & 0x1F0 == 0 and board.read32(UART + 20) & 1 == 0,
              'polling disables all UART interrupt sources')
    check(board.call('test_rx_errors') == 0 and board.call('test_tx_errors') == 0, 'clean exchange')
    board.inject(ord('?'), 2)  # Framing error must not execute a corrupt command.
    board.step()
    check(board.call('test_rx_errors') == 1, 'framing error counted')
    check(b'input lost' in board.send(b'\r'), 'error line discarded')
    check(b'timeout 12' in board.send(b'?\r'), 'RX rearmed after error')
    # Exercise queue overflow with real interrupt RX and no application reads.
    board.send(b'set interrupts on\r')
    for _ in range(140):
        board.inject(ord('x'))
    check(board.call('test_overflows') == 1, 'RX overflow counted')
    board.step()
    check(b'input lost' in board.send(b'\r'), 'overflow recovery discards partial line')
    check(b'timeout 12' in board.send(b'?\r'), 'RX works after overflow')
    board.output.clear()
    check(board.call('test_fill_tx', 512) == 1, 'full TX queue accepted')
    check(board.call('test_fill_tx', 1) == 0, 'full queue backpressure')
    check(board.call('test_fill_tx', 513) == 0, 'oversized write is atomic')
    board.settle()
    check(bytes(board.output) == bytes(ord('a') + i % 26 for i in range(512)), 'TX order across wrap')
    # A pending switch must wait for TC, including the byte already removed from TX.
    board.hold_tx = True
    board.sr |= TXE | TC
    check(board.call('test_fill_tx', 1), 'queue byte for switch')
    board.step()
    board.call('serial_request_mode', 0)
    for _ in range(5):
        board.step()
    check(board.call('serial_interrupts') == 1 and board.call('serial_switch_pending'), 'switch waits for TC')
    board.hold_tx = False
    board.settle()
    check(board.call('serial_interrupts') == 0 and not board.nvic, 'switch completes after TC')
    if backend == 'hal':
        board.output.clear()
        board.hold_tx = True
        board.sr |= TXE | TC
        board.call('test_fill_tx', 1)
        board.step()
        check(board.call('test_tx_errors') == 1, 'bounded HAL TX timeout')
        board.hold_tx = False
        board.settle()
        check(bytes(board.output) == b'a', 'timeout does not resend an accepted byte')


def boot_firmware(backend):
    board = Board(backend, firmware=True)
    check(board.read32(0x080000D4) == board.symbols['USART1_IRQHandler'], 'USART1 vector points to real ISR')
    board.uc.emu_start(board.symbols['Reset_Handler'] | 1, 0x080FFFF0, count=15000)
    for _ in range(200):
        # ISR has its own test stack; restore main registers after dispatch.
        context = board.uc.context_save()
        board.sr |= TC | TXE
        board.irq()
        board.uc.context_restore(context)
        board.uc.emu_start(board.uc.reg_read(UC_ARM_REG_PC) | 1, 0x080FFFF0, count=1000)
    check(b'Lab2 traffic: 57600 8N1, mode 1, timeout 12, I' in board.output,
          'production Reset_Handler/main emits startup greeting')
    check(board.read32(board.symbols['lab2_fault']) == 0, 'production boot has no fatal error')
    check(board.nvic and board.read32(UART + 8) == 278, 'production UART initialized')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--backend', choices=['hal', 'register', 'both'], default='both')
    args = parser.parse_args()
    backends = ['hal', 'register'] if args.backend == 'both' else [args.backend]
    reports = []
    for backend in backends:
        board = Board(backend)
        board.call('test_entry')
        count = board.read32(board.symbols['test_checks'])
        failure = board.read32(board.symbols['test_failure'])
        check(board.completed and failure == 0, f'test_lab2.c:{failure} (check {count})')
        check(count >= 56, f'Only {count} C assertions')
        before = checks
        for mode in (False, True):
            exercise(backend, mode)
        boot_firmware(backend)
        reports.append(f'PASS {backend}: {count} C assertions; {checks - before} UART integration checks')
    report = '\n'.join(reports) + '\n'
    (ROOT / 'build/test-results.txt').write_text(report)
    print(report, end='')


if __name__ == '__main__':
    main()
