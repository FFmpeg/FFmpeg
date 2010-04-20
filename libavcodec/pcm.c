/*
 * PCM codecs
 * Copyright (c) 2001 Fabrice Bellard
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
 * PCM codecs
 */

#include "avcodec.h"
#include "libavutil/common.h" /* for av_reverse */
#include "bytestream.h"
#include "pcm_tablegen.h"

#define MAX_CHANNELS 64

static av_cold int pcm_encode_init(AVCodecContext *avctx)
{
    avctx->frame_size = 1;
    switch(avctx->codec->id) {
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
    avctx->block_align = avctx->channels * avctx->bits_per_coded_sample/8;
    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

    return 0;
}

static av_cold int pcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

/**
 * Write PCM samples macro
 * @param type Datatype of native machine format
 * @param endian bytestream_put_xxx() suffix
 * @param src Source pointer (variable name)
 * @param dst Destination pointer (variable name)
 * @param n Total number of samples (variable name)
 * @param shift Bitshift (bits)
 * @param offset Sample value offset
 */
#define ENCODE(type, endian, src, dst, n, shift, offset) \
    samples_##type = (type*)src; \
    for(;n>0;n--) { \
        register type v = (*samples_##type++ >> shift) + offset; \
        bytestream_put_##endian(&dst, v); \
    }

static int pcm_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    int n, sample_size, v;
    short *samples;
    unsigned char *dst;
    uint8_t *srcu8;
    int16_t *samples_int16_t;
    int32_t *samples_int32_t;
    int64_t *samples_int64_t;
    uint16_t *samples_uint16_t;
    uint32_t *samples_uint32_t;

    sample_size = av_get_bits_per_sample(avctx->codec->id)/8;
    n = buf_size / sample_size;
    samples = data;
    dst = frame;

    if (avctx->sample_fmt!=avctx->codec->sample_fmts[0]) {
        av_log(avctx, AV_LOG_ERROR, "invalid sample_fmt\n");
        return -1;
    }

    switch(avctx->codec->id) {
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
        for(;n>0;n--) {
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
        srcu8= data;
        for(;n>0;n--) {
            v = *srcu8++;
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
        memcpy(dst, samples, n*sample_size);
        dst += n*sample_size;
        break;
    case CODEC_ID_PCM_ZORK:
        for(;n>0;n--) {
            v= *samples++ >> 8;
            if(v<0)   v = -v;
            else      v+= 128;
            *dst++ = v;
        }
        break;
    case CODEC_ID_PCM_ALAW:
        for(;n>0;n--) {
            v = *samples++;
            *dst++ = linear_to_alaw[(v + 32768) >> 2];
        }
        break;
    case CODEC_ID_PCM_MULAW:
        for(;n>0;n--) {
            v = *samples++;
            *dst++ = linear_to_ulaw[(v + 32768) >> 2];
        }
        break;
    default:
        return -1;
    }
    //avctx->frame_size = (dst - frame) / (sample_size * avctx->channels);

    return dst - frame;
}

typedef struct PCMDecode {
    short table[256];
} PCMDecode;

static av_cold int pcm_decode_init(AVCodecContext * avctx)
{
    PCMDecode *s = avctx->priv_data;
    int i;

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        for(i=0;i<256;i++)
            s->table[i] = alaw2linear(i);
        break;
    case CODEC_ID_PCM_MULAW:
        for(i=0;i<256;i++)
            s->table[i] = ulaw2linear(i);
        break;
    default:
        break;
    }

    avctx->sample_fmt = avctx->codec->sample_fmts[0];
    return 0;
}

/**
 * Read PCM samples macro
 * @param type Datatype of native machine format
 * @param endian bytestream_get_xxx() endian suffix
 * @param src Source pointer (variable name)
 * @param dst Destination pointer (variable name)
 * @param n Total number of samples (variable name)
 * @param shift Bitshift (bits)
 * @param offset Sample value offset
 */
#define DECODE(type, endian, src, dst, n, shift, offset) \
    dst_##type = (type*)dst; \
    for(;n>0;n--) { \
        register type v = bytestream_get_##endian(&src); \
        *dst_##type++ = (v - offset) << shift; \
    } \
    dst = (short*)dst_##type;

static int pcm_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    PCMDecode *s = avctx->priv_data;
    int sample_size, c, n;
    short *samples;
    const uint8_t *src, *src8, *src2[MAX_CHANNELS];
    uint8_t *dstu8;
    int16_t *dst_int16_t;
    int32_t *dst_int32_t;
    int64_t *dst_int64_t;
    uint16_t *dst_uint16_t;
    uint32_t *dst_uint32_t;

    samples = data;
    src = buf;

    if (avctx->sample_fmt!=avctx->codec->sample_fmts[0]) {
        av_log(avctx, AV_LOG_ERROR, "invalid sample_fmt\n");
        return -1;
    }

    if(avctx->channels <= 0 || avctx->channels > MAX_CHANNELS){
        av_log(avctx, AV_LOG_ERROR, "PCM channels out of bounds\n");
        return -1;
    }

    sample_size = av_get_bits_per_sample(avctx->codec_id)/8;

    /* av_get_bits_per_sample returns 0 for CODEC_ID_PCM_DVD */
    if (CODEC_ID_PCM_DVD == avctx->codec_id)
        /* 2 samples are interleaved per block in PCM_DVD */
        sample_size = avctx->bits_per_coded_sample * 2 / 8;

    n = avctx->channels * sample_size;

    if(n && buf_size % n){
        if (buf_size < n) {
            av_log(avctx, AV_LOG_ERROR, "invalid PCM packet\n");
            return -1;
        }else
            buf_size -= buf_size % n;
    }

    buf_size= FFMIN(buf_size, *data_size/2);
    *data_size=0;

    n = buf_size/sample_size;

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_U32LE:
        DECODE(uint32_t, le32, src, samples, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_U32BE:
        DECODE(uint32_t, be32, src, samples, n, 0, 0x80000000)
        break;
    case CODEC_ID_PCM_S24LE:
        DECODE(int32_t, le24, src, samples, n, 8, 0)
        break;
    case CODEC_ID_PCM_S24BE:
        DECODE(int32_t, be24, src, samples, n, 8, 0)
        break;
    case CODEC_ID_PCM_U24LE:
        DECODE(uint32_t, le24, src, samples, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_U24BE:
        DECODE(uint32_t, be24, src, samples, n, 8, 0x800000)
        break;
    case CODEC_ID_PCM_S24DAUD:
        for(;n>0;n--) {
          uint32_t v = bytestream_get_be24(&src);
          v >>= 4; // sync flags are here
          *samples++ = av_reverse[(v >> 8) & 0xff] +
                       (av_reverse[v & 0xff] << 8);
        }
        break;
    case CODEC_ID_PCM_S16LE_PLANAR:
        n /= avctx->channels;
        for(c=0;c<avctx->channels;c++)
            src2[c] = &src[c*n*2];
        for(;n>0;n--)
            for(c=0;c<avctx->channels;c++)
                *samples++ = bytestream_get_le16(&src2[c]);
        src = src2[avctx->channels-1];
        break;
    case CODEC_ID_PCM_U16LE:
        DECODE(uint16_t, le16, src, samples, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_U16BE:
        DECODE(uint16_t, be16, src, samples, n, 0, 0x8000)
        break;
    case CODEC_ID_PCM_S8:
        dstu8= (uint8_t*)samples;
        for(;n>0;n--) {
            *dstu8++ = *src++ + 128;
        }
        samples= (short*)dstu8;
        break;
#if HAVE_BIGENDIAN
    case CODEC_ID_PCM_F64LE:
        DECODE(int64_t, le64, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_F32LE:
        DECODE(int32_t, le32, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16LE:
        DECODE(int16_t, le16, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64BE:
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_S16BE:
#else
    case CODEC_ID_PCM_F64BE:
        DECODE(int64_t, be64, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_S32BE:
        DECODE(int32_t, be32, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_S16BE:
        DECODE(int16_t, be16, src, samples, n, 0, 0)
        break;
    case CODEC_ID_PCM_F64LE:
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S16LE:
#endif /* HAVE_BIGENDIAN */
    case CODEC_ID_PCM_U8:
        memcpy(samples, src, n*sample_size);
        src += n*sample_size;
        samples = (short*)((uint8_t*)data + n*sample_size);
        break;
    case CODEC_ID_PCM_ZORK:
        for(;n>0;n--) {
            int x= *src++;
            if(x&128) x-= 128;
            else      x = -x;
            *samples++ = x << 8;
        }
        break;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        for(;n>0;n--) {
            *samples++ = s->table[*src++];
        }
        break;
    case CODEC_ID_PCM_DVD:
        dst_int32_t = data;
        n /= avctx->channels;
        switch (avctx->bits_per_coded_sample) {
        case 20:
            while (n--) {
                c = avctx->channels;
                src8 = src + 4*c;
                while (c--) {
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8   &0xf0) << 8);
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++ &0x0f) << 12);
                }
                src = src8;
            }
            break;
        case 24:
            while (n--) {
                c = avctx->channels;
                src8 = src + 4*c;
                while (c--) {
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++) << 8);
                    *dst_int32_t++ = (bytestream_get_be16(&src) << 16) + ((*src8++) << 8);
                }
                src = src8;
            }
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "PCM DVD unsupported sample depth\n");
            return -1;
            break;
        }
        samples = (short *) dst_int32_t;
        break;
    default:
        return -1;
    }
    *data_size = (uint8_t *)samples - (uint8_t *)data;
    return src - buf;
}

#if CONFIG_ENCODERS
#define PCM_ENCODER(id,sample_fmt_,name,long_name_) \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    AVMEDIA_TYPE_AUDIO,                         \
    id,                                         \
    0,                                          \
    pcm_encode_init,                            \
    pcm_encode_frame,                           \
    pcm_encode_close,                           \
    NULL,                                       \
    .sample_fmts = (const enum SampleFormat[]){sample_fmt_,SAMPLE_FMT_NONE}, \
    .long_name = NULL_IF_CONFIG_SMALL(long_name_), \
};
#else
#define PCM_ENCODER(id,sample_fmt_,name,long_name_)
#endif

