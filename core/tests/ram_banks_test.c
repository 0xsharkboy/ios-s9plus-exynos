#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../xnu/files/osfmk/arm64/hakios_ram_banks.h"

int main(void)
{
    struct hakios_ram_banks table;
    assert(hakios_ram_banks_init(&table, 14) == HAKIOS_RAM_OK);
    /* S9+ high DRAM addresses; split adjacent firmware descriptors coalesce. */
    assert(hakios_ram_bank_add(&table, UINT64_C(0x880000000), UINT64_C(0x80000000)) == HAKIOS_RAM_OK);
    assert(hakios_ram_bank_add(&table, UINT64_C(0x900000000), UINT64_C(0x80000000)) == HAKIOS_RAM_OK);
    assert(table.count == 1 && table.total_pages == 262144);
    /* Synthetic claimed low spans, not permission to use these addresses on hardware. */
    assert(hakios_ram_bank_add(&table, UINT64_C(0x80004000), UINT64_C(0x100000)) == HAKIOS_RAM_OK);
    assert(hakios_ram_bank_add(&table, UINT64_C(0xc0080000), UINT64_C(0x200000)) == HAKIOS_RAM_OK);
    assert(table.count == 3 && table.total_pages == 262336);
    uint64_t value = 123, address = 0;
    assert(!hakios_ram_pa_index(&table, UINT64_C(0x100000000), &value) && value == 123);
    assert(!hakios_ram_pa_index(&table, UINT64_C(0x80000000), &value));
    assert(!hakios_ram_pa_index(&table, UINT64_C(0x980000000), &value));
    assert(!hakios_ram_index_pa(&table, table.total_pages, &address));
    assert(hakios_ram_contiguous_bytes(&table, UINT64_C(0x97ffffff0)) == 16);
    assert(hakios_ram_contiguous_bytes(&table, UINT64_C(0x100000000)) == 0);
    /* Every page, including both sides of each discontinuity, has one inverse. */
    for (uint64_t i = 0; i < table.total_pages; ++i) {
        assert(hakios_ram_index_pa(&table, i, &address));
        assert(hakios_ram_pa_index(&table, address, &value) && value == i);
        assert(hakios_ram_pa_index(&table, address + 16383, &value) && value == i);
    }
    struct hakios_ram_banks saved = table;
    assert(hakios_ram_bank_add(&table, UINT64_C(0x880004000), 16384) == HAKIOS_RAM_OVERLAP);
    assert(memcmp(&saved, &table, sizeof(table)) == 0);
    assert(hakios_ram_bank_add(&table, UINT64_C(0xffffffffffffc000), 16384) == HAKIOS_RAM_OVERFLOW);
    assert(hakios_ram_bank_add(&table, 1, 16384) == HAKIOS_RAM_INVALID);
    assert(hakios_ram_bank_add(&table, 0, 0) == HAKIOS_RAM_INVALID);
    assert(memcmp(&saved, &table, sizeof(table)) == 0);
    assert(hakios_ram_banks_init(&table, 12) == HAKIOS_RAM_OK);
    for (uint32_t i = 0; i < HAKIOS_RAM_BANK_MAX; ++i)
        assert(hakios_ram_bank_add(&table, (uint64_t)i * 8192, 4096) == HAKIOS_RAM_OK);
    assert(hakios_ram_bank_add(&table, UINT64_C(0x1000000), 4096) == HAKIOS_RAM_FULL);
    /* Full descriptor storage may still coalesce a physically adjacent final span. */
    assert(hakios_ram_bank_add(&table, (HAKIOS_RAM_BANK_MAX - 1) * 8192 + 4096, 4096) == HAKIOS_RAM_OK);
    assert(table.total_pages == HAKIOS_RAM_BANK_MAX + 1);
    assert(hakios_ram_banks_init(&table, 14) == HAKIOS_RAM_OK);
    assert(hakios_ram_bank_add_mode(&table, UINT64_C(0x880000000), UINT64_C(0x100000), 0) == HAKIOS_RAM_OK);
    assert(hakios_ram_bank_add_mode(&table, UINT64_C(0x880100000), UINT64_C(0x100000), 0) == HAKIOS_RAM_OK);
    assert(table.count == 2 && table.bank[0].pages == 64 && table.bank[1].first_index == 64);
    puts("RAM bank model PASS: sparse inverses, physical gaps, overflow, overlap, capacity, adjacency");
    return 0;
}
