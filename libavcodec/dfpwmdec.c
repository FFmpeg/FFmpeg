/*
 * DFPWM decoder
 * Copyright (c) 2022 Jack Bruienne
 * Copyright (c) 2012, 2016 Ben "GreaseMonkey" Russell
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
 * DFPWM1a decoder
 */

#include "libavutil/internal.h"
#include "avcodec.h"
#include "codec_id.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct {
    int fq, q, s, lt;
} DFPWMState;

// DFPWM codec from https://github.com/ChenThread/dfpwm/blob/master/1a/
// Licensed in the public domain

static void au_decompress(DFPWMState *state, int fs, int len,
                          uint8_t *outbuf, const uint8_t *inbuf)
{
    unsigned d;
    for (int i = 0; i < len; i++) {
        // get bits
        d = *(inbuf++);
        for (int j = 0; j < 8; j++) {
            int nq, lq, st, ns, ov;
            // set target
            int t = ((d&1) ? 127 : -128);
            d >>= 1;

            // adjust charge
            nq = state->q + ((state->s * (t-state->q) + 512)>>10);
            if(nq == state->q && nq != t)
                nq += (t == 127 ? 1 : -1);
            lq = state->q;
            state->q = nq;

            // adjust strength
            st = (t != state->lt ? 0 : 1023);
            ns = state->s;
            if(ns != st)
                ns += (st != 0 ? 1 : -1);
            if(ns < 8) ns = 8;
            state->s = ns;

            // FILTER: perform antijerk
            ov = (t != state->lt ? (nq+lq+1)>>1 : nq);

            // FILTER: perform LPF
            state->fq += ((fs*(ov-state->fq) + 0x80)>>8);
            ov = state->fq;

            // output sample
            *(outbuf++) = ov + 128;

            state->lt = t;
        }
    }
}

static av_cold int dfpwm_dec_init(struct AVCodecContext *ctx)
{
    DFPWMState *state = ctx->priv_data;

    state->fq = 0;
    state->q = 0;
    state->s = 0;
    state->lt = -128;

    ctx->sample_fmt = AV_SAMPLE_FMT_U8;
    ctx->bits_per_raw_sample = 8;

    return 0;
}

static int dfpwm_dec_frame(struct AVCodecContext *ctx, AVFrame *frame,
                           int *got_frame, struct AVPacket *packet)
{
    DFPWMState *state = ctx->priv_data;
    int ret;

    if (packet->size * 8LL % ctx->ch_layout.nb_channels)
        return AVERROR_PATCHWELCOME;

    frame->nb_samples = packet->size * 8LL / ctx->ch_layout.nb_channels;
    if (frame->nb_samples <= 0) {
        av_log(ctx, AV_LOG_ERROR, "invalid number of samples in packet\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(ctx, frame, 0)) < 0)
        return ret;

    au_decompress(state, 140, packet->size, frame->data[0], packet->data);

    *got_frame = 1;
    return packet->size;
}

const FFCodec ff_dfpwm_decoder = {
    .p.name         = "dfpwm",
    CODEC_LONG_NAME("DFPWM1a audio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_DFPWM,
    .priv_data_size = sizeof(DFPWMState),
    .init           = dfpwm_dec_init,
    FF_CODEC_DECODE_CB(dfpwm_dec_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
