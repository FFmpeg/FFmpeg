/*
 * MagicYUV encoder
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"
#include "thread.h"
#include "lossless_videoencdsp.h"

#define MAGICYUV_EXTRADATA_SIZE 32

typedef enum Prediction {
    LEFT = 1,
    GRADIENT,
    MEDIAN,
} Prediction;

typedef struct HuffEntry {
    uint8_t  len;
    uint32_t code;
} HuffEntry;

typedef struct PTable {
    int     value;  ///< input value
    int64_t prob;   ///< number of occurences of this value in input
} PTable;

typedef struct MagicYUVContext {
    const AVClass       *class;
    int                  frame_pred;
    PutBitContext        pb;
    int                  planes;
    uint8_t              format;
    int                  slice_height;
    int                  nb_slices;
    int                  correlate;
    int                  hshift[4];
    int                  vshift[4];
    uint8_t             *slices[4];
    unsigned             slice_pos[4];
    unsigned             tables_size;
    uint8_t             *decorrelate_buf[2];
    HuffEntry            he[4][256];
    LLVidEncDSPContext   llvidencdsp;
    void (*predict)(struct MagicYUVContext *s, const uint8_t *src, uint8_t *dst,
                    ptrdiff_t stride, int width, int height);
} MagicYUVContext;

static void left_predict(MagicYUVContext *s,
                         const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                         int width, int height)
{
    uint8_t prev = 0;
    int i, j;

    for (i = 0; i < width; i++) {
        dst[i] = src[i] - prev;
        prev   = src[i];
    }
    dst += width;
    src += stride;
    for (j = 1; j < height; j++) {
        prev = src[-stride];
        for (i = 0; i < width; i++) {
            dst[i] = src[i] - prev;
            prev   = src[i];
        }
        dst += width;
        src += stride;
    }
}

static void gradient_predict(MagicYUVContext *s,
                             const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                             int width, int height)
{
    int left = 0, top, lefttop;
    int i, j;

    for (i = 0; i < width; i++) {
        dst[i] = src[i] - left;
        left   = src[i];
    }
    dst += width;
    src += stride;
    for (j = 1; j < height; j++) {
        top = src[-stride];
        left = src[0] - top;
        dst[0] = left;
        for (i = 1; i < width; i++) {
            top = src[i - stride];
            lefttop = src[i - (stride + 1)];
            left = src[i-1];
            dst[i] = (src[i] - top) - left + lefttop;
        }
        dst += width;
        src += stride;
    }
}

static void median_predict(MagicYUVContext *s,
                           const uint8_t *src, uint8_t *dst, ptrdiff_t stride,
                           int width, int height)
{
    int left = 0, lefttop;
    int i, j;

    for (i = 0; i < width; i++) {
        dst[i] = src[i] - left;
        left   = src[i];
    }
    dst += width;
    src += stride;
    for (j = 1; j < height; j++) {
        left = lefttop = src[-stride];
        s->llvidencdsp.sub_median_pred(dst, src - stride, src, width, &left, &lefttop);
        dst += width;
        src += stride;
    }
}

static av_cold int magy_encode_init(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;
    PutByteContext pb;
    int i;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_GBRP:
        avctx->codec_tag = MKTAG('M', '8', 'R', 'G');
        s->correlate = 1;
        s->format = 0x65;
        break;
    case AV_PIX_FMT_GBRAP:
        avctx->codec_tag = MKTAG('M', '8', 'R', 'A');
        s->correlate = 1;
        s->format = 0x66;
        break;
    case AV_PIX_FMT_YUV420P:
        avctx->codec_tag = MKTAG('M', '8', 'Y', '0');
        s->hshift[1] =
        s->vshift[1] =
        s->hshift[2] =
        s->vshift[2] = 1;
        s->format = 0x69;
        break;
    case AV_PIX_FMT_YUV422P:
        avctx->codec_tag = MKTAG('M', '8', 'Y', '2');
        s->hshift[1] =
        s->hshift[2] = 1;
        s->format = 0x68;
        break;
    case AV_PIX_FMT_YUV444P:
        avctx->codec_tag = MKTAG('M', '8', 'Y', '4');
        s->format = 0x67;
        break;
    case AV_PIX_FMT_YUVA444P:
        avctx->codec_tag = MKTAG('M', '8', 'Y', 'A');
        s->format = 0x6a;
        break;
    case AV_PIX_FMT_GRAY8:
        avctx->codec_tag = MKTAG('M', '8', 'G', '0');
        s->format = 0x6b;
        break;
    }
    if (s->correlate) {
        s->decorrelate_buf[0] = av_calloc(2U * avctx->height, FFALIGN(avctx->width, 16));
        if (!s->decorrelate_buf[0])
            return AVERROR(ENOMEM);
        s->decorrelate_buf[1] = s->decorrelate_buf[0] + avctx->height * FFALIGN(avctx->width, 16);
    }

    ff_llvidencdsp_init(&s->llvidencdsp);

    s->planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    s->nb_slices = 1;

    for (i = 0; i < s->planes; i++) {
        s->slices[i] = av_malloc(avctx->width * (avctx->height + 2) +
                                 AV_INPUT_BUFFER_PADDING_SIZE);
        if (!s->slices[i]) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer.\n");
            return AVERROR(ENOMEM);
        }
    }

    switch (s->frame_pred) {
    case LEFT:     s->predict = left_predict;     break;
    case GRADIENT: s->predict = gradient_predict; break;
    case MEDIAN:   s->predict = median_predict;   break;
    }

    avctx->extradata_size = MAGICYUV_EXTRADATA_SIZE;

    avctx->extradata = av_mallocz(avctx->extradata_size +
                                  AV_INPUT_BUFFER_PADDING_SIZE);

    if (!avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate extradata.\n");
        return AVERROR(ENOMEM);
    }

    bytestream2_init_writer(&pb, avctx->extradata, MAGICYUV_EXTRADATA_SIZE);
    bytestream2_put_le32(&pb, MKTAG('M', 'A', 'G', 'Y'));
    bytestream2_put_le32(&pb, 32);
    bytestream2_put_byte(&pb, 7);
    bytestream2_put_byte(&pb, s->format);
    bytestream2_put_byte(&pb, 12);
    bytestream2_put_byte(&pb, 0);

    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 32);
    bytestream2_put_byte(&pb, 0);

    bytestream2_put_le32(&pb, avctx->width);
    bytestream2_put_le32(&pb, avctx->height);
    bytestream2_put_le32(&pb, avctx->width);
    bytestream2_put_le32(&pb, avctx->height);

    return 0;
}

static void calculate_codes(HuffEntry *he, uint16_t codes_count[33])
{
    for (unsigned i = 32, nb_codes = 0; i > 0; i--) {
        uint16_t curr = codes_count[i];   // # of leafs of length i
        codes_count[i] = nb_codes / 2;    // # of non-leaf nodes on level i
        nb_codes = codes_count[i] + curr; // # of nodes on level i
    }

    for (unsigned i = 0; i < 256; i++) {
        he[i].code = codes_count[he[i].len];
        codes_count[he[i].len]++;
    }
}

static void count_usage(uint8_t *src, int width,
                        int height, PTable *counts)
{
    int i, j;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            counts[src[i]].prob++;
        }
        src += width;
    }
}

typedef struct PackageMergerList {
    int nitems;             ///< number of items in the list and probability      ex. 4
    int item_idx[515];      ///< index range for each item in items                   0, 2, 5, 9, 13
    int probability[514];   ///< probability of each item                             3, 8, 18, 46
    int items[257 * 16];    ///< chain of all individual values that make up items    A, B, A, B, C, A, B, C, D, C, D, D, E
} PackageMergerList;

static int compare_by_prob(const void *a, const void *b)
{
    const PTable *a2 = a;
    const PTable *b2 = b;
    return a2->prob - b2->prob;
}

static void magy_huffman_compute_bits(PTable *prob_table, HuffEntry *distincts,
                                      uint16_t codes_counts[33],
                                      int size, int max_length)
{
    PackageMergerList list_a, list_b, *to = &list_a, *from = &list_b, *temp;
    int times, i, j, k;
    int nbits[257] = {0};
    int min;

    av_assert0(max_length > 0);

    to->nitems = 0;
    from->nitems = 0;
    to->item_idx[0] = 0;
    from->item_idx[0] = 0;
    AV_QSORT(prob_table, size, PTable, compare_by_prob);

    for (times = 0; times <= max_length; times++) {
        to->nitems = 0;
        to->item_idx[0] = 0;

        j = 0;
        k = 0;

        if (times < max_length) {
            i = 0;
        }
        while (i < size || j + 1 < from->nitems) {
            to->nitems++;
            to->item_idx[to->nitems] = to->item_idx[to->nitems - 1];
            if (i < size &&
                (j + 1 >= from->nitems ||
                 prob_table[i].prob <
                     from->probability[j] + from->probability[j + 1])) {
                to->items[to->item_idx[to->nitems]++] = prob_table[i].value;
                to->probability[to->nitems - 1] = prob_table[i].prob;
                i++;
            } else {
                for (k = from->item_idx[j]; k < from->item_idx[j + 2]; k++) {
                    to->items[to->item_idx[to->nitems]++] = from->items[k];
                }
                to->probability[to->nitems - 1] =
                    from->probability[j] + from->probability[j + 1];
                j += 2;
            }
        }
        temp = to;
        to = from;
        from = temp;
    }

    min = (size - 1 < from->nitems) ? size - 1 : from->nitems;
    for (i = 0; i < from->item_idx[min]; i++) {
        nbits[from->items[i]]++;
    }

    for (i = 0; i < size; i++) {
        distincts[i].len = nbits[i];
        codes_counts[nbits[i]]++;
    }
}

static int encode_table(AVCodecContext *avctx, uint8_t *dst,
                        int width, int height,
                        PutBitContext *pb, HuffEntry *he)
{
    PTable counts[256] = { {0} };
    uint16_t codes_counts[33] = { 0 };
    int i;

    count_usage(dst, width, height, counts);

    for (i = 0; i < 256; i++) {
        counts[i].prob++;
        counts[i].value = i;
    }

    magy_huffman_compute_bits(counts, he, codes_counts, 256, 12);

    calculate_codes(he, codes_counts);

    for (i = 0; i < 256; i++) {
        put_bits(pb, 1, 0);
        put_bits(pb, 7, he[i].len);
    }

    return 0;
}

static int encode_slice(uint8_t *src, uint8_t *dst, int dst_size,
                        int width, int height, HuffEntry *he, int prediction)
{
    PutBitContext pb;
    int i, j;
    int count;

    init_put_bits(&pb, dst, dst_size);

    put_bits(&pb, 8, 0);
    put_bits(&pb, 8, prediction);

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            const int idx = src[i];
            put_bits(&pb, he[idx].len, he[idx].code);
        }

        src += width;
    }

    count = put_bits_count(&pb) & 0x1F;

    if (count)
        put_bits(&pb, 32 - count, 0);

    flush_put_bits(&pb);

    return put_bytes_output(&pb);
}

static int magy_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    MagicYUVContext *s = avctx->priv_data;
    PutByteContext pb;
    const int width = avctx->width, height = avctx->height;
    int pos, slice, i, j, ret = 0;

    ret = ff_alloc_packet(avctx, pkt, (256 + 4 * s->nb_slices + width * height) *
                          s->planes + 256);
    if (ret < 0)
        return ret;

    bytestream2_init_writer(&pb, pkt->data, pkt->size);
    bytestream2_put_le32(&pb, MKTAG('M', 'A', 'G', 'Y'));
    bytestream2_put_le32(&pb, 32); // header size
    bytestream2_put_byte(&pb, 7);  // version
    bytestream2_put_byte(&pb, s->format);
    bytestream2_put_byte(&pb, 12); // max huffman length
    bytestream2_put_byte(&pb, 0);

    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 32); // coder type
    bytestream2_put_byte(&pb, 0);

    bytestream2_put_le32(&pb, avctx->width);
    bytestream2_put_le32(&pb, avctx->height);
    bytestream2_put_le32(&pb, avctx->width);
    bytestream2_put_le32(&pb, avctx->height);
    bytestream2_put_le32(&pb, 0);

    for (i = 0; i < s->planes; i++) {
        bytestream2_put_le32(&pb, 0);
        for (j = 1; j < s->nb_slices; j++) {
            bytestream2_put_le32(&pb, 0);
        }
    }

    bytestream2_put_byte(&pb, s->planes);

    for (i = 0; i < s->planes; i++) {
        for (slice = 0; slice < s->nb_slices; slice++) {
            bytestream2_put_byte(&pb, i);
        }
    }

    if (s->correlate) {
        uint8_t *r, *g, *b, *decorrelated[2] = { s->decorrelate_buf[0],
                                                 s->decorrelate_buf[1] };
        const int decorrelate_linesize = FFALIGN(width, 16);
        const uint8_t *const data[4] = { decorrelated[0], frame->data[0],
                                         decorrelated[1], frame->data[3] };
        const int linesize[4]  = { decorrelate_linesize, frame->linesize[0],
                                   decorrelate_linesize, frame->linesize[3] };

        g = frame->data[0];
        b = frame->data[1];
        r = frame->data[2];

        for (i = 0; i < height; i++) {
            s->llvidencdsp.diff_bytes(decorrelated[0], b, g, width);
            s->llvidencdsp.diff_bytes(decorrelated[1], r, g, width);
            g += frame->linesize[0];
            b += frame->linesize[1];
            r += frame->linesize[2];
            decorrelated[0] += decorrelate_linesize;
            decorrelated[1] += decorrelate_linesize;
        }

        for (i = 0; i < s->planes; i++) {
            for (slice = 0; slice < s->nb_slices; slice++) {
                s->predict(s, data[i], s->slices[i], linesize[i],
                           frame->width, frame->height);
            }
        }
    } else {
        for (i = 0; i < s->planes; i++) {
            for (slice = 0; slice < s->nb_slices; slice++) {
                s->predict(s, frame->data[i], s->slices[i], frame->linesize[i],
                           AV_CEIL_RSHIFT(frame->width, s->hshift[i]),
                           AV_CEIL_RSHIFT(frame->height, s->vshift[i]));
            }
        }
    }

    init_put_bits(&s->pb, pkt->data + bytestream2_tell_p(&pb), bytestream2_get_bytes_left_p(&pb));

    for (i = 0; i < s->planes; i++) {
        encode_table(avctx, s->slices[i],
                     AV_CEIL_RSHIFT(frame->width,  s->hshift[i]),
                     AV_CEIL_RSHIFT(frame->height, s->vshift[i]),
                     &s->pb, s->he[i]);
    }
    s->tables_size = put_bytes_count(&s->pb, 1);
    bytestream2_skip_p(&pb, s->tables_size);

    for (i = 0; i < s->planes; i++) {
        unsigned slice_size;

        s->slice_pos[i] = bytestream2_tell_p(&pb);
        slice_size = encode_slice(s->slices[i], pkt->data + bytestream2_tell_p(&pb),
                                  bytestream2_get_bytes_left_p(&pb),
                                  AV_CEIL_RSHIFT(frame->width,  s->hshift[i]),
                                  AV_CEIL_RSHIFT(frame->height, s->vshift[i]),
                                  s->he[i], s->frame_pred);
        bytestream2_skip_p(&pb, slice_size);
    }

    pos = bytestream2_tell_p(&pb);
    bytestream2_seek_p(&pb, 32, SEEK_SET);
    bytestream2_put_le32(&pb, s->slice_pos[0] - 32);
    for (i = 0; i < s->planes; i++) {
        bytestream2_put_le32(&pb, s->slice_pos[i] - 32);
    }
    bytestream2_seek_p(&pb, pos, SEEK_SET);

    pkt->size   = bytestream2_tell_p(&pb);

    *got_packet = 1;

    return 0;
}

static av_cold int magy_encode_close(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->planes; i++)
        av_freep(&s->slices[i]);
    av_freep(&s->decorrelate_buf);

    return 0;
}

#define OFFSET(x) offsetof(MagicYUVContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pred", "Prediction method", OFFSET(frame_pred), AV_OPT_TYPE_INT, {.i64=LEFT}, LEFT, MEDIAN, VE, "pred" },
    { "left",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LEFT },     0, 0, VE, "pred" },
    { "gradient", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = GRADIENT }, 0, 0, VE, "pred" },
    { "median",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MEDIAN },   0, 0, VE, "pred" },
    { NULL},
};

static const AVClass magicyuv_class = {
    .class_name = "magicyuv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_magicyuv_encoder = {
    .p.name           = "magicyuv",
    .p.long_name      = NULL_IF_CONFIG_SMALL("MagicYUV video"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MAGICYUV,
    .priv_data_size   = sizeof(MagicYUVContext),
    .p.priv_class     = &magicyuv_class,
    .init             = magy_encode_init,
    .close            = magy_encode_close,
    FF_CODEC_ENCODE_CB(magy_encode_frame),
    .p.capabilities   = AV_CODEC_CAP_FRAME_THREADS,
    .p.pix_fmts       = (const enum AVPixelFormat[]) {
                          AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_YUV422P,
                          AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_GRAY8,
                          AV_PIX_FMT_NONE
                      },
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
