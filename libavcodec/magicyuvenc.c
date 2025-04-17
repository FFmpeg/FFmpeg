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

#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"
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

typedef struct Slice {
    int width;
    int height;
    int encode_raw;
    unsigned pos;
    unsigned size;
    uint8_t *slice;
    uint8_t *dst;
    int64_t counts[256];
} Slice;

typedef struct MagicYUVContext {
    const AVClass       *class;
    int                  frame_pred;
    int                  planes;
    uint8_t              format;
    int                  slice_height;
    int                  nb_slices;
    int                  correlate;
    int                  hshift[4];
    int                  vshift[4];
    uint8_t             *decorrelate_buf[2];
    Slice               *slices;
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

    ff_llvidencdsp_init(&s->llvidencdsp);

    s->planes = av_pix_fmt_count_planes(avctx->pix_fmt);

    s->nb_slices = avctx->slices > 0 ? avctx->slices : avctx->thread_count;
    s->nb_slices = FFMIN(s->nb_slices, avctx->height >> s->vshift[1]);
    s->nb_slices = FFMAX(1, s->nb_slices);
    s->slice_height = FFALIGN((avctx->height + s->nb_slices - 1) / s->nb_slices, 1 << s->vshift[1]);
    s->nb_slices = (avctx->height + s->slice_height - 1) / s->slice_height;
    s->nb_slices = FFMIN(256U / s->planes, s->nb_slices);
    s->slices = av_calloc(s->nb_slices * s->planes, sizeof(*s->slices));
    if (!s->slices)
        return AVERROR(ENOMEM);

    if (s->correlate) {
        size_t max_align = av_cpu_max_align();
        size_t aligned_width = FFALIGN(avctx->width, max_align);
        s->decorrelate_buf[0] = av_calloc(2U * (s->nb_slices * s->slice_height),
                                          aligned_width);
        if (!s->decorrelate_buf[0])
            return AVERROR(ENOMEM);
        s->decorrelate_buf[1] = s->decorrelate_buf[0] + (s->nb_slices * s->slice_height) * aligned_width;
    }

    for (int n = 0; n < s->nb_slices; n++) {
        for (int i = 0; i < s->planes; i++) {
            Slice *sl = &s->slices[n * s->planes + i];

            sl->height = n == s->nb_slices - 1 ? avctx->height - n * s->slice_height : s->slice_height;
            sl->height = AV_CEIL_RSHIFT(sl->height, s->vshift[i]);
            sl->width  = AV_CEIL_RSHIFT(avctx->width, s->hshift[i]);

            sl->slice = av_malloc(avctx->width * (s->slice_height + 2) +
                                                     AV_INPUT_BUFFER_PADDING_SIZE);
            if (!sl->slice)
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
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    bytestream2_init_writer(&pb, avctx->extradata, MAGICYUV_EXTRADATA_SIZE);
    bytestream2_put_le32u(&pb, MKTAG('M', 'A', 'G', 'Y'));
    bytestream2_put_le32u(&pb, 32);
    bytestream2_put_byteu(&pb, 7);
    bytestream2_put_byteu(&pb, s->format);
    bytestream2_put_byteu(&pb, 12);
    bytestream2_put_byteu(&pb, 0);

    bytestream2_put_byteu(&pb, 0);
    bytestream2_put_byteu(&pb, 0);
    bytestream2_put_byteu(&pb, 32);
    bytestream2_put_byteu(&pb, 0);

    bytestream2_put_le32u(&pb, avctx->width);
    bytestream2_put_le32u(&pb, avctx->height);
    bytestream2_put_le32u(&pb, avctx->width);
    bytestream2_put_le32u(&pb, avctx->height);

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

static void count_usage(const uint8_t *src, int width,
                        int height, int64_t *counts)
{
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++)
            counts[src[i]]++;
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

static int count_plane_slice(AVCodecContext *avctx, int n, int plane)
{
    MagicYUVContext *s = avctx->priv_data;
    Slice *sl = &s->slices[n * s->planes + plane];
    const uint8_t *dst = sl->slice;
    int64_t *counts = sl->counts;

    memset(counts, 0, sizeof(sl->counts));

    count_usage(dst, sl->width, sl->height, counts);

    return 0;
}

static void generate_codes(AVCodecContext *avctx,
                           HuffEntry *he, int plane)
{
    MagicYUVContext *s = avctx->priv_data;
    PTable counts[256];
    uint16_t codes_counts[33] = { 0 };

    for (size_t i = 0; i < FF_ARRAY_ELEMS(counts); i++) {
        counts[i].prob  = 1;
        counts[i].value = i;
    }

    for (int n = 0; n < s->nb_slices; n++) {
        Slice *sl = &s->slices[n * s->planes + plane];
        int64_t *slice_counts = sl->counts;

        for (int i = 0; i < 256; i++)
            counts[i].prob += slice_counts[i];
    }

    magy_huffman_compute_bits(counts, he, codes_counts, 256, 12);

    calculate_codes(he, codes_counts);
}

static void output_codes(PutByteContext *pb, const HuffEntry he[256])
{
    for (int i = 0; i < 256; i++) {
        // The seven low bits are len; the top bit means the run of
        // codes of this length has length one.
        bytestream2_put_byteu(pb, he[i].len);
    }
}

static void encode_plane_slice_raw(const uint8_t *src, uint8_t *dst,
                                   int width, int height, int prediction)
{
    unsigned count = width * height;

    dst[0] = 1;
    dst[1] = prediction;

    memcpy(dst + 2, src, count);
}

static void encode_plane_slice(const uint8_t *src, uint8_t *dst, unsigned dst_size,
                               int width, int height, HuffEntry *he, int prediction)
{
    PutBitContext pb;

    init_put_bits(&pb, dst, dst_size);

    put_bits(&pb, 8, 0);
    put_bits(&pb, 8, prediction);

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            const int idx = src[i];
            const int len = he[idx].len;
            put_bits(&pb, len, he[idx].code);
        }

        src += width;
    }

    flush_put_bits(&pb);
    av_assert1(put_bytes_left(&pb, 0) <= 3);
}

static int encode_slice(AVCodecContext *avctx, void *tdata,
                        int n, int threadnr)
{
    MagicYUVContext *s = avctx->priv_data;

    for (int i = 0; i < s->planes; i++) {
        Slice *sl = &s->slices[n * s->planes + i];

        // Zero the padding now
        AV_WN32(sl->dst + sl->size - 4, 0);

        if (sl->encode_raw)
            encode_plane_slice_raw(sl->slice, sl->dst,
                                   sl->width, sl->height, s->frame_pred);
        else
            encode_plane_slice(sl->slice,
                               sl->dst,
                               sl->size,
                               sl->width, sl->height,
                               s->he[i], s->frame_pred);
    }

    return 0;
}

static int predict_slice(AVCodecContext *avctx, void *tdata,
                         int n, int threadnr)
{
    size_t max_align = av_cpu_max_align();
    const int aligned_width = FFALIGN(avctx->width, max_align);
    MagicYUVContext *s = avctx->priv_data;
    const int slice_height = s->slice_height;
    const int last_height = FFMIN(slice_height, avctx->height - n * slice_height);
    const int height = (n < (s->nb_slices - 1)) ? slice_height : last_height;
    const int width = avctx->width;
    AVFrame *frame = tdata;

    if (s->correlate) {
        uint8_t *decorrelated[2] = { s->decorrelate_buf[0] + n * slice_height * aligned_width,
                                     s->decorrelate_buf[1] + n * slice_height * aligned_width };
        const int decorrelate_linesize = aligned_width;
        const uint8_t *const data[4] = { decorrelated[0], frame->data[0] + n * slice_height * frame->linesize[0],
                                         decorrelated[1], s->planes == 4 ? frame->data[3] + n * slice_height * frame->linesize[3] : NULL };
        const uint8_t *r, *g, *b;
        const int linesize[4]  = { decorrelate_linesize, frame->linesize[0],
                                   decorrelate_linesize, frame->linesize[3] };

        g = frame->data[0] + n * slice_height * frame->linesize[0];
        b = frame->data[1] + n * slice_height * frame->linesize[1];
        r = frame->data[2] + n * slice_height * frame->linesize[2];

        for (int i = 0; i < height; i++) {
            s->llvidencdsp.diff_bytes(decorrelated[0], b, g, width);
            s->llvidencdsp.diff_bytes(decorrelated[1], r, g, width);
            g += frame->linesize[0];
            b += frame->linesize[1];
            r += frame->linesize[2];
            decorrelated[0] += decorrelate_linesize;
            decorrelated[1] += decorrelate_linesize;
        }

        for (int i = 0; i < s->planes; i++) {
            Slice *sl = &s->slices[n * s->planes + i];

            s->predict(s, data[i], sl->slice, linesize[i],
                       frame->width, height);
        }
    } else {
        for (int i = 0; i < s->planes; i++) {
            Slice *sl = &s->slices[n * s->planes + i];

            s->predict(s, frame->data[i] + n * (slice_height >> s->vshift[i]) * frame->linesize[i],
                       sl->slice,
                       frame->linesize[i],
                       sl->width, sl->height);
        }
    }

    for (int p = 0; p < s->planes; p++)
        count_plane_slice(avctx, n, p);

    return 0;
}

static int magy_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    MagicYUVContext *s = avctx->priv_data;
    PutByteContext pb;
    int header_size = 32 + (4 + 1) * (s->planes * s->nb_slices + 1)
                         + 256 * s->planes /* Hufftables */;
    int64_t pkt_size = header_size;
    int ret;

    avctx->execute2(avctx, predict_slice, (void *)frame, NULL, s->nb_slices);

    for (int i = 0; i < s->planes; i++)
        generate_codes(avctx, s->he[i], i);

    for (int i = 0; i < s->nb_slices; ++i) {
        for (int j = 0; j < s->planes; ++j) {
            Slice *const sl = &s->slices[i * s->planes + j];
            int64_t size = 0;

            for (size_t k = 0; k < FF_ARRAY_ELEMS(sl->counts); ++k)
                size += sl->counts[k] * s->he[j][k].len;
            size = AV_CEIL_RSHIFT(size, 3);
            sl->encode_raw = size >= sl->width * sl->height;
            if (sl->encode_raw)
                size = sl->width * sl->height;
            sl->size = FFALIGN(size + 2, 4);
            sl->pos  = pkt_size;
            pkt_size += sl->size;
        }
    }

    ret = ff_get_encode_buffer(avctx, pkt, pkt_size, 0);
    if (ret < 0)
        return ret;

    bytestream2_init_writer(&pb, pkt->data, pkt->size);
    bytestream2_put_le32u(&pb, MKTAG('M', 'A', 'G', 'Y'));
    bytestream2_put_le32u(&pb, 32); // header size
    bytestream2_put_byteu(&pb, 7);  // version
    bytestream2_put_byteu(&pb, s->format);
    bytestream2_put_byteu(&pb, 12); // max huffman length
    bytestream2_put_byteu(&pb, 0);

    bytestream2_put_byteu(&pb, 0);
    bytestream2_put_byteu(&pb, 0);
    bytestream2_put_byteu(&pb, 32); // coder type
    bytestream2_put_byteu(&pb, 0);

    bytestream2_put_le32u(&pb, avctx->width);
    bytestream2_put_le32u(&pb, avctx->height);
    bytestream2_put_le32u(&pb, avctx->width);
    bytestream2_put_le32u(&pb, s->slice_height);

    // Slice position is relative to the current position (i.e. 32)
    bytestream2_put_le32u(&pb, header_size - 32);

    for (int i = 0; i < s->planes; ++i) {
        for (int j = 0; j < s->nb_slices; ++j) {
            Slice *const sl = &s->slices[j * s->planes + i];
            bytestream2_put_le32u(&pb, sl->pos - 32);
            sl->dst    = pkt->data + sl->pos;
        }
    }

    bytestream2_put_byteu(&pb, s->planes);

    for (int i = 0; i < s->planes; i++) {
        for (int n = 0; n < s->nb_slices; n++)
            bytestream2_put_byteu(&pb, n * s->planes + i);
    }

    for (int i = 0; i < s->planes; ++i)
        output_codes(&pb, s->he[i]);

    avctx->execute2(avctx, encode_slice, NULL, NULL, s->nb_slices);

    *got_packet = 1;

    return 0;
}

