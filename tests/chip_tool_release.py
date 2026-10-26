"""Physical release liveness and optional own-fabric removal with stock chip-tool."""
import argparse
import json
from pathlib import Path
import re
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--chip-tool',type=Path,required=True);p.add_argument('--storage',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--node',type=int,default=22);p.add_argument('--version',type=int,required=True);p.add_argument('--remove-own-fabric',action='store_true')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);checks=[]
    def run(name,args,pattern):
        with (a.output/(name+'.log')).open('w') as log:r=subprocess.run([str(a.chip_tool),*map(str,args),'--storage-directory',str(a.storage),'--timeout','30'],stdout=log,stderr=subprocess.STDOUT,timeout=45)
        text=re.sub(r'\x1b\[[0-9;]*m','',(a.output/(name+'.log')).read_text());m=re.search(pattern,text)
        checks.append({'name':name,'passed':r.returncode==0 and m is not None});(a.output/'result.json').write_text(json.dumps(checks,indent=2)+'\n')
        assert checks[-1]['passed'],name
        print('PASS '+name,flush=True);return m
    run('version-after-silent-bootloader',['basicinformation','read','software-version',a.node,0],rf'SoftwareVersion: {a.version}\b')
    run('on',['onoff','on',a.node,1],r'Received Command Response Status')
    run('read-on',['onoff','read','on-off',a.node,1],r'OnOff: TRUE')
    if a.remove_own_fabric:
        fabric=int(run('own-fabric',['operationalcredentials','read','current-fabric-index',a.node,0],r'CurrentFabricIndex: (\d+)')[1])
        run('remove-own-fabric-response',['operationalcredentials','remove-fabric',fabric,a.node,0],r'statusCode: 0\b')

if __name__=='__main__':main()
