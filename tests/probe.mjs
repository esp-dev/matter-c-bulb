import {resolve} from 'node:path';
import {readFile,writeFile} from 'node:fs/promises';
import {controllerEnvironment} from './controller_env.mjs';
const [modules,directory,output,iface='Wi-Fi']=process.argv.slice(2);
const {load,env,CommissioningController,InteractionClientProvider}=await controllerEnvironment(modules,resolve(directory,'storage'),iface);
const controller=new CommissioningController({environment:{environment:env,id:'bulb-primary'},autoConnect:false,adminFabricLabel:'Bulb acceptance'});
const result={};
try {
    await controller.start();const node=BigInt(JSON.parse(await readFile(resolve(directory,'node.json'),'utf8'))),provider=new InteractionClientProvider(controller.node),cached=await controller.createInteractionClient(node,undefined,{forcedConnection:true}),client=await provider.interactionClientFor(cached.address);
    for(const [symbol,file,attrs] of [['BasicInformation','basic-information',['softwareVersion']],['OperationalCredentials','operational-credentials',['commissionedFabrics']],['GeneralDiagnostics','general-diagnostics',['rebootCount','upTime','bootReason']],['OtaSoftwareUpdateRequestor','ota-software-update-requestor',['updateState','updatePossible','defaultOtaProviders']]]){
        const c=(await load(`@matter/types/dist/esm/clusters/${file}.js`))[symbol].Cluster;
        for(const attribute of attrs)result[attribute]=await client.getAttribute({endpointId:0,clusterId:c.id,attribute:c.attributes[attribute],requestFromRemote:true});
        if(symbol==='OtaSoftwareUpdateRequestor'){
            result.versionApplied=await client.getEvent({endpointId:0,clusterId:c.id,event:c.events.versionApplied});
            result.downloadErrors=await client.getEvent({endpointId:0,clusterId:c.id,event:c.events.downloadError});
        }
    }
    result.passed=true;
}catch(error){result.error={type:error.constructor?.name,message:error.message,code:error.code};throw error;}
finally {await writeFile(output,JSON.stringify(result,(_,v)=>typeof v==='bigint'?String(v):v,2)+'\n');await controller.close();}
console.log(JSON.stringify(result,(_,v)=>typeof v==='bigint'?String(v):v,2));
