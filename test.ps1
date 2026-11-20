# Build the tests of src/core with the PC's g++ and run them.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

New-Item -ItemType Directory -Force build-tests | Out-Null
g++ -std=c++20 -Wall -Wextra -Werror -Isrc `
    tests/core_tests.cpp src/core/crsf.cpp src/core/channels.cpp src/core/settings.cpp src/core/joystick.cpp src/core/mode.cpp src/core/led.cpp src/core/xbox.cpp src/core/sound.cpp `
    -o build-tests/core_tests.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& build-tests/core_tests.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The configurator shows the project's version; it must be the one in VERSION.
$v = @{}
Get-Content VERSION | ForEach-Object { if ($_ -match '^(\w+)\s*=\s*(\S*)') { $v[$Matches[1]] = $Matches[2] } }
$version = "$($v.VERSION_MAJOR).$($v.VERSION_MINOR).$($v.PATCHLEVEL)"
if (-not (Select-String -Path web/index.html -SimpleMatch "const WEB_VERSION = `"$version`";" -Quiet)) {
    Write-Output "web/index.html: WEB_VERSION is not $version from VERSION"
    exit 1
}
Write-Output "web version matches VERSION ($version)"

# Firmware and configurator must speak the same protocol number (hello line).
$fwProto = [regex]::Match((Get-Content src/host.cpp -Raw), 'printk\("hello OpenLiteRadio %s proto=(\d+)').Groups[1].Value
$webProto = [regex]::Match((Get-Content web/index.html -Raw), 'const PROTOCOL = (\d+);').Groups[1].Value
if (-not $fwProto -or $fwProto -ne $webProto) {
    Write-Output "protocol: src/host.cpp says '$fwProto', web/index.html PROTOCOL '$webProto'"
    exit 1
}
Write-Output "protocol matches ($fwProto)"
exit 0
