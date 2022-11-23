/*
 * ViewQuest VQC decoder
 * Copyright (C) 2022 Peter Ross
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

#include "avcodec.h"
#include "get_bits.h"
#include "codec_internal.h"
#include "decode.h"
#include "libavutil/thread.h"

#define VECTOR_VLC_BITS 6

static const uint8_t vector_nbits[] = {
    2, 4, 4, 4, 4, 2, 4, 4,
    6, 6, 6, 6, 6, 6, 6, 6
};

enum {
    SKIP_3 = 0x10,
    SKIP_4,
    SKIP_5,
    SKIP_6,
    STOP_RUN,
    SIGNED_8BIT,
    SIGNED_6BIT
};

/* vector symbols are signed, but returned unsigned by get_vlc2()
   codebook indexes are cast as uint8_t in seed_codebook() to compensate */
static const int8_t vector_symbols[] = {
    0, SKIP_3, SKIP_4, SKIP_5, SKIP_6, STOP_RUN, 1, -1,
    2, 3, 4, SIGNED_8BIT, -2, -3, -4, SIGNED_6BIT
};

static VLC vector_vlc;

static av_cold void vqc_init_static_data(void)
{
    INIT_VLC_STATIC_FROM_LENGTHS(&vector_vlc, VECTOR_VLC_BITS, FF_ARRAY_ELEMS(vector_nbits),
                             vector_nbits, 1,
                             vector_symbols, 1, 1,
                             0, 0, 1 << VECTOR_VLC_BITS);
}

typedef struct VqcContext {
    AVFrame *frame;
    uint8_t * vectors;
    int16_t * coeff, *tmp1, *tmp2;
    int16_t codebook[4][256];
} VqcContext;

static av_cold int vqc_decode_init(AVCodecContext * avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    VqcContext *s = avctx->priv_data;

    if (avctx->width & 15)
        return AVERROR_PATCHWELCOME;

    s->vectors = av_malloc((avctx->width * avctx->height * 3) / 2);
    if (!s->vectors)
        return AVERROR(ENOMEM);

    s->coeff = av_malloc_array(2 * avctx->width, sizeof(s->coeff[0]));
    if (!s->coeff)
        return AVERROR(ENOMEM);

    s->tmp1 = av_malloc_array(avctx->width / 2, sizeof(s->tmp1[0]));
    if (!s->tmp1)
        return AVERROR(ENOMEM);

    s->tmp2 = av_malloc_array(avctx->width / 2, sizeof(s->tmp2[0]));
    if (!s->tmp2)
        return AVERROR(ENOMEM);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_static_once, vqc_init_static_data);

    return 0;
}

static int seed_pow1(int x)
{
    return x >= 1 && x <= 5 ? 1 << x : 0;
}

static int seed_pow2(int x)
{
    return x >= 1 && x <= 4 ? 1 << x : 1;
}

static int bias(int x, int c)
{
    if (x < 0)
        return x - c;
    else if (x > 0)
        return x + c;
    else
        return 0;
}

static void seed_codebooks(VqcContext * s, const int * seed)
{
    int book1 = -256 * seed[3];
    int book2 = -128 * seed[4];
    int book3 = -128 * seed[5];
    int book4 = -128 * seed[6];

    for (int i = -128; i < 128; i++) {
        s->codebook[0][(uint8_t)i] = book1;
        s->codebook[1][(uint8_t)i] = bias(book2, seed[0]);
        s->codebook[2][(uint8_t)i] = bias(book3, seed[1]);
        s->codebook[3][(uint8_t)i] = bias(book4, seed[2]);

        book1 += 2 * seed[3];
        book2 += seed[4];
        book3 += seed[5];
        book4 += seed[6];
    }
}

static int decode_vectors(VqcContext * s, const uint8_t * buf, int size, int width, int height)
{
    GetBitContext gb;
    uint8_t * vectors = s->vectors;
    uint8_t * vectors_end = s->vectors + (width * height * 3) / 2;

    memset(vectors, 0, 3 * width * height / 2);

    init_get_bits8(&gb, buf, size);

    for (int i = 0; i < 3 * width * height / 2 / 32; i++) {
        uint8_t * dst = vectors;
        int symbol;

        *dst++ = get_bits(&gb, 8);
        *dst++ = get_bits(&gb, 8);

        while (show_bits(&gb, 2) != 2) {
            if (dst >= vectors_end - 1)
                return 0;

            if (get_bits_left(&gb) < 4)
                return AVERROR_INVALIDDATA;

            if (!show_bits(&gb, 4)) {
                *dst++ = 0;
                *dst++ = 0;
                skip_bits(&gb, 4);
                continue;
            }

            symbol = get_vlc2(&gb, vector_vlc.table, VECTOR_VLC_BITS, 1);
            switch(symbol) {
            case SKIP_3: dst += 3; break;
            case SKIP_4: dst += 4; break;
            case SKIP_5: dst += 5; break;
            case SKIP_6: dst += 6; break;
            case SIGNED_8BIT: *dst++ = get_sbits(&gb, 8); break;
            case SIGNED_6BIT: *dst++ = get_sbits(&gb, 6); break;
            default:
                *dst++ = symbol;
            }
        }

        skip_bits(&gb, 2);
        vectors += 32;
    }

    return 0;
}

