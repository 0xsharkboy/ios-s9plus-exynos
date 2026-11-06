/* CPU cache maintenance keyed to the IOSurface content seed.
 * The caller serializes each buffer's stamp. Externally wrapped memory has no
 * enforced IOSurface write protocol and always requires maintenance.
 */
#ifndef HAKIOS_MALI_CPU_CACHE_H
#define HAKIOS_MALI_CPU_CACHE_H
#include <stdint.h>
#include <stdbool.h>

struct mali_cpu_cache_stamp { uint32_t seed; bool valid; };

static inline bool
mali_cpu_cache_needs_clean(const struct mali_cpu_cache_stamp *stamp, uint32_t seed, bool external)
{
    return external || !stamp->valid || stamp->seed != seed;
}

static inline void
mali_cpu_cache_cleaned(struct mali_cpu_cache_stamp *stamp, uint32_t before, uint32_t after, bool external)
{
    stamp->seed = after;
    stamp->valid = !external && before == after;
}
#endif
