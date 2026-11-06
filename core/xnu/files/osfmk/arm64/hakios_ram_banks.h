/* Shared loader/VM model for discontinuous physical RAM.
 * Only firmware-claimed, reservation-filtered ranges may be added here.
 * Bank order defines compact metadata indices; physical addresses need not be ordered.
 * This model never makes a physical gap into managed memory.
 */
#ifndef HAKIOS_RAM_BANKS_H
#define HAKIOS_RAM_BANKS_H

#include <stdint.h>

#define HAKIOS_RAM_BANK_MAX 64u

struct hakios_ram_bank {
    uint64_t base;
    uint64_t pages;
    uint64_t first_index;
};

struct hakios_ram_banks {
    uint32_t count;
    uint32_t page_shift;
    uint64_t total_pages;
    struct hakios_ram_bank bank[HAKIOS_RAM_BANK_MAX];
};

#if defined(KERNEL)
extern struct hakios_ram_banks hakios_managed_ram;
uint64_t hakios_ram_pmap_index(uint64_t address);
uint64_t hakios_ram_pmap_pa(uint64_t index);
void hakios_ram_set_vm_first_page(uint64_t ppnum);
uint64_t hakios_ram_vm_page_pa(uint64_t index);
#endif

enum hakios_ram_result {
    HAKIOS_RAM_OK = 0,
    HAKIOS_RAM_INVALID,
    HAKIOS_RAM_OVERLAP,
    HAKIOS_RAM_FULL,
    HAKIOS_RAM_OVERFLOW
};

static inline int
hakios_ram_banks_init(struct hakios_ram_banks *table, uint32_t page_shift)
{
    if (page_shift < 12 || page_shift > 16) return HAKIOS_RAM_INVALID;
    *table = (struct hakios_ram_banks){ .page_shift = page_shift };
    return HAKIOS_RAM_OK;
}

/* Append preserves every previously assigned index. Only forward adjacency to
 * the final bank is merged, so coalescing cannot reorder existing pages. */
static inline int
hakios_ram_bank_add_mode(struct hakios_ram_banks *table, uint64_t base, uint64_t bytes, int merge_adjacent)
{
    if (table->page_shift < 12 || table->page_shift > 16 || table->count > HAKIOS_RAM_BANK_MAX)
        return HAKIOS_RAM_INVALID;
    uint64_t mask = (UINT64_C(1) << table->page_shift) - 1;
    if (!bytes || ((base | bytes) & mask)) return HAKIOS_RAM_INVALID;
    if (bytes > UINT64_MAX - base) return HAKIOS_RAM_OVERFLOW;
    uint64_t end = base + bytes;
    uint64_t pages = bytes >> table->page_shift;
    if (pages > (UINT64_MAX >> table->page_shift) - table->total_pages)
        return HAKIOS_RAM_OVERFLOW;
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct hakios_ram_bank *bank = &table->bank[i];
        uint64_t bank_end = bank->base + (bank->pages << table->page_shift);
        if (base < bank_end && bank->base < end) return HAKIOS_RAM_OVERLAP;
    }
    if (table->count && merge_adjacent) {
        struct hakios_ram_bank *last = &table->bank[table->count - 1];
        if (last->base + (last->pages << table->page_shift) == base) {
            last->pages += pages;
            table->total_pages += pages;
            return HAKIOS_RAM_OK;
        }
    }
    if (table->count == HAKIOS_RAM_BANK_MAX) return HAKIOS_RAM_FULL;
    table->bank[table->count++] = (struct hakios_ram_bank){
        .base = base, .pages = pages, .first_index = table->total_pages };
    table->total_pages += pages;
    return HAKIOS_RAM_OK;
}

static inline int
hakios_ram_bank_add(struct hakios_ram_banks *table, uint64_t base, uint64_t bytes)
{
    return hakios_ram_bank_add_mode(table, base, bytes, 1);
}

/* Accept page-interior addresses, as pmap's pa_valid/pa_index do. */
static inline int
hakios_ram_pa_index(const struct hakios_ram_banks *table, uint64_t address, uint64_t *index)
{
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct hakios_ram_bank *bank = &table->bank[i];
        if (address >= bank->base) {
            uint64_t page = (address - bank->base) >> table->page_shift;
            if (page < bank->pages) {
                *index = bank->first_index + page;
                return 1;
            }
        }
    }
    return 0;
}

static inline int
hakios_ram_index_pa(const struct hakios_ram_banks *table, uint64_t index, uint64_t *address)
{
    if (index >= table->total_pages) return 0;
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct hakios_ram_bank *bank = &table->bank[i];
        if (index >= bank->first_index && index - bank->first_index < bank->pages) {
            *address = bank->base + ((index - bank->first_index) << table->page_shift);
            return 1;
        }
    }
    return 0;
}

/* Bound a physical/DMA operation to one real contiguous bank. */
static inline uint64_t
hakios_ram_contiguous_bytes(const struct hakios_ram_banks *table, uint64_t address)
{
    for (uint32_t i = 0; i < table->count; ++i) {
        const struct hakios_ram_bank *bank = &table->bank[i];
        uint64_t bytes = bank->pages << table->page_shift;
        if (address >= bank->base && address - bank->base < bytes)
            return bytes - (address - bank->base);
    }
    return 0;
}

#endif
