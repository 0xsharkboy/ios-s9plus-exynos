#include <assert.h>
#include <stdio.h>
#include "../drivers/gpu/hakios.mali_cpu_cache.h"

/* Model separate CPU dirty lines and GPU-visible memory. This checks that the
 * policy publishes new CPU generations while preserving GPU-only updates. It
 * does not prove that every real IOSurface producer follows the seed protocol.
 */
struct memory_model { uint32_t cpu, memory; bool dirty; unsigned cleans; };
static void submit(struct mali_cpu_cache_stamp *stamp, struct memory_model *memory,
                   uint32_t seed, bool external)
{
    if (!mali_cpu_cache_needs_clean(stamp, seed, external)) return;
    if (memory->dirty) memory->memory = memory->cpu;
    memory->dirty = false;
    memory->cleans++;
    mali_cpu_cache_cleaned(stamp, seed, seed, external);
}

int main(void)
{
    struct mali_cpu_cache_stamp first = {0}, second = {0};
    struct memory_model memory = { .cpu = 12, .dirty = true };
    submit(&first, &memory, 1, false);
    assert(memory.memory == 12 && memory.cleans == 1);
    for (unsigned i = 0; i < 100; i++) submit(&first, &memory, 1, false);
    assert(memory.cleans == 1);
    memory.memory = 34; /* GPU write, no dirty CPU cache lines. */
    submit(&first, &memory, 1, false);
    assert(memory.memory == 34 && memory.cleans == 1);
    memory.cpu = 56; memory.dirty = true;
    submit(&first, &memory, 2, false);
    assert(memory.memory == 56 && memory.cleans == 2);
    submit(&second, &memory, 2, false); /* Independent client/handle starts cleanly. */
    assert(memory.cleans == 3 && second.valid);
    memory.cpu = 78; memory.dirty = true;
    submit(&second, &memory, 3, false);
    submit(&first, &memory, 3, false);
    assert(memory.memory == 78 && memory.cleans == 5);
    /* Wrapped memory must publish writes even when its producer doesn't change the seed. */
    memory.cpu = 90; memory.dirty = true;
    submit(&first, &memory, 3, true);
    assert(memory.memory == 90 && !first.valid);
    memory.cpu = 91; memory.dirty = true;
    submit(&first, &memory, 3, true);
    assert(memory.memory == 91 && memory.cleans == 7);
    mali_cpu_cache_cleaned(&first, 10, 11, false);
    assert(!first.valid && mali_cpu_cache_needs_clean(&first, 11, false));
    mali_cpu_cache_cleaned(&first, UINT32_MAX, UINT32_MAX, false);
    assert(mali_cpu_cache_needs_clean(&first, 0, false));
    puts("PASS: CPU generations, GPU-only writes, independent clients, wrapped writes, racing seed and rollover");
    return 0;
}
