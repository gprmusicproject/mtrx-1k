# Draws the pictures of the setup wizard from the icon of the app and a screenshot of the plugin.
#   -Icon        Assets\Icon\MTRX-1K.png (the maker's symbol on its tile)
#   -Screenshot  a capture of the plugin window with the editor open, without title bar
# Writes 24 bit BMP files into installer\images.
param([string]$Icon = "$PSScriptRoot\..\Assets\Icon\MTRX-1K.png", [string]$Screenshot = "$PSScriptRoot\images\plugin_source.png")
Add-Type -AssemblyName System.Drawing
$out = "$PSScriptRoot\images"
New-Item -ItemType Directory -Force $out | Out-Null

function New-Canvas([int]$w, [int]$h) {
    $b = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'; $g.TextRenderingHint = 'AntiAliasGridFit'
    return @{ B = $b; G = $g }
}
function Save-Bmp($c, [string]$name) { $c.G.Dispose(); $c.B.Save("$out\$name", [System.Drawing.Imaging.ImageFormat]::Bmp); $c.B.Dispose(); "$name" }

$iconImg = [System.Drawing.Image]::FromFile((Resolve-Path $Icon))

# the tall picture at the left of the first and last page: 164 x 314 at 100 %, drawn at twice that
$w = 328; $h = 628
$c = New-Canvas $w $h
$grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.Point 0, 0), (New-Object System.Drawing.Point 0, $h), ([System.Drawing.Color]::FromArgb(255, 42, 45, 51)), ([System.Drawing.Color]::FromArgb(255, 14, 15, 18))
$c.G.FillRectangle($grad, 0, 0, $w, $h)
$c.G.DrawImage($iconImg, 54, 70, 220, 220)
$fmt = New-Object System.Drawing.StringFormat; $fmt.Alignment = 'Center'
$title = New-Object System.Drawing.Font 'Georgia', 40, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$small = New-Object System.Drawing.Font 'Arial', 19, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 244, 244, 244))
$grey = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 170, 176, 186))
$blue = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 47, 127, 230))
$c.G.DrawString('MTRX-1K', $title, $white, (New-Object System.Drawing.RectangleF 0, 330, $w, 60), $fmt)
$c.G.FillRectangle($blue, 84, 392, 160, 5)
$c.G.DrawString('GPR Music Project', $small, $grey, (New-Object System.Drawing.RectangleF 0, 412, $w, 30), $fmt)
Save-Bmp $c 'wizard_side.bmp'

# the small picture at the top right of the inner pages: 55 x 55 at 100 %, drawn at three times that
$c = New-Canvas 165 165
$c.G.Clear([System.Drawing.Color]::White)
$c.G.DrawImage($iconImg, 6, 6, 153, 153)
Save-Bmp $c 'wizard_small.bmp'

# the plugin itself, for the first page, at the sizes of the usual screen scalings
if (Test-Path $Screenshot) {
    $shot = [System.Drawing.Image]::FromFile((Resolve-Path $Screenshot))
    foreach ($s in @(@('100', 400), @('150', 600), @('200', 800))) {
        $pw = [int]$s[1]; $ph = [int][Math]::Round($pw * $shot.Height / $shot.Width)
        $c = New-Canvas $pw $ph
        $c.G.DrawImage($shot, 0, 0, $pw, $ph)
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 120, 126, 136)), 1
        $c.G.DrawRectangle($pen, 0, 0, $pw - 1, $ph - 1)
        Save-Bmp $c "plugin_$($s[0]).bmp"
        "  $pw x $ph"
    }
    $shot.Dispose()
} else { "no screenshot at ${Screenshot}, the plugin pictures were not drawn" }
$iconImg.Dispose()
