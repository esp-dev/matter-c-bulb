"""Five bounded EN resets on the explicit lab port; captures actual boot evidence.

This erases test fabrics/user Wi-Fi if the SDK reports the resets as power-on.
Read back factory status and recommission with the original QR afterward. EN is
not physical power removal: do not claim cold-power testing if the chip reports
a different reset reason. Factory identity is never written by this tool.
"""
import argparse
from pathlib import Path
import time
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    device=serial.Serial(port=None,baudrate=1000000,timeout=.1);device.dtr=False;device.rts=False;device.port=a.port;device.open()
    try:
        for step in range(1,6):
            device.rts=True;time.sleep(.15);device.rts=False
            end=time.monotonic()+3
            with (a.output/f'boot-{step}.bin').open('wb') as log:
                while time.monotonic()<end:log.write(device.read(4096))
            print('Captured EN reset '+str(step),flush=True)
        end=time.monotonic()+8
        with (a.output/'settled.bin').open('wb') as log:
            while time.monotonic()<end:log.write(device.read(4096))
    finally:device.rts=False;device.dtr=False;device.close()

if __name__=='__main__':main()
