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
#include "internal.h"
#include "ac3enc.h"
#include "eac3enc.h"
#include "kbdwin.h"


#if CONFIG_AC3_ENCODER
#define AC3ENC_TYPE AC3ENC_TYPE_AC3
#include "ac3enc_opts_template.c"
static const AVClass ac3enc_class = {
    .class_name = "AC-3 Encoder",
    .item_name  = av_default_item_name,
    .option     = ac3_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
#endif

#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 *
 * @param s  AC-3 encoder private context
 */
av_cold void ff_ac3_float_mdct_end(AC3EncodeContext *s)
{
    ff_mdct_end(&s->mdct);
    av_freep(&s->mdct_window);
}


/**
 * Initialize MDCT tables.
 *
 * @param s  AC-3 encoder private context
 * @return   0 on success, negative error code on failure
 */
av_cold int ff_ac3_float_mdct_init(AC3EncodeContext *s)
{
    float *window;
    int i, n, n2;

    n  = 1 << 9;
    n2 = n >> 1;

    window = av_malloc(n * sizeof(*window));
    if (!window) {
        av_log(s->avctx, AV_LOG_ERROR, "Cannot allocate memory.\n");
        return AVERROR(ENOMEM);
    }
    ff_kbd_window_init(window, 5.0, n2);
    for (i = 0; i < n2; i++)
        window[n-1-i] = window[i];
    s->mdct_window = window;

    return ff_mdct_init(&s->mdct, 9, 0, -2.0 / n);
}


/*
 * Apply KBD window to input samples prior to MDCT.
 */
static void apply_window(void *dsp, float *output,
                         const float *input, const float *window,
                         unsigned int len)
{
    AVFloatDSPContext *fdsp = dsp;
    fdsp->vector_fmul(output, input, window, len);
}


/*
 * Normalize the input samples.
 * Not needed for the floating-point encoder.
 */
static int normalize_samples(AC3EncodeContext *s)
{
    return 0;
}


/*
 * Scale MDCT coefficients from float to 24-bit fixed-point.
 */
static void scale_coefficients(AC3EncodeContext *s)
{
    int chan_size = AC3_MAX_COEFS * s->num_blocks;
    int cpl       = s->cpl_on;
    s->ac3dsp.float_to_fixed24(s->fixed_coef_buffer + (chan_size * !cpl),
                               s->mdct_coef_buffer  + (chan_size * !cpl),
                               chan_size * (s->channels + cpl));
}

static void sum_square_butterfly(AC3EncodeContext *s, float sum[4],
                                 const float *coef0, const float *coef1,
                                 int len)
{
    s->ac3dsp.sum_square_butterfly_float(sum, coef0, coef1, len);
}

/*
 * Clip MDCT coefficients to allowable range.
 */
static void clip_coefficients(DSPContext *dsp, float *coef, unsigned int len)
{
    dsp->vector_clipf(coef, coef, COEF_MIN, COEF_MAX, len);
}


/*
 * Calculate a single coupling coordinate.
 */
static CoefType calc_cpl_coord(CoefSumType energy_ch, CoefSumType energy_cpl)
{
    float coord = 0.125;
    if (energy_cpl > 0)
        coord *= sqrtf(energy_ch / energy_cpl);
    return FFMIN(coord, COEF_MAX);
}


#if CONFIG_AC3_ENCODER
AVCodec ff_ac3_encoder = {
    .name            = "ac3",
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AC3,
    .priv_data_size  = sizeof(AC3EncodeContext),
    .init            = ff_ac3_encode_init,
    .encode2         = ff_ac3_float_encode_frame,
    .close           = ff_ac3_encode_close,
    .sample_fmts     = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .long_name       = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .priv_class      = &ac3enc_class,
    .channel_layouts = ff_ac3_channel_layouts,
    .defaults        = ac3_defaults,
};
#endif
