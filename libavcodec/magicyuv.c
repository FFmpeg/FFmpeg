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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/qsort.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "huffyuvdsp.h"
#include "internal.h"
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

typedef struct MagicYUVContext {
    AVFrame            *p;
    int                 slice_height;
    int                 nb_slices;
    int                 planes;         // number of encoded planes in bitstream
    int                 decorrelate;    // postprocessing work
    int                 interlaced;     // video is interlaced
    uint8_t             *buf;           // pointer to AVPacket->data
    int                 hshift[4];
    int                 vshift[4];
    Slice               *slices[4];     // slice positions and size in bitstream for each plane
    int                 slices_size[4];
    uint8_t             len[4][256];    // table of code lengths for each plane
    VLC                 vlc[4];         // VLC for each plane
    HuffYUVDSPContext   hdsp;
} MagicYUVContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;
    ff_huffyuvdsp_init(&s->hdsp);
    return 0;
}

typedef struct HuffEntry {
    uint8_t  sym;
    uint8_t  len;
    uint32_t code;
} HuffEntry;

static int ff_magy_huff_cmp_len(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len) * 256 + aa->sym - bb->sym;
}

static int build_huff(VLC *vlc, uint8_t *len)
{
    HuffEntry he[256];
    uint32_t codes[256];
    uint8_t bits[256];
    uint8_t syms[256];
    uint32_t code;
    int i;

    for (i = 0; i < 256; i++) {
        he[i].sym = 255 - i;
        he[i].len = len[i];
    }
    AV_QSORT(he, 256, HuffEntry, ff_magy_huff_cmp_len);

    code = 1;
    for (i = 255; i >= 0; i--) {
        codes[i] = code >> (32 - he[i].len);
        bits[i]  = he[i].len;
        syms[i]  = he[i].sym;
        code += 0x80000000u >> (he[i].len - 1);
    }

    ff_free_vlc(vlc);
    return ff_init_vlc_sparse(vlc, FFMIN(he[255].len, 12), 256,
                              bits,  sizeof(*bits),  sizeof(*bits),
                              codes, sizeof(*codes), sizeof(*codes),
                              syms,  sizeof(*syms),  sizeof(*syms), 0);
}

