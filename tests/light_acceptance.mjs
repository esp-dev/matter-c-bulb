// Physical engineering acceptance, not an official CSA result. Private state
// and onboarding data are arguments/files, never included in normal output.
import {resolve} from 'node:path';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {controllerEnvironment} from './controller_env.mjs';
const [modules,ip,device,directory,mode='commission',iface='Wi-Fi',country='XX']=process.argv.slice(2);
if(!modules||!ip||!device||!directory||!['commission','reconnect'].includes(mode))throw Error('light_acceptance.mjs node_modules scoped-ipv6 private-device private-output [commission|reconnect] [interface]');
const root=resolve(directory);
await mkdir(root,{recursive:true});const identity=JSON.parse(await readFile(resolve(device,'manifest.json'),'utf8'));
const {load,peer,env,CommissioningController,InteractionClientProvider,ClientSubscriptions}=await controllerEnvironment(modules,resolve(root,'storage'),iface);
const clusters={};for(const [name,file] of Object.entries({Descriptor:'descriptor',BasicInformation:'basic-information',OnOff:'on-off',LevelControl:'level-control',ColorControl:'color-control',Groups:'groups',ScenesManagement:'scenes-management',Identify:'identify',OperationalCredentials:'operational-credentials'}))clusters[name]=(await load(`@matter/types/dist/esm/clusters/${file}.js`))[name].Cluster;
const controller=new CommissioningController({environment:{environment:env,id:'bulb-primary'},autoConnect:false,adminFabricLabel:'Bulb acceptance'});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));const checks=[];let subs=[],closing=false,closures=0,reports=0;
const save=()=>writeFile(resolve(root,'result.json'),JSON.stringify({mode,peer,checks,reports,closures},null,2)+'\n');
async function passed(name){checks.push({name,passed:true});await save();console.log('PASS '+name);}
try {
    await controller.start();
    const node=mode==='commission'?await controller.commissionNode({passcode:identity.passcode,discovery:{identifierData:{longDiscriminator:identity.discriminator},knownAddress:{type:'udp',ip,port:5540}},commissioning:{regulatoryLocation:0,regulatoryCountryCode:country}},{connectNodeAfterCommissioning:false}):BigInt(JSON.parse(await readFile(resolve(root,'node.json'),'utf8')));
    await writeFile(resolve(root,'node.json'),JSON.stringify(String(node))+'\n');await passed(mode==='commission'?'full-onnetwork-commissioning':'retained-fabric-after-restart');
    const provider=new InteractionClientProvider(controller.node);const cached=await controller.createInteractionClient(node,undefined,{forcedConnection:true});const client=await provider.interactionClientFor(cached.address);
    const attr=(ep,c,a)=>client.getAttribute({endpointId:ep,clusterId:c.id,attribute:c.attributes[a],requestFromRemote:true});
    const invoke=(ep,c,name,request)=>client.invoke({endpointId:ep,clusterId:c.id,command:c.commands[name],...(request===undefined?{}:{request})});
    const {Descriptor:D,OnOff:O,LevelControl:L,ColorControl:C,Groups:G,ScenesManagement:S,Identify:I,OperationalCredentials:F}=clusters;
    const rootServers=await attr(0,D,'serverList'),lightServers=await attr(1,D,'serverList');
    assert(rootServers.includes(0x65));assert.deepEqual(new Set(lightServers),new Set([0x1d,3,4,6,8,0x62,0x300]));
    const types=await attr(1,D,'deviceTypeList');assert(types.some(t=>Number(t.deviceType)===0x10d&&t.revision===5));
    assert.equal(await attr(0,F,'commissionedFabrics'),1);await passed('ECL-revision5-and-mandatory-server-inventory');
    await invoke(1,O,'off');assert.equal(await attr(1,O,'onOff'),false);await invoke(1,O,'on');assert.equal(await attr(1,O,'onOff'),true);await passed('on-off-read-invoke');
    await invoke(1,L,'moveToLevelWithOnOff',{level:180,transitionTime:2,optionsMask:0,optionsOverride:0});await sleep(400);assert.equal(await attr(1,L,'currentLevel'),180);
    await invoke(1,C,'moveToHueAndSaturation',{hue:85,saturation:254,transitionTime:2,optionsMask:0,optionsOverride:0});await sleep(400);assert.equal(await attr(1,C,'currentHue'),85);
    await invoke(1,C,'moveToColor',{colorX:30000,colorY:25000,transitionTime:2,optionsMask:0,optionsOverride:0});await sleep(400);assert.equal(await attr(1,C,'currentX'),30000);assert.equal(await attr(1,C,'colorMode'),1);
    await invoke(1,C,'moveToColorTemperature',{colorTemperatureMireds:350,transitionTime:2,optionsMask:0,optionsOverride:0});await sleep(400);assert.equal(await attr(1,C,'colorTemperatureMireds'),350);await passed('level-and-HS-XY-CT-transitions');
    const group=await invoke(1,G,'addGroup',{groupId:7,groupName:''});assert.equal(group.status,0);
    const stored=await invoke(1,S,'storeScene',{groupId:7,sceneId:1});assert.equal(stored.status,0);
    const viewed=await invoke(1,S,'viewScene',{groupId:7,sceneId:1});assert.equal(viewed.status,0);assert.equal(viewed.extensionFieldSetStructs.find(e=>e.clusterId===0x300).attributeValueList.length,9);
    await invoke(1,L,'moveToLevelWithOnOff',{level:20,transitionTime:0,optionsMask:0,optionsOverride:0});await invoke(1,C,'moveToColorTemperature',{colorTemperatureMireds:200,transitionTime:0,optionsMask:0,optionsOverride:0});
    await invoke(1,S,'recallScene',{groupId:7,sceneId:1,transitionTime:0});assert.equal(await attr(1,L,'currentLevel'),180);assert.equal(await attr(1,C,'colorTemperatureMireds'),350);await passed('atomic-groups-scenes-store-view-recall');
    await invoke(1,O,'offWithEffect',{effectIdentifier:0,effectVariant:0});assert.equal(await attr(1,O,'onOff'),false);
    await invoke(1,O,'onWithRecallGlobalScene');assert.equal(await attr(1,O,'onOff'),true);assert.equal(await attr(1,L,'currentLevel'),180);await passed('durable-global-scene');
    const registry=controller.node.env.get(ClientSubscriptions),before=new Set(registry);
    const initial=await client.subscribeMultipleAttributesAndEvents({attributes:[{endpointId:1,clusterId:6,attributeId:0},{endpointId:1,clusterId:8,attributeId:0},{endpointId:1,clusterId:0x300,attributeId:0}],minIntervalFloorSeconds:1,maxIntervalCeilingSeconds:5,keepSubscriptions:true,isUrgent:true,dataVersionFilters:[],attributeListener:()=>++reports,updateTimeoutHandler:()=>{if(!closing)++closures;}});
    assert.equal(initial.attributeReports.length,3);subs=[...registry].filter(s=>!before.has(s));assert.equal(subs.length,1);
    for(const hue of [170,85,42]){await invoke(1,C,'moveToHueAndSaturation',{hue,saturation:254,transitionTime:2,optionsMask:0,optionsOverride:0});await sleep(1500);assert.equal(await attr(1,C,'currentHue'),hue);}
    await sleep(5500);assert(reports>=6);assert.equal(closures,0);await passed('stable-three-path-Subscribe-updates-and-heartbeat');
    await invoke(1,I,'identify',{identifyTime:1});assert((await attr(1,I,'identifyTime'))<=1);await sleep(1200);assert.equal(await attr(1,I,'identifyTime'),0);await passed('identify-timeout');
    await save();
} catch(error){checks.push({name:'failure',passed:false,type:error.constructor?.name,message:error.message,code:error.code});await save();throw error;}
finally {closing=true;for(const sub of subs)await sub.close();await controller.close();}
