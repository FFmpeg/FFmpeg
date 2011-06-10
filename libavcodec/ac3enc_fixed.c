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

#define AC3ENC_TYPE AC3ENC_TYPE_AC3_FIXED
#include "ac3enc_opts_template.c"
static AVClass ac3enc_class = { "Fixed-Point AC-3 Encoder", av_default_item_name,
                                ac3fixed_options, LIBAVUTIL_VERSION_INT };

#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 */
av_cold void AC3_NAME(mdct_end)(AC3MDCTContext *mdct)
{
    ff_mdct_end(&mdct->fft);
}


/**
 * Initialize MDCT tables.
 * @param nbits log2(MDCT size)
 */
av_cold int AC3_NAME(mdct_init)(AVCodecContext *avctx, AC3MDCTContext *mdct,
                                int nbits)
{
    int ret = ff_mdct_init(&mdct->fft, nbits, 0, -1.0);
    mdct->window = ff_ac3_window;
    return ret;
}


/**
 * Apply KBD window to input samples prior to MDCT.
 */
void AC3_NAME(apply_window)(DSPContext *dsp, int16_t *output,
                            const int16_t *input, const int16_t *window,
                            unsigned int len)
{
    dsp->apply_window_int16(output, input, window, len);
}


/**
 * Calculate the log2() of the maximum absolute value in an array.
 * @param tab input array
 * @param n   number of values in the array
 * @return    log2(max(abs(tab[])))
 */
static int log2_tab(AC3EncodeContext *s, int16_t *src, int len)
{
    int v = s->ac3dsp.ac3_max_msb_abs_int16(src, len);
    return av_log2(v);
}


/**
 * Normalize the input samples to use the maximum available precision.
 * This assumes signed 16-bit input samples.
 *
 * @return exponent shift
 */
int AC3_NAME(normalize_samples)(AC3EncodeContext *s)
{
    int v = 14 - log2_tab(s, s->windowed_samples, AC3_WINDOW_SIZE);
    if (v > 0)
        s->ac3dsp.ac3_lshift_int16(s->windowed_samples, AC3_WINDOW_SIZE, v);
    /* +6 to right-shift from 31-bit to 25-bit */
    return v + 6;
}


/**
 * Scale MDCT coefficients to 25-bit signed fixed-point.
 */
void AC3_NAME(scale_coefficients)(AC3EncodeContext *s)
{
    int blk, ch;

    for (blk = 0; blk < AC3_MAX_BLOCKS; blk++) {
        AC3Block *block = &s->blocks[blk];
        for (ch = 1; ch <= s->channels; ch++) {
            s->ac3dsp.ac3_rshift_int32(block->mdct_coef[ch], AC3_MAX_COEFS,
                                       block->coeff_shift[ch]);
        }
    }
}


static av_cold int ac3_fixed_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    s->fixed_point = 1;
    return ff_ac3_encode_init(avctx);
}


AVCodec ff_ac3_fixed_encoder = {
    "ac3_fixed",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3EncodeContext),
    ac3_fixed_encode_init,
    ff_ac3_encode_frame,
    ff_ac3_encode_close,
    NULL,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .priv_class = &ac3enc_class,
    .channel_layouts = ff_ac3_channel_layouts,
};
