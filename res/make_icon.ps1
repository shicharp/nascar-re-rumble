# Genera res/nascar.ico (icono del exe): una R dorada en cursiva, con contorno negro, sobre un cuadrado rojo redondeado.
# Dibujado aqui con la fuente Impact de Windows, sin nada del juego. Uso: powershell -File res\make_icon.ps1 [preview.png]
# (con un argumento guarda tambien la imagen de 256 en ese PNG, para verla)
param([string]$Preview)
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function Draw-Icon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $s / 256.0

    # fondo: cuadrado redondeado con degradado rojo
    $r = 48 * $k; $m = 8 * $k; $w = $s - 2 * $m
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddArc($m, $m, 2 * $r, 2 * $r, 180, 90)
    $p.AddArc($m + $w - 2 * $r, $m, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($m + $w - 2 * $r, $m + $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $p.AddArc($m, $m + $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure()
    $br = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, 0), (New-Object System.Drawing.PointF $s, $s), ([System.Drawing.Color]::FromArgb(255, 226, 38, 38)), ([System.Drawing.Color]::FromArgb(255, 120, 10, 16))
    $g.FillPath($br, $p)

    # la R: contorno de la letra, inclinada (cizalla x += -0.18 * y, como cursiva) y encajada en el cuadrado
    $font = New-Object System.Drawing.FontFamily 'Impact'
    $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $gp.AddString('R', $font, 0, 100.0, (New-Object System.Drawing.PointF 0, 0), [System.Drawing.StringFormat]::GenericTypographic)
    $shear = New-Object System.Drawing.Drawing2D.Matrix 1, 0, -0.18, 1, 0, 0
    $gp.Transform($shear)
    $b = $gp.GetBounds()
    $box = 176 * $k                                    # alto de la letra dentro del icono de 256
    $sc = $box / [Math]::Max($b.Width, $b.Height)
    $fit = New-Object System.Drawing.Drawing2D.Matrix
    $fit.Translate($s / 2, $s / 2 + 2 * $k)
    $fit.Scale($sc, $sc)
    $fit.Translate(-($b.X + $b.Width / 2), -($b.Y + $b.Height / 2))
    $gp.Transform($fit)
    $lb = $gp.GetBounds()

    # sombra desplazada abajo a la derecha, contorno negro grueso y relleno dorado
    $sh = $gp.Clone()
    $off = New-Object System.Drawing.Drawing2D.Matrix
    $off.Translate([float](6 * $k), [float](7 * $k))
    $sh.Transform($off)
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(110, 40, 0, 0))), $sh)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 18, 14, 10)), ([float][Math]::Max(1.2, 14 * $k))
    $pen.LineJoin = 'Round'
    $g.DrawPath($pen, $gp)

    # dorado metalico: degradado vertical claro arriba, banda brillante, ocre en medio y bronce oscuro abajo
    $gold = New-Object System.Drawing.Drawing2D.LinearGradientBrush (New-Object System.Drawing.PointF 0, ($lb.Top - 1)), (New-Object System.Drawing.PointF 0, ($lb.Bottom + 1)), ([System.Drawing.Color]::White), ([System.Drawing.Color]::Black)
    $blend = New-Object System.Drawing.Drawing2D.ColorBlend 5
    $blend.Colors = [System.Drawing.Color[]]@(
        [System.Drawing.Color]::FromArgb(255, 255, 246, 190),
        [System.Drawing.Color]::FromArgb(255, 250, 214, 90),
        [System.Drawing.Color]::FromArgb(255, 214, 150, 30),
        [System.Drawing.Color]::FromArgb(255, 245, 200, 80),
        [System.Drawing.Color]::FromArgb(255, 140, 80, 14))
    $blend.Positions = [float[]]@(0.0, 0.3, 0.55, 0.72, 1.0)
    $gold.InterpolationColors = $blend
    $g.FillPath($gold, $gp)

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $ms.ToArray()
}

# ICO con las imagenes en PNG (Windows Vista en adelante): cabecera, una entrada de 16 bytes por tamano y los PNG
$sizes = 16, 24, 32, 48, 64, 256
$pngs = @($sizes | ForEach-Object { , (Draw-Icon $_) })
$out = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $out
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$off = 6 + 16 * $sizes.Count
for ($n = 0; $n -lt $sizes.Count; $n++) {
    $d = $sizes[$n] % 256   # 256 se escribe como 0
    $bw.Write([byte]$d); $bw.Write([byte]$d); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32); $bw.Write([UInt32]$pngs[$n].Length); $bw.Write([UInt32]$off)
    $off += $pngs[$n].Length
}
foreach ($png in $pngs) { $bw.Write($png) }
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'nascar.ico'), $out.ToArray())
if ($Preview) { [System.IO.File]::WriteAllBytes($Preview, $pngs[-1]) }
"nascar.ico: $($out.Length) bytes"
