/*
 * MagicYUV decoder
 * Copyright (c) 2016 Paul B Mahol
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

#include <stdlib.h>
#include <string.h>

#define CACHED_BITSTREAM_READER !ARCH_X86_32

#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "huffyuvdsp.h"
#include "internal.h"
#include "lossless_videodsp.h"
#include "thread.h"

typedef struct Slice {
    uint32_t start;
    uint32_t size;
} Slice;

typedef enum Prediction {
    LEFT = 1,
    GRADIENT,
    MEDIAN,
} Prediction;

typedef struct HuffEntry {
    uint8_t  len;
    uint16_t sym;
} HuffEntry;

typedef struct MagicYUVContext {
    AVFrame          *p;
    int               max;
    int               bps;
    int               slice_height;
    int               nb_slices;
    int               planes;         // number of encoded planes in bitstream
    int               decorrelate;    // postprocessing work
    int               color_matrix;   // video color matrix
    int               flags;
    int               interlaced;     // video is interlaced
    const uint8_t    *buf;            // pointer to AVPacket->data
    int               hshift[4];
    int               vshift[4];
    Slice            *slices[4];      // slice bitstream positions for each plane
    unsigned int      slices_size[4]; // slice sizes for each plane
    VLC               vlc[4];         // VLC for each plane
    int (*magy_decode_slice)(AVCodecContext *avctx, void *tdata,
                             int j, int threadnr);
    LLVidDSPContext   llviddsp;
} MagicYUVContext;

static int huff_build(const uint8_t len[], uint16_t codes_pos[33],
                      VLC *vlc, int nb_elems, void *logctx)
{
    HuffEntry he[4096];

    for (int i = 31; i > 0; i--)
        codes_pos[i] += codes_pos[i + 1];

    for (unsigned i = nb_elems; i-- > 0;)
        he[--codes_pos[len[i]]] = (HuffEntry){ len[i], i };

    ff_free_vlc(vlc);
    return ff_init_vlc_from_lengths(vlc, FFMIN(he[0].len, 12), nb_elems,
                                    &he[0].len, sizeof(he[0]),
                                    &he[0].sym, sizeof(he[0]), sizeof(he[0].sym),
                                    0, 0, logctx);
}

static void magicyuv_median_pred16(uint16_t *dst, const uint16_t *src1,
                                   const uint16_t *diff, intptr_t w,
                                   int *left, int *left_top, int max)
{
    int i;
    uint16_t l, lt;

    l  = *left;
    lt = *left_top;

    for (i = 0; i < w; i++) {
        l      = mid_pred(l, src1[i], (l + src1[i] - lt)) + diff[i];
        l     &= max;
        lt     = src1[i];
        dst[i] = l;
    }

    *left     = l;
    *left_top = lt;
}

static int magy_decode_slice10(AVCodecContext *avctx, void *tdata,
                               int j, int threadnr)
{
    MagicYUVContext *s = avctx->priv_data;
    int interlaced = s->interlaced;
    const int bps = s->bps;
    const int max = s->max - 1;
    AVFrame *p = s->p;
    int i, k, x;
    GetBitContext gb;
    uint16_t *dst;

    for (i = 0; i < s->planes; i++) {
        int left, lefttop, top;
        int height = AV_CEIL_RSHIFT(FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height), s->vshift[i]);
        int width = AV_CEIL_RSHIFT(avctx->coded_width, s->hshift[i]);
        int sheight = AV_CEIL_RSHIFT(s->slice_height, s->vshift[i]);
        ptrdiff_t fake_stride = (p->linesize[i] / 2) * (1 + interlaced);
        ptrdiff_t stride = p->linesize[i] / 2;
        int flags, pred;
        int ret = init_get_bits8(&gb, s->buf + s->slices[i][j].start,
                                 s->slices[i][j].size);

        if (ret < 0)
            return ret;

        flags = get_bits(&gb, 8);
        pred  = get_bits(&gb, 8);

        dst = (uint16_t *)p->data[i] + j * sheight * stride;
        if (flags & 1) {
            if (get_bits_left(&gb) < bps * width * height)
                return AVERROR_INVALIDDATA;
            for (k = 0; k < height; k++) {
                for (x = 0; x < width; x++)
                    dst[x] = get_bits(&gb, bps);

                dst += stride;
            }
        } else {
            for (k = 0; k < height; k++) {
                for (x = 0; x < width; x++) {
                    int pix;
                    if (get_bits_left(&gb) <= 0)
                        return AVERROR_INVALIDDATA;

                    pix = get_vlc2(&gb, s->vlc[i].table, s->vlc[i].bits, 3);
                    if (pix < 0)
                        return AVERROR_INVALIDDATA;

                    dst[x] = pix;
                }
                dst += stride;
            }
        }

        switch (pred) {
        case LEFT:
            dst = (uint16_t *)p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                s->llviddsp.add_left_pred_int16(dst, dst, max, width, dst[-fake_stride]);
                dst += stride;
            }
            break;
        case GRADIENT:
            dst = (uint16_t *)p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                top = dst[-fake_stride];
                left = top + dst[0];
                dst[0] = left & max;
                for (x = 1; x < width; x++) {
                    top = dst[x - fake_stride];
                    lefttop = dst[x - (fake_stride + 1)];
                    left += top - lefttop + dst[x];
                    dst[x] = left & max;
                }
                dst += stride;
            }
            break;
        case MEDIAN:
            dst = (uint16_t *)p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred_int16(dst, dst, max, width, 0);
                dst += stride;
            }
            lefttop = left = dst[0];
            for (k = 1 + interlaced; k < height; k++) {
                magicyuv_median_pred16(dst, dst - fake_stride, dst, width, &left, &lefttop, max);
                lefttop = left = dst[0];
                dst += stride;
            }
            break;
        default:
            avpriv_request_sample(avctx, "Unknown prediction: %d", pred);
        }
    }

    if (s->decorrelate) {
        int height = FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height);
        int width = avctx->coded_width;
        uint16_t *r = (uint16_t *)p->data[0] + j * s->slice_height * p->linesize[0] / 2;
        uint16_t *g = (uint16_t *)p->data[1] + j * s->slice_height * p->linesize[1] / 2;
        uint16_t *b = (uint16_t *)p->data[2] + j * s->slice_height * p->linesize[2] / 2;

        for (i = 0; i < height; i++) {
            for (k = 0; k < width; k++) {
                b[k] = (b[k] + g[k]) & max;
                r[k] = (r[k] + g[k]) & max;
            }
            b += p->linesize[0] / 2;
            g += p->linesize[1] / 2;
            r += p->linesize[2] / 2;
        }
    }

    return 0;
}

static int magy_decode_slice(AVCodecContext *avctx, void *tdata,
                             int j, int threadnr)
{
    MagicYUVContext *s = avctx->priv_data;
    int interlaced = s->interlaced;
    AVFrame *p = s->p;
    int i, k, x, min_width;
    GetBitContext gb;
    uint8_t *dst;

    for (i = 0; i < s->planes; i++) {
        int left, lefttop, top;
        int height = AV_CEIL_RSHIFT(FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height), s->vshift[i]);
        int width = AV_CEIL_RSHIFT(avctx->coded_width, s->hshift[i]);
        int sheight = AV_CEIL_RSHIFT(s->slice_height, s->vshift[i]);
        ptrdiff_t fake_stride = p->linesize[i] * (1 + interlaced);
        ptrdiff_t stride = p->linesize[i];
        const uint8_t *slice = s->buf + s->slices[i][j].start;
        int flags, pred;

        flags = bytestream_get_byte(&slice);
        pred  = bytestream_get_byte(&slice);

        dst = p->data[i] + j * sheight * stride;
        if (flags & 1) {
            if (s->slices[i][j].size - 2 < width * height)
                return AVERROR_INVALIDDATA;
            for (k = 0; k < height; k++) {
                bytestream_get_buffer(&slice, dst, width);
                dst += stride;
            }
        } else {
            int ret = init_get_bits8(&gb, slice, s->slices[i][j].size - 2);

            if (ret < 0)
                return ret;

            for (k = 0; k < height; k++) {
                for (x = 0; x < width; x++) {
                    int pix;
                    if (get_bits_left(&gb) <= 0)
                        return AVERROR_INVALIDDATA;

                    pix = get_vlc2(&gb, s->vlc[i].table, s->vlc[i].bits, 3);
                    if (pix < 0)
                        return AVERROR_INVALIDDATA;

                    dst[x] = pix;
                }
                dst += stride;
            }
        }

        switch (pred) {
        case LEFT:
            dst = p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred(dst, dst, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred(dst, dst, width, 0);
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                s->llviddsp.add_left_pred(dst, dst, width, dst[-fake_stride]);
                dst += stride;
            }
            break;
        case GRADIENT:
            dst = p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred(dst, dst, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred(dst, dst, width, 0);
                dst += stride;
            }
            min_width = FFMIN(width, 32);
            for (k = 1 + interlaced; k < height; k++) {
                top = dst[-fake_stride];
                left = top + dst[0];
                dst[0] = left;
                for (x = 1; x < min_width; x++) { /* dsp need aligned 32 */
                    top = dst[x - fake_stride];
                    lefttop = dst[x - (fake_stride + 1)];
                    left += top - lefttop + dst[x];
                    dst[x] = left;
                }
                if (width > 32)
                    s->llviddsp.add_gradient_pred(dst + 32, fake_stride, width - 32);
                dst += stride;
            }
            break;
        case MEDIAN:
            dst = p->data[i] + j * sheight * stride;
            s->llviddsp.add_left_pred(dst, dst, width, 0);
            dst += stride;
            if (interlaced) {
                s->llviddsp.add_left_pred(dst, dst, width, 0);
                dst += stride;
            }
            lefttop = left = dst[0];
            for (k = 1 + interlaced; k < height; k++) {
                s->llviddsp.add_median_pred(dst, dst - fake_stride,
                                             dst, width, &left, &lefttop);
                lefttop = left = dst[0];
                dst += stride;
            }
            break;
        default:
            avpriv_request_sample(avctx, "Unknown prediction: %d", pred);
        }
    }

    if (s->decorrelate) {
        int height = FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height);
        int width = avctx->coded_width;
        uint8_t *b = p->data[0] + j * s->slice_height * p->linesize[0];
        uint8_t *g = p->data[1] + j * s->slice_height * p->linesize[1];
        uint8_t *r = p->data[2] + j * s->slice_height * p->linesize[2];

        for (i = 0; i < height; i++) {
            s->llviddsp.add_bytes(b, g, width);
            s->llviddsp.add_bytes(r, g, width);
            b += p->linesize[0];
            g += p->linesize[1];
            r += p->linesize[2];
        }
    }

    return 0;
}