static int decode_slice(AVCodecContext *avctx, void *tdata,
                        int j, int threadnr)
{
    MagicYUVContext *s = avctx->priv_data;
    int interlaced = s->interlaced;
    AVFrame *p = s->p;
    int i, k, x, ret;
    GetBitContext b;
    uint8_t *dst;

    for (i = 0; i < s->planes; i++) {
        int height = AV_CEIL_RSHIFT(FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height), s->vshift[i]);
        int width = AV_CEIL_RSHIFT(avctx->coded_width, s->hshift[i]);
        int sheight = AV_CEIL_RSHIFT(s->slice_height, s->vshift[i]);
        int fake_stride = p->linesize[i] * (1 + interlaced);
        int stride = p->linesize[i];
        int flags, pred;

        if ((ret = init_get_bits8(&b, s->buf + s->slices[i][j].start, s->slices[i][j].size)) < 0)
            return ret;

        flags = get_bits(&b, 8);
        pred  = get_bits(&b, 8);

        dst = p->data[i] + j * sheight * stride;
        if (flags & 1) {
            for (k = 0; k < height; k++) {
                for (x = 0; x < width; x++) {
                    dst[x] = get_bits(&b, 8);
                }
                dst += stride;
            }
        } else {
            for (k = 0; k < height; k++) {
                for (x = 0; x < width; x++) {
                    int pix;
                    if (get_bits_left(&b) <= 0) {
                        return AVERROR_INVALIDDATA;
                    }
                    pix = get_vlc2(&b, s->vlc[i].table, s->vlc[i].bits, 3);
                    if (pix < 0) {
                        return AVERROR_INVALIDDATA;
                    }
                    dst[x] = 255 - pix;
                }
                dst += stride;
            }
        }

        if (pred == LEFT) {
            dst = p->data[i] + j * sheight * stride;
            s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
            dst += stride;
            if (interlaced) {
                s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                s->hdsp.add_hfyu_left_pred(dst, dst, width, dst[-fake_stride]);
                dst += stride;
            }
        } else if (pred == GRADIENT) {
            int left, lefttop, top;

            dst = p->data[i] + j * sheight * stride;
            s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
            left = lefttop = 0;
            dst += stride;
            if (interlaced) {
                s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
                left = lefttop = 0;
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                top = dst[-fake_stride];
                left = top + dst[0];
                dst[0] = left;
                for (x = 1; x < width; x++) {
                    top = dst[x - fake_stride];
                    lefttop = dst[x - (fake_stride + 1)];
                    left += top - lefttop + dst[x];
                    dst[x] = left;
                }
                dst += stride;
            }
        } else if (pred == MEDIAN) {
            int left, lefttop;

            dst = p->data[i] + j * sheight * stride;
            lefttop = left = dst[0];
            s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
            dst += stride;
            if (interlaced) {
                lefttop = left = dst[0];
                s->hdsp.add_hfyu_left_pred(dst, dst, width, 0);
                dst += stride;
            }
            for (k = 1 + interlaced; k < height; k++) {
                s->hdsp.add_hfyu_median_pred(dst, dst - fake_stride, dst, width, &left, &lefttop);
                lefttop = left = dst[0];
                dst += stride;
            }
        } else {
            avpriv_request_sample(avctx, "unknown prediction: %d", pred);
        }
    }

    if (s->decorrelate) {
        int height = FFMIN(s->slice_height, avctx->coded_height - j * s->slice_height);
        int width = avctx->coded_width;
        uint8_t *b = p->data[0] + j * s->slice_height * p->linesize[0];
        uint8_t *g = p->data[1] + j * s->slice_height * p->linesize[1];
        uint8_t *r = p->data[2] + j * s->slice_height * p->linesize[2];

        for (i = 0; i < height; i++) {
            s->hdsp.add_bytes(b, g, width);
            s->hdsp.add_bytes(r, g, width);
            b += p->linesize[0];
            g += p->linesize[1];
            r += p->linesize[2];
        }
    }

    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    uint32_t first_offset, offset, next_offset, header_size, slice_width;
    int ret, format, version, table_size;
    MagicYUVContext *s = avctx->priv_data;
    ThreadFrame frame = { .f = data };
    AVFrame *p = data;
    GetByteContext gb;
    GetBitContext b;
    int i, j, k, width, height;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    if (bytestream2_get_le32(&gb) != MKTAG('M','A','G','Y'))
        return AVERROR_INVALIDDATA;

    header_size = bytestream2_get_le32(&gb);
    if (header_size < 32 || header_size >= avpkt->size)
        return AVERROR_INVALIDDATA;

    version = bytestream2_get_byte(&gb);
    if (version != 7) {
        avpriv_request_sample(avctx, "unsupported version: %d", version);
        return AVERROR_PATCHWELCOME;
    }

    s->hshift[1] = s->vshift[1] = 0;
    s->hshift[2] = s->vshift[2] = 0;
    s->decorrelate = 0;

    format = bytestream2_get_byte(&gb);
    switch (format) {
    case 0x65:
        avctx->pix_fmt = AV_PIX_FMT_GBRP;
        s->decorrelate = 1;
        s->planes = 3;
        break;
    case 0x66:
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        s->decorrelate = 1;
        s->planes = 4;
        break;
    case 0x67:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        s->planes = 3;
        break;
    case 0x68:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->planes = 3;
        s->hshift[1] = s->hshift[2] = 1;
        break;
    case 0x69:
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        s->planes = 3;
        s->hshift[1] = s->vshift[1] = 1;
        s->hshift[2] = s->vshift[2] = 1;
        break;
    case 0x6a:
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P;
        s->planes = 4;
        break;
    case 0x6b:
        avctx->pix_fmt = AV_PIX_FMT_GRAY8;
        s->planes = 1;
        break;
    default:
        avpriv_request_sample(avctx, "unsupported format: 0x%X", format);
        return AVERROR_PATCHWELCOME;
    }

    bytestream2_skip(&gb, 2);
    s->interlaced = !!(bytestream2_get_byte(&gb) & 2);
    bytestream2_skip(&gb, 3);

    width  = bytestream2_get_le32(&gb);
    height = bytestream2_get_le32(&gb);
    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    slice_width = bytestream2_get_le32(&gb);
    if (slice_width != avctx->coded_width) {
        avpriv_request_sample(avctx, "unsupported slice width: %d", slice_width);
        return AVERROR_PATCHWELCOME;
    }
    s->slice_height = bytestream2_get_le32(&gb);
    if ((s->slice_height <= 0) || (s->slice_height > INT_MAX - avctx->coded_height)) {
        av_log(avctx, AV_LOG_ERROR, "invalid slice height: %d\n", s->slice_height);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(&gb, 4);

    s->nb_slices = (avctx->coded_height + s->slice_height - 1) / s->slice_height;
    if (s->nb_slices > INT_MAX / sizeof(Slice)) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of slices: %d\n", s->nb_slices);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < s->planes; i++) {
        av_fast_malloc(&s->slices[i], &s->slices_size[i], s->nb_slices * sizeof(Slice));
        if (!s->slices[i])
            return AVERROR(ENOMEM);

        offset = bytestream2_get_le32(&gb);
        if (offset >= avpkt->size - header_size)
            return AVERROR_INVALIDDATA;

        if (i == 0)
            first_offset = offset;

        for (j = 0; j < s->nb_slices - 1; j++) {
            s->slices[i][j].start = offset + header_size;
            next_offset = bytestream2_get_le32(&gb);
            s->slices[i][j].size  = next_offset - offset;
            offset = next_offset;

            if (offset >= avpkt->size - header_size)
                return AVERROR_INVALIDDATA;
        }

        s->slices[i][j].start = offset + header_size;
        s->slices[i][j].size  = avpkt->size - s->slices[i][j].start;
    }

    if (bytestream2_get_byte(&gb) != s->planes)
        return AVERROR_INVALIDDATA;

    bytestream2_skip(&gb, s->nb_slices * s->planes);

    table_size = header_size + first_offset - bytestream2_tell(&gb);
    if (table_size < 2)
        return AVERROR_INVALIDDATA;

    if ((ret = init_get_bits8(&b, avpkt->data + bytestream2_tell(&gb), table_size)) < 0)
        return ret;

    memset(s->len, 0, sizeof(s->len));
    j = i = 0;
    while (get_bits_left(&b) >= 8) {
        int l = get_bits(&b, 4);
        int x = get_bits(&b, 4);
        int L = get_bitsz(&b, l) + 1;

        for (k = 0; k < L; k++) {
            if (j + k < 256)
                s->len[i][j + k] = x;
        }

        j += L;
        if (j == 256) {
            j = 0;
            if (build_huff(&s->vlc[i], s->len[i])) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
                return AVERROR_INVALIDDATA;
            }
            i++;
            if (i == s->planes) {
                break;
            }
        } else if (j > 256) {
            return AVERROR_INVALIDDATA;
        }
    }

    if (i != s->planes) {
        av_log(avctx, AV_LOG_ERROR, "Huffman tables too short\n");
        return AVERROR_INVALIDDATA;
    }

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    s->buf = avpkt->data;
    s->p = p;
    avctx->execute2(avctx, decode_slice, NULL, NULL, s->nb_slices);

    if (avctx->pix_fmt == AV_PIX_FMT_GBRP ||
        avctx->pix_fmt == AV_PIX_FMT_GBRAP) {
        FFSWAP(uint8_t*, p->data[0], p->data[1]);
        FFSWAP(int, p->linesize[0], p->linesize[1]);
    }

    *got_frame = 1;

    if (ret < 0)
        return ret;
    return avpkt->size;
}

