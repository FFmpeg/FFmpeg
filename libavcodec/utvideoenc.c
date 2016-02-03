/*
 * Ut Video encoder
 * Copyright (c) 2012 Jan Ekström
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
 * Ut Video encoder
 */

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "bswapdsp.h"
#include "bytestream.h"
#include "put_bits.h"
#include "huffyuvencdsp.h"
#include "mathops.h"
#include "utvideo.h"
#include "huffman.h"

/* Compare huffentry symbols */
static int huff_cmp_sym(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return aa->sym - bb->sym;
}

static av_cold int utvideo_encode_close(AVCodecContext *avctx)
{
    UtvideoContext *c = avctx->priv_data;
    int i;

    av_freep(&c->slice_bits);
    for (i = 0; i < 4; i++)
        av_freep(&c->slice_buffer[i]);

    return 0;
}

static av_cold int utvideo_encode_init(AVCodecContext *avctx)
{
    UtvideoContext *c = avctx->priv_data;
    int i, subsampled_height;
    uint32_t original_format;

    c->avctx           = avctx;
    c->frame_info_size = 4;
    c->slice_stride    = FFALIGN(avctx->width, 32);

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGB24:
        c->planes        = 3;
        avctx->codec_tag = MKTAG('U', 'L', 'R', 'G');
        original_format  = UTVIDEO_RGB;
        break;
    case AV_PIX_FMT_RGBA:
        c->planes        = 4;
        avctx->codec_tag = MKTAG('U', 'L', 'R', 'A');
        original_format  = UTVIDEO_RGBA;
        break;
    case AV_PIX_FMT_YUV420P:
        if (avctx->width & 1 || avctx->height & 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "4:2:0 video requires even width and height.\n");
            return AVERROR_INVALIDDATA;
        }
        c->planes        = 3;
        if (avctx->colorspace == AVCOL_SPC_BT709)
            avctx->codec_tag = MKTAG('U', 'L', 'H', '0');
        else
            avctx->codec_tag = MKTAG('U', 'L', 'Y', '0');
        original_format  = UTVIDEO_420;
        break;
    case AV_PIX_FMT_YUV422P:
        if (avctx->width & 1) {
            av_log(avctx, AV_LOG_ERROR,
                   "4:2:2 video requires even width.\n");
            return AVERROR_INVALIDDATA;
        }
        c->planes        = 3;
        if (avctx->colorspace == AVCOL_SPC_BT709)
            avctx->codec_tag = MKTAG('U', 'L', 'H', '2');
        else
            avctx->codec_tag = MKTAG('U', 'L', 'Y', '2');
        original_format  = UTVIDEO_422;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown pixel format: %d\n",
               avctx->pix_fmt);
        return AVERROR_INVALIDDATA;
    }

    ff_bswapdsp_init(&c->bdsp);
    ff_huffyuvencdsp_init(&c->hdsp);

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    /* Check the prediction method, and error out if unsupported */
    if (avctx->prediction_method < 0 || avctx->prediction_method > 4) {
        av_log(avctx, AV_LOG_WARNING,
               "Prediction method %d is not supported in Ut Video.\n",
               avctx->prediction_method);
        return AVERROR_OPTION_NOT_FOUND;
    }

    if (avctx->prediction_method == FF_PRED_PLANE) {
        av_log(avctx, AV_LOG_ERROR,
               "Plane prediction is not supported in Ut Video.\n");
        return AVERROR_OPTION_NOT_FOUND;
    }

    /* Convert from libavcodec prediction type to Ut Video's */
    if (avctx->prediction_method)
        c->frame_pred = ff_ut_pred_order[avctx->prediction_method];
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (c->frame_pred == PRED_GRADIENT) {
        av_log(avctx, AV_LOG_ERROR, "Gradient prediction is not supported.\n");
        return AVERROR_OPTION_NOT_FOUND;
    }

    /*
     * Check the asked slice count for obviously invalid
     * values (> 256 or negative).
     */
    if (avctx->slices > 256 || avctx->slices < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Slice count %d is not supported in Ut Video (theoretical range is 0-256).\n",
               avctx->slices);
        return AVERROR(EINVAL);
    }

    /* Check that the slice count is not larger than the subsampled height */
    subsampled_height = avctx->height >> av_pix_fmt_desc_get(avctx->pix_fmt)->log2_chroma_h;
    if (avctx->slices > subsampled_height) {
        av_log(avctx, AV_LOG_ERROR,
               "Slice count %d is larger than the subsampling-applied height %d.\n",
               avctx->slices, subsampled_height);
        return AVERROR(EINVAL);
    }

    /* extradata size is 4 * 32bit */
    avctx->extradata_size = 16;

    avctx->extradata = av_mallocz(avctx->extradata_size +
                                  AV_INPUT_BUFFER_PADDING_SIZE);

    if (!avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate extradata.\n");
        utvideo_encode_close(avctx);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < c->planes; i++) {
        c->slice_buffer[i] = av_malloc(c->slice_stride * (avctx->height + 2) +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
        if (!c->slice_buffer[i]) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer 1.\n");
            utvideo_encode_close(avctx);
            return AVERROR(ENOMEM);
        }
    }

    /*
     * Set the version of the encoder.
     * Last byte is "implementation ID", which is
     * obtained from the creator of the format.
     * Libavcodec has been assigned with the ID 0xF0.
     */
    AV_WB32(avctx->extradata, MKTAG(1, 0, 0, 0xF0));

    /*
     * Set the "original format"
     * Not used for anything during decoding.
     */
    AV_WL32(avctx->extradata + 4, original_format);

    /* Write 4 as the 'frame info size' */
    AV_WL32(avctx->extradata + 8, c->frame_info_size);

    /*
     * Set how many slices are going to be used.
     * By default uses multiple slices depending on the subsampled height.
     * This enables multithreading in the official decoder.
     */
    if (!avctx->slices) {
        c->slices = subsampled_height / 120;

        if (!c->slices)
            c->slices = 1;
        else if (c->slices > 256)
            c->slices = 256;
    } else {
        c->slices = avctx->slices;
    }

    /* Set compression mode */
    c->compression = COMP_HUFF;

    /*
     * Set the encoding flags:
     * - Slice count minus 1
     * - Interlaced encoding mode flag, set to zero for now.
     * - Compression mode (none/huff)
     * And write the flags.
     */
    c->flags  = (c->slices - 1) << 24;
    c->flags |= 0 << 11; // bit field to signal interlaced encoding mode
    c->flags |= c->compression;

    AV_WL32(avctx->extradata + 12, c->flags);

    return 0;
}

