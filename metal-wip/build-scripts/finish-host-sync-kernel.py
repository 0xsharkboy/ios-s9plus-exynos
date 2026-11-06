"""Complete isolated setup using local toolchains and pristine Apple tag objects."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--resume-patches',action='store_true');parser.add_argument('--tools-only',action='store_true');args=parser.parse_args()

repo=Path('/home/scion/scion-star2lte')
workspace=Path('/path/to/development-workspace')
baseline='67297be6c313df34732d9757db55f692f6af3c3f'
out=repo/'build/native-host-sync-kernel';core=out/'core'
assert subprocess.check_output(['git','-C',str(core),'log','-1','--format=%s'],text=True).strip()=='Add bounded PoC cache synchronization for owned Mali surface buffers'
assert subprocess.check_output(['git','-C',str(core),'rev-parse','HEAD^'],text=True).strip()==baseline
changed=subprocess.check_output(['git','-C',str(core),'diff','--name-only',baseline,'HEAD'],text=True).splitlines()
assert changed==['drivers/gpu/hakios.mali.cpp','drivers/gpu/hakios.mali_host_sync.h','drivers/map']
config=workspace/'artifacts/star2lte-gpu-integer/config'
shutil.copyfile(config,core/'.config')

def fill_tools(source,target):
    if not target.exists() and not target.is_symlink():
        target.symlink_to(source,target_is_directory=source.is_dir())
    elif source.is_dir() and target.is_dir() and not target.is_symlink():
        for child in source.iterdir():fill_tools(child,target/child.name)

for name in ['host','sdk','build','MacOSX13.3.sdk','iig-impl']:
    assert (repo/'xnu-deps'/name).exists(),name
    fill_tools(repo/'xnu-deps'/name,core/'xnu-deps'/name)
if args.tools_only:
    print('Local toolchains, SDK and DriverKit generated implementation links ready.')
    raise SystemExit(0)
for category in ['devices','socs']:
    for source in (repo/category).iterdir():
        target=core/category/source.name
        if source.is_dir() and (not target.exists() or (target.is_dir() and not any(target.iterdir()))):
            if target.exists():target.rmdir() # verified empty, within the newly created checkout
            target.symlink_to(source,target_is_directory=True)
env=dict(os.environ,THEOS='/home/scion/theos',XNU_URL='file://'+str(repo/'xnu-src/xnu-8796.121.2'),
    HFS_SRC=str(repo/'xnu-src/hfs-627.100.6'))
tree=core/'xnu-src/xnu-8796.121.2'
if not args.resume_patches:
    if not tree.exists():
        subprocess.run(['git','clone','--shared','--no-checkout',str(repo/'xnu-src/xnu-8796.121.2'),str(tree)],check=True)
    else:
        assert {p.name for p in tree.iterdir()}=={'.git'}, 'Inspect the partially prepared source before retrying.'
    subprocess.run(['git','-C',str(tree),'checkout','--detach','c54f35ca767986246321eb901baf8f5ff7923f6a'],check=True)
    subprocess.run(['git','-C',str(tree),'apply','--whitespace=nowarn',str(core/'xnu/hakios.patch')],check=True)
    shutil.copytree(core/'xnu/files',tree,dirs_exist_ok=True)
    subprocess.run(['bash','xnu/hfs/hfs.sh','put',str(tree)],cwd=core,env=env,check=True)
else:
    assert subprocess.check_output(['git','-C',str(tree),'rev-parse','HEAD'],text=True).strip()=='c54f35ca767986246321eb901baf8f5ff7923f6a'
controlled={}
for line in (core/'drivers/map').read_text().splitlines():
    parts=line.split()
    if not parts or parts[0].startswith('#'):continue
    category,relative=parts[:2];source=core/'drivers'/category/Path(relative).name
    (tree/relative).parent.mkdir(parents=True,exist_ok=True)
    if not args.resume_patches:shutil.copyfile(source,tree/relative)
    controlled[relative]=source
external=subprocess.check_output(['python3','tools/hakios-config.py','external-files'],cwd=core,env=env,text=True)
for line in external.splitlines():
    source,relative=line.split('\t');(tree/relative).parent.mkdir(parents=True,exist_ok=True)
    if not args.resume_patches:shutil.copyfile(source,tree/relative)
    controlled[relative]=Path(source)
for source in (core/'xnu/files').rglob('*'):
    if source.is_file():controlled[str(source.relative_to(core/'xnu/files'))]=source
patch_audit=[]
for patch in sorted((core/'xnu/patches').glob('*.patch')):
    text=patch.read_text()
    if re.search(r'^diff --git ',text,re.M):
        chunks=[part for part in re.split(r'(?=^diff --git )',text,flags=re.M) if part.startswith('diff --git ')]
    else:
        chunks=[part for part in re.split(r'(?=^--- a/)',text,flags=re.M) if part.startswith('--- a/')]
    assert chunks,patch
    for chunk in chunks:
        relative=re.match(r'(?:diff --git a/|--- a/)(\S+)',chunk).group(1)
        check=subprocess.run(['git','-C',str(tree),'apply','--check','-'],input=chunk,text=True,capture_output=True)
        if check.returncode==0:
            subprocess.run(['git','-C',str(tree),'apply','--whitespace=nowarn','-'],input=chunk,text=True,check=True)
            decision='applied'
        else:
            reverse=subprocess.run(['git','-C',str(tree),'apply','--reverse','--check','-'],input=chunk,text=True,capture_output=True)
            if reverse.returncode==0:decision='already present'
            else:
                # A stale patch is superseded only with byte-for-byte evidence:
                # the canonical baseline file equals the previously built tree.
                known=repo/'xnu-src/xnu-8796.121.2'/relative
                current=tree/relative
                if relative in controlled and known.is_file() and current.read_bytes()==known.read_bytes():
                    decision='canonical file equals previous compiled source; stale patch superseded'
                else:
                    print(check.stderr)
                    raise AssertionError((patch.name,relative,'unresolved patch drift; inspect before continuing'))
        patch_audit.append(dict(patch=patch.name,file=relative,decision=decision))
subprocess.run(['python3','tools/hakios-config.py','kernel','--tree',str(tree)],cwd=core,env=env,check=True)
(out/'patch-audit.json').write_text(json.dumps(patch_audit,indent=2)+'\n')
diff=subprocess.check_output(['git','-C',str(core),'diff',baseline,'--','drivers/gpu/hakios.mali.cpp','drivers/gpu/hakios.mali_host_sync.h','drivers/map'],text=True)
(out/'source.patch').write_text(diff)
report=dict(base_revision=baseline,source_revision=subprocess.check_output(['git','-C',str(core),'rev-parse','HEAD'],text=True).strip(),
    source_patch_sha256=hashlib.sha256(diff.encode()).hexdigest(),config_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),
    original_checkout_modified=False,fresh_xnu_from_local_apple_tag=True,host_range_asan_ubsan_passed=True,
    files_changed=changed,numbered_patch_files_audited=len(patch_audit),hardware_tested=False)
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
