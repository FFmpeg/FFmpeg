/*
 * Copyright (c) 2002-2003 Michael Niedermayer <michaelni@gmx.at>
 *
 * see http://www.pcisys.net/~melanson/codecs/huffyuv.txt for a description of
 * the algorithm used
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
 * huffyuv encoder
 */

#include "libavutil/opt.h"

#include "avcodec.h"
#include "huffyuv.h"
#include "huffman.h"
#include "huffyuvencdsp.h"
#include "internal.h"
#include "put_bits.h"

static inline int sub_left_prediction(HYuvContext *s, uint8_t *dst,
                                      uint8_t *src, int w, int left)
{
    int i;
    if (w < 32) {
        for (i = 0; i < w; i++) {
            const int temp = src[i];
            dst[i] = temp - left;
            left   = temp;
        }
        return left;
    } else {
        for (i = 0; i < 16; i++) {
            const int temp = src[i];
            dst[i] = temp - left;
            left   = temp;
        }
        s->hencdsp.diff_bytes(dst + 16, src + 16, src + 15, w - 16);
        return src[w-1];
    }
}

static inline void sub_left_prediction_bgr32(HYuvContext *s, uint8_t *dst,
                                             uint8_t *src, int w,
                                             int *red, int *green, int *blue,
                                             int *alpha)
{
    int i;
    int r, g, b, a;
    r = *red;
    g = *green;
    b = *blue;
    a = *alpha;

    for (i = 0; i < FFMIN(w, 4); i++) {
        const int rt = src[i * 4 + R];
        const int gt = src[i * 4 + G];
        const int bt = src[i * 4 + B];
        const int at = src[i * 4 + A];
        dst[i * 4 + R] = rt - r;
        dst[i * 4 + G] = gt - g;
        dst[i * 4 + B] = bt - b;
        dst[i * 4 + A] = at - a;
        r = rt;
        g = gt;
        b = bt;
        a = at;
    }

    s->hencdsp.diff_bytes(dst + 16, src + 16, src + 12, w * 4 - 16);

    *red   = src[(w - 1) * 4 + R];
    *green = src[(w - 1) * 4 + G];
    *blue  = src[(w - 1) * 4 + B];
    *alpha = src[(w - 1) * 4 + A];
}

static inline void sub_left_prediction_rgb24(HYuvContext *s, uint8_t *dst,
                                             uint8_t *src, int w,
                                             int *red, int *green, int *blue)
{
    int i;
    int r, g, b;
    r = *red;
    g = *green;
    b = *blue;
    for (i = 0; i < FFMIN(w, 16); i++) {
        const int rt = src[i * 3 + 0];
        const int gt = src[i * 3 + 1];
        const int bt = src[i * 3 + 2];
        dst[i * 3 + 0] = rt - r;
        dst[i * 3 + 1] = gt - g;
        dst[i * 3 + 2] = bt - b;
        r = rt;
        g = gt;
        b = bt;
    }

    s->hencdsp.diff_bytes(dst + 48, src + 48, src + 48 - 3, w * 3 - 48);

    *red   = src[(w - 1) * 3 + 0];
    *green = src[(w - 1) * 3 + 1];
    *blue  = src[(w - 1) * 3 + 2];
}

static int store_table(HYuvContext *s, const uint8_t *len, uint8_t *buf)
{
    int i;
    int index = 0;

    for (i = 0; i < 256;) {
        int val = len[i];
        int repeat = 0;

        for (; i < 256 && len[i] == val && repeat < 255; i++)
            repeat++;

        assert(val < 32 && val >0 && repeat<256 && repeat>0);
        if ( repeat > 7) {
            buf[index++] = val;
            buf[index++] = repeat;
        } else {
            buf[index++] = val | (repeat << 5);
        }
    }

    return index;
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;
    int i, j;

    ff_huffyuv_common_init(avctx);
    ff_huffyuvencdsp_init(&s->hencdsp);

    avctx->extradata = av_mallocz(1024*30); // 256*3+4 == 772
    avctx->stats_out = av_mallocz(1024*30); // 21*256*3(%llu ) + 3(\n) + 1(0) = 16132
    s->version = 2;

    if (!avctx->extradata || !avctx->stats_out)
        return AVERROR(ENOMEM);

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->context_model == 1)
        s->context = avctx->context_model;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
        if (s->width & 1) {
            av_log(avctx, AV_LOG_ERROR, "Width must be even for this colorspace.\n");
            return -1;
        }
        s->bitstream_bpp = avctx->pix_fmt == AV_PIX_FMT_YUV420P ? 12 : 16;
        break;
    case AV_PIX_FMT_RGB32:
        s->bitstream_bpp = 32;
        break;
    case AV_PIX_FMT_RGB24:
        s->bitstream_bpp = 24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "format not supported\n");
        return -1;
    }
    avctx->bits_per_coded_sample = s->bitstream_bpp;
    s->decorrelate = s->bitstream_bpp >= 24;
