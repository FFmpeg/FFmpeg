/*
 * MOFLEX Fast Audio decoder
 * Copyright (c) 2015-2016 Florian Nouwt
 * Copyright (c) 2017 Adib Surani
 * Copyright (c) 2020 Paul B Mahol
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

#include "libavutil/intfloat.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct ChannelItems {
    float f[8];
    float last;
} ChannelItems;

typedef struct FastAudioContext {
    float table[8][64];

    ChannelItems *ch;
} FastAudioContext;

static av_cold int fastaudio_init(AVCodecContext *avctx)
{
    FastAudioContext *s = avctx->priv_data;

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    for (int i = 0; i < 8; i++)
        s->table[0][i] = (i - 159.5f) / 160.f;
    for (int i = 0; i < 11; i++)
        s->table[0][i + 8] = (i - 37.5f) / 40.f;
    for (int i = 0; i < 27; i++)
        s->table[0][i + 8 + 11] = (i - 13.f) / 20.f;
    for (int i = 0; i < 11; i++)
        s->table[0][i + 8 + 11 + 27] = (i + 27.5f) / 40.f;
    for (int i = 0; i < 7; i++)
        s->table[0][i + 8 + 11 + 27 + 11] = (i + 152.5f) / 160.f;

    memcpy(s->table[1], s->table[0], sizeof(s->table[0]));

    for (int i = 0; i < 7; i++)
        s->table[2][i] = (i - 33.5f) / 40.f;
    for (int i = 0; i < 25; i++)
        s->table[2][i + 7] = (i - 13.f) / 20.f;

    for (int i = 0; i < 32; i++)
        s->table[3][i] = -s->table[2][31 - i];

    for (int i = 0; i < 16; i++)
        s->table[4][i] = i * 0.22f / 3.f - 0.6f;

    for (int i = 0; i < 16; i++)
        s->table[5][i] = i * 0.20f / 3.f - 0.3f;

    for (int i = 0; i < 8; i++)
        s->table[6][i] = i * 0.36f / 3.f - 0.4f;

    for (int i = 0; i < 8; i++)
        s->table[7][i] = i * 0.34f / 3.f - 0.2f;

    s->ch = av_calloc(avctx->ch_layout.nb_channels, sizeof(*s->ch));
    if (!s->ch)
        return AVERROR(ENOMEM);

    return 0;
}

static int read_bits(int bits, int *ppos, unsigned *src)
{
    int r, pos;

    pos = *ppos;
    pos += bits;
    r = src[(pos - 1) / 32] >> ((-pos) & 31);
    *ppos = pos;

    return r & ((1 << bits) - 1);
}

static const uint8_t bits[8] = { 6, 6, 5, 5, 4, 0, 3, 3, };

static void set_sample(int i, int j, int v, float *result, int *pads, float value)
{
    result[i * 64 + pads[i] + j * 3] = value * (2 * v - 7);
}

static int fastaudio_decode(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *pkt)
{
    FastAudioContext *s = avctx->priv_data;
    GetByteContext gb;
    int subframes;
    int ret;

    subframes = pkt->size / (40 * avctx->ch_layout.nb_channels);
    frame->nb_samples = subframes * 256;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    bytestream2_init(&gb, pkt->data, pkt->size);

    for (int subframe = 0; subframe < subframes; subframe++) {
        for (int channel = 0; channel < avctx->ch_layout.nb_channels; channel++) {
            ChannelItems *ch = &s->ch[channel];
            float result[256] = { 0 };
            unsigned src[10];
            int inds[4], pads[4];
            float m[8];
            int pos = 0;

            for (int i = 0; i < 10; i++)
                src[i] = bytestream2_get_le32(&gb);

            for (int i = 0; i < 8; i++)
                m[7 - i] = s->table[i][read_bits(bits[i], &pos, src)];

            for (int i = 0; i < 4; i++)
                inds[3 - i] = read_bits(6, &pos, src);

            for (int i = 0; i < 4; i++)
                pads[3 - i] = read_bits(2, &pos, src);

            for (int i = 0, index5 = 0; i < 4; i++) {
                float value = av_int2float((inds[i] + 1) << 20) * powf(2.f, 116.f);

                for (int j = 0, tmp = 0; j < 21; j++) {
                    set_sample(i, j, j == 20 ? tmp / 2 : read_bits(3, &pos, src), result, pads, value);
                    if (j % 10 == 9)
                        tmp = 4 * tmp + read_bits(2, &pos, src);
                    if (j == 20)
                        index5 = FFMIN(2 * index5 + tmp % 2, 63);
                }

                m[2] = s->table[5][index5];
            }

            for (int i = 0; i < 256; i++) {
                float x = result[i];

                for (int j = 0; j < 8; j++) {
                    x -= m[j] * ch->f[j];
                    ch->f[j] += m[j] * x;
                }

                memmove(&ch->f[0], &ch->f[1], sizeof(float) * 7);
                ch->f[7] = x;
                ch->last = x + ch->last * 0.86f;
                result[i] = ch->last * 2.f;
            }

            memcpy(frame->extended_data[channel] + 1024 * subframe, result, 256 * sizeof(float));
        }
    }

    *got_frame = 1;

    return pkt->size;
}

static av_cold int fastaudio_close(AVCodecContext *avctx)
{
    FastAudioContext *s = avctx->priv_data;

    av_freep(&s->ch);

    return 0;
}

const FFCodec ff_fastaudio_decoder = {
    .p.name         = "fastaudio",
    CODEC_LONG_NAME("MobiClip FastAudio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_FASTAUDIO,
    .priv_data_size = sizeof(FastAudioContext),
    .init           = fastaudio_init,
    FF_CODEC_DECODE_CB(fastaudio_decode),
    .close          = fastaudio_close,
    .p.capabilities = AV_CODEC_CAP_DR1,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP),
};
