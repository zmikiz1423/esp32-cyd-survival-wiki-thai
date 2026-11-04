/** Verify the generated Thai SD website in a real headless browser.
 * Uses the real SD files, including gzip responses. Only ESP32 status/chat APIs
 * are simulated; this does not claim to test a physical board or phone sensors.
 */
const fs=require('node:fs'),path=require('node:path'),http=require('node:http'),zlib=require('node:zlib'),crypto=require('node:crypto');
const {pathToFileURL}=require('node:url');
const root=path.resolve(__dirname,'..'),sd=path.join(root,'sdcard');
let chromium;try{({chromium}=require('playwright-core'))}catch{({chromium}=require(path.join(root,'.thai-work/web-qa/node_modules/playwright-core')))}
const out=path.join(root,'.thai-work/final-web-qa');fs.mkdirSync(out,{recursive:true});
const assert=(ok,msg)=>{if(!ok)throw new Error(msg)};
const read=p=>fs.readFileSync(path.join(sd,p),'utf8');
const rows=s=>s.split(/\r?\n/).filter(x=>x&&!x.startsWith('#')).map(x=>x.split('\t'));
const entries=rows(read('wiki/catalog.tsv'));
const books=rows(read('library/catalog.tsv'));
const sent=[],requests=[],pageErrors=[];
const embedded={};
for(const [url,file] of [['/mesh','meshpage.h'],['/map','mappage.h'],['/tools','toolspage.h']])embedded[url]=fs.readFileSync(path.join(root,'firmware/src',file),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
function mime(p){return p.endsWith('.json')?'application/json':p.endsWith('.jpg')?'image/jpeg':p.endsWith('.png')?'image/png':p.endsWith('.ttf')?'font/ttf':p.endsWith('.html')?'text/html; charset=utf-8':p.endsWith('.pdf')?'application/pdf':'text/plain; charset=utf-8'}
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost'),p=decodeURIComponent(url.pathname);requests.push({path:p,method:req.method});
  if(embedded[p]){res.setHeader('Content-Type','text/html; charset=utf-8');return res.end(embedded[p])}
  if(p==='/api/status'){res.setHeader('Content-Type','application/json');return res.end(JSON.stringify({clients:2,uptime:120,sdUsedMB:8,sdTotalMB:1024,version:'1.6.0-th'}))}
  if(p==='/api/mesh'){res.setHeader('Content-Type','application/json');return res.end(JSON.stringify({name:'ไทย',on:1,seq:1,msgs:[{n:1,f:'ไทย',t:'น้ำสะอาดอยู่ใกล้ที่พัก',a:2}],acks:[],peers:[]}))}
  if(p==='/api/mesh/send'){let body='';req.on('data',c=>body+=c);return req.on('end',()=>{sent.push(Object.fromEntries(new URLSearchParams(body)));res.setHeader('Content-Type','application/json');res.end('{"ok":true}')})}
  if(p==='/api/pos'){res.setHeader('Content-Type','application/json');return res.end('{"ok":true}')}
  const rel=p==='/'?'web/index.html':p.replace(/^\//,'');const file=path.resolve(sd,rel);
  if(!file.startsWith(sd+path.sep)){res.statusCode=403;return res.end('forbidden')}
  let actual=file;if(fs.existsSync(file+'.gz')){actual=file+'.gz';res.setHeader('Content-Encoding','gzip')}
  if(!fs.existsSync(actual)||fs.statSync(actual).isDirectory()){res.statusCode=404;return res.end('missing')}
  res.setHeader('Content-Type',mime(file));fs.createReadStream(actual).pipe(res);
});
const plain=s=>s.replace(/[\s\u200b]+/g,'');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
function jpegInfo(file){
  const b=fs.readFileSync(file);assert(b.readUInt16BE(0)===0xffd8,'Invalid JPEG '+file);let i=2;
  while(i<b.length){while(b[i]===0xff)i++;const m=b[i++];if(m===0xd9||m===0xda)break;const len=b.readUInt16BE(i);
    if([0xc0,0xc1,0xc2].includes(m))return {width:b.readUInt16BE(i+5),height:b.readUInt16BE(i+3),progressive:m===0xc2,baseline:m===0xc0};i+=len;
  }throw new Error('No JPEG frame '+file);
}
(async()=>{
 assert(entries.length===118,'Expected all 118 articles');
 const index=fs.readFileSync(path.join(sd,'web/index.html')),gz=zlib.gunzipSync(fs.readFileSync(path.join(sd,'web/index.html.gz')));
 assert(index.equals(gz),'index.html.gz is stale');
 const search=JSON.parse(fs.readFileSync(path.join(sd,'wiki/search.json'),'utf8'));
 assert(search.length===118,'Expected 118 search records');
 assert(fs.readFileSync(path.join(sd,'wiki/search.json')).equals(zlib.gunzipSync(fs.readFileSync(path.join(sd,'wiki/search.json.gz')))),'search gzip is stale');
 const images=[...new Set(entries.flatMap(([id])=>Array.from(read('wiki/articles/'+id+'.md').matchAll(/!\[[^\]]*\]\(([^)]+)\)/g),m=>'/wiki/'+m[1])))];
 assert(images.length===22,'Expected 22 distinct diagram images');
 const imageInfo=images.map(p=>({path:p,...jpegInfo(path.join(sd,p))}));
 assert(imageInfo.every(x=>x.baseline&&!x.progressive&&x.width===448),'Diagrams must be 448px baseline JPEGs');
 for(const [id] of entries){for(const m of read('wiki/articles/'+id+'.md').matchAll(/\[[^\]]*\]\(([a-z0-9-]+)\.md\)/g))assert(fs.existsSync(path.join(sd,'wiki/articles',m[1]+'.md')),'Broken article link '+id+' -> '+m[1])}
 console.log('Static SD checks: 118 articles/search records; 22 baseline JPEGs; links and gzip match.');
 await new Promise(ok=>server.listen(0,'127.0.0.1',ok));const base='http://127.0.0.1:'+server.address().port;
 const executable=process.env.CHROME_PATH||['C:/Program Files/Google/Chrome/Application/chrome.exe','C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe','/usr/bin/chromium','/usr/bin/google-chrome'].find(fs.existsSync);
 const browser=await chromium.launch({executablePath:executable,headless:true});const context=await browser.newContext({acceptDownloads:true});const page=await context.newPage();page.on('pageerror',e=>pageErrors.push(e.message));
 const articleResults=[];
 async function bounds(){return page.evaluate(()=>({viewport:document.documentElement.clientWidth,width:document.documentElement.scrollWidth,lang:document.documentElement.lang}))}
 for(const width of [240,320,390]){
   await page.setViewportSize({width,height:740});
   for(const url of ['/#/','/#/library','/#/about','/mesh','/map','/tools','/tools#sun','/tools#level','/tools#cam']){
     await page.goto(base+url);await page.waitForTimeout(50);const b=await bounds();assert(b.lang==='th'&&b.width<=b.viewport,'Page overflow: '+url+' @'+width);articleResults.push({width,url,...b});
   }
 }
 await page.setViewportSize({width:240,height:740});
 for(let i=0;i<entries.length;i++){
   const [id,,title]=entries[i];await page.goto(base+'/#/a/'+id);
   await page.waitForFunction(expected=>document.querySelector('main article h1')?.textContent===expected,title);
   const b=await bounds();assert(b.width<=b.viewport,'Article overflows: '+id);
   const renderedImages=await page.evaluate(async()=>{const ims=[...document.querySelectorAll('main article img')];for(const im of ims)im.loading='eager';return Promise.all(ims.map(async im=>{await im.decode();return {src:im.getAttribute('src'),width:im.naturalWidth,height:im.naturalHeight}}))});
   articleResults.push({width:240,url:'/#/a/'+id,...b,images:renderedImages});
   if(i%30===0)console.log('Articles rendered '+(i+1)+'/118');
 }
 await page.evaluate(()=>document.fonts.ready);const font=await page.evaluate(async()=>{const f=await document.fonts.load('17px Sarabun','ภาษาไทย');return {loaded:f.length>0&&f.every(x=>x.status==='loaded'),check:document.fonts.check('17px Sarabun','ภาษาไทย')}});assert(font.loaded&&font.check,'Local Sarabun font not loaded');
 const searches=[];
 for(const [query,mustInclude] of [['ห้ามเลือด','severe-bleeding'],['เงื่อน','knots'],['น้ำ','purifying-water']]){
   await page.goto(base+'/#/search/'+encodeURIComponent(query));await page.waitForFunction(()=>document.querySelector('main h1')?.textContent.startsWith('พบ '));
   const links=await page.locator('main .list a').evaluateAll(es=>es.map(e=>e.getAttribute('href')));assert(links.includes('#/a/'+mustInclude),'Thai query missed '+mustInclude);
   searches.push({query,resultCount:links.length,requiredResult:mustInclude});
 }
 console.log('All 118 articles, local font and 3 Thai searches passed.');
 const bookResults=[];
 for(const [id,title,parts] of books){
   assert(Number(parts)===(id==='field-guide'?22:62),'Incorrect book part count');
   for(let n=1;n<=Number(parts);n++){const file=path.join(sd,'library',id,String(n).padStart(4,'0')+'.txt');assert(fs.existsSync(file)&&fs.statSync(file).size>0&&fs.statSync(file).size<=32768,'Missing/oversize book part '+file)}
   for(const part of [1,Number(parts)]){
     const rel='library/'+id+'/'+String(part).padStart(4,'0')+'.txt',expected=read(rel);await page.goto(base+'/#/book/'+id+'/'+part);
     await page.waitForSelector('main .book-text');const actual=await page.locator('main .book-text').textContent();assert(plain(actual)===plain(expected),'Book text truncated/changed '+rel);const b=await bounds();assert(b.width<=b.viewport,'Book overflow '+rel);
     const actions=await page.locator('main .book-actions').first().locator('a').evaluateAll(es=>es.map(e=>({text:e.textContent,disabled:e.getAttribute('aria-disabled')})));
     assert(part!==1||actions[0].disabled==='true','First book part previous button enabled');assert(part!==Number(parts)||actions[1].disabled==='true','Last book part next button enabled');
     bookResults.push({id,part,totalParts:Number(parts),sourceBytes:Buffer.byteLength(expected),fullTextSha256:sha(plain(actual)),...b});
   }
 }
 await page.goto(base+'/mesh');await page.fill('#name','มด');await page.fill('#msg','สวัสดีครับ');await page.locator('form button').click();await page.waitForTimeout(100);assert(sent.length===1&&sent[0].from==='มด'&&sent[0].text==='สวัสดีครับ','Thai chat round trip failed');
 await page.fill('#msg','ก'.repeat(22));await page.locator('form button').click();assert(sent.length===1&&(await page.inputValue('#msg'))==='ก'.repeat(22),'Overlong message was cut or sent');assert(await page.locator('#msg').evaluate(e=>!e.validity.valid),'Message byte limit not reported');
 await page.fill('#msg','ทดสอบ');await page.fill('#name','ก'.repeat(4));await page.locator('form button').click();assert(sent.length===1&&await page.locator('#name').evaluate(e=>!e.validity.valid),'Name byte limit not reported');
 await page.goto(base+'/#/');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('118'));
 const downloadPromise=page.waitForEvent('download',{timeout:120000});await page.click('#saveBtn');const download=await downloadPromise;const saved=path.join(out,'survival-wiki-offline.html');await download.saveAs(saved);await page.waitForFunction(()=>document.getElementById('saveMsg').textContent.startsWith('บันทึกแล้ว'));
 const saveMessage=await page.locator('#saveMsg').textContent();assert(!saveMessage.includes('ข้าม'),'Offline save skipped files');
 const html=fs.readFileSync(saved,'utf8'),embeddedMatch=html.match(/<script>window\.OFFLINE=(.*?);window\.OFFLINE_IMG=(.*?);<\/script>/s);assert(embeddedMatch,'Offline payload missing');const offlineData=JSON.parse(embeddedMatch[1]),offlineImages=JSON.parse(embeddedMatch[2]);
 assert(entries.every(([id])=>offlineData['/wiki/articles/'+id+'.md']),'Offline export lacks articles');assert(JSON.parse(offlineData['/wiki/search.json']).length===118,'Offline search not embedded');assert(images.every(p=>/^data:image\/jpeg;base64,/.test(offlineImages[p]||'')),'Offline images not embedded');assert(/url\('data:[^']+;base64,/.test(html),'Offline font not embedded');
 const offlineContext=await browser.newContext({offline:true}),offlinePage=await offlineContext.newPage();offlinePage.on('pageerror',e=>pageErrors.push('offline: '+e.message));const offlineRequests=[];offlinePage.on('request',r=>{if(/^https?:/.test(r.url()))offlineRequests.push(r.url())});
 await offlinePage.setViewportSize({width:240,height:740});await offlinePage.goto(pathToFileURL(saved).href+'#/a/rule-of-threes');await offlinePage.waitForSelector('main article h1');
 assert((await offlinePage.locator('main article h1').textContent()).includes('กฎเลขสาม'),'Offline Thai article failed');
 const offlineImageResults=await offlinePage.evaluate(async()=>{const ims=[...document.querySelectorAll('article img')];for(const im of ims)im.loading='eager';return Promise.all(ims.map(async im=>{await im.decode();return {embedded:im.src.startsWith('data:image/jpeg;base64,'),width:im.naturalWidth,height:im.naturalHeight}}))});assert(offlineImageResults.length&&offlineImageResults.every(x=>x.embedded&&x.width===448),'Offline Thai diagram failed');
 await offlinePage.evaluate(()=>document.fonts.ready);assert(await offlinePage.evaluate(async()=>{const f=await document.fonts.load('17px Sarabun','ภาษาไทย');return f.length>0&&f.every(x=>x.status==='loaded')}),'Offline embedded font failed');
 await offlinePage.goto(pathToFileURL(saved).href+'#/search/'+encodeURIComponent('เงื่อน'));await offlinePage.waitForFunction(()=>document.querySelector('main h1')?.textContent.startsWith('พบ '));assert(await offlinePage.locator('main a[href="#/a/knots"]').count(),'Offline Thai search failed');assert(offlineRequests.length===0,'Offline page tried an HTTP request');
 assert(pageErrors.length===0,'JavaScript errors: '+pageErrors.join('; '));
 const report={result:'PASS',testedAtUtc:new Date().toISOString(),browser:'Headless Chromium / installed Chrome',environment:'Local HTTP server serves real generated SD files and gzip. ESP32 status/chat API responses are simulated.',articleCount:118,allArticlesAt240px:true,viewports:[240,320,390],layoutChecks:articleResults,thaiSearch:searches,books:bookResults,allBookPartFilesChecked:84,localFont:font,images:imageInfo,thaiChat:{utf8RoundTrip:true,message63ByteLimit:true,name11ByteLimit:true,unsentTextPreserved:true},offlineExport:{articleFiles:118,searchRecords:118,embeddedJpegImages:images.length,embeddedSarabunFont:true,fileBytes:Buffer.byteLength(html),reopenedWithNetworkingDisabled:true,thaiArticle:true,thaiSearch:true,thaiImageDecode:true,fontLoaded:true,httpRequests:offlineRequests},gzipMatches:true,javaScriptErrors:pageErrors,limitations:['No physical ESP32, LCD, touchscreen or SD electrical interface was connected.','Phone GPS, compass, camera, sensor permissions and self-signed HTTPS certificate flow were not hardware tested.','PDF layout and firmware binary validation are checked separately.']};
 fs.writeFileSync(path.join(root,'docs/thai-web-test-report.json'),JSON.stringify(report,null,2)+'\n');
 const dr=JSON.parse(fs.readFileSync(path.join(root,'.thai-work/diagrams/validation.json'),'utf8'));assert(dr.diagrams===22&&dr.reports.every(r=>r.issues.length===0),'Diagram bounds report failed');fs.writeFileSync(path.join(root,'docs/thai-diagram-test-report.json'),JSON.stringify({...dr,result:'PASS',jpegFrames:imageInfo,validation:['All 22 original diagrams were inspected before reauthoring.','Native SVGs use Sarabun and preserve labels, numeric instructions and diagram relationships.','All text fits its assigned width and canvas; no text bounding boxes overlap.','All exported JPEGs use baseline encoding, width 448, for CYD 1/2 scaling.']},null,2)+'\n');
 await offlineContext.close();await context.close();await browser.close();server.close();
 console.log('PASS: 118 articles; 84 book files / first-last rendered; 22 baseline images; Thai search/chat; full offline export reopened without network.');
})().catch(error=>{console.error(error);server.close();process.exit(1)});
