param(
    [switch]$Usb,
    [switch]$HwTest
)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot.Replace('\', '/')
$python = "$repoRoot/.venv/Scripts/python.exe"
if (-not (Test-Path $python) -or -not (Test-Path "$repoRoot/.deps/zephyr/VERSION")) {
    throw "Workspace is not set up. Follow the setup steps in docs/ZEPHYR.md first."
}
# CMake and Ninja are installed in the venv.
$env:PATH = "$repoRoot/.venv/Scripts;$env:PATH"
$env:ZEPHYR_BASE = "$repoRoot/.deps/zephyr"
$buildDir = if ($Usb) { "$repoRoot/build-usb" } elseif ($HwTest) { "$repoRoot/build-hwtest" } else { "$repoRoot/build" }
$cmakeArgs = @("-DBOARD_ROOT=$repoRoot")
if ($Usb) {
    $cmakeArgs += "-DEXTRA_CONF_FILE=$repoRoot/usb.conf"
    $cmakeArgs += "-DEXTRA_DTC_OVERLAY_FILE=$repoRoot/usb.overlay"
}
if ($HwTest) {
    $cmakeArgs += "-DEXTRA_CONF_FILE=$repoRoot/hwtest.conf"
    $cmakeArgs += "-DEXTRA_DTC_OVERLAY_FILE=$repoRoot/hwtest.overlay"
}
& $python -m west build --no-sysbuild -b openliteradio -d $buildDir $repoRoot -- @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $python "$repoRoot/tools/check_image.py" "$buildDir/zephyr/zephyr.bin" --elf "$buildDir/zephyr/zephyr.elf"
exit $LASTEXITCODE