static int build_huffman(AVCodecContext *avctx, const uint8_t *table,
                         int table_size, int max)
{
    MagicYUVContext *s = avctx->priv_data;
    GetByteContext gb;
    uint8_t len[4096];
    uint16_t length_count[33] = { 0 };
    int i = 0, j = 0, k;

    bytestream2_init(&gb, table, table_size);

    while (bytestream2_get_bytes_left(&gb) > 0) {
        int b = bytestream2_peek_byteu(&gb) &  0x80;
        int x = bytestream2_get_byteu(&gb)  & ~0x80;
        int l = 1;

        if (b) {
            if (bytestream2_get_bytes_left(&gb) <= 0)
                break;
            l += bytestream2_get_byteu(&gb);
        }
        k = j + l;
        if (k > max || x == 0 || x > 32) {
            av_log(avctx, AV_LOG_ERROR, "Invalid Huffman codes\n");
            return AVERROR_INVALIDDATA;
        }

        length_count[x] += l;
        for (; j < k; j++)
            len[j] = x;

        if (j == max) {
            j = 0;
            if (huff_build(len, length_count, &s->vlc[i], max, avctx)) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
                return AVERROR_INVALIDDATA;
            }
            i++;
            if (i == s->planes) {
                break;
            }
            memset(length_count, 0, sizeof(length_count));
        }
    }

    if (i != s->planes) {
        av_log(avctx, AV_LOG_ERROR, "Huffman tables too short\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int magy_decode_frame(AVCodecContext *avctx, AVFrame *p,
                             int *got_frame, AVPacket *avpkt)
{
    MagicYUVContext *s = avctx->priv_data;
    GetByteContext gb;
    uint32_t first_offset, offset, next_offset, header_size, slice_width;
    int width, height, format, version, table_size;
    int ret, i, j;

    if (avpkt->size < 36)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    if (bytestream2_get_le32u(&gb) != MKTAG('M', 'A', 'G', 'Y'))
        return AVERROR_INVALIDDATA;

    header_size = bytestream2_get_le32u(&gb);
    if (header_size < 32 || header_size >= avpkt->size) {
        av_log(avctx, AV_LOG_ERROR,
               "header or packet too small %"PRIu32"\n", header_size);
        return AVERROR_INVALIDDATA;
    }

    version = bytestream2_get_byteu(&gb);
    if (version != 7) {
        avpriv_request_sample(avctx, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }

    s->hshift[1] =
    s->vshift[1] =
    s->hshift[2] =
    s->vshift[2] = 0;
    s->decorrelate = 0;
    s->bps = 8;

    format = bytestream2_get_byteu(&gb);
    switch (format) {
    case 0x65:
        avctx->pix_fmt = AV_PIX_FMT_GBRP;
        s->decorrelate = 1;
        break;
    case 0x66:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        s->decorrelate = 1;
        break;
    case 0x67:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        break;
    case 0x68:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->hshift[1] =
        s->hshift[2] = 1;
        break;
    case 0x69:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        s->hshift[1] =
        s->vshift[1] =
        s->hshift[2] =
        s->vshift[2] = 1;
        break;
    case 0x6a:
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        break;
    case 0x6b:
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        break;
    case 0x6c:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
        s->hshift[1] =
        s->hshift[2] = 1;
        s->bps = 10;
        break;
    case 0x76:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P10;
        s->bps = 10;
        break;
    case 0x6d:
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        s->decorrelate = 1;
        s->bps = 10;
        break;
    case 0x6e:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
        s->decorrelate = 1;
        s->bps = 10;
        break;
    case 0x6f:
        avctx->pix_fmt = AV_PIX_FMT_GBRP12;
        s->decorrelate = 1;
        s->bps = 12;
        break;
    case 0x70:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP12;
        s->decorrelate = 1;
        s->bps = 12;
        break;
    case 0x73:
        avctx->pix_fmt = AV_PIX_FMT_GRAY10;
        s->bps = 10;
        break;
    case 0x7b:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P10;
        s->hshift[1] =
        s->vshift[1] =
        s->hshift[2] =
        s->vshift[2] = 1;
        s->bps = 10;
        break;
    default:
        avpriv_request_sample(avctx, "Format 0x%X", format);
        return AVERROR_PATCHWELCOME;
    }
    s->max = 1 << s->bps;
    s->magy_decode_slice = s->bps == 8 ? magy_decode_slice : magy_decode_slice10;
    s->planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    bytestream2_skipu(&gb, 1);
    s->color_matrix = bytestream2_get_byteu(&gb);
    s->flags        = bytestream2_get_byteu(&gb);
    s->interlaced   = !!(s->flags & 2);
    bytestream2_skipu(&gb, 3);

    width  = bytestream2_get_le32u(&gb);
    height = bytestream2_get_le32u(&gb);
    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    slice_width = bytestream2_get_le32u(&gb);
    if (slice_width != avctx->coded_width) {
        avpriv_request_sample(avctx, "Slice width %"PRIu32, slice_width);
        return AVERROR_PATCHWELCOME;
    }
    s->slice_height = bytestream2_get_le32u(&gb);
    if (s->slice_height <= 0 || s->slice_height > INT_MAX - avctx->coded_height) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid slice height: %d\n", s->slice_height);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skipu(&gb, 4);

    s->nb_slices = (avctx->coded_height + s->slice_height - 1) / s->slice_height;
    if (s->nb_slices > INT_MAX / FFMAX(sizeof(Slice), 4 * 5)) {
        av_log(avctx, AV_LOG_ERROR,
               "invalid number of slices: %d\n", s->nb_slices);
        return AVERROR_INVALIDDATA;
    }

    if (s->interlaced) {
        if ((s->slice_height >> s->vshift[1]) < 2) {
            av_log(avctx, AV_LOG_ERROR, "impossible slice height\n");
            return AVERROR_INVALIDDATA;
        }
        if ((avctx->coded_height % s->slice_height) && ((avctx->coded_height % s->slice_height) >> s->vshift[1]) < 2) {
            av_log(avctx, AV_LOG_ERROR, "impossible height\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (bytestream2_get_bytes_left(&gb) <= s->nb_slices * s->planes * 5)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < s->planes; i++) {
        av_fast_malloc(&s->slices[i], &s->slices_size[i], s->nb_slices * sizeof(Slice));
        if (!s->slices[i])
            return AVERROR(ENOMEM);

        offset = bytestream2_get_le32u(&gb);
        if (offset >= avpkt->size - header_size)
            return AVERROR_INVALIDDATA;

        if (i == 0)
            first_offset = offset;

        for (j = 0; j < s->nb_slices - 1; j++) {
            s->slices[i][j].start = offset + header_size;

            next_offset = bytestream2_get_le32u(&gb);
            if (next_offset <= offset || next_offset >= avpkt->size - header_size)
                return AVERROR_INVALIDDATA;

            s->slices[i][j].size = next_offset - offset;
            if (s->slices[i][j].size < 2)
                return AVERROR_INVALIDDATA;
            offset = next_offset;
        }

        s->slices[i][j].start = offset + header_size;
        s->slices[i][j].size  = avpkt->size - s->slices[i][j].start;

        if (s->slices[i][j].size < 2)
            return AVERROR_INVALIDDATA;
    }

    if (bytestream2_get_byteu(&gb) != s->planes)
        return AVERROR_INVALIDDATA;

    bytestream2_skipu(&gb, s->nb_slices * s->planes);

    table_size = header_size + first_offset - bytestream2_tell(&gb);
    if (table_size < 2)
        return AVERROR_INVALIDDATA;

    ret = build_huffman(avctx, avpkt->data + bytestream2_tell(&gb),
                        table_size, s->max);
    if (ret < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    s->buf = avpkt->data;
    s->p = p;
    avctx->execute2(avctx, s->magy_decode_slice, NULL, NULL, s->nb_slices);

    if (avctx->pix_fmt == AV_PIX_FMT_GBRP   ||
        avctx->pix_fmt == AV_PIX_FMT_GBRAP  ||
        avctx->pix_fmt == AV_PIX_FMT_GBRP10 ||
        avctx->pix_fmt == AV_PIX_FMT_GBRAP10||
        avctx->pix_fmt == AV_PIX_FMT_GBRAP12||
        avctx->pix_fmt == AV_PIX_FMT_GBRP12) {
        FFSWAP(uint8_t*, p->data[0], p->data[1]);
        FFSWAP(int, p->linesize[0], p->linesize[1]);
    } else {
        switch (s->color_matrix) {
        case 1:
            p->colorspace = AVCOL_SPC_BT470BG;
            break;
        case 2:
            p->colorspace = AVCOL_SPC_BT709;
            break;
        }
        p->color_range = (s->flags & 4) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int magy_decode_init(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;
    ff_llviddsp_init(&s->llviddsp);
    return 0;
}

static av_cold int magy_decode_end(AVCodecContext *avctx)
{
    MagicYUVContext * const s = avctx->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->slices); i++) {
        av_freep(&s->slices[i]);
        s->slices_size[i] = 0;
        ff_free_vlc(&s->vlc[i]);
    }

    return 0;
}

const FFCodec ff_magicyuv_decoder = {
    .p.name           = "magicyuv",
    .p.long_name      = NULL_IF_CONFIG_SMALL("MagicYUV video"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MAGICYUV,
    .priv_data_size   = sizeof(MagicYUVContext),
    .init             = magy_decode_init,
    .close            = magy_decode_end,
    FF_CODEC_DECODE_CB(magy_decode_frame),
    .p.capabilities   = AV_CODEC_CAP_DR1 |
                        AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE,
};
