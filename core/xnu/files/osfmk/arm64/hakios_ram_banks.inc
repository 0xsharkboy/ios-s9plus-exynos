/* Included by arm_vm_init.c after its physical-window globals.
 * boot_args retains the primary contiguous bootstrap window. Only the explicit,
 * opt-in DT bank list contributes extra pages; gaps never enlarge that window.
 */
#include <arm64/hakios_ram_banks.h>

SECURITY_READ_ONLY_LATE(struct hakios_ram_banks) hakios_managed_ram;
SECURITY_READ_ONLY_LATE(static uint64_t) hakios_vm_first_ram_index;

uint64_t
hakios_ram_pmap_index(uint64_t address)
{
    uint64_t index;
    if (!hakios_ram_pa_index(&hakios_managed_ram, address, &index))
        panic("ram_banks: unmanaged physical address 0x%llx", address);
    return index;
}

uint64_t
hakios_ram_pmap_pa(uint64_t index)
{
    uint64_t address;
    if (!hakios_ram_index_pa(&hakios_managed_ram, index, &address))
        panic("ram_banks: invalid metadata index 0x%llx", index);
    return address;
}

void
hakios_ram_set_vm_first_page(uint64_t ppnum)
{
    if (hakios_managed_ram.count > 1)
        hakios_vm_first_ram_index = hakios_ram_pmap_index(ppnum << PAGE_SHIFT);
}

uint64_t
hakios_ram_vm_page_pa(uint64_t index)
{
    if (index > UINT64_MAX - hakios_vm_first_ram_index)
        panic("ram_banks: VM page index overflow");
    return hakios_ram_pmap_pa(hakios_vm_first_ram_index + index);
}

static uint64_t
hakios_ram_extra_bytes(void)
{
    if (hakios_managed_ram.count <= 1) return 0;
    return (hakios_managed_ram.total_pages - hakios_managed_ram.bank[0].pages) << PAGE_SHIFT;
}

/* The primary mapping may include the KASAN shadow or an unused capped tail,
 * whereas additional mappings must end at their own bank's exact boundary. */
static pmap_paddr_t
hakios_ram_mapping_end(pmap_paddr_t address)
{
    if (address >= gPhysBase && address < real_avail_end) return real_avail_end;
    for (uint32_t i = 1; i < hakios_managed_ram.count; ++i) {
        const struct hakios_ram_bank *bank = &hakios_managed_ram.bank[i];
        uint64_t size = bank->pages << PAGE_SHIFT;
        if (address >= bank->base && address - bank->base < size) return bank->base + size;
    }
    return 0;
}

static uint64_t
hakios_ram_boot_memory_size(boot_args *args, uint64_t limit)
{
    int enabled = 0;
    if (!PE_parse_boot_argn("ram_banks", &enabled, sizeof(enabled)) || enabled != 1)
        return gPhysSize;
    DTEntry chosen;
    void *property = NULL;
    unsigned int length = 0;
    if (SecureDTLookupEntry(NULL, "/chosen", &chosen) != kSuccess ||
        SecureDTGetProperty(chosen, "hakios-ram-banks", &property, &length) != kSuccess ||
        !property || !length || length % 16 || length / 16 > HAKIOS_RAM_BANK_MAX)
        panic("ram_banks: missing or malformed claimed-bank list");
    struct { uint64_t base, bytes; } range;
    if (hakios_ram_banks_init(&hakios_managed_ram, PAGE_SHIFT) != HAKIOS_RAM_OK)
        panic("ram_banks: unsupported page size");
    uint64_t remaining = limit ? (limit & ~((uint64_t)PAGE_MASK)) : UINT64_MAX;
    for (unsigned int i = 0; i < length / 16; ++i) {
        memcpy(&range, (const char *)property + i * 16, sizeof(range));
        if (i == 0 && (range.base != args->physBase || range.bytes != args->memSize))
            panic("ram_banks: primary bank differs from boot_args");
        if (!remaining) break;
        if (range.bytes > remaining) range.bytes = remaining;
        /* Keep the bootstrap bank boundary even if another claim is adjacent. */
        if (hakios_ram_bank_add_mode(&hakios_managed_ram, range.base, range.bytes, 0) != HAKIOS_RAM_OK)
            panic("ram_banks: invalid, overlapping or overflowing bank %u", i);
        remaining -= range.bytes;
    }
    if (!hakios_managed_ram.count) panic("ram_banks: no usable memory");
#if KASAN || XNU_MONITOR
    if (hakios_managed_ram.count > 1)
        panic("ram_banks: extra banks require the generic non-KASAN pmap configuration");
#endif
    uint64_t bytes = hakios_managed_ram.total_pages << PAGE_SHIFT;
    kprintf("ram_banks: %u claimed banks, %llu MiB managed, primary %llu MiB\n",
            hakios_managed_ram.count, bytes >> 20, hakios_managed_ram.bank[0].pages >> (20 - PAGE_SHIFT));
    return bytes;
}
