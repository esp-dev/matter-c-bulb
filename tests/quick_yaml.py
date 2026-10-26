"""Run unchanged upstream YAML cases with the product PICS, against a paired DUT.

Engineering regression using CSA test-plan cases; not a certification campaign.
Requires a private chip-tool controller directory and an isolated Python venv
with the upstream matter-yamltests/IDL runner dependencies installed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--chip-tool',type=Path,required=True)
    p.add_argument('--storage',type=Path,required=True);p.add_argument('--pics',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--node',type=int,default=22)
    p.add_argument('--port',type=int,default=19002)
    p.add_argument('--cases',nargs='+',default=['OO_2_1','OO_2_2','LVL_2_1','LVL_2_2','CC_3_1','CC_4_1','CC_5_1','CC_6_1','CC_8_1'])
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    result={'scope':'unchanged upstream YAML on physical C6; engineering regression, not certification','sdk':subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.sdk,text=True).strip(),'pics_sha256':hashlib.sha256(a.pics.read_bytes()).hexdigest(),'cases':[]}
    for case in a.cases:
        if not re.fullmatch(r'[A-Z]+_[0-9]+_[0-9]+',case):p.error('Invalid case identifier')
        source=a.sdk/f'src/app/tests/suites/certification/Test_TC_{case}.yaml'
        committed=subprocess.check_output(['git','show','HEAD:'+source.relative_to(a.sdk).as_posix()],cwd=a.sdk)
        if source.read_bytes()!=committed:raise ValueError('Refusing a modified upstream test: '+case)
        command=[sys.executable,str(a.sdk/'scripts/tests/chipyaml/runner.py'),'--PICS',str(a.pics),'--nodeId',str(a.node),'--endpoint','1','Test_TC_'+case,'chiptool','--server_path',str(a.chip_tool),'--server_port',str(a.port),'--server_arguments',shlex.join(['interactive','server','--port',str(a.port),'--storage-directory',str(a.storage)])]
        started=time.monotonic();entry={'id':'TC-'+case.replace('_','-',1).replace('_','.'),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
        try:
            with (a.output/(case+'.log')).open('w') as log:completed=subprocess.run(command,cwd=a.sdk,stdout=log,stderr=subprocess.STDOUT,timeout=240,stdin=subprocess.DEVNULL)
            text=re.sub(r'\x1b\[[0-9;]*m','',(a.output/(case+'.log')).read_text())
            checks=re.search(r'Test finished in \d+ms with (\d+) success, (\d+) errors and (\d+) warnings',text)
            steps=re.search(r'Run finished in \d+ms with (\d+) steps runned and (\d+) steps skipped',text)
            entry.update(exit_code=completed.returncode,passed=completed.returncode==0 and checks is not None and int(checks[1])>0 and checks[2]=='0' and checks[3]=='0' and steps is not None and int(steps[1])>0)
            if checks:entry.update(checks=int(checks[1]),errors=int(checks[2]),warnings=int(checks[3]))
            if steps:entry.update(steps=int(steps[1]),skipped=int(steps[2]))
        except subprocess.TimeoutExpired:entry.update(passed=False,error='240s case deadline')
        entry['seconds']=round(time.monotonic()-started,2);result['cases'].append(entry)
        (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(('PASS ' if entry['passed'] else 'FAIL ')+entry['id']+' '+str(entry['seconds'])+'s',flush=True)
    return 0 if all(c['passed'] for c in result['cases']) else 1

if __name__=='__main__':raise SystemExit(main())