static void load_coeffs(VqcContext * s, const uint8_t * v, int width, int coeff_width)
{
    int16_t * c0     = s->coeff;
    int16_t * c1     = s->coeff + coeff_width;
    int16_t * c0_125 = s->coeff + (coeff_width >> 3);
    int16_t * c1_125 = s->coeff + coeff_width + (coeff_width >> 3);
    int16_t * c0_25  = s->coeff + (coeff_width >> 2);
    int16_t * c1_25 =  s->coeff + coeff_width + (coeff_width >> 2);
    int16_t * c0_5  =  s->coeff + (coeff_width >> 1);
    int16_t * c1_5  =  s->coeff + coeff_width + (coeff_width >> 1);

    for (int i = 0; i < width; i++) {
        c0[0] = s->codebook[0][v[0]];
        c0[1] = s->codebook[0][v[1]];
        c0 += 2;

        c1[0] = s->codebook[0][v[2]];
        c1[1] = s->codebook[0][v[3]];
        c1 += 2;

        c0_125[0] = s->codebook[1][v[4]];
        c0_125[1] = s->codebook[1][v[5]];
        c0_125 += 2;

        c1_125[0] = s->codebook[1][v[6]];
        c1_125[1] = s->codebook[1][v[7]];
        c1_125 += 2;

        c0_25[0] = s->codebook[2][v[8]];
        c0_25[1] = s->codebook[2][v[9]];
        c0_25[2] = s->codebook[2][v[10]];
        c0_25[3] = s->codebook[2][v[11]];
        c0_25 += 4;

        c1_25[0] = s->codebook[2][v[12]];
        c1_25[1] = s->codebook[2][v[13]];
        c1_25[2] = s->codebook[2][v[14]];
        c1_25[3] = s->codebook[2][v[15]];
        c1_25 += 4;

        if (v[16] | v[17] | v[18] | v[19]) {
            c0_5[0] = s->codebook[3][v[16]];
            c0_5[1] = s->codebook[3][v[17]];
            c0_5[2] = s->codebook[3][v[18]];
            c0_5[3] = s->codebook[3][v[19]];
        } else {
            c0_5[0] = c0_5[1] = c0_5[2] = c0_5[3] = 0;
        }

        if (v[20] | v[21] | v[22] | v[23]) {
            c0_5[4] = s->codebook[3][v[20]];
            c0_5[5] = s->codebook[3][v[21]];
            c0_5[6] = s->codebook[3][v[22]];
            c0_5[7] = s->codebook[3][v[23]];
        } else {
            c0_5[4] = c0_5[5] = c0_5[6] = c0_5[7] = 0;
        }
        c0_5 += 8;

        if (v[24] | v[25] | v[26] | v[27]) {
            c1_5[0] = s->codebook[3][v[24]];
            c1_5[1] = s->codebook[3][v[25]];
            c1_5[2] = s->codebook[3][v[26]];
            c1_5[3] = s->codebook[3][v[27]];
        } else {
            c1_5[0] = c1_5[1] = c1_5[2] = c1_5[3] = 0;
        }

        if (v[28] | v[29] | v[30] | v[31]) {
            c1_5[4] = s->codebook[3][v[28]];
            c1_5[5] = s->codebook[3][v[29]];
            c1_5[6] = s->codebook[3][v[30]];
            c1_5[7] = s->codebook[3][v[31]];
        } else {
            c1_5[4] = c1_5[5] = c1_5[6] = c1_5[7] = 0;
        }
        c1_5 += 8;

        v += 32;
    }
}

static void transform1(const int16_t * a, const int16_t * b, int16_t * dst, int width)
{
    int s0 = a[0] + (b[0] >> 1);

    for (int i = 0; i < width / 2 - 1; i++) {
        dst[i * 2] = s0;
        s0 = a[i + 1] + ((b[i] + b[i + 1]) >> 1);
        dst[i * 2 + 1] = ((dst[i * 2] + s0) >> 1) - 2 * b[i];
    }

    dst[width - 2] = s0;
    dst[width - 1] = a[width / 2 - 1] + ((b[width / 2 - 2] - 2 * b[width / 2 - 1]) >> 2) - b[width / 2 - 1];
}

static uint8_t clip(int x)
{
    return x >= -128 ? x <= 127 ? x + 0x80 : 0x00 : 0xFF;
}

