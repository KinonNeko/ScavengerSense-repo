# Sets up the portable MSVC toolchain (D:\msvc, unpacked via msvc-wine vsdownload.py)
# for native Windows builds. Dot-source this, then run cmake/ninja as usual.
$msvc = "D:\msvc\VC\Tools\MSVC\14.44.35207"
$sdk  = "D:\msvc\Windows Kits\10"
$sdkVer = "10.0.26100.0"

$env:INCLUDE = "$msvc\include;$sdk\Include\$sdkVer\ucrt;$sdk\Include\$sdkVer\um;$sdk\Include\$sdkVer\shared;$sdk\Include\$sdkVer\winrt;$sdk\Include\$sdkVer\cppwinrt"
$env:LIB     = "$msvc\lib\x64;$sdk\Lib\$sdkVer\ucrt\x64;$sdk\Lib\$sdkVer\um\x64"
$env:PATH    = "$msvc\bin\Hostx64\x64;$sdk\bin\$sdkVer\x64;C:\Program Files\CMake\bin;$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;$env:PATH"
