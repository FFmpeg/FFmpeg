/*
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
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
 * G.722 ADPCM audio decoder
 *
 * This G.722 decoder is a bit-exact implementation of the ITU G.722
 * specification for all three specified bitrates - 64000bps, 56000bps
 * and 48000bps. It passes the ITU tests.
 *
 * @note For the 56000bps and 48000bps bitrates, the lowest 1 or 2 bits
 *       respectively of each byte are ignored.
 */

#include "avcodec.h"
#include "get_bits.h"
#include "g722.h"
#include "libavutil/opt.h"

#define OFFSET(x) offsetof(G722Context, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "bits_per_codeword", "Bits per G722 codeword", OFFSET(bits_per_codeword), AV_OPT_TYPE_FLAGS, { .i64 = 8 }, 6, 8, AD },
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

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono tracks are allowed.\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    c->band[0].scale_factor = 8;
    c->band[1].scale_factor = 2;
    c->prev_samples_pos = 22;

    avcodec_get_frame_defaults(&c->frame);
    avctx->coded_frame = &c->frame;

    return 0;
}

static const int16_t low_inv_quant5[32] = {
     -35,   -35, -2919, -2195, -1765, -1458, -1219, -1023,
    -858,  -714,  -587,  -473,  -370,  -276,  -190,  -110,
    2919,  2195,  1765,  1458,  1219,  1023,   858,   714,
     587,   473,   370,   276,   190,   110,    35,   -35
};

static const int16_t *low_inv_quants[3] = { ff_g722_low_inv_quant6,
                                                    low_inv_quant5,
                                            ff_g722_low_inv_quant4 };

static int g722_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    G722Context *c = avctx->priv_data;
    int16_t *out_buf;
    int j, ret;
    const int skip = 8 - c->bits_per_codeword;
    const int16_t *quantizer_table = low_inv_quants[skip];
    GetBitContext gb;

    /* get output buffer */
    c->frame.nb_samples = avpkt->size * 2;
    if ((ret = avctx->get_buffer(avctx, &c->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    out_buf = (int16_t *)c->frame.data[0];

    init_get_bits(&gb, avpkt->data, avpkt->size * 8);

    for (j = 0; j < avpkt->size; j++) {
        int ilow, ihigh, rlow, rhigh, dhigh;
        int xout1, xout2;

        ihigh = get_bits(&gb, 2);
        ilow = get_bits(&gb, 6 - skip);
        skip_bits(&gb, skip);

        rlow = av_clip((c->band[0].scale_factor * quantizer_table[ilow] >> 10)
                      + c->band[0].s_predictor, -16384, 16383);

        ff_g722_update_low_predictor(&c->band[0], ilow >> (2 - skip));

        dhigh = c->band[1].scale_factor * ff_g722_high_inv_quant[ihigh] >> 10;
        rhigh = av_clip(dhigh + c->band[1].s_predictor, -16384, 16383);

        ff_g722_update_high_predictor(&c->band[1], dhigh, ihigh);

        c->prev_samples[c->prev_samples_pos++] = rlow + rhigh;
        c->prev_samples[c->prev_samples_pos++] = rlow - rhigh;
        ff_g722_apply_qmf(c->prev_samples + c->prev_samples_pos - 24,
                          &xout1, &xout2);
        *out_buf++ = av_clip_int16(xout1 >> 11);
        *out_buf++ = av_clip_int16(xout2 >> 11);
        if (c->prev_samples_pos >= PREV_SAMPLES_BUF_SIZE) {
            memmove(c->prev_samples, c->prev_samples + c->prev_samples_pos - 22,
                    22 * sizeof(c->prev_samples[0]));
            c->prev_samples_pos = 22;
        }
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = c->frame;

    return avpkt->size;
}

AVCodec ff_adpcm_g722_decoder = {
    .name           = "g722",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_decode_init,
    .decode         = g722_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("G.722 ADPCM"),
    .priv_class     = &g722_decoder_class,
};
