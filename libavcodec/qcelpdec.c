/*
 * QCELP decoder
 * Copyright (c) 2007 Reynaldo H. Verdejo Pinochet
 *
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
/**
 * @file qcelpdec.c
 * QCELP decoder
 * @author Reynaldo H. Verdejo Pinochet
 */

#include <stddef.h>

#include "avcodec.h"
#include "bitstream.h"

#include "qcelp.h"
#include "qcelpdata.h"

#include "celp_math.h"
#include "celp_filters.h"

#undef NDEBUG
#include <assert.h>

static void weighted_vector_sumf(float *out,
                                 const float *in_a,
                                 const float *in_b,
                                 float weight_coeff_a,
                                 float weight_coeff_b,
                                 int length) {
    int   i;

    for (i = 0; i < length; i++)
        out[i] = weight_coeff_a * in_a[i]
               + weight_coeff_b * in_b[i];
}

/**
 * Apply filter in pitch-subframe steps.
 *
 * @param memory buffer for the previous state of the filter
 *        - must be able to contain 303 elements
 *        - the 143 first elements are from the previous state
 *        - the next 160 are for output
 * @param v_in input filter vector
 * @param gain per-subframe gain array, each element is between 0.0 and 2.0
 * @param lag per-subframe lag array, each element is
 *        - between 16 and 143 if its corresponding pfrac is 0,
 *        - between 16 and 139 otherwise
 * @param pfrac per-subframe boolean array, 1 if the lag is fractional, 0 otherwise
 *
 * @return filter output vector
 */
static const float *do_pitchfilter(float memory[303],
                                   const float v_in[160],
                                   const float gain[4],
                                   const uint8_t *lag,
                                   const uint8_t pfrac[4]) {
    int         i, j;
    float       *v_lag, *v_out;
    const float *v_len;

    v_out = memory + 143; // Output vector starts at memory[143].

    for (i = 0; i < 4; i++)
        if (gain[i]) {
            v_lag = memory + 143 + 40 * i - lag[i];
            for (v_len = v_in + 40; v_in < v_len; v_in++) {
                if (pfrac[i]) { // If it is a fractional lag...
                    for (j = 0, *v_out = 0.; j < 4; j++)
                        *v_out += qcelp_hammsinc_table[j] * (v_lag[j-4] + v_lag[3-j]);
                } else
                    *v_out = *v_lag;

                *v_out = *v_in + gain[i] * *v_out;

                v_lag++;
                v_out++;
            }
        } else {
            memcpy(v_out, v_in, 40 * sizeof(float));
            v_in  += 40;
            v_out += 40;
        }

    memmove(memory, memory + 160, 143 * sizeof(float));
    return memory + 143;
}

/**
 * Interpolates LSP frequencies and computes LPC coefficients
 * for a given framerate & pitch subframe.
 *
 * TIA/EIA/IS-733 2.4.3.3.4
 *
 * @param q the context
 * @param curr_lspf LSP frequencies vector of the current frame
 * @param lpc float vector for the resulting LPC
 * @param subframe_num frame number in decoded stream
 */
void interpolate_lpc(QCELPContext *q,
                     const float *curr_lspf,
                     float *lpc,
                     const int subframe_num) {
    float interpolated_lspf[10];
    float weight;

    if (q->framerate >= RATE_QUARTER) {
        weight = 0.25 * (subframe_num + 1);
    } else if (q->framerate == RATE_OCTAVE && !subframe_num) {
        weight = 0.625;
    } else {
        weight = 1.0;
    }

    if (weight != 1.0) {
        weighted_vector_sumf(interpolated_lspf, curr_lspf, q->prev_lspf, weight, 1.0 - weight, 10);
        lspf2lpc(q, interpolated_lspf, lpc);
    } else if (q->framerate >= RATE_QUARTER || (q->framerate == I_F_Q && !subframe_num))
        lspf2lpc(q, curr_lspf, lpc);
}

static int buf_size2framerate(const int buf_size) {
    switch (buf_size) {
    case 35:
        return RATE_FULL;
    case 17:
        return RATE_HALF;
    case  8:
        return RATE_QUARTER;
    case  4:
        return RATE_OCTAVE;
    case  1:
        return SILENCE;
    }
    return -1;
}

static void warn_insufficient_frame_quality(AVCodecContext *avctx,
                                            const char *message) {
    av_log(avctx, AV_LOG_WARNING, "Frame #%d, IFQ: %s\n", avctx->frame_number, message);
}

AVCodec qcelp_decoder =
{
    .name   = "qcelp",
    .type   = CODEC_TYPE_AUDIO,
    .id     = CODEC_ID_QCELP,
    .init   = qcelp_decode_init,
    .decode = qcelp_decode_frame,
    .priv_data_size = sizeof(QCELPContext),
    .long_name = NULL_IF_CONFIG_SMALL("QCELP / PureVoice"),
};
