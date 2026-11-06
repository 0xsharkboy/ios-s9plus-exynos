"""Run the exact installer in an isolated filesystem, using real BOOT assembly."""
from pathlib import Path
import argparse,hashlib,json,os,shlex,shutil,subprocess,tempfile,zipfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--stock-boot',required=True,type=Path)
parser.add_argument('--zip',required=True,type=Path)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
profile=json.loads((repo/'profiles/installed-gles.json').read_text())
original=args.stock_boot.resolve()
assert hashlib.sha256(original.read_bytes()).hexdigest()==profile['original_boot_sha256']
script=(repo/'installer/update-binary').read_text()
subprocess.run(['sh','-n',str(repo/'installer/update-binary')],check=True)
checks=[]
with tempfile.TemporaryDirectory(prefix='scion-publication-test-') as temporary:
    root=Path(temporary);helpers=root/'bin';helpers.mkdir()
    boot=root/'BOOT';system=root/'SYSTEM';recovery=root/'RECOVERY';mac_file=root/'device-mac.info'
    text=script
    for name,path in [('BOOT',boot),('SYSTEM',system),('RECOVERY',recovery)]:
        text=text.replace('/dev/block/platform/11120000.ufs/by-name/'+name,str(path))
    text=text.replace('/tmp/star2lte-installed-gles-$$',str(root)+'/stage-$$')
    text=text.replace('/efs/wifi/.mac.info',str(mac_file))
    installer=root/'installer.sh';installer.write_text(text)
    programs={
      'getprop':'''#!/bin/sh
case "$1" in
ro.product.device) echo "${SCION_TEST_DEVICE:-star2lte}";;
ro.bootloader) echo G965FXXUHFVG4;;
ro.twrp.version) echo 3.7.0_9-0;;
esac
''',
      'blockdev':'#!/bin/sh\nstat -c %s "$2"\n',
      'unzip':'''#!/usr/bin/python3
import sys,zipfile
assert sys.argv[1]=='-p'
with zipfile.ZipFile(sys.argv[2]) as archive:
 sys.stdout.buffer.write(archive.read(sys.argv[3]))
''',
      'sha256sum':'''#!/usr/bin/python3
from pathlib import Path
import hashlib,sys
p=Path(sys.argv[1])
if p==Path(SYSTEM_PATH) and p.read_bytes()==b'ios':digest=SYSTEM_HASH
else:
 h=hashlib.sha256()
 with p.open('rb') as f:
  while chunk:=f.read(1024*1024):h.update(chunk)
 digest=h.hexdigest()
print(digest+'  '+str(p))
'''.replace('SYSTEM_PATH',repr(str(system))).replace('SYSTEM_HASH',repr(profile['full_system_sha256'])),
      'dd':'''#!/usr/bin/python3
from pathlib import Path
import os,sys
marker=Path(MARKER)
if os.environ.get('SCION_TEST_DD_FAIL')=='1' and 'of='+BOOT_PATH in sys.argv and not marker.exists():
 marker.write_text('injected failure')
 with open(BOOT_PATH,'r+b') as f:f.write(b'failed write')
 sys.exit(1)
if os.environ.get('SCION_TEST_DD_FAIL')=='2' and 'of='+BOOT_PATH in sys.argv:
 with open(BOOT_PATH,'r+b') as f:f.write(b'failed write')
 sys.exit(1)
os.execv('/usr/bin/dd',['dd',*sys.argv[1:]])
'''.replace('MARKER',repr(str(root/'failed-once'))).replace('BOOT_PATH',repr(str(boot)))
    }
    for name,contents in programs.items():
        file=helpers/name;file.write_text(contents);file.chmod(0o755)
    environment=dict(os.environ,PATH=str(helpers)+':/usr/bin:/bin')
    archive=args.zip.resolve()
    fixture_mac=b'02:11:22:33:44:55\n'
    with zipfile.ZipFile(archive) as zipped:template=zipped.read('boot-prefix.template')
    offset=profile['mac_offset']
    fixture_prefix=template[:offset]+fixture_mac+template[offset+18:]
    fixture_full=fixture_prefix+original.read_bytes()[len(template):]
    fixture_digest=hashlib.sha256(fixture_full).hexdigest()
    def run(device='star2lte',system_ok=True,write_failure=False,mac_ok=True):
        shutil.copyfile(original,boot);system.write_bytes(b'ios' if system_ok else b'other')
        if mac_ok is None:
            if mac_file.exists():mac_file.unlink()
        else:mac_file.write_bytes(fixture_mac if mac_ok else b'invalid')
        recovery.write_bytes(b'authored recovery fixture stays unchanged')
        before=recovery.read_bytes()
        env=dict(environment,SCION_TEST_DEVICE=device,SCION_TEST_DD_FAIL=str(int(write_failure)))
        result=subprocess.run(['sh',str(installer),'3','1',str(archive)],env=env,capture_output=True,text=True,timeout=120)
        assert recovery.read_bytes()==before
        return result,hashlib.sha256(boot.read_bytes()).hexdigest()
    for name,parameters in [('wrong target',dict(device='star2qlte')),('wrong SYSTEM',dict(system_ok=False)),
                            ('missing original MAC',dict(mac_ok=None)),
                            ('invalid original MAC',dict(mac_ok=False)),
                            ('write failure rollback',dict(write_failure=True))]:
        result,digest=run(**parameters)
        assert result.returncode!=0 and digest==profile['original_boot_sha256'],(name,result.stdout,result.stderr)
        if write_failure:=parameters.get('write_failure'):
            assert 'BOOT write failed' in result.stdout and 'Restoring the previous BOOT' in result.stdout
            assert (root/'failed-once').is_file()
        checks.append(name)
    result,digest=run()
    assert result.returncode==0 and digest==fixture_digest,(result.stdout,result.stderr)
    assert boot.read_bytes()==fixture_full
    checks.append('verified template plus device-local MAC installed and RECOVERY unchanged')
    result=subprocess.run(['sh',str(installer),'3','1',str(archive)],env=environment,capture_output=True,text=True,timeout=120)
    assert result.returncode==0 and 'already present' in result.stdout
    checks.append('already installed produces no write')
    result,digest=run(write_failure=2)
    assert result.returncode!=0 and 'RAM backup retained' in result.stdout
    retained=list(root.glob('stage-*/before.img'))
    assert len(retained)==1 and hashlib.sha256(retained[0].read_bytes()).hexdigest()==profile['original_boot_sha256']
    checks.append('failed rollback retains verified RAM backup for manual recovery')
report=dict(shell_syntax_passed=True,tests=checks,real_prefix_and_stock_tail_assembled=True,
    fake_block_devices_only=True,actual_phone_writes=False,host_zip_extractor='Python zipfile (-p equivalent)',
    canonical_template_sha256=profile['template_full_boot_sha256'],fixture_boot_sha256=fixture_digest)
print(json.dumps(report,indent=2))
