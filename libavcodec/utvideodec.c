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

#define CACHED_BITSTREAM_READER !ARCH_X86_32
#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bswapdsp.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "thread.h"
#include "utvideo.h"

typedef struct HuffEntry {
    uint8_t len;
    uint16_t sym;
} HuffEntry;

static int build_huff(UtvideoContext *c, const uint8_t *src, VLC *vlc,
                      int *fsym, unsigned nb_elems)
{
    int i;
    HuffEntry he[1024];
    uint8_t bits[1024];
    uint16_t codes_count[33] = { 0 };

    *fsym = -1;
    for (i = 0; i < nb_elems; i++) {
        if (src[i] == 0) {
            *fsym = i;
            return 0;
        } else if (src[i] == 255) {
            bits[i] = 0;
        } else if (src[i] <= 32) {
            bits[i] = src[i];
        } else
            return AVERROR_INVALIDDATA;

        codes_count[bits[i]]++;
    }
    if (codes_count[0] == nb_elems)
        return AVERROR_INVALIDDATA;

    /* For Ut Video, longer codes are to the left of the tree and
     * for codes with the same length the symbol is descending from
     * left to right. So after the next loop --codes_count[i] will
     * be the index of the first (lowest) symbol of length i when
     * indexed by the position in the tree with left nodes being first. */
    for (int i = 31; i >= 0; i--)
        codes_count[i] += codes_count[i + 1];

    for (unsigned i = 0; i < nb_elems; i++)
        he[--codes_count[bits[i]]] = (HuffEntry) { bits[i], i };

#define VLC_BITS 11
    return ff_init_vlc_from_lengths(vlc, VLC_BITS, codes_count[0],
                                    &he[0].len, sizeof(*he),
                                    &he[0].sym, sizeof(*he), 2, 0, 0, c->avctx);
}