#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->prediction_method)
        s->predictor = avctx->prediction_method;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    s->interlaced = avctx->flags & AV_CODEC_FLAG_INTERLACED_ME ? 1 : 0;
    if (s->context) {
        if (s->flags & (AV_CODEC_FLAG_PASS1 | AV_CODEC_FLAG_PASS2)) {
            av_log(avctx, AV_LOG_ERROR,
                   "context=1 is not compatible with "
                   "2 pass huffyuv encoding\n");
            return -1;
        }
    }

    if (avctx->codec->id == AV_CODEC_ID_HUFFYUV) {
        if (avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error: YV12 is not supported by huffyuv; use "
                   "vcodec=ffvhuff or format=422p\n");
            return -1;
        }
#if FF_API_PRIVATE_OPT
        if (s->context) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error: per-frame huffman tables are not supported "
                   "by huffyuv; use vcodec=ffvhuff\n");
            return -1;
        }
#endif
        if (s->interlaced != ( s->height > 288 ))
            av_log(avctx, AV_LOG_INFO,
                   "using huffyuv 2.2.0 or newer interlacing flag\n");
    }

    if (s->bitstream_bpp >= 24 && s->predictor == MEDIAN) {
        av_log(avctx, AV_LOG_ERROR,
               "Error: RGB is incompatible with median predictor\n");
        return -1;
    }

    ((uint8_t*)avctx->extradata)[0] = s->predictor | (s->decorrelate << 6);
    ((uint8_t*)avctx->extradata)[1] = s->bitstream_bpp;
    ((uint8_t*)avctx->extradata)[2] = s->interlaced ? 0x10 : 0x20;
    if (s->context)
        ((uint8_t*)avctx->extradata)[2] |= 0x40;
    ((uint8_t*)avctx->extradata)[3] = 0;
    s->avctx->extradata_size = 4;

    if (avctx->stats_in) {
        char *p = avctx->stats_in;

        for (i = 0; i < 3; i++)
            for (j = 0; j < 256; j++)
                s->stats[i][j] = 1;

        for (;;) {
            for (i = 0; i < 3; i++) {
                char *next;

                for (j = 0; j < 256; j++) {
                    s->stats[i][j] += strtol(p, &next, 0);
                    if (next == p) return -1;
                    p = next;
                }
            }
            if (p[0] == 0 || p[1] == 0 || p[2] == 0) break;
        }
    } else {
        for (i = 0; i < 3; i++)
            for (j = 0; j < 256; j++) {
                int d = FFMIN(j, 256 - j);

                s->stats[i][j] = 100000000 / (d + 1);
            }
    }

    for (i = 0; i < 3; i++) {
        ff_huff_gen_len_table(s->len[i], s->stats[i]);

        if (ff_huffyuv_generate_bits_table(s->bits[i], s->len[i]) < 0) {
            return -1;
        }

        s->avctx->extradata_size +=
            store_table(s, s->len[i], &((uint8_t*)s->avctx->extradata)[s->avctx->extradata_size]);
    }

    if (s->context) {
        for (i = 0; i < 3; i++) {
            int pels = s->width * s->height / (i ? 40 : 10);
            for (j = 0; j < 256; j++) {
                int d = FFMIN(j, 256 - j);
                s->stats[i][j] = pels/(d + 1);
            }
        }
    } else {
        for (i = 0; i < 3; i++)
            for (j = 0; j < 256; j++)
                s->stats[i][j]= 0;
    }

    ff_huffyuv_alloc_temp(s);

    s->picture_number=0;

    return 0;
}
static int encode_422_bitstream(HYuvContext *s, int offset, int count)
{
    int i;
    const uint8_t *y = s->temp[0] + offset;
    const uint8_t *u = s->temp[1] + offset / 2;
    const uint8_t *v = s->temp[2] + offset / 2;

    if (s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb) >> 3) < 2 * 4 * count) {
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

#define LOAD4\
            int y0 = y[2 * i];\
            int y1 = y[2 * i + 1];\
            int u0 = u[i];\
            int v0 = v[i];

    count /= 2;

    if (s->flags & AV_CODEC_FLAG_PASS1) {
        for(i = 0; i < count; i++) {
            LOAD4;
            s->stats[0][y0]++;
            s->stats[1][u0]++;
            s->stats[0][y1]++;
            s->stats[2][v0]++;
        }
    }
    if (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT)
        return 0;
    if (s->context) {
        for (i = 0; i < count; i++) {
            LOAD4;
            s->stats[0][y0]++;
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);
            s->stats[1][u0]++;
            put_bits(&s->pb, s->len[1][u0], s->bits[1][u0]);
            s->stats[0][y1]++;
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);
            s->stats[2][v0]++;
            put_bits(&s->pb, s->len[2][v0], s->bits[2][v0]);
        }
    } else {
        for(i = 0; i < count; i++) {
            LOAD4;
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);
            put_bits(&s->pb, s->len[1][u0], s->bits[1][u0]);
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);
            put_bits(&s->pb, s->len[2][v0], s->bits[2][v0]);
        }
    }
    return 0;
}

