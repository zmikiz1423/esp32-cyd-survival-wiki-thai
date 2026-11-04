/** Real Windows map-server/browser integration regression.
 * Start thailand-map-server.ps1 -NoBrowser -StateFile <file> first, then:
 * node scripts/verify_thai_map_app.cjs <state.json> [--keep-server]
 * Uses the server's selected output directory. Fetches real OpenFreeMap data
 * and writes the default chosen region; do not point at unrelated map data.
 */
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
let chromium;try{({chromium}=require('playwright-core'))}catch{({chromium}=require(path.resolve(__dirname,'../.thai-work/web-qa/node_modules/playwright-core')))}
const root=path.resolve(__dirname,'..');
const state=JSON.parse(fs.readFileSync(path.resolve(process.argv[2]||'.thai-work/maps-integration/state.json'),'utf8'));
const base=new URL(state.url).origin,headers={'X-Map-Token':state.token};
const assert=(ok,msg)=>{if(!ok)throw Error(msg)};
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const out=path.join(root,'.thai-work/maps-integration');fs.mkdirSync(out,{recursive:true});
async function request(p,body,extra={}){return fetch(base+p,{method:body===undefined?'GET':'POST',headers:{...headers,...(body===undefined?{}:{'Content-Type':'application/json'}),...(extra.headers||{})},body:body===undefined?undefined:JSON.stringify(body),...extra})}
function center(folder){const p=path.join(folder,'center.txt');return fs.existsSync(p)?fs.readFileSync(p):null}
function baselineJpeg(bytes){if(bytes[0]!==255||bytes[1]!==216)return false;for(let p=2;p<bytes.length-8;){if(bytes[p++]!==255)continue;while(bytes[p]===255)p++;const marker=bytes[p++];if(marker===0xd9||marker===0xda)break;if(marker===0xc0)return bytes.readUInt16BE(p+3)===256&&bytes.readUInt16BE(p+5)===256;if(marker===0xc2)return false;const len=bytes.readUInt16BE(p);if(len<2)return false;p+=len}return false}
(async()=>{
  const configResponse=await request('/api/config');assert(configResponse.ok,'server config inaccessible');const config=await configResponse.json();
  assert((await fetch(base+'/api/config')).status===403,'missing token accepted');
  assert((await request('/api/config',undefined,{headers:{...headers,Origin:'http://unexpected.invalid'}})).status===403,'cross-origin accepted');
  const centerBefore=center(config.output),errors=[],requests=[];
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
  const page=await browser.newPage({viewport:{width:1100,height:950}});page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(r.url().startsWith(base+'/api/'))requests.push(new URL(r.url()).pathname)});
  await page.goto(state.url);await page.waitForFunction(()=>document.getElementById('status').textContent.includes('พร้อมใช้งาน'));
  const initial=await page.evaluate(()=>({coordinates:document.getElementById('coordinates').value,radius:document.getElementById('radius').value,profile:document.getElementById('profile').value,presets:document.getElementById('preset').options.length}));
  assert(initial.radius===String(config.initial.radius),'custom radius not preserved');assert(initial.profile===`${config.initial.minZoom},${config.initial.maxZoom}`,'custom zoom not preserved');assert(initial.presets===82,'provincial presets missing');
  const layouts=[];for(const width of [320,390,900,1200]){await page.setViewportSize({width,height:950});layouts.push(await page.evaluate(()=>({width:innerWidth,scrollWidth:document.documentElement.scrollWidth})));}
  await page.setViewportSize({width:1100,height:950});
  let plan;page.on('response',async r=>{if(new URL(r.url()).pathname==='/api/start'&&r.ok()){try{plan=await r.json()}catch{}}});
  await page.click('#prepare');await page.waitForFunction(()=>!document.getElementById('download').disabled,{timeout:120000});assert(plan&&plan.tiles.length>1,'need at least two planned tiles');
  // Use a separate unpopulated area to test incomplete activation even when
  // the main demo region was completed in an earlier run. It fetches no data.
  const incompletePlanResponse=await request('/api/start',{lat:18.7883,lon:98.9853,radius:.5,minZoom:14,maxZoom:14});assert(incompletePlanResponse.ok,'incomplete test plan rejected');const incompletePlan=await incompletePlanResponse.json();
  const incomplete=await request('/api/finish',{job:incompletePlan.job});assert(!incomplete.ok,'incomplete region activated');assert((center(config.output)?.toString()||'')===(centerBefore?.toString()||''),'incomplete finish changed center');
  await page.click('#prepare');await page.waitForFunction(()=>!document.getElementById('download').disabled,{timeout:120000});
  const invalid=await request(`/api/tile?job=${encodeURIComponent(plan.job)}&z=14&x=0&y=0`);assert(!invalid.ok,'unplanned tile accepted');
  if(plan.tiles.some(t=>!t.exists)){
    let saveStarted;const saveSignal=new Promise(r=>saveStarted=r);let slowed=false;
    await page.route('**/api/save?**',async route=>{if(!slowed){slowed=true;saveStarted();await new Promise(r=>setTimeout(r,700))}await route.continue()});
    await page.click('#download');await saveSignal;await page.click('#cancel');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('หยุดแล้ว'),{timeout:120000});await page.unroute('**/api/save?**');
  }else{
    await page.evaluate(()=>{document.getElementById('download').click();document.getElementById('cancel').click()});await page.waitForFunction(()=>document.getElementById('status').textContent.includes('หยุดแล้ว'),{timeout:120000});
  }
  assert((center(config.output)?.toString()||'')===(centerBefore?.toString()||''),'cancel changed center');
  await page.click('#download');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('เสร็จครบ'),{timeout:300000});
  assert(fs.existsSync(path.join(config.output,'center.txt')),'center.txt missing');
  const tiles=plan.tiles.map(t=>path.join(config.output,String(t.z),String(t.x),`${t.y}.jpg`));for(const file of tiles)assert(baselineJpeg(fs.readFileSync(file)),'invalid JPEG '+file);
  const tileHashes=tiles.map(p=>hash(fs.readFileSync(p))),source=JSON.parse(fs.readFileSync(path.join(config.output,'source.json'),'utf8'));
  const initialApiCounts=requests.length;await page.click('#prepare');await page.waitForFunction(count=>document.getElementById('status').textContent.includes(`มีอยู่แล้ว ${count}`),tiles.length,{timeout:120000});await page.click('#download');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('เสร็จครบ'),{timeout:120000});
  assert(!requests.slice(initialApiCounts).includes('/api/tile'),'resume fetched upstream tile data');assert(tiles.every((p,i)=>hash(fs.readFileSync(p))===tileHashes[i]),'resume rewrote tile');
  const oversize=await request('/api/start',{lat:13.7563,lon:100.5018,radius:50,minZoom:6,maxZoom:16});assert(!oversize.ok,'oversize plan accepted');
  const screenshot=path.join(root,'docs/thai-map-app-preview.png');await page.screenshot({path:screenshot,fullPage:true});
  const png=await page.locator('#preview').evaluate(c=>c.toDataURL('image/png'));fs.writeFileSync(path.join(root,'docs/thai-map-tile-preview.png'),Buffer.from(png.split(',')[1],'base64'));
  assert(layouts.every(x=>x.width===x.scrollWidth),'page overflows');assert(errors.length===0,'JavaScript errors '+errors.join(','));
  const report={result:'PASS',backend:'Real Windows PowerShell localhost server',mapData:'Real OpenFreeMap OpenMapTiles data',physicalCydTested:false,defaultRegion:config.initial,tiles:tiles.length,jpeg256Baseline:true,allTilesWritten:true,centerAfterComplete:true,cancelPreservesPreviousCenter:true,incompleteFinishRejected:true,missingTokenRejected:true,crossOriginRejected:true,unplannedTileRejected:true,oversizeRejected:true,existingDownloadResumedWithoutPbfFetch:true,provinceAndCityPresets:initial.presets,customRadiusAndZoomPreserved:true,layout:layouts,source,javaScriptErrors:errors,screenshot:path.relative(root,screenshot)};
  fs.writeFileSync(path.join(root,'docs/thai-map-integration-test-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({...report,source:'see report'},null,2));
  if(!process.argv.includes('--keep-server')){await page.click('#closeApp');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('ปิดโปรแกรมแผนที่แล้ว'));}
  await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