static void mangle_rgb_planes(uint8_t *dst[4], int dst_stride, uint8_t *src,
                              int step, int stride, int width, int height)
{
    int i, j;
    int k = 2 * dst_stride;
    unsigned int g;

    for (j = 0; j < height; j++) {
        if (step == 3) {
            for (i = 0; i < width * step; i += step) {
                g         = src[i + 1];
                dst[0][k] = g;
                g        += 0x80;
                dst[1][k] = src[i + 2] - g;
                dst[2][k] = src[i + 0] - g;
                k++;
            }
        } else {
            for (i = 0; i < width * step; i += step) {
                g         = src[i + 1];
                dst[0][k] = g;
                g        += 0x80;
                dst[1][k] = src[i + 2] - g;
                dst[2][k] = src[i + 0] - g;
                dst[3][k] = src[i + 3];
                k++;
            }
        }
        k += dst_stride - width;
        src += stride;
    }
}

/* Write data to a plane with left prediction */
static void left_predict(uint8_t *src, uint8_t *dst, int stride,
                         int width, int height)
{
    int i, j;
    uint8_t prev;

    prev = 0x80; /* Set the initial value */
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            *dst++ = src[i] - prev;
            prev   = src[i];
        }
        src += stride;
    }
}

/* Write data to a plane with median prediction */
static void median_predict(UtvideoContext *c, uint8_t *src, uint8_t *dst, int stride,
                           int width, int height)
{
    int i, j;
    int A, B;
    uint8_t prev;

    /* First line uses left neighbour prediction */
    prev = 0x80; /* Set the initial value */
    for (i = 0; i < width; i++) {
        *dst++ = src[i] - prev;
        prev   = src[i];
    }

    if (height == 1)
        return;

    src += stride;

    /*
     * Second line uses top prediction for the first sample,
     * and median for the rest.
     */
    A = B = 0;

    /* Rest of the coded part uses median prediction */
    for (j = 1; j < height; j++) {
        c->hdsp.sub_hfyu_median_pred(dst, src - stride, src, width, &A, &B);
        dst += width;
        src += stride;
    }
}

/* Count the usage of values in a plane */
static void count_usage(uint8_t *src, int width,
                        int height, uint64_t *counts)
{
    int i, j;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            counts[src[i]]++;
        }
        src += width;
    }
}

/* Calculate the actual huffman codes from the code lengths */
static void calculate_codes(HuffEntry *he)
{
    int last, i;
    uint32_t code;

    qsort(he, 256, sizeof(*he), ff_ut_huff_cmp_len);

    last = 255;
    while (he[last].len == 255 && last)
        last--;

    code = 1;
    for (i = last; i >= 0; i--) {
        he[i].code  = code >> (32 - he[i].len);
        code       += 0x80000000u >> (he[i].len - 1);
    }

    qsort(he, 256, sizeof(*he), huff_cmp_sym);
}

