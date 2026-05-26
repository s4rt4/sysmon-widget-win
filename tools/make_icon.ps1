# Generates src/native/app.ico from the same design as the runtime tray
# icon (BuildAppIcon in widget_app.cpp). One-time generator — run when the
# design changes. Output is committed to the repo so the build doesn't
# depend on PowerShell at compile time.

Add-Type -AssemblyName System.Drawing

function New-IconBitmap {
    param([int]$size)
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.Clear([System.Drawing.Color]::Transparent)

    # Rounded-card background (#0E2422).
    $pad   = $size * 0.06
    $w     = $size - 2 * $pad
    $h     = $size - 2 * $pad
    $r     = $size * 0.22
    $path  = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddArc($pad,             $pad,             $r, $r, 180, 90)
    $path.AddArc($pad + $w - $r,   $pad,             $r, $r, 270, 90)
    $path.AddArc($pad + $w - $r,   $pad + $h - $r,   $r, $r, 0,   90)
    $path.AddArc($pad,             $pad + $h - $r,   $r, $r, 90,  90)
    $path.CloseFigure()

    $bg = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 14, 36, 34))
    $g.FillPath($bg, $path)
    $bg.Dispose()

    # Sky-blue rim.
    $rim = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 64, 196, 255)), ([float]($size * 0.045))
    $g.DrawPath($rim, $path)
    $rim.Dispose()

    # Orange ring (gauge dial).
    $rp = $size * 0.24
    $rs = $size - 2 * $rp
    $ring = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 152, 0)), ([float]($size * 0.10))
    $ring.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $ring.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawArc($ring, [float]$rp, [float]$rp, [float]$rs, [float]$rs, -130, 280)
    $ring.Dispose()

    # White center dot.
    $dp  = $size * 0.40
    $dot = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 247, 235, 237))
    $g.FillEllipse($dot, [float]$dp, [float]$dp, [float]($size - 2 * $dp), [float]($size - 2 * $dp))
    $dot.Dispose()

    $path.Dispose()
    $g.Dispose()
    return $bmp
}

function Get-PngBytes {
    param($bitmap)
    $ms = New-Object IO.MemoryStream
    $bitmap.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return ,$bytes
}

# Pack multiple resolutions so Windows picks the sharpest for every
# context (Explorer 16/32/48, Alt-Tab/jumplist 64-128, large-icon 256).
$sizes = @(16, 32, 48, 64, 128, 256)
$entries = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap -size $s
    $entries += [PSCustomObject]@{
        Size = $s
        Data = (Get-PngBytes -bitmap $bmp)
    }
    $bmp.Dispose()
}

$out = New-Object IO.MemoryStream
$bw  = New-Object IO.BinaryWriter $out

# ICONDIR header.
$bw.Write([UInt16]0)              # reserved
$bw.Write([UInt16]1)              # type = ICO
$bw.Write([UInt16]$entries.Count) # number of images

# ICONDIRENTRY for each size.
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $byte = if ($e.Size -ge 256) { 0 } else { $e.Size }  # 0 means 256
    $bw.Write([Byte]$byte)
    $bw.Write([Byte]$byte)
    $bw.Write([Byte]0)              # palette entries
    $bw.Write([Byte]0)              # reserved
    $bw.Write([UInt16]1)            # planes
    $bw.Write([UInt16]32)           # bits per pixel
    $bw.Write([UInt32]$e.Data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $e.Data.Length
}

# Image payloads (PNG bytes — Vista+ supports PNG-compressed ICO entries).
foreach ($e in $entries) { $bw.Write($e.Data) }
$bw.Flush()

$dest = Join-Path $PSScriptRoot "..\src\native\app.ico"
[IO.File]::WriteAllBytes($dest, $out.ToArray())
$out.Dispose()

Write-Host "Wrote $dest ($((Get-Item $dest).Length) bytes, $($entries.Count) sizes: $($sizes -join ','))"