static int encode_gray_bitstream(HYuvContext *s, int count)
{
    int i;

    if (s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb) >> 3) < 4 * count) {
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

#define LOAD2\
            int y0 = s->temp[0][2 * i];\
            int y1 = s->temp[0][2 * i + 1];
#define STAT2\
            s->stats[0][y0]++;\
            s->stats[0][y1]++;
#define WRITE2\
            put_bits(&s->pb, s->len[0][y0], s->bits[0][y0]);\
            put_bits(&s->pb, s->len[0][y1], s->bits[0][y1]);

    count /= 2;

    if (s->flags & AV_CODEC_FLAG_PASS1) {
        for (i = 0; i < count; i++) {
            LOAD2;
            STAT2;
        }
    }
    if (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT)
        return 0;

    if (s->context) {
        for (i = 0; i < count; i++) {
            LOAD2;
            STAT2;
            WRITE2;
        }
    } else {
        for (i = 0; i < count; i++) {
            LOAD2;
            WRITE2;
        }
    }
    return 0;
}

static inline int encode_bgra_bitstream(HYuvContext *s, int count, int planes)
{
    int i;

    if (s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb) >> 3) <
        4 * planes * count) {
        av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
        return -1;
    }

#define LOAD_GBRA                                                       \
    int g = s->temp[0][planes == 3 ? 3 * i + 1 : 4 * i + G];            \
    int b = s->temp[0][planes == 3 ? 3 * i + 2 : 4 * i + B] - g & 0xFF; \
    int r = s->temp[0][planes == 3 ? 3 * i + 0 : 4 * i + R] - g & 0xFF; \
    int a = s->temp[0][planes * i + A];

#define STAT_BGRA                                                       \
    s->stats[0][b]++;                                                   \
    s->stats[1][g]++;                                                   \
    s->stats[2][r]++;                                                   \
    if (planes == 4)                                                    \
        s->stats[2][a]++;

