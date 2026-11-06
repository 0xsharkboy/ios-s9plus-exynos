/* Replay one captured Panfrost compute submission through the real IOKit
 * client. A parameter/VA mismatch stops before submission. On any GPU failure
 * buffers remain owned by PID 1 until the loader resets the phone: do not free
 * memory a stalled GPU could still access. This is not a rendering benchmark.
 */
struct replay_event { unsigned op, id, size, flags; uint64_t va; };
struct replay_param { unsigned param; uint64_t value; };
struct replay_span { unsigned id, offset, size; const unsigned char *data; };
#include "mali_replay_payload.h"
#define REPLAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
static struct {
    unsigned handle, size;
    uint64_t gpu_va, cpu_va;
} replay_buffers[128];

static int
replay_free(unsigned ident)
{
    if (!replay_buffers[ident].handle) return 0;
    if (replay_buffers[ident].cpu_va &&
        !check("REPLAY_UNMAP", io_connect_unmap_memory_from_task(gpu,
            replay_buffers[ident].handle, mach_task_self_, replay_buffers[ident].cpu_va))) return 0;
    struct { uint32_t handle, pad; } close = { replay_buffers[ident].handle, 0 };
    if (!check("REPLAY_FREE", request(0x09, &close, sizeof(close)))) return 0;
    replay_buffers[ident].handle = 0;
    replay_buffers[ident].cpu_va = 0;
    return 1;
}

static int
replay(void)
{
    int compatible = 1;
    for (unsigned i = 0; i < REPLAY_COUNT(replay_params); i++) {
        struct { uint32_t param, pad; uint64_t value; } p = { replay_params[i].param, 0, 0 };
        value("REPLAY_PARAM_ID", p.param);
        if (!check("REPLAY_PARAM_STATUS", request(0x44, &p, sizeof(p)))) return 0;
        value("REPLAY_PARAM_VALUE", p.value);
        if (p.value != replay_params[i].value) {
            value("REPLAY_PARAM_EXPECTED", replay_params[i].value);
            compatible = 0;
        }
    }
    if (!compatible) {
        probe_put("mali_replay: PARAMETER_MISMATCH; no captured job submitted\n");
        return 0;
    }
    for (unsigned i = 0; i < REPLAY_COUNT(replay_events); i++) {
        const struct replay_event *event = &replay_events[i];
        if (!event->id || event->id >= REPLAY_COUNT(replay_buffers)) return 0;
        if (event->op == 2) {
            if (!replay_free(event->id)) return 0;
            continue;
        }
        if (event->op != 1 || replay_buffers[event->id].handle) return 0;
        struct { uint32_t size, flags, handle, pad; uint64_t offset; } bo = { event->size, event->flags, 0, 0, 0 };
        if (!check("REPLAY_ALLOC", request(0x42, &bo, sizeof(bo))) || !bo.handle) return 0;
        value("REPLAY_BO_ID", event->id); value("REPLAY_BO_VA", bo.offset);
        if (bo.offset != event->va) {
            value("REPLAY_BO_EXPECTED_VA", event->va);
            probe_put("mali_replay: VA_MISMATCH; no captured job submitted\n");
            return 0;
        }
        replay_buffers[event->id].handle = bo.handle;
        replay_buffers[event->id].size = event->size;
        replay_buffers[event->id].gpu_va = bo.offset;
    }
    uint32_t handles[64];
    if (REPLAY_COUNT(replay_submit_ids) > REPLAY_COUNT(handles)) return 0;
    for (unsigned i = 0; i < REPLAY_COUNT(replay_submit_ids); i++) {
        unsigned ident = replay_submit_ids[i];
        if (ident >= REPLAY_COUNT(replay_buffers) || !replay_buffers[ident].handle) return 0;
        handles[i] = replay_buffers[ident].handle;
        struct { uint32_t handle, flags; uint64_t offset; } mm = { handles[i], 0, 0 };
        if (!check("REPLAY_MMAP_BO", request(0x43, &mm, sizeof(mm)))) return 0;
        mach_vm_address_t address = 0;
        mach_vm_size_t size = 0;
        if (!check("REPLAY_MAP", io_connect_map_memory_into_task(gpu, handles[i],
            mach_task_self_, &address, &size, 1)) || size < replay_buffers[ident].size) return 0;
        replay_buffers[ident].cpu_va = address;
        volatile unsigned char *bytes = (volatile unsigned char *)(uintptr_t)address;
        for (unsigned j = 0; j < replay_buffers[ident].size; j++) bytes[j] = 0;
    }
    for (unsigned i = 0; i < REPLAY_COUNT(replay_spans); i++) {
        const struct replay_span *span = &replay_spans[i];
        if (span->id >= REPLAY_COUNT(replay_buffers) || !replay_buffers[span->id].cpu_va ||
            span->offset > replay_buffers[span->id].size ||
            span->size > replay_buffers[span->id].size - span->offset) return 0;
        volatile unsigned char *to = (volatile unsigned char *)(uintptr_t)(replay_buffers[span->id].cpu_va + span->offset);
        for (unsigned j = 0; j < span->size; j++) to[j] = span->data[j];
    }
    volatile uint32_t *input = (volatile uint32_t *)(uintptr_t)(replay_buffers[REPLAY_INPUT_ID].cpu_va + REPLAY_INPUT_OFFSET);
    volatile uint32_t *output = (volatile uint32_t *)(uintptr_t)(replay_buffers[REPLAY_OUTPUT_ID].cpu_va + REPLAY_OUTPUT_OFFSET);
    if (input[0] != 17 || input[1] != 0x4e49504c || output[0] != 0xa5a5a5a5 || output[1] != 0x54554f50) return 0;
    value("REPLAY_INITIAL_OUTPUT", output[0]);
    __asm__ volatile ("dsb sy" ::: "memory");
    struct {
        uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync;
        uint64_t bo_handles; uint32_t bo_handle_count, requirements;
    } submit = { .jc = REPLAY_JC, .bo_handles = (uint64_t)(uintptr_t)handles,
        .bo_handle_count = REPLAY_COUNT(replay_submit_ids) };
    if (!check("REPLAY_SUBMIT", request(0x40, &submit, sizeof(submit))) || !submit.jc) return 0;
    struct { uint64_t sequence; int64_t timeout_ns; } wait = { submit.jc, 1000000000 };
    if (!check("REPLAY_WAIT_SEQ", request(0x7d, &wait, sizeof(wait)))) return 0;
    for (unsigned i = 0; i < REPLAY_COUNT(replay_submit_ids); i++) {
        struct { uint32_t handle, pad; int64_t timeout_ns; } wb = { handles[i], 0, 1000000000 };
        if (!check("REPLAY_WAIT_BO", request(0x41, &wb, sizeof(wb)))) return 0;
    }
    __asm__ volatile ("dsb sy" ::: "memory");
    value("REPLAY_SHADER_OUTPUT", output[0]);
    if (output[0] != 58 || output[1] != 0x54554f50 || input[0] != 17 || input[1] != 0x4e49504c) return 0;
    const volatile unsigned char *guard = (const volatile unsigned char *)output;
    for (unsigned i = 8; i < 4096; i++) if (guard[i] != 0xa5) return 0;
    for (unsigned i = 1; i < REPLAY_COUNT(replay_buffers); i++)
        if (replay_buffers[i].handle && !replay_free(i)) return 0;
    return check("REPLAY_SERVICE_CLOSE", io_service_close(gpu));
}
