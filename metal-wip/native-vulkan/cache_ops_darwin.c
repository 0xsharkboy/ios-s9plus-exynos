/* SPDX-License-Identifier: MIT
 * Darwin cache maintenance for Mesa's common CPU/GPU memory helpers.
 * EL0 cannot assume Linux's CTR_EL0/DC instruction permissions on XNU.
 */
#include "cache_ops.h"
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>

static pthread_once_t cache_once = PTHREAD_ONCE_INIT;
static size_t cache_granularity;

static void initialize_cache_granularity(void)
{
   uint64_t reported = 0;
   size_t length = sizeof(reported);
   if (sysctlbyname("hw.cachelinesize", &reported, &length, NULL, 0) ||
       (length != sizeof(uint32_t) && length != sizeof(uint64_t)) ||
       !reported || reported > SIZE_MAX || (reported & (reported - 1))) {
      fprintf(stderr, "Mesa Darwin: cannot obtain a valid OS cache-line granularity\n");
      abort();
   }
   cache_granularity = (size_t)reported;
   fprintf(stderr, "Mesa Darwin: OS cache granularity %zu bytes; DMA cache maintenance via sys_cache_control\n",
           cache_granularity);
}

size_t util_cache_granularity(void)
{
   pthread_once(&cache_once, initialize_cache_granularity);
   return cache_granularity;
}

static void flush_darwin_range(void *address, size_t length)
{
   if (!length) return;
   if (length > UINTPTR_MAX - (uintptr_t)address ||
       sys_cache_control(kCacheFunctionFlushDcache, address, length)) {
      fprintf(stderr, "Mesa Darwin: OS DMA cache flush failed\n");
      abort();
   }
}

void util_pre_flush_fence(void)
{
   atomic_thread_fence(memory_order_seq_cst);
}
void util_post_flush_fence(void)
{
   atomic_thread_fence(memory_order_seq_cst);
}
void util_post_flush_inval_fence(void)
{
   atomic_thread_fence(memory_order_seq_cst);
}
void util_flush_range_no_fence(void *address, size_t length)
{
   /* The OS operation also invalidates: this is stronger than clean-only.
    * It includes its own completion, so the no-fence API remains safe. */
   flush_darwin_range(address, length);
}
void util_flush_inval_range_no_fence(void *address, size_t length)
{
   /* OSCacheControl documents FlushDcache as reaching DRAM and removing
    * processor copies, including for non-coherent devices and DMA. */
   flush_darwin_range(address, length);
}
void util_flush_range(void *address, size_t length)
{
   if (!length) return;
   util_pre_flush_fence();
   flush_darwin_range(address, length);
   util_post_flush_fence();
}
void util_flush_inval_range(void *address, size_t length)
{
   if (!length) return;
   util_pre_flush_fence();
   flush_darwin_range(address, length);
   util_post_flush_inval_fence();
}
