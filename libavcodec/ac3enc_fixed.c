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
 * fixed-point AC-3 encoder.
 */

#define AC3ENC_FLOAT 0
#define FFT_FLOAT 0
#include "internal.h"
#include "audiodsp.h"
#include "ac3enc.h"
#include "eac3enc.h"
#include "kbdwin.h"

static void sum_square_butterfly(AC3EncodeContext *s, int64_t sum[4],
                                 const int32_t *coef0, const int32_t *coef1,
                                 int len)
{
    s->ac3dsp.sum_square_butterfly_int32(sum, coef0, coef1, len);
}

/*
 * Clip MDCT coefficients to allowable range.
 */
static void clip_coefficients(AudioDSPContext *adsp, int32_t *coef,
                              unsigned int len)
{
    adsp->vector_clip_int32(coef, coef, COEF_MIN, COEF_MAX, len);
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


#include "ac3enc_template.c"


/**
 * Finalize MDCT and free allocated memory.
 *
 * @param s  AC-3 encoder private context
 */
static av_cold void ac3_fixed_mdct_end(AC3EncodeContext *s)
{
    ff_mdct_end(&s->mdct);
}

/**
 * Initialize MDCT tables.
 *
 * @param s  AC-3 encoder private context
 * @return   0 on success, negative error code on failure
 */
static av_cold int ac3_fixed_mdct_init(AC3EncodeContext *s)
{
    float fwin[AC3_BLOCK_SIZE];

    int32_t *iwin = av_malloc_array(AC3_BLOCK_SIZE, sizeof(*iwin));
    if (!iwin)
        return AVERROR(ENOMEM);

    ff_kbd_window_init(fwin, 5.0, AC3_BLOCK_SIZE);
    for (int i = 0; i < AC3_BLOCK_SIZE; i++)
        iwin[i] = lrintf(fwin[i] * (1 << 22));

    s->mdct_window = iwin;

    s->fdsp = avpriv_alloc_fixed_dsp(s->avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return ff_mdct_init(&s->mdct, 9, 0, -1.0);
}


static av_cold int ac3_fixed_encode_init(AVCodecContext *avctx)
{
    AC3EncodeContext *s = avctx->priv_data;
    s->fixed_point = 1;
    s->mdct_end                = ac3_fixed_mdct_end;
    s->mdct_init               = ac3_fixed_mdct_init;
    s->allocate_sample_buffers = allocate_sample_buffers;
    return ff_ac3_encode_init(avctx);
}


const AVCodec ff_ac3_fixed_encoder = {
    .name            = "ac3_fixed",
    .long_name       = NULL_IF_CONFIG_SMALL("ATSC A/52A (AC-3)"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_AC3,
    .capabilities    = AV_CODEC_CAP_DR1,
    .priv_data_size  = sizeof(AC3EncodeContext),
    .init            = ac3_fixed_encode_init,
    .encode2         = ff_ac3_fixed_encode_frame,
    .close           = ff_ac3_encode_close,
    .sample_fmts     = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S32P,
                                                      AV_SAMPLE_FMT_NONE },
    .priv_class      = &ff_ac3enc_class,
    .caps_internal   = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .supported_samplerates = ff_ac3_sample_rate_tab,
    .channel_layouts = ff_ac3_channel_layouts,
    .defaults        = ff_ac3_enc_defaults,
};