#define WRITE_GBRA                                                      \
    put_bits(&s->pb, s->len[1][g], s->bits[1][g]);                      \
    put_bits(&s->pb, s->len[0][b], s->bits[0][b]);                      \
    put_bits(&s->pb, s->len[2][r], s->bits[2][r]);                      \
    if (planes == 4)                                                    \
        put_bits(&s->pb, s->len[2][a], s->bits[2][a]);

    if ((s->flags & AV_CODEC_FLAG_PASS1) &&
        (s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT)) {
        for (i = 0; i < count; i++) {
            LOAD_GBRA;
            STAT_BGRA;
        }
    } else if (s->context || (s->flags & AV_CODEC_FLAG_PASS1)) {
        for (i = 0; i < count; i++) {
            LOAD_GBRA;
            STAT_BGRA;
            WRITE_GBRA;
        }
    } else {
        for (i = 0; i < count; i++) {
            LOAD_GBRA;
            WRITE_GBRA;
        }
    }
    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    HYuvContext *s = avctx->priv_data;
    const int width = s->width;
    const int width2 = s->width>>1;
    const int height = s->height;
    const int fake_ystride = s->interlaced ? pict->linesize[0]*2  : pict->linesize[0];
    const int fake_ustride = s->interlaced ? pict->linesize[1]*2  : pict->linesize[1];
    const int fake_vstride = s->interlaced ? pict->linesize[2]*2  : pict->linesize[2];
    const AVFrame * const p = pict;
    int i, j, size = 0, ret;

    if (!pkt->data &&
        (ret = av_new_packet(pkt, width * height * 3 * 4 + AV_INPUT_BUFFER_MIN_SIZE)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating output packet.\n");
        return ret;
    }

    if (s->context) {
        for (i = 0; i < 3; i++) {
            ff_huff_gen_len_table(s->len[i], s->stats[i]);
            if (ff_huffyuv_generate_bits_table(s->bits[i], s->len[i]) < 0)
                return -1;
            size += store_table(s, s->len[i], &pkt->data[size]);
        }

        for (i = 0; i < 3; i++)
            for (j = 0; j < 256; j++)
                s->stats[i][j] >>= 1;
    }

    init_put_bits(&s->pb, pkt->data + size, pkt->size - size);

    if (avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
        avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
        int lefty, leftu, leftv, y, cy;

        put_bits(&s->pb, 8, leftv = p->data[2][0]);
        put_bits(&s->pb, 8, lefty = p->data[0][1]);
        put_bits(&s->pb, 8, leftu = p->data[1][0]);
        put_bits(&s->pb, 8,         p->data[0][0]);

        lefty = sub_left_prediction(s, s->temp[0], p->data[0], width , 0);
        leftu = sub_left_prediction(s, s->temp[1], p->data[1], width2, 0);
        leftv = sub_left_prediction(s, s->temp[2], p->data[2], width2, 0);

        encode_422_bitstream(s, 2, width-2);

        if (s->predictor==MEDIAN) {
            int lefttopy, lefttopu, lefttopv;
            cy = y = 1;
            if (s->interlaced) {
                lefty = sub_left_prediction(s, s->temp[0], p->data[0] + p->linesize[0], width , lefty);
                leftu = sub_left_prediction(s, s->temp[1], p->data[1] + p->linesize[1], width2, leftu);
                leftv = sub_left_prediction(s, s->temp[2], p->data[2] + p->linesize[2], width2, leftv);

                encode_422_bitstream(s, 0, width);
                y++; cy++;
            }

            lefty = sub_left_prediction(s, s->temp[0], p->data[0] + fake_ystride, 4, lefty);
            leftu = sub_left_prediction(s, s->temp[1], p->data[1] + fake_ustride, 2, leftu);
            leftv = sub_left_prediction(s, s->temp[2], p->data[2] + fake_vstride, 2, leftv);

            encode_422_bitstream(s, 0, 4);

            lefttopy = p->data[0][3];
            lefttopu = p->data[1][1];
            lefttopv = p->data[2][1];
            s->hencdsp.sub_hfyu_median_pred(s->temp[0], p->data[0] + 4, p->data[0] + fake_ystride + 4, width  - 4, &lefty, &lefttopy);
            s->hencdsp.sub_hfyu_median_pred(s->temp[1], p->data[1] + 2, p->data[1] + fake_ustride + 2, width2 - 2, &leftu, &lefttopu);
            s->hencdsp.sub_hfyu_median_pred(s->temp[2], p->data[2] + 2, p->data[2] + fake_vstride + 2, width2 - 2, &leftv, &lefttopv);
            encode_422_bitstream(s, 0, width - 4);
            y++; cy++;

            for (; y < height; y++,cy++) {
                uint8_t *ydst, *udst, *vdst;

                if (s->bitstream_bpp == 12) {
                    while (2 * cy > y) {
                        ydst = p->data[0] + p->linesize[0] * y;
                        s->hencdsp.sub_hfyu_median_pred(s->temp[0], ydst - fake_ystride, ydst, width, &lefty, &lefttopy);
                        encode_gray_bitstream(s, width);
                        y++;
                    }
                    if (y >= height) break;
                }
                ydst = p->data[0] + p->linesize[0] * y;
                udst = p->data[1] + p->linesize[1] * cy;
                vdst = p->data[2] + p->linesize[2] * cy;

                s->hencdsp.sub_hfyu_median_pred(s->temp[0], ydst - fake_ystride, ydst, width,  &lefty, &lefttopy);
                s->hencdsp.sub_hfyu_median_pred(s->temp[1], udst - fake_ustride, udst, width2, &leftu, &lefttopu);
                s->hencdsp.sub_hfyu_median_pred(s->temp[2], vdst - fake_vstride, vdst, width2, &leftv, &lefttopv);

                encode_422_bitstream(s, 0, width);
            }
        } else {
            for (cy = y = 1; y < height; y++, cy++) {
                uint8_t *ydst, *udst, *vdst;

                /* encode a luma only line & y++ */
                if (s->bitstream_bpp == 12) {
                    ydst = p->data[0] + p->linesize[0] * y;

                    if (s->predictor == PLANE && s->interlaced < y) {
                        s->hencdsp.diff_bytes(s->temp[1], ydst, ydst - fake_ystride, width);

                        lefty = sub_left_prediction(s, s->temp[0], s->temp[1], width , lefty);
                    } else {
                        lefty = sub_left_prediction(s, s->temp[0], ydst, width , lefty);
                    }
                    encode_gray_bitstream(s, width);
                    y++;
                    if (y >= height) break;
                }

                ydst = p->data[0] + p->linesize[0] * y;
                udst = p->data[1] + p->linesize[1] * cy;
                vdst = p->data[2] + p->linesize[2] * cy;

                if (s->predictor == PLANE && s->interlaced < cy) {
                    s->hencdsp.diff_bytes(s->temp[1],          ydst, ydst - fake_ystride, width);
                    s->hencdsp.diff_bytes(s->temp[2],          udst, udst - fake_ustride, width2);
                    s->hencdsp.diff_bytes(s->temp[2] + width2, vdst, vdst - fake_vstride, width2);

                    lefty = sub_left_prediction(s, s->temp[0], s->temp[1], width , lefty);
                    leftu = sub_left_prediction(s, s->temp[1], s->temp[2], width2, leftu);
                    leftv = sub_left_prediction(s, s->temp[2], s->temp[2] + width2, width2, leftv);
                } else {
                    lefty = sub_left_prediction(s, s->temp[0], ydst, width , lefty);
                    leftu = sub_left_prediction(s, s->temp[1], udst, width2, leftu);
                    leftv = sub_left_prediction(s, s->temp[2], vdst, width2, leftv);
                }

                encode_422_bitstream(s, 0, width);
            }
        }
    } else if(avctx->pix_fmt == AV_PIX_FMT_RGB32) {
        uint8_t *data = p->data[0] + (height - 1) * p->linesize[0];
        const int stride = -p->linesize[0];
        const int fake_stride = -fake_ystride;
        int y;
        int leftr, leftg, leftb, lefta;

        put_bits(&s->pb, 8, lefta = data[A]);
        put_bits(&s->pb, 8, leftr = data[R]);
        put_bits(&s->pb, 8, leftg = data[G]);
        put_bits(&s->pb, 8, leftb = data[B]);

        sub_left_prediction_bgr32(s, s->temp[0], data + 4, width - 1,
                                  &leftr, &leftg, &leftb, &lefta);
        encode_bgra_bitstream(s, width - 1, 4);

        for (y = 1; y < s->height; y++) {
            uint8_t *dst = data + y*stride;
            if (s->predictor == PLANE && s->interlaced < y) {
                s->hencdsp.diff_bytes(s->temp[1], dst, dst - fake_stride, width * 4);
                sub_left_prediction_bgr32(s, s->temp[0], s->temp[1], width,
                                          &leftr, &leftg, &leftb, &lefta);
            } else {
                sub_left_prediction_bgr32(s, s->temp[0], dst, width,
                                          &leftr, &leftg, &leftb, &lefta);
            }
            encode_bgra_bitstream(s, width, 4);
        }
    } else if (avctx->pix_fmt == AV_PIX_FMT_RGB24) {
        uint8_t *data = p->data[0] + (height - 1) * p->linesize[0];
        const int stride = -p->linesize[0];
        const int fake_stride = -fake_ystride;
        int y;
        int leftr, leftg, leftb;

        put_bits(&s->pb, 8, leftr = data[0]);
        put_bits(&s->pb, 8, leftg = data[1]);
        put_bits(&s->pb, 8, leftb = data[2]);
        put_bits(&s->pb, 8, 0);

        sub_left_prediction_rgb24(s, s->temp[0], data + 3, width - 1,
                                  &leftr, &leftg, &leftb);
        encode_bgra_bitstream(s, width-1, 3);

        for (y = 1; y < s->height; y++) {
            uint8_t *dst = data + y * stride;
            if (s->predictor == PLANE && s->interlaced < y) {
                s->hencdsp.diff_bytes(s->temp[1], dst, dst - fake_stride,
                                      width * 3);
                sub_left_prediction_rgb24(s, s->temp[0], s->temp[1], width,
                                          &leftr, &leftg, &leftb);
            } else {
                sub_left_prediction_rgb24(s, s->temp[0], dst, width,
                                          &leftr, &leftg, &leftb);
            }
            encode_bgra_bitstream(s, width, 3);
        }
    } else {
        av_log(avctx, AV_LOG_ERROR, "Format not supported!\n");
    }
    emms_c();

    size += (put_bits_count(&s->pb) + 31) / 8;
    put_bits(&s->pb, 16, 0);
    put_bits(&s->pb, 15, 0);
    size /= 4;

    if ((s->flags & AV_CODEC_FLAG_PASS1) && (s->picture_number & 31) == 0) {
        int j;
        char *p = avctx->stats_out;
        char *end = p + 1024*30;
        for (i = 0; i < 3; i++) {
            for (j = 0; j < 256; j++) {
                snprintf(p, end-p, "%"PRIu64" ", s->stats[i][j]);
                p += strlen(p);
                s->stats[i][j]= 0;
            }
            snprintf(p, end-p, "\n");
            p++;
        }
    } else
        avctx->stats_out[0] = '\0';
    if (!(s->avctx->flags2 & AV_CODEC_FLAG2_NO_OUTPUT)) {
        flush_put_bits(&s->pb);
        s->bdsp.bswap_buf((uint32_t *) pkt->data, (uint32_t *) pkt->data, size);
    }

    s->picture_number++;

    pkt->size   = size * 4;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    HYuvContext *s = avctx->priv_data;

    ff_huffyuv_common_end(s);

    av_freep(&avctx->extradata);
    av_freep(&avctx->stats_out);

    return 0;
}

