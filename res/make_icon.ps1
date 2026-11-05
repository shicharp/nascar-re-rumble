# Genera res/nascar.ico (icono del exe): bandera a cuadros ondeando sobre un cuadrado rojo redondeado.
# Dibujado aqui, sin nada del juego. Uso: powershell -File res\make_icon.ps1
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

    # mastil
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 235, 235, 235))), [float](52 * $k), [float](44 * $k), [float]([Math]::Max(1, 14 * $k)), [float](172 * $k))

    # bandera: rejilla de cols x filas; cada esquina baja y = A * sin(fase) segun su x (la onda)
    $cols = if ($s -le 24) { 3 } else { 5 }; $rows = if ($s -le 24) { 2 } else { 4 }
    $x0 = 66 * $k; $y0 = 50 * $k; $fw = 150 * $k; $fh = 100 * $k; $amp = 10 * $k
    $cw = $fw / $cols; $ch = $fh / $rows
    $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $black = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 20, 20, 24))
    for ($i = 0; $i -lt $cols; $i++) {
        for ($j = 0; $j -lt $rows; $j++) {
            $xa = $x0 + $i * $cw; $xb = $xa + $cw
            # media onda a lo ancho (0..2pi), con mas amplitud lejos del mastil
            $ya = $y0 + $j * $ch + $amp * [Math]::Sin(2 * [Math]::PI * $i / $cols) * ($i / $cols + 0.3)
            $yb = $y0 + $j * $ch + $amp * [Math]::Sin(2 * [Math]::PI * ($i + 1) / $cols) * (($i + 1) / $cols + 0.3)
            $pts = [System.Drawing.PointF[]]@(
                (New-Object System.Drawing.PointF $xa, $ya), (New-Object System.Drawing.PointF $xb, $yb),
                (New-Object System.Drawing.PointF $xb, ($yb + $ch)), (New-Object System.Drawing.PointF $xa, ($ya + $ch)))
            $g.FillPolygon($(if ((($i + $j) % 2) -eq 0) { $white } else { $black }), $pts)
        }
    }
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
"nascar.ico: $($out.Length) bytes"
