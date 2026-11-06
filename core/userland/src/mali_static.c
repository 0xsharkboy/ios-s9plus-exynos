/*
 * Hardware GPU client probe small enough for the Samsung BOOT partition.
 * Uses unmodified MIG stubs generated from this kernel's Apple device.defs
 * and mach_host.defs. Every GPU operation crosses the actual IOKit user client.
 * Three write-value jobs test mapped buffers, submission, waits and cleanup.
 * These jobs do not run shaders and do not establish rendering or frame rate.
 */
#include "iokit_user.h"
#include "mach_host_user.h"
#include <stdint.h>

extern void probe_runtime_init(void);
extern void probe_put(const char *);
extern void probe_hex(uint64_t);
extern void probe_sleep(void);
extern void probe_park(void) __attribute__((noreturn));
extern mach_port_t probe_host_self(void);
extern void probe_port_release(mach_port_t);
extern mach_port_t mach_task_self_;

static mach_port_t gpu;

static void
value(const char *label, uint64_t number)
{
    probe_put("mali_static: "); probe_put(label); probe_put("=");
    probe_hex(number); probe_put("\n");
}

static int
check(const char *label, kern_return_t status)
{
    value(label, (uint32_t)status);
    return status == KERN_SUCCESS;
}

static kern_return_t
request(uint32_t selector, void *structure, uint32_t size)
{
    mach_msg_type_number_t output_size = size, scalar_count = 0;
    mach_vm_size_t ool_size = 0;
    kern_return_t status = io_connect_method(gpu, selector, NULL, 0,
        structure, size, 0, 0, structure, &output_size, NULL, &scalar_count, 0, &ool_size);
    return status ? status : output_size == size ? KERN_SUCCESS : MIG_TYPE_ERROR;
}

#ifdef MALI_REPLAY
#include "mali_replay.inc"
#endif

static int
test(void)
{
    mach_port_t main_port = 0, iterator = 0, service = 0;
    kern_return_t status, open_status = KERN_FAILURE;
    char matching[] = "<dict><key>IOProviderClass</key><string>HakiosMali</string></dict>";
    if (!check("host_get_io_main", host_get_io_main(probe_host_self(), &main_port))) return 0;
    value("main_port", main_port);
    for (unsigned attempt = 0; attempt < 30; attempt++) {
        status = io_service_get_matching_services_bin(main_port, matching, sizeof(matching), &iterator);
        if (!check("matching", status)) return 0;
        status = io_iterator_next(iterator, &service);
        probe_port_release(iterator);
        if (status == KERN_SUCCESS && service) break;
        probe_sleep();
    }
    if (!service) {
        probe_put("mali_static: NO_GPU_SERVICE (expected in the QEMU transport check)\n");
        return 0;
    }
    status = io_service_open_extended(service, mach_task_self_, 0, NDR_record,
        NULL, 0, &open_status, &gpu);
    if (!check("open_transport", status) || !check("open_result", open_status) || !gpu) return 0;
    probe_put("mali_static: CLIENT_OPEN\n");

    struct { uint32_t param, pad; uint64_t value; } parameter = { 0 };
    if (!check("GET_PARAM", request(0x44, &parameter, sizeof(parameter)))) return 0;
    value("GPU_PROD_ID", parameter.value);
    if (parameter.value != 0x6221) return 0;
#ifdef MALI_REPLAY
    return replay();
#else
    struct { uint32_t size, flags, handle, pad; uint64_t offset; } bo = { .size = 16384 };
    if (!check("CREATE_BO", request(0x42, &bo, sizeof(bo))) || !bo.handle) return 0;
    value("buffer_handle", bo.handle); value("GPU_VA", bo.offset);
    if (bo.offset < 0x2000000ull || bo.offset + bo.size > 0x100000000ull) return 0;
    struct { uint32_t handle, flags; uint64_t offset; } mm = { .handle = bo.handle };
    if (!check("MMAP_BO", request(0x43, &mm, sizeof(mm)))) return 0;
    mach_vm_address_t address = 0;
    mach_vm_size_t mapped_size = 0;
    if (!check("MAP_MEMORY", io_connect_map_memory_into_task(gpu, bo.handle,
        mach_task_self_, &address, &mapped_size, 1)) || mapped_size < 16384) return 0;
    value("CPU_VA", address); value("mapped_size", mapped_size);

    unsigned passed = 0;
    for (unsigned round = 0; round < 3; round++) {
        volatile uint32_t *job = (volatile uint32_t *)(uintptr_t)address;
        volatile uint32_t *word = (volatile uint32_t *)(uintptr_t)(address + 0x800);
        uint32_t expected = 0xc0ffee01u + round;
        for (unsigned i = 0; i < 1024; i++) job[i] = 0;
        job[4] = (2u << 1) | 1u | (1u << 16);
        job[8] = (uint32_t)(bo.offset + 0x800);
        job[9] = (uint32_t)((bo.offset + 0x800) >> 32);
        job[10] = 6;
        job[12] = expected;
        __asm__ volatile ("dsb sy" ::: "memory");
        struct {
            uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync;
            uint64_t bo_handles; uint32_t bo_handle_count, requirements;
        } submit = { .jc = bo.offset, .bo_handles = (uint64_t)(uintptr_t)&bo.handle, .bo_handle_count = 1 };
        if (!check("SUBMIT", request(0x40, &submit, sizeof(submit))) || !submit.jc) break;
        struct { uint64_t sequence; int64_t timeout_ns; } wait = { submit.jc, 1000000000 };
        struct { uint32_t handle, pad; int64_t timeout_ns; } wait_bo = { bo.handle, 0, 1000000000 };
        if (!check("WAIT_SEQ", request(0x7d, &wait, sizeof(wait))) ||
            !check("WAIT_BO", request(0x41, &wait_bo, sizeof(wait_bo)))) break;
        __asm__ volatile ("dsb sy" ::: "memory");
        value("job_status", job[0]); value("readback", *word);
        if (*word != expected || job[0] != 1) break;
        passed++;
    }
    int unmapped = check("UNMAP_MEMORY", io_connect_unmap_memory_from_task(gpu,
        bo.handle, mach_task_self_, address));
    struct { uint32_t handle, pad; } close = { bo.handle, 0 };
    int freed = check("GEM_CLOSE", request(0x09, &close, sizeof(close)));
    int closed = check("SERVICE_CLOSE", io_service_close(gpu));
    probe_port_release(gpu); probe_port_release(service); probe_port_release(main_port);
    value("jobs_passed", passed);
    return passed == 3 && unmapped && freed && closed;
#endif
}

void
start(void)
{
    probe_runtime_init();
    probe_put("\nmali_static: START static IOKit client; no dyld or libc\n");
#ifdef MALI_REPLAY
    probe_put(test() ? "mali_replay: PASS Panfrost compute shader output 58 verified\n" : "mali_replay: FAIL; buffers retained until reset\n");
#else
    probe_put(test() ? "mali_static: PASS 3 real user-client GPU jobs\n" : "mali_static: FAIL or GPU absent; inspect preceding status\n");
#endif
    probe_park();
}
