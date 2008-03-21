/*
 * PCM codecs
 * Copyright (c) 2001 Fabrice Bellard.
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
 * @file pcm.c
 * PCM codecs
 */

#include "avcodec.h"
#include "bitstream.h" // for ff_reverse
#include "bytestream.h"

#define MAX_CHANNELS 64

/* from g711.c by SUN microsystems (unrestricted use) */

#define         SIGN_BIT        (0x80)      /* Sign bit for a A-law byte. */
#define         QUANT_MASK      (0xf)       /* Quantization field mask. */
#define         NSEGS           (8)         /* Number of A-law segments. */
#define         SEG_SHIFT       (4)         /* Left shift for segment number. */
#define         SEG_MASK        (0x70)      /* Segment field mask. */

#define         BIAS            (0x84)      /* Bias for linear code. */

/*
 * alaw2linear() - Convert an A-law value to 16-bit linear PCM
 *
 */
static av_cold int alaw2linear(unsigned char a_val)
{
        int t;
        int seg;

        a_val ^= 0x55;

        t = a_val & QUANT_MASK;
        seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
        if(seg) t= (t + t + 1 + 32) << (seg + 2);
        else    t= (t + t + 1     ) << 3;

        return ((a_val & SIGN_BIT) ? t : -t);
}

static av_cold int ulaw2linear(unsigned char u_val)
{
        int t;

        /* Complement to obtain normal u-law value. */
        u_val = ~u_val;

        /*
         * Extract and bias the quantization bits. Then
         * shift up by the segment number and subtract out the bias.
         */
        t = ((u_val & QUANT_MASK) << 3) + BIAS;
        t <<= ((unsigned)u_val & SEG_MASK) >> SEG_SHIFT;

        return ((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS));
}

/* 16384 entries per table */
static uint8_t linear_to_alaw[16384];
static uint8_t linear_to_ulaw[16384];

static av_cold void build_xlaw_table(uint8_t *linear_to_xlaw,
                             int (*xlaw2linear)(unsigned char),
                             int mask)
{
    int i, j, v, v1, v2;

    j = 0;
    for(i=0;i<128;i++) {
        if (i != 127) {
            v1 = xlaw2linear(i ^ mask);
            v2 = xlaw2linear((i + 1) ^ mask);
            v = (v1 + v2 + 4) >> 3;
        } else {
            v = 8192;
        }
        for(;j<v;j++) {
            linear_to_xlaw[8192 + j] = (i ^ mask);
            if (j > 0)
                linear_to_xlaw[8192 - j] = (i ^ (mask ^ 0x80));
        }
    }
    linear_to_xlaw[0] = linear_to_xlaw[1];
}

static av_cold int pcm_encode_init(AVCodecContext *avctx)
{
    avctx->frame_size = 1;
    switch(avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        build_xlaw_table(linear_to_alaw, alaw2linear, 0xd5);
        break;
    case CODEC_ID_PCM_MULAW:
        build_xlaw_table(linear_to_ulaw, ulaw2linear, 0xff);
        break;
    default:
        break;
    }

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_U32LE:
    case CODEC_ID_PCM_U32BE:
        avctx->block_align = 4 * avctx->channels;
        break;
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_U24LE:
    case CODEC_ID_PCM_U24BE:
    case CODEC_ID_PCM_S24DAUD:
        avctx->block_align = 3 * avctx->channels;
        break;
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
        avctx->block_align = 2 * avctx->channels;
        break;
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    case CODEC_ID_PCM_MULAW:
    case CODEC_ID_PCM_ALAW:
        avctx->block_align = avctx->channels;
        break;
    default:
        break;
    }

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
 * \brief convert samples from 16 bit
 * \param bps byte per sample for the destination format, must be >= 2
 * \param le 0 for big-, 1 for little-endian
 * \param us 0 for signed, 1 for unsigned output
 * \param samples input samples
 * \param dst output samples
 * \param n number of samples in samples buffer.
 */
static inline void encode_from16(int bps, int le, int us,
                               short **samples, uint8_t **dst, int n) {
    int usum = us ? 0x8000 : 0;
    if (bps > 2)
        memset(*dst, 0, n * bps);
    if (le) *dst += bps - 2;
    for(;n>0;n--) {
        register int v = *(*samples)++;
        v += usum;
        if (le) AV_WL16(*dst, v);
        else    AV_WB16(*dst, v);
        *dst += bps;
    }
    if (le) *dst -= bps - 2;
}

