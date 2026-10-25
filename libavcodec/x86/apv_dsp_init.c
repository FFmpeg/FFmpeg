/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include <stddef.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/apv_dsp.h"

#if ARCH_X86_64

typedef struct APVConstantsAVX2 {
    int32_t qmatrix[64];
    int32_t dequant_round[8];
    int32_t output_round[8];
    int32_t qp_shift;
    int32_t bd_shift;
    int32_t output_shift;
} APVConstantsAVX2;

static_assert(sizeof(APVConstantsAVX2) <= APV_DSP_CONSTANTS_SIZE,
              "APV AVX2 constants buffer too small");
static_assert(offsetof(APVConstantsAVX2, dequant_round) == 256 &&
              offsetof(APVConstantsAVX2, output_round)  == 288 &&
              offsetof(APVConstantsAVX2, qp_shift)      == 320 &&
              offsetof(APVConstantsAVX2, bd_shift)      == 324 &&
              offsetof(APVConstantsAVX2, output_shift)  == 328,
              "APV AVX2 constants layout does not match assembly");

static void apv_prepare_constants_avx2(void *constants,
                                       const int16_t *qmatrix,
                                       int bit_depth,
                                       int qp_shift)
{
    APVConstantsAVX2 *c = constants;
    int bd_shift = bit_depth - 2;

    for (int i = 0; i < 64; i++)
        c->qmatrix[i] = (uint16_t)qmatrix[i];
    for (int i = 0; i < 8; i++) {
        c->dequant_round[i] = 1 << (bd_shift - 1);
        c->output_round[i]  = (1 << (19 - bit_depth)) + (1 << 19);
    }
    c->qp_shift     = qp_shift;
    c->bd_shift     = bd_shift;
    c->output_shift = 16 - bit_depth;
}

void ff_apv_decode_transquant_avx2(void *output,
                                   ptrdiff_t pitch,
                                   const int16_t *input,
                                   const void *constants);

av_cold void ff_apv_dsp_init_x86_64(APVDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        dsp->prepare_constants = apv_prepare_constants_avx2;
        dsp->decode_transquant = ff_apv_decode_transquant_avx2;
    }
}

#endif /* ARCH_X86_64 */
