"""Capture two explicit C6 EN resets at ROM and develop baud; no eFuse writes.

Reject application/SDK log prefixes. ROM output is counted/reported separately,
because application configuration cannot suppress immutable ROM behavior.
Verify network control separately: silence alone is not a liveness test.
"""
import argparse
import json
from pathlib import Path
import re
import time
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);result=[]
    for baud in (115200,1000000):
        device=serial.Serial(port=None,baudrate=baud,timeout=.1);device.dtr=False;device.rts=False;device.port=a.port;device.open();raw=bytearray()
        try:
            device.rts=True;time.sleep(.15);device.rts=False;until=time.monotonic()+20
            while time.monotonic()<until:raw+=device.read(4096)
        finally:device.rts=False;device.dtr=False;device.close()
        (a.output/(str(baud)+'.bin')).write_bytes(raw)
        logs=bool(re.search(rb'BULB_|MATTER[_ ]|(?:^|\n)[IWE] \([0-9]+\)',raw))
        result.append({'baud':baud,'bytes':len(raw),'application_or_sdk_logs':logs,'rom_banner':b'ESP-ROM:' in raw,'passed':not logs})
        (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        assert not logs,'Application/SDK output in release'
    print('PASS release application/SDK/bootloader silence; ROM bytes recorded separately')

if __name__=='__main__':main()
