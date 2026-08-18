<#
.SYNOPSIS
    Re-encodes an oversized GIF down to a byte budget.

.DESCRIPTION
    Screen-capture GIFs are usually enormous for one reason: every frame is
    written as a full-canvas image with its own local palette, so nothing is
    shared between frames. Re-encoding with a single global palette fixes that
    much before quality is touched at all.

    After that it is a straight trade between four things, and only measurement
    says which to spend:

      duration x frame rate = frame count, which dominates everything
      width, which costs roughly with area
      palette size
      how noisy the picture is, because LZW pays for every stray pixel

    That last one is why this defaults to "area" scaling rather than lanczos
    and applies a mild denoise: sharpening filters invent high-frequency detail
    that GIF cannot compress. Measured on a 16 s 2560x1440 game capture, area
    plus denoise came in 8% under lanczos at identical settings.

    The script does not guess. It walks a ladder of (width, fps, colours)
    ordered best-to-worst, binary-searches for the highest rung that lands
    under the budget, and reports what it actually measured.

    If the result comes out smaller or choppier than you wanted, the lever with
    by far the most give is -Speed or -Duration. Frame count is linear in the
    output size; width is not, and colours barely move it.

.EXAMPLE
    tools\compress-gif.ps1 "D:\downloads\Recording.gif"

.EXAMPLE
    # Trim to the interesting 6 seconds and keep full width.
    tools\compress-gif.ps1 in.gif -Start 2 -Duration 6 -MaxWidth 1280

.EXAMPLE
    # Keep the whole action but play it 1.6x faster: 38% fewer frames.
    tools\compress-gif.ps1 in.gif -Speed 1.6
#>

[CmdletBinding()]
param(
    # Source GIF, or any video ffmpeg can read.
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Path,

    # Destination. Defaults to the source name with "-small" appended.
    [string] $Output,

    # Byte budget in megabytes.
    [double] $TargetMB = 6,

    # Widest rung of the ladder. Never upscales past the source.
    [int] $MaxWidth = 1280,

    # Seconds to skip from the start.
    [double] $Start = 0,

    # Seconds to keep. 0 means to the end.
    [double] $Duration = 0,

    # Playback speed multiplier. 1.6 keeps all the action in 38% fewer frames,
    # which is the cheapest quality-per-byte lever available.
    [double] $Speed = 1.0,

    # Pin the frame rate instead of letting the ladder choose it.
    [int] $Fps = 0,

    # Pin the palette size instead of letting the ladder choose it.
    [int] $Colors = 0,

    # Downscaler. "area" compresses best; "lanczos" is sharper but larger.
    [ValidateSet('area', 'bilinear', 'bicubic', 'lanczos')]
    [string] $Scaler = 'area',

    # Denoise strength, 0 disables. Smoothing costs detail but buys real bytes.
    [ValidateRange(0, 20)]
    [double] $Denoise = 4,

    # Ordered dither strength, 0-5. Higher is a less visible pattern.
    [ValidateRange(0, 5)]
    [int] $BayerScale = 5,

    # Set to "none" for flat art or UI capture, where dithering only adds noise.
    [ValidateSet('bayer', 'none', 'sierra2_4a', 'floyd_steinberg')]
    [string] $Dither = 'bayer',

    # palettegen stats_mode. "full" suits moving footage; "diff" wins on
    # recordings where the background sits still.
    [ValidateSet('full', 'diff', 'single')]
    [string] $StatsMode = 'full',

    # Keep the normalised intermediate for inspection.
    [switch] $KeepIntermediate
)

$ErrorActionPreference = 'Stop'

