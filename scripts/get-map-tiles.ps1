<# Legacy bounds entry point. Opens the browser downloader with these bounds selected.
   Downloads are started by the user in the browser. No Python/Node is required.
   The previous USGS-only downloader remains get-map-tiles-usgs.ps1.
#>
param(
 [Parameter(Mandatory)][double]$North,[Parameter(Mandatory)][double]$South,
 [Parameter(Mandatory)][double]$East,[Parameter(Mandatory)][double]$West,
 [int]$MinZoom=10,[int]$MaxZoom=15,
 [string]$Out=(Join-Path $PSScriptRoot '..\small-area-maps\maps'),
 [int]$MaxTiles=3000,[int]$DelayMs=100,[switch]$Force
)
$ErrorActionPreference='Stop'
if($North -le $South -or $East -le $West -or $North -gt 85 -or $South -lt -85 -or $West -lt -180 -or $East -ge 180){throw 'ขอบเขตพิกัดไม่ถูกต้อง'}
if($MinZoom -lt 6 -or $MaxZoom -gt 16 -or $MinZoom -gt $MaxZoom){throw 'เลือกระดับซูม 6 ถึง 16'}
$lat=($North+$South)/2;$lon=($East+$West)/2
$radius=[math]::Max(($North-$South)/2*110.574,($East-$West)/2*111.320*[math]::Cos($lat*[math]::PI/180))
$radius=[math]::Max(0.5,[math]::Ceiling($radius*10)/10)
if($radius -gt 50){throw 'พื้นที่นี้กว้างเกินรัศมี 50 กม. โปรดเลือกพื้นที่เล็กลง'}
if($Force -or $MaxTiles -ne 3000){Write-Warning 'ข้อจำกัด 3,000 ภาพและ 50 MB ของแหล่งข้อมูลไม่สามารถข้ามด้วย Force หรือ MaxTiles ได้'}
$inv=[Globalization.CultureInfo]::InvariantCulture
$server=Join-Path $PSScriptRoot 'thailand-map-server.ps1'
$argsText='-NoProfile -ExecutionPolicy Bypass -STA -File "{0}" -Output "{1}" -InitialLat {2} -InitialLon {3} -Radius {4} -MinZoom {5} -MaxZoom {6}' -f $server,[IO.Path]::GetFullPath($Out),$lat.ToString('R',$inv),$lon.ToString('R',$inv),$radius.ToString('R',$inv),$MinZoom,$MaxZoom
Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList $argsText | Out-Null
Write-Host 'เปิดตัวดาวน์โหลดแล้ว ตรวจพื้นที่ในเบราว์เซอร์แล้วกดดาวน์โหลด'
