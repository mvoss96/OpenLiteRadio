param(
    [Parameter(Mandatory=$true)][string]$ZephyrBase,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [switch]$Usb
)
$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot.Replace('\', '/')
$env:ZEPHYR_BASE = $ZephyrBase
$env:ZEPHYR_SDK_INSTALL_DIR = "$ToolchainRoot/opt/zephyr-sdk"
$env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr/gnu'
$env:PATH = "$ToolchainRoot/opt/bin;$ToolchainRoot/opt/bin/Scripts;$ToolchainRoot/bin;$ToolchainRoot/mingw64/bin;$env:PATH"
$buildDir = if ($Usb) { "$repoRoot/build-usb" } else { "$repoRoot/build" }
$cmakeArgs = @("-DBOARD_ROOT=$repoRoot", "-DZEPHYR_BASE=$ZephyrBase")
$halPath = "$repoRoot/.deps/hal_stm32"
if (Test-Path "$halPath/zephyr/module.yml") {
    $cmakeArgs += "-DZEPHYR_EXTRA_MODULES=$halPath"
}
if ($Usb) {
    $cmakeArgs += "-DEXTRA_CONF_FILE=$repoRoot/usb.conf"
    $cmakeArgs += "-DEXTRA_DTC_OVERLAY_FILE=$repoRoot/usb.overlay"
}
& "$ToolchainRoot/opt/bin/python.exe" -m west build --no-sysbuild -b openliteradio -d $buildDir $repoRoot -- @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$ToolchainRoot/opt/bin/python.exe" "$repoRoot/tools/check_image.py" "$buildDir/zephyr/zephyr.bin" --elf "$buildDir/zephyr/zephyr.elf"
exit $LASTEXITCODE
