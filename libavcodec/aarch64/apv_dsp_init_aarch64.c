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
#include <string.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/apv_dsp.h"
#include "apv_dsp_constants.h"

typedef struct APVConstantsNEON {
    int16_t qmatrix[64];
    int32_t qp_shift;
    int32_t dequant_round;
    int32_t neg_bd_shift;
    int32_t output_round;
    int32_t output_max;
    int32_t output_shift;
} APVConstantsNEON;

static_assert(sizeof(APVConstantsNEON) <= APV_DSP_CONSTANTS_SIZE,
              "APV NEON constants buffer too small");
static_assert(offsetof(APVConstantsNEON, qmatrix)       == APV_CONST_QMATRIX       &&
              offsetof(APVConstantsNEON, qp_shift)      == APV_CONST_QP_SHIFT      &&
              offsetof(APVConstantsNEON, dequant_round) == APV_CONST_DEQUANT_ROUND &&
              offsetof(APVConstantsNEON, neg_bd_shift)  == APV_CONST_NEG_BD_SHIFT  &&
              offsetof(APVConstantsNEON, output_round)  == APV_CONST_OUTPUT_ROUND  &&
              offsetof(APVConstantsNEON, output_max)    == APV_CONST_OUTPUT_MAX    &&
              offsetof(APVConstantsNEON, output_shift)  == APV_CONST_OUTPUT_SHIFT,
              "APV NEON constants layout does not match assembly");

static void apv_prepare_constants_neon(void *constants,
                                       const int16_t *qmatrix,
                                       int bit_depth,
                                       int qp_shift)
{
    APVConstantsNEON *c = constants;
    int bd_shift = bit_depth - 2;

    memcpy(c->qmatrix, qmatrix, sizeof(c->qmatrix));
    c->qp_shift      = qp_shift;
    c->dequant_round = 1 << (bd_shift - 1);
    c->neg_bd_shift  = -bd_shift;
    c->output_round  = (1 << (19 - bit_depth)) + (1 << 19);
    c->output_max    = (1 << bit_depth) - 1;
    c->output_shift  = bit_depth - 20;
}

void ff_apv_decode_transquant_neon(void *output,
                                   ptrdiff_t pitch,
                                   const int16_t *input,
                                   const void *constants);

av_cold void ff_apv_dsp_init_aarch64(APVDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        dsp->prepare_constants = apv_prepare_constants_neon;
        dsp->decode_transquant = ff_apv_decode_transquant_neon;
    }
}
