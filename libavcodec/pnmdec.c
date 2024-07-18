/*
 * PNM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard
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

#include "config_components.h"

#include "libavutil/half2float.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "put_bits.h"
#include "pnm.h"

static void samplecpy(uint8_t *dst, const uint8_t *src, int n, int maxval)
{
    if (maxval <= 255) {
        memcpy(dst, src, n);
    } else {
        int i;
        for (i=0; i<n/2; i++) {
            ((uint16_t *)dst)[i] = AV_RB16(src+2*i);
        }
    }
}

static int pnm_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf   = avpkt->data;
    int buf_size         = avpkt->size;
    PNMContext * const s = avctx->priv_data;
    int i, j, k, n, linesize, h, upgrade = 0, is_mono = 0;
    unsigned char *ptr;
    int components, sample_len, ret;
    float scale;

    s->bytestream_start =
    s->bytestream       = buf;
    s->bytestream_end   = buf + buf_size;

    if ((ret = ff_pnm_decode_header(avctx, s)) < 0)
        return ret;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    avctx->bits_per_raw_sample = av_log2(s->maxval) + 1;

    switch (avctx->pix_fmt) {
    default:
        return AVERROR(EINVAL);
    case AV_PIX_FMT_RGBA64:
        n = avctx->width * 8;
        components=4;
        sample_len=16;
        if (s->maxval < 65535)
            upgrade = 2;
        goto do_read;
    case AV_PIX_FMT_RGB48:
        n = avctx->width * 6;
        components=3;
        sample_len=16;
        if (s->maxval < 65535)
            upgrade = 2;
        goto do_read;
    case AV_PIX_FMT_RGBA:
        n = avctx->width * 4;
        components=4;
        sample_len=8;
        goto do_read;
    case AV_PIX_FMT_RGB24:
        n = avctx->width * 3;
        components=3;
        sample_len=8;
        if (s->maxval < 255)
            upgrade = 1;
        goto do_read;
    case AV_PIX_FMT_GRAY8:
        n = avctx->width;
        components=1;
        sample_len=8;
        if (s->maxval < 255)
            upgrade = 1;
        goto do_read;
    case AV_PIX_FMT_GRAY8A:
        n = avctx->width * 2;
        components=2;
        sample_len=8;
        goto do_read;
    case AV_PIX_FMT_GRAY16:
        n = avctx->width * 2;
        components=1;
        sample_len=16;
        if (s->maxval < 65535)
            upgrade = 2;
        goto do_read;
    case AV_PIX_FMT_YA16:
        n =  avctx->width * 4;
        components=2;
        sample_len=16;
        if (s->maxval < 65535)
            upgrade = 2;
        goto do_read;
    case AV_PIX_FMT_MONOWHITE:
    case AV_PIX_FMT_MONOBLACK:
        n = (avctx->width + 7) >> 3;
        components=1;
        sample_len=1;
        is_mono = 1;
    do_read:
        ptr      = p->data[0];
        linesize = p->linesize[0];
        if (n * avctx->height > s->bytestream_end - s->bytestream)
            return AVERROR_INVALIDDATA;
        if(s->type < 4 || (is_mono && s->type==7)){
            for (i=0; i<avctx->height; i++) {
                PutBitContext pb;
                init_put_bits(&pb, ptr, FFABS(linesize));
                for(j=0; j<avctx->width * components; j++){
                    unsigned int c=0;
                    unsigned v=0;
                    if(s->type < 4)
                    while(s->bytestream < s->bytestream_end && (*s->bytestream < '0' || *s->bytestream > '9' ))
                        s->bytestream++;
                    if(s->bytestream >= s->bytestream_end)
                        return AVERROR_INVALIDDATA;
                    if (is_mono) {
                        /* read a single digit */
                        v = (*s->bytestream++)&1;
                    } else {
                        /* read a sequence of digits */
                        for (k = 0; k < 6 && c <= 9; k += 1) {
                            v = 10*v + c;
                            c = (*s->bytestream++) - '0';
                        }
                        if (v > s->maxval) {
                            av_log(avctx, AV_LOG_ERROR, "value %d larger than maxval %d\n", v, s->maxval);
                            return AVERROR_INVALIDDATA;
                        }
                    }
                    if (sample_len == 16) {
                        ((uint16_t*)ptr)[j] = (((1<<sample_len)-1)*v + (s->maxval>>1))/s->maxval;
                    } else
                        put_bits(&pb, sample_len, (((1<<sample_len)-1)*v + (s->maxval>>1))/s->maxval);
                }
                if (sample_len != 16)
                    flush_put_bits(&pb);
                ptr+= linesize;
            }
        }else{
            for (int i = 0; i < avctx->height; i++) {
                if (!upgrade)
                    samplecpy(ptr, s->bytestream, n, s->maxval);
                else if (upgrade == 1) {
                    unsigned int f = (255 * 128 + s->maxval / 2) / s->maxval;
                    for (unsigned j = 0; j < n; j++)
                        ptr[j] = (s->bytestream[j] * f + 64) >> 7;
                } else if (upgrade == 2) {
                    unsigned int f = (65535 * 32768 + s->maxval / 2) / s->maxval;
                    for (unsigned j = 0; j < n / 2; j++) {
                        unsigned v = AV_RB16(s->bytestream + 2*j);
                        ((uint16_t *)ptr)[j] = (v * f + 16384) >> 15;
                    }
                }
                s->bytestream += n;
                ptr           += linesize;
            }
        }
        break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV420P10:
        {
            unsigned char *ptr1, *ptr2;

            n        = avctx->width;
            ptr      = p->data[0];
            linesize = p->linesize[0];
            if (s->maxval >= 256)
                n *= 2;
            if (n * avctx->height * 3 / 2 > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            for (i = 0; i < avctx->height; i++) {
                samplecpy(ptr, s->bytestream, n, s->maxval);
                s->bytestream += n;
                ptr           += linesize;
            }
            ptr1 = p->data[1];
            ptr2 = p->data[2];
            n >>= 1;
            h = avctx->height >> 1;
            for (i = 0; i < h; i++) {
                samplecpy(ptr1, s->bytestream, n, s->maxval);
                s->bytestream += n;
                samplecpy(ptr2, s->bytestream, n, s->maxval);
                s->bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
            }
        }
        break;
    case AV_PIX_FMT_YUV420P16:
        {
            uint16_t *ptr1, *ptr2;
            const int f = (65535 * 32768 + s->maxval / 2) / s->maxval;
            unsigned int j, v;

            n        = avctx->width * 2;
            ptr      = p->data[0];
            linesize = p->linesize[0];
            if (n * avctx->height * 3 / 2 > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            for (i = 0; i < avctx->height; i++) {
                for (j = 0; j < n / 2; j++) {
                    v = AV_RB16(s->bytestream + 2*j);
                    ((uint16_t *)ptr)[j] = (v * f + 16384) >> 15;
                }
                s->bytestream += n;
                ptr           += linesize;
            }
            ptr1 = (uint16_t*)p->data[1];
            ptr2 = (uint16_t*)p->data[2];
            n >>= 1;
            h = avctx->height >> 1;
            for (i = 0; i < h; i++) {
                for (j = 0; j < n / 2; j++) {
                    v = AV_RB16(s->bytestream + 2*j);
                    ptr1[j] = (v * f + 16384) >> 15;
                }
                s->bytestream += n;

                for (j = 0; j < n / 2; j++) {
                    v = AV_RB16(s->bytestream + 2*j);
                    ptr2[j] = (v * f + 16384) >> 15;
                }
                s->bytestream += n;

                ptr1 += p->linesize[1] / 2;
                ptr2 += p->linesize[2] / 2;
            }
        }
        break;
    case AV_PIX_FMT_GBRPF32:
        if (!s->half) {
            if (avctx->width * avctx->height * 12LL > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            scale = 1.f / s->scale;
            if (s->endian) {
                float *r, *g, *b;

                r = (float *)p->data[2];
                g = (float *)p->data[0];
                b = (float *)p->data[1];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        r[j] = av_int2float(AV_RL32(s->bytestream+0)) * scale;
                        g[j] = av_int2float(AV_RL32(s->bytestream+4)) * scale;
                        b[j] = av_int2float(AV_RL32(s->bytestream+8)) * scale;
                        s->bytestream += 12;
                    }

                    r += p->linesize[2] / 4;
                    g += p->linesize[0] / 4;
                    b += p->linesize[1] / 4;
                }
            } else {
                float *r, *g, *b;

                r = (float *)p->data[2];
                g = (float *)p->data[0];
                b = (float *)p->data[1];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        r[j] = av_int2float(AV_RB32(s->bytestream+0)) * scale;
                        g[j] = av_int2float(AV_RB32(s->bytestream+4)) * scale;
                        b[j] = av_int2float(AV_RB32(s->bytestream+8)) * scale;
                        s->bytestream += 12;
                    }

                    r += p->linesize[2] / 4;
                    g += p->linesize[0] / 4;
                    b += p->linesize[1] / 4;
                }
            }
        } else {
            if (avctx->width * avctx->height * 6 > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            scale = 1.f / s->scale;
            if (s->endian) {
                float *r, *g, *b;

                r = (float *)p->data[2];
                g = (float *)p->data[0];
                b = (float *)p->data[1];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        r[j] = av_int2float(half2float(AV_RL16(s->bytestream+0), &s->h2f_tables)) * scale;
                        g[j] = av_int2float(half2float(AV_RL16(s->bytestream+2), &s->h2f_tables)) * scale;
                        b[j] = av_int2float(half2float(AV_RL16(s->bytestream+4), &s->h2f_tables)) * scale;
                        s->bytestream += 6;
                    }

                    r += p->linesize[2] / 4;
                    g += p->linesize[0] / 4;
                    b += p->linesize[1] / 4;
                }
            } else {
                float *r, *g, *b;

                r = (float *)p->data[2];
                g = (float *)p->data[0];
                b = (float *)p->data[1];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        r[j] = av_int2float(half2float(AV_RB16(s->bytestream+0), &s->h2f_tables)) * scale;
                        g[j] = av_int2float(half2float(AV_RB16(s->bytestream+2), &s->h2f_tables)) * scale;
                        b[j] = av_int2float(half2float(AV_RB16(s->bytestream+4), &s->h2f_tables)) * scale;
                        s->bytestream += 6;
                    }

                    r += p->linesize[2] / 4;
                    g += p->linesize[0] / 4;
                    b += p->linesize[1] / 4;
                }
            }
        }
        /* PFM is encoded from bottom to top */
        p->data[0] += (avctx->height - 1) * p->linesize[0];
        p->data[1] += (avctx->height - 1) * p->linesize[1];
        p->data[2] += (avctx->height - 1) * p->linesize[2];
        p->linesize[0] = -p->linesize[0];
        p->linesize[1] = -p->linesize[1];
        p->linesize[2] = -p->linesize[2];
        break;
    case AV_PIX_FMT_GRAYF32:
        if (!s->half) {
            if (avctx->width * avctx->height * 4 > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            scale = 1.f / s->scale;
            if (s->endian) {
                float *g = (float *)p->data[0];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        g[j] = av_int2float(AV_RL32(s->bytestream)) * scale;
                        s->bytestream += 4;
                    }
                    g += p->linesize[0] / 4;
                }
            } else {
                float *g = (float *)p->data[0];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        g[j] = av_int2float(AV_RB32(s->bytestream)) * scale;
                        s->bytestream += 4;
                    }
                    g += p->linesize[0] / 4;
                }
            }
        } else {
            if (avctx->width * avctx->height * 2 > s->bytestream_end - s->bytestream)
                return AVERROR_INVALIDDATA;
            scale = 1.f / s->scale;
            if (s->endian) {
                float *g = (float *)p->data[0];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        g[j] = av_int2float(half2float(AV_RL16(s->bytestream), &s->h2f_tables)) * scale;
                        s->bytestream += 2;
                    }
                    g += p->linesize[0] / 4;
                }
            } else {
                float *g = (float *)p->data[0];
                for (int i = 0; i < avctx->height; i++) {
                    for (int j = 0; j < avctx->width; j++) {
                        g[j] = av_int2float(half2float(AV_RB16(s->bytestream), &s->h2f_tables)) * scale;
                        s->bytestream += 2;
                    }
                    g += p->linesize[0] / 4;
                }
            }
        }
        /* PFM is encoded from bottom to top */
        p->data[0] += (avctx->height - 1) * p->linesize[0];
        p->linesize[0] = -p->linesize[0];
        break;
    }
    *got_frame = 1;

    return s->bytestream - s->bytestream_start;
}


