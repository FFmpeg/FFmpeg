/*
 * OSQ audio decoder
 * Copyright (c) 2023 Paul B Mahol
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
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "unary.h"

#define OFFSET 5

typedef struct OSQChannel {
    unsigned prediction;
    unsigned coding_mode;
    unsigned residue_parameter;
    unsigned residue_bits;
    unsigned history[3];
    unsigned pos, count;
    double sum;
    int32_t prev;
} OSQChannel;

typedef struct OSQContext {
    GetBitContext gb;
    OSQChannel ch[2];

    uint8_t *bitstream;
    size_t max_framesize;
    size_t bitstream_size;

    int factor;
    int decorrelate;
    int frame_samples;
    uint64_t nb_samples;

    int32_t *decode_buffer[2];

    AVPacket *pkt;
    int pkt_offset;
} OSQContext;

static void osq_flush(AVCodecContext *avctx)
{
    OSQContext *s = avctx->priv_data;

    s->bitstream_size = 0;
    s->pkt_offset = 0;
}

static av_cold int osq_close(AVCodecContext *avctx)
{
    OSQContext *s = avctx->priv_data;

    av_freep(&s->bitstream);
    s->bitstream_size = 0;

    for (int ch = 0; ch < FF_ARRAY_ELEMS(s->decode_buffer); ch++)
        av_freep(&s->decode_buffer[ch]);

    return 0;
}

static av_cold int osq_init(AVCodecContext *avctx)
{
    OSQContext *s = avctx->priv_data;

    if (avctx->extradata_size < 48)
        return AVERROR(EINVAL);

    if (avctx->extradata[0] != 1) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported version.\n");
        return AVERROR_INVALIDDATA;
    }

    avctx->sample_rate = AV_RL32(avctx->extradata + 4);
    if (avctx->sample_rate < 1)
        return AVERROR_INVALIDDATA;

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
    avctx->ch_layout.nb_channels = avctx->extradata[3];
    if (avctx->ch_layout.nb_channels < 1)
        return AVERROR_INVALIDDATA;
    if (avctx->ch_layout.nb_channels > FF_ARRAY_ELEMS(s->decode_buffer))
        return AVERROR_INVALIDDATA;

    s->factor = 1;
    switch (avctx->extradata[2]) {
    case  8: avctx->sample_fmt = AV_SAMPLE_FMT_U8P; break;
    case 16: avctx->sample_fmt = AV_SAMPLE_FMT_S16P; break;
    case 20:
    case 24: s->factor = 256;
             avctx->sample_fmt = AV_SAMPLE_FMT_S32P; break;
    default: return AVERROR_INVALIDDATA;
    }

    avctx->bits_per_raw_sample = avctx->extradata[2];
    s->nb_samples = AV_RL64(avctx->extradata + 16);
    s->frame_samples = AV_RL16(avctx->extradata + 8);
    s->max_framesize = (s->frame_samples * 16 + 1024) * avctx->ch_layout.nb_channels;

    s->bitstream = av_calloc(s->max_framesize + AV_INPUT_BUFFER_PADDING_SIZE, sizeof(*s->bitstream));
    if (!s->bitstream)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < avctx->ch_layout.nb_channels; ch++) {
        s->decode_buffer[ch] = av_calloc(s->frame_samples + OFFSET,
                                         sizeof(*s->decode_buffer[ch]));
        if (!s->decode_buffer[ch])
            return AVERROR(ENOMEM);
    }

    s->pkt = avctx->internal->in_pkt;

    return 0;
}

static void reset_stats(OSQChannel *cb)
{
    memset(cb->history, 0, sizeof(cb->history));
    cb->pos = cb->count = cb->sum = 0;
}

static void update_stats(OSQChannel *cb, int val)
{
    cb->sum += FFABS(val) - cb->history[cb->pos];
    cb->history[cb->pos] = FFABS(val);
    cb->pos++;
    cb->count++;
    if (cb->pos >= FF_ARRAY_ELEMS(cb->history))
        cb->pos = 0;
}

static int update_residue_parameter(OSQChannel *cb)
{
    double sum, x;
    int rice_k;

    sum = cb->sum;
    if (!sum)
        return 0;
    x = sum / cb->count;
    rice_k = ceil(log2(x));
    if (rice_k >= 30) {
        double f = floor(sum / 1.4426952 + 0.5);
        if (f <= 1) {
            rice_k = 1;
        } else if (f >= 31) {
            rice_k = 31;
        } else
            rice_k = f;
    }

    return rice_k;
}

static uint32_t get_urice(GetBitContext *gb, int k)
{
    uint32_t z, x, b;

    x = get_unary(gb, 1, 512);
    b = get_bits_long(gb, k);
    z = b | x << k;

    return z;
}

static int32_t get_srice(GetBitContext *gb, int x)
{
    int32_t y = get_urice(gb, x);
    return get_bits1(gb) ? -y : y;
}

static int osq_channel_parameters(AVCodecContext *avctx, int ch)
{
    OSQContext *s = avctx->priv_data;
    OSQChannel *cb = &s->ch[ch];
    GetBitContext *gb = &s->gb;

    cb->prev = 0;
    cb->prediction = get_urice(gb, 5);
    cb->coding_mode = get_urice(gb, 3);
    if (cb->prediction >= 15)
        return AVERROR_INVALIDDATA;
    if (cb->coding_mode > 0 && cb->coding_mode < 3) {
        cb->residue_parameter = get_urice(gb, 4);
        if (!cb->residue_parameter || cb->residue_parameter >= 31)
            return AVERROR_INVALIDDATA;
    } else if (cb->coding_mode == 3) {
        cb->residue_bits = get_urice(gb, 4);
        if (!cb->residue_bits || cb->residue_bits >= 31)
            return AVERROR_INVALIDDATA;
    } else if (cb->coding_mode) {
        return AVERROR_INVALIDDATA;
    }

    if (cb->coding_mode == 2)
        reset_stats(cb);

    return 0;
}

#define A (-1)
#define B (-2)
#define C (-3)
#define D (-4)
#define E (-5)
#define P2 (((unsigned)dst[A] + dst[A]) - dst[B])
#define P3 (((unsigned)dst[A] - dst[B]) * 3 + dst[C])

static int do_decode(AVCodecContext *avctx, AVFrame *frame, int decorrelate, int downsample)
{
    OSQContext *s = avctx->priv_data;
    const int nb_channels = avctx->ch_layout.nb_channels;
    const int nb_samples = frame->nb_samples;
    GetBitContext *gb = &s->gb;

    for (int n = 0; n < nb_samples; n++) {
        for (int ch = 0; ch < nb_channels; ch++) {
            OSQChannel *cb = &s->ch[ch];
            int32_t *dst = s->decode_buffer[ch] + OFFSET;
            int32_t p, prev = cb->prev;

            if (nb_channels == 2 && ch == 1 && decorrelate != s->decorrelate) {
                if (!decorrelate) {
                    s->decode_buffer[1][OFFSET+A] += s->decode_buffer[0][OFFSET+B];
                    s->decode_buffer[1][OFFSET+B] += s->decode_buffer[0][OFFSET+C];
                    s->decode_buffer[1][OFFSET+C] += s->decode_buffer[0][OFFSET+D];
                    s->decode_buffer[1][OFFSET+D] += s->decode_buffer[0][OFFSET+E];
                } else {
                    s->decode_buffer[1][OFFSET+A] -= s->decode_buffer[0][OFFSET+B];
                    s->decode_buffer[1][OFFSET+B] -= s->decode_buffer[0][OFFSET+C];
                    s->decode_buffer[1][OFFSET+C] -= s->decode_buffer[0][OFFSET+D];
                    s->decode_buffer[1][OFFSET+D] -= s->decode_buffer[0][OFFSET+E];
                }
                s->decorrelate = decorrelate;
            }

            if (!cb->coding_mode) {
                dst[n] = 0;
            } else if (cb->coding_mode == 3) {
                dst[n] = get_sbits_long(gb, cb->residue_bits);
            } else {
                dst[n] = get_srice(gb, cb->residue_parameter);
            }

            if (get_bits_left(gb) < 0) {
                av_log(avctx, AV_LOG_ERROR, "overread!\n");
                return AVERROR_INVALIDDATA;
            }

            p = prev / 2;
            prev = dst[n];

            switch (cb->prediction) {
            case 0:
                break;
            case 1:
                dst[n] += (unsigned)dst[A];
                break;
            case 2:
                dst[n] += (unsigned)dst[A] + p;
                break;
            case 3:
                dst[n] += P2;
                break;
            case 4:
                dst[n] += P2 + p;
                break;
            case 5:
                dst[n] += P3;
                break;
            case 6:
                dst[n] += P3 + p;
                break;
            case 7:
                dst[n] += (int)(P2 + P3) / 2 + (unsigned)p;
                break;
            case 8:
                dst[n] += (int)(P2 + P3) / 2 + 0U;
                break;
            case 9:
                dst[n] += (int)(P2 * 2 + P3) / 3 + (unsigned)p;
                break;
            case 10:
                dst[n] += (int)(P2 + P3 * 2) / 3 + (unsigned)p;
                break;
            case 11:
                dst[n] += (int)((unsigned)dst[A] + dst[B]) / 2 + 0U;
                break;
            case 12:
                dst[n] += (unsigned)dst[B];
                break;
            case 13:
                dst[n] += (int)((unsigned)dst[D] + dst[B]) / 2 + 0U;
                break;
            case 14:
                dst[n] += (int)((unsigned)P2 + dst[A]) / 2 + (unsigned)p;
                break;
            default:
                return AVERROR_INVALIDDATA;
            }

            cb->prev = prev;

            if (downsample)
                dst[n] *= 256U;

            dst[E] = dst[D];
            dst[D] = dst[C];
            dst[C] = dst[B];
            dst[B] = dst[A];
            dst[A] = dst[n];

            if (cb->coding_mode == 2) {
                update_stats(cb, dst[n]);
                cb->residue_parameter = update_residue_parameter(cb);
            }

            if (nb_channels == 2 && ch == 1) {
                if (decorrelate)
                    dst[n] += (unsigned)s->decode_buffer[0][OFFSET+n];
            }

            if (downsample)
                dst[A] /= 256;
        }
    }

    return 0;
}

static int osq_decode_block(AVCodecContext *avctx, AVFrame *frame)
{
    const int nb_channels = avctx->ch_layout.nb_channels;
    const int nb_samples = frame->nb_samples;
    OSQContext *s = avctx->priv_data;
    const unsigned factor = s->factor;
    int ret, decorrelate, downsample;
    GetBitContext *gb = &s->gb;

    skip_bits1(gb);
    decorrelate = get_bits1(gb);
    downsample = get_bits1(gb);

    for (int ch = 0; ch < nb_channels; ch++) {
        if ((ret = osq_channel_parameters(avctx, ch)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "invalid channel parameters\n");
            return ret;
        }
    }

    if ((ret = do_decode(avctx, frame, decorrelate, downsample)) < 0)
        return ret;

    align_get_bits(gb);

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_U8P:
        for (int ch = 0; ch < nb_channels; ch++) {
            uint8_t *dst = (uint8_t *)frame->extended_data[ch];
            int32_t *src = s->decode_buffer[ch] + OFFSET;

            for (int n = 0; n < nb_samples; n++)
                dst[n] = av_clip_uint8(src[n] + 0x80);
        }
        break;
    case AV_SAMPLE_FMT_S16P:
        for (int ch = 0; ch < nb_channels; ch++) {
            int16_t *dst = (int16_t *)frame->extended_data[ch];
            int32_t *src = s->decode_buffer[ch] + OFFSET;

            for (int n = 0; n < nb_samples; n++)
                dst[n] = (int16_t)src[n];
        }
        break;
    case AV_SAMPLE_FMT_S32P:
        for (int ch = 0; ch < nb_channels; ch++) {
            int32_t *dst = (int32_t *)frame->extended_data[ch];
            int32_t *src = s->decode_buffer[ch] + OFFSET;

            for (int n = 0; n < nb_samples; n++)
                dst[n] = src[n] * factor;
        }
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int osq_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    OSQContext *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret, n;

    while (s->bitstream_size < s->max_framesize) {
        int size;

        if (!s->pkt->data) {
            ret = ff_decode_get_packet(avctx, s->pkt);
            if (ret == AVERROR_EOF && s->bitstream_size > 0)
                break;
            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                return ret;
            if (ret < 0)
                goto fail;
        }

        size = FFMIN(s->pkt->size - s->pkt_offset, s->max_framesize - s->bitstream_size);
        memcpy(s->bitstream + s->bitstream_size, s->pkt->data + s->pkt_offset, size);
        s->bitstream_size += size;
        s->pkt_offset += size;

        if (s->pkt_offset == s->pkt->size) {
            av_packet_unref(s->pkt);
            s->pkt_offset = 0;
        }
    }

    frame->nb_samples = FFMIN(s->frame_samples, s->nb_samples);
    if (frame->nb_samples <= 0)
        return AVERROR_EOF;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        goto fail;

    if ((ret = init_get_bits8(gb, s->bitstream, s->bitstream_size)) < 0)
        goto fail;

    if ((ret = osq_decode_block(avctx, frame)) < 0)
        goto fail;

    s->nb_samples -= frame->nb_samples;

    n = get_bits_count(gb) / 8;
    if (n > s->bitstream_size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    memmove(s->bitstream, &s->bitstream[n], s->bitstream_size - n);
    s->bitstream_size -= n;

    return 0;

fail:
    s->bitstream_size = 0;
    s->pkt_offset = 0;
    av_packet_unref(s->pkt);

    return ret;
}

const FFCodec ff_osq_decoder = {
    .p.name           = "osq",
    CODEC_LONG_NAME("OSQ (Original Sound Quality)"),
    .p.type           = AVMEDIA_TYPE_AUDIO,
    .p.id             = AV_CODEC_ID_OSQ,
    .priv_data_size   = sizeof(OSQContext),
    .init             = osq_init,
    FF_CODEC_RECEIVE_FRAME_CB(osq_receive_frame),
    .close            = osq_close,
    .p.capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                        AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    .p.sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_U8P,
                                                        AV_SAMPLE_FMT_S16P,
                                                        AV_SAMPLE_FMT_S32P,
                                                        AV_SAMPLE_FMT_NONE },
    .flush            = osq_flush,
};
