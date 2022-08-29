/*
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
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
 * G.722 ADPCM audio decoder
 *
 * This G.722 decoder is a bit-exact implementation of the ITU G.722
 * specification for all three specified bitrates - 64000bps, 56000bps
 * and 48000bps. It passes the ITU tests.
 *
 * @note For the 56000bps and 48000bps bitrates, the lowest 1 or 2 bits
 *       respectively of each byte are ignored.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"
#include "g722.h"

#define OFFSET(x) offsetof(G722Context, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "bits_per_codeword", "Bits per G722 codeword", OFFSET(bits_per_codeword), AV_OPT_TYPE_INT, { .i64 = 8 }, 6, 8, AD },
    { NULL }
};

static const AVClass g722_decoder_class = {
    .class_name = "g722 decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int g722_decode_init(AVCodecContext * avctx)
{
    G722Context *c = avctx->priv_data;

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;

    c->band[0].scale_factor = 8;
    c->band[1].scale_factor = 2;
    c->prev_samples_pos = 22;

    ff_g722dsp_init(&c->dsp);

    return 0;
}

static const int16_t low_inv_quant5[32] = {
     -35,   -35, -2919, -2195, -1765, -1458, -1219, -1023,
    -858,  -714,  -587,  -473,  -370,  -276,  -190,  -110,
    2919,  2195,  1765,  1458,  1219,  1023,   858,   714,
     587,   473,   370,   276,   190,   110,    35,   -35
};

static const int16_t * const low_inv_quants[3] = { ff_g722_low_inv_quant6,
                                                           low_inv_quant5,
                                                   ff_g722_low_inv_quant4 };

static int g722_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    G722Context *c = avctx->priv_data;
    int16_t *out_buf;
    int j, ret;
    const int skip = 8 - c->bits_per_codeword;
    const int16_t *quantizer_table = low_inv_quants[skip];
    GetBitContext gb;

    /* get output buffer */
    frame->nb_samples = avpkt->size * 2;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;
    out_buf = (int16_t *)frame->data[0];

    ret = init_get_bits8(&gb, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    for (j = 0; j < avpkt->size; j++) {
        int ilow, ihigh, rlow, rhigh, dhigh;
        int xout[2];

        ihigh = get_bits(&gb, 2);
        ilow = get_bits(&gb, 6 - skip);
        skip_bits(&gb, skip);

        rlow = av_clip_intp2((c->band[0].scale_factor * quantizer_table[ilow] >> 10)
                      + c->band[0].s_predictor, 14);

        ff_g722_update_low_predictor(&c->band[0], ilow >> (2 - skip));

        dhigh = c->band[1].scale_factor * ff_g722_high_inv_quant[ihigh] >> 10;
        rhigh = av_clip_intp2(dhigh + c->band[1].s_predictor, 14);

        ff_g722_update_high_predictor(&c->band[1], dhigh, ihigh);

        c->prev_samples[c->prev_samples_pos++] = rlow + rhigh;
        c->prev_samples[c->prev_samples_pos++] = rlow - rhigh;
        c->dsp.apply_qmf(c->prev_samples + c->prev_samples_pos - 24, xout);
        *out_buf++ = av_clip_int16(xout[0] >> 11);
        *out_buf++ = av_clip_int16(xout[1] >> 11);
        if (c->prev_samples_pos >= PREV_SAMPLES_BUF_SIZE) {
            memmove(c->prev_samples, c->prev_samples + c->prev_samples_pos - 22,
                    22 * sizeof(c->prev_samples[0]));
            c->prev_samples_pos = 22;
        }
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

const FFCodec ff_adpcm_g722_decoder = {
    .p.name         = "g722",
    CODEC_LONG_NAME("G.722 ADPCM"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_decode_init,
    FF_CODEC_DECODE_CB(g722_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .p.priv_class   = &g722_decoder_class,
};
