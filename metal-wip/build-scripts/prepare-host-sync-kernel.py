"""Reconstruct the tested kernel revision locally, with only the host-sync ABI added."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile

repo=Path('/home/scion/scion-star2lte')
workspace=Path('/path/to/development-workspace')
baseline='67297be6c313df34732d9757db55f692f6af3c3f'
out=repo/'build/native-host-sync-kernel'
assert not out.exists()
core=out/'core'
subprocess.run(['git','clone','--shared','--no-checkout',str(repo),str(core)],check=True)
subprocess.run(['git','-C',str(core),'checkout','--detach',baseline],check=True)
original=(core/'drivers/gpu/hakios.mali.cpp').read_text()
edited=(workspace/'sources/HakiOS-Scion/drivers/gpu/hakios.mali.cpp').read_text()
candidate=original
needle='#include "MiniIOSurface.h"\n';assert candidate.count(needle)==1
candidate=candidate.replace(needle,needle+'#include "hakios.mali_host_sync.h"\n',1)
needle='\t\tstruct mali_surface_seq sseq;\n';assert candidate.count(needle)==1
candidate=candidate.replace(needle,needle+'\t\tstruct hakios_mali_host_sync hostSync;\n',1)
guard=edited[edited.index('\tif (selector == HAKIOS_MALI_HOST_SYNC_SELECTOR &&'):edited.index('\tbzero(&r, sizeof(r));',edited.index('\tif (selector == HAKIOS_MALI_HOST_SYNC_SELECTOR &&'))]
needle='\tbzero(&r, sizeof(r));';assert candidate.count(needle)==1
candidate=candidate.replace(needle,guard+needle,1)
block=edited[edited.index('\tcase HAKIOS_MALI_HOST_SYNC_SELECTOR: {'):edited.index('\tcase kPanfrostGetParam:',edited.index('\tcase HAKIOS_MALI_HOST_SYNC_SELECTOR: {'))]
needle='\tcase kPanfrostGetParam:';assert candidate.count(needle)==1
candidate=candidate.replace(needle,block+needle,1)
(core/'drivers/gpu/hakios.mali.cpp').write_text(candidate)
header=workspace/'sources/HakiOS-Scion/drivers/gpu/hakios.mali_host_sync.h'
shutil.copyfile(header,core/'drivers/gpu/hakios.mali_host_sync.h')
mapping=core/'drivers/map';text=mapping.read_text()
assert 'hakios.mali_host_sync.h' not in text
mapping.write_text(text+'\ngpu      iokit/Kernel/hakios.mali_host_sync.h\n')
test=workspace/'tools/mesa/panvk-native/test-mali-host-sync.c'
with tempfile.TemporaryDirectory(prefix='scion-host-sync-') as temporary:
    binary=Path(temporary)/'check'
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(header.parent),str(test),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    # The same declaration must compile in the kernel's C++ dialect too.
    subprocess.run(['c++','-x','c++','-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only',str(header)],check=True)
changes=subprocess.check_output(['git','-C',str(core),'diff','--name-only'],text=True).splitlines()
assert changes==['drivers/gpu/hakios.mali.cpp','drivers/map']
subprocess.run(['git','-C',str(core),'checkout','-b','port/star2lte-host-sync'],check=True)
subprocess.run(['git','-C',str(core),'add','drivers/gpu/hakios.mali.cpp','drivers/gpu/hakios.mali_host_sync.h','drivers/map'],check=True)
subprocess.run(['git','-C',str(core),'-c','user.name=Codex','-c','user.email=codex@localhost','commit',
    '-m','Add bounded PoC cache synchronization for owned Mali surface buffers',
    '-m','Use a versioned request for one kernel-side clean/invalidate per range. Validate task ownership, exact input size and allocation bounds. Preserve the tested kernel base and all existing paths. Host ABI/bounds/overflow checks pass with ASAN and UBSAN; hardware validation pending.'],check=True)
config=workspace/'artifacts/star2lte-gpu-integer/config'
shutil.copyfile(config,core/'.config')
for name in ['host','sdk','build']:
    target=core/'xnu-deps'/name
    assert not target.exists()
    target.symlink_to(repo/'xnu-deps'/name,target_is_directory=True)
# Device/SoC checkouts live outside the core's tracked files. Read the existing,
# unchanged repositories through links; no source or branch there is modified.
for category in ['devices','socs']:
    for source in (repo/category).iterdir():
        target=core/category/source.name
        if source.is_dir() and (not target.exists() or (target.is_dir() and not any(target.iterdir()))):
            if target.exists():target.rmdir() # verified empty directory inside this new checkout
            target.symlink_to(source,target_is_directory=True)
env=dict(os.environ,THEOS='/home/scion/theos',XNU_URL='file://'+str(repo/'xnu-src/xnu-8796.121.2'),
    HFS_SRC=str(repo/'xnu-src/hfs-627.100.6'))
log=out/'prepare.log'
with log.open('w') as output:
    result=subprocess.run(['bash','xnu/apply.sh'],cwd=core,env=env,stdout=output,stderr=subprocess.STDOUT)
print('\n'.join(log.read_text().splitlines()[-15:]))
assert result.returncode==0
diff=subprocess.check_output(['git','-C',str(core),'diff',baseline,'--','drivers/gpu/hakios.mali.cpp','drivers/gpu/hakios.mali_host_sync.h','drivers/map'],text=True)
(out/'source.patch').write_text(diff)
report=dict(base_revision=baseline,source_revision=subprocess.check_output(['git','-C',str(core),'rev-parse','HEAD'],text=True).strip(),
    source_patch_sha256=hashlib.sha256(diff.encode()).hexdigest(),config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),
    original_checkout_modified=False,fresh_xnu_from_local_apple_tag=True,host_range_asan_ubsan_passed=True,
    files_changed=['drivers/gpu/hakios.mali.cpp','drivers/gpu/hakios.mali_host_sync.h','drivers/map'],hardware_tested=False)
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
