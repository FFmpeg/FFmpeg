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
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

enum {
    BUF_SIZE = 16384,
};

typedef struct CustomTest {
    struct CustomTest *prev;
    AVCRC ctx[1024];
} CustomTest;

static CustomTest *ctx_list = NULL;

void checkasm_uninit_crc(void)
{
    for (CustomTest *cur = ctx_list; cur;) {
        CustomTest *prev = cur->prev;
        av_free(cur);
        cur = prev;
    }
    ctx_list = NULL;
}

static void check_crc(const AVCRC *table_new, const char *name,
                      size_t size, size_t offset)
{
    declare_func(uint32_t, const AVCRC *ctx, uint32_t crc,
                 const uint8_t *buffer, size_t length);
    const AVCRC *table_ref = (const AVCRC *) check_key((CheckasmKey) table_new, "crc_%s", name);

    if (!table_ref)
        return;

    DECLARE_ALIGNED(4, uint8_t, buf)[BUF_SIZE];
    uint32_t prev_crc = rnd();

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

    size_t offsets[AV_CRC_MAX + 1];
    size_t sizes[AV_CRC_MAX + 1];
    uint32_t poly;
    int le, bits;

    // Initialize parameters before any test so that different instruction sets
    // use the same values.
    for (size_t i = 0; i < FF_ARRAY_ELEMS(offsets); ++i) {
        offsets[i] = rnd() & 31;
        sizes[i]   = rnd() % (BUF_SIZE - 1 - offsets[i]);
    }
    le   = rnd() & 1;
    bits = 8 + rnd() % 25; // av_crc_init() accepts between 8 and 32 bits
    poly = rnd() >> (32 - bits);

    for (unsigned i = 0; i < AV_CRC_MAX; ++i)
        check_crc(av_crc_get_table(i), tests[i], sizes[i], offsets[i]);

    struct CustomTest *new = av_mallocz(sizeof(*new));

    if (!new)
        fail();

    av_assert0(av_crc_init(new->ctx, le, bits, poly, sizeof(new->ctx)) >= 0);
    if (ctx_list && !memcmp(ctx_list->ctx, new->ctx, sizeof(new->ctx))) {
        av_free(new);
    } else {
        new->prev = ctx_list;
        ctx_list = new;
    }

    check_crc(ctx_list->ctx, "custom_polynomial",
              sizes[AV_CRC_MAX], offsets[AV_CRC_MAX]);
    report("crc");
}
