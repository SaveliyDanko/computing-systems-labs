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
              '-DSTM32F427xx', '-ICore/Inc', '-IDrivers/CMSIS/Include',
              '-IDrivers/CMSIS/Device/ST/STM32F4xx/Include']
    if args.test:
        sources = [ROOT/'Core/Src'/s for s in ('button.c','traffic.c','gpio_driver.c','led.c')]
        sources += [ROOT/'tests/test_lab1.c']
        target = 'tests'
    else:
        common += ['-DUSE_HAL_DRIVER', '-IDrivers/STM32F4xx_HAL_Driver/Inc']
        sources = sorted((ROOT/'Core/Src').glob('*.c'))
        sources += sorted((ROOT/'Core/Startup').glob('*.s'))
        hal = ROOT/'Drivers/STM32F4xx_HAL_Driver/Src'
        sources += [hal/f'stm32f4xx_hal{name}.c' for name in ('','_cortex','_rcc','_rcc_ex','_pwr','_pwr_ex','_flash','_flash_ex')]
        target = 'lab1'
    objects = []
    for source in sources:
        obj = out / (target + '_' + source.stem + '.o')
        vendor_flags = ['-Wno-unused-parameter'] if 'Drivers' in source.parts else []
        run([gcc, *common, *vendor_flags, '-c', source, '-o', obj])
        objects.append(obj)
    elf = out / (target + '.elf')
    flags = ['-nostdlib', '-Ttests/test.ld', '-Wl,-e,test_entry'] if args.test else [
        '-TSTM32F427VGTX_FLASH.ld', '--specs=nano.specs', '--specs=nosys.specs', '-static', '-lc', '-lm']
    run([gcc, '-mcpu=cortex-m4', '-mthumb', '-mfloat-abi=soft', *objects,
         *flags, '-Wl,--gc-sections', '-Wl,-Map=build/'+target+'.map', '-o', elf])
    run([bin_dir/('arm-none-eabi-objcopy'+suffix), '-O', 'binary', elf, out/(target+'.bin')])
    run([bin_dir/('arm-none-eabi-size'+suffix), elf])
    print('Built', elf)

if __name__ == '__main__':
    main()
