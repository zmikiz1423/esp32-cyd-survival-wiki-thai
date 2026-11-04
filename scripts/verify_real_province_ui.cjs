/** Full real-data 8..16 province build through the actual one-click browser UI. */
const fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const {chromium}=require(path.resolve(__dirname,'../.thai-work/web-qa/node_modules/playwright-core'));
const root=path.resolve(__dirname,'..'),work=path.join(root,'.thai-work','real-province-ui');fs.mkdirSync(work,{recursive:true});
const stateFile=path.join(work,'state-'+Date.now()+'.json'),log=fs.openSync(path.join(work,'server.log'),'a');
const child=spawn(path.join(root,'scripts/offline-runtime/python.exe'),['-X','utf8','-u',path.join(root,'scripts/offline-map-server.py'),'--no-browser','--state-file',stateFile,'--output',path.join(root,'province-maps/maps'),'--cache',path.join(root,'.thai-work/offline-cache'),'--pbf',path.join(root,'.thai-work/offline-cache/thailand-verified.osm.pbf')],{cwd:root,windowsHide:true,stdio:['ignore',log,log]});
const pause=ms=>new Promise(r=>setTimeout(r,ms));let info,browser,base,finished=false;
async function api(path,body){const r=await fetch(base+path,{method:body===undefined?'GET':'POST',headers:{'X-Map-Token':info.token,'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body)});const v=await r.json();if(!r.ok)throw Error(JSON.stringify(v));return v}
(async()=>{try{
 for(let i=0;i<200&&!fs.existsSync(stateFile);i++){if(child.exitCode!==null)throw Error('Server exited');await pause(100)}info=JSON.parse(fs.readFileSync(stateFile));base=new URL(info.url).origin;
 browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});const page=await browser.newPage({viewport:{width:1120,height:1050}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(info.url);await page.waitForFunction(()=>!document.querySelector('#start').disabled,{timeout:60000});
 if(await page.locator('#profile').inputValue()!=='8,16'||await page.locator('#province').inputValue()!=='TH-84')throw Error('Unexpected default province/profile');
 await page.click('#start');let previous=-1,lastStage='',final;
 for(let n=0;n<720;n++){
  const s=await api('/api/status');if(s.stage==='error')throw Error(s.error||s.message);
  if(s.stage!==lastStage||Math.floor(s.done/1000)!==previous){console.log(JSON.stringify({stage:s.stage,done:s.done,total:s.total,bytes:s.completedBytes,batch:s.batch}));previous=Math.floor(s.done/1000);lastStage=s.stage;}
  if(s.stage==='complete'&&!s.running){final=s;break}await pause(5000)
 }
 if(!final)throw Error('Full province build timed out');if(final.done!==55125||final.missing!==0)throw Error('Incomplete full province');
 await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('เสร็จครบและตรวจไฟล์แล้ว'),{timeout:15000});
 await page.screenshot({path:path.join(root,'docs/thai-offline-province-real-complete.png'),fullPage:true});
 if(errors.length)throw Error(errors.join('\n'));
 const report={result:'PASS',source:'Real checksum-verified Geofabrik Thailand OSM',oneClickBrowserBuild:true,province:'TH-84',minZoom:8,maxZoom:16,tiles:final.done,missing:final.missing,jpegBytes:final.completedBytes,job:final.job,javaScriptErrors:errors,physicalBoardTested:false};
 fs.writeFileSync(path.join(root,'docs/thai-offline-province-real-ui-test-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 await page.click('#closeApp');await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('ปิดโปรแกรมแล้ว'));finished=true;
 }finally{
  if(!finished&&info){try{await api('/api/cancel',{});for(let i=0;i<30;i++){if(!(await api('/api/status')).running)break;await pause(1000)}await api('/api/close',{})}catch{}}
  if(browser)await browser.close();if(child.exitCode===null&&!finished)child.kill();fs.closeSync(log)
 }})().catch(e=>{console.error(e);process.exitCode=1});
