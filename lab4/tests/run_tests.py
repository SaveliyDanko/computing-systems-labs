"""Execute the compiled Cortex-M4 keypad app and real I2C/UART drivers with modeled MMIO.

The model supplies RCC flags, UART IRQ boundaries, GPIO BSRR, I2C handshakes and
a PCA9538 connected to a diode-less switch matrix. Timer registers are inspected;
physical I2C edges, PWM, sound, USB and interrupt timing require board validation.
"""
from pathlib import Path
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
I2C = 0x40005400
RXNE, TC, TXE = 0x20, 0x40, 0x80
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


class I2cModel:
    """Byte-level I2C1 and PCA9538 model; no application functions are mocked."""
    def __init__(self, board):
        self.board = board
        self.pca = [0xFF, 0xFF, 0, 0xFF]
        self.pressed = 0
        self.present = True
        self.stall = False
        self.external_busy = False
        self.target_stuck = False
        self.scl_stuck = False
        self.release_after_pulses = 9
        self.pulses = 0
        self.stop_delay_reads = 3
        self.stop_remaining = 0
        self.read_drained = True
        self.gpio_scl = self.gpio_sda = True
        self.master_sda_low = False
        self.nack_next = 0
        self.trace = []
        self.reset()

    def reset(self):
        if getattr(self, 'cr1', 0) & 0x200 or (
                getattr(self, 'read_mode', False) and not self.read_drained):
            self.target_stuck = True
            self.trace.append(('reset_before_stop',))
        self.trace.append(('reset',))
        self.cr1 = self.sr1 = self.sr2 = 0
        self.phase = 'idle'
        self.read_mode = False
        self.pointer = 0
        self.sr1_seen = False
        self.stop_remaining = 0
        self.read_drained = True
        for offset in (0, 0x10, 0x14, 0x18):
            self.board.write32(I2C + offset, 0)

    def inputs(self):
        # Connected switch components model ordinary diode-less matrix ghosting.
        edges = [set() for _ in range(7)]
        for col in range(3):
            for row in range(4):
                if self.pressed & (1 << (row + 4 * col)):
                    edges[row].add(4 + col)
                    edges[4 + col].add(row)
        levels = (self.pca[1] & ~self.pca[3]) | self.pca[3]
        for source in range(7):
            if self.pca[3] & (1 << source) or self.pca[1] & (1 << source):
                continue
            pending, seen = [source], set()
            while pending:
                pin = pending.pop()
                if pin in seen:
                    continue
                seen.add(pin)
                levels &= ~(1 << pin)
                pending.extend(edges[pin] - seen)
        return (levels ^ self.pca[2]) & 0xFF

    def on_read(self, address):
        if address == I2C:
            if self.cr1 & 0x200 and self.read_drained and not self.target_stuck:
                self.stop_remaining -= 1
                if self.stop_remaining <= 0:
                    self.finish_stop()
            self.board.write32(address, self.cr1)
        elif address == I2C + 0x14:
            self.sr1_seen = True
            self.board.write32(address, 0 if self.stall else self.sr1)
        elif address == I2C + 0x18:
            busy = self.external_busy or self.target_stuck or self.scl_stuck
            self.board.write32(address, self.sr2 | (2 if busy else 0))
            if self.sr1_seen and self.sr1 & 2:
                if self.read_mode:
                    ack = bool(self.cr1 & 0x400)
                    self.trace.append(('read_addr_clear', ack,
                                       self.board.uc.reg_read(UC_ARM_REG_PRIMASK)))
                    self.sr1 = 0x40
                else:
                    self.sr1 = 0x84
                self.sr1_seen = False
        elif address == I2C + 0x10:
            value = self.inputs() if self.pointer == 0 else self.pca[self.pointer]
            self.board.write32(address, value)
            self.trace.append(('read', self.pointer, value))
            self.sr1 &= ~0x40
            self.read_drained = True

    def finish_stop(self):
        self.cr1 &= ~0x200
        self.sr1 = self.sr2 = 0
        self.phase = 'idle'
        self.trace.append(('stop_completed',))

    def gpio_levels(self, moder=None, odr=None):
        if moder is None:
            moder = self.board.read32(0x40020400)
        if odr is None:
            odr = self.board.read32(0x40020414)
        scl_gpio = (moder >> 16) & 3 == 1
        sda_gpio = (moder >> 18) & 3 == 1
        scl = not self.scl_stuck and (not scl_gpio or bool(odr & 0x100))
        sda = not self.target_stuck and (not sda_gpio or bool(odr & 0x200))
        return scl, sda, scl_gpio

    def gpio_changed(self, moder=None, odr=None):
        scl, sda, scl_gpio = self.gpio_levels(moder, odr)
        if moder is None:
            moder = self.board.read32(0x40020400)
        if odr is None:
            odr = self.board.read32(0x40020414)
        master_sda_low = (moder >> 18) & 3 == 1 and not odr & 0x200
        if scl_gpio and scl and not self.gpio_scl:
            if master_sda_low:
                self.trace.append(('stop_scl_release',))
            else:
                self.pulses += 1
                self.trace.append(('scl_pulse', self.pulses))
                if self.release_after_pulses is not None and self.pulses >= self.release_after_pulses:
                    self.target_stuck = False
                    scl, sda, _ = self.gpio_levels(moder, odr)
        if scl_gpio and scl and sda and self.master_sda_low and not master_sda_low:
            self.trace.append(('gpio_stop',))
        self.gpio_scl, self.gpio_sda = scl, sda
        self.master_sda_low = master_sda_low

    def gpio_idr(self):
        scl, sda, _ = self.gpio_levels()
        return (0x100 if scl else 0) | (0x200 if sda else 0)

    def on_write(self, address, value):
        if address == I2C:
            previous = self.cr1
            if (previous & 0x200 and not value & 0x200) or (
                    previous & 1 and not value & 1 and not self.read_drained):
                self.target_stuck = True
                self.trace.append(('stop_cancelled',))
            self.cr1 = value & ~0x100  # START self-clears when sent.
            if value & 0x100:
                if self.target_stuck or self.scl_stuck:
                    return
                self.trace.append(('restart' if self.sr2 & 2 else 'start',))
                self.phase = 'address'
                self.sr1, self.sr2 = 1, 3
            if value & 0x200 and not previous & 0x200:
                self.trace.append(('stop',))
                self.stop_remaining = self.stop_delay_reads
        elif address == I2C + 0x10:
            if self.phase == 'address':
                address7 = (value >> 1) & 127
                self.read_mode = bool(value & 1)
                self.trace.append(('address', value & 0xFF))
                if address7 != 0x71 or not self.present or self.nack_next:
                    self.nack_next = max(0, self.nack_next - 1)
                    self.sr1 = 0x400
                    return
                self.phase = 'read' if self.read_mode else 'register'
                self.read_drained = not self.read_mode
                self.sr1 = 2
            elif self.phase == 'register':
                self.pointer = value & 3
                self.trace.append(('register', self.pointer))
                self.phase = 'value'
                self.sr1 = 0x84
            elif self.phase == 'value':
                self.pca[self.pointer] = value & 0xFF
                self.trace.append(('write', self.pointer, value & 0xFF))
                self.sr1 = 0x84
        elif address == I2C + 0x14:
            self.sr1 &= value


