// Bounded administration of an already commissioned physical lab bulb.
import {resolve} from 'node:path';
import {readFile,writeFile} from 'node:fs/promises';
import {randomBytes,randomInt} from 'node:crypto';
import {controllerEnvironment} from './controller_env.mjs';
const [modules,directory,operation,output,iface='Wi-Fi']=process.argv.slice(2);
if(!['open-window','close-window','state','remove-own-fabric'].includes(operation)||!output)throw Error('admin.mjs modules controller-dir open-window|close-window|state|remove-own-fabric output [interface]');
const {load,env,CommissioningController,InteractionClientProvider}=await controllerEnvironment(modules,resolve(directory,'storage'),iface);
const controller=new CommissioningController({environment:{environment:env,id:'bulb-primary'},autoConnect:false,adminFabricLabel:'Bulb acceptance'});const result={operation};
try {
    await controller.start();const node=BigInt(JSON.parse(await readFile(resolve(directory,'node.json'),'utf8'))),provider=new InteractionClientProvider(controller.node),cached=await controller.createInteractionClient(node,undefined,{forcedConnection:true}),client=await provider.interactionClientFor(cached.address);
    const {OperationalCredentials}=await load('@matter/types/dist/esm/clusters/operational-credentials.js'),{AdministratorCommissioning}=await load('@matter/types/dist/esm/clusters/administrator-commissioning.js');
    result.fabrics=await client.getAttribute({endpointId:0,clusterId:0x3e,attribute:OperationalCredentials.Cluster.attributes.commissionedFabrics,requestFromRemote:true});
    result.ownFabric=await client.getAttribute({endpointId:0,clusterId:0x3e,attribute:OperationalCredentials.Cluster.attributes.currentFabricIndex,requestFromRemote:true});
    if(operation==='open-window') {
        const {Spake2p}=await load('@matter/general/dist/esm/crypto/Spake2p.js'),{NodeJsCrypto}=await load('@matter/nodejs/dist/esm/crypto/NodeJsCrypto.js');
        const passcode=randomInt(10000001,99999998),discriminator=randomInt(0,4096),salt=randomBytes(16),iterations=1000;
        const {w0,L}=await Spake2p.computeW0L(new NodeJsCrypto(),{salt,iterations},passcode),pakePasscodeVerifier=Buffer.concat([Buffer.from(w0.toString(16).padStart(64,'0'),'hex'),Buffer.from(L)]);
        await client.invoke({endpointId:0,clusterId:0x3c,command:AdministratorCommissioning.Cluster.commands.openCommissioningWindow,asTimedRequest:true,request:{commissioningTimeout:180,pakePasscodeVerifier,discriminator,iterations,salt}});
        result.window={passcode,discriminator}; // private output only
    }
    if(operation==='remove-own-fabric')result.removed=await client.invoke({endpointId:0,clusterId:0x3e,command:OperationalCredentials.Cluster.commands.removeFabric,request:{fabricIndex:result.ownFabric}});
    if(operation==='close-window')await client.invoke({endpointId:0,clusterId:0x3c,command:AdministratorCommissioning.Cluster.commands.revokeCommissioning,asTimedRequest:true});
    result.passed=true;console.log('PASS '+operation+' fabrics='+result.fabrics);
}catch(error){result.error={type:error.constructor?.name,message:error.message,code:error.code};throw error;}
finally {await writeFile(output,JSON.stringify(result,null,2)+'\n');await controller.close();}
