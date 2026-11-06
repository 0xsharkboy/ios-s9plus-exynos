from pathlib import Path
import hashlib,json,os,shutil,subprocess

repo=Path('/home/scion/scion-star2lte')
workspace=Path('/path/to/development-workspace')
out=repo/'build/native-host-sync-kernel';core=out/'core'
report=json.loads((out/'preparation.json').read_text())
assert report['host_range_asan_ubsan_passed']
assert not subprocess.check_output(['git','-C',str(core),'status','--porcelain','--untracked-files=no'],text=True).strip()
env=dict(os.environ,THEOS='/home/scion/theos')
log=out/'build.log'
print('Building isolated kernel from tested base plus owned-buffer PoC range API.',flush=True)
with log.open('w') as output:
    result=subprocess.run(['make','SHELL=/bin/bash','JOBS=4','kernel'],cwd=core,env=env,stdout=output,stderr=subprocess.STDOUT)
print('\n'.join(log.read_text().splitlines()[-35:]),flush=True)
assert result.returncode==0
kernel=core/'hakios-kernel'
data=kernel.read_bytes()
report.update(kernel_sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),build_exit_code=result.returncode)
dest=workspace/'artifacts/star2lte-gpu-host-sync'
dest.mkdir(exist_ok=False)
shutil.copyfile(kernel,dest/'hakios-kernel');shutil.copyfile(core/'.config',dest/'config')
shutil.copyfile(out/'source.patch',dest/'source.patch')
shutil.copyfile(log,dest/'build.log')
(dest/'kernel-build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
