"""DNS metadata parsing cannot redirect a second lab board into the bridge."""
import importlib.util
from pathlib import Path
import struct
import unittest
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).resolve().parents[1]/'tools/lab_bridge.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
def name(text):
    return b''.join(bytes([len(p)])+p.encode() for p in text.split('.'))+b'\0'
def reply(host):
    body=struct.pack('!3H',0,0,5540)+name(host+'.local')
    return struct.pack('!6H',0,0x8400,0,1,0,0)+name('0123456789ABCDEF-0000000000000001._matter._tcp.local')+struct.pack('!HHIH',33,1,120,len(body))+body
class Metadata(unittest.TestCase):
    def test_target_only(self):
        packet=reply('543204014520')
        self.assertEqual(bridge.operational_instances(packet,'543204014520'),['0123456789ABCDEF-0000000000000001'])
        self.assertEqual(bridge.operational_instances(packet,'543204014224'),[])
    def test_malformed(self):
        packet=reply('543204014520')
        for end in range(len(packet)):
            self.assertEqual(bridge.operational_instances(packet[:end],'543204014520'),[])
        self.assertEqual(bridge.operational_instances(packet[:12]+b'\xc0\x0c'+packet[14:],'543204014520'),[])
if __name__=='__main__':unittest.main()
