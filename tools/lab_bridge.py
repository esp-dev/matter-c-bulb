"""Bounded SSH-tunnel UDP lab bridge. Matter datagrams are never decoded/changed.

target runs on Windows; gateway runs on the VM. Forward gateway TCP localhost
45540 to target localhost45540 with SSH -R. Gateway publishes a DNS-SD alias for
operational instances observed from the one physical target. This tests real
chip-tool PDUs, but is not a native LAN discovery/routing test.
"""
import argparse
import asyncio
import ipaddress
import json
import re
import socket
import struct
import time

HEADER = struct.Struct('!BIH')

def dns_name(data, pos, seen=None):
    seen = set() if seen is None else seen
    if pos in seen or len(seen) > 32:
        raise ValueError('DNS name loop')
    seen.add(pos)
    parts = []
    while True:
        n = data[pos]; pos += 1
        if n & 0xc0 == 0xc0:
            pointer = ((n & 63) << 8) | data[pos]
            suffix, _ = dns_name(data, pointer, seen)
            parts.append(suffix)
            return '.'.join(parts), pos + 1
        if n & 0xc0 or n > 63:
            raise ValueError('DNS label')
        if not n:
            return '.'.join(parts), pos
        parts.append(data[pos:pos+n].decode('ascii')); pos += n

def operational_instances(data, hostname, include_ttl=False):
    """Extract only SRV instances whose target is the physical target hostname."""
    if len(data) < 12:
        return []
    _, flags, questions, answers, authority, additional = struct.unpack_from('!6H', data)
    if not flags & 0x8000:
        return []
    pos = 12; found = []
    try:
        for _ in range(questions):
            _, pos = dns_name(data, pos); pos += 4
        for _ in range(answers + authority + additional):
            name, pos = dns_name(data, pos)
            kind, _, ttl, size = struct.unpack_from('!HHIH', data, pos); pos += 10
            end = pos + size
            if end > len(data):
                raise ValueError('DNS length')
            if kind == 33 and size >= 7:
                target, _ = dns_name(data, pos + 6)
                match = re.fullmatch(r'([0-9a-fA-F]{16}-[0-9a-fA-F]{16})\._matter\._tcp\.local', name)
                if match and target.lower() == hostname.lower() + '.local':
                    if include_ttl:found.append((match[1].upper(),ttl))
                    elif ttl:found.append(match[1].upper())
            pos = end
    except (IndexError, ValueError, struct.error, UnicodeError):
        return []
    return found

def name_bytes(text):
    return b''.join(bytes([len(part)])+part.encode('ascii') for part in text.split('.'))+b'\0'

def alias_reply(instances, local, port, ttl=10):
    records=[];host='bulb-lab-relay.local'
    def rr(name,kind,body,unique=True):
        return name_bytes(name)+struct.pack('!HHIH',kind,0x8001 if unique else 1,ttl,len(body))+body
    for instance in instances:
        service=instance+'._matter._tcp.local'
        records.append(rr('_matter._tcp.local',12,name_bytes(service),False))
        records.append(rr(service,33,struct.pack('!3H',0,0,port)+name_bytes(host)))
        records.append(rr(service,16,b'\x07SII=500\x07SAI=300\x08SAT=4000'))
    records.append(rr(host,28,socket.inet_pton(socket.AF_INET6,local)))
    return struct.pack('!6H',0,0x8400,0,len(records),0,0)+b''.join(records)

async def read_frame(reader):
    kind, peer, size = HEADER.unpack(await reader.readexactly(HEADER.size))
    if size > 4096 or kind not in (0, 1):
        raise ValueError('Bridge frame')
    return kind, peer, await reader.readexactly(size)

class Wire:
    def __init__(self, writer):
        self.writer = writer
    def send(self, peer, data, kind=1):
        if len(data) > 4096 or self.writer.transport.get_write_buffer_size() > 262144:
            return
        self.writer.write(HEADER.pack(kind, peer, len(data)) + data)

class Target(asyncio.DatagramProtocol):
    def __init__(self, wire, peer):
        self.wire, self.peer = wire, peer
    def datagram_received(self, data, _):
        self.wire.send(self.peer, data)

async def target(args):
    loop = asyncio.get_running_loop(); active = False
    async def connected(reader, writer):
        nonlocal active
        if active:
            writer.close(); return
        active = True; wire = Wire(writer); peers = {}; mdns = None
        async def discover():
            nonlocal mdns
            mdns = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            mdns.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_MULTICAST_IF, args.interface)
            mdns.bind((args.local, 0, 0, args.interface)); mdns.setblocking(False)
            query = bytes.fromhex('000000000001000000000000') + b'\x07_matter\x04_tcp\x05local\0\0\x0c\x80\x01'
            advertised = {}
            while True:
                await loop.sock_sendto(mdns, query, ('ff02::fb', 5353, 0, args.interface))
                until = loop.time() + 2
                while loop.time() < until:
                    try:
                        packet, address = await asyncio.wait_for(loop.sock_recvfrom(mdns, 4096), until-loop.time())
                    except asyncio.TimeoutError:
                        break
                    if ipaddress.ip_address(address[0].split('%')[0]) != ipaddress.ip_address(args.device):
                        continue
                    for instance,ttl in operational_instances(packet, args.hostname,True):
                        if not ttl:
                            if instance in advertised:del advertised[instance];wire.send(0,('-'+instance).encode('ascii'),0)
                        else:
                            if instance not in advertised:wire.send(0,instance.encode('ascii'),0)
                            advertised[instance]=loop.time()+min(ttl,30)
                for instance,expires in list(advertised.items()):
                    if loop.time()>=expires:del advertised[instance];wire.send(0,('-'+instance).encode('ascii'),0)
                await asyncio.sleep(1)
        discovery = asyncio.create_task(discover())
        try:
            while True:
                kind, peer, data = await read_frame(reader)
                if kind != 1:
                    continue
                if peer not in peers:
                    if len(peers) >= 32:
                        raise ValueError('Peer limit')
                    udp=socket.socket(socket.AF_INET6,socket.SOCK_DGRAM);udp.setblocking(False)
                    try:
                        udp.bind((args.local,0,0,args.interface));udp.connect((args.device,args.device_port,0,args.interface))
                        transport, _ = await loop.create_datagram_endpoint(lambda: Target(wire, peer),sock=udp)
                    except BaseException:
                        udp.close();raise
                    peers[peer] = transport
                peers[peer].sendto(data,(args.device,args.device_port,0,args.interface))
        except asyncio.IncompleteReadError:
            pass
        finally:
            discovery.cancel()
            await asyncio.gather(discovery, return_exceptions=True)
            if mdns: mdns.close()
            for transport in peers.values(): transport.close()
            writer.close(); await writer.wait_closed(); active = False
    server = await asyncio.start_server(connected, '127.0.0.1', args.tcp_port)
    async with server:
        await asyncio.sleep(args.seconds)

