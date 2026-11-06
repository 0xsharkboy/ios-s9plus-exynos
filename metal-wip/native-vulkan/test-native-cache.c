#include "cache_ops.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static uint64_t reported = 64;
static unsigned queries, calls;
static int cache_error;
static void *last_address;
static size_t last_length;
int sysctlbyname(const char *name, void *value, size_t *length, const void *new_value, size_t new_length)
{
    assert(!strcmp(name,"hw.cachelinesize") && !new_value && !new_length);
    assert(*length == sizeof(reported)); memcpy(value,&reported,sizeof(reported)); *length=sizeof(reported); ++queries; return 0;
}
int sys_cache_control(int operation, void *address, size_t length)
{
    assert(operation == 2); ++calls; last_address=address; last_length=length;
    if (cache_error) { errno=EIO; return -1; } return 0;
}
static void expect_abort(void (*operation)(void))
{
    pid_t child=fork(); assert(child>=0);
    if (!child) { operation(); _exit(0); }
    int status; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
}
static void missing_granularity(void) { reported=0; util_cache_granularity(); }
static void invalid_granularity(void) { reported=3; util_cache_granularity(); }
static void failed_cache_api(void) { cache_error=1; util_flush_range((void *)4096,128); }
static void overflow_range(void) { util_flush_range((void *)(UINTPTR_MAX-4),16); }
int main(void)
{
    expect_abort(missing_granularity); expect_abort(invalid_granularity);
    assert(util_cache_granularity()==64 && util_cache_granularity()==64 && queries==1);
    char buffer[1024]={0};
    util_flush_range(buffer,0); util_flush_inval_range(buffer,0);
    util_flush_range_no_fence(buffer,0); util_flush_inval_range_no_fence(buffer,0); assert(!calls);
    util_flush_range(buffer+3,513); assert(calls==1 && last_address==buffer+3 && last_length==513);
    util_flush_inval_range(buffer+11,19); assert(calls==2 && last_address==buffer+11 && last_length==19);
    util_flush_range_no_fence(buffer+21,31); assert(calls==3 && last_address==buffer+21 && last_length==31);
    util_flush_inval_range_no_fence(buffer+47,63); assert(calls==4 && last_address==buffer+47 && last_length==63);
    expect_abort(failed_cache_api); expect_abort(overflow_range);
    puts("Darwin cache adapter: OS granularity, exact ranges, flush+invalidate, empty ranges, invalid size/API failure/overflow rejection PASS");
    return 0;
}
