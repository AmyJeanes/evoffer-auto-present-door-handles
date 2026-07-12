# Build the EVOffer custom app -> CX_CAN.bin using the ARM toolchain bundled with
# PlatformIO. No 'make' and no PATH changes needed. Run from anywhere:
#   pwsh firmware\build.ps1
# If your toolchain lives elsewhere, pass -Toolchain "<path to the bin dir>".
param(
    [string]$Toolchain = "$env:USERPROFILE\.platformio\packages\toolchain-gccarmnoneeabi\bin"
)
$ErrorActionPreference = 'Stop'

$gcc     = Join-Path $Toolchain 'arm-none-eabi-gcc.exe'
$objcopy = Join-Path $Toolchain 'arm-none-eabi-objcopy.exe'
$size    = Join-Path $Toolchain 'arm-none-eabi-size.exe'
if (-not (Test-Path $gcc)) { throw "arm-none-eabi-gcc not found under '$Toolchain' - pass -Toolchain <path>" }

Set-Location $PSScriptRoot

$flags = @(
    '-mcpu=cortex-m4','-mthumb','-mfloat-abi=soft','-O2','-Wall','-ffreestanding','-fno-common',
    '-T','linker.ld','-nostdlib','-nostartfiles','-Wl,--gc-sections',
    'src/main.c','-o','firmware.elf'
)
& $gcc @flags
& $objcopy -O binary firmware.elf CX_CAN.bin
& $size firmware.elf
Write-Host ("CX_CAN.bin: {0} bytes  ->  copy to SD card root, or SWD-flash to 0x08005000" -f (Get-Item CX_CAN.bin).Length)
