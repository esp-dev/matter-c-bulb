"""Bounded development UART capture; no console commands or flash writes."""
import argparse
import json
from pathlib import Path
import time
import serial

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--baud', type=int, default=1000000)
    p.add_argument('--seconds', type=int, default=900)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--allow-button', action='store_true', help='Test-only: consume control.json button_ms100..11000 through BOOT/DTR; never resets EN')
    p.add_argument('--allow-reset', action='store_true', help='Test-only: consume control.json reset_ms100..500 through EN/RTS; never erases flash directly')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    device = serial.Serial(port=None, baudrate=a.baud, timeout=.2)
    device.dtr = False
    device.rts = False
    device.port = a.port
    device.open()
    until = time.monotonic()+a.seconds
    pending = b''; button_until=0;reset_until=0
    with (a.output/'uart.bin').open('wb') as raw, (a.output/'events.jsonl').open('w', encoding='utf-8') as log:
        try:
            while time.monotonic()<until and not (a.output/'stop').exists():
                control=a.output/'control.json'
                if (a.allow_button or a.allow_reset) and control.exists() and not button_until and not reset_until:
                    request=json.loads(control.read_text())
                    if set(request)=={'button_ms'} and a.allow_button:
                        duration=request['button_ms']
                        if type(duration) is not int or not 100<=duration<=11000:raise ValueError('Invalid bounded BOOT request')
                        control.unlink();device.dtr=True;button_until=time.monotonic()+duration/1000;label='TEST_BOOT_PRESS'
                    elif set(request)=={'reset_ms'} and a.allow_reset:
                        duration=request['reset_ms']
                        if type(duration) is not int or not 100<=duration<=500:raise ValueError('Invalid bounded EN request')
                        control.unlink();device.rts=True;reset_until=time.monotonic()+duration/1000;label='TEST_EN_RESET'
                    else:raise ValueError('Unauthorized or malformed UART control request')
                    log.write(json.dumps(dict(unix_ms=int(time.time()*1000),text=label+' milliseconds='+str(duration)))+'\n');log.flush()
                if button_until and time.monotonic()>=button_until:device.dtr=False;button_until=0
                if reset_until and time.monotonic()>=reset_until:device.rts=False;reset_until=0
                data = device.read(4096)
                if not data:
                    continue
                raw.write(data);raw.flush();pending += data
                while b'\n' in pending:
                    line,pending = pending.split(b'\n',1)
                    text = line.decode('utf-8', errors='replace').rstrip('\r')
                    log.write(json.dumps(dict(unix_ms=int(time.time()*1000),text=text))+'\n');log.flush()
                    if text.startswith(('BULB_HEAP ', 'BULB_FAULT ', 'BULB_IPV6 ', 'BULB_IPV4 ')):
                        print(text, flush=True)
        finally:
            device.dtr=False
            device.rts=False
            device.close()
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
