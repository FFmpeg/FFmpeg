/*
 * The simplest AC-3 encoder
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2006-2010 Justin Ruggles <justin.ruggles@gmail.com>
 * Copyright (c) 2006-2010 Prakash Punnoor <prakash@punnoor.de>
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
 * @file
 * floating-point AC-3 encoder.
 */

#define CONFIG_AC3ENC_FLOAT 1
#include "ac3enc.h"
#include "eac3enc.h"
#include "kbdwin.h"


#if CONFIG_AC3_ENCODER
#define AC3ENC_TYPE AC3ENC_TYPE_AC3
#include "ac3enc_opts_template.c"
static AVClass ac3enc_class = { "AC-3 Encoder", av_default_item_name,
                                ac3_options, LIBAVUTIL_VERSION_INT };
#endif

#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 */
av_cold void ff_ac3_float_mdct_end(AC3MDCTContext *mdct)
{
    ff_mdct_end(&mdct->fft);
    av_freep(&mdct->window);
}


/**
 * Initialize MDCT tables.
 * @param nbits log2(MDCT size)
 */
av_cold int ff_ac3_float_mdct_init(AVCodecContext *avctx, AC3MDCTContext *mdct,
                                   int nbits)
{
    float *window;
    int i, n, n2;

    n  = 1 << nbits;
    n2 = n >> 1;

    window = av_malloc(n * sizeof(*window));
    if (!window) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate memory.\n");
        return AVERROR(ENOMEM);
    }
    ff_kbd_window_init(window, 5.0, n2);
    for (i = 0; i < n2; i++)
        window[n-1-i] = window[i];
    mdct->window = window;

    return ff_mdct_init(&mdct->fft, nbits, 0, -2.0 / n);
}


/**
 * Apply KBD window to input samples prior to MDCT.
 */
void ff_ac3_float_apply_window(DSPContext *dsp, float *output,
                               const float *input, const float *window,
                               unsigned int len)
{
    dsp->vector_fmul(output, input, window, len);
}


/**
 * Scale MDCT coefficients from float to 24-bit fixed-point.
 */
void ff_ac3_float_scale_coefficients(AC3EncodeContext *s)
{
    int chan_size = AC3_MAX_COEFS * AC3_MAX_BLOCKS;
    s->ac3dsp.float_to_fixed24(s->fixed_coef_buffer + chan_size,
                               s->mdct_coef_buffer  + chan_size,
                               chan_size * s->channels);
}


#if CONFIG_AC3_ENCODER
AVCodec ff_ac3_float_encoder = {
    "ac3_float",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3EncodeContext),
    ff_ac3_encode_init,
    ff_ac3_encode_frame,
    ff_ac3_encode_close,
    NULL,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_FLT,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .priv_class = &ac3enc_class,
    .channel_layouts = ff_ac3_channel_layouts,
};
#endif
