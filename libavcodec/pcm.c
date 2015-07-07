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

#include "libavutil/attributes.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "mathops.h"
#include "pcm_tablegen.h"

static av_cold int pcm_encode_init(AVCodecContext *avctx)
{
    avctx->frame_size = 0;
    switch (avctx->codec->id) {
    case AV_CODEC_ID_PCM_ALAW:
        pcm_alaw_tableinit();
        break;
    case AV_CODEC_ID_PCM_MULAW:
        pcm_ulaw_tableinit();
        break;
    default:
        break;
    }

    avctx->bits_per_coded_sample = av_get_bits_per_sample(avctx->codec->id);
    avctx->block_align           = avctx->channels * avctx->bits_per_coded_sample / 8;
    avctx->bit_rate              = avctx->block_align * avctx->sample_rate * 8;

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
    case AV_CODEC_ID_PCM_U32LE:
        ENCODE(uint32_t, le32, samples, dst, n, 0, 0x80000000)
        break;
    case AV_CODEC_ID_PCM_U32BE:
        ENCODE(uint32_t, be32, samples, dst, n, 0, 0x80000000)
        break;
    case AV_CODEC_ID_PCM_S24LE:
        ENCODE(int32_t, le24, samples, dst, n, 8, 0)
        break;
    case AV_CODEC_ID_PCM_S24BE:
        ENCODE(int32_t, be24, samples, dst, n, 8, 0)
        break;
    case AV_CODEC_ID_PCM_U24LE:
        ENCODE(uint32_t, le24, samples, dst, n, 8, 0x800000)
        break;
    case AV_CODEC_ID_PCM_U24BE:
        ENCODE(uint32_t, be24, samples, dst, n, 8, 0x800000)
        break;
    case AV_CODEC_ID_PCM_S24DAUD:
        for (; n > 0; n--) {
            uint32_t tmp = ff_reverse[(*samples >> 8) & 0xff] +
                           (ff_reverse[*samples & 0xff] << 8);
            tmp <<= 4; // sync flags would go here
            bytestream_put_be24(&dst, tmp);
            samples++;
        }
        break;
    case AV_CODEC_ID_PCM_U16LE:
        ENCODE(uint16_t, le16, samples, dst, n, 0, 0x8000)
        break;
    case AV_CODEC_ID_PCM_U16BE:
        ENCODE(uint16_t, be16, samples, dst, n, 0, 0x8000)
        break;
    case AV_CODEC_ID_PCM_S8:
        srcu8 = frame->data[0];
        for (; n > 0; n--) {
            v      = *srcu8++;
            *dst++ = v - 128;
        }
        break;
#if HAVE_BIGENDIAN
    case AV_CODEC_ID_PCM_F64LE:
        ENCODE(int64_t, le64, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_F32LE:
        ENCODE(int32_t, le32, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S16LE:
        ENCODE(int16_t, le16, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S16BE:
#else
    case AV_CODEC_ID_PCM_F64BE:
        ENCODE(int64_t, be64, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_S32BE:
        ENCODE(int32_t, be32, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S16BE:
        ENCODE(int16_t, be16, samples, dst, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S16LE:
#endif /* HAVE_BIGENDIAN */
    case AV_CODEC_ID_PCM_U8:
        memcpy(dst, samples, n * sample_size);
        dst += n * sample_size;
        break;
    case AV_CODEC_ID_PCM_ALAW:
        for (; n > 0; n--) {
            v      = *samples++;
            *dst++ = linear_to_alaw[(v + 32768) >> 2];
        }
        break;
    case AV_CODEC_ID_PCM_MULAW:
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
    short   table[256];
} PCMDecode;

static av_cold int pcm_decode_init(AVCodecContext *avctx)
{
    PCMDecode *s = avctx->priv_data;
    int i;

    if (avctx->channels <= 0) {
        av_log(avctx, AV_LOG_ERROR, "PCM channels out of bounds\n");
        return AVERROR(EINVAL);
    }

    switch (avctx->codec->id) {
    case AV_CODEC_ID_PCM_ALAW:
        for (i = 0; i < 256; i++)
            s->table[i] = alaw2linear(i);
        break;
    case AV_CODEC_ID_PCM_MULAW:
        for (i = 0; i < 256; i++)
            s->table[i] = ulaw2linear(i);
        break;
    default:
        break;
    }

    avctx->sample_fmt = avctx->codec->sample_fmts[0];

    if (avctx->sample_fmt == AV_SAMPLE_FMT_S32)
        avctx->bits_per_raw_sample = av_get_bits_per_sample(avctx->codec->id);

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

#if HAVE_BIGENDIAN
#define DECODE_PLANAR(size, endian, src, dst, n, shift, offset)         \
    {                                                                   \
        int n2;                                                         \
        n /= avctx->channels;                                           \
        for (c = 0; c < avctx->channels; c++) {                         \
            samples = frame->extended_data[c];                          \
            n2 = n;                                                     \
            DECODE(size, endian, src, samples, n2, 0, 0)                \
        }                                                               \
    }
#else
#define DECODE_PLANAR(size, endian, src, dst, n, shift, offset)         \
    {                                                                   \
        n /= avctx->channels;                                           \
        for (c = 0; c < avctx->channels; c++) {                         \
            samples = frame->extended_data[c];                          \
            memcpy(samples, src, n * size / 8);                         \
            src += n * size / 8;                                        \
        }                                                               \
    }
#endif /* HAVE_BIGENDIAN */

static int pcm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *src = avpkt->data;
    int buf_size       = avpkt->size;
    PCMDecode *s       = avctx->priv_data;
    AVFrame *frame     = data;
    int sample_size, c, n, ret, samples_per_block;
    uint8_t *samples;
    int32_t *dst_int32_t;

    sample_size = av_get_bits_per_sample(avctx->codec_id) / 8;

    /* av_get_bits_per_sample returns 0 for AV_CODEC_ID_PCM_DVD */
    samples_per_block = 1;
    if (avctx->codec_id == AV_CODEC_ID_PCM_LXF) {
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
    frame->nb_samples = n * samples_per_block / avctx->channels;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples = frame->data[0];

    switch (avctx->codec->id) {
    case AV_CODEC_ID_PCM_U32LE:
        DECODE(32, le32, src, samples, n, 0, 0x80000000)
        break;
    case AV_CODEC_ID_PCM_U32BE:
        DECODE(32, be32, src, samples, n, 0, 0x80000000)
        break;
    case AV_CODEC_ID_PCM_S24LE:
        DECODE(32, le24, src, samples, n, 8, 0)
        break;
    case AV_CODEC_ID_PCM_S24BE:
        DECODE(32, be24, src, samples, n, 8, 0)
        break;
    case AV_CODEC_ID_PCM_U24LE:
        DECODE(32, le24, src, samples, n, 8, 0x800000)
        break;
    case AV_CODEC_ID_PCM_U24BE:
        DECODE(32, be24, src, samples, n, 8, 0x800000)
        break;
    case AV_CODEC_ID_PCM_S24DAUD:
        for (; n > 0; n--) {
            uint32_t v = bytestream_get_be24(&src);
            v >>= 4; // sync flags are here
            AV_WN16A(samples, ff_reverse[(v >> 8) & 0xff] +
                             (ff_reverse[v        & 0xff] << 8));
            samples += 2;
        }
        break;
    case AV_CODEC_ID_PCM_S16BE_PLANAR:
        DECODE_PLANAR(16, be16, src, samples, n, 0, 0);
        break;
    case AV_CODEC_ID_PCM_S16LE_PLANAR:
        DECODE_PLANAR(16, le16, src, samples, n, 0, 0);
        break;
    case AV_CODEC_ID_PCM_S24LE_PLANAR:
        DECODE_PLANAR(32, le24, src, samples, n, 8, 0);
        break;
    case AV_CODEC_ID_PCM_S32LE_PLANAR:
        DECODE_PLANAR(32, le32, src, samples, n, 0, 0);
        break;
    case AV_CODEC_ID_PCM_U16LE:
        DECODE(16, le16, src, samples, n, 0, 0x8000)
        break;
    case AV_CODEC_ID_PCM_U16BE:
        DECODE(16, be16, src, samples, n, 0, 0x8000)
        break;
    case AV_CODEC_ID_PCM_S8:
        for (; n > 0; n--)
            *samples++ = *src++ + 128;
        break;
#if HAVE_BIGENDIAN
    case AV_CODEC_ID_PCM_F64LE:
        DECODE(64, le64, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_F32LE:
        DECODE(32, le32, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S16LE:
        DECODE(16, le16, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S16BE:
#else
    case AV_CODEC_ID_PCM_F64BE:
        DECODE(64, be64, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_S32BE:
        DECODE(32, be32, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_S16BE:
        DECODE(16, be16, src, samples, n, 0, 0)
        break;
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S16LE:
#endif /* HAVE_BIGENDIAN */
    case AV_CODEC_ID_PCM_U8:
        memcpy(samples, src, n * sample_size);
        break;
    case AV_CODEC_ID_PCM_ZORK:
        for (; n > 0; n--) {
            int v = *src++;
            if (v < 128)
                v = 128 - v;
            *samples++ = v;
        }
        break;
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
        for (; n > 0; n--) {
            AV_WN16A(samples, s->table[*src++]);
            samples += 2;
        }
        break;
    case AV_CODEC_ID_PCM_LXF:
    {
        int i;
        n /= avctx->channels;
        for (c = 0; c < avctx->channels; c++) {
            dst_int32_t = (int32_t *)frame->extended_data[c];
            for (i = 0; i < n; i++) {
                // extract low 20 bits and expand to 32 bits
                *dst_int32_t++ =  (src[2]         << 28) |
                                  (src[1]         << 20) |
                                  (src[0]         << 12) |
                                 ((src[2] & 0x0F) <<  8) |
                                   src[1];
                // extract high 20 bits and expand to 32 bits
                *dst_int32_t++ =  (src[4]         << 24) |
                                  (src[3]         << 16) |
                                 ((src[2] & 0xF0) <<  8) |
                                  (src[4]         <<  4) |
                                  (src[3]         >>  4);
                src += 5;
            }
        }
        break;
    }
    default:
        return -1;
    }

    *got_frame_ptr = 1;

    return buf_size;
}

#define PCM_ENCODER_0(id_, sample_fmt_, name_, long_name_)
#define PCM_ENCODER_1(id_, sample_fmt_, name_, long_name_)                  \
AVCodec ff_ ## name_ ## _encoder = {                                        \
    .name         = #name_,                                                 \
    .long_name    = NULL_IF_CONFIG_SMALL(long_name_),                       \
    .type         = AVMEDIA_TYPE_AUDIO,                                     \
    .id           = AV_CODEC_ID_ ## id_,                                    \
    .init         = pcm_encode_init,                                        \
    .encode2      = pcm_encode_frame,                                       \
    .capabilities = AV_CODEC_CAP_VARIABLE_FRAME_SIZE,                       \
    .sample_fmts  = (const enum AVSampleFormat[]){ sample_fmt_,             \
                                                   AV_SAMPLE_FMT_NONE },    \
}

#define PCM_ENCODER_2(cf, id, sample_fmt, name, long_name)                  \
    PCM_ENCODER_ ## cf(id, sample_fmt, name, long_name)
#define PCM_ENCODER_3(cf, id, sample_fmt, name, long_name)                  \
    PCM_ENCODER_2(cf, id, sample_fmt, name, long_name)
#define PCM_ENCODER(id, sample_fmt, name, long_name)                        \
    PCM_ENCODER_3(CONFIG_ ## id ## _ENCODER, id, sample_fmt, name, long_name)

#define PCM_DECODER_0(id, sample_fmt, name, long_name)
#define PCM_DECODER_1(id_, sample_fmt_, name_, long_name_)                  \
AVCodec ff_ ## name_ ## _decoder = {                                        \
    .name           = #name_,                                               \
    .long_name      = NULL_IF_CONFIG_SMALL(long_name_),                     \
    .type           = AVMEDIA_TYPE_AUDIO,                                   \
    .id             = AV_CODEC_ID_ ## id_,                                  \
    .priv_data_size = sizeof(PCMDecode),                                    \
    .init           = pcm_decode_init,                                      \
    .decode         = pcm_decode_frame,                                     \
    .capabilities   = AV_CODEC_CAP_DR1,                                     \
    .sample_fmts    = (const enum AVSampleFormat[]){ sample_fmt_,           \
                                                     AV_SAMPLE_FMT_NONE },  \
}

#define PCM_DECODER_2(cf, id, sample_fmt, name, long_name)                  \
    PCM_DECODER_ ## cf(id, sample_fmt, name, long_name)
#define PCM_DECODER_3(cf, id, sample_fmt, name, long_name)                  \
    PCM_DECODER_2(cf, id, sample_fmt, name, long_name)
#define PCM_DECODER(id, sample_fmt, name, long_name)                        \
    PCM_DECODER_3(CONFIG_ ## id ## _DECODER, id, sample_fmt, name, long_name)

#define PCM_CODEC(id, sample_fmt_, name, long_name_)                    \
    PCM_ENCODER(id, sample_fmt_, name, long_name_);                     \
    PCM_DECODER(id, sample_fmt_, name, long_name_)

/* Note: Do not forget to add new entries to the Makefile as well. */
PCM_CODEC  (PCM_ALAW,         AV_SAMPLE_FMT_S16, pcm_alaw,         "PCM A-law");
PCM_CODEC  (PCM_F32BE,        AV_SAMPLE_FMT_FLT, pcm_f32be,        "PCM 32-bit floating point big-endian");
PCM_CODEC  (PCM_F32LE,        AV_SAMPLE_FMT_FLT, pcm_f32le,        "PCM 32-bit floating point little-endian");
PCM_CODEC  (PCM_F64BE,        AV_SAMPLE_FMT_DBL, pcm_f64be,        "PCM 64-bit floating point big-endian");
PCM_CODEC  (PCM_F64LE,        AV_SAMPLE_FMT_DBL, pcm_f64le,        "PCM 64-bit floating point little-endian");
PCM_DECODER(PCM_LXF,          AV_SAMPLE_FMT_S32P, pcm_lxf,          "PCM signed 20-bit little-endian planar");
PCM_CODEC  (PCM_MULAW,        AV_SAMPLE_FMT_S16, pcm_mulaw,        "PCM mu-law");
PCM_CODEC  (PCM_S8,           AV_SAMPLE_FMT_U8,  pcm_s8,           "PCM signed 8-bit");
PCM_CODEC  (PCM_S16BE,        AV_SAMPLE_FMT_S16, pcm_s16be,        "PCM signed 16-bit big-endian");
PCM_DECODER(PCM_S16BE_PLANAR, AV_SAMPLE_FMT_S16P,pcm_s16be_planar, "PCM signed 16-bit big-endian planar");
PCM_CODEC  (PCM_S16LE,        AV_SAMPLE_FMT_S16, pcm_s16le,        "PCM signed 16-bit little-endian");
PCM_DECODER(PCM_S16LE_PLANAR, AV_SAMPLE_FMT_S16P, pcm_s16le_planar, "PCM 16-bit little-endian planar");
PCM_CODEC  (PCM_S24BE,        AV_SAMPLE_FMT_S32, pcm_s24be,        "PCM signed 24-bit big-endian");
PCM_CODEC  (PCM_S24DAUD,      AV_SAMPLE_FMT_S16, pcm_s24daud,      "PCM D-Cinema audio signed 24-bit");
PCM_CODEC  (PCM_S24LE,        AV_SAMPLE_FMT_S32, pcm_s24le,        "PCM signed 24-bit little-endian");
PCM_DECODER(PCM_S24LE_PLANAR, AV_SAMPLE_FMT_S32P,pcm_s24le_planar, "PCM signed 24-bit little-endian planar");
PCM_CODEC  (PCM_S32BE,        AV_SAMPLE_FMT_S32, pcm_s32be,        "PCM signed 32-bit big-endian");
PCM_CODEC  (PCM_S32LE,        AV_SAMPLE_FMT_S32, pcm_s32le,        "PCM signed 32-bit little-endian");
PCM_DECODER(PCM_S32LE_PLANAR, AV_SAMPLE_FMT_S32P,pcm_s32le_planar, "PCM signed 32-bit little-endian planar");
PCM_CODEC  (PCM_U8,           AV_SAMPLE_FMT_U8,  pcm_u8,           "PCM unsigned 8-bit");
PCM_CODEC  (PCM_U16BE,        AV_SAMPLE_FMT_S16, pcm_u16be,        "PCM unsigned 16-bit big-endian");
PCM_CODEC  (PCM_U16LE,        AV_SAMPLE_FMT_S16, pcm_u16le,        "PCM unsigned 16-bit little-endian");
PCM_CODEC  (PCM_U24BE,        AV_SAMPLE_FMT_S32, pcm_u24be,        "PCM unsigned 24-bit big-endian");
PCM_CODEC  (PCM_U24LE,        AV_SAMPLE_FMT_S32, pcm_u24le,        "PCM unsigned 24-bit little-endian");
PCM_CODEC  (PCM_U32BE,        AV_SAMPLE_FMT_S32, pcm_u32be,        "PCM unsigned 32-bit big-endian");
PCM_CODEC  (PCM_U32LE,        AV_SAMPLE_FMT_S32, pcm_u32le,        "PCM unsigned 32-bit little-endian");
PCM_DECODER(PCM_ZORK,         AV_SAMPLE_FMT_U8,  pcm_zork,         "PCM Zork");
