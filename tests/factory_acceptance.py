"""Physical factory-AP rejection tests; never prints onboarding/auth/key data."""
import argparse
import json
from pathlib import Path
import struct
import urllib.error
import urllib.request
import zlib

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--device',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();manifest=json.loads((a.device/'manifest.json').read_text());token=manifest['http_bearer']
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}));checks=[]
    def request(path,body=None,auth=token):
        headers={'Content-Type':'application/octet-stream'}
        if auth is not None:headers['Authorization']='Bearer '+auth
        try:
            with opener.open(urllib.request.Request('http://192.168.4.1/api/v1'+path,data=body,headers=headers),timeout=20) as r:return r.status,json.loads(r.read(2048))
        except urllib.error.HTTPError as error:return error.code,json.loads(error.read(2048))
    def check(name,test):
        checks.append({'name':name,'passed':bool(test)});a.output.write_text(json.dumps(checks,indent=2)+'\n')
        assert test,name
    status,before=request('/status');check('authenticated-status',status==200 and before['identity'])
    check('missing-token',request('/status',auth=None)[0]==403)
    check('wrong-token',request('/status',auth='0'*64)[0]==403)
    check('oversize-before-body-allocation',request('/factory/identity',b'x'*4097)[0]==413)
    body=(a.device/'factory.bin').read_bytes()
    if before['fabrics']:
        check('commissioned-identity-replacement-rejected',request('/factory/identity',body)[0]==409)
    else:
        forged=bytearray(body);forged[31]^=1;forged[-4:]=struct.pack('<I',zlib.crc32(forged[:-4]))
        check('valid-CRC-wrong-DAC-key',request('/factory/identity',bytes(forged))[0]==400)
        check('identity-invalid-CRC',request('/factory/identity',body[:-1]+bytes([body[-1]^1]))[0]==400)
    check('invalid-WiFi-body',request('/wifi',b'MWIFI001\0'*2)[0]==400)
    status,after=request('/status');check('rejections-preserve-identity-fabrics-and-version',status==200 and all(after[k]==before[k] for k in ('identity','fabrics','version')))
    print('PASS factory API: '+str(len(checks))+' rejection/state checks')

if __name__=='__main__':main()