/* Write huffman bit codes to a memory block */
static int write_huff_codes(uint8_t *src, uint8_t *dst, int dst_size,
                            int width, int height, HuffEntry *he)
{
    PutBitContext pb;
    int i, j;
    int count;

    init_put_bits(&pb, dst, dst_size);

    /* Write the codes */
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++)
            put_bits(&pb, he[src[i]].len, he[src[i]].code);

        src += width;
    }

    /* Pad output to a 32bit boundary */
    count = put_bits_count(&pb) & 0x1F;

    if (count)
        put_bits(&pb, 32 - count, 0);

    /* Get the amount of bits written */
    count = put_bits_count(&pb);

    /* Flush the rest with zeroes */
    flush_put_bits(&pb);

    return count;
}

static int encode_plane(AVCodecContext *avctx, uint8_t *src,
                        uint8_t *dst, int stride, int plane_no,
                        int width, int height, PutByteContext *pb)
{
    UtvideoContext *c        = avctx->priv_data;
    uint8_t  lengths[256];
    uint64_t counts[256]     = { 0 };

    HuffEntry he[256];

    uint32_t offset = 0, slice_len = 0;
    const int cmask = ~(!plane_no && avctx->pix_fmt == AV_PIX_FMT_YUV420P);
    int      i, sstart, send = 0;
    int      symbol;
    int      ret;

    /* Do prediction / make planes */
    switch (c->frame_pred) {
    case PRED_NONE:
        for (i = 0; i < c->slices; i++) {
            sstart = send;
            send   = height * (i + 1) / c->slices & cmask;
            av_image_copy_plane(dst + sstart * width, width,
                                src + sstart * stride, stride,
                                width, send - sstart);
        }
        break;
    case PRED_LEFT:
        for (i = 0; i < c->slices; i++) {
            sstart = send;
            send   = height * (i + 1) / c->slices & cmask;
            left_predict(src + sstart * stride, dst + sstart * width,
                         stride, width, send - sstart);
        }
        break;
    case PRED_MEDIAN:
        for (i = 0; i < c->slices; i++) {
            sstart = send;
            send   = height * (i + 1) / c->slices & cmask;
            median_predict(c, src + sstart * stride, dst + sstart * width,
                           stride, width, send - sstart);
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown prediction mode: %d\n",
               c->frame_pred);
        return AVERROR_OPTION_NOT_FOUND;
    }

    /* Count the usage of values */
    count_usage(dst, width, height, counts);

    /* Check for a special case where only one symbol was used */
    for (symbol = 0; symbol < 256; symbol++) {
        /* If non-zero count is found, see if it matches width * height */
        if (counts[symbol]) {
            /* Special case if only one symbol was used */
            if (counts[symbol] == width * (int64_t)height) {
                /*
                 * Write a zero for the single symbol
                 * used in the plane, else 0xFF.
                 */
                for (i = 0; i < 256; i++) {
                    if (i == symbol)
                        bytestream2_put_byte(pb, 0);
                    else
                        bytestream2_put_byte(pb, 0xFF);
                }

                /* Write zeroes for lengths */
                for (i = 0; i < c->slices; i++)
                    bytestream2_put_le32(pb, 0);

                /* And that's all for that plane folks */
                return 0;
            }
            break;
        }
    }

    /* Calculate huffman lengths */
    if ((ret = ff_huff_gen_len_table(lengths, counts, 256, 1)) < 0)
        return ret;

    /*
     * Write the plane's header into the output packet:
     * - huffman code lengths (256 bytes)
     * - slice end offsets (gotten from the slice lengths)
     */
    for (i = 0; i < 256; i++) {
        bytestream2_put_byte(pb, lengths[i]);

        he[i].len = lengths[i];
        he[i].sym = i;
    }

    /* Calculate the huffman codes themselves */
    calculate_codes(he);

    send = 0;
    for (i = 0; i < c->slices; i++) {
        sstart  = send;
        send    = height * (i + 1) / c->slices & cmask;

        /*
         * Write the huffman codes to a buffer,
         * get the offset in bits and convert to bytes.
         */
        offset += write_huff_codes(dst + sstart * width, c->slice_bits,
                                   width * height + 4, width,
                                   send - sstart, he) >> 3;

        slice_len = offset - slice_len;

        /* Byteswap the written huffman codes */
        c->bdsp.bswap_buf((uint32_t *) c->slice_bits,
                          (uint32_t *) c->slice_bits,
                          slice_len >> 2);

        /* Write the offset to the stream */
        bytestream2_put_le32(pb, offset);

        /* Seek to the data part of the packet */
        bytestream2_seek_p(pb, 4 * (c->slices - i - 1) +
                           offset - slice_len, SEEK_CUR);

        /* Write the slices' data into the output packet */
        bytestream2_put_buffer(pb, c->slice_bits, slice_len);

        /* Seek back to the slice offsets */
        bytestream2_seek_p(pb, -4 * (c->slices - i - 1) - offset,
                           SEEK_CUR);

        slice_len = offset;
    }

    /* And at the end seek to the end of written slice(s) */
    bytestream2_seek_p(pb, offset, SEEK_CUR);

    return 0;
}