static int pcm_encode_frame(AVCodecContext *avctx,
                            unsigned char *frame, int buf_size, void *data)
{
    int n, sample_size, v;
    short *samples;
    unsigned char *dst;

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_S32LE:
    case CODEC_ID_PCM_S32BE:
    case CODEC_ID_PCM_U32LE:
    case CODEC_ID_PCM_U32BE:
        sample_size = 4;
        break;
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_U24LE:
    case CODEC_ID_PCM_U24BE:
    case CODEC_ID_PCM_S24DAUD:
        sample_size = 3;
        break;
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
        sample_size = 2;
        break;
    default:
        sample_size = 1;
        break;
    }
    n = buf_size / sample_size;
    samples = data;
    dst = frame;

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_S32LE:
        encode_from16(4, 1, 0, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_S32BE:
        encode_from16(4, 0, 0, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_U32LE:
        encode_from16(4, 1, 1, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_U32BE:
        encode_from16(4, 0, 1, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_S24LE:
        encode_from16(3, 1, 0, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_S24BE:
        encode_from16(3, 0, 0, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_U24LE:
        encode_from16(3, 1, 1, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_U24BE:
        encode_from16(3, 0, 1, &samples, &dst, n);
        break;
    case CODEC_ID_PCM_S24DAUD:
        for(;n>0;n--) {
            uint32_t tmp = ff_reverse[*samples >> 8] +
                           (ff_reverse[*samples & 0xff] << 8);
            tmp <<= 4; // sync flags would go here
            bytestream_put_be24(&dst, tmp);
            samples++;
        }
        break;
    case CODEC_ID_PCM_S16LE:
        for(;n>0;n--) {
            v = *samples++;
            bytestream_put_le16(&dst, v);
        }
        break;
    case CODEC_ID_PCM_S16BE:
        for(;n>0;n--) {
            v = *samples++;
            bytestream_put_be16(&dst, v);
        }
        break;
    case CODEC_ID_PCM_U16LE:
        for(;n>0;n--) {
            v = *samples++;
            v += 0x8000;
            bytestream_put_le16(&dst, v);
        }
        break;
    case CODEC_ID_PCM_U16BE:
        for(;n>0;n--) {
            v = *samples++;
            v += 0x8000;
            bytestream_put_be16(&dst, v);
        }
        break;
    case CODEC_ID_PCM_S8:
        for(;n>0;n--) {
            v = *samples++;
            *dst++ = v >> 8;
        }
        break;
    case CODEC_ID_PCM_U8:
        for(;n>0;n--) {
            v = *samples++;
            *dst++ = (v >> 8) + 128;
        }
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
    return 0;
}

/**
 * \brief convert samples to 16 bit
 * \param bps byte per sample for the source format, must be >= 2
 * \param le 0 for big-, 1 for little-endian
 * \param us 0 for signed, 1 for unsigned input
 * \param src input samples
 * \param samples output samples
 * \param src_len number of bytes in src
 */
static inline void decode_to16(int bps, int le, int us,
                               const uint8_t **src, short **samples, int src_len)
{
    int usum = us ? -0x8000 : 0;
    register int n = src_len / bps;
    if (le) *src += bps - 2;
    for(;n>0;n--) {
        register int v;
        if (le) v = AV_RL16(*src);
        else    v = AV_RB16(*src);
        v += usum;
        *(*samples)++ = v;
        *src += bps;
    }
    if (le) *src -= bps - 2;
}

static int pcm_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    PCMDecode *s = avctx->priv_data;
    int c, n;
    short *samples;
    const uint8_t *src, *src2[MAX_CHANNELS];

    samples = data;
    src = buf;

    n= av_get_bits_per_sample(avctx->codec_id)/8;
    if(n && buf_size % n){
        av_log(avctx, AV_LOG_ERROR, "invalid PCM packet\n");
        return -1;
    }
    if(avctx->channels <= 0 || avctx->channels > MAX_CHANNELS){
        av_log(avctx, AV_LOG_ERROR, "PCM channels out of bounds\n");
        return -1;
    }

    buf_size= FFMIN(buf_size, *data_size/2);
    *data_size=0;

    n = buf_size/avctx->channels;
    for(c=0;c<avctx->channels;c++)
        src2[c] = &src[c*n];

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_S32LE:
        decode_to16(4, 1, 0, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_S32BE:
        decode_to16(4, 0, 0, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_U32LE:
        decode_to16(4, 1, 1, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_U32BE:
        decode_to16(4, 0, 1, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_S24LE:
        decode_to16(3, 1, 0, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_S24BE:
        decode_to16(3, 0, 0, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_U24LE:
        decode_to16(3, 1, 1, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_U24BE:
        decode_to16(3, 0, 1, &src, &samples, buf_size);
        break;
    case CODEC_ID_PCM_S24DAUD:
        n = buf_size / 3;
        for(;n>0;n--) {
          uint32_t v = bytestream_get_be24(&src);
          v >>= 4; // sync flags are here
          *samples++ = ff_reverse[(v >> 8) & 0xff] +
                       (ff_reverse[v & 0xff] << 8);
        }
        break;
    case CODEC_ID_PCM_S16LE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = bytestream_get_le16(&src);
        }
        break;
    case CODEC_ID_PCM_S16LE_PLANAR:
        for(n>>=1;n>0;n--)
            for(c=0;c<avctx->channels;c++)
                *samples++ = bytestream_get_le16(&src2[c]);
        src = src2[avctx->channels-1];
        break;
    case CODEC_ID_PCM_S16BE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = bytestream_get_be16(&src);
        }
        break;
    case CODEC_ID_PCM_U16LE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = bytestream_get_le16(&src) - 0x8000;
        }
        break;
    case CODEC_ID_PCM_U16BE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = bytestream_get_be16(&src) - 0x8000;
        }
        break;
    case CODEC_ID_PCM_S8:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = *src++ << 8;
        }
        break;
    case CODEC_ID_PCM_U8:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = ((int)*src++ - 128) << 8;
        }
        break;
    case CODEC_ID_PCM_ZORK:
        n = buf_size;
        for(;n>0;n--) {
            int x= *src++;
            if(x&128) x-= 128;
            else      x = -x;
            *samples++ = x << 8;
        }
        break;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = s->table[*src++];
        }
        break;
    default:
        return -1;
    }
    *data_size = (uint8_t *)samples - (uint8_t *)data;
    return src - buf;
}

#ifdef CONFIG_ENCODERS
#define PCM_ENCODER(id,name)                    \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    0,                                          \
    pcm_encode_init,                            \
    pcm_encode_frame,                           \
    pcm_encode_close,                           \
    NULL,                                       \
};
#else
#define PCM_ENCODER(id,name)
#endif

#ifdef CONFIG_DECODERS
#define PCM_DECODER(id,name)                    \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(PCMDecode),                          \
    pcm_decode_init,                            \
    NULL,                                       \
    NULL,                                       \
    pcm_decode_frame,                           \
};
#else
#define PCM_DECODER(id,name)
#endif

#define PCM_CODEC(id, name)                     \
PCM_ENCODER(id,name) PCM_DECODER(id,name)

PCM_CODEC  (CODEC_ID_PCM_S32LE, pcm_s32le);
PCM_CODEC  (CODEC_ID_PCM_S32BE, pcm_s32be);
PCM_CODEC  (CODEC_ID_PCM_U32LE, pcm_u32le);
PCM_CODEC  (CODEC_ID_PCM_U32BE, pcm_u32be);
PCM_CODEC  (CODEC_ID_PCM_S24LE, pcm_s24le);
PCM_CODEC  (CODEC_ID_PCM_S24BE, pcm_s24be);
PCM_CODEC  (CODEC_ID_PCM_U24LE, pcm_u24le);
PCM_CODEC  (CODEC_ID_PCM_U24BE, pcm_u24be);
PCM_CODEC  (CODEC_ID_PCM_S24DAUD, pcm_s24daud);
PCM_CODEC  (CODEC_ID_PCM_S16LE, pcm_s16le);
PCM_DECODER(CODEC_ID_PCM_S16LE_PLANAR, pcm_s16le_planar);
PCM_CODEC  (CODEC_ID_PCM_S16BE, pcm_s16be);
PCM_CODEC  (CODEC_ID_PCM_U16LE, pcm_u16le);
PCM_CODEC  (CODEC_ID_PCM_U16BE, pcm_u16be);
PCM_CODEC  (CODEC_ID_PCM_S8, pcm_s8);
PCM_CODEC  (CODEC_ID_PCM_U8, pcm_u8);
PCM_CODEC  (CODEC_ID_PCM_ALAW, pcm_alaw);
PCM_CODEC  (CODEC_ID_PCM_MULAW, pcm_mulaw);
PCM_CODEC  (CODEC_ID_PCM_ZORK, pcm_zork);
