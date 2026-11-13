param([Parameter(Mandatory=$true)][string]$Image,[Parameter(Mandatory=$true)][string]$OutputFolder)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$destination=[IO.Path]::GetFullPath($OutputFolder)
if(Test-Path -LiteralPath $destination){throw 'Output exists'}
New-Item -ItemType Directory -Path $destination | Out-Null
$source=[System.Drawing.Bitmap]::FromFile([IO.Path]::GetFullPath($Image))
$face=[System.Drawing.Bitmap]::new(1024,1024)
$graphics=[System.Drawing.Graphics]::FromImage($face)
try {
 $graphics.Clear([System.Drawing.Color]::Black)
 $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
 # Exact raster-to-native texture packing. Keep complete artwork in a square face.
 $scale=[Math]::Min(1024.0/$source.Width,1024.0/$source.Height)
 $width=[int]($source.Width*$scale);$height=[int]($source.Height*$scale)
 $graphics.DrawImage($source,[System.Drawing.Rectangle]::new([int]((1024-$width)/2),[int]((1024-$height)/2),$width,$height))
 $face.Save((Join-Path $destination 'face-preview.png'),[System.Drawing.Imaging.ImageFormat]::Png)
 for($row=0;$row -lt 2;$row++){
  for($column=0;$column -lt 2;$column++){
   $tile=$face.Clone([System.Drawing.Rectangle]::new($column*512,$row*512,512,512),[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
   try{$tile.Save((Join-Path $destination ('{0:000}_{1:000}.jpg' -f $row,$column)),[System.Drawing.Imaging.ImageFormat]::Jpeg)}finally{$tile.Dispose()}
  }
 }
} finally {$graphics.Dispose();$face.Dispose();$source.Dispose()}
