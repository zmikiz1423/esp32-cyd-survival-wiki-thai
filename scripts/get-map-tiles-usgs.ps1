<#
.SYNOPSIS
  Downloads USGS topo map tiles for an area and writes them where the Survival Wiki map viewer reads them.

.DESCRIPTION
  Saves tiles as maps\{zoom}\{x}\{y}.jpg under -Out (default: sdcard\maps in this repo), plus maps\center.txt
  so the viewer opens on the area you downloaded. Copy the whole "maps" folder to the root of the SD card.

  Tiles come from the USGS National Map (US public domain). That service covers the United States only.
  Please be gentle with it: this script pauses between requests and skips tiles you already have.
  Do NOT point this at OpenStreetMap's own tile servers; their usage policy forbids bulk downloads.

.EXAMPLE
  .\scripts\get-map-tiles.ps1 -North 39.0 -South 38.8 -East -76.9 -West -77.2 -MinZoom 10 -MaxZoom 15

.NOTES
  Roughly 35 KB per tile at the higher zooms. A town-sized area at zoom 10 to 15 is a few hundred megabytes at most.
#>
param(
  [Parameter(Mandatory)][double]$North,
  [Parameter(Mandatory)][double]$South,
  [Parameter(Mandatory)][double]$East,
  [Parameter(Mandatory)][double]$West,
  [int]$MinZoom = 10,
  [int]$MaxZoom = 15,
  [string]$Out = (Join-Path $PSScriptRoot "..\sdcard\maps"),
  [int]$MaxTiles = 6000,
  [int]$DelayMs = 100,
  [switch]$Force
)

$ErrorActionPreference = "Stop"
if ($North -le $South -or $East -le $West) { throw "Need North > South and East > West." }
if ($MinZoom -gt $MaxZoom -or $MinZoom -lt 0 -or $MaxZoom -gt 18) { throw "Zoom levels must be 0 to 18 with MinZoom <= MaxZoom." }

function Get-TileX([double]$lon, [int]$z) { [int][math]::Floor(($lon + 180.0) / 360.0 * [math]::Pow(2, $z)) }
function Get-TileY([double]$lat, [int]$z) {
  $r = $lat * [math]::PI / 180.0
  [int][math]::Floor((1.0 - [math]::Log([math]::Tan($r) + 1.0 / [math]::Cos($r)) / [math]::PI) / 2.0 * [math]::Pow(2, $z))
}

# Work out every tile first so we can show the total before downloading anything.
$jobs = New-Object System.Collections.Generic.List[object]
for ($z = $MinZoom; $z -le $MaxZoom; $z++) {
  $x0 = Get-TileX $West $z;  $x1 = Get-TileX $East $z
  $y0 = Get-TileY $North $z; $y1 = Get-TileY $South $z
  for ($x = $x0; $x -le $x1; $x++) { for ($y = $y0; $y -le $y1; $y++) { $jobs.Add([pscustomobject]@{ z = $z; x = $x; y = $y }) } }
}
$estMb = [math]::Round($jobs.Count * 35 / 1024)
Write-Host "$($jobs.Count) tiles, roughly $estMb MB, zoom $MinZoom to $MaxZoom."
if ($jobs.Count -gt $MaxTiles -and -not $Force) {
  throw "That is more than $MaxTiles tiles. Shrink the area or the zoom range, or pass -Force if you really want it."
}

$agent = "SurvivalWiki-map-tiles/1.0 (personal offline use)"
$done = 0; $skipped = 0; $failed = 0
foreach ($j in $jobs) {
  $dir = Join-Path $Out "$($j.z)\$($j.x)"
  $file = Join-Path $dir "$($j.y).jpg"
  if ((Test-Path $file) -and ((Get-Item $file).Length -gt 500)) { $skipped++; continue }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $url = "https://basemap.nationalmap.gov/arcgis/rest/services/USGSTopo/MapServer/tile/$($j.z)/$($j.y)/$($j.x)"
  $ok = $false
  for ($try = 1; $try -le 3 -and -not $ok; $try++) {
    try {
      Invoke-WebRequest -Uri $url -OutFile $file -UserAgent $agent -UseBasicParsing | Out-Null
      $ok = (Get-Item $file).Length -gt 500
    } catch { Start-Sleep -Milliseconds (500 * $try) }
  }
  if ($ok) { $done++ } else { $failed++; if (Test-Path $file) { Remove-Item $file } }
  if (($done + $failed) % 50 -eq 0) { Write-Host ("  {0}/{1} downloaded..." -f ($done + $skipped + $failed), $jobs.Count) }
  Start-Sleep -Milliseconds $DelayMs
}

# The viewer opens here: center latitude, longitude, starting zoom, min zoom, max zoom.
New-Item -ItemType Directory -Force $Out | Out-Null
$cLat = ($North + $South) / 2; $cLon = ($East + $West) / 2
$startZ = [math]::Min($MaxZoom, [math]::Max($MinZoom, 13))
"{0} {1} {2} {3} {4}" -f $cLat.ToString([cultureinfo]::InvariantCulture), $cLon.ToString([cultureinfo]::InvariantCulture), $startZ, $MinZoom, $MaxZoom |
  Set-Content -Path (Join-Path $Out "center.txt") -Encoding ascii

Write-Host "Done: $done downloaded, $skipped already there, $failed failed."
Write-Host "Copy the folder '$Out' to the root of the SD card (it must end up as /maps on the card)."