class Board:
    def __init__(self, firmware=False):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in ((0x08000000, 1024 * 1024),
                              (0x20000000, 192 * 1024),
                              (0x40000000, 0x30000),
                              (0x42000000, 0x2000000),
                              (0xE0000000, 0x100000)):
            self.uc.mem_map(address, size)
        target = 'lab4' if firmware else 'tests'
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
        self.write32(0x40020810, 1 << 15)  # Released active-low PC15.
        self.i2c = I2cModel(self)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.on_read)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)

    def read32(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def write32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def on_read(self, uc, access, address, size, value, user):
        if address == 0x40020410:
            self.write32(address, self.i2c.gpio_idr())
        elif I2C <= address < I2C + 0x28:
            self.i2c.on_read(address)
        elif address == RCC:
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
        if I2C <= address < I2C + 0x28:
            self.i2c.on_write(address, value)
        elif address == RCC + 0x20 and value & (1 << 21):
            self.i2c.reset()
        elif 0x42000000 <= address < 0x44000000:
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
            if base == 0x40020400:
                self.i2c.gpio_changed()
        elif address == 0x40020400:
            self.i2c.gpio_changed(moder=value)
        elif address == 0x40020414:
            self.i2c.gpio_changed(odr=value)

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

    def inject(self, byte, errors=0):
        if self.sr & RXNE:
            self.sr |= 8
        else:
            self.rx = byte
        self.sr |= RXNE | errors
        self.irq()



def i2c_transfer(board, read, reg, value=0, address=0x71, now=0):
    if read:
        started = board.call('i2c_bus_read_reg', address, reg, now)
    else:
        started = board.call('i2c_bus_write_reg', address, reg, value, now)
    check(started == 1, 'register transaction starts without waiting')
    for _ in range(30):
        board.call('i2c_bus_service', now)
        if board.call('i2c_bus_result') != 1:
            return board.call('i2c_bus_result')
    raise AssertionError('healthy byte transaction did not complete')


def finish_i2c(board, now, limit=35):
    for elapsed in range(limit + 1):
        for _ in range(8):
            board.call('i2c_bus_service', (now + elapsed) & 0xFFFFFFFF)
            if board.call('i2c_bus_result') != 1:
                return board.call('i2c_bus_result'), elapsed
    raise AssertionError('I2C cleanup/recovery exceeded its bounded deadline')


def bus_registers():
    board = Board()
    check(board.call('i2c_bus_init') == 1, 'I2C bus initializes')
    check(board.read32(I2C + 4) & 63 == 30, 'I2C CR2 uses APB1=30 MHz')
    check(board.read32(I2C + 0x1C) == 188, 'Standard mode remains below 80 kHz')
    check(board.read32(I2C + 0x20) == 31, '1000 ns Standard-mode TRISE')
    check(board.read32(I2C + 8) & (1 << 14), 'required OAR1 bit14 set')
    check(board.read32(0x40020400) >> 16 & 15 == 10, 'PB8/PB9 alternate function')
    check(board.read32(0x40020420 + 4) & 255 == 0x44, 'PB8/PB9 use AF4')
    check(board.read32(0x40020404) & 0x300 == 0x300, 'SCL/SDA open drain')
    check(board.read32(0x40020C14) & 128, 'PD7 enables I2C buffer')
    check(i2c_transfer(board, False, 3, 0xFF) == 2, 'PCA config write completes')
    check(i2c_transfer(board, False, 1, 0) == 2, 'PCA output write completes')
    check(i2c_transfer(board, False, 2, 0) == 2, 'PCA polarity write completes')
    check(i2c_transfer(board, False, 3, 0xFE) == 2, 'select first row')
    board.i2c.pressed = 1 << 8  # row0, column2: physical SB9.
    board.i2c.trace.clear()
    check(i2c_transfer(board, True, 0) == 2, 'PCA input read completes')
    check(board.call('i2c_bus_data') & 0x70 == 0x30, 'selected row drives column2 low')
    check(('address', 0xE2) in board.i2c.trace and ('address', 0xE3) in board.i2c.trace,
          'keyboard address is seven-bit 0x71 in both directions')
    check(('restart',) in board.i2c.trace, 'read uses repeated START')
    check(('read_addr_clear', False, 1) in board.i2c.trace,
          'one-byte RX clears ACK then ADDR in a short critical section')
    stop_index = board.i2c.trace.index(('stop',))
    read_index = next(i for i, event in enumerate(board.i2c.trace) if event[0] == 'read')
    check(stop_index < read_index, 'STOP scheduled before reading the one received byte')
    check(not board.call('i2c_bus_write_reg', 0x80, 0, 0, 0), 'invalid seven-bit address rejected')
    check(i2c_transfer(board, False, 1, 0, address=0x70) == 3, 'other PCA address NACKs')
    check(board.call('test_i2c_errors') == 1 and board.call('test_i2c_nacks') == 1,
          'NACK diagnostics counted')
    check(i2c_transfer(board, True, 3) == 2, 'bus recovers after NACK')
    check(board.call('i2c_bus_read_reg', 0x71, 0, 100) == 1, 'start pending transfer')
    check(not board.call('i2c_bus_read_reg', 0x71, 0, 100), 'concurrent transaction rejected')
    board.i2c.stall = True
    board.call('i2c_bus_service', 100)
    board.call('i2c_bus_service', 109)
    check(board.call('i2c_bus_result') == 1, 'pending service returns before deadline')
    board.call('i2c_bus_service', 110)
    result, _ = finish_i2c(board, 110)
    check(result == 3 and board.call('test_i2c_timeouts') == 1,
          'missing I2C event times out without active wait')
    board.i2c.stall = False
    check(i2c_transfer(board, True, 3, now=111) == 2, 'bus recovers after timeout')
    check(board.call('i2c_bus_read_reg', 0x71, 0, 0xFFFFFFFB) == 1, 'start near tick rollover')
    board.i2c.stall = True
    board.call('i2c_bus_service', 4)
    check(board.call('i2c_bus_result') == 1, 'unsigned deadline before rollover timeout')
    board.call('i2c_bus_service', 5)
    result, _ = finish_i2c(board, 5)
    check(result == 3 and board.call('test_i2c_timeouts') == 2,
          'I2C timeout handles uint32 rollover')
    board.i2c.stall = False
    check(board.call('i2c_bus_read_reg', 0x71, 0, 200) == 1, 'start for bus-error injection')
    board.i2c.sr1 = 0x100
    board.call('i2c_bus_service', 200)
    result, _ = finish_i2c(board, 200)
    check(result == 2 and board.call('test_i2c_errors') == 4,
          'lone spurious BERR is counted and cleared without aborting transfer')
    check(i2c_transfer(board, True, 3, now=201) == 2, 'bus continues after spurious BERR')
    board.i2c.external_busy = True
    board.i2c.trace.clear()
    check(board.call('i2c_bus_read_reg', 0x71, 0, 300) == 1, 'request while external bus is busy')
    board.call('i2c_bus_service', 300)
    check(not board.i2c.trace, 'busy bus does not generate a START')
    board.call('i2c_bus_service', 310)
    result, elapsed = finish_i2c(board, 310)
    check(result == 3 and elapsed <= 30 and board.call('test_i2c_timeouts') == 3,
          'permanently busy bus still has bounded timeout')
    board.i2c.external_busy = False
    check(i2c_transfer(board, True, 3, now=311) == 2, 'external bus release permits recovery')


def bus_stop_and_recovery():
    board = Board()
    check(board.call('i2c_bus_init') == 1, 'initialize delayed STOP test')
    board.i2c.trace.clear()
    check(board.call('i2c_bus_write_reg', 0x70, 1, 0, 0) == 1, 'start NACK transaction')
    for _ in range(10):
        board.call('i2c_bus_service', 0)
        if ('stop',) in board.i2c.trace:
            break
    check(('stop',) in board.i2c.trace and board.call('i2c_bus_result') == 1,
          'fatal error requests STOP and leaves cleanup pending')
    check(('reset',) not in board.i2c.trace, 'requesting STOP does not immediately reset I2C')
    for _ in range(2):
        board.call('i2c_bus_service', 0)
        check(('reset',) not in board.i2c.trace, 'reset waits while STOP has not reached target')
    board.call('i2c_bus_service', 0)
    check(board.call('i2c_bus_result') == 3, 'error latched after STOP completion')
    check(board.i2c.trace.index(('stop_completed',)) < board.i2c.trace.index(('reset',)),
          'normal error reset follows completed STOP')
    check(not board.i2c.target_stuck and not board.i2c.pulses,
          'ordinary NACK cleanup does not strand SDA or use bus-clear clocks')

    board.i2c.trace.clear()
    check(board.call('i2c_bus_read_reg', 0x71, 0, 1) == 1, 'start interrupted single-byte reception')
    for _ in range(15):
        board.call('i2c_bus_service', 1)
        if any(item[0] == 'read_addr_clear' for item in board.i2c.trace):
            break
    check(not board.i2c.read_drained, 'receive byte is pending before injected fatal error')
    board.i2c.sr1 |= 0x400  # AF with RXNE already set.
    board.call('i2c_bus_service', 1)
    result, _ = finish_i2c(board, 1)
    check(result == 3 and board.i2c.read_drained, 'error cleanup drains one-byte RXNE')
    check(sum(item[0] == 'read' for item in board.i2c.trace) == 1,
          'interrupted byte is drained exactly once')
    check(('reset_before_stop',) not in board.i2c.trace and not board.i2c.target_stuck,
          'RX cleanup completes STOP before peripheral reset')

    stuck = Board()
    check(stuck.call('i2c_bus_init') == 1, 'initialize stuck-SDA test')
    stuck.i2c.target_stuck = True
    check(stuck.call('i2c_bus_init') == 1 and stuck.i2c.target_stuck,
          'MCU peripheral reset cannot reset the external PCA9538 target')
    stuck.i2c.trace.clear()
    check(stuck.call('i2c_bus_read_reg', 0x71, 0, 100) == 1, 'request while slave holds SDA')
    stuck.call('i2c_bus_service', 110)
    result, elapsed = finish_i2c(stuck, 110)
    check(result == 3 and elapsed <= 30, 'GPIO bus recovery is bounded by 30 ms')
    check(stuck.i2c.pulses == 9 and not stuck.i2c.target_stuck,
          'nine open-drain SCL clocks release stuck target SDA')
    check(('gpio_stop',) in stuck.i2c.trace, 'bus clear sends a GPIO STOP after clock pulses')
    check(stuck.i2c.trace.index(('gpio_stop',)) < stuck.i2c.trace.index(('reset',)),
          'peripheral reset follows GPIO STOP')
    check(stuck.read32(0x40020400) >> 16 & 15 == 10 and
          stuck.read32(0x40020424) & 255 == 0x44 and
          stuck.read32(0x40020404) & 0x300 == 0x300,
          'GPIO recovery restores PB8/PB9 AF4 open-drain mode')
    check(i2c_transfer(stuck, True, 3, now=150) == 2, 'target communication works after bus clear')

    held_clock = Board()
    check(held_clock.call('i2c_bus_init') == 1, 'initialize permanently held SCL test')
    held_clock.i2c.scl_stuck = True
    check(held_clock.call('i2c_bus_read_reg', 0x71, 0, 100) == 1, 'request with SCL held low')
    held_clock.call('i2c_bus_service', 110)
    result, elapsed = finish_i2c(held_clock, 110)
    check(result == 3 and elapsed == 30 and held_clock.i2c.pulses == 0,
          'held SCL cannot create false clock counts or exceed recovery deadline')
    check(held_clock.read32(0x40020400) >> 16 & 15 == 10,
          'bounded failed recovery still restores alternate-function pins')


def advance(board, milliseconds, scanner=False):
    if not hasattr(board, 'now'):
        board.now = 0
    events = []
    for _ in range(milliseconds):
        board.now = (board.now + 1) & 0xFFFFFFFF
        for _ in range(8):
            if scanner:
                key = board.call('test_keypad_step', board.now)
                if key:
                    events.append(key)
            else:
                board.sr |= TC | TXE
                board.irq()
                if not board.call('test_step', board.now):
                    raise AssertionError('app_update returned failure')
                board.irq()
    return events


def scanner_matrix():
    board = Board()
    check(board.call('test_keypad_boot', 0) == 1, 'scanner initializes')
    check(not advance(board, 150, scanner=True), 'no boot events on released matrix')
    check(board.call('test_keypad_valid') and not board.call('test_keypad_blocked'),
          'scanner arms after complete stable released frames')
    writes = [event for event in board.i2c.trace if event[0] == 'write']
    check(writes[:3] == [('write', 3, 0xFF), ('write', 1, 0), ('write', 2, 0)],
          'expander becomes all-input before zero output and polarity initialization')
    configurations = {event[2] for event in writes if event[1] == 3}
    check({0xFE, 0xFD, 0xFB, 0xF7} <= configurations, 'scan selects all four rows separately')
    check(all(value == 0xFF or value in (0xFE, 0xFD, 0xFB, 0xF7)
              for value in configurations), 'unselected rows remain high impedance')
    for code in range(1, 13):
        board.i2c.pressed = 1 << (code - 1)
        check(advance(board, 130, scanner=True) == [code], f'physical SB{code} decoded once')
        check(board.call('test_keypad_mask') == 1 << (code - 1), f'physical SB{code} raw mapping')
        check(not advance(board, 100, scanner=True), 'holding a key does not repeat')
        board.i2c.pressed = 0
        check(not advance(board, 100, scanner=True), 'release does not generate press')
    before = board.call('test_keypad_events')
    board.i2c.pressed = 3
    check(not advance(board, 150, scanner=True), 'two-key chord produces no event')
    board.i2c.pressed = 1
    check(not advance(board, 150, scanner=True), 'partial chord release cannot create a key')
    board.i2c.pressed = 0
    advance(board, 100, scanner=True)
    board.i2c.pressed = 0x23  # Three rectangle corners create a fourth electrical phantom.
    check(not advance(board, 150, scanner=True), 'ghost rectangle is suppressed')
    check(board.call('test_keypad_mask') == 0x33, 'model reproduces diode-less ghost key')
    check(board.call('test_keypad_events') == before, 'chords do not alter event count')
    board.i2c.pressed = 0
    advance(board, 100, scanner=True)
    board.i2c.pressed = 1
    board.i2c.nack_next = 1
    check(not advance(board, 300, scanner=True), 'failed frame and held key cannot create phantom press')
    check(board.call('test_keypad_errors') > 0 and board.call('test_keypad_valid'),
          'scanner reports error and reconnects after retry')
    board.i2c.pressed = 0
    advance(board, 100, scanner=True)
    board.i2c.pressed = 1
    check(advance(board, 130, scanner=True) == [1], 'new press works after failed frame recovery')


def set_side(board, pressed):
    board.write32(0x40020810, 0 if pressed else 1 << 15)


def app_press(board, code, release=True):
    before = board.call('test_events')
    board.i2c.pressed = 1 << (code - 1)
    advance(board, 130)
    check(board.call('test_events') == before + 1, f'app accepts SB{code} before release')
    if release:
        board.i2c.pressed = 0
        advance(board, 100)


def application():
    board = Board()
    check(board.call('test_boot') == 1, 'application initializes')
    check(board.read32(board.symbols['SystemCoreClock']) == 120000000, 'application HCLK 120 MHz')
    check(board.read32(RCC + 8) & 0xFC00 == 0x9400, 'APB1 30 MHz and APB2 60 MHz')
    check(board.read32(0xE000E014) == 119999, 'SysTick remains one millisecond')
    check(board.read32(UART + 8) == 1042, 'UART 57600 baud at APB2 60 MHz')
    advance(board, 150)
    check(board.call('test_mode') == 0, 'starts in keyboard testing mode')
    check(b'Mode: keyboard test' in board.output, 'initial mode reported in UART')
    app_press(board, 1, release=False)
    check(b'Key 1' in board.output and not board.call('test_frequency'),
          'test mode reports code without playing a note')
    before = board.call('test_events')
    advance(board, 300)
    check(board.call('test_events') == before, 'app does not repeat held matrix key')
    set_side(board, True)
    advance(board, 10)
    set_side(board, False)
    advance(board, 10)
    check(board.call('test_mode_changes') == 0, 'PC15 bounce does not switch mode')
    set_side(board, True)
    advance(board, 40)
    check(board.call('test_mode') == 1 and board.call('test_mode_changes') == 1,
          'PC15 confirmed press switches to piano before release')
    advance(board, 150)
    check(board.call('test_mode_changes') == 1, 'held PC15 switches only once')
    check(b'Mode: piano' in board.output, 'new mode reported')
    check(board.call('test_events') == before and not board.call('test_frequency'),
          'matrix key held through mode change remains suppressed')
    set_side(board, False)
    board.i2c.pressed = 0
    advance(board, 100)
    # Incoming terminal characters cannot bypass the physical keyboard.
    board.inject(ord('6'))
    advance(board, 30)
    check(not board.call('test_frequency'), 'UART input does not control the piano')
    app_press(board, 6)
    check(board.call('test_frequency') == 440000, 'SB6 starts la/A4')
    check(board.read32(TIM2 + 0x2C) == 136363 and board.read32(TIM2) & 1,
          'matrix key reaches real timer PWM')
    app_press(board, 8)
    app_press(board, 10)
    check(board.call('test_octave') == 6 and board.call('test_duration') == 1100,
          'SB8/SB10 change future octave and duration')
    check(board.call('test_frequency') == 440000, 'active note retains launch settings')
    advance(board, 1000)
    check(not board.call('test_frequency') and board.read32(TIM2 + 0x34) == 0,
          'PWM stops by deadline without waiting for key release or more input')
    app_press(board, 12)
    check(board.call('test_gamma') and board.call('test_note') == 0,
          'SB12 starts ascending scale')
    app_press(board, 9)
    app_press(board, 11)
    check(board.call('test_octave') == 5 and board.call('test_duration') == 1000,
          'SB9/SB11 adjust future settings')
    advance(board, 600)
    check(board.call('test_frequency') == 587330 and board.call('test_note') == 1,
          'playing scale retains launch octave/duration')
    app_press(board, 3)
    check(not board.call('test_gamma') and board.call('test_frequency') == 329628,
          'new matrix note interrupts scale and uses future settings')
    board.i2c.stall = True
    board.i2c.target_stuck = True
    advance(board, 1100)
    check(not board.call('test_frequency') and board.read32(TIM2 + 0x34) == 0,
          'stalled I2C cannot delay note stop')
    check(board.call('test_app_errors') > 0, 'I2C outage diagnosed while application continues')
    check(board.i2c.pulses > 0, 'application services GPIO bus recovery while note timing continues')
    board.i2c.stall = False
    advance(board, 250)
    app_press(board, 7)
    check(board.call('test_frequency') == 493883, 'piano recovers after I2C outage')
    set_side(board, True)
    advance(board, 40)
    check(board.call('test_mode') == 0 and not board.call('test_frequency'),
          'returning to test mode immediately stops current note')
    check(board.call('test_octave') == 5 and board.call('test_duration') == 1000,
          'mode switch preserves selected piano settings')


def mode_during_bus_clear():
    board = Board()
    check(board.call('test_boot') == 1, 'initialize application bus-clear case')
    advance(board, 150)
    set_side(board, True)
    advance(board, 40)
    set_side(board, False)
    advance(board, 40)
    app_press(board, 6)
    check(board.call('test_frequency') == 440000, 'note playing before stuck target')
    board.i2c.target_stuck = True
    board.i2c.release_after_pulses = None
    set_side(board, True)
    advance(board, 40)
    check(board.i2c.pulses > 0 and board.call('test_mode') == 0,
          'PC15 remains responsive during persistent SDA bus clear')
    check(not board.call('test_frequency') and board.read32(TIM2 + 0x34) == 0,
          'mode change silences PWM during bus recovery')
    set_side(board, False)
    board.i2c.target_stuck = False
    board.i2c.release_after_pulses = 9
    advance(board, 250)
    app_press(board, 2)
    check(b'Key 2' in board.output, 'matrix is rearmed after electrical bus recovery')


def absent_keyboard_boot():
    board = Board()
    board.i2c.present = False
    set_side(board, True)
    check(board.call('test_boot') == 1, 'slave absence does not fail MCU initialization')
    advance(board, 200)
    check(board.call('test_app_errors') > 0 and board.call('test_mode_changes') == 0,
          'absent keyboard diagnosed; PC15 held at boot is not a new press')
    set_side(board, False)
    advance(board, 40)
    set_side(board, True)
    advance(board, 40)
    check(board.call('test_mode') == 1, 'mode button remains responsive without I2C slave')
    set_side(board, False)
    board.i2c.present = True
    advance(board, 200)
    app_press(board, 1)
    check(board.call('test_frequency') == 261626, 'hot reconnection restores physical piano input')


def production_boot():
    board = Board(firmware=True)
    board.uc.reg_write(UC_ARM_REG_SP, board.read32(0x08000000))
    board.uc.emu_start(board.symbols['Reset_Handler'] | 1, 0x080FFFF0, count=30000)
    for now in range(1, 301):
        board.write32(board.symbols['uwTick'], now)
        for _ in range(4):
            context = board.uc.context_save()
            board.sr |= TC | TXE
            board.irq()
            board.uc.context_restore(context)
            board.uc.emu_start(board.uc.reg_read(UC_ARM_REG_PC) | 1,
                               0x080FFFF0, count=1500)
    check(b'Lab4' in board.output and b'Mode: keyboard test' in board.output,
          'production Reset_Handler initializes and reports test mode')
    check(board.read32(board.symbols['lab4_fault']) == 0, 'production boot has no fatal')
    board.i2c.pressed = 1
    for now in range(301, 501):
        board.write32(board.symbols['uwTick'], now)
        for _ in range(4):
            context = board.uc.context_save()
            board.sr |= TC | TXE
            board.irq()
            board.uc.context_restore(context)
            board.uc.emu_start(board.uc.reg_read(UC_ARM_REG_PC) | 1,
                               0x080FFFF0, count=1500)
    check(b'Key 1' in board.output, 'production firmware reads actual modeled matrix through I2C')
    check(board.read32(board.symbols['lab4_fault']) == 0, 'production scan has no fatal')


def main():
    unit = Board()
    unit.call('test_entry')
    count = unit.read32(unit.symbols['test_checks'])
    failure = unit.read32(unit.symbols['test_failure'])
    check(unit.completed and failure == 0, f'test_lab4.c:{failure} (check {count})')
    check(count >= 170, f'Only {count} C assertions executed')
    before = checks
    bus_registers()
    bus_stop_and_recovery()
    scanner_matrix()
    application()
    mode_during_bus_clear()
    absent_keyboard_boot()
    production_boot()
    report = f'PASS: {count} Cortex-M4 C assertions; {checks - before} I2C/keypad/application/boot checks\n'
    (ROOT / 'build/test-results.txt').write_text(report, encoding='utf-8')
    print(report, end='')


if __name__ == '__main__':
    main()
