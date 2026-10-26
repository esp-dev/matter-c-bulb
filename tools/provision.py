"""Authenticated connected-STA client. Never prints passwords/tokens/pairing data."""
import argparse
import json
from pathlib import Path
import struct
import http.client
import zlib
import ipaddress
from urllib.parse import urlsplit

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('status', 'identity', 'wifi', 'reset', 'ota-query'))
    p.add_argument('--device', type=Path, required=True, help='Private per-device factory output directory')
    p.add_argument('--url', required=True, help='Explicit local device URL, including IPv6 scope when needed')
    p.add_argument('--ssid')
    p.add_argument('--password-file', type=Path, help='Private UTF-8 password file (no command-line secret)')
    p.add_argument('--disable-wifi', action='store_true')
    a = p.parse_args()
    address=urlsplit(a.url)
    try:
        host=ipaddress.ip_address((address.hostname or '').split('%')[0]);local=host.is_private or host.is_link_local
    except ValueError:
        local=bool(address.hostname and address.hostname.endswith('.local'))
    if address.scheme!='http' or not local or address.username or address.password or address.path not in ('','/') or address.query or address.fragment:
        p.error('Expected an explicit local HTTP device URL, without credentials, path or query')
    a.url=a.url.rstrip('/')
    manifest = json.loads((a.device/'manifest.json').read_text())
    paths = dict(status='/status', identity='/factory/identity', wifi='/wifi', reset='/reset', **{'ota-query':'/ota/query'})
    body = None if a.action == 'status' else b''
    if a.action == 'identity':
        body = (a.device/'factory.bin').read_bytes()
    if a.action == 'wifi':
        ssid = (a.ssid or '').encode('utf-8')
        password = a.password_file.read_text(encoding='utf-8').rstrip('\r\n').encode('utf-8') if a.password_file else b''
        if not a.disable_wifi and not 1 <= len(ssid) <= 32:
            p.error('Wi-Fi requires a 1..32 byte SSID')
        if len(ssid)>32 or len(password)>64 or (password and len(password)<8):
            p.error('Network credentials exceed firmware constraints')
        body = b'MWIFI001'+struct.pack('BBB', not a.disable_wifi, len(ssid), len(password))+ssid+password
        body += struct.pack('<I', zlib.crc32(body))
    # Direct sockets ignore workstation proxy settings. Decode only the RFC6874
    # zone delimiter, not arbitrary percent escapes in an IPv6 interface index.
    connection=http.client.HTTPConnection(address.hostname.replace('%25','%',1),address.port or 80,timeout=20)
    try:
        connection.request('GET' if a.action=='status' else 'POST','/api/v1'+paths[a.action],body=body,
            headers={'Authorization':'Bearer '+manifest['http_bearer'], 'Content-Type':'application/octet-stream'})
        response=connection.getresponse()
        result = json.loads(response.read(2048))
        print(json.dumps(result, indent=2))
        if response.status>=400:raise SystemExit(1)
    finally:
        connection.close()

if __name__ == '__main__':
    main()
