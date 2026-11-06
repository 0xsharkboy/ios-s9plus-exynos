/* Read-only memory samples. No allocations proportional to RAM, no storage writes. */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <unistd.h>

static double
mib(uint64_t bytes)
{
    return (double)bytes / (1024.0 * 1024.0);
}

static void
filesystem(const char *path)
{
    struct statfs s;
    if (statfs(path, &s) == 0) {
        printf("mem_info: fs=%s type=%s total_mib=%.2f used_mib=%.2f available_mib=%.2f\n",
               path, s.f_fstypename, mib(s.f_blocks * (uint64_t)s.f_bsize),
               mib((s.f_blocks - s.f_bfree) * (uint64_t)s.f_bsize),
               mib(s.f_bavail * (uint64_t)s.f_bsize));
    } else {
        perror(path);
    }
}

int
main(int argc, char **argv)
{
    unsigned samples = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 1;
    unsigned interval = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 30;
    if (!samples || samples > 120 || !interval || interval > 300) {
        fprintf(stderr, "usage: mem_info [samples 1..120] [interval_seconds 1..300]\n");
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) != KERN_SUCCESS) {
        fprintf(stderr, "mem_info: host_page_size failed\n");
        mach_port_deallocate(mach_task_self(), host);
        return 1;
    }
    uint64_t physical = 0;
    size_t length = sizeof(physical);
    if (sysctlbyname("hw.memsize", &physical, &length, NULL, 0) != 0) {
        perror("hw.memsize");
    }
    for (unsigned i = 0; i < samples; ++i) {
        vm_statistics64_data_t vm = {0};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        kern_return_t result = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count);
        if (result != KERN_SUCCESS) {
            fprintf(stderr, "mem_info: host_statistics64 failed %d\n", result);
            mach_port_deallocate(mach_task_self(), host);
            return 1;
        }
        /* Speculative is part of free; purgeable overlaps other states. Do not sum them. */
        printf("mem_info: sample=%u ticks=%llu physical_mib=%.2f page_bytes=%lu "
               "free_mib=%.2f active_mib=%.2f inactive_mib=%.2f wired_mib=%.2f "
               "compressed_physical_mib=%.2f compressed_logical_mib=%.2f "
               "purgeable_mib=%.2f speculative_mib=%.2f "
               "pageins=%llu pageouts=%llu compressions=%llu decompressions=%llu "
               "swapins=%llu swapouts=%llu\n",
               i, (unsigned long long)mach_absolute_time(), mib(physical), (unsigned long)page_size,
               mib((uint64_t)vm.free_count * page_size), mib((uint64_t)vm.active_count * page_size),
               mib((uint64_t)vm.inactive_count * page_size), mib((uint64_t)vm.wire_count * page_size),
               mib((uint64_t)vm.compressor_page_count * page_size),
               mib(vm.total_uncompressed_pages_in_compressor * page_size),
               mib((uint64_t)vm.purgeable_count * page_size), mib((uint64_t)vm.speculative_count * page_size),
               (unsigned long long)vm.pageins, (unsigned long long)vm.pageouts,
               (unsigned long long)vm.compressions, (unsigned long long)vm.decompressions,
               (unsigned long long)vm.swapins, (unsigned long long)vm.swapouts);
        int pressure = 0;
        length = sizeof(pressure);
        if (sysctlbyname("kern.memorystatus_vm_pressure_level", &pressure, &length, NULL, 0) == 0) {
            printf("mem_info: memory_pressure_level=%d\n", pressure);
        }
        filesystem("/private/var");
        filesystem("/private/preboot/jb-rw");
        if (i + 1 < samples) {
            sleep(interval);
        }
    }
    mach_port_deallocate(mach_task_self(), host);
    return 0;
}
