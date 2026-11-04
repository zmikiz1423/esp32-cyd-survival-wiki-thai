# Compatibility entry point: the downloader now runs in the default browser.
param([switch]$SelfTest)
$ErrorActionPreference='Stop'
$server=Join-Path $PSScriptRoot 'thailand-map-server.ps1'
if($SelfTest){& $server -SelfTest;exit $LASTEXITCODE}
Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -STA -File "{0}"' -f $server) | Out-Null