static av_cold int magy_encode_close(AVCodecContext *avctx)
{
    MagicYUVContext *s = avctx->priv_data;

    if (s->slices) {
        for (int i = 0; i < s->planes * s->nb_slices; i++) {
            Slice *sl = &s->slices[i];

            av_freep(&sl->slice);
        }
        av_freep(&s->slices);
    }
    av_freep(&s->decorrelate_buf);

    return 0;
}

#define OFFSET(x) offsetof(MagicYUVContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pred", "Prediction method", OFFSET(frame_pred), AV_OPT_TYPE_INT, {.i64=LEFT}, LEFT, MEDIAN, VE, .unit = "pred" },
    { "left",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = LEFT },     0, 0, VE, .unit = "pred" },
    { "gradient", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = GRADIENT }, 0, 0, VE, .unit = "pred" },
    { "median",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MEDIAN },   0, 0, VE, .unit = "pred" },
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
    CODEC_LONG_NAME("MagicYUV video"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MAGICYUV,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                        AV_CODEC_CAP_SLICE_THREADS |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size   = sizeof(MagicYUVContext),
    .p.priv_class     = &magicyuv_class,
    .init             = magy_encode_init,
    .close            = magy_encode_close,
    FF_CODEC_ENCODE_CB(magy_encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_YUV422P,
                  AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
                  AV_PIX_FMT_GRAY8),
    .color_ranges     = AVCOL_RANGE_MPEG, /* FIXME: implement tagging */
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
