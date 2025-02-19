/*
 * APAC audio decoder
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

#include "libavutil/audio_fifo.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"

typedef struct ChContext {
    int have_code;
    int last_sample;
    int last_delta;
    int bit_length;
    int block_length;
    uint8_t block[32 * 2];
    AVAudioFifo *samples;
} ChContext;

typedef struct APACContext {
    GetBitContext gb;
    int skip;

    int cur_ch;
    ChContext ch[2];

    uint8_t *bitstream;
    int64_t max_framesize;
    int bitstream_size;
    int bitstream_index;
} APACContext;

static av_cold int apac_close(AVCodecContext *avctx)
{
    APACContext *s = avctx->priv_data;

    av_freep(&s->bitstream);
    s->bitstream_size = 0;

    for (int ch = 0; ch < 2; ch++) {
        ChContext *c = &s->ch[ch];

        av_audio_fifo_free(c->samples);
    }

    return 0;
}

static av_cold int apac_init(AVCodecContext *avctx)
{
    APACContext *s = avctx->priv_data;

    if (avctx->bits_per_coded_sample > 8)
        avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    else
        avctx->sample_fmt = AV_SAMPLE_FMT_U8P;

    if (avctx->ch_layout.nb_channels < 1 ||
        avctx->ch_layout.nb_channels > 2 ||
        avctx->bits_per_coded_sample < 8 ||
        avctx->bits_per_coded_sample > 16
    )
        return AVERROR_INVALIDDATA;

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        ChContext *c = &s->ch[ch];

        c->bit_length = avctx->bits_per_coded_sample;
        c->block_length = 8;
        c->have_code = 0;
        c->samples = av_audio_fifo_alloc(avctx->sample_fmt, 1, 1024);
        if (!c->samples)
            return AVERROR(ENOMEM);
    }

    s->max_framesize = 1024;
    s->bitstream = av_realloc_f(s->bitstream, s->max_framesize + AV_INPUT_BUFFER_PADDING_SIZE, sizeof(*s->bitstream));
    if (!s->bitstream)
        return AVERROR(ENOMEM);

    return 0;
}

static int get_code(ChContext *c, GetBitContext *gb)
{
    if (get_bits1(gb)) {
        int code = get_bits(gb, 2);

        switch (code) {
        case 0:
            c->bit_length--;
            break;
        case 1:
            c->bit_length++;
            break;
        case 2:
            c->bit_length = get_bits(gb, 5);
            break;
        case 3:
            c->block_length = get_bits(gb, 4);
            return 1;
        }
    }

    return 0;
}

static int apac_decode(AVCodecContext *avctx, AVFrame *frame,
                       int *got_frame_ptr, AVPacket *pkt)
{
    APACContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret, n, buf_size, input_buf_size;
    uint8_t *buf;
    int nb_samples;

    if (!pkt->size && s->bitstream_size <= 0) {
        *got_frame_ptr = 0;
        return 0;
    }

    buf_size = pkt->size;
    input_buf_size = buf_size;

    if (s->bitstream_index > 0 && s->bitstream_size > 0) {
        memmove(s->bitstream, &s->bitstream[s->bitstream_index], s->bitstream_size);
        s->bitstream_index = 0;
    }

    if (s->bitstream_index + s->bitstream_size + buf_size > s->max_framesize) {
        s->bitstream = av_realloc_f(s->bitstream, s->bitstream_index +
                                    s->bitstream_size +
                                    buf_size + AV_INPUT_BUFFER_PADDING_SIZE,
                                    sizeof(*s->bitstream));
        if (!s->bitstream)
            return AVERROR(ENOMEM);
        s->max_framesize = s->bitstream_index + s->bitstream_size + buf_size;
    }
    if (pkt->data)
        memcpy(&s->bitstream[s->bitstream_index + s->bitstream_size], pkt->data, buf_size);
    buf                = &s->bitstream[s->bitstream_index];
    buf_size          += s->bitstream_size;
    s->bitstream_size  = buf_size;
    memset(buf + buf_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    frame->nb_samples = s->bitstream_size * 16 * 8;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if ((ret = init_get_bits8(gb, buf, buf_size)) < 0)
        return ret;

    skip_bits(gb, s->skip);
    s->skip = 0;

    while (get_bits_left(gb) > 0) {
        for (int ch = s->cur_ch; ch < avctx->ch_layout.nb_channels; ch++) {
            ChContext *c = &s->ch[ch];
            int16_t *dst16 = (int16_t *)c->block;
            uint8_t *dst8 = (uint8_t *)c->block;
            void *samples[4];

            samples[0] = &c->block[0];
            if (get_bits_left(gb) < 16 && pkt->size) {
                s->cur_ch = ch;
                goto end;
            }

            if (!c->have_code && get_code(c, gb))
                get_code(c, gb);
            c->have_code = 0;

            if (c->block_length <= 0)
                continue;

            if (c->bit_length < 0 ||
                c->bit_length > 17) {
                c->bit_length = avctx->bits_per_coded_sample;
                s->bitstream_index = 0;
                s->bitstream_size  = 0;
                return AVERROR_INVALIDDATA;
            }

            if (get_bits_left(gb) < c->block_length * c->bit_length) {
                if (pkt->size) {
                    c->have_code = 1;
                    s->cur_ch = ch;
                    goto end;
                } else {
                    break;
                }
            }

            for (int i = 0; i < c->block_length; i++) {
                int val = get_bits_long(gb, c->bit_length);
                unsigned delta = (val & 1) ? ~(val >> 1) : (val >> 1);
                int sample;

                delta += c->last_delta;
                sample = c->last_sample + delta;
                c->last_delta = delta;
                c->last_sample = sample;

                switch (avctx->sample_fmt) {
                case AV_SAMPLE_FMT_S16P:
                    dst16[i] = sample;
                    break;
                case AV_SAMPLE_FMT_U8P:
                    dst8[i] = sample;
                    break;
                }
            }

            av_audio_fifo_write(c->samples, samples, c->block_length);
        }

        s->cur_ch = 0;
    }
end:
    nb_samples = frame->nb_samples;
    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++)
        nb_samples = FFMIN(av_audio_fifo_size(s->ch[ch].samples), nb_samples);

    frame->nb_samples = nb_samples;
    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        void *samples[1] = { frame->extended_data[ch] };
        av_audio_fifo_read(s->ch[ch].samples, samples, nb_samples);
    }

    s->skip = get_bits_count(gb) - 8 * (get_bits_count(gb) / 8);
    n = get_bits_count(gb) / 8;

    if (nb_samples > 0 || pkt->size)
        *got_frame_ptr = 1;

    if (s->bitstream_size > 0) {
        s->bitstream_index += n;
        s->bitstream_size  -= n;
        return input_buf_size;
    }
    return n;
}

const FFCodec ff_apac_decoder = {
    .p.name           = "apac",
    CODEC_LONG_NAME("Marian's A-pac audio"),
    .p.type           = AVMEDIA_TYPE_AUDIO,
    .p.id             = AV_CODEC_ID_APAC,
    .priv_data_size   = sizeof(APACContext),
    .init             = apac_init,
    FF_CODEC_DECODE_CB(apac_decode),
    .close            = apac_close,
    .p.capabilities   = AV_CODEC_CAP_DELAY |
                        AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_U8P, AV_SAMPLE_FMT_S16P),
};
