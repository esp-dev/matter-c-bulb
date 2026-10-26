// Physical two-fabric reconnect regression. Only the specified C6 is briefly
// banned on the lab AP; the AP, Windows and other boards remain available.
import {resolve} from 'node:path';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import assert from 'node:assert/strict';
import {controllerEnvironment} from './controller_env.mjs';
const [modules,directory,device,output,routerConfig,iface='Wi-Fi']=process.argv.slice(2);
if(!routerConfig)throw Error('network_acceptance.mjs modules private-controller-dir private-device output ssh-router-config [interface]');
await mkdir(output,{recursive:true});const identity=JSON.parse(await readFile(resolve(device,'manifest.json'),'utf8'));
const mac=Array.isArray(identity.mac)?identity.mac.map(b=>b.toString(16).padStart(2,'0')).join(':'):identity.mac;
assert.match(mac,/^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$/);
const {load,peer,env,CommissioningController,InteractionClientProvider,ClientSubscriptions}=await controllerEnvironment(modules,resolve(directory,'storage'),iface);
const {OnOff}=await load('@matter/types/dist/esm/clusters/on-off.js'),{OperationalCredentials}=await load('@matter/types/dist/esm/clusters/operational-credentials.js');
const controller=new CommissioningController({environment:{environment:env,id:'bulb-primary'},autoConnect:false,adminFabricLabel:'Bulb acceptance'});
const result={peer,checks:[],reports:0,updates:0,closures:0};let subscriptions=[],closing=false;
const save=()=>writeFile(resolve(output,'result.json'),JSON.stringify(result,null,2)+'\n');
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function wait(test,seconds){const end=Date.now()+seconds*1000;while(!test()){assert(Date.now()<end,'subscription recovery deadline');await sleep(100);}}
async function passed(name){result.checks.push({name,passed:true});await save();console.log('PASS '+name);}
async function ban(milliseconds){await promisify(execFile)('C:/Program Files/Git/usr/bin/ssh.exe',['-F',routerConfig,'-o','BatchMode=yes','-o','ConnectTimeout=8','mr3020-whatwatt-lab',"ubus call hostapd.wlan0 del_client '"+JSON.stringify({addr:mac,reason:3,deauth:true,ban_time:milliseconds})+"'"],{timeout:15000});}
try {
    await controller.start();const node=BigInt(JSON.parse(await readFile(resolve(directory,'node.json'),'utf8'))),provider=new InteractionClientProvider(controller.node);
    const fresh=async()=>{const cached=await controller.createInteractionClient(node,undefined,{forcedConnection:true});return provider.interactionClientFor(cached.address);};
    let client=await fresh();const attr=(c,cluster,name)=>c.getAttribute({endpointId:cluster.id===6?1:0,clusterId:cluster.id,attribute:cluster.attributes[name],requestFromRemote:true});
    const fabrics=await attr(client,OperationalCredentials.Cluster,'commissionedFabrics');assert.equal(fabrics,2);
    const registry=controller.node.env.get(ClientSubscriptions),before=new Set(registry);
    await client.subscribeMultipleAttributesAndEvents({attributes:[{endpointId:1,clusterId:6,attributeId:0}],minIntervalFloorSeconds:1,maxIntervalCeilingSeconds:5,keepSubscriptions:true,isUrgent:true,dataVersionFilters:[],attributeListener:()=>++result.reports,updateReceived:()=>++result.updates,updateTimeoutHandler:()=>{if(!closing)++result.closures;}});
    subscriptions=[...registry].filter(s=>!before.has(s));assert.equal(subscriptions.length,1);await passed('two-fabrics-and-live-subscription');
    const toggle=()=>client.invoke({endpointId:1,clusterId:6,command:OnOff.Cluster.commands.toggle});
    await toggle();await wait(()=>result.reports>0&&result.updates>=2,15);await sleep(600);
    await passed('priming-response-ACK-drained-data-and-heartbeat-before-fault');
    await ban(2000);await sleep(6000);const shortReports=result.reports;await toggle();assert.equal(await attr(client,OperationalCredentials.Cluster,'commissionedFabrics'),fabrics);await wait(()=>result.reports>shortReports,20);await passed('short-WiFi-loss-reconnect-reports-and-fabric-retention');
    result.shortLossClosures=result.closures;await ban(20000);await sleep(27000);
    await controller.disconnectNode(node,true);client=await fresh();assert.equal(await attr(client,OperationalCredentials.Cluster,'commissionedFabrics'),fabrics);await toggle();await attr(client,OnOff.Cluster,'onOff');await passed('long-WiFi-loss-new-CASE-and-post-reconnect-control');
    // A sufficiently long loss may expire the old subscription. Re-establish
    // explicitly and require fresh reports instead of hiding closure counters.
    closing=true;for(const sub of subscriptions)await sub.close();subscriptions=[];closing=false;
    const baseline=new Set(registry),reports=result.reports,updates=result.updates;
    await client.subscribeMultipleAttributesAndEvents({attributes:[{endpointId:1,clusterId:6,attributeId:0}],minIntervalFloorSeconds:1,maxIntervalCeilingSeconds:5,keepSubscriptions:true,isUrgent:true,dataVersionFilters:[],attributeListener:()=>++result.reports,updateReceived:()=>++result.updates,updateTimeoutHandler:()=>{if(!closing)++result.closures;}});
    subscriptions=[...registry].filter(s=>!baseline.has(s));await toggle();await wait(()=>result.reports>reports&&result.updates>updates+1,20);await passed('resubscribe-updates-and-heartbeat-after-long-loss');
}catch(error){result.error={type:error.constructor?.name,message:error.message,code:error.code};await save();throw error;}
finally {closing=true;for(const sub of subscriptions)await sub.close();await controller.close();}
