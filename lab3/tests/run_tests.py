"""Execute the compiled Cortex-M4 piano and real HAL UART with modeled MMIO.

The model supplies RCC ready/status flags, UART flags and IRQ boundaries, and
GPIO BSRR effects. Timer registers are inspected; PWM edges, sound, USB and real
interrupt timing require validation on the SDK-1.1MC.427 board.
"""
from pathlib import Path
import math
import struct

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                              UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3, UC_ARM_REG_PRIMASK)

ROOT = Path(__file__).resolve().parents[1]
UART = 0x40011000
RCC = 0x40023800
TIM2 = 0x40000000
RXNE, TC, TXE = 0x20, 0x40, 0x80
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


class Board:
    def __init__(self, firmware=False):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in ((0x08000000, 1024 * 1024),
                              (0x20000000, 192 * 1024),
                              (0x40000000, 0x30000),
                              (0x42000000, 0x2000000),
                              (0xE0000000, 0x100000)):
            self.uc.mem_map(address, size)
        target = 'lab3' if firmware else 'tests'
        with (ROOT / f'build/{target}.elf').open('rb') as stream:
            elf = ELFFile(stream)
            self.symbols = {s.name: s['st_value'] for s in
                            elf.get_section_by_name('.symtab').iter_symbols()}
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD' and segment['p_filesz']:
                    self.uc.mem_write(segment['p_vaddr'], segment.data())
                    if segment['p_paddr'] != segment['p_vaddr']:
                        self.uc.mem_write(segment['p_paddr'], segment.data())
        self.sr = TXE | TC
        self.rx = 0
        self.nvic = False
        self.output = bytearray()
        self.completed = False
        self.tx_reads = 0
        self.write32(RCC, 0x83)
        self.write32(RCC + 4, 0x24003010)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.on_read)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)

    def read32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def write32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def on_read(self, uc, access, address, size, value, user):
        if address == RCC:
            cr = self.read32(RCC)
            for enable, ready in ((0, 1), (16, 17), (24, 25)):
                cr = (cr & ~(1 << ready)) | (((cr >> enable) & 1) << ready)
            self.write32(RCC, cr)
        elif address == RCC + 8:
            cfgr = self.read32(RCC + 8)
            self.write32(RCC + 8, (cfgr & ~12) | ((cfgr & 3) << 2))
        elif address == UART:
            if not self.sr & TC:
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
            self.sr &= value | ~0x7F
        elif address == UART + 4:
            self.output.append(value & 0xFF)
            self.sr &= ~(TXE | TC)
            self.tx_reads = 0
        elif address == RCC + 0x24 and value & (1 << 4):
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
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1,
                               UC_ARM_REG_R2, UC_ARM_REG_R3), args):
            self.uc.reg_write(reg, value)
        self.uc.emu_start(self.symbols[name] | 1, 0x080FFFF0, count=2000000)
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        if not self.completed and pc != 0x080FFFF0:
            raise AssertionError(f'{name} failed to return (PC={pc:#x})')
        return self.uc.reg_read(UC_ARM_REG_R0)

    def irq(self):
        cr1, cr3 = self.read32(UART + 12), self.read32(UART + 20)
        pending = ((self.sr & RXNE and cr1 & RXNE) or
                   (self.sr & TC and cr1 & TC) or
                   (self.sr & TXE and cr1 & TXE) or
                   (self.sr & 0x0F and cr3 & 1))
        if self.nvic and pending and not self.uc.reg_read(UC_ARM_REG_PRIMASK):
            self.call('USART1_IRQHandler')

    def step(self, now=0):
        self.sr |= TC | TXE
        self.irq()
        self.call('test_step', now)
        self.irq()

    def inject(self, byte, errors=0):
        if self.sr & RXNE:
            self.sr |= 8
        else:
            self.rx = byte
        self.sr |= RXNE | errors
        self.irq()

    def settle(self, now=0):
        for _ in range(3000):
            self.step(now)
            if self.call('serial_idle'):
                return
        raise AssertionError('TX failed to drain')

    def send(self, data, now=0):
        self.output.clear()
        for byte in data:
            self.inject(byte)
            self.step(now)
        self.settle(now)
        return bytes(self.output)


