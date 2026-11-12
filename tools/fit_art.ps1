param([Parameter(Mandatory=$true)][string]$InputFolder,[Parameter(Mandatory=$true)][string]$OutputFolder)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$inputRoot=[IO.Path]::GetFullPath($InputFolder)
$outputRoot=[IO.Path]::GetFullPath($OutputFolder)
if(Test-Path -LiteralPath $outputRoot){throw 'Output already exists'}
$credits=Get-Content -LiteralPath (Join-Path $inputRoot 'credits.json') -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $outputRoot | Out-Null
foreach($item in $credits){
 if([IO.Path]::GetFileName($item.file) -ne $item.file){throw 'Unsafe image filename'}
 $source=Join-Path $inputRoot $item.file
 $image=[System.Drawing.Bitmap]::FromFile($source)
 $bitmap=[System.Drawing.Bitmap]::new(1280,720)
 $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
 try {
  $graphics.Clear([System.Drawing.Color]::Black)
  $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $scale=[Math]::Min(1280.0/$image.Width,720.0/$image.Height)
  $width=[int]($image.Width*$scale);$height=[int]($image.Height*$scale)
  $graphics.DrawImage($image,[System.Drawing.Rectangle]::new([int]((1280-$width)/2),[int]((720-$height)/2),$width,$height))
  $target=Join-Path $outputRoot $item.file
  if($item.file.EndsWith('.png')){$bitmap.Save($target,[System.Drawing.Imaging.ImageFormat]::Png)}
  else {$bitmap.Save($target,[System.Drawing.Imaging.ImageFormat]::Jpeg)}
 } finally {$graphics.Dispose();$bitmap.Dispose();$image.Dispose()}
 $item | Add-Member -NotePropertyName download_sha256 -NotePropertyValue $item.sha256
 $item.sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
 $item.bytes=(Get-Item -LiteralPath $target).Length
 $item | Add-Member -NotePropertyName deployed_size -NotePropertyValue @(1280,720)
}
$credits | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputRoot 'credits.json') -Encoding utf8
