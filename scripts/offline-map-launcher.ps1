# Windows x64 portable authoring runtime. No administrator/Python installation needed.
param([switch]$PrepareOnly,[string]$RuntimeDirectory,[switch]$SelfTest)
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$manifestPath=Join-Path $PSScriptRoot 'offline-runtime-manifest.json'
$appData=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'SurvivalWikiMaps'
$logDirectory=Join-Path $appData 'logs'
[void][IO.Directory]::CreateDirectory($logDirectory)
$bootstrapLog=Join-Path $logDirectory 'offline-setup.log'

function Write-Setup([string]$Message) {
 Write-Host $Message
 [IO.File]::AppendAllText($bootstrapLog,([datetime]::Now.ToString('s')+' '+$Message+"`r`n"),(New-Object Text.UTF8Encoding($false)))
}
function Verify-Archive([string]$Path,[string]$Expected) {
 (Test-Path -LiteralPath $Path) -and ((File-Sha256 $Path) -eq $Expected)
}
function File-Sha256([string]$Path) {
 $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
 try {return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
 finally {$stream.Dispose();$sha.Dispose()}
}
function Get-Archive($Item,[string]$ArchiveCache) {
 $target=Join-Path $ArchiveCache $Item.filename
 if(-not (Verify-Archive $target $Item.sha256)) {
  $uri=[uri]$Item.url
  if($uri.Scheme -ne 'https' -or $uri.Host -notin @('www.python.org','files.pythonhosted.org')) {throw 'Runtime download host is not allowed'}
  Write-Setup ('Downloading '+$Item.filename+' ('+[math]::Round($Item.bytes/1MB,1)+' MB)...')
  $temporary=$target+'.part'
  $web=New-Object Net.WebClient
  try {$web.Headers['User-Agent']='SurvivalWiki-OfflineMaps/1.0';$web.DownloadFile($uri,$temporary)} finally {$web.Dispose()}
  if(-not (Verify-Archive $temporary $Item.sha256)) {throw ('SHA256 check failed: '+$Item.filename)}
  Move-Item -LiteralPath $temporary -Destination $target -Force
 }
 return $target
}
function Expand-CheckedArchive([string]$Archive,[string]$Destination) {
 # .NET ZIP extraction supports wheel archives directly, independent of extension.
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 [IO.Compression.ZipFile]::ExtractToDirectory($Archive,$Destination)
}

try {
 if(-not [Environment]::Is64BitOperatingSystem){throw 'This map builder requires 64-bit Windows 10/11.'}
 $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $manifestHash=File-Sha256 $manifestPath
 $bundled=Join-Path $PSScriptRoot 'offline-runtime'
 if(-not $RuntimeDirectory) {
  if(Test-Path -LiteralPath (Join-Path $bundled 'runtime-ready.json')) {$RuntimeDirectory=$bundled}
  else {$RuntimeDirectory=Join-Path $appData ('runtime-'+$manifest.python.version)}
 }
 $runtime=[IO.Path]::GetFullPath($RuntimeDirectory)
 $marker=Join-Path $runtime 'runtime-ready.json'
 $ready=$false
 if(Test-Path -LiteralPath $marker) {
  try {$state=Get-Content -LiteralPath $marker -Raw -Encoding UTF8 | ConvertFrom-Json;$ready=($state.manifestSHA256 -eq $manifestHash) -and (Test-Path -LiteralPath (Join-Path $runtime 'python.exe'))} catch {$ready=$false}
 }
 if(-not $ready) {
  Write-Setup 'Preparing the map builder. This first setup downloads the portable runtime; please keep this window open.'
  $archiveCache=Join-Path $appData 'runtime-downloads'
  [void][IO.Directory]::CreateDirectory($archiveCache)
  $staging=$runtime+'.setup-'+[guid]::NewGuid().ToString('N')
  [void][IO.Directory]::CreateDirectory($staging)
  $archive=Get-Archive $manifest.python $archiveCache
  Expand-CheckedArchive $archive $staging
  $site=Join-Path $staging 'Lib\site-packages'
  [void][IO.Directory]::CreateDirectory($site)
  foreach($wheel in $manifest.wheels) {
   $archive=Get-Archive $wheel $archiveCache
   Expand-CheckedArchive $archive $site
  }
  $pth=Join-Path $staging 'python313._pth'
  [IO.File]::WriteAllText($pth,"python313.zip`r`n.`r`nLib\site-packages`r`nimport site`r`n",(New-Object Text.UTF8Encoding($false)))
  $testCode="import osmium,shapely,numpy,PIL,uharfbuzz,freetype,requests;from PIL import Image;assert shapely.Point(1,2).x==1;print('Runtime imports: PASS')"
  & (Join-Path $staging 'python.exe') -I -c $testCode
  if($LASTEXITCODE -ne 0){throw 'Portable runtime dependency check failed.'}
  [IO.File]::WriteAllText((Join-Path $staging 'runtime-ready.json'),(@{manifestSHA256=$manifestHash;python=$manifest.python.version;platform='win_amd64'} | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
  if(Test-Path -LiteralPath $runtime) {
   # Preserve the prior runtime in case another instance is still using it.
   $backup=$runtime+'.previous-'+[guid]::NewGuid().ToString('N')
   Move-Item -LiteralPath $runtime -Destination $backup
  }
  Move-Item -LiteralPath $staging -Destination $runtime
  Write-Setup 'Portable runtime ready.'
 }
 $python=Join-Path $runtime 'python.exe'
 if($SelfTest) {
  & $python -I -c "import osmium,shapely,numpy,PIL,uharfbuzz,freetype,requests;print('PASS: portable Python and map dependencies')"
  if($LASTEXITCODE -ne 0){throw 'Portable runtime self-test failed'}
 }
 if($PrepareOnly -or $SelfTest) {
  @{result='PASS';runtime=$runtime;python=$manifest.python.version;manifestSHA256=$manifestHash}|ConvertTo-Json
  exit 0
 }
 $server=Join-Path $PSScriptRoot 'offline-map-server.py'
 $stdout=Join-Path $logDirectory 'offline-server.log'
 $stderr=Join-Path $logDirectory 'offline-server-errors.log'
 $stateFile=Join-Path $appData 'offline-active.json'
 # Reopen the existing local UI instead of launching a second build server
 # when the browser tab was closed or the BAT was double-clicked twice.
 if(Test-Path -LiteralPath $stateFile) {
  try {
   $active=Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8|ConvertFrom-Json
   if($active.root -eq (Split-Path -Parent $PSScriptRoot)) {
    $request=[Net.HttpWebRequest]::Create(([uri]$active.url).GetLeftPart([UriPartial]::Authority)+'/api/config')
    $request.Timeout=1500;$request.Headers['X-Map-Token']=$active.token
    $response=$request.GetResponse();$reader=New-Object IO.StreamReader($response.GetResponseStream())
    try {$existing=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose();$response.Dispose()}
    if($existing.version -eq '1.6.0-th3'){Start-Process -FilePath $active.url|Out-Null;exit 0}
   }
  }catch{}
  Remove-Item -LiteralPath $stateFile -Force
 }
 $process=Start-Process -FilePath $python -WindowStyle Hidden -PassThru -WorkingDirectory (Split-Path -Parent $PSScriptRoot) -ArgumentList ('-u "{0}" --state-file "{1}"' -f $server,$stateFile) -RedirectStandardOutput $stdout -RedirectStandardError $stderr
 for($attempt=0;$attempt -lt 100;$attempt++) {
  if(Test-Path -LiteralPath $stateFile){Write-Setup 'Map builder opened in your browser.';exit 0}
  if($process.HasExited){throw ('The map server exited. '+[IO.File]::ReadAllText($stderr))}
  Start-Sleep -Milliseconds 100
 }
 throw ('The map server did not start. Check '+$stderr)
} catch {
 $message='Map builder setup/start failed: '+$_.Exception.Message
 Write-Setup $message
 Write-Host ('Log: '+$bootstrapLog)
 if(-not ($SelfTest -or $PrepareOnly)) {
  Add-Type -AssemblyName System.Windows.Forms
  [void][Windows.Forms.MessageBox]::Show(($message+"`r`n`r`nLog: "+$bootstrapLog),'Survival Wiki Offline Maps')
 }
 exit 1
}