# --- tools -----------------------------------------------------------------
# winget adds ffmpeg to PATH but not to an already-running shell, so look in
# the package directory too rather than making the user restart anything.
function Find-Tool {
    param([string] $Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $pkgs = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $pkgs) {
        $hit = Get-ChildItem $pkgs -Filter "$Name.exe" -Recurse -ErrorAction SilentlyContinue |
               Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

$ffmpeg  = Find-Tool 'ffmpeg'
$ffprobe = Find-Tool 'ffprobe'
if (-not $ffmpeg) {
    throw "ffmpeg not found. Install it with:  winget install --id Gyan.FFmpeg -e"
}

# Optional. On noisy footage --lossy is worth more than any other single knob.
$gifsicle = Find-Tool 'gifsicle'

# --- paths -----------------------------------------------------------------
$src = (Resolve-Path -LiteralPath $Path).Path
if (-not $Output) {
    $dir  = Split-Path $src -Parent
    $stem = [IO.Path]::GetFileNameWithoutExtension($src)
    $Output = Join-Path $dir "$stem-small.gif"
}
$outDir = Split-Path $Output -Parent
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("gifcomp-" + [IO.Path]::GetRandomFileName().Split('.')[0])
New-Item -ItemType Directory -Path $work -Force | Out-Null

function Get-MB { param([string] $p) return (Get-Item -LiteralPath $p).Length / 1MB }

# --- probe -----------------------------------------------------------------
$srcMB = Get-MB $src
$srcW = 0; $srcH = 0; $srcDur = 0.0
if ($ffprobe) {
    $j = & $ffprobe -v error -select_streams v:0 `
                    -show_entries stream=width,height -show_entries format=duration `
                    -of json $src | ConvertFrom-Json
    if ($j.streams -and $j.streams.Count -gt 0) {
        $srcW = [int] $j.streams[0].width
        $srcH = [int] $j.streams[0].height
    }
    if ($j.format -and $j.format.duration) { $srcDur = [double] $j.format.duration }
}

# Work out what the clip will be after trim and speed, because that is the
# number the budget really has to buy.
$clipDur = $srcDur
if ($Duration -gt 0) {
    $clipDur = [Math]::Min($Duration, [Math]::Max($srcDur - $Start, 0))
} elseif ($Start -gt 0) {
    $clipDur = [Math]::Max($srcDur - $Start, 0)
}
if ($Speed -ne 1.0 -and $Speed -gt 0) { $clipDur = $clipDur / $Speed }

Write-Host ""
Write-Host "  source   $(Split-Path $src -Leaf)"
$dims = if ($srcW) { "$srcW x $srcH" } else { "unknown" }
$dur  = if ($srcDur) { ("{0:N2} s" -f $srcDur) } else { "unknown" }
Write-Host ("           {0:N2} MB   {1}   {2}" -f $srcMB, $dims, $dur)
if ($clipDur -ne $srcDur) {
    Write-Host ("  clip     {0:N2} s after trim/speed" -f $clipDur)
}
Write-Host ("  budget   {0:N2} MB" -f $TargetMB)
Write-Host ""

if ($srcW -gt 0 -and $MaxWidth -gt $srcW) {
    Write-Host "  (source is narrower than -MaxWidth; capping at $srcW)"
    $MaxWidth = $srcW
}

# --- normalise -------------------------------------------------------------
# Decoding 300 MB of per-frame-palette LZW is the slow part, and the ladder
# would pay it on every rung. Pay it once into a near-lossless intermediate at
# the widest rung, then every trial encode reads that instead.
$mid = Join-Path $work 'mid.mp4'
Write-Host "  normalising source ..." -NoNewline
$t0 = Get-Date

$inArgs = @('-v', 'error', '-y')
if ($Start -gt 0)    { $inArgs += @('-ss', $Start) }
$inArgs += @('-i', $src)
if ($Duration -gt 0) { $inArgs += @('-t', $Duration) }

$midChain = "scale=${MaxWidth}:-2:flags=$Scaler"
if ($Speed -ne 1.0 -and $Speed -gt 0) { $midChain = "setpts=PTS/$Speed,$midChain" }

& $ffmpeg @inArgs -vf $midChain -r 25 `
          -c:v libx264 -crf 14 -preset veryfast -pix_fmt yuv420p -an $mid
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed to read the source." }
Write-Host (" {0:N1}s, {1:N1} MB" -f ((Get-Date) - $t0).TotalSeconds, (Get-MB $mid))

# --- ladder ----------------------------------------------------------------
# Best first. Each rung should be no larger than the one before it.
$ladder = @()
foreach ($rung in @(
    @{ w = 1.00; fps = 15; c = 256 },
    @{ w = 1.00; fps = 12; c = 256 },
    @{ w = 1.00; fps = 12; c = 192 },
    @{ w = 1.00; fps = 12; c = 128 },
    @{ w = 1.00; fps = 10; c = 128 },
    @{ w = 0.85; fps = 12; c = 128 },
    @{ w = 0.85; fps = 10; c = 128 },
    @{ w = 0.85; fps = 10; c = 96  },
    @{ w = 0.70; fps = 10; c = 128 },
    @{ w = 0.70; fps = 10; c = 96  },
    @{ w = 0.70; fps = 8;  c = 96  },
    @{ w = 0.60; fps = 10; c = 96  },
    @{ w = 0.60; fps = 8;  c = 64  },
    @{ w = 0.50; fps = 8;  c = 64  },
    @{ w = 0.50; fps = 7;  c = 64  },
    @{ w = 0.45; fps = 7;  c = 48  },
    @{ w = 0.40; fps = 6;  c = 48  },
    @{ w = 0.40; fps = 6;  c = 32  },
    @{ w = 0.35; fps = 6;  c = 32  },
    @{ w = 0.30; fps = 5;  c = 32  },
    @{ w = 0.25; fps = 5;  c = 32  },
    @{ w = 0.25; fps = 4;  c = 24  }
)) {
    $width = [int] ([Math]::Round($MaxWidth * $rung.w / 2) * 2)
    $ladder += [pscustomobject]@{
        Width  = $width
        Fps    = if ($Fps -gt 0)    { $Fps }    else { $rung.fps }
        Colors = if ($Colors -gt 0) { $Colors } else { $rung.c }
    }
}

# Pinning fps or colours collapses rungs into duplicates; drop them.
$seen = @{}
$ladder = @($ladder | Where-Object {
    $k = "$($_.Width)/$($_.Fps)/$($_.Colors)"
    if ($seen.ContainsKey($k)) { return $false }
    $seen[$k] = $true
    return $true
})

# --- encode ----------------------------------------------------------------
function Get-Chain {
    param([pscustomobject] $Cfg)
    $c = "fps=$($Cfg.Fps),scale=$($Cfg.Width):-2:flags=$Scaler"
    if ($Denoise -gt 0) {
        # Spatial and temporal smoothing. Temporal is the half that pays here:
        # it stops every pixel shimmering between frames.
        $s = $Denoise
        $t = $Denoise * 1.5
        $c = "$c,hqdn3d=${s}:${s}:${t}:${t}"
    }
    return $c
}

function Invoke-Encode {
    param([pscustomobject] $Cfg, [string] $Dest)

    $pal   = Join-Path $work ("pal-{0}-{1}-{2}.png" -f $Cfg.Width, $Cfg.Fps, $Cfg.Colors)
    $chain = Get-Chain $Cfg

    if (-not (Test-Path $pal)) {
        & $ffmpeg -v error -y -i $mid `
                  -vf "$chain,palettegen=max_colors=$($Cfg.Colors):stats_mode=$StatsMode" $pal
        if ($LASTEXITCODE -ne 0) { throw "palettegen failed." }
    }

    # Leave paletteuse "new" at its default of 0. Setting it tells the filter to
    # expect a fresh palette per frame, which writes per-frame local palettes
    # back into the output -- the exact thing that made the source huge.
    # diff_mode=rectangle limits each frame to the bounding box of what changed.
    $use = "paletteuse=dither=$Dither:diff_mode=rectangle"
    if ($Dither -eq 'bayer') {
        $use = "paletteuse=dither=bayer:bayer_scale=${BayerScale}:diff_mode=rectangle"
    }

    & $ffmpeg -v error -y -i $mid -i $pal `
              -lavfi "[0:v]$chain[x];[x][1:v]$use" `
              -loop 0 -f gif $Dest
    if ($LASTEXITCODE -ne 0) { throw "paletteuse failed." }

    if ($gifsicle) {
        $opt = "$Dest.opt"
        & $gifsicle -O3 --lossy=60 --no-warnings -o $opt $Dest
        if ($LASTEXITCODE -eq 0 -and (Test-Path $opt) -and (Get-MB $opt) -lt (Get-MB $Dest)) {
            Move-Item -LiteralPath $opt -Destination $Dest -Force
        } elseif (Test-Path $opt) {
            Remove-Item -LiteralPath $opt -Force
        }
    }

    return Get-MB $Dest
}

