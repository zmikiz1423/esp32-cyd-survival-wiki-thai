const fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require(path.resolve(__dirname,'../.thai-work/web-qa/node_modules/playwright-core'));
const root=path.resolve(__dirname,'..'),html=fs.readFileSync(path.join(root,'firmware/src/mappage.h'),'utf8').split('R"HTML(')[1].split(')HTML"')[0];
let revision=1,delayedPoi=null,delayedRoute=null;const requests=[],checks=[];
const sets={1:{name:'สุราษฎร์ธานี',center:'9.1382 99.3215 14 8 16',provider:'local-osm'},2:{name:'ชุมพร',center:'10.493 99.18 13 8 16',provider:'openfreemap'}};
function check(name,ok){if(!ok)throw Error(name);checks.push(name)}
const server=http.createServer((req,res)=>{const u=new URL(req.url,'http://localhost');requests.push(u.pathname+u.search);const json=x=>{res.setHeader('Content-Type','application/json');res.end(JSON.stringify(x))};
 if(u.pathname==='/map'){res.setHeader('Content-Type','text/html;charset=utf-8');return res.end(html)}
 if(u.pathname==='/api/map/status')return json({mapRevision:revision,mapName:sets[revision].name,valid:false,age:4294967295,destination:null});
 if(u.searchParams.has('v')&&u.searchParams.get('v')!==String(revision)){res.statusCode=409;return res.end('Map changed')}
 if(u.pathname==='/maps/center.txt')return res.end(sets[revision].center);
 if(u.pathname==='/maps/source.json')return json({provider:sets[revision].provider});
 if(u.pathname==='/api/map/routes')return json({items:revision===1?['surat.gpx']:['chumphon.gpx'],selected:''});
 if(u.pathname==='/api/map/route'){if(u.searchParams.get('delayed')){delayedRoute=()=>json({points:[[9,99,1]]});return}return json({points:[]})}
 if(u.pathname==='/api/map/pois'){delayedPoi=()=>json({items:[{name:'OLD SURAT',lat:9,lon:99,category:'old'}],next:null});return}
 if(u.pathname==='/web/Sarabun-Regular.ttf')return res.end(fs.readFileSync(path.join(root,'sdcard/web/Sarabun-Regular.ttf')));
 if(u.pathname.endsWith('.jpg')){res.setHeader('Content-Type','image/png');return res.end(Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==','base64'))}
 res.statusCode=404;res.end();
});
(async()=>{let browser;try{
 await new Promise(r=>server.listen(0,'127.0.0.1',r));browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});const page=await browser.newPage({viewport:{width:240,height:320}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.goto('http://127.0.0.1:'+server.address().port+'/map');await page.waitForFunction(()=>mapLoaded&&mapRevision==='1'&&document.querySelector('#routes').options.length===2);
 check('Initial map selection loads its center and Thai name',await page.evaluate(()=>Math.abs(ll(cx,cy,z).lat-9.1382)<0.0001&&document.querySelector('#mapName').textContent==='สุราษฎร์ธานี'));
 await page.evaluate(()=>{document.querySelector('#poiQuery').value='OLD';searchPois(true);window.oldRequest=mapJson('route?delayed=1').then(()=>false,()=>true)});await page.waitForTimeout(100);check('Old set requests are in flight',!!delayedPoi&&!!delayedRoute);
 revision=2;await page.evaluate(()=>pollGps());await page.waitForFunction(()=>mapLoaded&&mapRevision==='2'&&document.querySelector('#mapName').textContent==='ชุมพร');delayedPoi();delayedRoute();
 check('Old map API response is rejected after set change',await page.evaluate(()=>oldRequest));await page.waitForTimeout(100);
 check('Selection switch clears previous POI results and overlays',await page.evaluate(()=>!document.querySelector('#poiResults').children.length&&!routePoints.length&&destination===null));
 check('New map uses its own center and zoom limits',await page.evaluate(()=>z===13&&zmin===8&&zmax===16&&Math.abs(ll(cx,cy,z).lat-10.493)<0.0001));
 await page.waitForFunction(()=>document.querySelector('#routes').options[1]?.value==='chumphon.gpx');
 check('New map loads its own GPX list',true);check('Switching local to OpenFreeMap restores full attribution',(await page.locator('#attribution').textContent()).includes('OpenFreeMap'));
 check('Tile requests carry selected-map revision',requests.some(x=>x.includes('.jpg?v=1'))&&requests.some(x=>x.includes('.jpg?v=2')));
 check('Metadata requests carry selected-map revision',requests.includes('/maps/center.txt?v=2')&&requests.includes('/maps/source.json?v=2'));
 check('API requests carry selected-map revision',requests.includes('/api/map/routes?v=2'));
 check('Map selection name fits narrow screen',await page.evaluate(()=>document.documentElement.scrollWidth===innerWidth));check('No JavaScript errors',errors.length===0);
 const report={result:'PASS',checks,count:checks.length,html:'actual firmware MAP_PAGE',gpsAndSdApis:'simulated',physicalBoardTested:false};fs.writeFileSync(path.join(root,'docs/thai-multi-map-phone-test-report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
 }finally{if(browser)await browser.close();await new Promise(r=>server.close(r))}})().catch(e=>{console.error(e);process.exitCode=1});
