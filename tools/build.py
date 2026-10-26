"""Build a separate develop/release configuration; never flash implicitly."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import sys

def main():
    root = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('variant', choices=('develop', 'release'))
    p.add_argument('--version', type=int, default=1)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--fresh-config', action='store_true', help='Regenerate this variant SDK configuration from defaults; save the previous generated file')
    p.add_argument('--idf-driver', type=Path, default=root.parent/'.tools/idf-v6.1.py')
    p.add_argument('--stack', type=Path, default=root/'matter')
    p.add_argument('--mdns', type=Path, help='Offline pinned mdns 1.14.0 component; otherwise use component manager')
    a = p.parse_args()
    if not 1 <= a.version <= 0xffffffff or not 1 <= a.jobs <= 16:
        p.error('Version or jobs out of range')
    if not (root/'local/ota-signing-key.pem').is_file():
        p.error('Provision local/ota-signing-key.pem (RSA3072); preserve it for future signed OTA')
    config = root/'build'/(a.variant+'-sdkconfig')
    if a.fresh_config and config.exists():
        shutil.copy2(config,config.with_suffix('.previous'))
        config.unlink()
    env = dict(os.environ)
    env['IDF_PY_BUILD_JOBS'] = str(a.jobs)
    args = [sys.executable, str(a.idf_driver.resolve()), '-B', 'build/'+a.variant,
            '-D', 'SDKCONFIG='+config.as_posix(), '-D', 'SDKCONFIG_DEFAULTS=config/base.defaults;config/'+a.variant+'.defaults',
            '-D', 'MATTER_BULB_STACK_DIR='+a.stack.resolve().as_posix(), '-D', 'PROJECT_VER='+str(a.version)]
    if a.mdns:
        env['IDF_COMPONENT_MANAGER'] = '0'
        args += ['-D', 'BULB_MDNS_DIR='+a.mdns.resolve().as_posix()]
    return subprocess.run([*args, 'reconfigure', 'build'], cwd=root, env=env).returncode

if __name__ == '__main__':
    raise SystemExit(main())
