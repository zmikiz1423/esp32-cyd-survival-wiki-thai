/** Real portable server/browser integration with a tiny synthetic OSM fixture. */
const fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const {chromium}=require(path.resolve(__dirname,'../.thai-work/web-qa/node_modules/playwright-core'));
const root=path.resolve(__dirname,'..'),work=path.join(root,'.thai-work','province-ui-'+Date.now());fs.mkdirSync(work,{recursive:true});
const fixture=path.join(work,'fixture.osm'),stateFile=path.join(work,'state.json');
fs.writeFileSync(fixture,`<osm version="0.6" generator="QA"><node id="1" lat="9.13" lon="99.32"><tag k="name:th" v="โรงพยาบาลทดสอบ"/><tag k="amenity" v="hospital"/></node><node id="2" lat="9.131" lon="99.321"/><way id="1"><nd ref="1"/><nd ref="2"/><tag k="highway" v="residential"/><tag k="name:th" v="ถนนทดสอบ"/></way></osm>`);
const log=fs.openSync(path.join(work,'server.log'),'w'),server=spawn(path.join(root,'scripts/offline-runtime/python.exe'),['-u',path.join(root,'scripts/offline-map-server.py'),'--no-browser','--state-file',stateFile,'--output',path.join(work,'maps'),'--cache',path.join(work,'cache'),'--pbf',fixture],{cwd:root,windowsHide:true,stdio:['ignore',log,log]});
const pause=ms=>new Promise(r=>setTimeout(r,ms)),checks=[];function check(name,condition){if(!condition)throw Error(name);checks.push(name)}
async function until(fn,ms=30000){const end=Date.now()+ms;while(Date.now()<end){if(await fn())return;await pause(100)}throw Error('Timed out waiting')}
let browser,info;
(async()=>{try{
 await until(()=>fs.existsSync(stateFile));info=JSON.parse(fs.readFileSync(stateFile));
 browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:1120,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(info.url);await page.waitForFunction(()=>!document.querySelector('#start').disabled);
 check('77 provinces and Surat default',await page.locator('#province option').count()===77&&await page.locator('#province').inputValue()==='TH-84');
 check('Province plan counts above old single-job cap',Number((await page.locator('#tileCount').textContent()).replace(/[^0-9]/g,''))>3000);
 const layouts=[];for(const width of [320,390,760,1120]){await page.setViewportSize({width,height:1000});layouts.push(await page.evaluate(()=>({width:innerWidth,scrollWidth:document.documentElement.scrollWidth})));}
 check('No horizontal overflow 320/390/760/1120',layouts.every(x=>x.width===x.scrollWidth));
 await page.setViewportSize({width:1120,height:1000});await page.screenshot({path:path.join(root,'docs/thai-offline-province-app-preview.png'),fullPage:true});
 await page.fill('#provinceFilter','สุราษ');await until(async()=>await page.locator('#province option').count()===1);check('Thai province search',await page.locator('#province').inputValue()==='TH-84');
 await page.fill('#provinceFilter','NOT_A_PROVINCE');await until(async()=>await page.locator('#province option').count()===0);check('No-results disables start',await page.locator('#start').isDisabled());
 await page.fill('#provinceFilter','สุราษ');await page.waitForFunction(()=>!document.querySelector('#start').disabled);
 await page.click('#start');await page.waitForFunction(()=>document.querySelector('#province').disabled);check('One click starts asynchronous whole-province job',true);
 await page.click('#cancel');await page.waitForFunction(()=>!document.querySelector('#province').disabled,{timeout:60000});check('Cancel unlocks and preserves settings',await page.locator('#province').inputValue()==='TH-84');
 const center=path.join(work,'maps/center.txt');check('Cancelled first job has no committed center',!fs.existsSync(center));
 // Reduced zoom still covers the full province. The source remains a synthetic fixture.
 await page.evaluate(()=>{const s=document.querySelector('#profile');s.add(new Option('QA province overview 8–8','8,8'));s.value='8,8';s.dispatchEvent(new Event('change'))});await page.waitForFunction(()=>!document.querySelector('#start').disabled);
 await page.click('#start');await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('เสร็จครบและตรวจไฟล์แล้ว'),{timeout:120000});
 check('Automatic build finishes and verifies without per-batch clicks',fs.existsSync(center));const coverage=JSON.parse(fs.readFileSync(path.join(work,'maps/coverage.json')));check('Coverage complete and zero missing',coverage.complete&&coverage.missing===0);
 const tiles=[];function walk(d){for(const f of fs.readdirSync(d,{withFileTypes:true})){const p=path.join(d,f.name);f.isDirectory()?walk(p):f.name.endsWith('.jpg')&&tiles.push(p)}}walk(path.join(work,'maps'));const times=tiles.map(p=>fs.statSync(p).mtimeMs);
 await page.click('#start');await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('เสร็จครบและตรวจไฟล์แล้ว'),{timeout:120000});await until(()=>!page.isClosed());await pause(1500);
 check('Resume does not rewrite existing valid JPEGs',tiles.every((p,i)=>fs.statSync(p).mtimeMs===times[i]));check('No browser JavaScript errors',errors.length===0);
 await page.click('#closeApp');await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('ปิดโปรแกรมแล้ว'));check('Close program from browser',true);
 const report={result:'PASS',checks,count:checks.length,layouts,javaScriptErrors:errors,source:'Tiny synthetic OSM fixture, actual Windows portable Python server and browser; not proof of real data completeness',physicalBoardTested:false};fs.writeFileSync(path.join(root,'docs/thai-offline-map-ui-test-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 }finally{if(browser)await browser.close();if(server.exitCode===null)server.kill();fs.closeSync(log)}})().catch(e=>{console.error(e);process.exitCode=1});
