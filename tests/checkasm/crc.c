/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "checkasm.h"
#include "libavutil/attributes.h"
// Undefine av_pure so that calls to av_crc are not optimized away.
#undef av_pure
#define av_pure
#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"


static void check_crc(const AVCRC *table_new, const char *name, unsigned idx)
{
    declare_func(uint32_t, const AVCRC *ctx, uint32_t crc,
                 const uint8_t *buffer, size_t length);
    const AVCRC *table_ref = check_key((AVCRC*)table_new, "crc_%s", name);

    if (!table_ref)
        return;

    DECLARE_ALIGNED(4, uint8_t, buf)[8192];
    size_t offset = rnd() & 31;
    static size_t sizes[AV_CRC_MAX + 1];
    static unsigned sizes_initialized = 0;
    uint32_t prev_crc = rnd();

    if (!(sizes_initialized & (1 << idx))) {
        sizes_initialized |= 1 << idx;
        sizes[idx] = rnd() % (sizeof(buf) - 1 - offset);
    }

    size_t size = sizes[idx];

    for (size_t j = 0; j < sizeof(buf); j += 4)
        AV_WN32A(buf + j, rnd());

    uint32_t crc_ref = checkasm_call        (av_crc, table_ref, prev_crc, buf + offset, size);
    uint32_t crc_new = checkasm_call_checked(av_crc, table_new, prev_crc, buf + offset, size);

    if (crc_ref != crc_new)
        fail();

    bench(av_crc, table_new, prev_crc, buf + offset, size);
}

void checkasm_check_crc(void)
{
    static const char *const tests[] = {
#define TEST(CRC) [AV_CRC_ ## CRC] = #CRC
        TEST(8_ATM),   TEST(8_EBU),
        TEST(16_ANSI), TEST(16_ANSI_LE), TEST(16_CCITT),
        TEST(24_IEEE), TEST(32_IEEE_LE), TEST(32_IEEE),
    };
    static_assert(FF_ARRAY_ELEMS(tests) == AV_CRC_MAX, "test needs to be added");

    for (unsigned i = 0; i < AV_CRC_MAX; ++i)
        check_crc(av_crc_get_table(i), tests[i], i);

    static struct CustomTest {
        struct CustomTest *prev;
        AVCRC ctx[1024];
    } *ctx = NULL;
    struct CustomTest *new = malloc(sizeof(*new));
    static int le, bits;
    static uint32_t poly;

    if (!new)
        fail();

    memset(new, 0, sizeof(*new));

    if (!ctx) {
        le   = rnd() & 1;
        bits = 8 + rnd() % 25; // av_crc_init() accepts between 8 and 32 bits
        poly = rnd() >> (32 - bits);
    }
    av_assert0(av_crc_init(new->ctx, le, bits, poly, sizeof(new->ctx)) >= 0);
    if (ctx && !memcmp(ctx->ctx, new->ctx, sizeof(new->ctx))) {
        free(new);
    } else {
        new->prev = ctx;
        ctx = new;
    }

    check_crc(ctx->ctx, "custom_polynomial", AV_CRC_MAX);
    report("crc");
}
