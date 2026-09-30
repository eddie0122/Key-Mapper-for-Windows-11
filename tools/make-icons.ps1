# Regenerates res/keymapper.ico and res/keymapper-paused.ico.
# Run with Windows PowerShell 5.1:  powershell -ExecutionPolicy Bypass -File tools\make-icons.ps1
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$resDir = Join-Path $PSScriptRoot '..\res'

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function New-IconPng([int]$size, [bool]$paused) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = [float]$size
    $pad = [Math]::Max(0.5, $s * 0.04)
    $base = if ($paused) { [System.Drawing.Color]::FromArgb(255, 96, 96, 96) } else { [System.Drawing.Color]::FromArgb(255, 0, 95, 184) }
    $top = if ($paused) { [System.Drawing.Color]::FromArgb(255, 150, 150, 150) } else { [System.Drawing.Color]::FromArgb(255, 58, 150, 221) }

    # Key cap: darker base with a lighter top face.
    $outer = New-RoundedRect $pad $pad ($s - 2 * $pad) ($s - 2 * $pad) ($s * 0.2)
    $g.FillPath((New-Object System.Drawing.SolidBrush $base), $outer)
    $inset = $s * 0.12
    $face = New-RoundedRect ($pad + $inset * 0.6) ($pad + $inset * 0.45) ($s - 2 * $pad - 1.2 * $inset) ($s - 2 * $pad - 1.5 * $inset) ($s * 0.14)
    $g.FillPath((New-Object System.Drawing.SolidBrush $top), $face)

    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    if ($paused) {
        # Pause bars.
        $bw = $s * 0.13; $bh = $s * 0.38; $cy = $s * 0.28
        $g.FillRectangle($white, $s * 0.33, $cy, $bw, $bh)
        $g.FillRectangle($white, $s * 0.54, $cy, $bw, $bh)
    } else {
        # Arrow from one key to another: "A -> B" idea as a bold right arrow.
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([Math]::Max(1.5, $s * 0.11))
        $pen.StartCap = 'Round'; $pen.EndCap = 'Round'; $pen.LineJoin = 'Round'
        $cy = $s * 0.46
        $g.DrawLine($pen, $s * 0.28, $cy, $s * 0.70, $cy)
        $g.DrawLine($pen, $s * 0.55, $cy - $s * 0.15, $s * 0.71, $cy)
        $g.DrawLine($pen, $s * 0.55, $cy + $s * 0.15, $s * 0.71, $cy)
    }
    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $ms.ToArray()
}

function Write-Ico([string]$path, [bool]$paused) {
    $images = @()
    foreach ($sz in $sizes) { $images += , (New-IconPng $sz $paused) }
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter $fs
    $bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $sz = $sizes[$i]; $data = $images[$i]
        $dim = if ($sz -ge 256) { 0 } else { $sz }
        $bw.Write([Byte]$dim); $bw.Write([Byte]$dim); $bw.Write([Byte]0); $bw.Write([Byte]0)
        $bw.Write([UInt16]1); $bw.Write([UInt16]32)
        $bw.Write([UInt32]$data.Length); $bw.Write([UInt32]$offset)
        $offset += $data.Length
    }
    foreach ($data in $images) { $bw.Write($data) }
    $bw.Close()
}

Write-Ico (Join-Path $resDir 'keymapper.ico') $false
Write-Ico (Join-Path $resDir 'keymapper-paused.ico') $true
Write-Host "Icons written to $resDir"
