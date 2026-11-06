"""Build the private, committed runtime fork with an explicit bridge policy; preserve R10 and the old runtime."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--variant');parser.add_argument('--padded-rows',choices=['alias-writes','shadow'],default='alias-writes');args=parser.parse_args()
assert args.variant is None or re.fullmatch('[a-z0-9-]+',args.variant)
suffix='-'+args.variant if args.variant else ''
repo=Path('/home/scion/scion-star2lte');workspace=Path('/path/to/development-workspace')
area=repo/'xnu-deps/build/panvk-native';fork=area/'fb-fetch-repo';old=area/'metal2vulkan-build';build=area/'fb-fetch-build'
assert subprocess.check_output(['git','-C',str(fork),'branch','--show-current'],text=True).strip()=='development'
assert not subprocess.check_output(['git','-C',str(fork),'status','--porcelain'],text=True).strip()
build.mkdir(exist_ok=True)
if not (build/'src').exists():(build/'src').symlink_to(fork,target_is_directory=True)
if not (build/'bridge').exists():shutil.copytree(old/'bridge',build/'bridge')
# Select the existing public policy only for this separately packaged runtime build.
# The runtime restores exact ordering for its admitted subset, and refuses other draw shapes.
for name in ['NativeMetalObjects.rs','NativeMetalBridge.rs']:
    f=build/'bridge'/name;s=f.read_text();needle='let mut options = DeviceOptions::default();'
    assert s.count(needle)==1
    if 'options.policy.framebuffer_fetch' not in s:
        s=s.replace(needle,needle+'\n        options.policy.framebuffer_fetch = metal2vulkan::device::FramebufferFetchPolicy::AcceptUnordered;')
    if name=='NativeMetalObjects.rs':
        policy='Shadow' if args.padded_rows=='shadow' else 'AliasWrites'
        s,count=re.subn(r'options\.policy\.padded_reads = metal2vulkan::device::PaddedReadsPolicy::\w+;',
            'options.policy.padded_reads = metal2vulkan::device::PaddedReadsPolicy::'+policy+';',s)
        assert count==1
    f.write_text(s)
sdk='/home/scion/theos/sdks/iPhoneOS16.5.sdk';cc=old/'ios-clang';cxx=old/'ios-clang++';rust=area/'rust'
env=dict(os.environ,RUSTUP_HOME=str(rust/'rustup'),CARGO_HOME=str(rust/'cargo'),RUSTUP_TOOLCHAIN='1.98.1',
    CARGO_BUILD_JOBS='4',CARGO_TARGET_DIR=str(old/'target-ios'),CARGO_TARGET_AARCH64_APPLE_IOS_LINKER=str(cc),
    SDKROOT=sdk,IPHONEOS_DEPLOYMENT_TARGET='16.5',CC_aarch64_apple_ios=str(cc),CXX_aarch64_apple_ios=str(cxx),
    M2V_RUNTIME_ID='scion-native-mali-serialized-fetch-'+subprocess.check_output(['git','-C',str(fork),'rev-parse','--short','HEAD'],text=True).strip()+'-padded-'+args.padded_rows,
    M2V_INSTALL_NAME='/redwood/lib/libmetal2vulkan.dylib',M2V_FIXTURE_AIR=str(old/'runtime-examples.bc'))
env['PATH']=str(rust/'cargo/bin')+':'+env['PATH'];env.pop('M2V_FC_AIR',None)
log=build/('build-ios'+suffix+'.log');print('Cross-building committed primitive-serialized Metal runtime',flush=True)
with log.open('w') as output:
    result=subprocess.run([str(rust/'cargo/bin/cargo'),'build','--locked','--release','--target','aarch64-apple-ios'],cwd=build/'bridge',env=env,stdout=output,stderr=subprocess.STDOUT)
print('\n'.join(log.read_text().splitlines()[-60:]),flush=True)
assert result.returncode==0
binary=old/'target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'
dest=workspace/('artifacts/star2lte-native-vulkan/libmetal2vulkan-serialized-fetch'+suffix+'.dylib');shutil.copyfile(binary,dest)
report=dict(source_commit=subprocess.check_output(['git','-C',str(fork),'rev-parse','HEAD'],text=True).strip(),
    binary_sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),binary_bytes=dest.stat().st_size,
    compile_policy='AcceptUnordered; bounded exact primitive serialization in runtime',padded_rows_policy=args.padded_rows,
    hardware_executed=False,stable_60fps_verified=False,log=str(log))
(workspace/('artifacts/star2lte-native-vulkan/metal-runtime-serialized-fetch-build'+suffix+'.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