#if CONFIG_PGM_DECODER
const FFCodec ff_pgm_decoder = {
    .p.name         = "pgm",
    CODEC_LONG_NAME("PGM (Portable GrayMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PGMYUV_DECODER
const FFCodec ff_pgmyuv_decoder = {
    .p.name         = "pgmyuv",
    CODEC_LONG_NAME("PGMYUV (Portable GrayMap YUV) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PGMYUV,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PPM_DECODER
const FFCodec ff_ppm_decoder = {
    .p.name         = "ppm",
    CODEC_LONG_NAME("PPM (Portable PixelMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PPM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PBM_DECODER
const FFCodec ff_pbm_decoder = {
    .p.name         = "pbm",
    CODEC_LONG_NAME("PBM (Portable BitMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PBM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PAM_DECODER
const FFCodec ff_pam_decoder = {
    .p.name         = "pam",
    CODEC_LONG_NAME("PAM (Portable AnyMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PAM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PFM_DECODER
const FFCodec ff_pfm_decoder = {
    .p.name         = "pfm",
    CODEC_LONG_NAME("PFM (Portable FloatMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PFM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif

#if CONFIG_PHM_DECODER
static av_cold int phm_dec_init(AVCodecContext *avctx)
{
    PNMContext *s = avctx->priv_data;

    ff_init_half2float_tables(&s->h2f_tables);

    return 0;
}

const FFCodec ff_phm_decoder = {
    .p.name         = "phm",
    CODEC_LONG_NAME("PHM (Portable HalfFloatMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PHM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(PNMContext),
    .init           = phm_dec_init,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(pnm_decode_frame),
};
#endif
