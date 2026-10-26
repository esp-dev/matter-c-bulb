import {resolve} from 'node:path';
import {pathToFileURL} from 'node:url';
import {networkInterfaces} from 'node:os';
import {readFile} from 'node:fs/promises';
import {peerRole} from '../matter/tools/matter/tests/peer_role.mjs';
let hooked=false;
export async function controllerEnvironment(modules,storage,iface='Wi-Fi') {
    const readyUntil=Date.now()+15000;
    while(!networkInterfaces()[iface]?.some(a=>a.family==='IPv6'&&!a.internal)) {
        if(Date.now()>=readyUntil)throw Error('Test controller interface has no IPv6 address: '+iface);
        await new Promise(r=>setTimeout(r,100));
    }
    const load=p=>import(pathToFileURL(resolve(modules,p)).href);
    if(JSON.parse(await readFile(resolve(modules,'@project-chip/matter.js/package.json'),'utf8')).version!=='0.17.9')throw Error('Expected pinned Matter.js0.17.9');
    const peer=await peerRole(modules,'corrected');
    const {NodeJsEnvironment}=await load('@matter/nodejs/dist/esm/index.js');
    if(!hooked){
        const {NodeJsNetwork}=await load('@matter/nodejs/dist/esm/net/NodeJsNetwork.js');
        const named=i=>Object.entries(networkInterfaces()).find(([,list])=>list.some(a=>String(a.scopeid)===i))?.[0]??i;
        const getMac=NodeJsNetwork.prototype.getIpMac,udp=NodeJsNetwork.prototype.createUdpSocket;
        NodeJsNetwork.prototype.getIpMac=function(i){return getMac.call(this,named(i));};
        NodeJsNetwork.prototype.createUdpSocket=function(o){return udp.call(this,{...o,netInterface:named(o.netInterface)});};hooked=true;
    }
    const {Logger,LogLevel}=await load('@matter/general/dist/esm/index.js');Logger.level=process.env.BULB_TEST_LOG==='info'?LogLevel.INFO:LogLevel.WARN;
    const {CommissioningController}=await load('@project-chip/matter.js/dist/esm/CommissioningController.js');
    const {InteractionClientProvider}=await load('@project-chip/matter.js/dist/esm/cluster/client/InteractionClient.js');
    const {ClientSubscriptions}=await load('@matter/protocol/dist/esm/action/client/subscription/ClientSubscriptions.js');
    const env=NodeJsEnvironment();env.vars.set('storage.path',resolve(storage));env.vars.set('mdns.networkInterface',iface);
    return {load,peer,env,CommissioningController,InteractionClientProvider,ClientSubscriptions};
}
