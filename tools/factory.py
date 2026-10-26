"""Per-device manufacturing records, network request and real onboarding QR.

Output contains secrets and must be outside tracked sources (normally local/).
The bootstrap image contains only per-device AP/auth data. An authenticated HTTP
POST of factory.bin provisions the Matter identity separately before commissioning.
Test PKI is individual and local; production requires supplied certified inputs.
"""
import argparse
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import secrets
import struct
import subprocess
import zlib


def record(data):
    return data + struct.pack('<I', zlib.crc32(data))


def pin_valid(pin):
    return 0 < pin < 99999999 and pin not in (12345678, 87654321) and all(pin != n * 11111111 for n in range(1, 9))


def onboarding(pin, discriminator, vendor, product):
    if not pin_valid(pin) or not 0 <= discriminator <= 4095:
        raise ValueError('Invalid onboarding data')
    value, shift = 0, 0
    for field, bits in ((0, 3), (vendor, 16), (product, 16), (0, 2), (2, 8), (discriminator, 12), (pin, 27), (0, 4)):
        value |= field << shift
        shift += bits
    data = value.to_bytes(11, 'little')
    alphabet, encoded = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.', 'MT:'
    for offset in range(0, len(data), 3):
        chunk = data[offset:offset+3]
        n = int.from_bytes(chunk, 'little')
        for _ in range({1: 2, 2: 4, 3: 5}[len(chunk)]):
            encoded += alphabet[n % 38]
            n //= 38
    short = discriminator >> 8
    digits = f'{short >> 2}{((short & 3) << 14) | (pin & 0x3fff):05}{pin >> 14:04}'
    # Verhoeff's dihedral permutation; append one check digit to the 10 digits.
    d = ((0,1,2,3,4,5,6,7,8,9),(1,2,3,4,0,6,7,8,9,5),(2,3,4,0,1,7,8,9,5,6),(3,4,0,1,2,8,9,5,6,7),(4,0,1,2,3,9,5,6,7,8),(5,9,8,7,6,0,4,3,2,1),(6,5,9,8,7,1,0,4,3,2),(7,6,5,9,8,2,1,0,4,3),(8,7,6,5,9,3,2,1,0,4),(9,8,7,6,5,4,3,2,1,0))
    p = ((0,1,2,3,4,5,6,7,8,9),(1,5,7,6,2,8,3,0,9,4),(5,8,0,3,7,9,6,1,4,2),(8,9,1,6,0,4,3,5,2,7),(9,4,5,3,1,2,6,8,7,0),(4,2,8,6,5,7,3,9,0,1),(2,7,9,3,8,0,6,4,1,5),(7,0,4,6,9,1,3,2,5,8))
    inverse, c = (0,4,3,2,1,5,6,7,8,9), 0
    for i, number in enumerate(reversed(digits)):
        c = d[c][p[(i+1) % 8][int(number)]]
    return encoded, digits + str(inverse[c])


def test_identity(out, serial, vendor, product, test_cd):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID, ObjectIdentifier
    if vendor != 0xfff1 or product != 0x8000:
        raise ValueError('Test PKI is restricted to VID/PID FFF1/8000')
    now = datetime.now(timezone.utc)
    keys, certs = {}, {}
    for name, issuer, ca in [('paa', None, True), ('pai', 'paa', True), ('dac', 'pai', False)]:
        key = keys[name] = ec.generate_private_key(ec.SECP256R1())
        subject = [x509.NameAttribute(NameOID.COMMON_NAME, 'Matter C Bulb TEST ' + name.upper() + (' ' + serial if name == 'dac' else '')),
                   x509.NameAttribute(ObjectIdentifier('1.3.6.1.4.1.37244.2.1'), f'{vendor:04X}')]
        if name == 'dac':
            subject.append(x509.NameAttribute(ObjectIdentifier('1.3.6.1.4.1.37244.2.2'), f'{product:04X}'))
        subject = x509.Name(subject)
        issuer_name = certs[issuer].subject if issuer else subject
        builder = x509.CertificateBuilder().subject_name(subject).issuer_name(issuer_name).public_key(key.public_key()).serial_number(secrets.randbits(120) | 1).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=3650))
        builder = builder.add_extension(x509.BasicConstraints(ca, (1 if name == 'paa' else 0) if ca else None), critical=True)
        builder = builder.add_extension(x509.KeyUsage(not ca, False, False, False, False, ca, ca, None, None), critical=True)
        builder = builder.add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
        if issuer:
            builder = builder.add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(keys[issuer].public_key()), critical=False)
        certs[name] = builder.sign(keys[issuer] if issuer else key, hashes.SHA256())
        (out / (name + '.der')).write_bytes(certs[name].public_bytes(serialization.Encoding.DER))
        (out / (name + '-key.pem')).write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    (out / 'dac-private.bin').write_bytes(keys['dac'].private_numbers().private_value.to_bytes(32, 'big'))
    # Use the caller's official development CD, which chip-tool can verify.
    # The library's synthetic unit-test CD has an intentionally separate signer.
    cd = test_cd.read_bytes()
    if not 1 <= len(cd) <= 853:
        raise ValueError('Expected bounded official development CD for FFF1/8000')
    (out / 'cd.der').write_bytes(cd)


