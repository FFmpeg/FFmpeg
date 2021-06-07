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

#define AC3ENC_FLOAT 1
#include "internal.h"
#include "audiodsp.h"
#include "ac3enc.h"
#include "eac3enc.h"
#include "kbdwin.h"


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


/*
 * Clip MDCT coefficients to allowable range.
 */
static void clip_coefficients(AudioDSPContext *adsp, float *coef,
                              unsigned int len)
{
    adsp->vector_clipf(coef, coef, len, COEF_MIN, COEF_MAX);
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

static void sum_square_butterfly(AC3EncodeContext *s, float sum[4],
                                 const float *coef0, const float *coef1,
                                 int len)
{
    s->ac3dsp.sum_square_butterfly_float(sum, coef0, coef1, len);
}


#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 *
 * @param s  AC-3 encoder private context
 */
static av_cold void ac3_float_mdct_end(AC3EncodeContext *s)
{
    ff_mdct_end(&s->mdct);
}


/**
 * Initialize MDCT tables.
 *
 * @param s  AC-3 encoder private context
 * @return   0 on success, negative error code on failure
 */
static av_cold int ac3_float_mdct_init(AC3EncodeContext *s)
{
    float *window = av_malloc_array(AC3_BLOCK_SIZE, sizeof(*window));
    if (!window) {
        av_log(s->avctx, AV_LOG_ERROR, "Cannot allocate memory.\n");
        return AVERROR(ENOMEM);
    }

    ff_kbd_window_init(window, 5.0, AC3_BLOCK_SIZE);
    s->mdct_window = window;

    return ff_mdct_init(&s->mdct, 9, 0, -2.0 / AC3_WINDOW_SIZE);
}


av_cold int ff_ac3_float_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    s->mdct_end                = ac3_float_mdct_end;
    s->mdct_init               = ac3_float_mdct_init;
    s->allocate_sample_buffers = allocate_sample_buffers;
    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);
    return ff_ac3_encode_init(avctx);
}

const AVCodec ff_ac3_encoder = {
    .name            = "ac3",
    .long_name       = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AC3,
    .capabilities    = AV_CODEC_CAP_DR1,
    .priv_data_size  = sizeof(AC3EncodeContext),
    .init            = ff_ac3_float_encode_init,
    .encode2         = ff_ac3_float_encode_frame,
    .close           = ff_ac3_encode_close,
    .sample_fmts     = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .priv_class      = &ff_ac3enc_class,
    .supported_samplerates = ff_ac3_sample_rate_tab,
    .channel_layouts = ff_ac3_channel_layouts,
    .defaults        = ff_ac3_enc_defaults,
    .caps_internal   = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
