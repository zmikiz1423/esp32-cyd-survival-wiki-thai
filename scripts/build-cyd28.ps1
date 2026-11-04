# Build the Thai CYD firmware and merge a flash-at-0x0 image.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwareDir = Join-Path $projectRoot 'firmware'
Push-Location -LiteralPath $firmwareDir
try {
    python -m platformio run -e survival-wiki-28
    if ($LASTEXITCODE -ne 0) { throw 'PlatformIO build failed' }
    $buildDir = Join-Path $firmwareDir '.pio/build/survival-wiki-28'
    $pioCore = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE '.platformio' }
    $esptoolPath = Join-Path $pioCore 'packages/tool-esptoolpy/esptool.py'
    $bootAppPath = Join-Path $pioCore 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    $outputPath = Join-Path $projectRoot 'firmware-CYD28.bin'
    python $esptoolPath --chip esp32 merge_bin -o $outputPath --flash_mode dio --flash_freq 40m --flash_size 4MB `
        0x1000 (Join-Path $buildDir 'bootloader.bin') `
        0x8000 (Join-Path $buildDir 'partitions.bin') `
        0xe000 $bootAppPath `
        0x10000 (Join-Path $buildDir 'firmware.bin')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware merge failed' }
    $destinations = @('firmware/bin/survival-wiki-28-full.bin','web-flasher/firmware/survival-wiki-28-full.bin','docs/firmware/survival-wiki-28-full.bin')
    foreach ($relative in $destinations) {
        $targetPath = Join-Path $projectRoot $relative
        if (Test-Path -LiteralPath (Split-Path -Parent $targetPath)) { Copy-Item -LiteralPath $outputPath -Destination $targetPath -Force }
    }
    Get-FileHash -LiteralPath $outputPath -Algorithm SHA256
}
finally { Pop-Location }
