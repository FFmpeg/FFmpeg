/*
 * Ut Video decoder
 * Copyright (c) 2011 Konstantin Shishkov
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
 * Ut Video decoder
 */

#include <inttypes.h>
#include <stdlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bswapdsp.h"
#include "bytestream.h"
#include "get_bits.h"
#include "thread.h"
#include "utvideo.h"

static int build_huff(const uint8_t *src, VLC *vlc, int *fsym)
{
    int i;
    HuffEntry he[256];
    int last;
    uint32_t codes[256];
    uint8_t bits[256];
    uint8_t syms[256];
    uint32_t code;

    *fsym = -1;
    for (i = 0; i < 256; i++) {
        he[i].sym = i;
        he[i].len = *src++;
    }
    qsort(he, 256, sizeof(*he), ff_ut_huff_cmp_len);

    if (!he[0].len) {
        *fsym = he[0].sym;
        return 0;
    }

    last = 255;
    while (he[last].len == 255 && last)
        last--;

    if (he[last].len > 32)
        return -1;

    code = 1;
    for (i = last; i >= 0; i--) {
        codes[i] = code >> (32 - he[i].len);
        bits[i]  = he[i].len;
        syms[i]  = he[i].sym;
        code += 0x80000000u >> (he[i].len - 1);
    }

    return ff_init_vlc_sparse(vlc, FFMIN(he[last].len, 11), last + 1,
                              bits,  sizeof(*bits),  sizeof(*bits),
                              codes, sizeof(*codes), sizeof(*codes),
                              syms,  sizeof(*syms),  sizeof(*syms), 0);
}

static int decode_plane(UtvideoContext *c, int plane_no,
                        uint8_t *dst, int step, int stride,
                        int width, int height,
                        const uint8_t *src, int use_pred)
{
    int i, j, slice, pix;
    int sstart, send;
    VLC vlc;
    GetBitContext gb;
    int prev, fsym;
    const int cmask = ~(!plane_no && c->avctx->pix_fmt == AV_PIX_FMT_YUV420P);

    if (build_huff(src, &vlc, &fsym)) {
        av_log(c->avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
        return AVERROR_INVALIDDATA;
    }
    if (fsym >= 0) { // build_huff reported a symbol to fill slices with
        send = 0;
        for (slice = 0; slice < c->slices; slice++) {
            uint8_t *dest;

            sstart = send;
            send   = (height * (slice + 1) / c->slices) & cmask;
            dest   = dst + sstart * stride;

            prev = 0x80;
            for (j = sstart; j < send; j++) {
                for (i = 0; i < width * step; i += step) {
                    pix = fsym;
                    if (use_pred) {
                        prev += pix;
                        pix   = prev;
                    }
                    dest[i] = pix;
                }
                dest += stride;
            }
        }
        return 0;
    }

    src      += 256;

    send = 0;
    for (slice = 0; slice < c->slices; slice++) {
        uint8_t *dest;
        int slice_data_start, slice_data_end, slice_size;

        sstart = send;
        send   = (height * (slice + 1) / c->slices) & cmask;
        dest   = dst + sstart * stride;

        // slice offset and size validation was done earlier
        slice_data_start = slice ? AV_RL32(src + slice * 4 - 4) : 0;
        slice_data_end   = AV_RL32(src + slice * 4);
        slice_size       = slice_data_end - slice_data_start;

        if (!slice_size) {
            av_log(c->avctx, AV_LOG_ERROR, "Plane has more than one symbol "
                   "yet a slice has a length of zero.\n");
            goto fail;
        }

        memcpy(c->slice_bits, src + slice_data_start + c->slices * 4,
               slice_size);
        memset(c->slice_bits + slice_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        c->bdsp.bswap_buf((uint32_t *) c->slice_bits,
                          (uint32_t *) c->slice_bits,
                          (slice_data_end - slice_data_start + 3) >> 2);
        init_get_bits(&gb, c->slice_bits, slice_size * 8);

        prev = 0x80;
        for (j = sstart; j < send; j++) {
            for (i = 0; i < width * step; i += step) {
                if (get_bits_left(&gb) <= 0) {
                    av_log(c->avctx, AV_LOG_ERROR,
                           "Slice decoding ran out of bits\n");
                    goto fail;
                }
                pix = get_vlc2(&gb, vlc.table, vlc.bits, 3);
                if (pix < 0) {
                    av_log(c->avctx, AV_LOG_ERROR, "Decoding error\n");
                    goto fail;
                }
                if (use_pred) {
                    prev += pix;
                    pix   = prev;
                }
                dest[i] = pix;
            }
            dest += stride;
        }
        if (get_bits_left(&gb) > 32)
            av_log(c->avctx, AV_LOG_WARNING,
                   "%d bits left after decoding slice\n", get_bits_left(&gb));
    }

    ff_free_vlc(&vlc);

    return 0;
fail:
    ff_free_vlc(&vlc);
    return AVERROR_INVALIDDATA;
}

static void restore_rgb_planes(uint8_t *src, int step, int stride, int width,
                               int height)
{
    int i, j;
    uint8_t r, g, b;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width * step; i += step) {
            r = src[i];
            g = src[i + 1];
            b = src[i + 2];
            src[i]     = r + g - 0x80;
            src[i + 2] = b + g - 0x80;
        }
        src += stride;
    }
}

