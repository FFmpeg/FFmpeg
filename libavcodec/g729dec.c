/*
 * G.729 decoder
 * Copyright (c) 2008 Vladimir Voroshilov
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
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "avcodec.h"
#include "libavutil/avutil.h"
#include "bitstream.h"

#include "g729.h"
#include "lsp.h"
#include "celp_math.h"
#include "acelp_filters.h"
#include "acelp_pitch_delay.h"
#include "acelp_vectors.h"
#include "g729data.h"

/**
 * minimum quantized LSF value (3.2.4)
 * 0.005 in Q13
 */
#define LSFQ_MIN                   40

/**
 * maximum quantized LSF value (3.2.4)
 * 3.135 in Q13
 */
#define LSFQ_MAX                   25681

/**
 * minimum LSF distance (3.2.4)
 * 0.0391 in Q13
 */
#define LSFQ_DIFF_MIN              321

/**
 * minimum gain pitch value (3.8, Equation 47)
 * 0.2 in (1.14)
 */
#define SHARP_MIN                  3277

/**
 * maximum gain pitch value (3.8, Equation 47)
 * (EE) This does not comply with the specification.
 * Specification says about 0.8, which should be
 * 13107 in (1.14), but reference C code uses
 * 13017 (equals to 0.7945) instead of it.
 */
#define SHARP_MAX                  13017

typedef struct
{
    int sample_rate;
    uint8_t packed_frame_size;  ///< input frame size(in bytes)
    uint8_t unpacked_frame_size;///< output frame size (in bytes)
    uint8_t fc_indexes_bits;    ///< size (in bits) of fixed-codebook index entry

    /// mr_energy = mean_energy + 10 * log10(2^26  * subframe_size) in (7.13)
    int mr_energy;
} G729_format_description;

/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729_random(uint16_t value)
{
    return 31821 * value + 13849;
}

/**
 * Get parity bit of bit 2..7
 */
static inline int g729_get_parity(uint8_t value)
{
   return (0x6996966996696996ULL >> (value >> 2)) & 1;
}

    if(avctx->channels != 1)
    {
        av_log(avctx, AV_LOG_ERROR, "Only mono sound is supported (requested channels: %d).\n", avctx->channels);
        return AVERROR_NOFMT;
    }

        ff_acelp_weighted_vector_sum(
                fc + pitch_delay_int[i],
                fc + pitch_delay_int[i],
                fc,
                1 << 14,
                av_clip(ctx->gain_pitch, SHARP_MIN, SHARP_MAX),
                0,
                14,
                ctx->subframe_size - pitch_delay_int[i]);

        if(ctx->frame_erasure)
        {
            ctx->gain_pitch = (29491 * ctx->gain_pitch) >> 15; // 0.9 (0.15)
            ctx->gain_code  = (2007 * ctx->gain_code) >> 11;   // 0.98 in (0.11)

            gain_corr_factor = 0;
        }
        else
        {
            ctx->gain_pitch  = cb_gain_1st_8k[parm->gc_1st_index[i]][0] +
                               cb_gain_2nd_8k[parm->gc_2nd_index[i]][0];
            gain_corr_factor = cb_gain_1st_8k[parm->gc_1st_index[i]][1] +
                               cb_gain_2nd_8k[parm->gc_2nd_index[i]][1];

        ff_acelp_weighted_vector_sum(
                ctx->exc + i * ctx->subframe_size,
                ctx->exc + i * ctx->subframe_size,
                fc,
                (!voicing && ctx->frame_erasure) ? 0 : ctx->gain_pitch,
                ( voicing && ctx->frame_erasure) ? 0 : ctx->gain_code,
                1<<13,
                14,
                ctx->subframe_size);

    if (buf_size<packed_frame_size)
    {
        av_log(avctx, AV_LOG_ERROR, "Error processing packet: packet size too small\n");
        return AVERROR(EIO);
    }
    if (*data_size<unpacked_frame_size)
    {
        av_log(avctx, AV_LOG_ERROR, "Error processing packet: output buffer too small\n");
        return AVERROR(EIO);
    }

AVCodec g729_decoder =
{
    "g729",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729,
    sizeof(G729_Context),
    ff_g729_decoder_init,
    NULL,
    NULL,
    ff_g729_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("G.729"),
};
