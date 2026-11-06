"""Exercise the actual FD-to-kernel import adapter with a deterministic service."""
from pathlib import Path
import subprocess,tempfile

root=Path('/path/to/development-workspace')
area=Path('/home/scion/scion-star2lte/xnu-deps/build/panvk-native')
transport=(area/'native-src/src/hakios-drm/hakios.drm.c').read_text()
adapter=transport[transport.index('int\ndrmPrimeFDToHandle('):transport.index('\nstatic int\nnone(void)')]
original=(root/'sources/HakiOS-Scion/tools/mesa/hakios-drm/xf86drm.h').read_text()
begin=original.index('struct hakios_drm_surface {')
layout=original[begin:original.index('\n};',begin)+3]
test='''#include "hakios-native-surface-fd.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#define HAKIOS_GPU_SURFACE 0x7e
#define HAKIOS_GPU_DESCRIBE 0xffffffffu
'''+layout+'''
static unsigned calls;static int refuse;static uint64_t described_size=16384;
static int call(unsigned selector,void *data,size_t size) {
    assert(selector==HAKIOS_GPU_SURFACE && size==sizeof(struct hakios_drm_surface));
    struct hakios_drm_surface *request=data;assert(request->surface==55);calls++;
    if(refuse)return refuse;
    if(request->handle==HAKIOS_GPU_DESCRIBE)request->size=described_size;
    else {assert(request->handle==0);request->handle=77;}
    return 0;
}
'''+adapter+'''
int main(void) {
    uint64_t token;uint32_t handle=999;
    int fd=hakios_native_surface_fd_create(55,16384,&token);assert(fd>=0);
    assert(!drmPrimeFDToHandle(0,fd,&handle) && calls==2 && handle==77);
    described_size=32768;calls=0;handle=999;
    assert(drmPrimeFDToHandle(0,fd,&handle)<0 && errno==EINVAL && calls==1 && handle==999);
    described_size=16384;calls=0;refuse=EIO;
    assert(drmPrimeFDToHandle(0,fd,&handle)<0 && errno==EIO && calls==1 && handle==999);
    refuse=0;calls=0;assert(drmPrimeFDToHandle(0,fd,NULL)<0 && calls==0);
    int unrelated=open("/dev/null",O_RDWR);assert(unrelated>=0);calls=0;
    assert(drmPrimeFDToHandle(0,unrelated,&handle)<0 && calls==0);
    close(unrelated);close(fd);assert(!hakios_native_surface_fd_release(token));
    puts("PASS actual surface import adapter: describe-before-import, ABI size, mismatched length, service errors, null output and unknown FD rejected without kernel import");
}
'''
with tempfile.TemporaryDirectory(prefix='scion-surface-transport-') as temporary:
    path=Path(temporary);(path/'test.c').write_text(test)
    canonical=root/'tools/mesa/panvk-native'
    subprocess.run(['cc','-std=c11','-D_GNU_SOURCE','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-pthread','-I'+str(canonical),str(path/'test.c'),
        str(canonical/'hakios-native-surface-fd.c'),'-o',str(path/'check')],check=True)
    subprocess.run([str(path/'check')],check=True)