async def gateway(args):
    reader, writer = await asyncio.open_connection('127.0.0.1', args.tcp_port)
    wire = Wire(writer); peers = {}; reverse = {}; instances = set()
    interface=socket.if_nametoindex(args.interface_name)
    class Discovery(asyncio.DatagramProtocol):
        def datagram_received(self,data,address):
            if not instances or len(data)<12 or data[2]&0x80:return
            try:
                count=struct.unpack_from('!H',data,4)[0];pos=12;wanted=False
                for _ in range(min(count,16)):
                    name,pos=dns_name(data,pos);pos+=4
                    wanted |= name=='bulb-lab-relay.local' or name=='_matter._tcp.local' or any(name==i+'._matter._tcp.local' for i in instances)
                if wanted:
                    packet=alias_reply(instances,args.local,args.udp_port)
                    mdns.sendto(packet,address);mdns.sendto(packet,('ff02::fb',5353,0,interface))
            except (IndexError,ValueError,struct.error,UnicodeError):pass
    sock=socket.socket(socket.AF_INET6,socket.SOCK_DGRAM);sock.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    sock.setsockopt(socket.IPPROTO_IPV6,socket.IPV6_V6ONLY,1);sock.bind(('::',5353))
    sock.setsockopt(socket.IPPROTO_IPV6,socket.IPV6_JOIN_GROUP,socket.inet_pton(socket.AF_INET6,'ff02::fb')+struct.pack('@I',interface))
    sock.setsockopt(socket.IPPROTO_IPV6,socket.IPV6_MULTICAST_IF,interface);sock.setsockopt(socket.IPPROTO_IPV6,socket.IPV6_MULTICAST_HOPS,255);sock.setblocking(False)
    mdns,_=await asyncio.get_running_loop().create_datagram_endpoint(Discovery,sock=sock)
    class Gateway(asyncio.DatagramProtocol):
        def datagram_received(self, data, address):
            if address not in reverse:
                if len(peers) >= 32: return
                peer = len(peers)+1; peers[peer] = address; reverse[address] = peer
            wire.send(reverse[address], data)
    transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(Gateway, local_addr=('::', args.udp_port), family=socket.AF_INET6)
    try:
        async with asyncio.timeout(args.seconds):
            while True:
                kind, peer, data = await read_frame(reader)
                if kind == 1:
                    if peer in peers: transport.sendto(data, peers[peer])
                else:
                    instance = data.decode('ascii')
                    if instance.startswith('-') and instance[1:] in instances:
                        instances.remove(instance[1:]);mdns.sendto(alias_reply([instance[1:]],args.local,args.udp_port,0),('ff02::fb',5353,0,interface))
                        if instances:mdns.sendto(alias_reply(instances,args.local,args.udp_port),('ff02::fb',5353,0,interface))
                        continue
                    if re.fullmatch('[0-9A-F]{16}-[0-9A-F]{16}', instance) and instance not in instances:
                        instances.add(instance)
                        mdns.sendto(alias_reply(instances,args.local,args.udp_port),('ff02::fb',5353,0,interface))
                        print(json.dumps({'published': instance}), flush=True)
    finally:
        transport.close(); writer.close(); await writer.wait_closed()
        if instances:mdns.sendto(alias_reply(instances,args.local,args.udp_port,0),('ff02::fb',5353,0,interface))
        mdns.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='role', required=True)
    for role in ('target', 'gateway'):
        p = sub.add_parser(role); p.add_argument('--tcp-port', type=int, default=45540); p.add_argument('--seconds', type=int, default=1800)
        if role == 'target':
            p.add_argument('--local', required=True); p.add_argument('--interface', type=int, required=True); p.add_argument('--device', required=True); p.add_argument('--device-port', type=int, default=5540); p.add_argument('--hostname', required=True)
        else:
            p.add_argument('--interface-name', default='ens33'); p.add_argument('--local',required=True);p.add_argument('--udp-port', type=int, default=55540)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600: parser.error('Bounded run: 1..3600 seconds')
    try: asyncio.run(target(args) if args.role == 'target' else gateway(args))
    except (asyncio.TimeoutError, asyncio.IncompleteReadError, KeyboardInterrupt): pass

if __name__ == '__main__': main()
