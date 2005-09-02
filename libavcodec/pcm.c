/*
 * PCM codecs
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/**
 * @file pcm.c
 * PCM codecs
 */
 
#include "avcodec.h"
#include "bitstream.h" // for ff_reverse

/* from g711.c by SUN microsystems (unrestricted use) */

#define	SIGN_BIT	(0x80)		/* Sign bit for a A-law byte. */
#define	QUANT_MASK	(0xf)		/* Quantization field mask. */
#define	NSEGS		(8)		/* Number of A-law segments. */
#define	SEG_SHIFT	(4)		/* Left shift for segment number. */
#define	SEG_MASK	(0x70)		/* Segment field mask. */

#define	BIAS		(0x84)		/* Bias for linear code. */

/*
 * alaw2linear() - Convert an A-law value to 16-bit linear PCM
 *
 */
static int alaw2linear(unsigned char	a_val)
{
	int		t;
	int		seg;

	a_val ^= 0x55;

	t = a_val & QUANT_MASK;
	seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
	if(seg) t= (t + t + 1 + 32) << (seg + 2);
	else    t= (t + t + 1     ) << 3;

	return ((a_val & SIGN_BIT) ? t : -t);
}

static int ulaw2linear(unsigned char	u_val)
{
	int		t;

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
static uint8_t *linear_to_alaw = NULL;
static int linear_to_alaw_ref = 0;

static uint8_t *linear_to_ulaw = NULL;
static int linear_to_ulaw_ref = 0;

static void build_xlaw_table(uint8_t *linear_to_xlaw, 
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

static int pcm_encode_init(AVCodecContext *avctx)
{
    avctx->frame_size = 1;
    switch(avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        if (linear_to_alaw_ref == 0) {
            linear_to_alaw = av_malloc(16384);
            if (!linear_to_alaw)
                return -1;
            build_xlaw_table(linear_to_alaw, alaw2linear, 0xd5);
        }
        linear_to_alaw_ref++;
        break;
    case CODEC_ID_PCM_MULAW:
        if (linear_to_ulaw_ref == 0) {
            linear_to_ulaw = av_malloc(16384);
            if (!linear_to_ulaw)
                return -1;
            build_xlaw_table(linear_to_ulaw, ulaw2linear, 0xff);
        }
        linear_to_ulaw_ref++;
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

static int pcm_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    switch(avctx->codec->id) {
    case CODEC_ID_PCM_ALAW:
        if (--linear_to_alaw_ref == 0)
            av_free(linear_to_alaw);
        break;
    case CODEC_ID_PCM_MULAW:
        if (--linear_to_ulaw_ref == 0)
            av_free(linear_to_ulaw);
        break;
    default:
        /* nothing to free */
        break;
    }
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
    if (bps > 2)
        memset(*dst, 0, n * bps);
    if (le) *dst += bps - 2;
    for(;n>0;n--) {
        register int v = *(*samples)++;
        if (us) v += 0x8000;
        (*dst)[le] = v >> 8;
        (*dst)[1 - le] = v;
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
            dst[2] = tmp & 0xff;
            tmp >>= 8;
            dst[1] = tmp & 0xff;
            dst[0] = tmp >> 8;
            samples++;
            dst += 3;
        }
        break;
    case CODEC_ID_PCM_S16LE:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = v & 0xff;
            dst[1] = v >> 8;
            dst += 2;
        }
        break;
    case CODEC_ID_PCM_S16BE:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = v >> 8;
            dst[1] = v;
            dst += 2;
        }
        break;
    case CODEC_ID_PCM_U16LE:
        for(;n>0;n--) {
            v = *samples++;
            v += 0x8000;
            dst[0] = v & 0xff;
            dst[1] = v >> 8;
            dst += 2;
        }
        break;
    case CODEC_ID_PCM_U16BE:
        for(;n>0;n--) {
            v = *samples++;
            v += 0x8000;
            dst[0] = v >> 8;
            dst[1] = v;
            dst += 2;
        }
        break;
    case CODEC_ID_PCM_S8:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = v >> 8;
            dst++;
        }
        break;
    case CODEC_ID_PCM_U8:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = (v >> 8) + 128;
            dst++;
        }
        break;
    case CODEC_ID_PCM_ALAW:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = linear_to_alaw[(v + 32768) >> 2];
            dst++;
        }
        break;
    case CODEC_ID_PCM_MULAW:
        for(;n>0;n--) {
            v = *samples++;
            dst[0] = linear_to_ulaw[(v + 32768) >> 2];
            dst++;
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

static int pcm_decode_init(AVCodecContext * avctx)
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
                               uint8_t **src, short **samples, int src_len)
{
    register int n = src_len / bps;
    if (le) *src += bps - 2;
    for(;n>0;n--) {
        *(*samples)++ = ((*src)[le] << 8 | (*src)[1 - le]) - (us?0x8000:0);
        *src += bps;
    }
    if (le) *src -= bps - 2;
}

