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
#include "audiodsp.h"
#include "ac3enc.h"
#include "codec_internal.h"
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
 * Initialize MDCT tables.
 *
 * @param s  AC-3 encoder private context
 * @return   0 on success, negative error code on failure
 */
static av_cold int ac3_float_mdct_init(AC3EncodeContext *s)
{
    const float scale = -2.0 / AC3_WINDOW_SIZE;

    ff_kbd_window_init(s->mdct_window_float, 5.0, AC3_BLOCK_SIZE);

    return av_tx_init(&s->tx, &s->tx_fn, AV_TX_FLOAT_MDCT, 0,
                      AC3_BLOCK_SIZE, &scale, 0);
}


av_cold int ff_ac3_float_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    int ret;

    s->encode_frame            = encode_frame;
    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    ret = ac3_float_mdct_init(s);
    if (ret < 0)
        return ret;

    return ff_ac3_encode_init(avctx);
}

const FFCodec ff_ac3_encoder = {
    .p.name          = "ac3",
    CODEC_LONG_NAME("ATSC A/52A (AC-3)"),
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_AC3,
    .p.capabilities  = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size  = sizeof(AC3EncodeContext),
    .init            = ff_ac3_float_encode_init,
    FF_CODEC_ENCODE_CB(ff_ac3_encode_frame),
    .close           = ff_ac3_encode_close,
    .p.sample_fmts   = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .p.priv_class    = &ff_ac3enc_class,
    .p.supported_samplerates = ff_ac3_sample_rate_tab,
    .p.ch_layouts    = ff_ac3_ch_layouts,
    .defaults        = ff_ac3_enc_defaults,
    .caps_internal   = FF_CODEC_CAP_INIT_CLEANUP,
};
