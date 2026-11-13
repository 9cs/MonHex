# Draws the MonHex logo (the "honeycomb": six blue hexagons around a red and
# white one) as:
#   icon.jpg        - 256x256 app icon for the Homebrew Menu (dark background)
#   romfs/logo.png  - 256x256 with a transparent background, for the app's header
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot

function Hex([float]$cx, [float]$cy, [float]$r) {
    $pts = @()
    foreach ($k in 0..5) {
        $a = [Math]::PI / 180 * (-90 + 60 * $k)
        $pts += New-Object System.Drawing.PointF ($cx + $r * [Math]::Cos($a)), ($cy + $r * [Math]::Sin($a))
    }
    return ,$pts
}

function Draw-Logo([System.Drawing.Graphics]$g, [float]$size) {
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $c = $size / 2
    $s = $size / 256 * 1.83          # the drawing was made on a 140 px tile
    $r = 15 * $s                      # each hexagon
    $dx = 29.4 * $s; $hx = 14.7 * $s; $vy = 25.5 * $s
    $blue  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 46, 127, 168))
    $red   = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 233, 69, 96))
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 244, 244, 248))
    $dark  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 20, 21, 29))
    foreach ($o in @(@(-$dx, 0), @($dx, 0), @(-$hx, -$vy), @($hx, -$vy), @(-$hx, $vy), @($hx, $vy))) {
        $g.FillPolygon($blue, (Hex ($c + $o[0]) ($c + $o[1]) $r))
    }
    # Centre: red top, white bottom, dark button
    $h = Hex $c $c $r
    $top = @($h[5], $h[0], $h[1], (New-Object System.Drawing.PointF $h[1].X, $c), (New-Object System.Drawing.PointF $h[5].X, $c))
    $bottom = @((New-Object System.Drawing.PointF $h[4].X, $c), (New-Object System.Drawing.PointF $h[2].X, $c), $h[2], $h[3], $h[4])
    $g.FillPolygon($red, $top)
    $g.FillPolygon($white, $bottom)
    $b = 4 * $s
    $g.FillEllipse($dark, $c - $b, $c - $b, 2 * $b, 2 * $b)
}

# App icon (JPEG has no transparency: dark background like the app)
$bmp = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(255, 29, 31, 43))
Draw-Logo $g 256
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$params = New-Object System.Drawing.Imaging.EncoderParameters 1
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 95L
$bmp.Save((Join-Path $root "icon.jpg"), $codec, $params)
$g.Dispose(); $bmp.Dispose()

# Header logo with a transparent background
$bmp = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::Transparent)
Draw-Logo $g 256
$bmp.Save((Join-Path $root "romfs/logo.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "icon.jpg and romfs/logo.png written"
