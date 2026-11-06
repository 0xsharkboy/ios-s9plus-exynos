"""Build and audit native-only FD import; never change the Panfrost directories."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--stage-only',action='store_true')
parser.add_argument('--label',default='')
args=parser.parse_args()

workspace=Path('/path/to/development-workspace')
repo=Path('/home/scion/scion-star2lte')
area=repo/'xnu-deps/build/panvk-native'
source=workspace/'tools/mesa/panvk-native'
shim=area/'native-src/src/hakios-drm'
if not args.stage_only:
    with tempfile.TemporaryDirectory(prefix='scion-surface-fd-') as temporary:
        exe=Path(temporary)/'check'
        subprocess.run(['cc','-std=c11','-D_GNU_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
            '-pthread','-I'+str(source),str(source/'hakios-native-surface-fd.c'),str(source/'test-native-surface-fd.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
for name in ['hakios-native-surface-fd.h','hakios-native-surface-fd.c']:
    (shim/name).write_bytes((source/name).read_bytes())
(shim/'hakios.mali_host_sync.h').write_bytes((workspace/'sources/HakiOS-Scion/drivers/gpu/hakios.mali_host_sync.h').read_bytes())
transport=shim/'hakios.drm.c'
text=transport.read_text().replace('#include "hakios-native-surface-fd.h"\n','')
begin=text.index('int\ndrmPrimeFDToHandle(')
end=text.index('\nstatic int\nnone(void)',begin)
replacement='''int
drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    uint32_t surface=0;uint64_t expected_size=0;
    int error=hakios_native_surface_fd_lookup(prime_fd,&surface,&expected_size);
    (void)fd;
    if(!handle){errno=EINVAL;return -1;}
    struct hakios_drm_surface request={.surface=surface,.handle=HAKIOS_GPU_DESCRIBE};
    if(!error)error=call(HAKIOS_GPU_SURFACE,&request,sizeof request);
    if(!error && request.size!=expected_size)error=EINVAL;
    if(!error){request.handle=0;error=call(HAKIOS_GPU_SURFACE,&request,sizeof request);}
    if(error){errno=error;return -1;}
    error=hakios_native_surface_fd_bind(prime_fd,request.handle);
    if(error){errno=error;return -1;}
    *handle=request.handle;return 0;
}
'''
text=text[:begin]+replacement+text[end:]
text=text.replace('#include "xf86drm.h"','#include "xf86drm.h"\n#include "hakios-native-surface-fd.h"',1)
# A registry token cannot keep referring to a retired/reused kernel handle.
needle='\tstruct drm_gem_close request = { .handle = handle };\n'
assert text.count(needle)==1
if 'hakios_native_surface_fd_forget(handle);' not in text:
    text=text.replace(needle,needle+'\thakios_native_surface_fd_forget(handle);\n',1)
if '#include "hakios-native-surface-cache.inc"' not in text:
    text+='\n#include "hakios-native-surface-cache.inc"\n'
(shim/'hakios-native-surface-cache.inc').write_bytes((source/'hakios-native-surface-cache.inc').read_bytes())
transport.write_text(text)
if args.stage_only:
    print('Staged deterministic native surface FD adapter; no compilation or hardware action.')
    raise SystemExit(0)
sdk='/home/scion/theos/sdks/iPhoneOS16.5.sdk'
common=['/usr/bin/clang','-target','arm64-apple-ios16.5','-isysroot',sdk,'-O2','-Wall','-Wextra','-Werror',
    '-I'+str(shim),'-I'+str(area/'native-src/include')]
for name,output in [('hakios.drm.c','hakios-native-drm.o'),('hakios-native-surface-fd.c','hakios-native-surface-fd.o')]:
    subprocess.run([*common,'-c',str(shim/name),'-o',str(area/output)],check=True)
subprocess.run(['llvm-ar','rcs',str(area/'libhakios-native-drm.a'),
    *[str(area/name) for name in ['hakios-native-drm.o','hakios-native-posix.o','hakios-native-sync.o','hakios-native-surface-fd.o']]],check=True)
modified=subprocess.check_output(['git','-C',str(area/'native-src'),'diff','--name-only'],text=True).splitlines()
assert not any(p.startswith(('src/panfrost/','src/gallium/drivers/panfrost/')) for p in modified)
report=dict(asan_ubsan_passed=True,strict_ios_compile_passed=True,
    sources={name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in ['hakios-native-surface-fd.c','hakios-native-surface-fd.h']},
    native_transport_sha256=hashlib.sha256(transport.read_bytes()).hexdigest(),
    panfrost_directories_unchanged=True,hardware_executed=False)
assert not args.label or __import__('re').fullmatch('[a-z0-9-]+',args.label)
suffix='-'+args.label if args.label else ''
(workspace/('artifacts/star2lte-native-vulkan/native-surface-fd-host-validation'+suffix+'.json')).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