def check_clock(board):
    check(board.read32(board.symbols['SystemCoreClock']) == 120000000,
          'PLL produces 120 MHz HCLK')
    pll = board.read32(RCC + 4)
    check(pll & 63 == 8 and (pll >> 6) & 511 == 120 and
          (pll >> 16) & 3 == 0 and (pll >> 24) & 15 == 5,
          'HSI PLL M8 N120 P2 Q5')
    check(not pll & (1 << 22), 'PLL source is HSI')
    cfgr = board.read32(RCC + 8)
    check(cfgr & 12 == 8 and cfgr & 0xF0 == 0,
          'SYSCLK PLL and AHB divide by one')
    check((cfgr >> 10) & 7 == 5 and (cfgr >> 13) & 7 == 4,
          'APB1 30 MHz and APB2 60 MHz')
    check(board.read32(0x40023C00) & 7 == 3, 'Flash latency 3')
    check(board.read32(0xE000E014) == 119999, 'SysTick remains 1 ms at 120 MHz')
    check(board.read32(UART + 8) == 1042, '57600 UART baud at APB2 60 MHz')


def frequencies_and_timer():
    board = Board()
    check(board.call('sound_init') == 1, 'sound init')
    natural_semitones = (0, 2, 4, 5, 7, 9, 11)
    for octave in range(1, 10):
        for note, semitone in enumerate(natural_semitones):
            midi = 12 + (octave - 1) * 12 + semitone
            expected = math.floor(440000 * 2 ** ((midi - 69) / 12) + 0.5)
            actual = board.call('piano_note_frequency_millihz', octave, note)
            check(actual == expected, f'equal temperament octave {octave}, note {note}')
            check(board.call('sound_set_frequency', actual) == 1,
                  f'timer accepts octave {octave}, note {note}')
            ticks = board.read32(TIM2 + 0x2C) + 1
            # Independent error bound: nearest integer period differs by <= 0.5 tick.
            check(abs(60000000000 / actual - ticks) <= 0.500001,
                  f'32-bit timer period octave {octave}, note {note}')
            check(board.read32(TIM2 + 0x34) == ticks // 2,
                  'PWM duty cycle is half the period')
    check(board.read32(0x40020020) >> 20 & 15 == 1, 'PA5 uses TIM2 AF1')


def uart_application():
    board = Board()
    check(board.call('test_boot') == 1, 'application and real HAL UART initialize')
    check_clock(board)
    check(board.nvic and board.read32(UART + 12) & RXNE, 'UART receive IRQ armed')
    check(board.read32(0x40020024) & 0xFF0 == 0x770, 'UART PA9/PA10 AF7')
    check(not board.call('test_active'), 'silent at boot')
    # A single key without Enter must reach the timer and produce an echoed reply.
    output = board.send(b'6', 100)
    check(output.startswith(b'6') and b'440.000 Hz' in output,
          'instant key echo and note report without Enter')
    check(board.call('test_frequency') == 440000 and board.call('test_active'),
          'single UART key starts A4 immediately')
    check(board.read32(TIM2 + 0x2C) == 136363 and board.read32(TIM2) & 1,
          'UART note programs and starts TIM2')
    board.send(b'+A', 200)
    check(board.call('test_octave') == 6 and board.call('test_duration') == 1100,
          'UART octave and case-sensitive duration controls')
    check(board.call('test_frequency') == 440000, 'future settings preserve active note')
    board.step(1099)
    check(board.call('test_active'), 'active note retains original end time')
    board.step(1100)
    check(not board.call('test_active') and board.read32(TIM2 + 0x34) == 0,
          'note expires and output becomes silent')
    output = board.send(b'\r\n', 2000)
    check(output.count(b'Scale ') == 1, 'CRLF starts one scale')
    check(output.count(b'Note do,') == 1, 'first scale note reported once')
    check(board.call('test_gamma') and board.call('test_frequency') == 523251,
          'scale starts at do in selected octave')
    board.send(b'-a', 2100)
    board.output.clear()
    board.step(3100)
    board.settle(3100)
    check(board.call('test_frequency') == 587330, 'scale keeps launch octave and duration')
    check(board.output.count(b'Note re,') == 1 and b'587.330 Hz' in board.output,
          'next scale note is reported asynchronously with snapshot frequency')
    board.step(3101)
    board.settle(3101)
    check(board.output.count(b'Note re,') == 1, 'scale report is not repeated each loop')
    output = board.send(b'7', 3200)
    check(b'493.883 Hz' in output and not board.call('test_gamma'),
          'new note interrupts scale and uses new settings')
    check(board.call('test_note') == 6, 'replacement note is si')
    board.send(b'\n', 3300)
    check(board.call('test_gamma') and board.call('test_note') == 0,
          'new Enter interrupts current note')
    board.step(10300)
    check(not board.call('test_active'), 'complete seven-note scale stops')
    board.send(b'1', 11000)
    frequency = board.call('test_frequency')
    output = board.send(b'x', 11001)
    check(b'ERROR:' in output and board.call('test_frequency') == frequency,
          'invalid byte is rejected without interrupting valid note')
    board.inject(ord('7'), errors=2)
    board.step(11002)
    board.settle(11002)
    check(board.call('test_rx_errors') == 1 and board.call('test_frequency') == frequency,
          'framing-error byte cannot start another note')
    check(b'input lost' in board.output, 'UART input loss reported')
    check(b'493.883 Hz' in board.send(b'7', 11003),
          'single-key protocol recovers after RX error without waiting for Enter')
    for _ in range(140):
        board.inject(ord('x'))
    check(board.call('test_overflows') == 1, 'RX overflow counted')
    board.step(11004)
    board.settle(11004)
    check(b'input lost' in board.output, 'RX overflow reported')
    check(b'261.626 Hz' in board.send(b'1', 11005), 'RX works after overflow')


