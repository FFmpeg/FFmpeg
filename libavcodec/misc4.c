/*
 * Micronas SC-4 audio decoder
 * Copyright (c) 2022 Paul B Mahol
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

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "bytestream.h"
#include "mathops.h"

static const int16_t steps[16] = {
    4084, 18, 41, 64, 112, 198, 355, 1122,
    1122, 355, 198, 112, 64, 41, 18, 4084,
};

static const int16_t diffs[16] = {
    2048, 4, 135, 213, 273, 323, 373, 425,
    425, 373, 323, 273, 213, 135, 4, 2048,
};

typedef struct ChannelContext {
    unsigned last_step;
    int64_t new_pred;
    int64_t pred;
    int64_t weights_tab[6];
    int32_t diffs_tab[6];
} ChannelContext;

typedef struct MISC4Context {
    GetByteContext gb;

    uint32_t marker;

    ChannelContext ch[2];
} MISC4Context;

static av_cold int misc4_init(AVCodecContext *avctx)
{
    MISC4Context *s = avctx->priv_data;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    switch (avctx->sample_rate) {
    case 8000:
    case 11025:
        s->marker = 0x11b;
        break;
    case 16000:
    case 32000:
        s->marker = 0x2b2;
        break;
    }

    return 0;
}

#define FRACBITS 12
#define WEIGHTSBITS 26

static int64_t prediction(int delta, ChannelContext *c)
{
    const int isign = FFDIFFSIGN(delta, 0);
    int64_t dotpr = 0;

    c->new_pred = delta * (1LL << FRACBITS) + c->pred;

    for (int i = 0; i < 6; i++) {
        const int sign = FFSIGN(c->diffs_tab[i]);
        c->weights_tab[i] = (c->weights_tab[i] * 255LL) / 256;
        c->weights_tab[i] += (1LL << (WEIGHTSBITS + 1)) * sign * isign;
    }

    memmove(&c->diffs_tab[1], &c->diffs_tab[0], 5 * sizeof(int32_t));

    c->diffs_tab[0] = -delta * (1 << (FRACBITS-8));
    c->pred = c->new_pred;

    dotpr = 0;
    for (int i = 0; i < 6; i++)
        dotpr += ((int64_t)c->diffs_tab[i] * c->weights_tab[i]) >> WEIGHTSBITS;

    c->pred += dotpr;
    c->pred = av_clip64(c->pred, -16383 * (1 << FRACBITS), 16383 * (1 << FRACBITS));
    c->pred = c->pred * 9 / 10;

    return c->new_pred;
}

static int16_t decode(ChannelContext *c, unsigned nibble)
{
    int diff, diff_sign, adiff = 0, delta;
    uint32_t step, newstep;
    int64_t pred;

    diff_sign = nibble >> 3;
    diff = diffs[nibble];
    step = diff + (c->last_step >> 2);
    newstep = step & 0xfff;
    if (newstep >> 11 == 0)
        adiff = (((step & 0x7f) + 0x80) * 128) >>
                 (14 - (newstep >> 7));
    delta = diff_sign ? -adiff : adiff;
    delta = av_clip_intp2(delta, 15);
    pred = prediction(delta, c);
    nibble = steps[nibble];
    newstep = nibble * 32 - c->last_step & 0x1ffff;
    newstep = ((newstep >> 5) + (newstep & 0x10000 ? 0x1000 : 0) + c->last_step) & 0x1fff;
    c->last_step = av_clip(newstep, 544, 5120);

    return av_clip_int16(pred >> (FRACBITS - 3));
}

static int misc4_decode(AVCodecContext *avctx, AVFrame *frame,
                       int *got_frame_ptr, AVPacket *pkt)
{
    MISC4Context *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    uint32_t hdr;
    int ret;

    bytestream2_init(gb, pkt->data, pkt->size);

    frame->nb_samples = 29 * (1 + (avctx->ch_layout.nb_channels == 1));
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    hdr = bytestream2_peek_be32(gb);
    if (hdr == s->marker) {
        bytestream2_skip(gb, 5);
    } else if ((hdr >> 16) == s->marker) {
        bytestream2_skip(gb, 3);
    }

    {
        int16_t *samples = (int16_t *)frame->data[0];
        const int st = avctx->ch_layout.nb_channels == 2;
        int n;

        for (n = 0; n < 29; n++) {
            int nibble = bytestream2_get_byte(gb);
            samples[2*n+0] = decode(&s->ch[0 ], nibble >> 4);
            samples[2*n+1] = decode(&s->ch[st], nibble & 15);
            if (bytestream2_get_bytes_left(gb) <= 0)
                break;
        }

        if (n == 29 && bytestream2_get_byte(gb) != 0x55)
            return AVERROR_INVALIDDATA;
    }

    *got_frame_ptr = 1;

    return bytestream2_tell(gb);
}

const FFCodec ff_misc4_decoder = {
    .p.name           = "misc4",
    CODEC_LONG_NAME("Micronas SC-4 Audio"),
    .p.type           = AVMEDIA_TYPE_AUDIO,
    .p.id             = AV_CODEC_ID_MISC4,
    .priv_data_size   = sizeof(MISC4Context),
    .init             = misc4_init,
    FF_CODEC_DECODE_CB(misc4_decode),
    .p.capabilities   = AV_CODEC_CAP_DR1 |
#if FF_API_SUBFRAMES
                        AV_CODEC_CAP_SUBFRAMES |
#endif
                        AV_CODEC_CAP_CHANNEL_CONF,
    .p.sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16,
                                                        AV_SAMPLE_FMT_NONE },
};
