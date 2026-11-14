/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include "platform.h"

#include "common/crc.h"
#include "common/maths.h"

#include "pg.h"

const pgRegistry_t* pgFind(pgn_t pgn)
{
    PG_FOREACH(reg) {
        if (pgN(reg) == pgn) {
            return reg;
        }
    }
    return NULL;
}

static uint8_t *pgOffset(const pgRegistry_t* reg)
{
    return reg->address;
}

void pgResetInstance(const pgRegistry_t *reg, uint8_t *base)
{
    const uint16_t regSize = pgSize(reg);

    for (uint16_t i = 0; i < regSize; i++) {
        base[i] = 0;
    }

    if (reg->reset.ptr >= (void*)__pg_resetdata_start && reg->reset.ptr < (void*)__pg_resetdata_end) {
        const uint8_t *src = (const uint8_t *)reg->reset.ptr;
        for (uint16_t i = 0; i < regSize; i++) {
            base[i] = src[i];
        }
    } else if (reg->reset.fn) {
        // reset function, call it
        reg->reset.fn(base);
    }
}

void pgReset(const pgRegistry_t* reg)
{
    pgResetInstance(reg, pgOffset(reg));
}

bool pgResetCopy(void *copy, pgn_t pgn)
{
    const pgRegistry_t *reg = pgFind(pgn);
    if (reg) {
        pgResetInstance(reg, copy);
        return true;
    }
    return false;
}

bool pgLoad(const pgRegistry_t* reg, const void *from, int size, int version)
{
    pgResetInstance(reg, pgOffset(reg));
    // restore only matching version, keep defaults otherwise
    if (version == pgVersion(reg)) {
        const int take = MIN(size, pgSize(reg));
        memcpy(pgOffset(reg), from, take);

        *reg->fnv_hash = fnv_update(FNV_OFFSET_BASIS, from, take);

        return true;
    }

    return false;
}

int pgStore(const pgRegistry_t* reg, void *to, int size)
{
    const int take = MIN(size, pgSize(reg));
    memcpy(to, pgOffset(reg), take);
    return take;
}

void pgResetAll(void)
{
    THEJAS_LOG("[PG] pgResetAll: start\r\n");
#ifdef THEJAS32
    int idx = 0;
    PG_FOREACH(reg) {
        thejas32_early_uart_puts("[PG] reset #");
        thejas32_early_uart_print_hex((uint32_t)idx);
        thejas32_early_uart_puts(" pgn=0x");
        thejas32_early_uart_print_hex((uint32_t)pgN(reg));
        thejas32_early_uart_puts(" ... ");
        pgReset(reg);
        thejas32_early_uart_puts("done\r\n");
        idx++;
    }
#else
    PG_FOREACH(reg) {
        pgReset(reg);
    }
#endif
    THEJAS_LOG("[PG] pgResetAll: complete\r\n");
}
