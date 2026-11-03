"""Build the standalone I2C matrix keyboard firmware or its Cortex-M4 tests."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def toolchain():
    supplied = os.environ.get('ARM_GCC_BIN')
    if supplied:
        return Path(supplied)
    executable = shutil.which('arm-none-eabi-gcc')
    if executable:
        return Path(executable).parent
    candidates = sorted(Path('C:/ST').glob('STM32CubeIDE*/STM32CubeIDE/plugins/*gnu-tools*/tools/bin/arm-none-eabi-gcc.exe'))
    if not candidates:
        raise SystemExit('Set ARM_GCC_BIN to the folder containing arm-none-eabi-gcc.')
    return candidates[-1].parent


def run(args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--test', action='store_true', help='build executable C tests for Unicorn')
    args = parser.parse_args()
    bin_dir = toolchain()
    suffix = '.exe' if os.name == 'nt' else ''
    gcc = bin_dir / ('arm-none-eabi-gcc' + suffix)
    out = ROOT / 'build'
    out.mkdir(exist_ok=True)
    common = ['-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', '-std=c11', '-g3', '-Og',
              '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror',
              '-DSTM32F427xx', '-DUSE_HAL_DRIVER', '-ICore/Inc', '-IDrivers/CMSIS/Include',
              '-IDrivers/CMSIS/Device/ST/STM32F4xx/Include', '-IDrivers/STM32F4xx_HAL_Driver/Inc']
    sources = sorted((ROOT / 'Core/Src').glob('*.c'))
    if args.test:
        sources = [s for s in sources if s.name != 'main.c']
        sources += [ROOT / 'tests/test_lab4.c']
    else:
        sources += sorted((ROOT / 'Core/Startup').glob('*.s'))
    hal = ROOT / 'Drivers/STM32F4xx_HAL_Driver/Src'
    modules = ['', '_cortex', '_rcc', '_rcc_ex', '_pwr', '_pwr_ex', '_flash', '_flash_ex', '_uart', '_dma']
    sources += [hal / f'stm32f4xx_hal{name}.c' for name in modules]
    target = 'tests' if args.test else 'lab4'
    objects = []
    for source in sources:
        obj = out / (target + '_' + source.stem + '.o')
        vendor_flags = ['-Wno-unused-parameter'] if 'Drivers' in source.parts else []
        run([gcc, *common, *vendor_flags, '-c', source, '-o', obj])
        objects.append(obj)
    elf = out / (target + '.elf')
    if args.test:
        roots = ['test_entry', 'test_boot', 'test_step', 'test_mode', 'test_mode_changes',
                 'test_events', 'test_last_key', 'test_frequency', 'test_octave', 'test_duration',
                 'test_gamma', 'test_note', 'test_app_errors', 'test_keypad_boot', 'test_keypad_step',
                 'test_keypad_events', 'test_keypad_errors', 'test_keypad_frames', 'test_keypad_mask',
                 'test_keypad_valid', 'test_keypad_blocked', 'test_i2c_errors', 'test_i2c_nacks',
                 'test_i2c_timeouts', 'i2c_bus_init', 'i2c_bus_write_reg', 'i2c_bus_read_reg',
                 'i2c_bus_service', 'i2c_bus_result', 'i2c_bus_data',
                 'USART1_IRQHandler', 'serial_idle']
        flags = ['-Ttests/test.ld', '-Wl,-e,test_entry', '-nostartfiles', '--specs=nano.specs', '--specs=nosys.specs']
        flags += ['-Wl,--undefined=' + symbol for symbol in roots]
    else:
        flags = ['-TSTM32F427VGTX_FLASH.ld', '--specs=nano.specs', '--specs=nosys.specs', '-static', '-lc', '-lm']
    run([gcc, '-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', *objects, *flags,
         '-Wl,--gc-sections', '-Wl,-Map=build/' + target + '.map', '-o', elf])
    run([bin_dir / ('arm-none-eabi-objcopy' + suffix), '-O', 'binary', elf, out / (target + '.bin')])
    run([bin_dir / ('arm-none-eabi-size' + suffix), elf])
    print('Built', elf)


if __name__ == '__main__':
    main()
