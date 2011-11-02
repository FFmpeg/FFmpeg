/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * fixed-point AC-3 encoder.
 */

#define CONFIG_FFT_FLOAT 0
#undef CONFIG_AC3ENC_FLOAT
#include "ac3enc.h"
#include "eac3enc.h"

#define AC3ENC_TYPE AC3ENC_TYPE_AC3_FIXED
#include "ac3enc_opts_template.c"
static const AVClass ac3enc_class = { "Fixed-Point AC-3 Encoder", av_default_item_name,
                                      ac3fixed_options, LIBAVUTIL_VERSION_INT };

#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 *
 * @param s  AC-3 encoder private context
 */
av_cold void AC3_NAME(mdct_end)(AC3EncodeContext *s)
{
    ff_mdct_end(&s->mdct);
}


/**
 * Initialize MDCT tables.
 *
 * @param s  AC-3 encoder private context
 * @return   0 on success, negative error code on failure
 */
av_cold int AC3_NAME(mdct_init)(AC3EncodeContext *s)
{
    int ret = ff_mdct_init(&s->mdct, 9, 0, -1.0);
    s->mdct_window = ff_ac3_window;
    return ret;
}


/*
 * Apply KBD window to input samples prior to MDCT.
 */
static void apply_window(DSPContext *dsp, int16_t *output, const int16_t *input,
                         const int16_t *window, unsigned int len)
{
    dsp->apply_window_int16(output, input, window, len);
}


/*
 * Normalize the input samples to use the maximum available precision.
 * This assumes signed 16-bit input samples.
 */
static int normalize_samples(AC3EncodeContext *s)
{
    int v = s->ac3dsp.ac3_max_msb_abs_int16(s->windowed_samples, AC3_WINDOW_SIZE);
    v = 14 - av_log2(v);
    if (v > 0)
        s->ac3dsp.ac3_lshift_int16(s->windowed_samples, AC3_WINDOW_SIZE, v);
    /* +6 to right-shift from 31-bit to 25-bit */
    return v + 6;
}


/*
 * Scale MDCT coefficients to 25-bit signed fixed-point.
 */
static void scale_coefficients(AC3EncodeContext *s)
{
    int blk, ch;

    for (blk = 0; blk < s->num_blocks; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->channels; ch++) {
            s->ac3dsp.ac3_rshift_int32(block->mdct_coef[ch], AC3_MAX_COEFS,
                                       block->coeff_shift[ch]);
        }
    }
}


/*
 * Clip MDCT coefficients to allowable range.
 */
static void clip_coefficients(DSPContext *dsp, int32_t *coef, unsigned int len)
{
    dsp->vector_clip_int32(coef, coef, COEF_MIN, COEF_MAX, len);
}


/*
 * Calculate a single coupling coordinate.
 */
static CoefType calc_cpl_coord(CoefSumType energy_ch, CoefSumType energy_cpl)
{
    if (energy_cpl <= COEF_MAX) {
        return 1048576;
    } else {
        uint64_t coord   = energy_ch / (energy_cpl >> 24);
        uint32_t coord32 = FFMIN(coord, 1073741824);
        coord32          = ff_sqrt(coord32) << 9;
        return FFMIN(coord32, COEF_MAX);
    }
}


static av_cold int ac3_fixed_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    s->fixed_point = 1;
    return ff_ac3_encode_init(avctx);
}


AVCodec ff_ac3_fixed_encoder = {
    .name           = "ac3_fixed",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AC3,
    .priv_data_size = sizeof(AC3EncodeContext),
    .init           = ac3_fixed_encode_init,
    .encode         = ff_ac3_fixed_encode_frame,
    .close          = ff_ac3_encode_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .priv_class = &ac3enc_class,
    .channel_layouts = ff_ac3_channel_layouts,
};