def warm_clock():
    board = Board()
    # Debugger reset-init can leave a running PLL with a different multiplier.
    board.write32(RCC, 0x03000083)
    board.write32(RCC + 4, 8 | (168 << 6) | (7 << 24))
    board.write32(RCC + 8, 10)
    check(board.call('test_boot') == 1, 'initialization tolerates an already running different PLL')
    check_clock(board)


def boot_firmware():
    board = Board(firmware=True)
    check(board.read32(0x080000D4) == board.symbols['USART1_IRQHandler'],
          'production USART1 vector points to real ISR')
    board.uc.reg_write(UC_ARM_REG_SP, board.read32(0x08000000))
    board.uc.emu_start(board.symbols['Reset_Handler'] | 1, 0x080FFFF0, count=30000)

    def run_main():
        # Dispatch at instruction boundaries while keeping main's live registers.
        context = board.uc.context_save()
        board.sr |= TC | TXE
        board.irq()
        board.uc.context_restore(context)
        board.uc.emu_start(board.uc.reg_read(UC_ARM_REG_PC) | 1,
                           0x080FFFF0, count=1000)

    for _ in range(800):
        run_main()
        if b'Enter: scale\r\n' in board.output:
            break
    check(b'Lab3 piano' in board.output, 'Reset_Handler/main emits startup greeting')
    check(board.read32(board.symbols['lab3_fault']) == 0, 'production boot has no fatal')
    check_clock(board)
    context = board.uc.context_save()
    board.inject(ord('6'))
    board.uc.context_restore(context)
    for _ in range(400):
        run_main()
        if b'440.000 Hz' in board.output:
            break
    check(b'440.000 Hz' in board.output, 'production main accepts immediate UART key')
    check(board.read32(TIM2 + 0x2C) == 136363 and board.read32(TIM2) & 1,
          'production main drives PWM')
    board.write32(board.symbols['uwTick'], 1000)
    for _ in range(10):
        run_main()
    check(board.read32(TIM2 + 0x34) == 0 and not board.read32(TIM2) & 1,
          'production main stops PWM at note deadline')


def main():
    unit = Board()
    unit.call('test_entry')
    count = unit.read32(unit.symbols['test_checks'])
    failure = unit.read32(unit.symbols['test_failure'])
    check(unit.completed and failure == 0, f'test_lab3.c:{failure} (check {count})')
    check(count >= 300, f'Only {count} C assertions executed')
    before = checks
    frequencies_and_timer()
    uart_application()
    warm_clock()
    boot_firmware()
    report = f'PASS: {count} Cortex-M4 C assertions; {checks - before} timer/UART/boot checks\n'
    (ROOT / 'build/test-results.txt').write_text(report, encoding='utf-8')
    print(report, end='')


if __name__ == '__main__':
    main()