#if HAVE_THREADS
static int decode_init_thread_copy(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;

    s->slices[0] = 0;
    s->slices[1] = 0;
    s->slices[2] = 0;
    s->slices[3] = 0;
    s->slices_size[0] = 0;
    s->slices_size[1] = 0;
    s->slices_size[2] = 0;
    s->slices_size[3] = 0;

    return 0;
}
#endif

static av_cold int decode_end(AVCodecContext *avctx)
{
    MagicYUVContext * const s = avctx->priv_data;

    av_freep(&s->slices[0]);
    av_freep(&s->slices[1]);
    av_freep(&s->slices[2]);
    av_freep(&s->slices[3]);
    s->slices_size[0] = 0;
    s->slices_size[1] = 0;
    s->slices_size[2] = 0;
    s->slices_size[3] = 0;
    ff_free_vlc(&s->vlc[0]);
    ff_free_vlc(&s->vlc[1]);
    ff_free_vlc(&s->vlc[2]);
    ff_free_vlc(&s->vlc[3]);

    return 0;
}

AVCodec ff_magicyuv_decoder = {
    .name             = "magicyuv",
    .long_name        = NULL_IF_CONFIG_SMALL("MagicYUV Lossless Video"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MAGICYUV,
    .priv_data_size   = sizeof(MagicYUVContext),
    .init             = decode_init,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .close            = decode_end,
    .decode           = decode_frame,
    .capabilities     = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS,
};