static int pcm_decode_frame(AVCodecContext *avctx,
			    void *data, int *data_size,
			    uint8_t *buf, int buf_size)
{
    PCMDecode *s = avctx->priv_data;
    int n;
    short *samples;
    uint8_t *src;

    samples = data;
    src = buf;

    if(buf_size > AVCODEC_MAX_AUDIO_FRAME_SIZE/2)
        buf_size = AVCODEC_MAX_AUDIO_FRAME_SIZE/2;

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
          uint32_t v = src[0] << 16 | src[1] << 8 | src[2];
          v >>= 4; // sync flags are here
          *samples++ = ff_reverse[(v >> 8) & 0xff] +
                       (ff_reverse[v & 0xff] << 8);
          src += 3;
        }
        break;
    case CODEC_ID_PCM_S16LE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = src[0] | (src[1] << 8);
            src += 2;
        }
        break;
    case CODEC_ID_PCM_S16BE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = (src[0] << 8) | src[1];
            src += 2;
        }
        break;
    case CODEC_ID_PCM_U16LE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = (src[0] | (src[1] << 8)) - 0x8000;
            src += 2;
        }
        break;
    case CODEC_ID_PCM_U16BE:
        n = buf_size >> 1;
        for(;n>0;n--) {
            *samples++ = ((src[0] << 8) | src[1]) - 0x8000;
            src += 2;
        }
        break;
    case CODEC_ID_PCM_S8:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = src[0] << 8;
            src++;
        }
        break;
    case CODEC_ID_PCM_U8:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = ((int)src[0] - 128) << 8;
            src++;
        }
        break;
    case CODEC_ID_PCM_ALAW:
    case CODEC_ID_PCM_MULAW:
        n = buf_size;
        for(;n>0;n--) {
            *samples++ = s->table[src[0]];
            src++;
        }
        break;
    default:
        return -1;
    }
    *data_size = (uint8_t *)samples - (uint8_t *)data;
    return src - buf;
}

#define PCM_CODEC(id, name)                     \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    0,                                          \
    pcm_encode_init,				\
    pcm_encode_frame,				\
    pcm_encode_close,				\
    NULL,                                       \
};                                              \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(PCMDecode),                          \
    pcm_decode_init,				\
    NULL,                                       \
    NULL,                                       \
    pcm_decode_frame,                           \
}

PCM_CODEC(CODEC_ID_PCM_S32LE, pcm_s32le);
PCM_CODEC(CODEC_ID_PCM_S32BE, pcm_s32be);
PCM_CODEC(CODEC_ID_PCM_U32LE, pcm_u32le);
PCM_CODEC(CODEC_ID_PCM_U32BE, pcm_u32be);
PCM_CODEC(CODEC_ID_PCM_S24LE, pcm_s24le);
PCM_CODEC(CODEC_ID_PCM_S24BE, pcm_s24be);
PCM_CODEC(CODEC_ID_PCM_U24LE, pcm_u24le);
PCM_CODEC(CODEC_ID_PCM_U24BE, pcm_u24be);
PCM_CODEC(CODEC_ID_PCM_S24DAUD, pcm_s24daud);
PCM_CODEC(CODEC_ID_PCM_S16LE, pcm_s16le);
PCM_CODEC(CODEC_ID_PCM_S16BE, pcm_s16be);
PCM_CODEC(CODEC_ID_PCM_U16LE, pcm_u16le);
PCM_CODEC(CODEC_ID_PCM_U16BE, pcm_u16be);
PCM_CODEC(CODEC_ID_PCM_S8, pcm_s8);
PCM_CODEC(CODEC_ID_PCM_U8, pcm_u8);
PCM_CODEC(CODEC_ID_PCM_ALAW, pcm_alaw);
PCM_CODEC(CODEC_ID_PCM_MULAW, pcm_mulaw);

#undef PCM_CODEC
