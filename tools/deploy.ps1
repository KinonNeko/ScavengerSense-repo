<#
    Copies a finished build into the live Mod Organizer install.

    The repo and the test install are deliberately separate: source lives here,
    a built DLL goes over there. This is the trip between them, so it stops
    being a manual step you can forget and then spend twenty minutes wondering
    why a fix did not take.

    Usage:
        .\tools\deploy.ps1
        .\tools\deploy.ps1 -Target "E:\iniRePather-Skyrim\mods\Scavenger Sense - SKSE"
        .\tools\deploy.ps1 -WhatIf
#>
param(
    [string] $Target = "E:\iniRePather-Skyrim\mods\Scavenger Sense - SKSE",
    [switch] $WhatIf
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path $Target)) {
    throw "Target does not exist: $Target`nPass -Target with your own path."
}

# The DLL lands in a different place depending on the generator.
$dll = @(
    "$repo\plugin\build\Release\ScavengerSense.dll",
    "$repo\plugin\build\ScavengerSense.dll"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $dll) {
    throw "No built DLL found. Build first:`n  cmake --build plugin/build --config Release"
}

# What to copy, and where it goes relative to the mod folder root.
$jobs = @(
    @{ From = $dll;                                              To = "SKSE\Plugins\ScavengerSense.dll" }
    @{ From = "$repo\ScavengerSense.esp";                        To = "ScavengerSense.esp" }
    @{ From = "$repo\mod\README.md";                             To = "README.md" }
    @{ From = "$repo\mod\Sound\FX\ScavengerSense\sweep.wav";     To = "Sound\FX\ScavengerSense\sweep.wav" }
    @{ From = "$repo\mod\Sound\FX\ScavengerSense\README-sound.txt"; To = "Sound\FX\ScavengerSense\README-sound.txt" }
    @{ From = "$repo\mod\SKSE\Plugins\ScavengerSense\presets\Default.ini"; To = "SKSE\Plugins\ScavengerSense\presets\Default.ini" }
)
foreach ($f in Get-ChildItem "$repo\mod\SKSE\Plugins" -Filter "ScavengerSense*.ini" -ErrorAction SilentlyContinue) {
    # Never the settings file: that one belongs to the game, not to the repo.
    if ($f.Name -eq "ScavengerSense.ini") { continue }
    $jobs += @{ From = $f.FullName; To = "SKSE\Plugins\$($f.Name)" }
}

$copied = 0
$skipped = 0
foreach ($job in $jobs) {
    if (-not (Test-Path $job.From)) { continue }
    $dest = Join-Path $Target $job.To

    # Titles carry an [Assigned] block written in game. Overwriting it would
    # throw away every title typed onto somebody by hand, so it is merged.
    if ($job.To -like "*_titles.ini" -and (Test-Path $dest)) {
        $mine = Get-Content $dest -Raw -Encoding UTF8
        $marker = "[Assigned]"
        if ($mine.Contains($marker)) {
            $assigned = $mine.Substring($mine.IndexOf($marker) + $marker.Length)
            $fresh = Get-Content $job.From -Raw -Encoding UTF8
            $out = $fresh.Substring(0, $fresh.IndexOf($marker) + $marker.Length) + $assigned
            if (-not $WhatIf) {
                [System.IO.File]::WriteAllText($dest, $out, (New-Object System.Text.UTF8Encoding $false))
            }
            Write-Host "  merged   $($job.To)  (kept your [Assigned] titles)"
            $copied++
            continue
        }
    }

    if (Test-Path $dest) {
        $a = (Get-FileHash $job.From -Algorithm MD5).Hash
        $b = (Get-FileHash $dest     -Algorithm MD5).Hash
        if ($a -eq $b) { $skipped++; continue }
    }

    $dir = Split-Path -Parent $dest
    if (-not (Test-Path $dir) -and -not $WhatIf) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    if (-not $WhatIf) { Copy-Item $job.From $dest -Force }
    Write-Host "  copied   $($job.To)"
    $copied++
}

Write-Host ""
Write-Host "$copied copied, $skipped already current  ->  $Target"
if ($WhatIf) { Write-Host "(-WhatIf: nothing was written)" }