def render(qr_tool, payload, manual, serial, out):
    from PIL import Image, ImageDraw
    lines = subprocess.check_output([str(qr_tool)], input=payload+'\n', text=True).splitlines()
    size = int(lines[0]); assert len(lines) == size+1 and all(len(row) == size for row in lines[1:])
    scale, quiet = 10, 4
    width = (size+quiet*2)*scale
    image = Image.new('RGB', (width, width+54), 'white'); draw = ImageDraw.Draw(image)
    for y, row in enumerate(lines[1:]):
        for x, bit in enumerate(row):
            if bit == '1':
                draw.rectangle(((x+quiet)*scale,(y+quiet)*scale,(x+quiet+1)*scale-1,(y+quiet+1)*scale-1), fill='black')
    draw.text((20,width+5),'Matter RGB  '+serial,fill='black')
    draw.text((20,width+28),manual[:4]+'-'+manual[4:7]+'-'+manual[7:],fill='black')
    image.save(out/'onboarding.png')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mac', required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--vendor', type=lambda s:int(s,0), default=0xfff1)
    p.add_argument('--product', type=lambda s:int(s,0), default=0x8000)
    p.add_argument('--identity', type=Path, help='Private directory containing dac/pai/paa/cd.der and dac-private.bin')
    p.add_argument('--test-identity', action='store_true')
    p.add_argument('--test-cd', type=Path, help='Official development CD authorizing FFF1/8000; required for test identity')
    p.add_argument('--qr-tool', type=Path, required=True)
    a = p.parse_args()
    if bool(a.identity) == a.test_identity:
        p.error('Select exactly one of --test-identity or --identity')
    if a.test_identity and not a.test_cd:
        p.error('--test-identity requires --test-cd (official development declaration)')
    if not 1 <= a.vendor <= 0xfff4 or not 1 <= a.product <= 65535:
        p.error('Invalid VID/PID')
    for value in (a.serial,):
        if not 1 <= len(value.encode('ascii')) <= 32 or any(ord(c)<32 or ord(c)>126 for c in value):
            p.error('Serial must be 1..32 printable ASCII bytes')
    mac = bytes.fromhex(a.mac.replace(':','').replace('-',''))
    if len(mac) != 6 or mac[0]&1:
        p.error('Expected unicast device MAC')
    out = a.output.resolve()
    repo = Path(__file__).resolve().parents[1]
    if out.is_relative_to(repo) and not any(out.is_relative_to(repo/x) for x in ('local','build','output')):
        p.error('Secret output must be in ignored local/build/output or outside the repository')
    out.mkdir(parents=True, exist_ok=False)
    if a.test_identity:
        test_identity(out,a.serial,a.vendor,a.product,a.test_cd.resolve())
    identity = out if a.test_identity else a.identity.resolve()
    token = secrets.token_bytes(32)
    password = ''.join(secrets.choice('ABCDEFGHJKLMNPQRSTUVWXYZ23456789') for _ in range(16))
    bootstrap = record(b'MBOOT001'+mac+token+password.encode('ascii'))
    assert len(bootstrap)==66
    (out/'bootstrap.bin').write_bytes(bootstrap)
    (out/'bootstrap-nvs.csv').write_text('key,type,encoding,value\nbootstrap,namespace,,\nrecord,data,hex2bin,'+bootstrap.hex()+'\n')
    while True:
        pin = secrets.randbelow(99999998)+1
        if pin_valid(pin): break
    discriminator = secrets.randbelow(4096)
    strings = [a.serial.encode('ascii'),b'Matter C',b'Wi-Fi RGB bulb']
    certs = [(identity/(name+'.der')).read_bytes() for name in ('dac','pai','paa','cd')]
    key = (identity/'dac-private.bin').read_bytes()
    if len(key)!=32 or not any(key) or any(not c or len(c)>(853 if i==3 else 600) for i,c in enumerate(certs)):
        raise ValueError('Identity exceeds the existing Matter attestation bounds')
    factory = record(struct.pack('<8sHHHHI3B4H32s', b'MCBULB01', a.vendor,a.product,discriminator,1,pin,*map(len,strings),*map(len,certs),key)+b''.join(strings)+b''.join(certs))
    assert len(factory)<=4096
    (out/'factory.bin').write_bytes(factory)
    # BLE attestation needs the identity before first commissioning. Keep the
    # established partition/record formats; initial flash supplies both blobs.
    (out/'bootstrap-nvs.csv').write_text('key,type,encoding,value\nbootstrap,namespace,,\nrecord,data,hex2bin,'+bootstrap.hex()+'\nidentity,namespace,,\nrecord,data,hex2bin,'+factory.hex()+'\n')
    payload, manual = onboarding(pin,discriminator,a.vendor,a.product)
    manifest = dict(schema=1,serial=a.serial,mac=mac.hex(':'),vendor=a.vendor,product=a.product,
                    discriminator=discriminator,passcode=pin,qr=payload,manual_code=manual,
                    ap_ssid='Matter-Bulb-'+mac[-3:].hex().upper(),ap_password=password,
                    http_bearer=token.hex(),test_identity=a.test_identity)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    render(a.qr_tool.resolve(),payload,manual,a.serial,out)
    print('Created private per-device bootstrap, identity request and QR; secrets were not printed')


if __name__ == '__main__':
    main()
