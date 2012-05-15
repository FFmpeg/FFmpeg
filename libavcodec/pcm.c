/*
 * PCM codecs
 * Copyright (c) 2001 Fabrice Bellard
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
 * PCM codecs
 */

#include "libavutil/common.h" /* for av_reverse */
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "pcm_tablegen.h"

#define MAX_CHANNELS 64

static av_cold int pcm_encode_init(AVCodecContext *avctx)
{
    avctx->frame_size = 0;
    switch (avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        pcm_alaw_tableinit();
        break;
    case CODEC_ID_PCM_MULAW:
        pcm_ulaw_tableinit();
        break;
    default:
        break;
    }

    avctx->bits_per_coded_sample = av_get_bits_per_sample(avctx->codec->id);
    avctx->block_align           = avctx->channels * avctx->bits_per_coded_sample / 8;
    avctx->bit_rate              = avctx->block_align * avctx->sample_rate * 8;
    avctx->coded_frame           = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int pcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

/**
 * Write PCM samples macro
 * @param type   Datatype of native machine format
 * @param endian bytestream_put_xxx() suffix
 * @param src    Source pointer (variable name)
 * @param dst    Destination pointer (variable name)
 * @param n      Total number of samples (variable name)
 * @param shift  Bitshift (bits)
 * @param offset Sample value offset
 */
#define ENCODE(type, endian, src, dst, n, shift, offset)                \
    samples_ ## type = (const type *) src;                              \
    for (; n > 0; n--) {                                                \
        register type v = (*samples_ ## type++ >> shift) + offset;      \
        bytestream_put_ ## endian(&dst, v);                             \
    }

static int pcm_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    int n, sample_size, v, ret;
    const short *samples;
    unsigned char *dst;
    const uint8_t *srcu8;
    const int16_t *samples_int16_t;
    const int32_t *samples_int32_t;
    const int64_t *samples_int64_t;
    const uint16_t *samples_uint16_t;
    const uint32_t *samples_uint32_t;

    sample_size = av_get_bits_per_sample(avctx->codec->id) / 8;
    n           = frame->nb_samples * avctx->channels;
    samples     = (const short *)frame->data[0];

    if ((ret = ff_alloc_packet(avpkt, n * sample_size))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }
    dst = avpkt->data;

    switch (avctx->codec->id) {
    case CODEC_ID_PCM_U32LE:
        ENCODE(uint32_t, le32, samples, dst, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_U32BE:
        ENCODE(uint32_t, be32, samples, dst, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_S24LE:
        ENCODE(int32_t, le24, samples, dst, n, 8, 0)
        break;
    case CODEC_ID_PCM_S24BE:
        ENCODE(int32_t, be24, samples, dst, n, 8, 0)
        break;
    case CODEC_ID_PCM_U24LE:
        ENCODE(uint32_t, le24, samples, dst, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_U24BE:
        ENCODE(uint32_t, be24, samples, dst, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_S24DAUD:
        for (; n > 0; n--) {
            uint32_t tmp = av_reverse[(*samples >> 8) & 0xff] +
                           (av_reverse[*samples & 0xff] << 8);
            tmp <<= 4; // sync flags would go here
            bytestream_put_be24(&dst, tmp);
            samples++;
        }
        break;
    case CODEC_ID_PCM_U16LE:
        ENCODE(uint16_t, le16, samples, dst, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_U16BE:
        ENCODE(uint16_t, be16, samples, dst, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_S8:
        srcu8 = frame->data[0];
        for (; n > 0; n--) {
            v      = *srcu8++;
            *dst++ = v - 128;
        }
        break;
#if HAVE_BIGENDIAN
    case CODEC_ID_PCM_F64LE:
        ENCODE(int64_t, le64, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_F32LE:
        ENCODE(int32_t, le32, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16LE:
        ENCODE(int16_t, le16, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_S16BE:
#else
    case CODEC_ID_PCM_F64BE:
        ENCODE(int64_t, be64, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
        ENCODE(int32_t, be32, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16BE:
        ENCODE(int16_t, be16, samples, dst, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64LE:
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S16LE:
#endif /* HAVE_BIGENDIAN */
    case CODEC_ID_PCM_U8:
        memcpy(dst, samples, n * sample_size);
        dst += n * sample_size;
        break;
    case CODEC_ID_PCM_ALAW:
        for (; n > 0; n--) {
            v      = *samples++;
            *dst++ = linear_to_alaw[(v + 32768) >> 2];
        }
        break;
    case CODEC_ID_PCM_MULAW:
        for (; n > 0; n--) {
            v      = *samples++;
            *dst++ = linear_to_ulaw[(v + 32768) >> 2];
        }
        break;
    default:
        return -1;
    }

    *got_packet_ptr = 1;
    return 0;
}

typedef struct PCMDecode {
    AVFrame frame;
    short   table[256];
} PCMDecode;

static av_cold int pcm_decode_init(AVCodecContext *avctx)
{
    PCMDecode *s = avctx->priv_data;
    int i;

    if (avctx->channels <= 0 || avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "PCM channels out of bounds\n");
        return AVERROR(EINVAL);
    }

    switch (avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        for (i = 0; i < 256; i++)
            s->table[i] = alaw2linear(i);
        break;
    case CODEC_ID_PCM_MULAW:
        for (i = 0; i < 256; i++)
            s->table[i] = ulaw2linear(i);
        break;
    default:
        break;
    }

    avctx->sample_fmt = avctx->codec->sample_fmts[0];

    if (avctx->sample_fmt == AV_SAMPLE_FMT_S32)
        avctx->bits_per_raw_sample = av_get_bits_per_sample(avctx->codec->id);

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = &s->frame;

    return 0;
}

/**
 * Read PCM samples macro
 * @param size   Data size of native machine format
 * @param endian bytestream_get_xxx() endian suffix
 * @param src    Source pointer (variable name)
 * @param dst    Destination pointer (variable name)
 * @param n      Total number of samples (variable name)
 * @param shift  Bitshift (bits)
 * @param offset Sample value offset
 */
#define DECODE(size, endian, src, dst, n, shift, offset)                \
    for (; n > 0; n--) {                                                \
        uint ## size ## _t v = bytestream_get_ ## endian(&src);         \
        AV_WN ## size ## A(dst, (v - offset) << shift);                 \
        dst += size / 8;                                                \
    }

static int pcm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *src = avpkt->data;
    int buf_size       = avpkt->size;
    PCMDecode *s       = avctx->priv_data;
    int sample_size, c, n, ret, samples_per_block;
    uint8_t *samples;
    int32_t *dst_int32_t;

    sample_size = av_get_bits_per_sample(avctx->codec_id) / 8;

    /* av_get_bits_per_sample returns 0 for CODEC_ID_PCM_DVD */
    samples_per_block = 1;
    if (CODEC_ID_PCM_DVD == avctx->codec_id) {
        if (avctx->bits_per_coded_sample != 20 &&
            avctx->bits_per_coded_sample != 24) {
            av_log(avctx, AV_LOG_ERROR, "PCM DVD unsupported sample depth\n");
            return AVERROR(EINVAL);
        }
        /* 2 samples are interleaved per block in PCM_DVD */
        samples_per_block = 2;
        sample_size       = avctx->bits_per_coded_sample * 2 / 8;
    } else if (avctx->codec_id == CODEC_ID_PCM_LXF) {
        /* we process 40-bit blocks per channel for LXF */
        samples_per_block = 2;
        sample_size       = 5;
    }

    if (sample_size == 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid sample_size\n");
        return AVERROR(EINVAL);
    }

    n = avctx->channels * sample_size;

    if (n && buf_size % n) {
        if (buf_size < n) {
            av_log(avctx, AV_LOG_ERROR, "invalid PCM packet\n");
            return -1;
        } else
            buf_size -= buf_size % n;
    }

    n = buf_size / sample_size;

    /* get output buffer */
    s->frame.nb_samples = n * samples_per_block / avctx->channels;
    if ((ret = avctx->get_buffer(avctx, &s->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples = s->frame.data[0];

    switch (avctx->codec->id) {
    case CODEC_ID_PCM_U32LE:
        DECODE(32, le32, src, samples, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_U32BE:
        DECODE(32, be32, src, samples, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_S24LE:
        DECODE(32, le24, src, samples, n, 8, 0)
        break;
    case CODEC_ID_PCM_S24BE:
        DECODE(32, be24, src, samples, n, 8, 0)
        break;
    case CODEC_ID_PCM_U24LE:
        DECODE(32, le24, src, samples, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_U24BE:
        DECODE(32, be24, src, samples, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_S24DAUD:
        for (; n > 0; n--) {
            uint32_t v = bytestream_get_be24(&src);
            v >>= 4; // sync flags are here
            AV_WN16A(samples, av_reverse[(v >> 8) & 0xff] +
                             (av_reverse[v        & 0xff] << 8));
            samples += 2;
        }
        break;
    case CODEC_ID_PCM_S16LE_PLANAR:
    {
        const uint8_t *src2[MAX_CHANNELS];
        n /= avctx->channels;
        for (c = 0; c < avctx->channels; c++)
            src2[c] = &src[c * n * 2];
        for (; n > 0; n--)
            for (c = 0; c < avctx->channels; c++) {
                AV_WN16A(samples, bytestream_get_le16(&src2[c]));
                samples += 2;
            }
        break;
    }
    case CODEC_ID_PCM_U16LE:
        DECODE(16, le16, src, samples, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_U16BE:
        DECODE(16, be16, src, samples, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_S8:
        for (; n > 0; n--)
            *samples++ = *src++ + 128;
        break;
#if HAVE_BIGENDIAN
    case CODEC_ID_PCM_F64LE:
        DECODE(64, le64, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_F32LE:
        DECODE(32, le32, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16LE:
        DECODE(16, le16, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_S16BE:
#else
    case CODEC_ID_PCM_F64BE:
        DECODE(64, be64, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
        DECODE(32, be32, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16BE:
        DECODE(16, be16, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64LE:
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S16LE:
#endif /* HAVE_BIGENDIAN */
    case CODEC_ID_PCM_U8:
        memcpy(samples, src, n * sample_size);
        break;
    case CODEC_ID_PCM_ZORK:
        for (; n > 0; n--) {
            int v = *src++;
            if (v < 128)
                v = 128 - v;
            *samples++ = v;
        }
        break;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        for (; n > 0; n--) {
            AV_WN16A(samples, s->table[*src++]);
            samples += 2;
        }
        break;
    case CODEC_ID_PCM_DVD:
    {
        const uint8_t *src8;
        dst_int32_t = (int32_t *)s->frame.data[0];
        n /= avctx->channels;
        switch (avctx->bits_per_coded_sample) {
        case 20:
            while (n--) {
                c    = avctx->channels;
                src8 = src + 4 * c;
                while (c--) {
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8   & 0xf0) <<  8);
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++ & 0x0f) << 12);
                }
                src = src8;
            }
            break;
        case 24:
            while (n--) {
                c    = avctx->channels;
                src8 = src + 4 * c;
                while (c--) {
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++) << 8);
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++) << 8);
                }
                src = src8;
            }
            break;
        }
        break;
    }
    case CODEC_ID_PCM_LXF:
    {
        int i;
        const uint8_t *src8;
        dst_int32_t = (int32_t *)s->frame.data[0];
        n /= avctx->channels;
        // unpack and de-planarize
        for (i = 0; i < n; i++) {
            for (c = 0, src8 = src + i * 5; c < avctx->channels; c++, src8 += n * 5) {
                // extract low 20 bits and expand to 32 bits
                *dst_int32_t++ = (src8[2] << 28)        |
                                 (src8[1] << 20)        |
                                 (src8[0] << 12)        |
                                 ((src8[2] & 0xF) << 8) |
                                 src8[1];
            }
            for (c = 0, src8 = src + i * 5; c < avctx->channels; c++, src8 += n * 5) {
                // extract high 20 bits and expand to 32 bits
                *dst_int32_t++ = (src8[4] << 24)         |
                                 (src8[3] << 16)         |
                                 ((src8[2] & 0xF0) << 8) |
                                 (src8[4] << 4)          |
                                 (src8[3] >> 4);
            }
        }
        break;
    }
    default:
        return -1;
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = s->frame;

    return buf_size;
}

#if CONFIG_ENCODERS
#define PCM_ENCODER(id_, sample_fmt_, name_, long_name_)                    \
AVCodec ff_ ## name_ ## _encoder = {                                        \
    .name         = #name_,                                                 \
    .type         = AVMEDIA_TYPE_AUDIO,                                     \
    .id           = id_,                                                    \
    .init         = pcm_encode_init,                                        \
    .encode2      = pcm_encode_frame,                                       \
    .close        = pcm_encode_close,                                       \
    .capabilities = CODEC_CAP_VARIABLE_FRAME_SIZE,                          \
    .sample_fmts  = (const enum AVSampleFormat[]){ sample_fmt_,             \
                                                   AV_SAMPLE_FMT_NONE },    \
    .long_name    = NULL_IF_CONFIG_SMALL(long_name_),                       \
}
#else
#define PCM_ENCODER(id, sample_fmt_, name, long_name_)
#endif

#if CONFIG_DECODERS
#define PCM_DECODER(id_, sample_fmt_, name_, long_name_)                    \
AVCodec ff_ ## name_ ## _decoder = {                                        \
    .name           = #name_,                                               \
    .type           = AVMEDIA_TYPE_AUDIO,                                   \
    .id             = id_,                                                  \
    .priv_data_size = sizeof(PCMDecode),                                    \
    .init           = pcm_decode_init,                                      \
    .decode         = pcm_decode_frame,                                     \
    .capabilities   = CODEC_CAP_DR1,                                        \
    .sample_fmts    = (const enum AVSampleFormat[]){ sample_fmt_,           \
                                                     AV_SAMPLE_FMT_NONE },  \
    .long_name      = NULL_IF_CONFIG_SMALL(long_name_),                     \
}
#else
#define PCM_DECODER(id, sample_fmt_, name, long_name_)
#endif

#define PCM_CODEC(id, sample_fmt_, name, long_name_)                    \
    PCM_ENCODER(id, sample_fmt_, name, long_name_);                     \
    PCM_DECODER(id, sample_fmt_, name, long_name_)

/* Note: Do not forget to add new entries to the Makefile as well. */
PCM_CODEC  (CODEC_ID_PCM_ALAW,         AV_SAMPLE_FMT_S16, pcm_alaw,         "PCM A-law");
PCM_DECODER(CODEC_ID_PCM_DVD,          AV_SAMPLE_FMT_S32, pcm_dvd,          "PCM signed 20|24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_F32BE,        AV_SAMPLE_FMT_FLT, pcm_f32be,        "PCM 32-bit floating point big-endian");
PCM_CODEC  (CODEC_ID_PCM_F32LE,        AV_SAMPLE_FMT_FLT, pcm_f32le,        "PCM 32-bit floating point little-endian");
PCM_CODEC  (CODEC_ID_PCM_F64BE,        AV_SAMPLE_FMT_DBL, pcm_f64be,        "PCM 64-bit floating point big-endian");
PCM_CODEC  (CODEC_ID_PCM_F64LE,        AV_SAMPLE_FMT_DBL, pcm_f64le,        "PCM 64-bit floating point little-endian");
PCM_DECODER(CODEC_ID_PCM_LXF,          AV_SAMPLE_FMT_S32, pcm_lxf,          "PCM signed 20-bit little-endian planar");
PCM_CODEC  (CODEC_ID_PCM_MULAW,        AV_SAMPLE_FMT_S16, pcm_mulaw,        "PCM mu-law");
PCM_CODEC  (CODEC_ID_PCM_S8,           AV_SAMPLE_FMT_U8,  pcm_s8,           "PCM signed 8-bit");
PCM_CODEC  (CODEC_ID_PCM_S16BE,        AV_SAMPLE_FMT_S16, pcm_s16be,        "PCM signed 16-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S16LE,        AV_SAMPLE_FMT_S16, pcm_s16le,        "PCM signed 16-bit little-endian");
PCM_DECODER(CODEC_ID_PCM_S16LE_PLANAR, AV_SAMPLE_FMT_S16, pcm_s16le_planar, "PCM 16-bit little-endian planar");
PCM_CODEC  (CODEC_ID_PCM_S24BE,        AV_SAMPLE_FMT_S32, pcm_s24be,        "PCM signed 24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S24DAUD,      AV_SAMPLE_FMT_S16, pcm_s24daud,      "PCM D-Cinema audio signed 24-bit");
PCM_CODEC  (CODEC_ID_PCM_S24LE,        AV_SAMPLE_FMT_S32, pcm_s24le,        "PCM signed 24-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_S32BE,        AV_SAMPLE_FMT_S32, pcm_s32be,        "PCM signed 32-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S32LE,        AV_SAMPLE_FMT_S32, pcm_s32le,        "PCM signed 32-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U8,           AV_SAMPLE_FMT_U8,  pcm_u8,           "PCM unsigned 8-bit");
PCM_CODEC  (CODEC_ID_PCM_U16BE,        AV_SAMPLE_FMT_S16, pcm_u16be,        "PCM unsigned 16-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U16LE,        AV_SAMPLE_FMT_S16, pcm_u16le,        "PCM unsigned 16-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U24BE,        AV_SAMPLE_FMT_S32, pcm_u24be,        "PCM unsigned 24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U24LE,        AV_SAMPLE_FMT_S32, pcm_u24le,        "PCM unsigned 24-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U32BE,        AV_SAMPLE_FMT_S32, pcm_u32be,        "PCM unsigned 32-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U32LE,        AV_SAMPLE_FMT_S32, pcm_u32le,        "PCM unsigned 32-bit little-endian");
PCM_DECODER(CODEC_ID_PCM_ZORK,         AV_SAMPLE_FMT_U8,  pcm_zork,         "PCM Zork");