#if CONFIG_DECODERS
#define PCM_DECODER(id,sample_fmt_,name,long_name_)         \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    AVMEDIA_TYPE_AUDIO,                         \
    id,                                         \
    sizeof(PCMDecode),                          \
    pcm_decode_init,                            \
    NULL,                                       \
    NULL,                                       \
    pcm_decode_frame,                           \
    .sample_fmts = (const enum SampleFormat[]){sample_fmt_,SAMPLE_FMT_NONE}, \
    .long_name = NULL_IF_CONFIG_SMALL(long_name_), \
};
#else
#define PCM_DECODER(id,sample_fmt_,name,long_name_)
#endif

#define PCM_CODEC(id, sample_fmt_, name, long_name_)         \
    PCM_ENCODER(id,sample_fmt_,name,long_name_) PCM_DECODER(id,sample_fmt_,name,long_name_)

/* Note: Do not forget to add new entries to the Makefile as well. */
PCM_CODEC  (CODEC_ID_PCM_ALAW,  SAMPLE_FMT_S16, pcm_alaw, "PCM A-law");
PCM_CODEC  (CODEC_ID_PCM_DVD,   SAMPLE_FMT_S32, pcm_dvd, "PCM signed 20|24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_F32BE, SAMPLE_FMT_FLT, pcm_f32be, "PCM 32-bit floating point big-endian");
PCM_CODEC  (CODEC_ID_PCM_F32LE, SAMPLE_FMT_FLT, pcm_f32le, "PCM 32-bit floating point little-endian");
PCM_CODEC  (CODEC_ID_PCM_F64BE, SAMPLE_FMT_DBL, pcm_f64be, "PCM 64-bit floating point big-endian");
PCM_CODEC  (CODEC_ID_PCM_F64LE, SAMPLE_FMT_DBL, pcm_f64le, "PCM 64-bit floating point little-endian");
PCM_CODEC  (CODEC_ID_PCM_MULAW, SAMPLE_FMT_S16, pcm_mulaw, "PCM mu-law");
PCM_CODEC  (CODEC_ID_PCM_S8,    SAMPLE_FMT_U8,  pcm_s8, "PCM signed 8-bit");
PCM_CODEC  (CODEC_ID_PCM_S16BE, SAMPLE_FMT_S16, pcm_s16be, "PCM signed 16-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S16LE, SAMPLE_FMT_S16, pcm_s16le, "PCM signed 16-bit little-endian");
PCM_DECODER(CODEC_ID_PCM_S16LE_PLANAR, SAMPLE_FMT_S16, pcm_s16le_planar, "PCM 16-bit little-endian planar");
PCM_CODEC  (CODEC_ID_PCM_S24BE, SAMPLE_FMT_S32, pcm_s24be, "PCM signed 24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S24DAUD, SAMPLE_FMT_S16,  pcm_s24daud, "PCM D-Cinema audio signed 24-bit");
PCM_CODEC  (CODEC_ID_PCM_S24LE, SAMPLE_FMT_S32, pcm_s24le, "PCM signed 24-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_S32BE, SAMPLE_FMT_S32, pcm_s32be, "PCM signed 32-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_S32LE, SAMPLE_FMT_S32, pcm_s32le, "PCM signed 32-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U8,    SAMPLE_FMT_U8,  pcm_u8, "PCM unsigned 8-bit");
PCM_CODEC  (CODEC_ID_PCM_U16BE, SAMPLE_FMT_S16, pcm_u16be, "PCM unsigned 16-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U16LE, SAMPLE_FMT_S16, pcm_u16le, "PCM unsigned 16-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U24BE, SAMPLE_FMT_S32, pcm_u24be, "PCM unsigned 24-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U24LE, SAMPLE_FMT_S32, pcm_u24le, "PCM unsigned 24-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_U32BE, SAMPLE_FMT_S32, pcm_u32be, "PCM unsigned 32-bit big-endian");
PCM_CODEC  (CODEC_ID_PCM_U32LE, SAMPLE_FMT_S32, pcm_u32le, "PCM unsigned 32-bit little-endian");
PCM_CODEC  (CODEC_ID_PCM_ZORK,  SAMPLE_FMT_S16, pcm_zork, "PCM Zork");
