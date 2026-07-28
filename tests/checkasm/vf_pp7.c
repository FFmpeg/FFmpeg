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
#include <string.h>

#include "checkasm.h"
#include "libavfilter/vf_pp7dsp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define randomize_buffer(buf)                                      \
    do {                                                           \
        static_assert(!(sizeof(buf) % 4), "Tail handling needed"); \
        for (size_t k = 0; k < sizeof(buf); k += 4) {              \
            AV_WN32A((char*)buf + k, rnd());                       \
        }                                                          \
    } while (0)

static void check_dctB(const PP7DSPContext *const pp7dsp)
{
    declare_func(void, int16_t *dst, const int16_t *src);

    if (!check_func(pp7dsp->dctB, "dctB"))
        return;

    DECLARE_ALIGNED(8, int16_t, src)[7 * 4];
    DECLARE_ALIGNED(8, int16_t, dst_ref)[6 * 4];
    DECLARE_ALIGNED(8, int16_t, dst_new)[6 * 4];

    randomize_buffer(src);
    randomize_buffer(dst_ref);
    memcpy(dst_new, dst_ref, sizeof(dst_new));
    call_ref(dst_ref, src);
    call_new(dst_new, src);
    if (memcmp(dst_new, dst_ref, sizeof(dst_new)))
        fail();

    bench_new(dst_new, src);
}

void checkasm_check_vf_pp7(void)
{
    PP7DSPContext pp7dsp;

    ff_pp7dsp_init(&pp7dsp);

    check_dctB(&pp7dsp);
    report("dctB");
}
