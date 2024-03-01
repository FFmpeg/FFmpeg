/*
 * Common parts of the AAC decoders
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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

#include <limits.h>
#include <stddef.h>

#include "libavcodec/aac.h"
#include "libavcodec/aacsbr.h"
#include "libavcodec/aacdec.h"
#include "libavcodec/avcodec.h"
#include "libavutil/attributes.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "libavutil/version.h"

av_cold int ff_aac_decode_close(AVCodecContext *avctx)
{
    AACDecContext *ac = avctx->priv_data;
    int is_fixed = ac->is_fixed;
    void (*sbr_close)(ChannelElement *che) = is_fixed ? RENAME_FIXED(ff_aac_sbr_ctx_close)
                                                      : ff_aac_sbr_ctx_close;

    for (int type = 0; type < FF_ARRAY_ELEMS(ac->che); type++) {
        for (int i = 0; i < MAX_ELEM_ID; i++) {
            if (ac->che[type][i]) {
                sbr_close(ac->che[type][i]);
                av_freep(&ac->che[type][i]);
            }
        }
    }

    av_tx_uninit(&ac->mdct120);
    av_tx_uninit(&ac->mdct128);
    av_tx_uninit(&ac->mdct480);
    av_tx_uninit(&ac->mdct512);
    av_tx_uninit(&ac->mdct960);
    av_tx_uninit(&ac->mdct1024);
    av_tx_uninit(&ac->mdct_ltp);

    // Compiler will optimize this branch away.
    if (is_fixed)
        av_freep(&ac->RENAME_FIXED(fdsp));
    else
        av_freep(&ac->fdsp);

    return 0;
}

av_cold int ff_aac_decode_init_common(AVCodecContext *avctx)
{
    AACDecContext *ac = avctx->priv_data;
    int is_fixed = ac->is_fixed, ret;
    float scale_fixed, scale_float;
    const float *const scalep = is_fixed ? &scale_fixed : &scale_float;
    enum AVTXType tx_type = is_fixed ? AV_TX_INT32_MDCT : AV_TX_FLOAT_MDCT;

    if (avctx->ch_layout.nb_channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Too many channels\n");
        return AVERROR_INVALIDDATA;
    }

    ac->random_state = 0x1f2e3d4c;

#define MDCT_INIT(s, fn, len, sval)                                          \
    scale_fixed = (sval) * 128.0f;                                           \
    scale_float = (sval) / 32768.0f;                                         \
    ret = av_tx_init(&s, &fn, tx_type, 1, len, scalep, 0);                   \
    if (ret < 0)                                                             \
        return ret

    MDCT_INIT(ac->mdct120,  ac->mdct120_fn,   120, 1.0/120);
    MDCT_INIT(ac->mdct128,  ac->mdct128_fn,   128, 1.0/128);
    MDCT_INIT(ac->mdct480,  ac->mdct480_fn,   480, 1.0/480);
    MDCT_INIT(ac->mdct512,  ac->mdct512_fn,   512, 1.0/512);
    MDCT_INIT(ac->mdct960,  ac->mdct960_fn,   960, 1.0/960);
    MDCT_INIT(ac->mdct1024, ac->mdct1024_fn, 1024, 1.0/1024);
#undef MDCT_INIT

    /* LTP forward MDCT */
    scale_fixed = -1.0;
    scale_float = -32786.0*2 + 36;
    ret = av_tx_init(&ac->mdct_ltp, &ac->mdct_ltp_fn, tx_type, 0, 1024, scalep, 0);
    if (ret < 0)
        return ret;

    return 0;
}

#define AACDEC_FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
#define OFF(field) offsetof(AACDecContext, field)
static const AVOption options[] = {
    /**
     * AVOptions for Japanese DTV specific extensions (ADTS only)
     */
    {"dual_mono_mode", "Select the channel to decode for dual mono",
     OFF(force_dmono_mode), AV_OPT_TYPE_INT, {.i64=-1}, -1, 2,
     AACDEC_FLAGS, .unit = "dual_mono_mode"},

    {"auto", "autoselection",            0, AV_OPT_TYPE_CONST, {.i64=-1}, INT_MIN, INT_MAX, AACDEC_FLAGS, .unit = "dual_mono_mode"},
    {"main", "Select Main/Left channel", 0, AV_OPT_TYPE_CONST, {.i64= 1}, INT_MIN, INT_MAX, AACDEC_FLAGS, .unit = "dual_mono_mode"},
    {"sub" , "Select Sub/Right channel", 0, AV_OPT_TYPE_CONST, {.i64= 2}, INT_MIN, INT_MAX, AACDEC_FLAGS, .unit = "dual_mono_mode"},
    {"both", "Select both channels",     0, AV_OPT_TYPE_CONST, {.i64= 0}, INT_MIN, INT_MAX, AACDEC_FLAGS, .unit = "dual_mono_mode"},

    { "channel_order", "Order in which the channels are to be exported",
        OFF(output_channel_order), AV_OPT_TYPE_INT,
        { .i64 = CHANNEL_ORDER_DEFAULT }, 0, 1, AACDEC_FLAGS, .unit = "channel_order" },
      { "default", "normal libavcodec channel order", 0, AV_OPT_TYPE_CONST,
        { .i64 = CHANNEL_ORDER_DEFAULT }, .flags = AACDEC_FLAGS, .unit = "channel_order" },
      { "coded",    "order in which the channels are coded in the bitstream",
        0, AV_OPT_TYPE_CONST, { .i64 = CHANNEL_ORDER_CODED }, .flags = AACDEC_FLAGS, .unit = "channel_order" },

    {NULL},
};

const AVClass ff_aac_decoder_class = {
    .class_name = "AAC decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
