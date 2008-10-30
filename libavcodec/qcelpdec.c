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

static void warn_insufficient_frame_quality(AVCodecContext *avctx,
                                            const char *message) {
    av_log(avctx, AV_LOG_WARNING, "Frame #%d, IFQ: %s\n", avctx->frame_number, message);
}