#define OFFSET(x) offsetof(HYuvContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

#define HUFF_CLASS(variant)                  \
static const AVClass variant ## _class = {   \
    .class_name = # variant,                 \
    .item_name  = av_default_item_name,      \
    .option     = variant ## _options,       \
    .version    = LIBAVUTIL_VERSION_INT,     \
}

#define FF_HUFFYUV_COMMON_OPTS \
{ "pred", "Prediction method", OFFSET(predictor), AV_OPT_TYPE_INT, { .i64 = LEFT }, LEFT, MEDIAN, VE, "pred" }, \
    { "left",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LEFT },   INT_MIN, INT_MAX, VE, "pred" }, \
    { "plane",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PLANE },  INT_MIN, INT_MAX, VE, "pred" }, \
    { "median", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MEDIAN }, INT_MIN, INT_MAX, VE, "pred" }

static const AVOption huffyuv_options[] = {
    FF_HUFFYUV_COMMON_OPTS,
    { NULL},
};

HUFF_CLASS(huffyuv);

AVCodec ff_huffyuv_encoder = {
    .name           = "huffyuv",
    .long_name      = NULL_IF_CONFIG_SMALL("Huffyuv / HuffYUV"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HUFFYUV,
    .priv_data_size = sizeof(HYuvContext),
    .priv_class     = &huffyuv_class,
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};

#if CONFIG_FFVHUFF_ENCODER
static const AVOption ffhuffyuv_options[] = {
    FF_HUFFYUV_COMMON_OPTS,
    { "context", "Set per-frame huffman tables", OFFSET(context), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VE },
    { NULL }
};

HUFF_CLASS(ffhuffyuv);

AVCodec ff_ffvhuff_encoder = {
    .name           = "ffvhuff",
    .long_name      = NULL_IF_CONFIG_SMALL("Huffyuv FFmpeg variant"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FFVHUFF,
    .priv_data_size = sizeof(HYuvContext),
    .priv_class     = &ffhuffyuv_class,
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_RGB32, AV_PIX_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
