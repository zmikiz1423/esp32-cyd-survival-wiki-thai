<# Local, token-protected map authoring server. Windows PowerShell 5.1; no Python/Node runtime needed.
   Public data: OpenFreeMap. Per-job ceilings: 3000 planned tiles, 50 MB received, sequential HTTP.
#>
param(
 [switch]$NoBrowser,[string]$StateFile,[int]$Port=0,[string]$CacheDir,
 [string]$Output=(Join-Path $PSScriptRoot '..\small-area-maps\maps'),
 [double]$InitialLat=13.7563,[double]$InitialLon=100.5018,[double]$Radius=2,
 [int]$MinZoom=11,[int]$MaxZoom=14,[switch]$SelfTest
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing,System.Web
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$script:Inv=[Globalization.CultureInfo]::InvariantCulture
$script:Utf8=New-Object Text.UTF8Encoding($false)
$script:Agent='SurvivalWiki-ThaiMaps/1.0 (+https://github.com/everyday3d/esp32-cyd-survival-wiki)'
$script:AppDir=Join-Path $PSScriptRoot 'map-app'
$script:Output=[IO.Path]::GetFullPath($Output)
if(-not $CacheDir){$CacheDir=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'SurvivalWikiMaps\cache'}
$script:Cache=[IO.Path]::GetFullPath($CacheDir)
$script:Job=$null;$script:LastSearch=[datetime]::MinValue;$script:Closing=$false
$script:Attribution='© OpenStreetMap contributors | © OpenMapTiles | OpenFreeMap'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class SurvivalMapFiles {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern bool MoveFileEx(string oldName,string newName,int flags);
}
'@
function Write-Atomic([string]$Path,[byte[]]$Bytes){
 $dir=Split-Path -Parent $Path;[void][IO.Directory]::CreateDirectory($dir)
 $tmp=Join-Path $dir ('.map-'+[guid]::NewGuid().ToString('N')+'.part')
 try{[IO.File]::WriteAllBytes($tmp,$Bytes);if(-not [SurvivalMapFiles]::MoveFileEx($tmp,$Path,9)){throw (New-Object ComponentModel.Win32Exception([Runtime.InteropServices.Marshal]::GetLastWin32Error()))}}
 finally{if([IO.File]::Exists($tmp)){[IO.File]::Delete($tmp)}}
}
function Write-Text([string]$Path,[string]$Text){Write-Atomic $Path ($script:Utf8.GetBytes($Text))}
function Json([object]$Value){ConvertTo-Json -InputObject $Value -Depth 12 -Compress}
function Write-Json([string]$Path,[object]$Value){Write-Text $Path ((Json $Value)+"`n")}
function Hash([string]$Text){$h=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($h.ComputeHash($script:Utf8.GetBytes($Text)))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}}
function TileX([double]$Lon,[int]$Z){[int][math]::Floor(($Lon+180)/360*[math]::Pow(2,$Z))}
function TileY([double]$Lat,[int]$Z){$r=$Lat*[math]::PI/180;[int][math]::Floor((1-[math]::Log([math]::Tan($r)+1/[math]::Cos($r))/[math]::PI)/2*[math]::Pow(2,$Z))}
function Number($Value,[string]$Name){
 $n=0.0;if($null -eq $Value -or -not [double]::TryParse([string]$Value,'Float',$script:Inv,[ref]$n) -or [double]::IsNaN($n) -or [double]::IsInfinity($n)){throw "ค่าตัวเลข $Name ไม่ถูกต้อง"};$n
}
function New-Plan($Data){
 $lat=Number $Data.lat 'ละติจูด';$lon=Number $Data.lon 'ลองจิจูด';$radius=Number $Data.radius 'รัศมี'
 $min=Number $Data.minZoom 'ซูมต่ำสุด';$max=Number $Data.maxZoom 'ซูมสูงสุด'
 if($radius -lt 0.5 -or $radius -gt 50){throw 'เลือกรัศมี 0.5 ถึง 50 กิโลเมตร'}
 if($min -ne [math]::Floor($min) -or $max -ne [math]::Floor($max) -or $min -lt 6 -or $max -gt 16 -or $min -gt $max){throw 'เลือกระดับซูม 6 ถึง 16 โดยค่าต่ำสุดไม่มากกว่าค่าสูงสุด'}
 if([math]::Abs($lat) -ge 85 -or [math]::Abs($lon) -gt 180){throw 'พิกัดอยู่นอกขอบเขตแผนที่'}
 $dlat=$radius/110.574;$dlon=$radius/(111.320*[math]::Cos($lat*[math]::PI/180))
 $north=$lat+$dlat;$south=$lat-$dlat;$west=$lon-$dlon;$east=$lon+$dlon
 if($north -gt 85.05112878 -or $south -lt -85.05112878 -or $west -lt -180 -or $east -ge 180){throw 'พื้นที่นี้ข้ามขอบแผนที่ โปรดลดรัศมี'}
 $tiles=New-Object Collections.Generic.List[object]
 for($z=[int]$min;$z -le [int]$max;$z++){
  $x0=TileX $west $z;$x1=TileX $east $z;$y0=TileY $north $z;$y1=TileY $south $z
  $count=[long]($x1-$x0+1)*[long]($y1-$y0+1)
  if($tiles.Count+$count -gt 3000){throw 'พื้นที่นี้เกิน 3,000 ภาพ โปรดลดรัศมีหรือระดับซูม'}
  for($x=$x0;$x -le $x1;$x++){for($y=$y0;$y -le $y1;$y++){$tiles.Add([pscustomobject]@{z=$z;x=$x;y=$y})}}
 }
 [pscustomobject]@{lat=$lat;lon=$lon;radius=$radius;minZoom=[int]$min;maxZoom=[int]$max;north=$north;south=$south;east=$east;west=$west;tiles=@($tiles.ToArray())}
}
function Read-Remote([string]$Url,[int]$Limit=4194304,[bool]$Charge=$false){
 $uri=[uri]$Url
 if($uri.Scheme -ne 'https' -or $uri.Host -notin @('tiles.openfreemap.org','nominatim.openstreetmap.org')){throw 'แหล่งดาวน์โหลดไม่ได้รับอนุญาต'}
 $req=[Net.HttpWebRequest]::Create($uri);$req.UserAgent=$script:Agent;$req.Timeout=30000;$req.ReadWriteTimeout=30000
 $req.AutomaticDecompression=[Net.DecompressionMethods]::GZip -bor [Net.DecompressionMethods]::Deflate
 $resp=$null;$stream=$null;$mem=New-Object IO.MemoryStream
 try{
  $resp=$req.GetResponse();$stream=$resp.GetResponseStream();$buffer=New-Object byte[] 16384
  while(($n=$stream.Read($buffer,0,$buffer.Length)) -gt 0){
   if($Charge){$script:Job.networkBytes+=$n;if($script:Job.networkBytes -gt 50000000){throw 'ถึงขีดจำกัดดาวน์โหลด 50 MB โปรดลดพื้นที่หรือซูม แล้วเริ่มใหม่'}}
   if($mem.Length+$n -gt $Limit){throw 'ข้อมูลตอบกลับใหญ่เกินขนาดที่อนุญาต'}
   $mem.Write($buffer,0,$n)
  }
  return ,$mem.ToArray()
 }catch [Net.WebException]{
  $code=if($_.Exception.Response){[int]$_.Exception.Response.StatusCode}else{0}
  if($code -eq 429){throw 'แหล่งข้อมูลขอให้พักการดาวน์โหลด (429) โปรดลองใหม่ภายหลัง ระบบจะใช้ข้อมูลที่มีแล้วต่อได้'}
  throw "ดาวน์โหลดไม่สำเร็จ (HTTP $code) โปรดลองใหม่ภายหลัง: $($_.Exception.Message)"
 }finally{if($stream){$stream.Dispose()};if($resp){$resp.Dispose()};$mem.Dispose();if($Charge -and $script:Job){Save-Ledger}}
}
function Get-TileSource {
 $path=Join-Path $script:Cache 'tilejson.json';$raw=$null
 if([IO.File]::Exists($path) -and ([datetime]::UtcNow-[IO.File]::GetLastWriteTimeUtc($path)).TotalHours -lt 24){$raw=[IO.File]::ReadAllText($path)}
 if(-not $raw){$raw=$script:Utf8.GetString((Read-Remote 'https://tiles.openfreemap.org/planet' 1048576));$check=$raw|ConvertFrom-Json
  if(-not $check.tiles -or $check.maxzoom -lt 14){throw 'รูปแบบแหล่งแผนที่เปลี่ยนไป โปรดอัปเดตเครื่องมือ'}
  Write-Text $path $raw
 }
 $source=$raw|ConvertFrom-Json;$template=[string]$source.tiles[0]
 if($template -notmatch '^https://tiles\.openfreemap\.org/planet/[A-Za-z0-9_-]+/\{z\}/\{x\}/\{y\}\.pbf$'){throw 'URL แหล่งแผนที่ไม่ตรงรูปแบบที่รองรับ'}
 [pscustomobject]@{template=$template;dataset=($template -split '/')[4]}
}
function Valid-Jpeg([byte[]]$Bytes){
 if($Bytes.Length -lt 100 -or $Bytes.Length -gt 184320 -or $Bytes[0] -ne 255 -or $Bytes[1] -ne 216){return $false}
 # Require a baseline SOF0 image; progressive JPEG is not supported by CYD.
 $p=2;$sof=$false
 while($p+3 -lt $Bytes.Length){
  if($Bytes[$p] -ne 255){break};while($p -lt $Bytes.Length -and $Bytes[$p] -eq 255){$p++}
  if($p -ge $Bytes.Length){break};$marker=$Bytes[$p];$p++
  if($marker -eq 218 -or $marker -eq 217){break}
  if($marker -eq 1 -or ($marker -ge 208 -and $marker -le 215)){continue}
  if($p+1 -ge $Bytes.Length){break};$length=([int]$Bytes[$p]*256)+$Bytes[$p+1]
  if($length -lt 2 -or $p+$length -gt $Bytes.Length){return $false}
  if($marker -eq 192){if($length -lt 8 -or ([int]$Bytes[$p+3]*256+$Bytes[$p+4]) -ne 256 -or ([int]$Bytes[$p+5]*256+$Bytes[$p+6]) -ne 256){return $false};$sof=$true}
  elseif($marker -ge 193 -and $marker -le 207 -and $marker -notin @(196,200,204)){return $false}
  $p+=$length
 }
 if(-not $sof){return $false}
 $ms=New-Object IO.MemoryStream(,$Bytes);$im=$null
 try{$im=[Drawing.Image]::FromStream($ms,$true,$true);return ($im.Width -eq 256 -and $im.Height -eq 256 -and $im.RawFormat.Guid -eq [Drawing.Imaging.ImageFormat]::Jpeg.Guid)}catch{return $false}finally{if($im){$im.Dispose()};$ms.Dispose()}
}
function Valid-File([string]$Path){if(-not [IO.File]::Exists($Path)){return $false};try{Valid-Jpeg ([IO.File]::ReadAllBytes($Path))}catch{$false}}
function Source-Info {
 [ordered]@{provider='openfreemap';rendererVersion=1;attribution=$script:Attribution;data='https://www.openstreetmap.org/copyright';schema='https://www.openmaptiles.org/';service='https://openfreemap.org/';terms='https://openfreemap.org/tos/';createdUtc=[datetime]::UtcNow.ToString('o')}
}
function Save-Ledger {
 Write-Json $script:Job.ledgerPath ([ordered]@{networkBytes=[long]$script:Job.networkBytes;includedBytes=[long]$script:Job.includedBytes;tiles=$script:Job.included;updatedUtc=[datetime]::UtcNow.ToString('o')})
}
function Register-Upstream([string]$Key,[long]$Length){
 if($script:Job.included.ContainsKey($Key)){return}
 if($script:Job.included.Count -ge 3000 -or $script:Job.includedBytes+$Length -gt 50000000){throw 'พื้นที่นี้มีข้อมูลต้นทางเกิน 50 MB หรือ 3,000 ภาพ โปรดลดรัศมีหรือระดับซูม'}
 $script:Job.included[$Key]=$Length;$script:Job.includedBytes+=$Length;Save-Ledger
}
function Check-Output {
 if([IO.Directory]::Exists($script:Output)){
  $sourcePath=Join-Path $script:Output 'source.json';$metadata=$null
  if([IO.File]::Exists($sourcePath)){$metadata=[IO.File]::ReadAllText($sourcePath)|ConvertFrom-Json}
  $hasJpeg=$null -ne (Get-ChildItem -LiteralPath $script:Output -Filter '*.jpg' -Recurse -File | Select-Object -First 1)
  if($metadata){if($metadata.provider -ne 'openfreemap' -or $metadata.rendererVersion -ne 1){throw 'โฟลเดอร์นี้มีแผนที่จากแหล่งหรือรูปแบบอื่น โปรดเลือกโฟลเดอร์ maps ใหม่เพื่อไม่ให้ปะปน'}}
  elseif($hasJpeg){throw 'โฟลเดอร์นี้มีภาพแผนที่เดิมที่ไม่ระบุแหล่ง โปรดสำรอง maps เดิมแล้วเลือกโฟลเดอร์ว่าง ไม่ต้องลบ waypoints.txt หรือ track.csv'}
 }
 [void][IO.Directory]::CreateDirectory($script:Output)
 if(-not [IO.File]::Exists((Join-Path $script:Output 'source.json'))){Write-Json (Join-Path $script:Output 'source.json') (Source-Info)}
}
function Begin-Job($Data){
 $plan=New-Plan $Data;Check-Output;$source=Get-TileSource
 $id=[guid]::NewGuid().ToString('N');$allowed=@{};$tiles=New-Object Collections.Generic.List[object]
 foreach($tile in $plan.tiles){$key="$($tile.z)/$($tile.x)/$($tile.y)";$path=Join-Path $script:Output ($key.Replace('/','\')+'.jpg');$exists=Valid-File $path;$allowed[$key]=$path;$tiles.Add([pscustomobject]@{z=$tile.z;x=$tile.x;y=$tile.y;exists=$exists})}
 # Persist the same area's accounting across retries/restarts. Cache hits still
 # count toward its unique included vector data, so resume cannot reset 50 MB.
 $identity=($plan.lat.ToString('R',$script:Inv)+'|'+$plan.lon.ToString('R',$script:Inv)+'|'+$plan.radius.ToString('R',$script:Inv)+"|$($plan.minZoom)|$($plan.maxZoom)|$($source.dataset)|renderer1")
 $ledgerPath=Join-Path $script:Cache ('plans\'+(Hash $identity)+'.json');$included=@{};$network=0L;$includedBytes=0L
 if([IO.File]::Exists($ledgerPath)){
  $ledger=[IO.File]::ReadAllText($ledgerPath)|ConvertFrom-Json;$network=[long]$ledger.networkBytes
  foreach($entry in $ledger.tiles.PSObject.Properties){if($entry.Name -notmatch '^\d+/\d+/\d+$' -or [long]$entry.Value -lt 0){throw 'บันทึกแคชเสียหาย โปรดเลือกพื้นที่ใหม่'};$included[$entry.Name]=[long]$entry.Value;$includedBytes+=[long]$entry.Value}
 }
 $missing=@($tiles.ToArray()|Where-Object {-not $_.exists}).Count
 if(($network -ge 50000000 -or $includedBytes -gt 50000000 -or $included.Count -gt 3000) -and $missing){throw 'พื้นที่เดิมถึงขีดจำกัด 50 MB แล้ว แม้เริ่มใหม่ก็ไม่ข้ามขีดจำกัด โปรดลดรัศมีหรือระดับซูม'}
 $script:Job=@{id=$id;plan=$plan;allowed=$allowed;output=$script:Output;source=$source;networkBytes=$network;includedBytes=$includedBytes;included=$included;ledgerPath=$ledgerPath;upstream=@{};finished=$false}
 Save-Ledger
 @{job=$id;tiles=@($tiles.ToArray());count=$tiles.Count;estimatedMB=[math]::Round($tiles.Count*18/1024,1);output=$script:Output}
}
function Require-Job([string]$Id){if(-not $script:Job -or $Id -ne $script:Job.id){throw 'งานดาวน์โหลดหมดอายุ โปรดกดเริ่มใหม่'}}
function Tile-Key($Query){
 Require-Job $Query['job'];$v=@()
 foreach($name in @('z','x','y')){if($Query[$name] -notmatch '^\d{1,8}$'){throw 'พิกัดภาพไม่ถูกต้อง'};$v+=[int]$Query[$name]}
 $key="$($v[0])/$($v[1])/$($v[2])";if(-not $script:Job.allowed.ContainsKey($key)){throw 'ภาพนี้ไม่ได้อยู่ในพื้นที่ที่เลือก'};$key
}
function Tile-Bytes([string]$Key){
 $a=$Key.Split('/');$z=[int]$a[0];$x=[int]$a[1];$y=[int]$a[2];$sz=[math]::Min($z,14);$factor=[math]::Pow(2,$z-$sz)
 $sx=[int][math]::Floor($x/$factor);$sy=[int][math]::Floor($y/$factor);$upstream="$sz/$sx/$sy"
 if(-not $script:Job.upstream.ContainsKey($upstream)){
  if($script:Job.upstream.Count -ge 3000){throw 'ถึงขีดจำกัดภาพต้นทาง 3,000 ภาพ'};$script:Job.upstream[$upstream]=$true
 }
 $cachePath=Join-Path $script:Cache ("pbf\"+$script:Job.source.dataset+"\$sz\$sx\$sy.pbf")
 if([IO.File]::Exists($cachePath)){$cached=[IO.File]::ReadAllBytes($cachePath);Register-Upstream $upstream $cached.Length;return ,$cached}
 $url=$script:Job.source.template.Replace('{z}',[string]$sz).Replace('{x}',[string]$sx).Replace('{y}',[string]$sy)
 $responseLimit=4194304
 if(-not $script:Job.included.ContainsKey($upstream)){
  $responseLimit=[int][math]::Min($responseLimit,50000000-$script:Job.includedBytes)
  if($responseLimit -le 0){throw 'ถึงขีดจำกัดข้อมูลพื้นที่ 50 MB โปรดลดรัศมีหรือระดับซูม'}
 }
 $bytes=Read-Remote $url $responseLimit $true
 if($bytes.Length -lt 1){throw 'ไม่พบข้อมูลแผนที่ในพื้นที่นี้'};Register-Upstream $upstream $bytes.Length;Write-Atomic $cachePath $bytes;return ,$bytes
}
function Finish-Job([string]$Id){
 Require-Job $Id;$bytes=0L
 foreach($path in $script:Job.allowed.Values){if(-not (Valid-File $path)){throw 'ภาพยังไม่ครบหรือเสียหาย โปรดกดดาวน์โหลดอีกครั้งเพื่อทำต่อ'};$bytes+=([IO.FileInfo]$path).Length}
 $plan=$script:Job.plan;$start=[math]::Min($plan.maxZoom,[math]::Max($plan.minZoom,13));$src=Source-Info
 $src['dataset']=$script:Job.source.dataset;$src['completedUtc']=[datetime]::UtcNow.ToString('o');$src['upstreamBytes']=$script:Job.includedBytes
 Write-Json (Join-Path $script:Job.output 'source.json') $src
 Write-Json (Join-Path $script:Job.output 'area.json') ([ordered]@{lat=$plan.lat;lon=$plan.lon;radius=$plan.radius;minZoom=$plan.minZoom;maxZoom=$plan.maxZoom;north=$plan.north;south=$plan.south;east=$plan.east;west=$plan.west;tileCount=$script:Job.allowed.Count;bytes=$bytes;completedUtc=[datetime]::UtcNow.ToString('o')})
 Write-Text (Join-Path $script:Job.output 'attribution.txt') ($script:Attribution+"`r`nhttps://www.openstreetmap.org/copyright`r`nhttps://www.openmaptiles.org/`r`nhttps://openfreemap.org/`r`nhttps://openfreemap.org/tos/`r`n")
 $center=($plan.lat.ToString('R',$script:Inv)+' '+$plan.lon.ToString('R',$script:Inv)+" $start $($plan.minZoom) $($plan.maxZoom)`n")
 Write-Text (Join-Path $script:Job.output 'center.txt') $center # activate only after all other files are committed
 $script:Job.finished=$true
 @{output=$script:Job.output;count=$script:Job.allowed.Count;bytes=$bytes;networkBytes=$script:Job.networkBytes;includedBytes=$script:Job.includedBytes}
}
function Read-Body($Request,[int]$Limit){
 if($Request.ContentLength64 -gt $Limit){throw 'ข้อมูลที่ส่งมาใหญ่เกินกำหนด'}
 $mem=New-Object IO.MemoryStream;$buffer=New-Object byte[] 8192
 try{while(($n=$Request.InputStream.Read($buffer,0,$buffer.Length)) -gt 0){if($mem.Length+$n -gt $Limit){throw 'ข้อมูลที่ส่งมาใหญ่เกินกำหนด'};$mem.Write($buffer,0,$n)};return ,$mem.ToArray()}finally{$mem.Dispose()}
}
function Body-Json($Request){$text=$script:Utf8.GetString((Read-Body $Request 8192));if(-not $text.Trim()){throw 'ไม่พบข้อมูลคำขอ'};$text|ConvertFrom-Json}
function Respond($Context,[byte[]]$Bytes,[string]$Type='application/json; charset=utf-8',[int]$Status=200){
 $r=$Context.Response;$r.StatusCode=$Status;$r.ContentType=$Type;$r.Headers['Cache-Control']='no-store';$r.Headers['X-Content-Type-Options']='nosniff';$r.Headers['Referrer-Policy']='no-referrer'
 $r.ContentLength64=$Bytes.Length
 try{$r.OutputStream.Write($Bytes,0,$Bytes.Length)}finally{$r.Close()}
}
function Respond-Json($Context,$Value,[int]$Status=200){Respond $Context ($script:Utf8.GetBytes((Json $Value))) 'application/json; charset=utf-8' $Status}
function Search-Place([string]$Query){
 $q=$Query.Trim();if($q.Length -lt 2 -or $q.Length -gt 160){throw 'กรอกชื่อสถานที่ 2 ถึง 160 ตัวอักษร'}
 $path=Join-Path $script:Cache ('search\'+(Hash $q.ToLowerInvariant())+'.json')
 if([IO.File]::Exists($path)){return [IO.File]::ReadAllText($path)}
 $elapsed=([datetime]::UtcNow-$script:LastSearch).TotalMilliseconds
 if($elapsed -lt 1100){Start-Sleep -Milliseconds ([int](1100-$elapsed))};$script:LastSearch=[datetime]::UtcNow
 $url='https://nominatim.openstreetmap.org/search?format=json&limit=8&countrycodes=th&accept-language=th&q='+[uri]::EscapeDataString($q)
 $text=$script:Utf8.GetString((Read-Remote $url 1048576))
 # Preserve the upstream JSON array verbatim, including []; PowerShell's
 # pipeline otherwise unwraps empty/single-element arrays in surprising ways.
 $null=$text|ConvertFrom-Json
 if(-not $text.TrimStart().StartsWith('[')){throw 'รูปแบบผลค้นหาสถานที่ไม่ถูกต้อง'}
 Write-Text $path $text;return $text
}
function Select-Folder {
 $dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.Description='เลือกไดรฟ์ SD Card หรือโฟลเดอร์ปลายทาง ระบบจะสร้าง maps ให้อัตโนมัติ';$dialog.ShowNewFolderButton=$true
 if([IO.Directory]::Exists($script:Output)){$dialog.SelectedPath=$script:Output}
 try{if($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK){$selected=$dialog.SelectedPath;if((Split-Path $selected -Leaf) -ne 'maps'){$selected=Join-Path $selected 'maps'};$script:Output=[IO.Path]::GetFullPath($selected)}}finally{$dialog.Dispose()}
 @{output=$script:Output}
}
if($SelfTest){
 $p=New-Plan @{lat=13.7563;lon=100.5018;radius=5;minZoom=11;maxZoom=14};if($p.tiles.Count -lt 4){throw 'Bangkok plan empty'}
 $threw=$false;try{New-Plan @{lat=13.7;lon=100.5;radius=50;minZoom=11;maxZoom=16}|Out-Null}catch{$threw=$true};if(-not $threw){throw 'Tile cap not enforced'}
 $threw=$false;try{New-Plan @{lat=0;lon=179.99;radius=50;minZoom=6;maxZoom=6}|Out-Null}catch{$threw=$true};if(-not $threw){throw 'Dateline validation failed'}
 @{result='PASS';bangkokTiles=$p.tiles.Count;maxTiles=3000;maxBytes=50000000;networkRequests=0}|ConvertTo-Json;return
}
$listener=$null
try{
 [void][IO.Directory]::CreateDirectory($script:Cache)
 if($Port -eq 0){$probe=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,0);$probe.Start();$Port=$probe.LocalEndpoint.Port;$probe.Stop()}
 if($Port -lt 1024 -or $Port -gt 65535){throw 'หมายเลขพอร์ตต้องอยู่ระหว่าง 1024 ถึง 65535'}
 $script:Token=[guid]::NewGuid().ToString('N');$script:Base="http://127.0.0.1:$Port";$url=$script:Base+'/?token='+$script:Token
 $listener=New-Object Net.HttpListener;$listener.Prefixes.Add($script:Base+'/');$listener.Start()
 if($StateFile){$statePath=[IO.Path]::GetFullPath($StateFile);Write-Json $statePath @{url=$url;token=$script:Token;pid=$PID;port=$Port}}
 Write-Output ('Survival Wiki Thai Maps: '+$url)
 if(-not $NoBrowser){Start-Process -FilePath $url | Out-Null}
 while(-not $script:Closing){
  $ctx=$listener.GetContext();$req=$ctx.Request;$path=$req.Url.AbsolutePath
  try{
   if($req.Headers['Host'] -ne "127.0.0.1:$Port"){Respond-Json $ctx @{error='Invalid host'} 403;continue}
   if($path.StartsWith('/api/')){
    $origin=$req.Headers['Origin']
    if($req.Headers['X-Map-Token'] -ne $script:Token -or ($origin -and $origin -ne $script:Base)){Respond-Json $ctx @{error='Unauthorized local request'} 403;continue}
    switch($req.HttpMethod+' '+$path){
     'GET /api/config' {Respond-Json $ctx @{output=$script:Output;version='1.6.0-th2';initial=@{lat=$InitialLat;lon=$InitialLon;radius=$Radius;minZoom=$MinZoom;maxZoom=$MaxZoom};maxTiles=3000;maxBytes=50000000}}
     'POST /api/select-folder' {Respond-Json $ctx (Select-Folder)}
     'POST /api/start' {Respond-Json $ctx (Begin-Job (Body-Json $req))}
     'GET /api/tile' {$key=Tile-Key $req.QueryString;Respond $ctx (Tile-Bytes $key) 'application/x-protobuf'}
     'GET /api/saved' {$key=Tile-Key $req.QueryString;$file=$script:Job.allowed[$key];if(-not (Valid-File $file)){Respond-Json $ctx @{error='Saved tile missing'} 404}else{Respond $ctx ([IO.File]::ReadAllBytes($file)) 'image/jpeg'}}
     'POST /api/save' {$key=Tile-Key $req.QueryString;$body=Read-Body $req 184320;if(-not (Valid-Jpeg $body)){throw 'ภาพต้องเป็น JPEG baseline ขนาด 256 × 256 พิกเซล'};Write-Atomic $script:Job.allowed[$key] $body;Respond-Json $ctx @{ok=$true}}
     'POST /api/finish' {$body=Body-Json $req;Respond-Json $ctx (Finish-Job ([string]$body.job))}
     'POST /api/open-folder' {if([IO.Directory]::Exists($script:Output)){Start-Process -FilePath 'explorer.exe' -ArgumentList ('"'+$script:Output+'"') | Out-Null};Respond-Json $ctx @{output=$script:Output}}
     'POST /api/close' {$script:Closing=$true;Respond-Json $ctx @{ok=$true}}
     'GET /api/search' {$query=[Web.HttpUtility]::ParseQueryString($req.Url.Query,[Text.Encoding]::UTF8);$searchJson=Search-Place ([string]$query['q']);Respond $ctx ($script:Utf8.GetBytes($searchJson)) 'application/json; charset=utf-8'}
     default {Respond-Json $ctx @{error='Not found'} 404}
    }
   }elseif($req.HttpMethod -ne 'GET'){Respond-Json $ctx @{error='Method not allowed'} 405}
   elseif($path -eq '/' -or $path -eq '/index.html'){
    if($req.QueryString['token'] -ne $script:Token){Respond-Json $ctx @{error='Open the downloader from map-downloader.bat'} 403;continue}
    $html=[IO.File]::ReadAllText((Join-Path $script:AppDir 'index.html')).Replace('__MAP_TOKEN__',$script:Token)
    Respond $ctx ($script:Utf8.GetBytes($html)) 'text/html; charset=utf-8'
   }elseif($path -eq '/Sarabun-Regular.ttf'){Respond $ctx ([IO.File]::ReadAllBytes((Join-Path $PSScriptRoot '..\sdcard\web\Sarabun-Regular.ttf'))) 'font/ttf'}
   elseif($path -in @('/app.js','/renderer.js') -or $path -match '^/vendor/[A-Za-z0-9_-]+\.js$'){
    $file=Join-Path $script:AppDir $path.TrimStart('/');if(-not [IO.File]::Exists($file)){Respond-Json $ctx @{error='Not found'} 404}else{Respond $ctx ([IO.File]::ReadAllBytes($file)) 'text/javascript; charset=utf-8'}
   }else{Respond-Json $ctx @{error='Not found'} 404}
  }catch{try{Respond-Json $ctx @{error=$_.Exception.Message} 400}catch{}}
 }
}catch{
 if(-not $NoBrowser){[void][Windows.Forms.MessageBox]::Show(('เปิดเครื่องมือแผนที่ไม่สำเร็จ: '+$_.Exception.Message),'Survival Wiki')}
 throw
}finally{if($listener){$listener.Stop();$listener.Close()}}