static void restore_median(uint8_t *src, int step, int stride,
                           int width, int height, int slices, int rmode)
{
    int i, j, slice;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;
    const int cmask = ~rmode;

    for (slice = 0; slice < slices; slice++) {
        slice_start  = ((slice * height) / slices) & cmask;
        slice_height = ((((slice + 1) * height) / slices) & cmask) -
                       slice_start;

        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        A = bsrc[0];
        for (i = step; i < width * step; i += step) {
            bsrc[i] += A;
            A        = bsrc[i];
        }
        bsrc += stride;
        if (slice_height <= 1)
            continue;
        // second line - first element has top prediction, the rest uses median
        C        = bsrc[-stride];
        bsrc[0] += C;
        A        = bsrc[0];
        for (i = step; i < width * step; i += step) {
            B        = bsrc[i - stride];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C        = B;
            A        = bsrc[i];
        }
        bsrc += stride;
        // the rest of lines use continuous median prediction
        for (j = 2; j < slice_height; j++) {
            for (i = 0; i < width * step; i += step) {
                B        = bsrc[i - stride];
                bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
                C        = B;
                A        = bsrc[i];
            }
            bsrc += stride;
        }
    }
}

/* UtVideo interlaced mode treats every two lines as a single one,
 * so restoring function should take care of possible padding between
 * two parts of the same "line".
 */
