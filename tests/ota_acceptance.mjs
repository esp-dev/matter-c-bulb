// Real signed ESP installation/restart, using the previously commissioned fabric.
import {resolve} from 'node:path';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {controllerEnvironment} from './controller_env.mjs';
const [modules,directory,output,image,versionText='2',iface='Wi-Fi']=process.argv.slice(2);
if(!modules||!directory||!output||!image)throw Error('ota_acceptance.mjs node_modules private-controller-dir private-output signed-matter.ota [version] [interface]');
await mkdir(output,{recursive:true});const version=Number(versionText),node=BigInt(JSON.parse(await readFile(resolve(directory,'node.json'),'utf8')));
const {load,peer,env,CommissioningController,InteractionClientProvider}=await controllerEnvironment(modules,resolve(directory,'storage'),iface);
const {SoftwareUpdateManager}=await load('@matter/node/dist/esm/behavior/system/software-update/SoftwareUpdateManager.js');
const {DeviceAdvertiser}=await load('@matter/protocol/dist/esm/protocol/DeviceAdvertiser.js');
const {OtaSoftwareUpdateProviderServer}=await load('@matter/node/dist/esm/behaviors/ota-software-update-provider/OtaSoftwareUpdateProviderServer.js');
const {PersistedFileDesignator}=await load('@matter/protocol/dist/esm/bdx/PersistedFileDesignator.js');
const {OtaSoftwareUpdateRequestor}=await load('@matter/types/dist/esm/clusters/ota-software-update-requestor.js');
const {BasicInformation}=await load('@matter/types/dist/esm/clusters/basic-information.js');
const {OperationalCredentials}=await load('@matter/types/dist/esm/clusters/operational-credentials.js');
const {GeneralCommissioning}=await load('@matter/types/dist/esm/clusters/general-commissioning.js');
const {OnOff}=await load('@matter/types/dist/esm/clusters/on-off.js');
const results={peer,queries:0,applies:0,notifications:0,version,passed:false};let token;
const save=()=>writeFile(resolve(output,'result.json'),JSON.stringify(results,null,2)+'\n');
const query=OtaSoftwareUpdateProviderServer.prototype.queryImage,apply=OtaSoftwareUpdateProviderServer.prototype.applyUpdateRequest,notify=OtaSoftwareUpdateProviderServer.prototype.notifyUpdateApplied;
OtaSoftwareUpdateProviderServer.prototype.checkUpdateAvailable=async function(request){
    if(request.softwareVersion>=version)return undefined;
    const fd=new PersistedFileDesignator('signed-bulb-firmware',this.updateStorage);await fd.writeFromStream(new Blob([await readFile(image)]).stream());
    return {fileDesignator:fd,newSoftwareVersion:version,newSoftwareVersionString:String(version),consentRequired:false};
};
OtaSoftwareUpdateProviderServer.prototype.queryImage=async function(request){++results.queries;const response=await query.call(this,request);if(response.updateToken)token=Buffer.from(response.updateToken);await save();console.log('OTA QueryImage received');return response;};
OtaSoftwareUpdateProviderServer.prototype.applyUpdateRequest=async function(request){assert.equal(request.newVersion,version);assert(token?.equals(Buffer.from(request.updateToken)));const response=await apply.call(this,request);++results.applies;await save();console.log('OTA ApplyUpdate received');return {...response,action:0,delayedActionTime:2};};
OtaSoftwareUpdateProviderServer.prototype.notifyUpdateApplied=async function(request){assert.equal(request.softwareVersion,version);assert(token?.equals(Buffer.from(request.updateToken)));await notify.call(this,request);++results.notifications;await save();console.log('OTA NotifyUpdateApplied received');};
const controller=new CommissioningController({environment:{environment:env,id:'bulb-primary'},autoConnect:false,enableOtaProvider:true,adminFabricLabel:'Bulb acceptance'});
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function wait(test,timeout){const end=Date.now()+timeout;while(Date.now()<end){if(await test())return;await sleep(200);}throw Error('Physical OTA deadline');}
try {
    await controller.start();await controller.otaProvider.setStateOf(SoftwareUpdateManager,{announceAsDefaultProvider:false});controller.node.env.get(DeviceAdvertiser).enterOperationalMode();controller.node.env.get(DeviceAdvertiser).restartAdvertisement();
    const provider=new InteractionClientProvider(controller.node);
    const fresh=async()=>{const cached=await controller.createInteractionClient(node,undefined,{forcedConnection:true});return provider.interactionClientFor(cached.address);};
    let client=await fresh();const attr=(c,cluster,name)=>c.getAttribute({endpointId:0,clusterId:cluster.id,attribute:cluster.attributes[name],requestFromRemote:true});
    assert.equal(await attr(client,OtaSoftwareUpdateRequestor.Cluster,'updatePossible'),true);
    results.previousVersion=await attr(client,BasicInformation.Cluster,'softwareVersion');assert(results.previousVersion<version);
    const fabricIndex=await attr(client,OperationalCredentials.Cluster,'currentFabricIndex');
    await client.setAttribute({attributeData:{endpointId:0,clusterId:0x2a,attribute:OtaSoftwareUpdateRequestor.Cluster.attributes.defaultOtaProviders,value:[{providerNodeId:controller.nodeId,endpoint:controller.otaProvider.number,fabricIndex}]}});
    await client.invoke({endpointId:0,clusterId:0x2a,command:OtaSoftwareUpdateRequestor.Cluster.commands.announceOtaProvider,request:{providerNodeId:controller.nodeId,vendorId:0xfff1,announcementReason:0,endpoint:controller.otaProvider.number}});
    await wait(()=>results.notifications===1,420000);
    await controller.disconnectNode(node,true);client=await fresh();assert.equal(await attr(client,BasicInformation.Cluster,'softwareVersion'),version);assert.equal(await attr(client,OperationalCredentials.Cluster,'commissionedFabrics'),1);
    results.regulatoryAfterBoot=await attr(client,GeneralCommissioning.Cluster,'regulatoryConfig');results.locationCapability=await attr(client,GeneralCommissioning.Cluster,'locationCapability');assert.equal(results.regulatoryAfterBoot,2);assert.equal(results.regulatoryAfterBoot,results.locationCapability);
    const applied=await client.getEvent({endpointId:0,clusterId:0x2a,event:OtaSoftwareUpdateRequestor.Cluster.events.versionApplied});assert(applied?.length);
    await client.invoke({endpointId:1,clusterId:6,command:OnOff.Cluster.commands.on});
    assert.equal(results.applies,1);results.passed=true;await save();console.log('PASS signed OTA installation, physical reboot, version, retained fabric, VersionApplied, NotifyUpdateApplied and post-boot control');
}catch(error){results.error={type:error.constructor?.name,message:error.message,code:error.code};await save();throw error;}
finally {await controller.close();}
