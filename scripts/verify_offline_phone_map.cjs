/** Browser integration against real embedded HTML, simulated GPS/SD APIs. */
const fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require(path.resolve(__dirname,'../.thai-work/web-qa/node_modules/playwright-core'));
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'firmware/src/mappage.h'),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
let gps={valid:true,lat:9.1382,lon:99.3215,speed:4,course:90,altitude:12,hdop:1.2,satellites:9,phone:false,age:0,destination:null},selected='',dest=null;
const positionPosts=[];
const points=[[9.1382,99.3215,1],[9.14,99.325,0],[9.15,99.33,1]],checks=[];function check(n,v){if(!v)throw Error(n);checks.push(n)}
const server=http.createServer((req,res)=>{const u=new URL(req.url,'http://localhost');function json(x){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(x))}
 if(u.pathname==='/map'){res.setHeader('Content-Type','text/html;charset=utf-8');return res.end(html)}
 if(u.pathname==='/web/Sarabun-Regular.ttf')return res.end(fs.readFileSync(path.join(root,'sdcard/web/Sarabun-Regular.ttf')));
 if(u.pathname==='/maps/center.txt')return res.end('9.1382 99.3215 14 8 16');
 if(u.pathname==='/maps/source.json')return json({provider:'local-osm'});
 if(u.pathname==='/api/map/status')return json({...gps,destination:dest});
 if(u.pathname==='/api/map/pois')return json({items:u.searchParams.get('offset')==='0'?[{name:'โรงพยาบาล <ทดสอบ>',lat:9.14,lon:99.325,category:'hospital'}]:[{name:'วัดทดสอบ',lat:9.15,lon:99.33,category:'place_of_worship'}],next:u.searchParams.get('offset')==='0'?120:null});
 if(u.pathname==='/api/map/routes')return json({items:['เส้นทางทดสอบ.gpx'],selected});
 if(req.method==='POST'){let body='';req.on('data',c=>body+=c);return req.on('end',()=>{const p=Object.fromEntries(new URLSearchParams(body));if(u.pathname==='/api/pos')positionPosts.push(p);if(u.pathname==='/api/map/destination')dest={name:p.name,lat:Number(p.lat),lon:Number(p.lon)};if(u.pathname==='/api/map/route')selected=p.name;json({ok:true})})}
 if(u.pathname==='/api/map/route')return json({name:selected,points:selected?points:[],sampled:false});
 if(u.pathname.endsWith('.jpg')){const files=fs.readdirSync(path.join(root,'sdcard/maps/14'));const dir=path.join(root,'sdcard/maps/14',files[0]);const file=fs.readdirSync(dir).find(n=>n.endsWith('.jpg'));res.setHeader('Content-Type','image/jpeg');return res.end(fs.readFileSync(path.join(dir,file)))}
 res.statusCode=404;res.end('missing');
});
(async()=>{let browser;try{await new Promise(r=>server.listen(0,'127.0.0.1',r));const base='http://127.0.0.1:'+server.address().port;
 browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});const page=await browser.newPage({viewport:{width:390,height:844}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto(base+'/map');await page.waitForFunction(()=>document.querySelector('#gpsState').textContent.includes('9 ดาวเทียม'));
 check('Board GPS updates without phone geolocation',true);await page.click('#follow');await page.waitForFunction(()=>document.querySelector('#follow').classList.contains('on'));
 gps={...gps,lat:9.16,lon:99.35};await page.waitForFunction(()=>document.querySelector('#info').textContent.includes('9.1600'),{timeout:10000});check('Follow recenters on changing board GPS',true);
 await page.mouse.move(200,220);await page.mouse.down();await page.mouse.move(245,240);await page.mouse.up();check('Manual map pan disables follow',await page.locator('#follow').getAttribute('class')==='');
 await page.click('#find');await page.fill('#poiQuery','โรงพยาบาล');await page.locator('#poiForm button').click();await page.waitForSelector('#poiResults button');check('POI API results rendered as escaped text',(await page.locator('#poiResults button').first().textContent()).includes('<ทดสอบ>')&&await page.locator('#poiResults ทดสอบ').count()===0);
 await page.click('#poiMore');await page.waitForFunction(()=>document.querySelectorAll('#poiResults button').length===2);check('POI pagination appends results',true);
 await page.locator('#poiResults button').first().click();await page.waitForFunction(()=>document.querySelector('#panel').hidden);check('POI selection sends destination',dest&&dest.name==='โรงพยาบาล <ทดสอบ>');
 await page.click('#find');await page.selectOption('#routes','เส้นทางทดสอบ.gpx');await page.click('#loadRoute');await page.waitForFunction(()=>document.querySelector('#msg').textContent.includes('แสดงเส้นทางแล้ว'));check('GPX selection displays route with separate segments',selected==='เส้นทางทดสอบ.gpx');
 const layouts=[];for(const width of [240,320,390,640]){await page.setViewportSize({width,height:844});layouts.push(await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth})));}check('Phone panel fits 240/320/390/640 pixels',layouts.every(x=>x.width===x.scroll));
 await page.setViewportSize({width:390,height:844});await page.screenshot({path:path.join(root,'docs/thai-offline-phone-map-preview.png'),fullPage:true});await page.click('#closePanel');gps.valid=false;await page.waitForFunction(()=>document.querySelector('#gpsState').textContent.includes('รอตำแหน่ง'),{timeout:10000});check('Lost GPS fix is visibly reported',true);
 check('Local map attribution only credits OSM',(await page.locator('#attribution').textContent()).trim()==='© OpenStreetMap contributors');
 // Exercise the actual sendPos function and await its real mock-server POSTs.
 // Tracking fetch promises also proves rejected samples never issue a request.
 const beforePositionPosts=positionPosts.length;
 const freshness=await page.evaluate(async()=>{
   const originalFetch=window.fetch,pending=[];let posted=0;
   window.fetch=function(input,options){const p=originalFetch.apply(this,arguments);if(new URL(input,location.href).pathname==='/api/pos'){posted++;pending.push(p)}return p};
   lastSend=0;lastPos=null;lastSentTimestamp=0;
   const coords={latitude:9.1382,longitude:99.3215,accuracy:7},first={timestamp:Date.now()-1000,coords},counts=[];
   async function sample(position,force){sendPos(position,force);await Promise.all(pending);counts.push(posted)}
   try{await sample(first,false);await sample(first,true);await sample({timestamp:Date.now()-60000,coords},true);await sample({timestamp:Date.now(),coords},true);await sample({timestamp:NaN,coords},true);return counts}
   finally{window.fetch=originalFetch;lastSend=0;lastPos=null;lastSentTimestamp=0}
 });
 check('Fresh phone GPS sample posts to board',freshness[0]===1);
 check('Keepalive cannot repost the same cached GPS timestamp',freshness[1]===1);
 check('Stale phone GPS timestamp cannot post',freshness[2]===1);
 check('Newer fresh phone GPS sample posts to board',freshness[3]===2&&positionPosts.length-beforePositionPosts===2);
 check('Non-finite phone GPS timestamp cannot post',freshness[4]===2);
 // Serve the same HTML at an intercepted HTTPS origin, allowing the real share
 // handler to run with deterministic geolocation and wake-lock source stubs.
 const secure=await browser.newPage({viewport:{width:390,height:844}});secure.on('pageerror',e=>errors.push(e.message));
 await secure.route('https://cyd.test/**',async route=>{const u=new URL(route.request().url());await route.fulfill({response:await route.fetch({url:base+u.pathname+u.search})})});
 await secure.addInitScript(()=>{
   window.__geoMock={success:null,error:null,cleared:[],wakeReleased:0};
   Object.defineProperty(navigator,'geolocation',{configurable:true,value:{watchPosition(success,error){__geoMock.success=success;__geoMock.error=error;return 77},clearWatch(id){__geoMock.cleared.push(id)}}});
   Object.defineProperty(navigator,'wakeLock',{configurable:true,value:{request:async()=>({release(){__geoMock.wakeReleased++;return Promise.resolve()}})}});
 });
 await secure.goto('https://cyd.test/map');await secure.click('#share');await secure.waitForFunction(()=>watchId===77&&keep!==null&&wake!==null);
 let shared=secure.waitForResponse(r=>new URL(r.url()).pathname==='/api/pos'&&r.request().method()==='POST');
 await secure.evaluate(()=>__geoMock.success({timestamp:Date.now(),coords:{latitude:9.1382,longitude:99.3215,accuracy:7}}));await shared;
 await secure.evaluate(()=>__geoMock.error({message:'simulated GPS source lost'}));
 check('Geolocation error clears watch, keepalive, cached fix, and wake lock',await secure.evaluate(()=>watchId===null&&keep===null&&lastPos===null&&lastSentTimestamp===0&&lastSend===0&&wake===null&&__geoMock.cleared.includes(77)&&__geoMock.wakeReleased===1&&document.getElementById('share').className===''));
 await secure.click('#share');await secure.waitForFunction(()=>watchId===77&&keep!==null&&wake!==null);
 shared=secure.waitForResponse(r=>new URL(r.url()).pathname==='/api/pos'&&r.request().method()==='POST');
 await secure.evaluate(()=>__geoMock.success({timestamp:Date.now(),coords:{latitude:9.1383,longitude:99.3216,accuracy:7}}));await shared;await secure.click('#share');
 check('Stopping GPS share clears watch, keepalive, cached fix, and wake lock',await secure.evaluate(()=>watchId===null&&keep===null&&lastPos===null&&lastSentTimestamp===0&&lastSend===0&&wake===null&&__geoMock.cleared.length===2&&__geoMock.wakeReleased===2&&document.getElementById('share').className===''));
 await secure.close();check('No JavaScript errors',errors.length===0);
 const report={result:'PASS',checks,count:checks.length,layouts,javaScriptErrors:errors,phonePositionPosts:positionPosts.length,gpsAndSdApis:'simulated',geolocationAndWakeLock:'stubbed on intercepted HTTPS origin',html:'actual firmware MAP_PAGE',physicalBoardTested:false};fs.writeFileSync(path.join(root,'docs/thai-offline-phone-map-test-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 }finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}})().catch(e=>{console.error(e);process.exitCode=1});
