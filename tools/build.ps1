param([switch] $ConfigureOnly, [switch] $Test, [int] $Jobs = 6)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Install Visual Studio C++ build tools first.' }
$devcmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$environment = & $env:ComSpec /d /c "call `"$devcmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialise MSVC.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
$testOption = if ($Test) { 'ON' } else { 'OFF' }
& $cmake -S "$repo/plugin" -B "$repo/plugin/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-Ddirectxtk_DIR=$repo/cmake-pkgs/directxtk" -DCOMMONLIB_PREBUILT=OFF "-DSCAVENGER_BUILD_TESTS=$testOption"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
if (-not $ConfigureOnly) {
    & $cmake --build "$repo/plugin/build" --config Release --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    if ($Test) {
        & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$repo/plugin/build" --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Offline checks failed.' }
    }
}
