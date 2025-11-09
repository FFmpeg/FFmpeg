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

#include "checkasm.h"
#include "libavfilter/vf_fsppdsp.h"
#include "libavutil/mem_internal.h"

#define randomize_buffers(buf)                           \
    do {                                                 \
        for (size_t j = 0; j < FF_ARRAY_ELEMS(buf); ++j) \
            buf[j] = rnd();                              \
    } while (0)


static void check_mul_thrmat(void)
{
    FSPPDSPContext fspp;
    DECLARE_ALIGNED(16, int16_t, src)[64];
    DECLARE_ALIGNED(16, int16_t, dst_ref)[64];
    DECLARE_ALIGNED(16, int16_t, dst_new)[64];
    const int q = (uint8_t)rnd();
    declare_func(void, int16_t *thr_adr_noq, int16_t *thr_adr, int q);

    ff_fsppdsp_init(&fspp);

    if (check_func(fspp.mul_thrmat, "mul_thrmat")) {
        randomize_buffers(src);
        call_ref(src, dst_ref, q);
        call_new(src, dst_new, q);
        if (memcmp(dst_ref, dst_new, sizeof(dst_ref)))
            fail();
        bench_new(src, dst_new, q);
    }
}

void checkasm_check_vf_fspp(void)
{
    check_mul_thrmat();
}