static void restore_median_il(uint8_t *src, int step, int stride,
                              int width, int height, int slices, int rmode)
{
    int i, j, slice;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;
    const int cmask   = ~(rmode ? 3 : 1);
    const int stride2 = stride << 1;

    for (slice = 0; slice < slices; slice++) {
        slice_start    = ((slice * height) / slices) & cmask;
        slice_height   = ((((slice + 1) * height) / slices) & cmask) -
                         slice_start;
        slice_height >>= 1;

        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        A        = bsrc[0];
        for (i = step; i < width * step; i += step) {
            bsrc[i] += A;
            A        = bsrc[i];
        }
        for (i = 0; i < width * step; i += step) {
            bsrc[stride + i] += A;
            A                 = bsrc[stride + i];
        }
        bsrc += stride2;
        if (slice_height <= 1)
            continue;
        // second line - first element has top prediction, the rest uses median
        C        = bsrc[-stride2];
        bsrc[0] += C;
        A        = bsrc[0];
        for (i = step; i < width * step; i += step) {
            B        = bsrc[i - stride2];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C        = B;
            A        = bsrc[i];
        }
        for (i = 0; i < width * step; i += step) {
            B                 = bsrc[i - stride];
            bsrc[stride + i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C                 = B;
            A                 = bsrc[stride + i];
        }
        bsrc += stride2;
        // the rest of lines use continuous median prediction
        for (j = 2; j < slice_height; j++) {
            for (i = 0; i < width * step; i += step) {
                B        = bsrc[i - stride2];
                bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
                C        = B;
                A        = bsrc[i];
            }
            for (i = 0; i < width * step; i += step) {
                B                 = bsrc[i - stride];
                bsrc[i + stride] += mid_pred(A, B, (uint8_t)(A + B - C));
                C                 = B;
                A                 = bsrc[i + stride];
            }
            bsrc += stride2;
        }
    }
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    UtvideoContext *c = avctx->priv_data;
    int i, j;
    const uint8_t *plane_start[5];
    int plane_size, max_slice_size = 0, slice_start, slice_end, slice_size;
    int ret;
    GetByteContext gb;
    ThreadFrame frame = { .f = data };

    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    /* parse plane structure to get frame flags and validate slice offsets */
    bytestream2_init(&gb, buf, buf_size);
    for (i = 0; i < c->planes; i++) {
        plane_start[i] = gb.buffer;
        if (bytestream2_get_bytes_left(&gb) < 256 + 4 * c->slices) {
            av_log(avctx, AV_LOG_ERROR, "Insufficient data for a plane\n");
            return AVERROR_INVALIDDATA;
        }
        bytestream2_skipu(&gb, 256);
        slice_start = 0;
        slice_end   = 0;
        for (j = 0; j < c->slices; j++) {
            slice_end   = bytestream2_get_le32u(&gb);
            slice_size  = slice_end - slice_start;
            if (slice_end < 0 || slice_size < 0 ||
                bytestream2_get_bytes_left(&gb) < slice_end) {
                av_log(avctx, AV_LOG_ERROR, "Incorrect slice size\n");
                return AVERROR_INVALIDDATA;
            }
            slice_start = slice_end;
            max_slice_size = FFMAX(max_slice_size, slice_size);
        }
        plane_size = slice_end;
        bytestream2_skipu(&gb, plane_size);
    }
    plane_start[c->planes] = gb.buffer;
    if (bytestream2_get_bytes_left(&gb) < c->frame_info_size) {
        av_log(avctx, AV_LOG_ERROR, "Not enough data for frame information\n");
        return AVERROR_INVALIDDATA;
    }
    c->frame_info = bytestream2_get_le32u(&gb);
    av_log(avctx, AV_LOG_DEBUG, "frame information flags %"PRIX32"\n",
           c->frame_info);

    c->frame_pred = (c->frame_info >> 8) & 3;

    if (c->frame_pred == PRED_GRADIENT) {
        avpriv_request_sample(avctx, "Frame with gradient prediction");
        return AVERROR_PATCHWELCOME;
    }

    av_fast_malloc(&c->slice_bits, &c->slice_bits_size,
                   max_slice_size + FF_INPUT_BUFFER_PADDING_SIZE);

    if (!c->slice_bits) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer\n");
        return AVERROR(ENOMEM);
    }

    switch (c->avctx->pix_fmt) {
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_RGBA:
        for (i = 0; i < c->planes; i++) {
            ret = decode_plane(c, i, frame.f->data[0] + ff_ut_rgb_order[i],
                               c->planes, frame.f->linesize[0], avctx->width,
                               avctx->height, plane_start[i],
                               c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median(frame.f->data[0] + ff_ut_rgb_order[i],
                                   c->planes, frame.f->linesize[0], avctx->width,
                                   avctx->height, c->slices, 0);
                } else {
                    restore_median_il(frame.f->data[0] + ff_ut_rgb_order[i],
                                      c->planes, frame.f->linesize[0],
                                      avctx->width, avctx->height, c->slices,
                                      0);
                }
            }
        }
        restore_rgb_planes(frame.f->data[0], c->planes, frame.f->linesize[0],
                           avctx->width, avctx->height);
        break;
    case AV_PIX_FMT_YUV420P:
        for (i = 0; i < 3; i++) {
            ret = decode_plane(c, i, frame.f->data[i], 1, frame.f->linesize[i],
                               avctx->width >> !!i, avctx->height >> !!i,
                               plane_start[i], c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median(frame.f->data[i], 1, frame.f->linesize[i],
                                   avctx->width >> !!i, avctx->height >> !!i,
                                   c->slices, !i);
                } else {
                    restore_median_il(frame.f->data[i], 1, frame.f->linesize[i],
                                      avctx->width  >> !!i,
                                      avctx->height >> !!i,
                                      c->slices, !i);
                }
            }
        }
        break;
    case AV_PIX_FMT_YUV422P:
        for (i = 0; i < 3; i++) {
            ret = decode_plane(c, i, frame.f->data[i], 1, frame.f->linesize[i],
                               avctx->width >> !!i, avctx->height,
                               plane_start[i], c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median(frame.f->data[i], 1, frame.f->linesize[i],
                                   avctx->width >> !!i, avctx->height,
                                   c->slices, 0);
                } else {
                    restore_median_il(frame.f->data[i], 1, frame.f->linesize[i],
                                      avctx->width >> !!i, avctx->height,
                                      c->slices, 0);
                }
            }
        }
        break;
    }

    frame.f->key_frame = 1;
    frame.f->pict_type = AV_PICTURE_TYPE_I;
    frame.f->interlaced_frame = !!c->interlaced;

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    UtvideoContext * const c = avctx->priv_data;

    c->avctx = avctx;

    ff_bswapdsp_init(&c->bdsp);

    if (avctx->extradata_size < 16) {
        av_log(avctx, AV_LOG_ERROR,
               "Insufficient extradata size %d, should be at least 16\n",
               avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    av_log(avctx, AV_LOG_DEBUG, "Encoder version %d.%d.%d.%d\n",
           avctx->extradata[3], avctx->extradata[2],
           avctx->extradata[1], avctx->extradata[0]);
    av_log(avctx, AV_LOG_DEBUG, "Original format %"PRIX32"\n",
           AV_RB32(avctx->extradata + 4));
    c->frame_info_size = AV_RL32(avctx->extradata + 8);
    c->flags           = AV_RL32(avctx->extradata + 12);

    if (c->frame_info_size != 4)
        avpriv_request_sample(avctx, "Frame info not 4 bytes");
    av_log(avctx, AV_LOG_DEBUG, "Encoding parameters %08"PRIX32"\n", c->flags);
    c->slices      = (c->flags >> 24) + 1;
    c->compression = c->flags & 1;
    c->interlaced  = c->flags & 0x800;

    c->slice_bits_size = 0;

    switch (avctx->codec_tag) {
    case MKTAG('U', 'L', 'R', 'G'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        break;
    case MKTAG('U', 'L', 'R', 'A'):
        c->planes      = 4;
        avctx->pix_fmt = AV_PIX_FMT_RGBA;
        break;
    case MKTAG('U', 'L', 'Y', '0'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        avctx->colorspace = AVCOL_SPC_BT470BG;
        break;
    case MKTAG('U', 'L', 'Y', '2'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        avctx->colorspace = AVCOL_SPC_BT470BG;
        break;
    case MKTAG('U', 'L', 'H', '0'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        avctx->colorspace = AVCOL_SPC_BT709;
        break;
    case MKTAG('U', 'L', 'H', '2'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        avctx->colorspace = AVCOL_SPC_BT709;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown Ut Video FOURCC provided (%08X)\n",
               avctx->codec_tag);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    UtvideoContext * const c = avctx->priv_data;

    av_freep(&c->slice_bits);

    return 0;
}

AVCodec ff_utvideo_decoder = {
    .name           = "utvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Ut Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_UTVIDEO,
    .priv_data_size = sizeof(UtvideoContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_FRAME_THREADS,
};