# Binary search the ladder for the best rung that fits. Size is monotonic
# enough across these rungs that this holds, and it costs ~5 encodes instead
# of 22.
$lo = 0
$hi = $ladder.Count - 1
$best = $null

while ($lo -le $hi) {
    $i   = [int] (($lo + $hi) / 2)
    $cfg = $ladder[$i]
    $try = Join-Path $work "try$i.gif"

    Write-Host ("  trying   {0,4}px  {1,2} fps  {2,3} colours ... " -f $cfg.Width, $cfg.Fps, $cfg.Colors) -NoNewline
    $mb = Invoke-Encode -Cfg $cfg -Dest $try
    $verdict = if ($mb -le $TargetMB) { "fits" } else { "over" }
    Write-Host ("{0,7:N2} MB  {1}" -f $mb, $verdict)

    if ($mb -le $TargetMB) {
        if ($null -eq $best -or $i -lt $best.Index) {
            $best = [pscustomobject]@{ Index = $i; Cfg = $cfg; MB = $mb; File = $try }
        }
        $hi = $i - 1          # a better-looking rung might also fit
    } else {
        $lo = $i + 1
    }
}

Write-Host ""
if ($null -eq $best) {
    # Nothing fit. Hand back the smallest thing the ladder can make rather than
    # nothing at all, and be explicit that the budget was missed.
    $floor = $ladder[$ladder.Count - 1]
    $try = Join-Path $work "floor.gif"
    Write-Host "  no rung fit the budget; encoding the floor rung ..."
    $mb = Invoke-Encode -Cfg $floor -Dest $try
    Copy-Item -LiteralPath $try -Destination $Output -Force
    Write-Warning ("Could not reach {0:N2} MB. Floor was {1:N2} MB at {2}px / {3} fps / {4} colours." -f `
                   $TargetMB, $mb, $floor.Width, $floor.Fps, $floor.Colors)
    Write-Warning ("At {0:N1} s the clip needs {1} frames before a single pixel is drawn. Use -Speed or -Duration." -f `
                   $clipDur, [int]($clipDur * $floor.Fps))
} else {
    Copy-Item -LiteralPath $best.File -Destination $Output -Force
    $outMB   = Get-MB $Output
    $frames  = [int] ($clipDur * $best.Cfg.Fps)
    Write-Host ("  output   {0}" -f (Split-Path $Output -Leaf))
    Write-Host ("           {0}px  {1} fps  {2} colours  ~{3} frames" -f `
                $best.Cfg.Width, $best.Cfg.Fps, $best.Cfg.Colors, $frames)
    Write-Host ("           {0:N2} MB  ({1:N2} MB in, {2:N0}x smaller, {3:N0}% of budget, {4:N0} KB/frame)" -f `
                $outMB, $srcMB, ($srcMB / $outMB), (100 * $outMB / $TargetMB), ($outMB * 1024 / [Math]::Max($frames, 1)))
    if (-not $gifsicle) {
        Write-Host "           (gifsicle absent - its --lossy pass is worth 20-40% on noisy footage)"
    }
}
Write-Host ""

if ($KeepIntermediate) {
    Write-Host "  intermediate kept at $work"
} else {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