static int utvideo_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pic, int *got_packet)
{
    UtvideoContext *c = avctx->priv_data;
    PutByteContext pb;

    uint32_t frame_info;

    uint8_t *dst;

    int width = avctx->width, height = avctx->height;
    int i, ret = 0;

    /* Allocate a new packet if needed, and set it to the pointer dst */
    ret = ff_alloc_packet2(avctx, pkt, (256 + 4 * c->slices + width * height) *
                           c->planes + 4, 0);

    if (ret < 0)
        return ret;

    dst = pkt->data;

    bytestream2_init_writer(&pb, dst, pkt->size);

    av_fast_padded_malloc(&c->slice_bits, &c->slice_bits_size, width * height + 4);

    if (!c->slice_bits) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer 2.\n");
        return AVERROR(ENOMEM);
    }

    /* In case of RGB, mangle the planes to Ut Video's format */
    if (avctx->pix_fmt == AV_PIX_FMT_RGBA || avctx->pix_fmt == AV_PIX_FMT_RGB24)
        mangle_rgb_planes(c->slice_buffer, c->slice_stride, pic->data[0],
                          c->planes, pic->linesize[0], width, height);

    /* Deal with the planes */
    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
        for (i = 0; i < c->planes; i++) {
            ret = encode_plane(avctx, c->slice_buffer[i] + 2 * c->slice_stride,
                               c->slice_buffer[i], c->slice_stride, i,
                               width, height, &pb);

            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "Error encoding plane %d.\n", i);
                return ret;
            }
        }
        break;
    case AV_PIX_FMT_YUV422P:
        for (i = 0; i < c->planes; i++) {
            ret = encode_plane(avctx, pic->data[i], c->slice_buffer[0],
                               pic->linesize[i], i, width >> !!i, height, &pb);

            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "Error encoding plane %d.\n", i);
                return ret;
            }
        }
        break;
    case AV_PIX_FMT_YUV420P:
        for (i = 0; i < c->planes; i++) {
            ret = encode_plane(avctx, pic->data[i], c->slice_buffer[0],
                               pic->linesize[i], i, width >> !!i, height >> !!i,
                               &pb);

            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "Error encoding plane %d.\n", i);
                return ret;
            }
        }
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown pixel format: %d\n",
               avctx->pix_fmt);
        return AVERROR_INVALIDDATA;
    }

    /*
     * Write frame information (LE 32bit unsigned)
     * into the output packet.
     * Contains the prediction method.
     */
    frame_info = c->frame_pred << 8;
    bytestream2_put_le32(&pb, frame_info);

    /*
     * At least currently Ut Video is IDR only.
     * Set flags accordingly.
     */
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->key_frame = 1;
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    pkt->size   = bytestream2_tell_p(&pb);
    pkt->flags |= AV_PKT_FLAG_KEY;

    /* Packet should be done */
    *got_packet = 1;

    return 0;
}

#define OFFSET(x) offsetof(UtvideoContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
{ "pred", "Prediction method", OFFSET(frame_pred), AV_OPT_TYPE_INT, { .i64 = PRED_LEFT }, PRED_NONE, PRED_MEDIAN, VE, "pred" },
    { "none",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRED_NONE }, INT_MIN, INT_MAX, VE, "pred" },
    { "left",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRED_LEFT }, INT_MIN, INT_MAX, VE, "pred" },
    { "gradient", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRED_GRADIENT }, INT_MIN, INT_MAX, VE, "pred" },
    { "median",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRED_MEDIAN }, INT_MIN, INT_MAX, VE, "pred" },

    { NULL},
};

static const AVClass utvideo_class = {
    .class_name = "utvideo",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_utvideo_encoder = {
    .name           = "utvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Ut Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_UTVIDEO,
    .priv_data_size = sizeof(UtvideoContext),
    .priv_class     = &utvideo_class,
    .init           = utvideo_encode_init,
    .encode2        = utvideo_encode_frame,
    .close          = utvideo_encode_close,
    .capabilities   = AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]) {
                          AV_PIX_FMT_RGB24, AV_PIX_FMT_RGBA, AV_PIX_FMT_YUV422P,
                          AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
                      },
};
