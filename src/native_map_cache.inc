/* SPDX-License-Identifier: GPL-2.0-or-later */
static const uint16_t *lp_active_map;
static const uint16_t *lp_code_pages[32];
static uint8_t lp_plain_read_pages[32];

/* Mapper writes resolve the verified instruction page once per bank switch. */
void lp_refresh_code_page(unsigned page)
{
    if (page >= 32) return;
    lp_code_pages[page] = NULL;
    if (page < 16 || !lp_plain_read_pages[page] || !lp_active_map || !PRGptr[0] || !Page[page])
        return;
    uintptr_t offset = (uintptr_t)Page[page] + page * 2048 - (uintptr_t)PRGptr[0];
    if (offset >= PRGsize[0] || offset >= 0x80000 || PRGsize[0] - offset < 2048)
        return;
    lp_code_pages[page] = lp_active_map + offset;
}

/* Read-handler changes are rare. Validate the whole page here instead of
 * reading the 256 KiB handler table at every translated instruction. A page
 * with even one intercepted byte conservatively uses ordinary execution. */
void lp_note_read_handlers(unsigned start, unsigned end)
{
    if (end < 0x8000 || start > end || start > 0xffff) return;
    if (start < 0x8000) start = 0x8000;
    if (end > 0xffff) end = 0xffff;
    for (unsigned page = start >> 11; page <= (end >> 11); ++page) {
        lp_plain_read_pages[page] = 1;
        for (unsigned address = page * 2048; address < (page + 1) * 2048; ++address)
            if (ARead[address] != CartBR) {
                lp_plain_read_pages[page] = 0;
                break;
            }
        lp_refresh_code_page(page);
    }
}

void lp_select_release(int release)
{
    lp_active_map = (release == 0 || release == 1) ? lp_instruction_map[release] : NULL;
    for (unsigned page = 16; page < 32; ++page)
        lp_refresh_code_page(page);
#ifndef PS2
    memset(&lp_stats, 0, sizeof(lp_stats));
#endif
}

static inline uint16_t lp_mapped_instruction(uint16_t pc)
{
    if (pc < 0x8000 || encryptOpcodes) return 0;
    const uint16_t *page = lp_code_pages[pc >> 11];
    return page ? page[pc & 2047] : 0;
}
