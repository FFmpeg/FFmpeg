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

static av_cold int g722_decode_init(AVCodecContext * avctx)
{
    G722Context *c = avctx->priv_data;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono tracks are allowed.\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    switch (avctx->bits_per_coded_sample) {
    case 8:
    case 7:
    case 6:
        break;
    default:
        av_log(avctx, AV_LOG_WARNING, "Unsupported bits_per_coded_sample [%d], "
                                      "assuming 8\n",
                                      avctx->bits_per_coded_sample);
    case 0:
        avctx->bits_per_coded_sample = 8;
        break;
    }

    c->band[0].scale_factor = 8;
    c->band[1].scale_factor = 2;
    c->prev_samples_pos = 22;

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
                             int *data_size, AVPacket *avpkt)
{
    G722Context *c = avctx->priv_data;
    int16_t *out_buf = data;
    int j, out_len;
    const int skip = 8 - avctx->bits_per_coded_sample;
    const int16_t *quantizer_table = low_inv_quants[skip];
    GetBitContext gb;

    out_len = avpkt->size * 2 * av_get_bytes_per_sample(avctx->sample_fmt);
    if (*data_size < out_len) {
        av_log(avctx, AV_LOG_ERROR, "Output buffer is too small\n");
        return AVERROR(EINVAL);
    }

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
        *out_buf++ = av_clip_int16(xout1 >> 12);
        *out_buf++ = av_clip_int16(xout2 >> 12);
        if (c->prev_samples_pos >= PREV_SAMPLES_BUF_SIZE) {
            memmove(c->prev_samples, c->prev_samples + c->prev_samples_pos - 22,
                    22 * sizeof(c->prev_samples[0]));
            c->prev_samples_pos = 22;
        }
    }
    *data_size = out_len;
    return avpkt->size;
}

AVCodec ff_adpcm_g722_decoder = {
    .name           = "g722",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_decode_init,
    .decode         = g722_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("G.722 ADPCM"),
};
