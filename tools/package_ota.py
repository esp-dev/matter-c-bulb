"""Wrap an already signed ESP image in a standard Matter OTA SHA256 envelope."""
import argparse
import hashlib
from pathlib import Path
import struct

def integer(tag,value):
    width=1 if value<=255 else 2 if value<=65535 else 4 if value<=0xffffffff else 8
    return bytes([0x24+{1:0,2:1,4:2,8:3}[width],tag])+value.to_bytes(width,'little')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--version',type=int,required=True)
    p.add_argument('--vendor',type=lambda v:int(v,0),default=0xfff1)
    p.add_argument('--product',type=lambda v:int(v,0),default=0x8000)
    a=p.parse_args()
    if not 1<=a.version<=0xffffffff or not 1<=a.vendor<=0xfff4 or not 1<=a.product<=65535:p.error('Invalid identity/version')
    if a.output.exists():p.error('Refusing to replace existing package')
    payload=a.input.read_bytes()
    if not payload or payload[0]!=0xe9 or len(payload)%4096 or payload[-4096]!=0xe7:p.error('Expected signed ESP image with a v2 signature sector')
    version=str(a.version).encode('ascii')
    header=b'\x15'+integer(0,a.vendor)+integer(1,a.product)+integer(2,a.version)+bytes([0x2c,3,len(version)])+version+integer(4,len(payload))+integer(8,1)+b'\x30\x09\x20'+hashlib.sha256(payload).digest()+b'\x18'
    image=struct.pack('<IQI',0x1beef11e,16+len(header)+len(payload),len(header))+header+payload
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(image)
    print(f'Created Matter OTA: {len(image)} bytes, version {a.version}, SHA256 {hashlib.sha256(image).hexdigest()}')

if __name__=='__main__':main()
