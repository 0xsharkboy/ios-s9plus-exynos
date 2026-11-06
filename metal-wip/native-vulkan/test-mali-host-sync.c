/* SPDX-License-Identifier: MIT */
#include "hakios.mali_host_sync.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    _Static_assert(sizeof(struct hakios_mali_host_sync)==24,"wire size");
    _Static_assert(offsetof(struct hakios_mali_host_sync,offset)==8,"wire offset");
    _Static_assert(offsetof(struct hakios_mali_host_sync,length)==16,"wire length");
    struct hakios_mali_host_sync r={HAKIOS_MALI_HOST_SYNC_VERSION,0,0,0};
    assert(hakios_mali_host_sync_query(&r) && !hakios_mali_host_sync_range(&r,16384));
    r.version++;assert(!hakios_mali_host_sync_query(&r));r.version--;
    r.handle=7;r.length=16384;
    assert(!hakios_mali_host_sync_query(&r) && hakios_mali_host_sync_range(&r,16384));
    r.offset=1;assert(!hakios_mali_host_sync_range(&r,16384));
    r.offset=16384;r.length=0;assert(hakios_mali_host_sync_range(&r,16384));
    r.offset=16385;assert(!hakios_mali_host_sync_range(&r,16384));
    r.offset=UINT64_MAX;r.length=1;assert(!hakios_mali_host_sync_range(&r,UINT64_MAX));
    r.offset=UINT64_MAX-63;r.length=64;assert(!hakios_mali_host_sync_range(&r,UINT64_MAX));
    r.length=63;assert(hakios_mali_host_sync_range(&r,UINT64_MAX));
    r.offset=1;r.length=UINT64_MAX;assert(!hakios_mali_host_sync_range(&r,16384));
    for(uint64_t offset=0;offset<257;offset++)for(uint64_t length=0;length<257;length++) {
        r.offset=offset;r.length=length;
        assert(hakios_mali_host_sync_range(&r,128)==(offset+length<=128));
    }
    puts("PASS host-sync ABI, version/query separation, bounds, zero-length end range and overflow rejection");
}