static void transform2(const int16_t * a, const int16_t * b, uint8_t * dst, int width)
{
    int s0 = a[0] + (b[0] >> 1);
    int tmp;

    for (int i = 0; i < width / 2 - 1; i++) {
        dst[i * 2] = av_clip_uint8(s0 + 0x80);
        tmp = a[i + 1] + ((b[i] + b[i + 1]) >> 1);
        dst[i * 2 + 1] = av_clip_uint8(((tmp + s0) >> 1) - 2 * b[i] + 0x80);
        s0 = tmp;
    }

    dst[width - 2] = clip(s0);
    dst[width - 1] = clip(a[width / 2 - 1] + ((b[width / 2 - 2] - 2 * b[width / 2 - 1]) >> 2) - b[width / 2 - 1]);
}

static void decode_strip(VqcContext * s, uint8_t * dst, int stride, int width)
{
    const int16_t * coeff;

    for (int i = 0; i < width; i++) {
        int v0 = s->coeff[i];
        int v1 = s->coeff[width + i];
        s->coeff[i] = v0 - v1;
        s->coeff[width + i] = v0 + v1;
    }

    coeff = s->coeff;

    transform1(coeff, coeff + width / 8, s->tmp1, width / 4);
    transform1(s->tmp1, coeff + width / 4, s->tmp2, width / 2);
    transform2(s->tmp2, coeff + width / 2, dst, width);

    coeff += width;
    dst += stride;

    transform1(coeff, coeff + width / 8, s->tmp1, width / 4);
    transform1(s->tmp1, coeff + width / 4, s->tmp2, width / 2);
    transform2(s->tmp2, coeff + width / 2, dst, width);
}

static void decode_frame(VqcContext * s, int width, int height)
{
    uint8_t * vectors = s->vectors;
    uint8_t * y = s->frame->data[0];
    uint8_t * u = s->frame->data[1];
    uint8_t * v = s->frame->data[2];

    for (int j = 0; j < height / 4; j++) {
        load_coeffs(s, vectors, width / 16, width);
        decode_strip(s, y, s->frame->linesize[0], width);
        vectors += 2 * width;
        y += 2 * s->frame->linesize[0];

        load_coeffs(s, vectors, width / 32, width / 2);
        decode_strip(s, u, s->frame->linesize[1], width / 2);
        vectors += width;
        u += 2 * s->frame->linesize[1];

        load_coeffs(s, vectors, width / 16, width);
        decode_strip(s, y, s->frame->linesize[0], width);
        vectors += 2 * width;
        y += 2 * s->frame->linesize[0];

        load_coeffs(s, vectors, width / 32, width / 2);
        decode_strip(s, v, s->frame->linesize[2], width / 2);
        vectors += width;
        v += 2 * s->frame->linesize[2];
    }
}

static int vqc_decode_frame(AVCodecContext *avctx, AVFrame * rframe,
                            int * got_frame, AVPacket * avpkt)
{
    VqcContext *s = avctx->priv_data;
    int ret;
    const uint8_t * buf = avpkt->data;
    int cache, seed[7], gamma, contrast;

    if (avpkt->size < 7)
        return AVERROR_INVALIDDATA;

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    av_log(avctx, AV_LOG_DEBUG, "VQC%d format\n", (buf[2] & 1) + 1);

    if (((buf[0] >> 1) & 7) != 5) {
        avpriv_request_sample(avctx, "subversion != 5\n");
        return AVERROR_PATCHWELCOME;
    }

    cache = AV_RL24(buf + 4);
    seed[2] = seed_pow1((cache >> 1) & 7);
    seed[1] = seed_pow1((cache >> 4) & 7);
    seed[0] = seed_pow1((cache >> 7) & 7);
    seed[6] = seed_pow2((cache >> 10) & 7);
    seed[5] = seed_pow2((cache >> 13) & 7);
    seed[4] = seed_pow2((cache >> 16) & 7);
    seed[3] = seed_pow2((cache >> 19) & 7);

    gamma = buf[0] >> 4;
    contrast = AV_RL16(buf + 2) >> 1;
    if (gamma || contrast)
        avpriv_request_sample(avctx, "gamma=0x%x, contrast=0x%x\n", gamma, contrast);

    seed_codebooks(s, seed);
    ret = decode_vectors(s, buf + 7, avpkt->size - 7, avctx->width, avctx->height);
    if (ret < 0)
        return ret;
    decode_frame(s, avctx->width, avctx->height);

    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int vqc_decode_end(AVCodecContext * avctx)
{
    VqcContext *s = avctx->priv_data;

    av_freep(&s->vectors);
    av_freep(&s->coeff);
    av_freep(&s->tmp1);
    av_freep(&s->tmp2);
    av_frame_free(&s->frame);

    return 0;
}

const FFCodec ff_vqc_decoder = {
    .p.name         = "vqc",
    CODEC_LONG_NAME("ViewQuest VQC"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VQC,
    .priv_data_size = sizeof(VqcContext),
    .init           = vqc_decode_init,
    .close          = vqc_decode_end,
    FF_CODEC_DECODE_CB(vqc_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
