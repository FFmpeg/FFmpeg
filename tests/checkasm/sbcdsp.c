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

#include <stddef.h>
#include <stdint.h>

#include "checkasm.h"
#include "libavcodec/sbcdsp.h"
#include "libavcodec/sbcdsp_data.h"
#include "libavutil/macros.h"
#include "libavutil/mem_internal.h"

enum {
    SBC_MAX_SUBBANDS = 8,
};

#define randomize_buffer(buf)                        \
do {                                                 \
    for (size_t k = 0; k < FF_ARRAY_ELEMS(buf); ++k) \
        buf[k] = rnd();                              \
} while (0)

static void check_sbc_analyze(SBCDSPContext *sbcdsp)
{
    DECLARE_ALIGNED(SBC_ALIGN, int16_t, in)[10 * SBC_MAX_SUBBANDS];
    DECLARE_ALIGNED(SBC_ALIGN, int32_t, out_ref)[SBC_MAX_SUBBANDS];
    DECLARE_ALIGNED(SBC_ALIGN, int32_t, out_new)[SBC_MAX_SUBBANDS];

    declare_func(void, const int16_t *in, int32_t *out, const int16_t *consts);

    for (int i = 0; i < 2; ++i) {
        if (check_func(i ? sbcdsp->sbc_analyze_8 : sbcdsp->sbc_analyze_4, "sbc_analyze_%u", i ? 8 : 4)) {
            randomize_buffer(in);
            randomize_buffer(out_ref);
            memcpy(out_new, out_ref, sizeof(out_new));

            // the input is always 16-byte aligned for sbc_analyze_8
            // for sbc_analyze_4 it can be 0 or 8 mod 16.
            const int16_t *const inp = i || rnd() & 1 ? in : in + 4;
            // Use the proper const tables as random ones can cause overflow
            #define CONST(SIZE, ODDEVEN) sbcdsp_analysis_consts_fixed ## SIZE ## _simd_ ## ODDEVEN
            const int16_t *const consts = rnd() & 1 ? (i ? CONST(8, odd)  : CONST(4, odd))
                                                    : (i ? CONST(8, even) : CONST(4, even));

            call_ref(inp, out_ref, consts);
            call_new(inp, out_new, consts);

            if (memcmp(out_ref, out_new, sizeof(out_new)))
                fail();

            bench_new(inp, out_new, consts);
        }
    }
    report("sbc_analyze");
}

static void check_sbc_calc_scalefactors(const SBCDSPContext *const sbcdsp)
{
    DECLARE_ALIGNED(SBC_ALIGN,  int32_t, sb_sample_f)[16][2][8];
    DECLARE_ALIGNED(SBC_ALIGN, uint32_t, scale_factor_ref)[2][8];
    DECLARE_ALIGNED(SBC_ALIGN, uint32_t, scale_factor_new)[2][8];

    declare_func(void, const int32_t sb_sample_f[16][2][8],
                       uint32_t scale_factor[2][8],
                       int blocks, int channels, int subbands);

    static int blocks = 0;
    if (!blocks)
        blocks = ((const int[]){4, 8, 12, 15, 16})[rnd() % 5];
    int inited = 0;

    for (int ch = 1; ch <= 2; ++ch) {
        for (int subbands = 4; subbands <= 8; subbands += 4) {
            if (!check_func(sbcdsp->sbc_calc_scalefactors, "calc_scalefactors_%dch_%dsubbands", ch, subbands))
                return;

            if (!inited) {
                for (size_t i = 0; i < FF_ARRAY_ELEMS(sb_sample_f); ++i)
                    for (size_t j = 0; j < FF_ARRAY_ELEMS(sb_sample_f[0]); ++j)
                        for (size_t k = 0; k < FF_ARRAY_ELEMS(sb_sample_f[0][0]); ++k)
                            sb_sample_f[i][j][k] = rnd();
            }

            call_ref(sb_sample_f, scale_factor_ref, blocks, ch, subbands);
            call_new(sb_sample_f, scale_factor_new, blocks, ch, subbands);
            for (int i = 0; i < ch; ++i)
                for (int j = 0; j < subbands; ++j)
                    if (scale_factor_ref[i][j] != scale_factor_new[i][j])
                        fail();

            bench_new(sb_sample_f, scale_factor_new, blocks, ch, subbands);
        }
    }
}

void checkasm_check_sbcdsp(void)
{
    SBCDSPContext sbcdsp;

    ff_sbcdsp_init(&sbcdsp);

    check_sbc_analyze(&sbcdsp);

    check_sbc_calc_scalefactors(&sbcdsp);
    report("calc_scalefactors");
}
