/*
 * DFPWM encoder
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
 * DFPWM1a encoder
 */

#include "libavutil/internal.h"
#include "avcodec.h"
#include "codec_id.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct {
    int fq, q, s, lt;
} DFPWMState;

// DFPWM codec from https://github.com/ChenThread/dfpwm/blob/master/1a/
// Licensed in the public domain

// note, len denotes how many compressed bytes there are (uncompressed bytes / 8).
static void au_compress(DFPWMState *state, int len, uint8_t *outbuf, const uint8_t *inbuf)
{
    unsigned d = 0;
    for (int i = 0; i < len; i++) {
        for (int j = 0; j < 8; j++) {
            int nq, st, ns;
            // get sample
            int v = *(inbuf++) - 128;
            // set bit / target
            int t = (v > state->q || (v == state->q && v == 127) ? 127 : -128);
            d >>= 1;
            if(t > 0)
                d |= 0x80;

            // adjust charge
            nq = state->q + ((state->s * (t-state->q) + 512)>>10);
            if(nq == state->q && nq != t)
                nq += (t == 127 ? 1 : -1);
            state->q = nq;

            // adjust strength
            st = (t != state->lt ? 0 : 1023);
            ns = state->s;
            if(ns != st)
                ns += (st != 0 ? 1 : -1);
            if(ns < 8) ns = 8;
            state->s = ns;

            state->lt = t;
        }

        // output bits
        *(outbuf++) = d;
    }
}

static av_cold int dfpwm_enc_init(struct AVCodecContext *ctx)
{
    DFPWMState *state = ctx->priv_data;

    state->fq = 0;
    state->q = 0;
    state->s = 0;
    state->lt = -128;

    ctx->bits_per_coded_sample = 1;

    return 0;
}

static int dfpwm_enc_frame(struct AVCodecContext *ctx, struct AVPacket *packet,
    const struct AVFrame *frame, int *got_packet)
{
    DFPWMState *state = ctx->priv_data;
    int size = frame->nb_samples * frame->ch_layout.nb_channels / 8 + (frame->nb_samples % 8 > 0 ? 1 : 0);
    int ret = ff_get_encode_buffer(ctx, packet, size, 0);

    if (ret) {
        *got_packet = 0;
        return ret;
    }

    au_compress(state, size, packet->data, frame->data[0]);

    *got_packet = 1;
    return 0;
}

const FFCodec ff_dfpwm_encoder = {
    .p.name          = "dfpwm",
    CODEC_LONG_NAME("DFPWM1a audio"),
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_DFPWM,
    .priv_data_size  = sizeof(DFPWMState),
    .init            = dfpwm_enc_init,
    FF_CODEC_ENCODE_CB(dfpwm_enc_frame),
    .p.sample_fmts   = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_U8, AV_SAMPLE_FMT_NONE},
    .p.capabilities  = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_VARIABLE_FRAME_SIZE |
                       AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
};
