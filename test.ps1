# Build the tests of src/core with the PC's g++ and run them.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

New-Item -ItemType Directory -Force build-tests | Out-Null
g++ -std=c++20 -Wall -Wextra -Werror -Isrc `
    tests/core_tests.cpp src/core/crsf.cpp src/core/channels.cpp src/core/settings.cpp src/core/joystick.cpp src/core/mode.cpp src/core/led.cpp `
    -o build-tests/core_tests.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& build-tests/core_tests.exe
exit $LASTEXITCODE
