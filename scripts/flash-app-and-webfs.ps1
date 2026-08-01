<#
.SYNOPSIS
    Flash the app firmware and the webfs LittleFS image together.

.DESCRIPTION
    Runs the two-step sequence documented in CLAUDE.md:
      1. `pio run --target upload` (app firmware only).
      2. Build `littlefs_webfs_bin` and write it to 0x510000 with esptool.

    Deliberately does NOT use `pio run --target uploadfs`: this project's
    partition table has two spiffs-type partitions (webfs, imagefs), and
    PlatformIO's built-in uploadfs picks the *last* matching partition in
    the CSV (imagefs, 0x630000) while still sourcing content from the
    top-level data/ directory (webfs content). Using uploadfs here would
    silently overwrite the user's stored images with WebUI files. This
    script never touches the imagefs partition (0x630000) or the
    littlefs_imagefs_bin target.

.PARAMETER Port
    COM port to flash (the CH343 UART upload port, not the native USB
    Serial/JTAG console port -- re-check the port before every run, see
    CLAUDE.md's USB notes).

.PARAMETER Environment
    PlatformIO environment name. Defaults to paperframe-s3.

.EXAMPLE
    .\scripts\flash-app-and-webfs.ps1 -Port COM7
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$Environment = "paperframe-s3"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

$pio = ".\.venv\Scripts\pio.exe"
$cmake = ".\.pio\packages\tool-cmake\bin\cmake.exe"
$python = ".\.venv\Scripts\python.exe"
$buildDir = ".\.pio\build\$Environment"
$webfsBin = Join-Path $buildDir "webfs.bin"

foreach ($tool in @($pio, $cmake, $python)) {
    if (-not (Test-Path $tool)) {
        throw "Required tool not found: $tool (run from repo root with .venv set up)"
    }
}

Write-Host "==> [1/3] Building + uploading app firmware to $Port (env=$Environment) ..." -ForegroundColor Cyan
& $pio run -e $Environment --target upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
    throw "pio run --target upload failed (exit $LASTEXITCODE)"
}

Write-Host "==> [2/3] Building webfs image (littlefs_webfs_bin) ..." -ForegroundColor Cyan
$env:IDF_PATH = (Resolve-Path ".\.pio\packages\framework-espidf").Path
& $cmake --build $buildDir --target littlefs_webfs_bin
if ($LASTEXITCODE -ne 0) {
    throw "cmake --build littlefs_webfs_bin failed (exit $LASTEXITCODE)"
}
if (-not (Test-Path $webfsBin)) {
    throw "Expected webfs image not found at $webfsBin after build"
}

Write-Host "==> [3/3] Flashing webfs.bin to 0x510000 on $Port (imagefs at 0x630000 is never touched) ..." -ForegroundColor Cyan
& $python -m esptool --chip esp32s3 --port $Port write-flash 0x510000 $webfsBin
if ($LASTEXITCODE -ne 0) {
    throw "esptool write-flash webfs failed (exit $LASTEXITCODE)"
}

Write-Host "==> Done: firmware + webfs flashed, imagefs untouched." -ForegroundColor Green