static int decode_plane10(UtvideoContext *c, int plane_no,
                          uint16_t *dst, ptrdiff_t stride,
                          int width, int height,
                          const uint8_t *src, const uint8_t *huff,
                          int use_pred)
{
    int i, j, slice, pix, ret;
    int sstart, send;
    VLC vlc;
    GetBitContext gb;
    int prev, fsym;

    if ((ret = build_huff(c, huff, &vlc, &fsym, 1024)) < 0) {
        av_log(c->avctx, AV_LOG_ERROR, "Cannot build Huffman codes\n");
        return ret;
    }
    if (fsym >= 0) { // build_huff reported a symbol to fill slices with
        send = 0;
        for (slice = 0; slice < c->slices; slice++) {
            uint16_t *dest;

            sstart = send;
            send   = (height * (slice + 1) / c->slices);
            dest   = dst + sstart * stride;

            prev = 0x200;
            for (j = sstart; j < send; j++) {
                for (i = 0; i < width; i++) {
                    pix = fsym;
                    if (use_pred) {
                        prev += pix;
                        prev &= 0x3FF;
                        pix   = prev;
                    }
                    dest[i] = pix;
                }
                dest += stride;
            }
        }
        return 0;
    }

    send = 0;
    for (slice = 0; slice < c->slices; slice++) {
        uint16_t *dest;
        int slice_data_start, slice_data_end, slice_size;

        sstart = send;
        send   = (height * (slice + 1) / c->slices);
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

        memset(c->slice_bits + slice_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        c->bdsp.bswap_buf((uint32_t *) c->slice_bits,
                          (uint32_t *)(src + slice_data_start + c->slices * 4),
                          (slice_data_end - slice_data_start + 3) >> 2);
        init_get_bits(&gb, c->slice_bits, slice_size * 8);

        prev = 0x200;
        for (j = sstart; j < send; j++) {
            for (i = 0; i < width; i++) {
                pix = get_vlc2(&gb, vlc.table, VLC_BITS, 3);
                if (pix < 0) {
                    av_log(c->avctx, AV_LOG_ERROR, "Decoding error\n");
                    goto fail;
                }
                if (use_pred) {
                    prev += pix;
                    prev &= 0x3FF;
                    pix   = prev;
                }
                dest[i] = pix;
            }
            dest += stride;
            if (get_bits_left(&gb) < 0) {
                av_log(c->avctx, AV_LOG_ERROR,
                        "Slice decoding ran out of bits\n");
                goto fail;
            }
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

static int compute_cmask(int plane_no, int interlaced, enum AVPixelFormat pix_fmt)
{
    const int is_luma = (pix_fmt == AV_PIX_FMT_YUV420P) && !plane_no;

    if (interlaced)
        return ~(1 + 2 * is_luma);

    return ~is_luma;
}

static int decode_plane(UtvideoContext *c, int plane_no,
                        uint8_t *dst, ptrdiff_t stride,
                        int width, int height,
                        const uint8_t *src, int use_pred)
{
    int i, j, slice, pix;
    int sstart, send;
    VLC vlc;
    GetBitContext gb;
    int ret, prev, fsym;
    const int cmask = compute_cmask(plane_no, c->interlaced, c->avctx->pix_fmt);

    if (c->pack) {
        send = 0;
        for (slice = 0; slice < c->slices; slice++) {
            GetBitContext cbit, pbit;
            uint8_t *dest, *p;

            ret = init_get_bits8_le(&cbit, c->control_stream[plane_no][slice], c->control_stream_size[plane_no][slice]);
            if (ret < 0)
                return ret;

            ret = init_get_bits8_le(&pbit, c->packed_stream[plane_no][slice], c->packed_stream_size[plane_no][slice]);
            if (ret < 0)
                return ret;

            sstart = send;
            send   = (height * (slice + 1) / c->slices) & cmask;
            dest   = dst + sstart * stride;

            if (3 * ((dst + send * stride - dest + 7)/8) > get_bits_left(&cbit))
                return AVERROR_INVALIDDATA;

            for (p = dest; p < dst + send * stride; p += 8) {
                int bits = get_bits_le(&cbit, 3);

                if (bits == 0) {
                    *(uint64_t *) p = 0;
                } else {
                    uint32_t sub = 0x80 >> (8 - (bits + 1)), add;
                    int k;

                    if ((bits + 1) * 8 > get_bits_left(&pbit))
                        return AVERROR_INVALIDDATA;

                    for (k = 0; k < 8; k++) {

                        p[k] = get_bits_le(&pbit, bits + 1);
                        add = (~p[k] & sub) << (8 - bits);
                        p[k] -= sub;
                        p[k] += add;
                    }
                }
            }
        }

        return 0;
    }

    if (build_huff(c, src, &vlc, &fsym, 256)) {
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
                for (i = 0; i < width; i++) {
                    pix = fsym;
                    if (use_pred) {
                        prev += (unsigned)pix;
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

        memset(c->slice_bits + slice_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        c->bdsp.bswap_buf((uint32_t *) c->slice_bits,
                          (uint32_t *)(src + slice_data_start + c->slices * 4),
                          (slice_data_end - slice_data_start + 3) >> 2);
        init_get_bits(&gb, c->slice_bits, slice_size * 8);

        prev = 0x80;
        for (j = sstart; j < send; j++) {
            for (i = 0; i < width; i++) {
                pix = get_vlc2(&gb, vlc.table, VLC_BITS, 3);
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
            if (get_bits_left(&gb) < 0) {
                av_log(c->avctx, AV_LOG_ERROR,
                        "Slice decoding ran out of bits\n");
                goto fail;
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

#undef A
#undef B
#undef C

static void restore_median_planar(UtvideoContext *c, uint8_t *src, ptrdiff_t stride,
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

        if (!slice_height)
            continue;
        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        c->llviddsp.add_left_pred(bsrc, bsrc, width, 0);
        bsrc += stride;
        if (slice_height <= 1)
            continue;
        // second line - first element has top prediction, the rest uses median
        C        = bsrc[-stride];
        bsrc[0] += C;
        A        = bsrc[0];
        for (i = 1; i < FFMIN(width, 16); i++) { /* scalar loop (DSP need align 16) */
            B        = bsrc[i - stride];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C        = B;
            A        = bsrc[i];
        }
        if (width > 16)
            c->llviddsp.add_median_pred(bsrc + 16, bsrc - stride + 16,
                                        bsrc + 16, width - 16, &A, &B);

        bsrc += stride;
        // the rest of lines use continuous median prediction
        for (j = 2; j < slice_height; j++) {
            c->llviddsp.add_median_pred(bsrc, bsrc - stride,
                                            bsrc, width, &A, &B);
            bsrc += stride;
        }
    }
}

/* UtVideo interlaced mode treats every two lines as a single one,
 * so restoring function should take care of possible padding between
 * two parts of the same "line".
 */
static void restore_median_planar_il(UtvideoContext *c, uint8_t *src, ptrdiff_t stride,
                                     int width, int height, int slices, int rmode)
{
    int i, j, slice;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;
    const int cmask   = ~(rmode ? 3 : 1);
    const ptrdiff_t stride2 = stride << 1;

    for (slice = 0; slice < slices; slice++) {
        slice_start    = ((slice * height) / slices) & cmask;
        slice_height   = ((((slice + 1) * height) / slices) & cmask) -
                         slice_start;
        slice_height >>= 1;
        if (!slice_height)
            continue;

        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        A = c->llviddsp.add_left_pred(bsrc, bsrc, width, 0);
        c->llviddsp.add_left_pred(bsrc + stride, bsrc + stride, width, A);
        bsrc += stride2;
        if (slice_height <= 1)
            continue;
        // second line - first element has top prediction, the rest uses median
        C        = bsrc[-stride2];
        bsrc[0] += C;
        A        = bsrc[0];
        for (i = 1; i < FFMIN(width, 16); i++) { /* scalar loop (DSP need align 16) */
            B        = bsrc[i - stride2];
            bsrc[i] += mid_pred(A, B, (uint8_t)(A + B - C));
            C        = B;
            A        = bsrc[i];
        }
        if (width > 16)
            c->llviddsp.add_median_pred(bsrc + 16, bsrc - stride2 + 16,
                                        bsrc + 16, width - 16, &A, &B);

        c->llviddsp.add_median_pred(bsrc + stride, bsrc - stride,
                                        bsrc + stride, width, &A, &B);
        bsrc += stride2;
        // the rest of lines use continuous median prediction
        for (j = 2; j < slice_height; j++) {
            c->llviddsp.add_median_pred(bsrc, bsrc - stride2,
                                            bsrc, width, &A, &B);
            c->llviddsp.add_median_pred(bsrc + stride, bsrc - stride,
                                            bsrc + stride, width, &A, &B);
            bsrc += stride2;
        }
    }
}

static void restore_gradient_planar(UtvideoContext *c, uint8_t *src, ptrdiff_t stride,
                                    int width, int height, int slices, int rmode)
{
    int i, j, slice;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;
    const int cmask = ~rmode;
    int min_width = FFMIN(width, 32);

    for (slice = 0; slice < slices; slice++) {
        slice_start  = ((slice * height) / slices) & cmask;
        slice_height = ((((slice + 1) * height) / slices) & cmask) -
                       slice_start;

        if (!slice_height)
            continue;
        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        c->llviddsp.add_left_pred(bsrc, bsrc, width, 0);
        bsrc += stride;
        if (slice_height <= 1)
            continue;
        for (j = 1; j < slice_height; j++) {
            // second line - first element has top prediction, the rest uses gradient
            bsrc[0] = (bsrc[0] + bsrc[-stride]) & 0xFF;
            for (i = 1; i < min_width; i++) { /* dsp need align 32 */
                A = bsrc[i - stride];
                B = bsrc[i - (stride + 1)];
                C = bsrc[i - 1];
                bsrc[i] = (A - B + C + bsrc[i]) & 0xFF;
            }
            if (width > 32)
                c->llviddsp.add_gradient_pred(bsrc + 32, stride, width - 32);
            bsrc += stride;
        }
    }
}

static void restore_gradient_planar_il(UtvideoContext *c, uint8_t *src, ptrdiff_t stride,
                                      int width, int height, int slices, int rmode)
{
    int i, j, slice;
    int A, B, C;
    uint8_t *bsrc;
    int slice_start, slice_height;
    const int cmask   = ~(rmode ? 3 : 1);
    const ptrdiff_t stride2 = stride << 1;
    int min_width = FFMIN(width, 32);

    for (slice = 0; slice < slices; slice++) {
        slice_start    = ((slice * height) / slices) & cmask;
        slice_height   = ((((slice + 1) * height) / slices) & cmask) -
                         slice_start;
        slice_height >>= 1;
        if (!slice_height)
            continue;

        bsrc = src + slice_start * stride;

        // first line - left neighbour prediction
        bsrc[0] += 0x80;
        A = c->llviddsp.add_left_pred(bsrc, bsrc, width, 0);
        c->llviddsp.add_left_pred(bsrc + stride, bsrc + stride, width, A);
        bsrc += stride2;
        if (slice_height <= 1)
            continue;
        for (j = 1; j < slice_height; j++) {
            // second line - first element has top prediction, the rest uses gradient
            bsrc[0] = (bsrc[0] + bsrc[-stride2]) & 0xFF;
            for (i = 1; i < min_width; i++) { /* dsp need align 32 */
                A = bsrc[i - stride2];
                B = bsrc[i - (stride2 + 1)];
                C = bsrc[i - 1];
                bsrc[i] = (A - B + C + bsrc[i]) & 0xFF;
            }
            if (width > 32)
                c->llviddsp.add_gradient_pred(bsrc + 32, stride2, width - 32);

            A = bsrc[-stride];
            B = bsrc[-(1 + stride + stride - width)];
            C = bsrc[width - 1];
            bsrc[stride] = (A - B + C + bsrc[stride]) & 0xFF;
            for (i = 1; i < width; i++) {
                A = bsrc[i - stride];
                B = bsrc[i - (1 + stride)];
                C = bsrc[i - 1 + stride];
                bsrc[i + stride] = (A - B + C + bsrc[i + stride]) & 0xFF;
            }
            bsrc += stride2;
        }
    }
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    UtvideoContext *c = avctx->priv_data;
    int i, j;
    const uint8_t *plane_start[5];
    int plane_size, max_slice_size = 0, slice_start, slice_end, slice_size;
    int ret;
    GetByteContext gb;

    if ((ret = ff_thread_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    /* parse plane structure to get frame flags and validate slice offsets */
    bytestream2_init(&gb, buf, buf_size);

    if (c->pack) {
        const uint8_t *packed_stream;
        const uint8_t *control_stream;
        GetByteContext pb;
        uint32_t nb_cbs;
        int left;

        c->frame_info = PRED_GRADIENT << 8;

        if (bytestream2_get_byte(&gb) != 1)
            return AVERROR_INVALIDDATA;
        bytestream2_skip(&gb, 3);
        c->offset = bytestream2_get_le32(&gb);

        if (buf_size <= c->offset + 8LL)
            return AVERROR_INVALIDDATA;

        bytestream2_init(&pb, buf + 8 + c->offset, buf_size - 8 - c->offset);

        nb_cbs = bytestream2_get_le32(&pb);
        if (nb_cbs > c->offset)
            return AVERROR_INVALIDDATA;

        packed_stream = buf + 8;
        control_stream = packed_stream + (c->offset - nb_cbs);
        left = control_stream - packed_stream;

        for (i = 0; i < c->planes; i++) {
            for (j = 0; j < c->slices; j++) {
                c->packed_stream[i][j] = packed_stream;
                c->packed_stream_size[i][j] = bytestream2_get_le32(&pb);
                if (c->packed_stream_size[i][j] > left)
                    return AVERROR_INVALIDDATA;
                left -= c->packed_stream_size[i][j];
                packed_stream += c->packed_stream_size[i][j];
            }
        }

        left = buf + buf_size - control_stream;

        for (i = 0; i < c->planes; i++) {
            for (j = 0; j < c->slices; j++) {
                c->control_stream[i][j] = control_stream;
                c->control_stream_size[i][j] = bytestream2_get_le32(&pb);
                if (c->control_stream_size[i][j] > left)
                    return AVERROR_INVALIDDATA;
                left -= c->control_stream_size[i][j];
                control_stream += c->control_stream_size[i][j];
            }
        }
    } else if (c->pro) {
        if (bytestream2_get_bytes_left(&gb) < c->frame_info_size) {
            av_log(avctx, AV_LOG_ERROR, "Not enough data for frame information\n");
            return AVERROR_INVALIDDATA;
        }
        c->frame_info = bytestream2_get_le32u(&gb);
        c->slices = ((c->frame_info >> 16) & 0xff) + 1;
        for (i = 0; i < c->planes; i++) {
            plane_start[i] = gb.buffer;
            if (bytestream2_get_bytes_left(&gb) < 1024 + 4 * c->slices) {
                av_log(avctx, AV_LOG_ERROR, "Insufficient data for a plane\n");
                return AVERROR_INVALIDDATA;
            }
            slice_start = 0;
            slice_end   = 0;
            for (j = 0; j < c->slices; j++) {
                slice_end   = bytestream2_get_le32u(&gb);
                if (slice_end < 0 || slice_end < slice_start ||
                    bytestream2_get_bytes_left(&gb) < slice_end + 1024LL) {
                    av_log(avctx, AV_LOG_ERROR, "Incorrect slice size\n");
                    return AVERROR_INVALIDDATA;
                }
                slice_size  = slice_end - slice_start;
                slice_start = slice_end;
                max_slice_size = FFMAX(max_slice_size, slice_size);
            }
            plane_size = slice_end;
            bytestream2_skipu(&gb, plane_size);
            bytestream2_skipu(&gb, 1024);
        }
        plane_start[c->planes] = gb.buffer;
    } else {
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
                if (slice_end < 0 || slice_end < slice_start ||
                    bytestream2_get_bytes_left(&gb) < slice_end) {
                    av_log(avctx, AV_LOG_ERROR, "Incorrect slice size\n");
                    return AVERROR_INVALIDDATA;
                }
                slice_size  = slice_end - slice_start;
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
    }
    av_log(avctx, AV_LOG_DEBUG, "frame information flags %"PRIX32"\n",
           c->frame_info);

    c->frame_pred = (c->frame_info >> 8) & 3;

    max_slice_size += 4*avctx->width;

    if (!c->pack) {
        av_fast_malloc(&c->slice_bits, &c->slice_bits_size,
                       max_slice_size + AV_INPUT_BUFFER_PADDING_SIZE);

        if (!c->slice_bits) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate temporary buffer\n");
            return AVERROR(ENOMEM);
        }
    }

    switch (c->avctx->pix_fmt) {
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRAP:
        for (i = 0; i < c->planes; i++) {
            ret = decode_plane(c, i, frame->data[i],
                               frame->linesize[i], avctx->width,
                               avctx->height, plane_start[i],
                               c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median_planar(c, frame->data[i],
                                          frame->linesize[i], avctx->width,
                                          avctx->height, c->slices, 0);
                } else {
                    restore_median_planar_il(c, frame->data[i],
                                             frame->linesize[i],
                                             avctx->width, avctx->height, c->slices,
                                             0);
                }
            } else if (c->frame_pred == PRED_GRADIENT) {
                if (!c->interlaced) {
                    restore_gradient_planar(c, frame->data[i],
                                            frame->linesize[i], avctx->width,
                                            avctx->height, c->slices, 0);
                } else {
                    restore_gradient_planar_il(c, frame->data[i],
                                               frame->linesize[i],
                                               avctx->width, avctx->height, c->slices,
                                               0);
                }
            }
        }
        c->utdsp.restore_rgb_planes(frame->data[2], frame->data[0], frame->data[1],
                                    frame->linesize[2], frame->linesize[0], frame->linesize[1],
                                    avctx->width, avctx->height);
        break;
    case AV_PIX_FMT_GBRAP10:
    case AV_PIX_FMT_GBRP10:
        for (i = 0; i < c->planes; i++) {
            ret = decode_plane10(c, i, (uint16_t *)frame->data[i],
                                 frame->linesize[i] / 2, avctx->width,
                                 avctx->height, plane_start[i],
                                 plane_start[i + 1] - 1024,
                                 c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
        }
        c->utdsp.restore_rgb_planes10((uint16_t *)frame->data[2], (uint16_t *)frame->data[0], (uint16_t *)frame->data[1],
                                      frame->linesize[2] / 2, frame->linesize[0] / 2, frame->linesize[1] / 2,
                                      avctx->width, avctx->height);
        break;
    case AV_PIX_FMT_YUV420P:
        for (i = 0; i < 3; i++) {
            ret = decode_plane(c, i, frame->data[i], frame->linesize[i],
                               avctx->width >> !!i, avctx->height >> !!i,
                               plane_start[i], c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median_planar(c, frame->data[i], frame->linesize[i],
                                          avctx->width >> !!i, avctx->height >> !!i,
                                          c->slices, !i);
                } else {
                    restore_median_planar_il(c, frame->data[i], frame->linesize[i],
                                             avctx->width  >> !!i,
                                             avctx->height >> !!i,
                                             c->slices, !i);
                }
            } else if (c->frame_pred == PRED_GRADIENT) {
                if (!c->interlaced) {
                    restore_gradient_planar(c, frame->data[i], frame->linesize[i],
                                            avctx->width >> !!i, avctx->height >> !!i,
                                            c->slices, !i);
                } else {
                    restore_gradient_planar_il(c, frame->data[i], frame->linesize[i],
                                               avctx->width  >> !!i,
                                               avctx->height >> !!i,
                                               c->slices, !i);
                }
            }
        }
        break;
    case AV_PIX_FMT_YUV422P:
        for (i = 0; i < 3; i++) {
            ret = decode_plane(c, i, frame->data[i], frame->linesize[i],
                               avctx->width >> !!i, avctx->height,
                               plane_start[i], c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median_planar(c, frame->data[i], frame->linesize[i],
                                          avctx->width >> !!i, avctx->height,
                                          c->slices, 0);
                } else {
                    restore_median_planar_il(c, frame->data[i], frame->linesize[i],
                                             avctx->width >> !!i, avctx->height,
                                             c->slices, 0);
                }
            } else if (c->frame_pred == PRED_GRADIENT) {
                if (!c->interlaced) {
                    restore_gradient_planar(c, frame->data[i], frame->linesize[i],
                                            avctx->width >> !!i, avctx->height,
                                            c->slices, 0);
                } else {
                    restore_gradient_planar_il(c, frame->data[i], frame->linesize[i],
                                               avctx->width  >> !!i, avctx->height,
                                               c->slices, 0);
                }
            }
        }
        break;
    case AV_PIX_FMT_YUV444P:
        for (i = 0; i < 3; i++) {
            ret = decode_plane(c, i, frame->data[i], frame->linesize[i],
                               avctx->width, avctx->height,
                               plane_start[i], c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
            if (c->frame_pred == PRED_MEDIAN) {
                if (!c->interlaced) {
                    restore_median_planar(c, frame->data[i], frame->linesize[i],
                                          avctx->width, avctx->height,
                                          c->slices, 0);
                } else {
                    restore_median_planar_il(c, frame->data[i], frame->linesize[i],
                                             avctx->width, avctx->height,
                                             c->slices, 0);
                }
            } else if (c->frame_pred == PRED_GRADIENT) {
                if (!c->interlaced) {
                    restore_gradient_planar(c, frame->data[i], frame->linesize[i],
                                            avctx->width, avctx->height,
                                            c->slices, 0);
                } else {
                    restore_gradient_planar_il(c, frame->data[i], frame->linesize[i],
                                               avctx->width, avctx->height,
                                               c->slices, 0);
                }
            }
        }
        break;
    case AV_PIX_FMT_YUV420P10:
        for (i = 0; i < 3; i++) {
            ret = decode_plane10(c, i, (uint16_t *)frame->data[i], frame->linesize[i] / 2,
                                 avctx->width >> !!i, avctx->height >> !!i,
                                 plane_start[i], plane_start[i + 1] - 1024, c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
        }
        break;
    case AV_PIX_FMT_YUV422P10:
        for (i = 0; i < 3; i++) {
            ret = decode_plane10(c, i, (uint16_t *)frame->data[i], frame->linesize[i] / 2,
                                 avctx->width >> !!i, avctx->height,
                                 plane_start[i], plane_start[i + 1] - 1024, c->frame_pred == PRED_LEFT);
            if (ret)
                return ret;
        }
        break;
    }

    frame->key_frame = 1;
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->interlaced_frame = !!c->interlaced;

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    UtvideoContext * const c = avctx->priv_data;
    int h_shift, v_shift;

    c->avctx = avctx;

    ff_utvideodsp_init(&c->utdsp);
    ff_bswapdsp_init(&c->bdsp);
    ff_llviddsp_init(&c->llviddsp);

    c->slice_bits_size = 0;

    switch (avctx->codec_tag) {
    case MKTAG('U', 'L', 'R', 'G'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_GBRP;
        break;
    case MKTAG('U', 'L', 'R', 'A'):
        c->planes      = 4;
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
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
    case MKTAG('U', 'L', 'Y', '4'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        avctx->colorspace = AVCOL_SPC_BT470BG;
        break;
    case MKTAG('U', 'Q', 'Y', '0'):
        c->planes      = 3;
        c->pro         = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV420P10;
        break;
    case MKTAG('U', 'Q', 'Y', '2'):
        c->planes      = 3;
        c->pro         = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P10;
        break;
    case MKTAG('U', 'Q', 'R', 'G'):
        c->planes      = 3;
        c->pro         = 1;
        avctx->pix_fmt = AV_PIX_FMT_GBRP10;
        break;
    case MKTAG('U', 'Q', 'R', 'A'):
        c->planes      = 4;
        c->pro         = 1;
        avctx->pix_fmt = AV_PIX_FMT_GBRAP10;
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
    case MKTAG('U', 'L', 'H', '4'):
        c->planes      = 3;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        avctx->colorspace = AVCOL_SPC_BT709;
        break;
    case MKTAG('U', 'M', 'Y', '2'):
        c->planes      = 3;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        avctx->colorspace = AVCOL_SPC_BT470BG;
        break;
    case MKTAG('U', 'M', 'H', '2'):
        c->planes      = 3;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        avctx->colorspace = AVCOL_SPC_BT709;
        break;
    case MKTAG('U', 'M', 'Y', '4'):
        c->planes      = 3;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        avctx->colorspace = AVCOL_SPC_BT470BG;
        break;
    case MKTAG('U', 'M', 'H', '4'):
        c->planes      = 3;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        avctx->colorspace = AVCOL_SPC_BT709;
        break;
    case MKTAG('U', 'M', 'R', 'G'):
        c->planes      = 3;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_GBRP;
        break;
    case MKTAG('U', 'M', 'R', 'A'):
        c->planes      = 4;
        c->pack        = 1;
        avctx->pix_fmt = AV_PIX_FMT_GBRAP;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown Ut Video FOURCC provided (%08X)\n",
               avctx->codec_tag);
        return AVERROR_INVALIDDATA;
    }

    av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt, &h_shift, &v_shift);
    if ((avctx->width  & ((1<<h_shift)-1)) ||
        (avctx->height & ((1<<v_shift)-1))) {
        avpriv_request_sample(avctx, "Odd dimensions");
        return AVERROR_PATCHWELCOME;
    }

    if (c->pack && avctx->extradata_size >= 16) {
        av_log(avctx, AV_LOG_DEBUG, "Encoder version %d.%d.%d.%d\n",
               avctx->extradata[3], avctx->extradata[2],
               avctx->extradata[1], avctx->extradata[0]);
        av_log(avctx, AV_LOG_DEBUG, "Original format %"PRIX32"\n",
               AV_RB32(avctx->extradata + 4));
        c->compression = avctx->extradata[8];
        if (c->compression != 2)
            avpriv_request_sample(avctx, "Unknown compression type");
        c->slices      = avctx->extradata[9] + 1;
    } else if (!c->pro && avctx->extradata_size >= 16) {
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
    } else if (c->pro && avctx->extradata_size == 8) {
        av_log(avctx, AV_LOG_DEBUG, "Encoder version %d.%d.%d.%d\n",
               avctx->extradata[3], avctx->extradata[2],
               avctx->extradata[1], avctx->extradata[0]);
        av_log(avctx, AV_LOG_DEBUG, "Original format %"PRIX32"\n",
               AV_RB32(avctx->extradata + 4));
        c->interlaced  = 0;
        c->frame_info_size = 4;
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "Insufficient extradata size %d, should be at least 16\n",
               avctx->extradata_size);
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

const FFCodec ff_utvideo_decoder = {
    .p.name         = "utvideo",
    CODEC_LONG_NAME("Ut Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_UTVIDEO,
    .priv_data_size = sizeof(UtvideoContext),
    .init           = decode_init,
    .close          = decode_end,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
