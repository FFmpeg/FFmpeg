/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264.h"
#include "h264data.h"
#include "h264_mvpred.h"
#include "golomb.h"
#include "mathops.h"
#include "rectangle.h"
#include "thread.h"
#include "vdpau_internal.h"
#include "libavutil/avassert.h"

// #undef NDEBUG
#include <assert.h>

const uint16_t ff_h264_mb_sizes[4] = { 256, 384, 512, 768 };

static const uint8_t rem6[QP_MAX_NUM + 1] = {
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3,
};

static const uint8_t div6[QP_MAX_NUM + 1] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
    3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
    7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10,
   10,10,10,11,11,11,11,11,11,12,12,12,12,12,12,13,13,13, 13, 13, 13,
   14,14,14,14,
};

static const enum PixelFormat hwaccel_pixfmt_list_h264_jpeg_420[] = {
    PIX_FMT_DXVA2_VLD,
    PIX_FMT_VAAPI_VLD,
    PIX_FMT_VDA_VLD,
    PIX_FMT_YUVJ420P,
    PIX_FMT_NONE
};

int avpriv_h264_has_num_reorder_frames(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    return h ? h->sps.num_reorder_frames : 0;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(H264Context *h)
{
    MpegEncContext *const s     = &h->s;
    static const int8_t top[12] = {
        -1, 0, LEFT_DC_PRED, -1, -1, -1, -1, -1, 0
    };
    static const int8_t left[12] = {
        0, -1, TOP_DC_PRED, 0, -1, -1, -1, 0, -1, DC_128_PRED
    };
    int i;

    if (!(h->top_samples_available & 0x8000)) {
        for (i = 0; i < 4; i++) {
            int status = top[h->intra4x4_pred_mode_cache[scan8[0] + i]];
            if (status < 0) {
                av_log(h->s.avctx, AV_LOG_ERROR,
                       "top block unavailable for requested intra4x4 mode %d at %d %d\n",
                       status, s->mb_x, s->mb_y);
                return -1;
            } else if (status) {
                h->intra4x4_pred_mode_cache[scan8[0] + i] = status;
            }
        }
    }

    if ((h->left_samples_available & 0x8888) != 0x8888) {
        static const int mask[4] = { 0x8000, 0x2000, 0x80, 0x20 };
        for (i = 0; i < 4; i++)
            if (!(h->left_samples_available & mask[i])) {
                int status = left[h->intra4x4_pred_mode_cache[scan8[0] + 8 * i]];
                if (status < 0) {
                    av_log(h->s.avctx, AV_LOG_ERROR,
                           "left block unavailable for requested intra4x4 mode %d at %d %d\n",
                           status, s->mb_x, s->mb_y);
                    return -1;
                } else if (status) {
                    h->intra4x4_pred_mode_cache[scan8[0] + 8 * i] = status;
                }
            }
    }

    return 0;
} // FIXME cleanup like ff_h264_check_intra_pred_mode

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(H264Context *h, int mode, int is_chroma)
{
    MpegEncContext *const s     = &h->s;
    static const int8_t top[7]  = { LEFT_DC_PRED8x8, 1, -1, -1 };
    static const int8_t left[7] = { TOP_DC_PRED8x8, -1, 2, -1, DC_128_PRED8x8 };

    if (mode > 6U) {
        av_log(h->s.avctx, AV_LOG_ERROR,
               "out of range intra chroma pred mode at %d %d\n",
               s->mb_x, s->mb_y);
        return -1;
    }

    if (!(h->top_samples_available & 0x8000)) {
        mode = top[mode];
        if (mode < 0) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "top block unavailable for requested intra mode at %d %d\n",
                   s->mb_x, s->mb_y);
            return -1;
        }
    }

    if ((h->left_samples_available & 0x8080) != 0x8080) {
        mode = left[mode];
        if (is_chroma && (h->left_samples_available & 0x8080)) {
            // mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode = ALZHEIMER_DC_L0T_PRED8x8 +
                   (!(h->left_samples_available & 0x8000)) +
                   2 * (mode == DC_128_PRED8x8);
        }
        if (mode < 0) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "left block unavailable for requested intra mode at %d %d\n",
                   s->mb_x, s->mb_y);
            return -1;
        }
    }

    return mode;
}

const uint8_t *ff_h264_decode_nal(H264Context *h, const uint8_t *src,
                                  int *dst_length, int *consumed, int length)
{
    int i, si, di;
    uint8_t *dst;
    int bufidx;

    // src[0]&0x80; // forbidden bit
    h->nal_ref_idc   = src[0] >> 5;
    h->nal_unit_type = src[0] & 0x1F;

    src++;
    length--;

#define STARTCODE_TEST                                                  \
        if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {     \
            if (src[i + 2] != 3) {                                      \
                /* startcode, so we must be past the end */             \
                length = i;                                             \
            }                                                           \
            break;                                                      \
        }
#if HAVE_FAST_UNALIGNED
#define FIND_FIRST_ZERO                                                 \
        if (i > 0 && !src[i])                                           \
            i--;                                                        \
        while (src[i])                                                  \
            i++
#if HAVE_FAST_64BIT
    for (i = 0; i + 1 < length; i += 9) {
        if (!((~AV_RN64A(src + i) &
               (AV_RN64A(src + i) - 0x0100010001000101ULL)) &
              0x8000800080008080ULL))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 7;
    }
#else
    for (i = 0; i + 1 < length; i += 5) {
        if (!((~AV_RN32A(src + i) &
               (AV_RN32A(src + i) - 0x01000101U)) &
              0x80008080U))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 3;
    }
#endif
#else
    for (i = 0; i + 1 < length; i += 2) {
        if (src[i])
            continue;
        if (i > 0 && src[i - 1] == 0)
            i--;
        STARTCODE_TEST;
    }
#endif

    // use second escape buffer for inter data
    bufidx = h->nal_unit_type == NAL_DPC ? 1 : 0;

    si = h->rbsp_buffer_size[bufidx];
    av_fast_padded_malloc(&h->rbsp_buffer[bufidx], &h->rbsp_buffer_size[bufidx], length+MAX_MBPAIR_SIZE);
    dst = h->rbsp_buffer[bufidx];

    if (dst == NULL)
        return NULL;

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        if(h->s.avctx->flags2 & CODEC_FLAG2_FAST){
            return src;
        }else{
            memcpy(dst, src, length);
            return dst;
        }
    }

    memcpy(dst, src, i);
    si = di = i;
    while (si + 2 < length) {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++]  = 0;
                dst[di++]  = 0;
                si        += 3;
                continue;
            } else // next start code
                goto nsc;
        }

        dst[di++] = src[si++];
    }
    while (si < length)
        dst[di++] = src[si++];
nsc:

    memset(dst + di, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    *dst_length = di;
    *consumed   = si + 1; // +1 for the header
    /* FIXME store exact number of bits in the getbitcontext
     * (it is needed for decoding) */
    return dst;
}

/**
 * Identify the exact end of the bitstream
 * @return the length of the trailing, or 0 if damaged
 */
static int decode_rbsp_trailing(H264Context *h, const uint8_t *src)
{
    int v = *src;
    int r;

    tprintf(h->s.avctx, "rbsp trailing %X\n", v);

    for (r = 1; r < 9; r++) {
        if (v & 1)
            return r;
        v >>= 1;
    }
    return 0;
}

static inline int get_lowest_part_list_y(H264Context *h, Picture *pic, int n,
                                         int height, int y_offset, int list)
{
    int raw_my        = h->mv_cache[list][scan8[n]][1];
    int filter_height = (raw_my & 3) ? 2 : 0;
    int full_my       = (raw_my >> 2) + y_offset;
    int top           = full_my - filter_height;
    int bottom        = full_my + filter_height + height;

    return FFMAX(abs(top), bottom);
}

static inline void get_lowest_part_y(H264Context *h, int refs[2][48], int n,
                                     int height, int y_offset, int list0,
                                     int list1, int *nrefs)
{
    MpegEncContext *const s = &h->s;
    int my;

    y_offset += 16 * (s->mb_y >> MB_FIELD);

    if (list0) {
        int ref_n    = h->ref_cache[0][scan8[n]];
        Picture *ref = &h->ref_list[0][ref_n];

        // Error resilience puts the current picture in the ref list.
        // Don't try to wait on these as it will cause a deadlock.
        // Fields can wait on each other, though.
        if (ref->f.thread_opaque   != s->current_picture.f.thread_opaque ||
            (ref->f.reference & 3) != s->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 0);
            if (refs[0][ref_n] < 0)
                nrefs[0] += 1;
            refs[0][ref_n] = FFMAX(refs[0][ref_n], my);
        }
    }

    if (list1) {
        int ref_n    = h->ref_cache[1][scan8[n]];
        Picture *ref = &h->ref_list[1][ref_n];

        if (ref->f.thread_opaque   != s->current_picture.f.thread_opaque ||
            (ref->f.reference & 3) != s->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 1);
            if (refs[1][ref_n] < 0)
                nrefs[1] += 1;
            refs[1][ref_n] = FFMAX(refs[1][ref_n], my);
        }
    }
}

/**
 * Wait until all reference frames are available for MC operations.
 *
 * @param h the H264 context
 */
static void await_references(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    const int mb_xy   = h->mb_xy;
    const int mb_type = s->current_picture.f.mb_type[mb_xy];
    int refs[2][48];
    int nrefs[2] = { 0 };
    int ref, list;

    memset(refs, -1, sizeof(refs));

    if (IS_16X16(mb_type)) {
        get_lowest_part_y(h, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
    } else if (IS_16X8(mb_type)) {
        get_lowest_part_y(h, refs, 0, 8, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 8, 8, 8,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else if (IS_8X16(mb_type)) {
        get_lowest_part_y(h, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 4, 16, 0,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else {
        int i;

        assert(IS_8X8(mb_type));

        for (i = 0; i < 4; i++) {
            const int sub_mb_type = h->sub_mb_type[i];
            const int n           = 4 * i;
            int y_offset          = (i & 2) << 2;

            if (IS_SUB_8X8(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_8X4(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 4, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, refs, n + 2, 4, y_offset + 4,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_4X8(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, refs, n + 1, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else {
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for (j = 0; j < 4; j++) {
                    int sub_y_offset = y_offset + 2 * (j & 2);
                    get_lowest_part_y(h, refs, n + j, 4, sub_y_offset,
                                      IS_DIR(sub_mb_type, 0, 0),
                                      IS_DIR(sub_mb_type, 0, 1),
                                      nrefs);
                }
            }
        }
    }

    for (list = h->list_count - 1; list >= 0; list--)
        for (ref = 0; ref < 48 && nrefs[list]; ref++) {
            int row = refs[list][ref];
            if (row >= 0) {
                Picture *ref_pic      = &h->ref_list[list][ref];
                int ref_field         = ref_pic->f.reference - 1;
                int ref_field_picture = ref_pic->field_picture;
                int pic_height        = 16 * s->mb_height >> ref_field_picture;

                row <<= MB_MBAFF;
                nrefs[list]--;

                if (!FIELD_PICTURE && ref_field_picture) { // frame referencing two fields
                    ff_thread_await_progress(&ref_pic->f,
                                             FFMIN((row >> 1) - !(row & 1),
                                                   pic_height - 1),
                                             1);
                    ff_thread_await_progress(&ref_pic->f,
                                             FFMIN((row >> 1), pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE && !ref_field_picture) { // field referencing one field of a frame
                    ff_thread_await_progress(&ref_pic->f,
                                             FFMIN(row * 2 + ref_field,
                                                   pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE) {
                    ff_thread_await_progress(&ref_pic->f,
                                             FFMIN(row, pic_height - 1),
                                             ref_field);
                } else {
                    ff_thread_await_progress(&ref_pic->f,
                                             FFMIN(row, pic_height - 1),
                                             0);
                }
            }
        }
}

static av_always_inline void mc_dir_part(H264Context *h, Picture *pic,
                                         int n, int square, int height,
                                         int delta, int list,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int src_x_offset, int src_y_offset,
                                         qpel_mc_func *qpix_op,
                                         h264_chroma_mc_func chroma_op,
                                         int pixel_shift, int chroma_idc)
{
    MpegEncContext *const s = &h->s;
    const int mx      = h->mv_cache[list][scan8[n]][0] + src_x_offset * 8;
    int my            = h->mv_cache[list][scan8[n]][1] + src_y_offset * 8;
    const int luma_xy = (mx & 3) + ((my & 3) << 2);
    int offset        = ((mx >> 2) << pixel_shift) + (my >> 2) * h->mb_linesize;
    uint8_t *src_y    = pic->f.data[0] + offset;
    uint8_t *src_cb, *src_cr;
    int extra_width  = h->emu_edge_width;
    int extra_height = h->emu_edge_height;
    int emu = 0;
    const int full_mx    = mx >> 2;
    const int full_my    = my >> 2;
    const int pic_width  = 16 * s->mb_width;
    const int pic_height = 16 * s->mb_height >> MB_FIELD;
    int ysh;

    if (mx & 7)
        extra_width -= 3;
    if (my & 7)
        extra_height -= 3;

    if (full_mx                <          0 - extra_width  ||
        full_my                <          0 - extra_height ||
        full_mx + 16 /*FIXME*/ > pic_width  + extra_width  ||
        full_my + 16 /*FIXME*/ > pic_height + extra_height) {
        s->dsp.emulated_edge_mc(s->edge_emu_buffer,
                                src_y - (2 << pixel_shift) - 2 * h->mb_linesize,
                                h->mb_linesize,
                                16 + 5, 16 + 5 /*FIXME*/, full_mx - 2,
                                full_my - 2, pic_width, pic_height);
        src_y = s->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        emu   = 1;
    }

    qpix_op[luma_xy](dest_y, src_y, h->mb_linesize); // FIXME try variable height perhaps?
    if (!square)
        qpix_op[luma_xy](dest_y + delta, src_y + delta, h->mb_linesize);

    if (CONFIG_GRAY && s->flags & CODEC_FLAG_GRAY)
        return;

    if (chroma_idc == 3 /* yuv444 */) {
        src_cb = pic->f.data[1] + offset;
        if (emu) {
            s->dsp.emulated_edge_mc(s->edge_emu_buffer,
                                    src_cb - (2 << pixel_shift) - 2 * h->mb_linesize,
                                    h->mb_linesize,
                                    16 + 5, 16 + 5 /*FIXME*/,
                                    full_mx - 2, full_my - 2,
                                    pic_width, pic_height);
            src_cb = s->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        }
        qpix_op[luma_xy](dest_cb, src_cb, h->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cb + delta, src_cb + delta, h->mb_linesize);

        src_cr = pic->f.data[2] + offset;
        if (emu) {
            s->dsp.emulated_edge_mc(s->edge_emu_buffer,
                                    src_cr - (2 << pixel_shift) - 2 * h->mb_linesize,
                                    h->mb_linesize,
                                    16 + 5, 16 + 5 /*FIXME*/,
                                    full_mx - 2, full_my - 2,
                                    pic_width, pic_height);
            src_cr = s->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        }
        qpix_op[luma_xy](dest_cr, src_cr, h->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cr + delta, src_cr + delta, h->mb_linesize);
        return;
    }

    ysh = 3 - (chroma_idc == 2 /* yuv422 */);
    if (chroma_idc == 1 /* yuv420 */ && MB_FIELD) {
        // chroma offset when predicting from a field of opposite parity
        my  += 2 * ((s->mb_y & 1) - (pic->f.reference - 1));
        emu |= (my >> 3) < 0 || (my >> 3) + 8 >= (pic_height >> 1);
    }

    src_cb = pic->f.data[1] + ((mx >> 3) << pixel_shift) +
             (my >> ysh) * h->mb_uvlinesize;
    src_cr = pic->f.data[2] + ((mx >> 3) << pixel_shift) +
             (my >> ysh) * h->mb_uvlinesize;

    if (emu) {
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, src_cb, h->mb_uvlinesize,
                                9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cb = s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->mb_uvlinesize,
              height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, (my << (chroma_idc == 2 /* yuv422 */)) & 7);

    if (emu) {
        s->dsp.emulated_edge_mc(s->edge_emu_buffer, src_cr, h->mb_uvlinesize,
                                9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cr = s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->mb_uvlinesize, height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, (my << (chroma_idc == 2 /* yuv422 */)) & 7);
}

static av_always_inline void mc_part_std(H264Context *h, int n, int square,
                                         int height, int delta,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int x_offset, int y_offset,
                                         qpel_mc_func *qpix_put,
                                         h264_chroma_mc_func chroma_put,
                                         qpel_mc_func *qpix_avg,
                                         h264_chroma_mc_func chroma_avg,
                                         int list0, int list1,
                                         int pixel_shift, int chroma_idc)
{
    MpegEncContext *const s       = &h->s;
    qpel_mc_func *qpix_op         = qpix_put;
    h264_chroma_mc_func chroma_op = chroma_put;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        dest_cb += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
    } else { /* yuv420 */
        dest_cb += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
    }
    x_offset += 8 * s->mb_x;
    y_offset += 8 * (s->mb_y >> MB_FIELD);

    if (list0) {
        Picture *ref = &h->ref_list[0][h->ref_cache[0][scan8[n]]];
        mc_dir_part(h, ref, n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);

        qpix_op   = qpix_avg;
        chroma_op = chroma_avg;
    }

    if (list1) {
        Picture *ref = &h->ref_list[1][h->ref_cache[1][scan8[n]]];
        mc_dir_part(h, ref, n, square, height, delta, 1,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);
    }
}

static av_always_inline void mc_part_weighted(H264Context *h, int n, int square,
                                              int height, int delta,
                                              uint8_t *dest_y, uint8_t *dest_cb,
                                              uint8_t *dest_cr,
                                              int x_offset, int y_offset,
                                              qpel_mc_func *qpix_put,
                                              h264_chroma_mc_func chroma_put,
                                              h264_weight_func luma_weight_op,
                                              h264_weight_func chroma_weight_op,
                                              h264_biweight_func luma_weight_avg,
                                              h264_biweight_func chroma_weight_avg,
                                              int list0, int list1,
                                              int pixel_shift, int chroma_idc)
{
    MpegEncContext *const s = &h->s;
    int chroma_height;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        chroma_height     = height;
        chroma_weight_avg = luma_weight_avg;
        chroma_weight_op  = luma_weight_op;
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        chroma_height = height;
        dest_cb      += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
    } else { /* yuv420 */
        chroma_height = height >> 1;
        dest_cb      += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
    }
    x_offset += 8 * s->mb_x;
    y_offset += 8 * (s->mb_y >> MB_FIELD);

    if (list0 && list1) {
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = s->obmc_scratchpad;
        uint8_t *tmp_cr = s->obmc_scratchpad + (16 << pixel_shift);
        uint8_t *tmp_y  = s->obmc_scratchpad + 16 * h->mb_uvlinesize;
        int refn0       = h->ref_cache[0][scan8[n]];
        int refn1       = h->ref_cache[1][scan8[n]];

        mc_dir_part(h, &h->ref_list[0][refn0], n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);
        mc_dir_part(h, &h->ref_list[1][refn1], n, square, height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);

        if (h->use_weight == 2) {
            int weight0 = h->implicit_weight[refn0][refn1][s->mb_y & 1];
            int weight1 = 64 - weight0;
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize,
                            height, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize,
                              chroma_height, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize,
                              chroma_height, 5, weight0, weight1, 0);
        } else {
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize, height,
                            h->luma_log2_weight_denom,
                            h->luma_weight[refn0][0][0],
                            h->luma_weight[refn1][1][0],
                            h->luma_weight[refn0][0][1] +
                            h->luma_weight[refn1][1][1]);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, chroma_height,
                              h->chroma_log2_weight_denom,
                              h->chroma_weight[refn0][0][0][0],
                              h->chroma_weight[refn1][1][0][0],
                              h->chroma_weight[refn0][0][0][1] +
                              h->chroma_weight[refn1][1][0][1]);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, chroma_height,
                              h->chroma_log2_weight_denom,
                              h->chroma_weight[refn0][0][1][0],
                              h->chroma_weight[refn1][1][1][0],
                              h->chroma_weight[refn0][0][1][1] +
                              h->chroma_weight[refn1][1][1][1]);
        }
    } else {
        int list     = list1 ? 1 : 0;
        int refn     = h->ref_cache[list][scan8[n]];
        Picture *ref = &h->ref_list[list][refn];
        mc_dir_part(h, ref, n, square, height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put, pixel_shift, chroma_idc);

        luma_weight_op(dest_y, h->mb_linesize, height,
                       h->luma_log2_weight_denom,
                       h->luma_weight[refn][list][0],
                       h->luma_weight[refn][list][1]);
        if (h->use_weight_chroma) {
            chroma_weight_op(dest_cb, h->mb_uvlinesize, chroma_height,
                             h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][0][0],
                             h->chroma_weight[refn][list][0][1]);
            chroma_weight_op(dest_cr, h->mb_uvlinesize, chroma_height,
                             h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][1][0],
                             h->chroma_weight[refn][list][1][1]);
        }
    }
}

static av_always_inline void prefetch_motion(H264Context *h, int list,
                                             int pixel_shift, int chroma_idc)
{
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    MpegEncContext *const s = &h->s;
    const int refn = h->ref_cache[list][scan8[0]];
    if (refn >= 0) {
        const int mx  = (h->mv_cache[list][scan8[0]][0] >> 2) + 16 * s->mb_x + 8;
        const int my  = (h->mv_cache[list][scan8[0]][1] >> 2) + 16 * s->mb_y;
        uint8_t **src = h->ref_list[list][refn].f.data;
        int off       = (mx << pixel_shift) +
                        (my + (s->mb_x & 3) * 4) * h->mb_linesize +
                        (64 << pixel_shift);
        s->dsp.prefetch(src[0] + off, s->linesize, 4);
        if (chroma_idc == 3 /* yuv444 */) {
            s->dsp.prefetch(src[1] + off, s->linesize, 4);
            s->dsp.prefetch(src[2] + off, s->linesize, 4);
        } else {
            off= (((mx>>1)+64)<<pixel_shift) + ((my>>1) + (s->mb_x&7))*s->uvlinesize;
            s->dsp.prefetch(src[1] + off, src[2] - src[1], 2);
        }
    }
}

static void free_tables(H264Context *h, int free_rbsp)
{
    int i;
    H264Context *hx;

    av_freep(&h->intra4x4_pred_mode);
    av_freep(&h->chroma_pred_mode_table);
    av_freep(&h->cbp_table);
    av_freep(&h->mvd_table[0]);
    av_freep(&h->mvd_table[1]);
    av_freep(&h->direct_table);
    av_freep(&h->non_zero_count);
    av_freep(&h->slice_table_base);
    h->slice_table = NULL;
    av_freep(&h->list_counts);

    av_freep(&h->mb2b_xy);
    av_freep(&h->mb2br_xy);

    for (i = 0; i < MAX_THREADS; i++) {
        hx = h->thread_context[i];
        if (!hx)
            continue;
        av_freep(&hx->top_borders[1]);
        av_freep(&hx->top_borders[0]);
        av_freep(&hx->s.obmc_scratchpad);
        if (free_rbsp) {
            av_freep(&hx->rbsp_buffer[1]);
            av_freep(&hx->rbsp_buffer[0]);
            hx->rbsp_buffer_size[0] = 0;
            hx->rbsp_buffer_size[1] = 0;
        }
        if (i)
            av_freep(&h->thread_context[i]);
    }
}

static void init_dequant8_coeff_table(H264Context *h)
{
    int i, j, q, x;
    const int max_qp = 51 + 6 * (h->sps.bit_depth_luma - 8);

    for (i = 0; i < 6; i++) {
        h->dequant8_coeff[i] = h->dequant8_buffer[i];
        for (j = 0; j < i; j++)
            if (!memcmp(h->pps.scaling_matrix8[j], h->pps.scaling_matrix8[i],
                        64 * sizeof(uint8_t))) {
                h->dequant8_coeff[i] = h->dequant8_buffer[j];
                break;
            }
        if (j < i)
            continue;

        for (q = 0; q < max_qp + 1; q++) {
            int shift = div6[q];
            int idx   = rem6[q];
            for (x = 0; x < 64; x++)
                h->dequant8_coeff[i][q][(x >> 3) | ((x & 7) << 3)] =
                    ((uint32_t)dequant8_coeff_init[idx][dequant8_coeff_init_scan[((x >> 1) & 12) | (x & 3)]] *
                     h->pps.scaling_matrix8[i][x]) << shift;
        }
    }
}

static void init_dequant4_coeff_table(H264Context *h)
{
    int i, j, q, x;
    const int max_qp = 51 + 6 * (h->sps.bit_depth_luma - 8);
    for (i = 0; i < 6; i++) {
        h->dequant4_coeff[i] = h->dequant4_buffer[i];
        for (j = 0; j < i; j++)
            if (!memcmp(h->pps.scaling_matrix4[j], h->pps.scaling_matrix4[i],
                        16 * sizeof(uint8_t))) {
                h->dequant4_coeff[i] = h->dequant4_buffer[j];
                break;
            }
        if (j < i)
            continue;

        for (q = 0; q < max_qp + 1; q++) {
            int shift = div6[q] + 2;
            int idx   = rem6[q];
            for (x = 0; x < 16; x++)
                h->dequant4_coeff[i][q][(x >> 2) | ((x << 2) & 0xF)] =
                    ((uint32_t)dequant4_coeff_init[idx][(x & 1) + ((x >> 2) & 1)] *
                     h->pps.scaling_matrix4[i][x]) << shift;
        }
    }
}

static void init_dequant_tables(H264Context *h)
{
    int i, x;
    init_dequant4_coeff_table(h);
    if (h->pps.transform_8x8_mode)
        init_dequant8_coeff_table(h);
    if (h->sps.transform_bypass) {
        for (i = 0; i < 6; i++)
            for (x = 0; x < 16; x++)
                h->dequant4_coeff[i][0][x] = 1 << 6;
        if (h->pps.transform_8x8_mode)
            for (i = 0; i < 6; i++)
                for (x = 0; x < 64; x++)
                    h->dequant8_coeff[i][0][x] = 1 << 6;
    }
}

int ff_h264_alloc_tables(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    const int big_mb_num    = s->mb_stride * (s->mb_height + 1);
    const int row_mb_num    = 2*s->mb_stride*FFMAX(s->avctx->thread_count, 1);
    int x, y;

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->intra4x4_pred_mode,
                      row_mb_num * 8 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->non_zero_count,
                      big_mb_num * 48 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->slice_table_base,
                      (big_mb_num + s->mb_stride) * sizeof(*h->slice_table_base), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->cbp_table,
                      big_mb_num * sizeof(uint16_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->chroma_pred_mode_table,
                      big_mb_num * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mvd_table[0],
                      16 * row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mvd_table[1],
                      16 * row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->direct_table,
                      4 * big_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->list_counts,
                      big_mb_num * sizeof(uint8_t), fail)

    memset(h->slice_table_base, -1,
           (big_mb_num + s->mb_stride) * sizeof(*h->slice_table_base));
    h->slice_table = h->slice_table_base + s->mb_stride * 2 + 1;

    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mb2b_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->mb2br_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    for (y = 0; y < s->mb_height; y++)
        for (x = 0; x < s->mb_width; x++) {
            const int mb_xy = x + y * s->mb_stride;
            const int b_xy  = 4 * x + 4 * y * h->b_stride;

            h->mb2b_xy[mb_xy]  = b_xy;
            h->mb2br_xy[mb_xy] = 8 * (FMO ? mb_xy : (mb_xy % (2 * s->mb_stride)));
        }

    s->obmc_scratchpad = NULL;

    if (!h->dequant4_coeff[0])
        init_dequant_tables(h);

    return 0;

fail:
    free_tables(h, 1);
    return -1;
}

/**
 * Mimic alloc_tables(), but for every context thread.
 */
static void clone_tables(H264Context *dst, H264Context *src, int i)
{
    MpegEncContext *const s     = &src->s;
    dst->intra4x4_pred_mode     = src->intra4x4_pred_mode + i * 8 * 2 * s->mb_stride;
    dst->non_zero_count         = src->non_zero_count;
    dst->slice_table            = src->slice_table;
    dst->cbp_table              = src->cbp_table;
    dst->mb2b_xy                = src->mb2b_xy;
    dst->mb2br_xy               = src->mb2br_xy;
    dst->chroma_pred_mode_table = src->chroma_pred_mode_table;
    dst->mvd_table[0]           = src->mvd_table[0] + i * 8 * 2 * s->mb_stride;
    dst->mvd_table[1]           = src->mvd_table[1] + i * 8 * 2 * s->mb_stride;
    dst->direct_table           = src->direct_table;
    dst->list_counts            = src->list_counts;
    dst->s.obmc_scratchpad      = NULL;
    ff_h264_pred_init(&dst->hpc, src->s.codec_id, src->sps.bit_depth_luma,
                      src->sps.chroma_format_idc);
}

/**
 * Init context
 * Allocate buffers which are not shared amongst multiple threads.
 */
static int context_init(H264Context *h)
{
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->top_borders[0],
                      h->s.mb_width * 16 * 3 * sizeof(uint8_t) * 2, fail)
    FF_ALLOCZ_OR_GOTO(h->s.avctx, h->top_borders[1],
                      h->s.mb_width * 16 * 3 * sizeof(uint8_t) * 2, fail)

    h->ref_cache[0][scan8[5]  + 1] =
    h->ref_cache[0][scan8[7]  + 1] =
    h->ref_cache[0][scan8[13] + 1] =
    h->ref_cache[1][scan8[5]  + 1] =
    h->ref_cache[1][scan8[7]  + 1] =
    h->ref_cache[1][scan8[13] + 1] = PART_NOT_AVAILABLE;

    return 0;

fail:
    return -1; // free_tables will clean up for us
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size);

static av_cold void common_init(H264Context *h)
{
    MpegEncContext *const s = &h->s;

    s->width    = s->avctx->width;
    s->height   = s->avctx->height;
    s->codec_id = s->avctx->codec->id;

    s->avctx->bits_per_raw_sample = 8;
    h->cur_chroma_format_idc = 1;

    ff_h264dsp_init(&h->h264dsp,
                    s->avctx->bits_per_raw_sample, h->cur_chroma_format_idc);
    ff_h264_pred_init(&h->hpc, s->codec_id,
                      s->avctx->bits_per_raw_sample, h->cur_chroma_format_idc);

    h->dequant_coeff_pps = -1;
    s->unrestricted_mv   = 1;

    s->dsp.dct_bits = 16;
    /* needed so that IDCT permutation is known early */
    ff_dsputil_init(&s->dsp, s->avctx);

    memset(h->pps.scaling_matrix4, 16, 6 * 16 * sizeof(uint8_t));
    memset(h->pps.scaling_matrix8, 16, 2 * 64 * sizeof(uint8_t));
}

int ff_h264_decode_extradata(H264Context *h, const uint8_t *buf, int size)
{
    AVCodecContext *avctx = h->s.avctx;

    if (!buf || size <= 0)
        return -1;

    if (buf[0] == 1) {
        int i, cnt, nalsize;
        const unsigned char *p = buf;

        h->is_avc = 1;

        if (size < 7) {
            av_log(avctx, AV_LOG_ERROR, "avcC too short\n");
            return -1;
        }
        /* sps and pps in the avcC always have length coded with 2 bytes,
         * so put a fake nal_length_size = 2 while parsing them */
        h->nal_length_size = 2;
        // Decode sps from avcC
        cnt = *(p + 5) & 0x1f; // Number of sps
        p  += 6;
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(nalsize > size - (p-buf))
                return -1;
            if (decode_nal_units(h, p, nalsize) < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding sps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(nalsize > size - (p-buf))
                return -1;
            if (decode_nal_units(h, p, nalsize) < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding pps %d from avcC failed\n", i);
                return -1;
            }
            p += nalsize;
        }
        // Now store right nal length size, that will be used to parse all other nals
        h->nal_length_size = (buf[4] & 0x03) + 1;
    } else {
        h->is_avc = 0;
        if (decode_nal_units(h, buf, size) < 0)
            return -1;
    }
    return size;
}

av_cold int ff_h264_decode_init(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *const s = &h->s;
    int i;

    ff_MPV_decode_defaults(s);

    s->avctx = avctx;
    common_init(h);

    s->out_format      = FMT_H264;
    s->workaround_bugs = avctx->workaround_bugs;

    /* set defaults */
    // s->decode_mb = ff_h263_decode_mb;
    s->quarter_sample = 1;
    if (!avctx->has_b_frames)
        s->low_delay = 1;

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    ff_h264_decode_init_vlc();

    h->pixel_shift = 0;
    h->sps.bit_depth_luma = avctx->bits_per_raw_sample = 8;

    h->thread_context[0] = h;
    h->outputed_poc      = h->next_outputed_poc = INT_MIN;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
    h->prev_poc_msb = 1 << 16;
    h->prev_frame_num = -1;
    h->x264_build   = -1;
    ff_h264_reset_sei(h);
    if (avctx->codec_id == AV_CODEC_ID_H264) {
        if (avctx->ticks_per_frame == 1)
            s->avctx->time_base.den *= 2;
        avctx->ticks_per_frame = 2;
    }

    if (avctx->extradata_size > 0 && avctx->extradata &&
        ff_h264_decode_extradata(h, avctx->extradata, avctx->extradata_size) < 0) {
        ff_h264_free_context(h);
        return -1;
    }

    if (h->sps.bitstream_restriction_flag &&
        s->avctx->has_b_frames < h->sps.num_reorder_frames) {
        s->avctx->has_b_frames = h->sps.num_reorder_frames;
        s->low_delay           = 0;
    }

    return 0;
}

#define IN_RANGE(a, b, size) (((a) >= (b)) && ((a) < ((b) + (size))))

static void copy_picture_range(Picture **to, Picture **from, int count,
                               MpegEncContext *new_base,
                               MpegEncContext *old_base)
{
    int i;

    for (i = 0; i < count; i++) {
        assert((IN_RANGE(from[i], old_base, sizeof(*old_base)) ||
                IN_RANGE(from[i], old_base->picture,
                         sizeof(Picture) * old_base->picture_count) ||
                !from[i]));
        to[i] = REBASE_PICTURE(from[i], new_base, old_base);
    }
}

static void copy_parameter_set(void **to, void **from, int count, int size)
{
    int i;

    for (i = 0; i < count; i++) {
        if (to[i] && !from[i])
            av_freep(&to[i]);
        else if (from[i] && !to[i])
            to[i] = av_malloc(size);

        if (from[i])
            memcpy(to[i], from[i], size);
    }
}

static int decode_init_thread_copy(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;

    if (!avctx->internal->is_copy)
        return 0;
    memset(h->sps_buffers, 0, sizeof(h->sps_buffers));
    memset(h->pps_buffers, 0, sizeof(h->pps_buffers));

    return 0;
}

#define copy_fields(to, from, start_field, end_field)                   \
    memcpy(&to->start_field, &from->start_field,                        \
           (char *)&to->end_field - (char *)&to->start_field)

static int decode_update_thread_context(AVCodecContext *dst,
                                        const AVCodecContext *src)
{
    H264Context *h = dst->priv_data, *h1 = src->priv_data;
    MpegEncContext *const s = &h->s, *const s1 = &h1->s;
    int inited = s->context_initialized, err;
    int i;

    if (dst == src)
        return 0;

    err = ff_mpeg_update_thread_context(dst, src);
    if (err)
        return err;

    // FIXME handle width/height changing
    if (!inited) {
        for (i = 0; i < MAX_SPS_COUNT; i++)
            av_freep(h->sps_buffers + i);

        for (i = 0; i < MAX_PPS_COUNT; i++)
            av_freep(h->pps_buffers + i);

        // copy all fields after MpegEnc
        memcpy(&h->s + 1, &h1->s + 1,
               sizeof(H264Context) - sizeof(MpegEncContext));
        memset(h->sps_buffers, 0, sizeof(h->sps_buffers));
        memset(h->pps_buffers, 0, sizeof(h->pps_buffers));

        if (s1->context_initialized) {
        if (ff_h264_alloc_tables(h) < 0) {
            av_log(dst, AV_LOG_ERROR, "Could not allocate memory for h264\n");
            return AVERROR(ENOMEM);
        }
        context_init(h);

        /* frame_start may not be called for the next thread (if it's decoding
         * a bottom field) so this has to be allocated here */
        h->s.obmc_scratchpad = av_malloc(16 * 6 * s->linesize);
        }

        for (i = 0; i < 2; i++) {
            h->rbsp_buffer[i]      = NULL;
            h->rbsp_buffer_size[i] = 0;
        }

        h->thread_context[0] = h;

        s->dsp.clear_blocks(h->mb);
        s->dsp.clear_blocks(h->mb + (24 * 16 << h->pixel_shift));
    }

    // extradata/NAL handling
    h->is_avc = h1->is_avc;

    // SPS/PPS
    copy_parameter_set((void **)h->sps_buffers, (void **)h1->sps_buffers,
                       MAX_SPS_COUNT, sizeof(SPS));
    h->sps = h1->sps;
    copy_parameter_set((void **)h->pps_buffers, (void **)h1->pps_buffers,
                       MAX_PPS_COUNT, sizeof(PPS));
    h->pps = h1->pps;

    // Dequantization matrices
    // FIXME these are big - can they be only copied when PPS changes?
    copy_fields(h, h1, dequant4_buffer, dequant4_coeff);

    for (i = 0; i < 6; i++)
        h->dequant4_coeff[i] = h->dequant4_buffer[0] +
                               (h1->dequant4_coeff[i] - h1->dequant4_buffer[0]);

    for (i = 0; i < 6; i++)
        h->dequant8_coeff[i] = h->dequant8_buffer[0] +
                               (h1->dequant8_coeff[i] - h1->dequant8_buffer[0]);

    h->dequant_coeff_pps = h1->dequant_coeff_pps;

    // POC timing
    copy_fields(h, h1, poc_lsb, redundant_pic_count);

    // reference lists
    copy_fields(h, h1, ref_count, list_count);
    copy_fields(h, h1, ref_list, intra_gb);
    copy_fields(h, h1, short_ref, cabac_init_idc);

    copy_picture_range(h->short_ref, h1->short_ref, 32, s, s1);
    copy_picture_range(h->long_ref, h1->long_ref, 32, s, s1);
    copy_picture_range(h->delayed_pic, h1->delayed_pic,
                       MAX_DELAYED_PIC_COUNT + 2, s, s1);

    h->last_slice_type = h1->last_slice_type;
    h->sync            = h1->sync;

    if (!s->current_picture_ptr)
        return 0;

    if (!s->dropable) {
        err = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
        h->prev_poc_msb = h->poc_msb;
        h->prev_poc_lsb = h->poc_lsb;
    }
    h->prev_frame_num_offset = h->frame_num_offset;
    h->prev_frame_num        = h->frame_num;
    h->outputed_poc          = h->next_outputed_poc;

    return err;
}

int ff_h264_frame_start(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    int i;
    const int pixel_shift = h->pixel_shift;

    if (ff_MPV_frame_start(s, s->avctx) < 0)
        return -1;
    ff_er_frame_start(s);
    /*
     * ff_MPV_frame_start uses pict_type to derive key_frame.
     * This is incorrect for H.264; IDR markings must be used.
     * Zero here; IDR markings per slice in frame or fields are ORed in later.
     * See decode_nal_units().
     */
    s->current_picture_ptr->f.key_frame = 0;
    s->current_picture_ptr->sync        = 0;
    s->current_picture_ptr->mmco_reset  = 0;

    assert(s->linesize && s->uvlinesize);

    for (i = 0; i < 16; i++) {
        h->block_offset[i]           = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * s->linesize * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * s->linesize * ((scan8[i] - scan8[0]) >> 3);
    }
    for (i = 0; i < 16; i++) {
        h->block_offset[16 + i]      =
        h->block_offset[32 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * s->uvlinesize * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + 16 + i] =
        h->block_offset[48 + 32 + i] = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * s->uvlinesize * ((scan8[i] - scan8[0]) >> 3);
    }

    /* can't be in alloc_tables because linesize isn't known there.
     * FIXME: redo bipred weight to not require extra buffer? */
    for (i = 0; i < s->slice_context_count; i++)
        if (h->thread_context[i] && !h->thread_context[i]->s.obmc_scratchpad)
            h->thread_context[i]->s.obmc_scratchpad = av_malloc(16 * 6 * s->linesize);

    /* Some macroblocks can be accessed before they're available in case
     * of lost slices, MBAFF or threading. */
    memset(h->slice_table, -1,
           (s->mb_height * s->mb_stride - 1) * sizeof(*h->slice_table));

    // s->decode = (s->flags & CODEC_FLAG_PSNR) || !s->encoding ||
    //             s->current_picture.f.reference /* || h->contains_intra */ || 1;

    /* We mark the current picture as non-reference after allocating it, so
     * that if we break out due to an error it can be released automatically
     * in the next ff_MPV_frame_start().
     * SVQ3 as well as most other codecs have only last/next/current and thus
     * get released even with set reference, besides SVQ3 and others do not
     * mark frames as reference later "naturally". */
    if (s->codec_id != AV_CODEC_ID_SVQ3)
        s->current_picture_ptr->f.reference = 0;

    s->current_picture_ptr->field_poc[0]     =
        s->current_picture_ptr->field_poc[1] = INT_MAX;

    h->next_output_pic = NULL;

    assert(s->current_picture_ptr->long_ref == 0);

    return 0;
}

/**
 * Run setup operations that must be run after slice header decoding.
 * This includes finding the next displayed frame.
 *
 * @param h h264 master context
 * @param setup_finished enough NALs have been read that we can call
 * ff_thread_finish_setup()
 */
static void decode_postinit(H264Context *h, int setup_finished)
{
    MpegEncContext *const s = &h->s;
    Picture *out = s->current_picture_ptr;
    Picture *cur = s->current_picture_ptr;
    int i, pics, out_of_order, out_idx;

    s->current_picture_ptr->f.qscale_type = FF_QSCALE_TYPE_H264;
    s->current_picture_ptr->f.pict_type   = s->pict_type;

    if (h->next_output_pic)
        return;

    if (cur->field_poc[0] == INT_MAX || cur->field_poc[1] == INT_MAX) {
        /* FIXME: if we have two PAFF fields in one packet, we can't start
         * the next thread here. If we have one field per packet, we can.
         * The check in decode_nal_units() is not good enough to find this
         * yet, so we assume the worst for now. */
        // if (setup_finished)
        //    ff_thread_finish_setup(s->avctx);
        return;
    }

    cur->f.interlaced_frame = 0;
    cur->f.repeat_pict      = 0;

    /* Signal interlacing information externally. */
    /* Prioritize picture timing SEI information over used
     * decoding process if it exists. */

    if (h->sps.pic_struct_present_flag) {
        switch (h->sei_pic_struct) {
        case SEI_PIC_STRUCT_FRAME:
            break;
        case SEI_PIC_STRUCT_TOP_FIELD:
        case SEI_PIC_STRUCT_BOTTOM_FIELD:
            cur->f.interlaced_frame = 1;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM:
        case SEI_PIC_STRUCT_BOTTOM_TOP:
            if (FIELD_OR_MBAFF_PICTURE)
                cur->f.interlaced_frame = 1;
            else
                // try to flag soft telecine progressive
                cur->f.interlaced_frame = h->prev_interlaced_frame;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
        case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
            /* Signal the possibility of telecined film externally
             * (pic_struct 5,6). From these hints, let the applications
             * decide if they apply deinterlacing. */
            cur->f.repeat_pict = 1;
            break;
        case SEI_PIC_STRUCT_FRAME_DOUBLING:
            // Force progressive here, doubling interlaced frame is a bad idea.
            cur->f.repeat_pict = 2;
            break;
        case SEI_PIC_STRUCT_FRAME_TRIPLING:
            cur->f.repeat_pict = 4;
            break;
        }

        if ((h->sei_ct_type & 3) &&
            h->sei_pic_struct <= SEI_PIC_STRUCT_BOTTOM_TOP)
            cur->f.interlaced_frame = (h->sei_ct_type & (1 << 1)) != 0;
    } else {
        /* Derive interlacing flag from used decoding process. */
        cur->f.interlaced_frame = FIELD_OR_MBAFF_PICTURE;
    }
    h->prev_interlaced_frame = cur->f.interlaced_frame;

    if (cur->field_poc[0] != cur->field_poc[1]) {
        /* Derive top_field_first from field pocs. */
        cur->f.top_field_first = cur->field_poc[0] < cur->field_poc[1];
    } else {
        if (cur->f.interlaced_frame || h->sps.pic_struct_present_flag) {
            /* Use picture timing SEI information. Even if it is a
             * information of a past frame, better than nothing. */
            if (h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM ||
                h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM_TOP)
                cur->f.top_field_first = 1;
            else
                cur->f.top_field_first = 0;
        } else {
            /* Most likely progressive */
            cur->f.top_field_first = 0;
        }
    }

    cur->mmco_reset = h->mmco_reset;
    h->mmco_reset = 0;
    // FIXME do something with unavailable reference frames

    /* Sort B-frames into display order */

    if (h->sps.bitstream_restriction_flag &&
        s->avctx->has_b_frames < h->sps.num_reorder_frames) {
        s->avctx->has_b_frames = h->sps.num_reorder_frames;
        s->low_delay           = 0;
    }

    if (s->avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT &&
        !h->sps.bitstream_restriction_flag) {
        s->avctx->has_b_frames = MAX_DELAYED_PIC_COUNT - 1;
        s->low_delay           = 0;
    }

    for (i = 0; 1; i++) {
        if(i == MAX_DELAYED_PIC_COUNT || cur->poc < h->last_pocs[i]){
            if(i)
                h->last_pocs[i-1] = cur->poc;
            break;
        } else if(i) {
            h->last_pocs[i-1]= h->last_pocs[i];
        }
    }
    out_of_order = MAX_DELAYED_PIC_COUNT - i;
    if(   cur->f.pict_type == AV_PICTURE_TYPE_B
       || (h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > INT_MIN && h->last_pocs[MAX_DELAYED_PIC_COUNT-1] - h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > 2))
        out_of_order = FFMAX(out_of_order, 1);
    if(s->avctx->has_b_frames < out_of_order && !h->sps.bitstream_restriction_flag){
        av_log(s->avctx, AV_LOG_VERBOSE, "Increasing reorder buffer to %d\n", out_of_order);
        s->avctx->has_b_frames = out_of_order;
        s->low_delay = 0;
    }

    pics = 0;
    while (h->delayed_pic[pics])
        pics++;

    av_assert0(pics <= MAX_DELAYED_PIC_COUNT);

    h->delayed_pic[pics++] = cur;
    if (cur->f.reference == 0)
        cur->f.reference = DELAYED_PIC_REF;

    out = h->delayed_pic[0];
    out_idx = 0;
    for (i = 1; h->delayed_pic[i] &&
                !h->delayed_pic[i]->f.key_frame &&
                !h->delayed_pic[i]->mmco_reset;
         i++)
        if (h->delayed_pic[i]->poc < out->poc) {
            out     = h->delayed_pic[i];
            out_idx = i;
        }
    if (s->avctx->has_b_frames == 0 &&
        (h->delayed_pic[0]->f.key_frame || h->delayed_pic[0]->mmco_reset))
        h->next_outputed_poc = INT_MIN;
    out_of_order = out->poc < h->next_outputed_poc;

    if (out_of_order || pics > s->avctx->has_b_frames) {
        out->f.reference &= ~DELAYED_PIC_REF;
        // for frame threading, the owner must be the second field's thread or
        // else the first thread can release the picture and reuse it unsafely
        out->owner2       = s;
        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];
    }
    if (!out_of_order && pics > s->avctx->has_b_frames) {
        h->next_output_pic = out;
        if (out_idx == 0 && h->delayed_pic[0] && (h->delayed_pic[0]->f.key_frame || h->delayed_pic[0]->mmco_reset)) {
            h->next_outputed_poc = INT_MIN;
        } else
            h->next_outputed_poc = out->poc;
    } else {
        av_log(s->avctx, AV_LOG_DEBUG, "no picture %s\n", out_of_order ? "ooo" : "");
    }

    if (h->next_output_pic && h->next_output_pic->sync) {
        h->sync |= 2;
    }

    if (setup_finished)
        ff_thread_finish_setup(s->avctx);
}

static av_always_inline void backup_mb_border(H264Context *h, uint8_t *src_y,
                                              uint8_t *src_cb, uint8_t *src_cr,
                                              int linesize, int uvlinesize,
                                              int simple)
{
    MpegEncContext *const s = &h->s;
    uint8_t *top_border;
    int top_idx = 1;
    const int pixel_shift = h->pixel_shift;
    int chroma444 = CHROMA444;
    int chroma422 = CHROMA422;

    src_y  -= linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

    if (!simple && FRAME_MBAFF) {
        if (s->mb_y & 1) {
            if (!MB_MBAFF) {
                top_border = h->top_borders[0][s->mb_x];
                AV_COPY128(top_border, src_y + 15 * linesize);
                if (pixel_shift)
                    AV_COPY128(top_border + 16, src_y + 15 * linesize + 16);
                if (simple || !CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY)) {
                    if (chroma444) {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cb + 15 * uvlinesize + 16);
                            AV_COPY128(top_border + 64, src_cr + 15 * uvlinesize);
                            AV_COPY128(top_border + 80, src_cr + 15 * uvlinesize + 16);
                        } else {
                            AV_COPY128(top_border + 16, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 32, src_cr + 15 * uvlinesize);
                        }
                    } else if (chroma422) {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cr + 15 * uvlinesize);
                        } else {
                            AV_COPY64(top_border + 16, src_cb + 15 * uvlinesize);
                            AV_COPY64(top_border + 24, src_cr + 15 * uvlinesize);
                        }
                    } else {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 7 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cr + 7 * uvlinesize);
                        } else {
                            AV_COPY64(top_border + 16, src_cb + 7 * uvlinesize);
                            AV_COPY64(top_border + 24, src_cr + 7 * uvlinesize);
                        }
                    }
                }
            }
        } else if (MB_MBAFF) {
            top_idx = 0;
        } else
            return;
    }

    top_border = h->top_borders[top_idx][s->mb_x];
    /* There are two lines saved, the line above the top macroblock
     * of a pair, and the line above the bottom macroblock. */
    AV_COPY128(top_border, src_y + 16 * linesize);
    if (pixel_shift)
        AV_COPY128(top_border + 16, src_y + 16 * linesize + 16);

    if (simple || !CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY)) {
        if (chroma444) {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 16 * linesize);
                AV_COPY128(top_border + 48, src_cb + 16 * linesize + 16);
                AV_COPY128(top_border + 64, src_cr + 16 * linesize);
                AV_COPY128(top_border + 80, src_cr + 16 * linesize + 16);
            } else {
                AV_COPY128(top_border + 16, src_cb + 16 * linesize);
                AV_COPY128(top_border + 32, src_cr + 16 * linesize);
            }
        } else if (chroma422) {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 16 * uvlinesize);
                AV_COPY128(top_border + 48, src_cr + 16 * uvlinesize);
            } else {
                AV_COPY64(top_border + 16, src_cb + 16 * uvlinesize);
                AV_COPY64(top_border + 24, src_cr + 16 * uvlinesize);
            }
        } else {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 8 * uvlinesize);
                AV_COPY128(top_border + 48, src_cr + 8 * uvlinesize);
            } else {
                AV_COPY64(top_border + 16, src_cb + 8 * uvlinesize);
                AV_COPY64(top_border + 24, src_cr + 8 * uvlinesize);
            }
        }
    }
}

static av_always_inline void xchg_mb_border(H264Context *h, uint8_t *src_y,
                                            uint8_t *src_cb, uint8_t *src_cr,
                                            int linesize, int uvlinesize,
                                            int xchg, int chroma444,
                                            int simple, int pixel_shift)
{
    MpegEncContext *const s = &h->s;
    int deblock_topleft;
    int deblock_top;
    int top_idx = 1;
    uint8_t *top_border_m1;
    uint8_t *top_border;

    if (!simple && FRAME_MBAFF) {
        if (s->mb_y & 1) {
            if (!MB_MBAFF)
                return;
        } else {
            top_idx = MB_MBAFF ? 0 : 1;
        }
    }

    if (h->deblocking_filter == 2) {
        deblock_topleft = h->slice_table[h->mb_xy - 1 - s->mb_stride] == h->slice_num;
        deblock_top     = h->top_type;
    } else {
        deblock_topleft = (s->mb_x > 0);
        deblock_top     = (s->mb_y > !!MB_FIELD);
    }

    src_y  -= linesize   + 1 + pixel_shift;
    src_cb -= uvlinesize + 1 + pixel_shift;
    src_cr -= uvlinesize + 1 + pixel_shift;

    top_border_m1 = h->top_borders[top_idx][s->mb_x - 1];
    top_border    = h->top_borders[top_idx][s->mb_x];

#define XCHG(a, b, xchg)                        \
    if (pixel_shift) {                          \
        if (xchg) {                             \
            AV_SWAP64(b + 0, a + 0);            \
            AV_SWAP64(b + 8, a + 8);            \
        } else {                                \
            AV_COPY128(b, a);                   \
        }                                       \
    } else if (xchg)                            \
        AV_SWAP64(b, a);                        \
    else                                        \
        AV_COPY64(b, a);

    if (deblock_top) {
        if (deblock_topleft) {
            XCHG(top_border_m1 + (8 << pixel_shift),
                 src_y - (7 << pixel_shift), 1);
        }
        XCHG(top_border + (0 << pixel_shift), src_y + (1 << pixel_shift), xchg);
        XCHG(top_border + (8 << pixel_shift), src_y + (9 << pixel_shift), 1);
        if (s->mb_x + 1 < s->mb_width) {
            XCHG(h->top_borders[top_idx][s->mb_x + 1],
                 src_y + (17 << pixel_shift), 1);
        }
    }
    if (simple || !CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY)) {
        if (chroma444) {
            if (deblock_topleft) {
                XCHG(top_border_m1 + (24 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                XCHG(top_border_m1 + (40 << pixel_shift), src_cr - (7 << pixel_shift), 1);
            }
            XCHG(top_border + (16 << pixel_shift), src_cb + (1 << pixel_shift), xchg);
            XCHG(top_border + (24 << pixel_shift), src_cb + (9 << pixel_shift), 1);
            XCHG(top_border + (32 << pixel_shift), src_cr + (1 << pixel_shift), xchg);
            XCHG(top_border + (40 << pixel_shift), src_cr + (9 << pixel_shift), 1);
            if (s->mb_x + 1 < s->mb_width) {
                XCHG(h->top_borders[top_idx][s->mb_x + 1] + (16 << pixel_shift), src_cb + (17 << pixel_shift), 1);
                XCHG(h->top_borders[top_idx][s->mb_x + 1] + (32 << pixel_shift), src_cr + (17 << pixel_shift), 1);
            }
        } else {
            if (deblock_top) {
                if (deblock_topleft) {
                    XCHG(top_border_m1 + (16 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                    XCHG(top_border_m1 + (24 << pixel_shift), src_cr - (7 << pixel_shift), 1);
                }
                XCHG(top_border + (16 << pixel_shift), src_cb + 1 + pixel_shift, 1);
                XCHG(top_border + (24 << pixel_shift), src_cr + 1 + pixel_shift, 1);
            }
        }
    }
}

static av_always_inline int dctcoef_get(DCTELEM *mb, int high_bit_depth,
                                        int index)
{
    if (high_bit_depth) {
        return AV_RN32A(((int32_t *)mb) + index);
    } else
        return AV_RN16A(mb + index);
}

static av_always_inline void dctcoef_set(DCTELEM *mb, int high_bit_depth,
                                         int index, int value)
{
    if (high_bit_depth) {
        AV_WN32A(((int32_t *)mb) + index, value);
    } else
        AV_WN16A(mb + index, value);
}

static av_always_inline void hl_decode_mb_predict_luma(H264Context *h,
                                                       int mb_type, int is_h264,
                                                       int simple,
                                                       int transform_bypass,
                                                       int pixel_shift,
                                                       int *block_offset,
                                                       int linesize,
                                                       uint8_t *dest_y, int p)
{
    MpegEncContext *const s = &h->s;
    void (*idct_add)(uint8_t *dst, DCTELEM *block, int stride);
    void (*idct_dc_add)(uint8_t *dst, DCTELEM *block, int stride);
    int i;
    int qscale = p == 0 ? s->qscale : h->chroma_qp[p - 1];
    block_offset += 16 * p;
    if (IS_INTRA4x4(mb_type)) {
        if (simple || !s->encoding) {
            if (IS_8x8DCT(mb_type)) {
                if (transform_bypass) {
                    idct_dc_add  =
                    idct_add     = s->dsp.add_pixels8;
                } else {
                    idct_dc_add = h->h264dsp.h264_idct8_dc_add;
                    idct_add    = h->h264dsp.h264_idct8_add;
                }
                for (i = 0; i < 16; i += 4) {
                    uint8_t *const ptr = dest_y + block_offset[i];
                    const int dir      = h->intra4x4_pred_mode_cache[scan8[i]];
                    if (transform_bypass && h->sps.profile_idc == 244 && dir <= 1) {
                        h->hpc.pred8x8l_add[dir](ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    } else {
                        const int nnz = h->non_zero_count_cache[scan8[i + p * 16]];
                        h->hpc.pred8x8l[dir](ptr, (h->topleft_samples_available << i) & 0x8000,
                                             (h->topright_samples_available << i) & 0x4000, linesize);
                        if (nnz) {
                            if (nnz == 1 && dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                                idct_dc_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                            else
                                idct_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                        }
                    }
                }
            } else {
                if (transform_bypass) {
                    idct_dc_add  =
                        idct_add = s->dsp.add_pixels4;
                } else {
                    idct_dc_add = h->h264dsp.h264_idct_dc_add;
                    idct_add    = h->h264dsp.h264_idct_add;
                }
                for (i = 0; i < 16; i++) {
                    uint8_t *const ptr = dest_y + block_offset[i];
                    const int dir      = h->intra4x4_pred_mode_cache[scan8[i]];

                    if (transform_bypass && h->sps.profile_idc == 244 && dir <= 1) {
                        h->hpc.pred4x4_add[dir](ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    } else {
                        uint8_t *topright;
                        int nnz, tr;
                        uint64_t tr_high;
                        if (dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED) {
                            const int topright_avail = (h->topright_samples_available << i) & 0x8000;
                            assert(s->mb_y || linesize <= block_offset[i]);
                            if (!topright_avail) {
                                if (pixel_shift) {
                                    tr_high  = ((uint16_t *)ptr)[3 - linesize / 2] * 0x0001000100010001ULL;
                                    topright = (uint8_t *)&tr_high;
                                } else {
                                    tr       = ptr[3 - linesize] * 0x01010101u;
                                    topright = (uint8_t *)&tr;
                                }
                            } else
                                topright = ptr + (4 << pixel_shift) - linesize;
                        } else
                            topright = NULL;

                        h->hpc.pred4x4[dir](ptr, topright, linesize);
                        nnz = h->non_zero_count_cache[scan8[i + p * 16]];
                        if (nnz) {
                            if (is_h264) {
                                if (nnz == 1 && dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                                    idct_dc_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                                else
                                    idct_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                            } else if (CONFIG_SVQ3_DECODER)
                                ff_svq3_add_idct_c(ptr, h->mb + i * 16 + p * 256, linesize, qscale, 0);
                        }
                    }
                }
            }
        }
    } else {
        h->hpc.pred16x16[h->intra16x16_pred_mode](dest_y, linesize);
        if (is_h264) {
            if (h->non_zero_count_cache[scan8[LUMA_DC_BLOCK_INDEX + p]]) {
                if (!transform_bypass)
                    h->h264dsp.h264_luma_dc_dequant_idct(h->mb + (p * 256 << pixel_shift),
                                                         h->mb_luma_dc[p],
                                                         h->dequant4_coeff[p][qscale][0]);
                else {
                    static const uint8_t dc_mapping[16] = {
                         0 * 16,  1 * 16,  4 * 16,  5 * 16,
                         2 * 16,  3 * 16,  6 * 16,  7 * 16,
                         8 * 16,  9 * 16, 12 * 16, 13 * 16,
                        10 * 16, 11 * 16, 14 * 16, 15 * 16 };
                    for (i = 0; i < 16; i++)
                        dctcoef_set(h->mb + (p * 256 << pixel_shift),
                                    pixel_shift, dc_mapping[i],
                                    dctcoef_get(h->mb_luma_dc[p],
                                                pixel_shift, i));
                }
            }
        } else if (CONFIG_SVQ3_DECODER)
            ff_svq3_luma_dc_dequant_idct_c(h->mb + p * 256,
                                           h->mb_luma_dc[p], qscale);
    }
}

static av_always_inline void hl_decode_mb_idct_luma(H264Context *h, int mb_type,
                                                    int is_h264, int simple,
                                                    int transform_bypass,
                                                    int pixel_shift,
                                                    int *block_offset,
                                                    int linesize,
                                                    uint8_t *dest_y, int p)
{
    MpegEncContext *const s = &h->s;
    void (*idct_add)(uint8_t *dst, DCTELEM *block, int stride);
    int i;
    block_offset += 16 * p;
    if (!IS_INTRA4x4(mb_type)) {
        if (is_h264) {
            if (IS_INTRA16x16(mb_type)) {
                if (transform_bypass) {
                    if (h->sps.profile_idc == 244 &&
                        (h->intra16x16_pred_mode == VERT_PRED8x8 ||
                         h->intra16x16_pred_mode == HOR_PRED8x8)) {
                        h->hpc.pred16x16_add[h->intra16x16_pred_mode](dest_y, block_offset,
                                                                      h->mb + (p * 256 << pixel_shift),
                                                                      linesize);
                    } else {
                        for (i = 0; i < 16; i++)
                            if (h->non_zero_count_cache[scan8[i + p * 16]] ||
                                dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                                s->dsp.add_pixels4(dest_y + block_offset[i],
                                                   h->mb + (i * 16 + p * 256 << pixel_shift),
                                                   linesize);
                    }
                } else {
                    h->h264dsp.h264_idct_add16intra(dest_y, block_offset,
                                                    h->mb + (p * 256 << pixel_shift),
                                                    linesize,
                                                    h->non_zero_count_cache + p * 5 * 8);
                }
            } else if (h->cbp & 15) {
                if (transform_bypass) {
                    const int di = IS_8x8DCT(mb_type) ? 4 : 1;
                    idct_add = IS_8x8DCT(mb_type) ? s->dsp.add_pixels8
                                                  : s->dsp.add_pixels4;
                    for (i = 0; i < 16; i += di)
                        if (h->non_zero_count_cache[scan8[i + p * 16]])
                            idct_add(dest_y + block_offset[i],
                                     h->mb + (i * 16 + p * 256 << pixel_shift),
                                     linesize);
                } else {
                    if (IS_8x8DCT(mb_type))
                        h->h264dsp.h264_idct8_add4(dest_y, block_offset,
                                                   h->mb + (p * 256 << pixel_shift),
                                                   linesize,
                                                   h->non_zero_count_cache + p * 5 * 8);
                    else
                        h->h264dsp.h264_idct_add16(dest_y, block_offset,
                                                   h->mb + (p * 256 << pixel_shift),
                                                   linesize,
                                                   h->non_zero_count_cache + p * 5 * 8);
                }
            }
        } else if (CONFIG_SVQ3_DECODER) {
            for (i = 0; i < 16; i++)
                if (h->non_zero_count_cache[scan8[i + p * 16]] || h->mb[i * 16 + p * 256]) {
                    // FIXME benchmark weird rule, & below
                    uint8_t *const ptr = dest_y + block_offset[i];
                    ff_svq3_add_idct_c(ptr, h->mb + i * 16 + p * 256, linesize,
                                       s->qscale, IS_INTRA(mb_type) ? 1 : 0);
                }
        }
    }
}

#define BITS   8
#define SIMPLE 1
#include "h264_mb_template.c"

#undef  BITS
#define BITS   16
#include "h264_mb_template.c"

#undef  SIMPLE
#define SIMPLE 0
#include "h264_mb_template.c"

void ff_h264_hl_decode_mb(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    const int mb_xy   = h->mb_xy;
    const int mb_type = s->current_picture.f.mb_type[mb_xy];
    int is_complex    = CONFIG_SMALL || h->is_complex || IS_INTRA_PCM(mb_type) || s->qscale == 0;

    if (CHROMA444) {
        if (is_complex || h->pixel_shift)
            hl_decode_mb_444_complex(h);
        else
            hl_decode_mb_444_simple_8(h);
    } else if (is_complex) {
        hl_decode_mb_complex(h);
    } else if (h->pixel_shift) {
        hl_decode_mb_simple_16(h);
    } else
        hl_decode_mb_simple_8(h);
}

static int pred_weight_table(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    int list, i;
    int luma_def, chroma_def;

    h->use_weight             = 0;
    h->use_weight_chroma      = 0;
    h->luma_log2_weight_denom = get_ue_golomb(&s->gb);
    if (h->sps.chroma_format_idc)
        h->chroma_log2_weight_denom = get_ue_golomb(&s->gb);
    luma_def   = 1 << h->luma_log2_weight_denom;
    chroma_def = 1 << h->chroma_log2_weight_denom;

    for (list = 0; list < 2; list++) {
        h->luma_weight_flag[list]   = 0;
        h->chroma_weight_flag[list] = 0;
        for (i = 0; i < h->ref_count[list]; i++) {
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag = get_bits1(&s->gb);
            if (luma_weight_flag) {
                h->luma_weight[i][list][0] = get_se_golomb(&s->gb);
                h->luma_weight[i][list][1] = get_se_golomb(&s->gb);
                if (h->luma_weight[i][list][0] != luma_def ||
                    h->luma_weight[i][list][1] != 0) {
                    h->use_weight             = 1;
                    h->luma_weight_flag[list] = 1;
                }
            } else {
                h->luma_weight[i][list][0] = luma_def;
                h->luma_weight[i][list][1] = 0;
            }

            if (h->sps.chroma_format_idc) {
                chroma_weight_flag = get_bits1(&s->gb);
                if (chroma_weight_flag) {
                    int j;
                    for (j = 0; j < 2; j++) {
                        h->chroma_weight[i][list][j][0] = get_se_golomb(&s->gb);
                        h->chroma_weight[i][list][j][1] = get_se_golomb(&s->gb);
                        if (h->chroma_weight[i][list][j][0] != chroma_def ||
                            h->chroma_weight[i][list][j][1] != 0) {
                            h->use_weight_chroma = 1;
                            h->chroma_weight_flag[list] = 1;
                        }
                    }
                } else {
                    int j;
                    for (j = 0; j < 2; j++) {
                        h->chroma_weight[i][list][j][0] = chroma_def;
                        h->chroma_weight[i][list][j][1] = 0;
                    }
                }
            }
        }
        if (h->slice_type_nos != AV_PICTURE_TYPE_B)
            break;
    }
    h->use_weight = h->use_weight || h->use_weight_chroma;
    return 0;
}

/**
 * Initialize implicit_weight table.
 * @param field  0/1 initialize the weight for interlaced MBAFF
 *                -1 initializes the rest
 */
static void implicit_weight_table(H264Context *h, int field)
{
    MpegEncContext *const s = &h->s;
    int ref0, ref1, i, cur_poc, ref_start, ref_count0, ref_count1;

    for (i = 0; i < 2; i++) {
        h->luma_weight_flag[i]   = 0;
        h->chroma_weight_flag[i] = 0;
    }

    if (field < 0) {
        if (s->picture_structure == PICT_FRAME) {
            cur_poc = s->current_picture_ptr->poc;
        } else {
            cur_poc = s->current_picture_ptr->field_poc[s->picture_structure - 1];
        }
        if (h->ref_count[0] == 1 && h->ref_count[1] == 1 && !FRAME_MBAFF &&
            h->ref_list[0][0].poc + h->ref_list[1][0].poc == 2 * cur_poc) {
            h->use_weight = 0;
            h->use_weight_chroma = 0;
            return;
        }
        ref_start  = 0;
        ref_count0 = h->ref_count[0];
        ref_count1 = h->ref_count[1];
    } else {
        cur_poc    = s->current_picture_ptr->field_poc[field];
        ref_start  = 16;
        ref_count0 = 16 + 2 * h->ref_count[0];
        ref_count1 = 16 + 2 * h->ref_count[1];
    }

    h->use_weight               = 2;
    h->use_weight_chroma        = 2;
    h->luma_log2_weight_denom   = 5;
    h->chroma_log2_weight_denom = 5;

    for (ref0 = ref_start; ref0 < ref_count0; ref0++) {
        int poc0 = h->ref_list[0][ref0].poc;
        for (ref1 = ref_start; ref1 < ref_count1; ref1++) {
            int w = 32;
            if (!h->ref_list[0][ref0].long_ref && !h->ref_list[1][ref1].long_ref) {
                int poc1 = h->ref_list[1][ref1].poc;
                int td   = av_clip(poc1 - poc0, -128, 127);
                if (td) {
                    int tb = av_clip(cur_poc - poc0, -128, 127);
                    int tx = (16384 + (FFABS(td) >> 1)) / td;
                    int dist_scale_factor = (tb * tx + 32) >> 8;
                    if (dist_scale_factor >= -64 && dist_scale_factor <= 128)
                        w = 64 - dist_scale_factor;
                }
            }
            if (field < 0) {
                h->implicit_weight[ref0][ref1][0] =
                h->implicit_weight[ref0][ref1][1] = w;
            } else {
                h->implicit_weight[ref0][ref1][field] = w;
            }
        }
    }
}

/**
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h)
{
    int i;
    ff_h264_remove_all_refs(h);
    h->prev_frame_num        = 0;
    h->prev_frame_num_offset = 0;
    h->prev_poc_msb          = 1<<16;
    h->prev_poc_lsb          = 0;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
}

/* forget old pics after a seek */
static void flush_dpb(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int i;
    for (i=0; i<=MAX_DELAYED_PIC_COUNT; i++) {
        if (h->delayed_pic[i])
            h->delayed_pic[i]->f.reference = 0;
        h->delayed_pic[i] = NULL;
    }
    h->outputed_poc = h->next_outputed_poc = INT_MIN;
    h->prev_interlaced_frame = 1;
    idr(h);
    h->prev_frame_num = -1;
    if (h->s.current_picture_ptr)
        h->s.current_picture_ptr->f.reference = 0;
    h->s.first_field = 0;
    ff_h264_reset_sei(h);
    ff_mpeg_flush(avctx);
    h->recovery_frame= -1;
    h->sync= 0;
}

static int init_poc(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    const int max_frame_num = 1 << h->sps.log2_max_frame_num;
    int field_poc[2];
    Picture *cur = s->current_picture_ptr;

    h->frame_num_offset = h->prev_frame_num_offset;
    if (h->frame_num < h->prev_frame_num)
        h->frame_num_offset += max_frame_num;

    if (h->sps.poc_type == 0) {
        const int max_poc_lsb = 1 << h->sps.log2_max_poc_lsb;

        if (h->poc_lsb < h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if (h->poc_lsb > h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
        field_poc[0] =
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if (s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc_bottom;
    } else if (h->sps.poc_type == 1) {
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if (h->sps.poc_cycle_length != 0)
            abs_frame_num = h->frame_num_offset + h->frame_num;
        else
            abs_frame_num = 0;

        if (h->nal_ref_idc == 0 && abs_frame_num > 0)
            abs_frame_num--;

        expected_delta_per_poc_cycle = 0;
        for (i = 0; i < h->sps.poc_cycle_length; i++)
            // FIXME integrate during sps parse
            expected_delta_per_poc_cycle += h->sps.offset_for_ref_frame[i];

        if (abs_frame_num > 0) {
            int poc_cycle_cnt          = (abs_frame_num - 1) / h->sps.poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % h->sps.poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for (i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + h->sps.offset_for_ref_frame[i];
        } else
            expectedpoc = 0;

        if (h->nal_ref_idc == 0)
            expectedpoc = expectedpoc + h->sps.offset_for_non_ref_pic;

        field_poc[0] = expectedpoc + h->delta_poc[0];
        field_poc[1] = field_poc[0] + h->sps.offset_for_top_to_bottom_field;

        if (s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    } else {
        int poc = 2 * (h->frame_num_offset + h->frame_num);

        if (!h->nal_ref_idc)
            poc--;

        field_poc[0] = poc;
        field_poc[1] = poc;
    }

    if (s->picture_structure != PICT_BOTTOM_FIELD)
        s->current_picture_ptr->field_poc[0] = field_poc[0];
    if (s->picture_structure != PICT_TOP_FIELD)
        s->current_picture_ptr->field_poc[1] = field_poc[1];
    cur->poc = FFMIN(cur->field_poc[0], cur->field_poc[1]);

    return 0;
}

/**
 * initialize scan tables
 */
static void init_scan_tables(H264Context *h)
{
    int i;
    for (i = 0; i < 16; i++) {
#define T(x) (x >> 2) | ((x << 2) & 0xF)
        h->zigzag_scan[i] = T(zigzag_scan[i]);
        h->field_scan[i]  = T(field_scan[i]);
#undef T
    }
    for (i = 0; i < 64; i++) {
#define T(x) (x >> 3) | ((x & 7) << 3)
        h->zigzag_scan8x8[i]       = T(ff_zigzag_direct[i]);
        h->zigzag_scan8x8_cavlc[i] = T(zigzag_scan8x8_cavlc[i]);
        h->field_scan8x8[i]        = T(field_scan8x8[i]);
        h->field_scan8x8_cavlc[i]  = T(field_scan8x8_cavlc[i]);
#undef T
    }
    if (h->sps.transform_bypass) { // FIXME same ugly
        memcpy(h->zigzag_scan_q0          , zigzag_scan             , sizeof(h->zigzag_scan_q0         ));
        memcpy(h->zigzag_scan8x8_q0       , ff_zigzag_direct        , sizeof(h->zigzag_scan8x8_q0      ));
        memcpy(h->zigzag_scan8x8_cavlc_q0 , zigzag_scan8x8_cavlc    , sizeof(h->zigzag_scan8x8_cavlc_q0));
        memcpy(h->field_scan_q0           , field_scan              , sizeof(h->field_scan_q0          ));
        memcpy(h->field_scan8x8_q0        , field_scan8x8           , sizeof(h->field_scan8x8_q0       ));
        memcpy(h->field_scan8x8_cavlc_q0  , field_scan8x8_cavlc     , sizeof(h->field_scan8x8_cavlc_q0 ));
    } else {
        memcpy(h->zigzag_scan_q0          , h->zigzag_scan          , sizeof(h->zigzag_scan_q0         ));
        memcpy(h->zigzag_scan8x8_q0       , h->zigzag_scan8x8       , sizeof(h->zigzag_scan8x8_q0      ));
        memcpy(h->zigzag_scan8x8_cavlc_q0 , h->zigzag_scan8x8_cavlc , sizeof(h->zigzag_scan8x8_cavlc_q0));
        memcpy(h->field_scan_q0           , h->field_scan           , sizeof(h->field_scan_q0          ));
        memcpy(h->field_scan8x8_q0        , h->field_scan8x8        , sizeof(h->field_scan8x8_q0       ));
        memcpy(h->field_scan8x8_cavlc_q0  , h->field_scan8x8_cavlc  , sizeof(h->field_scan8x8_cavlc_q0 ));
    }
}

static int field_end(H264Context *h, int in_setup)
{
    MpegEncContext *const s     = &h->s;
    AVCodecContext *const avctx = s->avctx;
    int err = 0;
    s->mb_y = 0;

    if (!in_setup && !s->dropable)
        ff_thread_report_progress(&s->current_picture_ptr->f, INT_MAX,
                                  s->picture_structure == PICT_BOTTOM_FIELD);

    if (CONFIG_H264_VDPAU_DECODER &&
        s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_set_reference_frames(s);

    if (in_setup || !(avctx->active_thread_type & FF_THREAD_FRAME)) {
        if (!s->dropable) {
            err = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
            h->prev_poc_msb = h->poc_msb;
            h->prev_poc_lsb = h->poc_lsb;
        }
        h->prev_frame_num_offset = h->frame_num_offset;
        h->prev_frame_num        = h->frame_num;
        h->outputed_poc          = h->next_outputed_poc;
    }

    if (avctx->hwaccel) {
        if (avctx->hwaccel->end_frame(avctx) < 0)
            av_log(avctx, AV_LOG_ERROR,
                   "hardware accelerator failed to decode picture\n");
    }

    if (CONFIG_H264_VDPAU_DECODER &&
        s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_picture_complete(s);

    /*
     * FIXME: Error handling code does not seem to support interlaced
     * when slices span multiple rows
     * The ff_er_add_slice calls don't work right for bottom
     * fields; they cause massive erroneous error concealing
     * Error marking covers both fields (top and bottom).
     * This causes a mismatched s->error_count
     * and a bad error table. Further, the error count goes to
     * INT_MAX when called for bottom field, because mb_y is
     * past end by one (callers fault) and resync_mb_y != 0
     * causes problems for the first MB line, too.
     */
    if (!FIELD_PICTURE)
        ff_er_frame_end(s);

    ff_MPV_frame_end(s);

    h->current_slice = 0;

    return err;
}

/**
 * Replicate H264 "master" context to thread contexts.
 */
static void clone_slice(H264Context *dst, H264Context *src)
{
    memcpy(dst->block_offset, src->block_offset, sizeof(dst->block_offset));
    dst->s.current_picture_ptr = src->s.current_picture_ptr;
    dst->s.current_picture     = src->s.current_picture;
    dst->s.linesize            = src->s.linesize;
    dst->s.uvlinesize          = src->s.uvlinesize;
    dst->s.first_field         = src->s.first_field;

    dst->prev_poc_msb          = src->prev_poc_msb;
    dst->prev_poc_lsb          = src->prev_poc_lsb;
    dst->prev_frame_num_offset = src->prev_frame_num_offset;
    dst->prev_frame_num        = src->prev_frame_num;
    dst->short_ref_count       = src->short_ref_count;

    memcpy(dst->short_ref,        src->short_ref,        sizeof(dst->short_ref));
    memcpy(dst->long_ref,         src->long_ref,         sizeof(dst->long_ref));
    memcpy(dst->default_ref_list, src->default_ref_list, sizeof(dst->default_ref_list));
    memcpy(dst->ref_list,         src->ref_list,         sizeof(dst->ref_list));

    memcpy(dst->dequant4_coeff,   src->dequant4_coeff,   sizeof(src->dequant4_coeff));
    memcpy(dst->dequant8_coeff,   src->dequant8_coeff,   sizeof(src->dequant8_coeff));
}

/**
 * Compute profile from profile_idc and constraint_set?_flags.
 *
 * @param sps SPS
 *
 * @return profile as defined by FF_PROFILE_H264_*
 */
int ff_h264_get_profile(SPS *sps)
{
    int profile = sps->profile_idc;

    switch (sps->profile_idc) {
    case FF_PROFILE_H264_BASELINE:
        // constraint_set1_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 1) ? FF_PROFILE_H264_CONSTRAINED : 0;
        break;
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
        // constraint_set3_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 3) ? FF_PROFILE_H264_INTRA : 0;
        break;
    }

    return profile;
}

/**
 * Decode a slice header.
 * This will also call ff_MPV_common_init() and frame_start() as needed.
 *
 * @param h h264context
 * @param h0 h264 master context (differs from 'h' when doing sliced based
 *           parallel decoding)
 *
 * @return 0 if okay, <0 if an error occurred, 1 if decoding must not be multithreaded
 */
static int decode_slice_header(H264Context *h, H264Context *h0)
{
    MpegEncContext *const s  = &h->s;
    MpegEncContext *const s0 = &h0->s;
    unsigned int first_mb_in_slice;
    unsigned int pps_id;
    int num_ref_idx_active_override_flag;
    unsigned int slice_type, tmp, i, j;
    int default_ref_list_done = 0;
    int last_pic_structure, last_pic_dropable;
    int must_reinit;

    /* FIXME: 2tap qpel isn't implemented for high bit depth. */
    if ((s->avctx->flags2 & CODEC_FLAG2_FAST) &&
        !h->nal_ref_idc && !h->pixel_shift) {
        s->me.qpel_put = s->dsp.put_2tap_qpel_pixels_tab;
        s->me.qpel_avg = s->dsp.avg_2tap_qpel_pixels_tab;
    } else {
        s->me.qpel_put = s->dsp.put_h264_qpel_pixels_tab;
        s->me.qpel_avg = s->dsp.avg_h264_qpel_pixels_tab;
    }

    first_mb_in_slice = get_ue_golomb_long(&s->gb);

    if (first_mb_in_slice == 0) { // FIXME better field boundary detection
        if (h0->current_slice && FIELD_PICTURE) {
            field_end(h, 1);
        }

        h0->current_slice = 0;
        if (!s0->first_field) {
            if (s->current_picture_ptr && !s->dropable &&
                s->current_picture_ptr->owner2 == s) {
                ff_thread_report_progress(&s->current_picture_ptr->f, INT_MAX,
                                          s->picture_structure == PICT_BOTTOM_FIELD);
            }
            s->current_picture_ptr = NULL;
        }
    }

    slice_type = get_ue_golomb_31(&s->gb);
    if (slice_type > 9) {
        av_log(h->s.avctx, AV_LOG_ERROR,
               "slice type too large (%d) at %d %d\n",
               h->slice_type, s->mb_x, s->mb_y);
        return -1;
    }
    if (slice_type > 4) {
        slice_type -= 5;
        h->slice_type_fixed = 1;
    } else
        h->slice_type_fixed = 0;

    slice_type = golomb_to_pict_type[slice_type];
    if (slice_type == AV_PICTURE_TYPE_I ||
        (h0->current_slice != 0 && slice_type == h0->last_slice_type)) {
        default_ref_list_done = 1;
    }
    h->slice_type     = slice_type;
    h->slice_type_nos = slice_type & 3;

    // to make a few old functions happy, it's wrong though
    s->pict_type = h->slice_type;

    pps_id = get_ue_golomb(&s->gb);
    if (pps_id >= MAX_PPS_COUNT) {
        av_log(h->s.avctx, AV_LOG_ERROR, "pps_id %d out of range\n", pps_id);
        return -1;
    }
    if (!h0->pps_buffers[pps_id]) {
        av_log(h->s.avctx, AV_LOG_ERROR,
               "non-existing PPS %u referenced\n",
               pps_id);
        return -1;
    }
    h->pps = *h0->pps_buffers[pps_id];

    if (!h0->sps_buffers[h->pps.sps_id]) {
        av_log(h->s.avctx, AV_LOG_ERROR,
               "non-existing SPS %u referenced\n",
               h->pps.sps_id);
        return -1;
    }
    h->sps = *h0->sps_buffers[h->pps.sps_id];

    s->avctx->profile = ff_h264_get_profile(&h->sps);
    s->avctx->level   = h->sps.level_idc;
    s->avctx->refs    = h->sps.ref_frame_count;

    must_reinit = (s->context_initialized &&
                    (   16*h->sps.mb_width != s->avctx->coded_width
                     || 16*h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag) != s->avctx->coded_height
                     || s->avctx->bits_per_raw_sample != h->sps.bit_depth_luma
                     || h->cur_chroma_format_idc != h->sps.chroma_format_idc
                     || av_cmp_q(h->sps.sar, s->avctx->sample_aspect_ratio)));

    if(must_reinit && (h != h0 || (s->avctx->active_thread_type & FF_THREAD_FRAME))) {
        av_log_missing_feature(s->avctx,
                                "Width/height/bit depth/chroma idc changing with threads is", 0);
        return AVERROR_PATCHWELCOME;   // width / height changed during parallelized decoding
    }

    s->mb_width  = h->sps.mb_width;
    s->mb_height = h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag);

    h->b_stride = s->mb_width * 4;

    s->chroma_y_shift = h->sps.chroma_format_idc <= 1; // 400 uses yuv420p

    s->width  = 16 * s->mb_width;
    s->height = 16 * s->mb_height;

    if(must_reinit) {
        free_tables(h, 0);
        flush_dpb(s->avctx);
        ff_MPV_common_end(s);
        h->list_count = 0;
        h->current_slice = 0;
    }
    if (!s->context_initialized) {
        if (h != h0) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "Cannot (re-)initialize context during parallel decoding.\n");
            return -1;
        }
        if(   FFALIGN(s->avctx->width , 16                                 ) == s->width
           && FFALIGN(s->avctx->height, 16*(2 - h->sps.frame_mbs_only_flag)) == s->height
           && !h->sps.crop_right && !h->sps.crop_bottom
           && (s->avctx->width != s->width || s->avctx->height && s->height)
        ) {
            av_log(h->s.avctx, AV_LOG_DEBUG, "Using externally provided dimensions\n");
            s->avctx->coded_width  = s->width;
            s->avctx->coded_height = s->height;
        } else{
            avcodec_set_dimensions(s->avctx, s->width, s->height);
            s->avctx->width  -= (2>>CHROMA444)*FFMIN(h->sps.crop_right, (8<<CHROMA444)-1);
            s->avctx->height -= (1<<s->chroma_y_shift)*FFMIN(h->sps.crop_bottom, (16>>s->chroma_y_shift)-1) * (2 - h->sps.frame_mbs_only_flag);
        }
        s->avctx->sample_aspect_ratio = h->sps.sar;
        av_assert0(s->avctx->sample_aspect_ratio.den);

        if (s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU
            && (h->sps.bit_depth_luma != 8 ||
                h->sps.chroma_format_idc > 1)) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "VDPAU decoding does not support video "
                   "colorspace\n");
            return -1;
        }

        if (s->avctx->bits_per_raw_sample != h->sps.bit_depth_luma ||
            h->cur_chroma_format_idc != h->sps.chroma_format_idc) {
            if (h->sps.bit_depth_luma >= 8 && h->sps.bit_depth_luma <= 14 && h->sps.bit_depth_luma != 11 && h->sps.bit_depth_luma != 13 &&
                (h->sps.bit_depth_luma != 9 || !CHROMA422)) {
                s->avctx->bits_per_raw_sample = h->sps.bit_depth_luma;
                h->cur_chroma_format_idc = h->sps.chroma_format_idc;
                h->pixel_shift = h->sps.bit_depth_luma > 8;

                ff_h264dsp_init(&h->h264dsp, h->sps.bit_depth_luma, h->sps.chroma_format_idc);
                ff_h264_pred_init(&h->hpc, s->codec_id, h->sps.bit_depth_luma, h->sps.chroma_format_idc);
                s->dsp.dct_bits = h->sps.bit_depth_luma > 8 ? 32 : 16;
                ff_dsputil_init(&s->dsp, s->avctx);
            } else {
                av_log(s->avctx, AV_LOG_ERROR, "Unsupported bit depth: %d chroma_idc: %d\n",
                       h->sps.bit_depth_luma, h->sps.chroma_format_idc);
                return -1;
            }
        }

        if (h->sps.video_signal_type_present_flag) {
            s->avctx->color_range = h->sps.full_range>0 ? AVCOL_RANGE_JPEG
                                                      : AVCOL_RANGE_MPEG;
            if (h->sps.colour_description_present_flag) {
                s->avctx->color_primaries = h->sps.color_primaries;
                s->avctx->color_trc       = h->sps.color_trc;
                s->avctx->colorspace      = h->sps.colorspace;
            }
        }

        if (h->sps.timing_info_present_flag) {
            int64_t den = h->sps.time_scale;
            if (h->x264_build < 44U)
                den *= 2;
            av_reduce(&s->avctx->time_base.num, &s->avctx->time_base.den,
                      h->sps.num_units_in_tick, den, 1 << 30);
        }

        switch (h->sps.bit_depth_luma) {
        case 9:
            if (CHROMA444) {
                if (s->avctx->colorspace == AVCOL_SPC_RGB) {
                    s->avctx->pix_fmt = PIX_FMT_GBRP9;
                } else
                    s->avctx->pix_fmt = PIX_FMT_YUV444P9;
            } else if (CHROMA422)
                s->avctx->pix_fmt = PIX_FMT_YUV422P9;
            else
                s->avctx->pix_fmt = PIX_FMT_YUV420P9;
            break;
        case 10:
            if (CHROMA444) {
                if (s->avctx->colorspace == AVCOL_SPC_RGB) {
                    s->avctx->pix_fmt = PIX_FMT_GBRP10;
                } else
                    s->avctx->pix_fmt = PIX_FMT_YUV444P10;
            } else if (CHROMA422)
                s->avctx->pix_fmt = PIX_FMT_YUV422P10;
            else
                s->avctx->pix_fmt = PIX_FMT_YUV420P10;
            break;
        case 12:
            if (CHROMA444) {
                if (s->avctx->colorspace == AVCOL_SPC_RGB) {
                    s->avctx->pix_fmt = PIX_FMT_GBRP12;
                } else
                    s->avctx->pix_fmt = PIX_FMT_YUV444P12;
            } else if (CHROMA422)
                s->avctx->pix_fmt = PIX_FMT_YUV422P12;
            else
                s->avctx->pix_fmt = PIX_FMT_YUV420P12;
            break;
        case 14:
            if (CHROMA444) {
                if (s->avctx->colorspace == AVCOL_SPC_RGB) {
                    s->avctx->pix_fmt = PIX_FMT_GBRP14;
                } else
                    s->avctx->pix_fmt = PIX_FMT_YUV444P14;
            } else if (CHROMA422)
                s->avctx->pix_fmt = PIX_FMT_YUV422P14;
            else
                s->avctx->pix_fmt = PIX_FMT_YUV420P14;
            break;
        case 8:
            if (CHROMA444) {
                    s->avctx->pix_fmt = s->avctx->color_range == AVCOL_RANGE_JPEG ? PIX_FMT_YUVJ444P
                                                                                  : PIX_FMT_YUV444P;
                    if (s->avctx->colorspace == AVCOL_SPC_RGB) {
                        s->avctx->pix_fmt = PIX_FMT_GBR24P;
                        av_log(h->s.avctx, AV_LOG_DEBUG, "Detected GBR colorspace.\n");
                    } else if (s->avctx->colorspace == AVCOL_SPC_YCGCO) {
                        av_log(h->s.avctx, AV_LOG_WARNING, "Detected unsupported YCgCo colorspace.\n");
                    }
            } else if (CHROMA422) {
                s->avctx->pix_fmt = s->avctx->color_range == AVCOL_RANGE_JPEG ? PIX_FMT_YUVJ422P
                                                                              : PIX_FMT_YUV422P;
            } else {
                s->avctx->pix_fmt = s->avctx->get_format(s->avctx,
                                                         s->avctx->codec->pix_fmts ?
                                                         s->avctx->codec->pix_fmts :
                                                         s->avctx->color_range == AVCOL_RANGE_JPEG ?
                                                         hwaccel_pixfmt_list_h264_jpeg_420 :
                                                         ff_hwaccel_pixfmt_list_420);
            }
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR,
                   "Unsupported bit depth: %d\n", h->sps.bit_depth_luma);
            return AVERROR_INVALIDDATA;
        }

        s->avctx->hwaccel = ff_find_hwaccel(s->avctx->codec->id,
                                            s->avctx->pix_fmt);

        if (ff_MPV_common_init(s) < 0) {
            av_log(h->s.avctx, AV_LOG_ERROR, "ff_MPV_common_init() failed.\n");
            return -1;
        }
        s->first_field = 0;
        h->prev_interlaced_frame = 1;

        init_scan_tables(h);
        if (ff_h264_alloc_tables(h) < 0) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "Could not allocate memory for h264\n");
            return AVERROR(ENOMEM);
        }

        if (!HAVE_THREADS || !(s->avctx->active_thread_type & FF_THREAD_SLICE)) {
            if (context_init(h) < 0) {
                av_log(h->s.avctx, AV_LOG_ERROR, "context_init() failed.\n");
                return -1;
            }
        } else {
            for (i = 1; i < s->slice_context_count; i++) {
                H264Context *c;
                c = h->thread_context[i] = av_malloc(sizeof(H264Context));
                memcpy(c, h->s.thread_context[i], sizeof(MpegEncContext));
                memset(&c->s + 1, 0, sizeof(H264Context) - sizeof(MpegEncContext));
                c->h264dsp     = h->h264dsp;
                c->sps         = h->sps;
                c->pps         = h->pps;
                c->pixel_shift = h->pixel_shift;
                c->cur_chroma_format_idc = h->cur_chroma_format_idc;
                init_scan_tables(c);
                clone_tables(c, h, i);
            }

            for (i = 0; i < s->slice_context_count; i++)
                if (context_init(h->thread_context[i]) < 0) {
                    av_log(h->s.avctx, AV_LOG_ERROR,
                           "context_init() failed.\n");
                    return -1;
                }
        }
    }

    if (h == h0 && h->dequant_coeff_pps != pps_id) {
        h->dequant_coeff_pps = pps_id;
        init_dequant_tables(h);
    }

    h->frame_num = get_bits(&s->gb, h->sps.log2_max_frame_num);

    h->mb_mbaff        = 0;
    h->mb_aff_frame    = 0;
    last_pic_structure = s0->picture_structure;
    last_pic_dropable  = s->dropable;
    s->dropable        = h->nal_ref_idc == 0;
    if (h->sps.frame_mbs_only_flag) {
        s->picture_structure = PICT_FRAME;
    } else {
        if (!h->sps.direct_8x8_inference_flag && slice_type == AV_PICTURE_TYPE_B) {
            av_log(h->s.avctx, AV_LOG_ERROR, "This stream was generated by a broken encoder, invalid 8x8 inference\n");
            return -1;
        }
        if (get_bits1(&s->gb)) { // field_pic_flag
            s->picture_structure = PICT_TOP_FIELD + get_bits1(&s->gb); // bottom_field_flag
        } else {
            s->picture_structure = PICT_FRAME;
            h->mb_aff_frame      = h->sps.mb_aff;
        }
    }
    h->mb_field_decoding_flag = s->picture_structure != PICT_FRAME;

    if (h0->current_slice != 0) {
        if (last_pic_structure != s->picture_structure ||
            last_pic_dropable  != s->dropable) {
            av_log(h->s.avctx, AV_LOG_ERROR,
                   "Changing field mode (%d -> %d) between slices is not allowed\n",
                   last_pic_structure, s->picture_structure);
            s->picture_structure = last_pic_structure;
            s->dropable          = last_pic_dropable;
            return AVERROR_INVALIDDATA;
        }
    } else {
        /* Shorten frame num gaps so we don't have to allocate reference
         * frames just to throw them away */
        if (h->frame_num != h->prev_frame_num && h->prev_frame_num >= 0) {
            int unwrap_prev_frame_num = h->prev_frame_num;
            int max_frame_num         = 1 << h->sps.log2_max_frame_num;

            if (unwrap_prev_frame_num > h->frame_num)
                unwrap_prev_frame_num -= max_frame_num;

            if ((h->frame_num - unwrap_prev_frame_num) > h->sps.ref_frame_count) {
                unwrap_prev_frame_num = (h->frame_num - h->sps.ref_frame_count) - 1;
                if (unwrap_prev_frame_num < 0)
                    unwrap_prev_frame_num += max_frame_num;

                h->prev_frame_num = unwrap_prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair...
         * Here, we're using that to see if we should mark previously
         * decode frames as "finished".
         * We have to do that before the "dummy" in-between frame allocation,
         * since that can modify s->current_picture_ptr. */
        if (s0->first_field) {
            assert(s0->current_picture_ptr);
            assert(s0->current_picture_ptr->f.data[0]);
            assert(s0->current_picture_ptr->f.reference != DELAYED_PIC_REF);

            /* Mark old field/frame as completed */
            if (!last_pic_dropable && s0->current_picture_ptr->owner2 == s0) {
                ff_thread_report_progress(&s0->current_picture_ptr->f, INT_MAX,
                                          last_pic_structure == PICT_BOTTOM_FIELD);
            }

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE || s->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                if (!last_pic_dropable && last_pic_structure != PICT_FRAME) {
                    ff_thread_report_progress(&s0->current_picture_ptr->f, INT_MAX,
                                              last_pic_structure == PICT_TOP_FIELD);
                }
            } else {
                if (s0->current_picture_ptr->frame_num != h->frame_num) {
                    /* This and previous field were reference, but had
                     * different frame_nums. Consider this field first in
                     * pair. Throw away previous field except for reference
                     * purposes. */
                    if (!last_pic_dropable && last_pic_structure != PICT_FRAME) {
                        ff_thread_report_progress(&s0->current_picture_ptr->f, INT_MAX,
                                                  last_pic_structure == PICT_TOP_FIELD);
                    }
                } else {
                    /* Second field in complementary pair */
                    if (!((last_pic_structure   == PICT_TOP_FIELD &&
                           s->picture_structure == PICT_BOTTOM_FIELD) ||
                          (last_pic_structure   == PICT_BOTTOM_FIELD &&
                           s->picture_structure == PICT_TOP_FIELD))) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Invalid field mode combination %d/%d\n",
                               last_pic_structure, s->picture_structure);
                        s->picture_structure = last_pic_structure;
                        s->dropable          = last_pic_dropable;
                        return AVERROR_INVALIDDATA;
                    } else if (last_pic_dropable != s->dropable) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Cannot combine reference and non-reference fields in the same frame\n");
                        av_log_ask_for_sample(s->avctx, NULL);
                        s->picture_structure = last_pic_structure;
                        s->dropable          = last_pic_dropable;
                        return AVERROR_INVALIDDATA;
                    }

                    /* Take ownership of this buffer. Note that if another thread owned
                     * the first field of this buffer, we're not operating on that pointer,
                     * so the original thread is still responsible for reporting progress
                     * on that first field (or if that was us, we just did that above).
                     * By taking ownership, we assign responsibility to ourselves to
                     * report progress on the second field. */
                    s0->current_picture_ptr->owner2 = s0;
                }
            }
        }

        while (h->frame_num != h->prev_frame_num && h->prev_frame_num >= 0 &&
               h->frame_num != (h->prev_frame_num + 1) % (1 << h->sps.log2_max_frame_num)) {
            Picture *prev = h->short_ref_count ? h->short_ref[0] : NULL;
            av_log(h->s.avctx, AV_LOG_DEBUG, "Frame num gap %d %d\n",
                   h->frame_num, h->prev_frame_num);
            if (ff_h264_frame_start(h) < 0)
                return -1;
            h->prev_frame_num++;
            h->prev_frame_num %= 1 << h->sps.log2_max_frame_num;
            s->current_picture_ptr->frame_num = h->prev_frame_num;
            ff_thread_report_progress(&s->current_picture_ptr->f, INT_MAX, 0);
            ff_thread_report_progress(&s->current_picture_ptr->f, INT_MAX, 1);
            ff_generate_sliding_window_mmcos(h);
            if (ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index) < 0 &&
                (s->avctx->err_recognition & AV_EF_EXPLODE))
                return AVERROR_INVALIDDATA;
            /* Error concealment: if a ref is missing, copy the previous ref in its place.
             * FIXME: avoiding a memcpy would be nice, but ref handling makes many assumptions
             * about there being no actual duplicates.
             * FIXME: this doesn't copy padding for out-of-frame motion vectors.  Given we're
             * concealing a lost frame, this probably isn't noticeable by comparison, but it should
             * be fixed. */
            if (h->short_ref_count) {
                if (prev) {
                    av_image_copy(h->short_ref[0]->f.data, h->short_ref[0]->f.linesize,
                                  (const uint8_t **)prev->f.data, prev->f.linesize,
                                  s->avctx->pix_fmt, s->mb_width * 16, s->mb_height * 16);
                    h->short_ref[0]->poc = prev->poc + 2;
                }
                h->short_ref[0]->frame_num = h->prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair...
         * We're using that to see whether to continue decoding in that
         * frame, or to allocate a new one. */
        if (s0->first_field) {
            assert(s0->current_picture_ptr);
            assert(s0->current_picture_ptr->f.data[0]);
            assert(s0->current_picture_ptr->f.reference != DELAYED_PIC_REF);

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE || s->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                s0->current_picture_ptr = NULL;
                s0->first_field         = FIELD_PICTURE;
            } else {
                if (s0->current_picture_ptr->frame_num != h->frame_num) {
                    ff_thread_report_progress((AVFrame*)s0->current_picture_ptr, INT_MAX,
                                              s0->picture_structure==PICT_BOTTOM_FIELD);
                    /* This and the previous field had different frame_nums.
                     * Consider this field first in pair. Throw away previous
                     * one except for reference purposes. */
                    s0->first_field         = 1;
                    s0->current_picture_ptr = NULL;
                } else {
                    /* Second field in complementary pair */
                    s0->first_field = 0;
                }
            }
        } else {
            /* Frame or first field in a potentially complementary pair */
            s0->first_field = FIELD_PICTURE;
        }

        if (!FIELD_PICTURE || s0->first_field) {
            if (ff_h264_frame_start(h) < 0) {
                s0->first_field = 0;
                return -1;
            }
        } else {
            ff_release_unused_pictures(s, 0);
        }
    }
    if (h != h0)
        clone_slice(h, h0);

    s->current_picture_ptr->frame_num = h->frame_num; // FIXME frame_num cleanup

    assert(s->mb_num == s->mb_width * s->mb_height);
    if (first_mb_in_slice << FIELD_OR_MBAFF_PICTURE >= s->mb_num ||
        first_mb_in_slice >= s->mb_num) {
        av_log(h->s.avctx, AV_LOG_ERROR, "first_mb_in_slice overflow\n");
        return -1;
    }
    s->resync_mb_x = s->mb_x =  first_mb_in_slice % s->mb_width;
    s->resync_mb_y = s->mb_y = (first_mb_in_slice / s->mb_width) << FIELD_OR_MBAFF_PICTURE;
    if (s->picture_structure == PICT_BOTTOM_FIELD)
        s->resync_mb_y = s->mb_y = s->mb_y + 1;
    assert(s->mb_y < s->mb_height);

    if (s->picture_structure == PICT_FRAME) {
        h->curr_pic_num = h->frame_num;
        h->max_pic_num  = 1 << h->sps.log2_max_frame_num;
    } else {
        h->curr_pic_num = 2 * h->frame_num + 1;
        h->max_pic_num  = 1 << (h->sps.log2_max_frame_num + 1);
    }

    if (h->nal_unit_type == NAL_IDR_SLICE)
        get_ue_golomb(&s->gb); /* idr_pic_id */

    if (h->sps.poc_type == 0) {
        h->poc_lsb = get_bits(&s->gb, h->sps.log2_max_poc_lsb);

        if (h->pps.pic_order_present == 1 && s->picture_structure == PICT_FRAME)
            h->delta_poc_bottom = get_se_golomb(&s->gb);
    }

    if (h->sps.poc_type == 1 && !h->sps.delta_pic_order_always_zero_flag) {
        h->delta_poc[0] = get_se_golomb(&s->gb);

        if (h->pps.pic_order_present == 1 && s->picture_structure == PICT_FRAME)
            h->delta_poc[1] = get_se_golomb(&s->gb);
    }

    init_poc(h);

    if (h->pps.redundant_pic_cnt_present)
        h->redundant_pic_count = get_ue_golomb(&s->gb);

    // set defaults, might be overridden a few lines later
    h->ref_count[0] = h->pps.ref_count[0];
    h->ref_count[1] = h->pps.ref_count[1];

    if (h->slice_type_nos != AV_PICTURE_TYPE_I) {
        unsigned max[2];
        max[0] = max[1] = s->picture_structure == PICT_FRAME ? 15 : 31;

        if (h->slice_type_nos == AV_PICTURE_TYPE_B)
            h->direct_spatial_mv_pred = get_bits1(&s->gb);
        num_ref_idx_active_override_flag = get_bits1(&s->gb);

        if (num_ref_idx_active_override_flag) {
            h->ref_count[0] = get_ue_golomb(&s->gb) + 1;
            if (h->slice_type_nos == AV_PICTURE_TYPE_B)
                h->ref_count[1] = get_ue_golomb(&s->gb) + 1;
            else
                // full range is spec-ok in this case, even for frames
                max[1] = 31;
        }

        if (h->ref_count[0]-1 > max[0] || h->ref_count[1]-1 > max[1]){
            av_log(h->s.avctx, AV_LOG_ERROR, "reference overflow %u > %u or %u > %u\n", h->ref_count[0]-1, max[0], h->ref_count[1]-1, max[1]);
            h->ref_count[0] = h->ref_count[1] = 1;
            return AVERROR_INVALIDDATA;
        }

        if (h->slice_type_nos == AV_PICTURE_TYPE_B)
            h->list_count = 2;
        else
            h->list_count = 1;
    } else
        h->ref_count[1]= h->ref_count[0]= h->list_count= 0;

    if (!default_ref_list_done)
        ff_h264_fill_default_ref_list(h);

    if (h->slice_type_nos != AV_PICTURE_TYPE_I &&
        ff_h264_decode_ref_pic_list_reordering(h) < 0) {
        h->ref_count[1] = h->ref_count[0] = 0;
        return -1;
    }

    if (h->slice_type_nos != AV_PICTURE_TYPE_I) {
        s->last_picture_ptr = &h->ref_list[0][0];
        ff_copy_picture(&s->last_picture, s->last_picture_ptr);
    }
    if (h->slice_type_nos == AV_PICTURE_TYPE_B) {
        s->next_picture_ptr = &h->ref_list[1][0];
        ff_copy_picture(&s->next_picture, s->next_picture_ptr);
    }

    if ((h->pps.weighted_pred && h->slice_type_nos == AV_PICTURE_TYPE_P) ||
        (h->pps.weighted_bipred_idc == 1 &&
         h->slice_type_nos == AV_PICTURE_TYPE_B))
        pred_weight_table(h);
    else if (h->pps.weighted_bipred_idc == 2 &&
             h->slice_type_nos == AV_PICTURE_TYPE_B) {
        implicit_weight_table(h, -1);
    } else {
        h->use_weight = 0;
        for (i = 0; i < 2; i++) {
            h->luma_weight_flag[i]   = 0;
            h->chroma_weight_flag[i] = 0;
        }
    }

    if (h->nal_ref_idc && ff_h264_decode_ref_pic_marking(h0, &s->gb) < 0 &&
        (s->avctx->err_recognition & AV_EF_EXPLODE))
        return AVERROR_INVALIDDATA;

    if (FRAME_MBAFF) {
        ff_h264_fill_mbaff_ref_list(h);

        if (h->pps.weighted_bipred_idc == 2 && h->slice_type_nos == AV_PICTURE_TYPE_B) {
            implicit_weight_table(h, 0);
            implicit_weight_table(h, 1);
        }
    }

    if (h->slice_type_nos == AV_PICTURE_TYPE_B && !h->direct_spatial_mv_pred)
        ff_h264_direct_dist_scale_factor(h);
    ff_h264_direct_ref_list_init(h);

    if (h->slice_type_nos != AV_PICTURE_TYPE_I && h->pps.cabac) {
        tmp = get_ue_golomb_31(&s->gb);
        if (tmp > 2) {
            av_log(s->avctx, AV_LOG_ERROR, "cabac_init_idc overflow\n");
            return -1;
        }
        h->cabac_init_idc = tmp;
    }

    h->last_qscale_diff = 0;
    tmp = h->pps.init_qp + get_se_golomb(&s->gb);
    if (tmp > 51 + 6 * (h->sps.bit_depth_luma - 8)) {
        av_log(s->avctx, AV_LOG_ERROR, "QP %u out of range\n", tmp);
        return -1;
    }
    s->qscale       = tmp;
    h->chroma_qp[0] = get_chroma_qp(h, 0, s->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, s->qscale);
    // FIXME qscale / qp ... stuff
    if (h->slice_type == AV_PICTURE_TYPE_SP)
        get_bits1(&s->gb); /* sp_for_switch_flag */
    if (h->slice_type == AV_PICTURE_TYPE_SP ||
        h->slice_type == AV_PICTURE_TYPE_SI)
        get_se_golomb(&s->gb); /* slice_qs_delta */

    h->deblocking_filter     = 1;
    h->slice_alpha_c0_offset = 52;
    h->slice_beta_offset     = 52;
    if (h->pps.deblocking_filter_parameters_present) {
        tmp = get_ue_golomb_31(&s->gb);
        if (tmp > 2) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "deblocking_filter_idc %u out of range\n", tmp);
            return -1;
        }
        h->deblocking_filter = tmp;
        if (h->deblocking_filter < 2)
            h->deblocking_filter ^= 1;  // 1<->0

        if (h->deblocking_filter) {
            h->slice_alpha_c0_offset += get_se_golomb(&s->gb) << 1;
            h->slice_beta_offset     += get_se_golomb(&s->gb) << 1;
            if (h->slice_alpha_c0_offset > 104U ||
                h->slice_beta_offset     > 104U) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "deblocking filter parameters %d %d out of range\n",
                       h->slice_alpha_c0_offset, h->slice_beta_offset);
                return -1;
            }
        }
    }

    if (s->avctx->skip_loop_filter >= AVDISCARD_ALL ||
        (s->avctx->skip_loop_filter >= AVDISCARD_NONKEY &&
         h->slice_type_nos != AV_PICTURE_TYPE_I) ||
        (s->avctx->skip_loop_filter >= AVDISCARD_BIDIR  &&
         h->slice_type_nos == AV_PICTURE_TYPE_B) ||
        (s->avctx->skip_loop_filter >= AVDISCARD_NONREF &&
         h->nal_ref_idc == 0))
        h->deblocking_filter = 0;

    if (h->deblocking_filter == 1 && h0->max_contexts > 1) {
        if (s->avctx->flags2 & CODEC_FLAG2_FAST) {
            /* Cheat slightly for speed:
             * Do not bother to deblock across slices. */
            h->deblocking_filter = 2;
        } else {
            h0->max_contexts = 1;
            if (!h0->single_decode_warning) {
                av_log(s->avctx, AV_LOG_INFO,
                       "Cannot parallelize deblocking type 1, decoding such frames in sequential order\n");
                h0->single_decode_warning = 1;
            }
            if (h != h0) {
                av_log(h->s.avctx, AV_LOG_ERROR,
                       "Deblocking switched inside frame.\n");
                return 1;
            }
        }
    }
    h->qp_thresh = 15 + 52 -
                   FFMIN(h->slice_alpha_c0_offset, h->slice_beta_offset) -
                   FFMAX3(0,
                          h->pps.chroma_qp_index_offset[0],
                          h->pps.chroma_qp_index_offset[1]) +
                   6 * (h->sps.bit_depth_luma - 8);

    h0->last_slice_type = slice_type;
    h->slice_num = ++h0->current_slice;

    if (h->slice_num)
        h0->slice_row[(h->slice_num-1)&(MAX_SLICES-1)]= s->resync_mb_y;
    if (   h0->slice_row[h->slice_num&(MAX_SLICES-1)] + 3 >= s->resync_mb_y
        && h0->slice_row[h->slice_num&(MAX_SLICES-1)] <= s->resync_mb_y
        && h->slice_num >= MAX_SLICES) {
        //in case of ASO this check needs to be updated depending on how we decide to assign slice numbers in this case
        av_log(s->avctx, AV_LOG_WARNING, "Possibly too many slices (%d >= %d), increase MAX_SLICES and recompile if there are artifacts\n", h->slice_num, MAX_SLICES);
    }

    for (j = 0; j < 2; j++) {
        int id_list[16];
        int *ref2frm = h->ref2frm[h->slice_num & (MAX_SLICES - 1)][j];
        for (i = 0; i < 16; i++) {
            id_list[i] = 60;
            if (h->ref_list[j][i].f.data[0]) {
                int k;
                uint8_t *base = h->ref_list[j][i].f.base[0];
                for (k = 0; k < h->short_ref_count; k++)
                    if (h->short_ref[k]->f.base[0] == base) {
                        id_list[i] = k;
                        break;
                    }
                for (k = 0; k < h->long_ref_count; k++)
                    if (h->long_ref[k] && h->long_ref[k]->f.base[0] == base) {
                        id_list[i] = h->short_ref_count + k;
                        break;
                    }
            }
        }

        ref2frm[0]     =
            ref2frm[1] = -1;
        for (i = 0; i < 16; i++)
            ref2frm[i + 2] = 4 * id_list[i] +
                             (h->ref_list[j][i].f.reference & 3);
        ref2frm[18 + 0]     =
            ref2frm[18 + 1] = -1;
        for (i = 16; i < 48; i++)
            ref2frm[i + 4] = 4 * id_list[(i - 16) >> 1] +
                             (h->ref_list[j][i].f.reference & 3);
    }

    // FIXME: fix draw_edges + PAFF + frame threads
    h->emu_edge_width  = (s->flags & CODEC_FLAG_EMU_EDGE ||
                          (!h->sps.frame_mbs_only_flag &&
                           s->avctx->active_thread_type))
                         ? 0 : 16;
    h->emu_edge_height = (FRAME_MBAFF || FIELD_PICTURE) ? 0 : h->emu_edge_width;

    if (s->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(h->s.avctx, AV_LOG_DEBUG,
               "slice:%d %s mb:%d %c%s%s pps:%u frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d:%d:%d weight:%d%s %s\n",
               h->slice_num,
               (s->picture_structure == PICT_FRAME ? "F" : s->picture_structure == PICT_TOP_FIELD ? "T" : "B"),
               first_mb_in_slice,
               av_get_picture_type_char(h->slice_type),
               h->slice_type_fixed ? " fix" : "",
               h->nal_unit_type == NAL_IDR_SLICE ? " IDR" : "",
               pps_id, h->frame_num,
               s->current_picture_ptr->field_poc[0],
               s->current_picture_ptr->field_poc[1],
               h->ref_count[0], h->ref_count[1],
               s->qscale,
               h->deblocking_filter,
               h->slice_alpha_c0_offset / 2 - 26, h->slice_beta_offset / 2 - 26,
               h->use_weight,
               h->use_weight == 1 && h->use_weight_chroma ? "c" : "",
               h->slice_type == AV_PICTURE_TYPE_B ? (h->direct_spatial_mv_pred ? "SPAT" : "TEMP") : "");
    }

    return 0;
}

int ff_h264_get_slice_type(const H264Context *h)
{
    switch (h->slice_type) {
    case AV_PICTURE_TYPE_P:
        return 0;
    case AV_PICTURE_TYPE_B:
        return 1;
    case AV_PICTURE_TYPE_I:
        return 2;
    case AV_PICTURE_TYPE_SP:
        return 3;
    case AV_PICTURE_TYPE_SI:
        return 4;
    default:
        return -1;
    }
}

static av_always_inline void fill_filter_caches_inter(H264Context *h,
                                                      MpegEncContext *const s,
                                                      int mb_type, int top_xy,
                                                      int left_xy[LEFT_MBS],
                                                      int top_type,
                                                      int left_type[LEFT_MBS],
                                                      int mb_xy, int list)
{
    int b_stride = h->b_stride;
    int16_t(*mv_dst)[2] = &h->mv_cache[list][scan8[0]];
    int8_t *ref_cache = &h->ref_cache[list][scan8[0]];
    if (IS_INTER(mb_type) || IS_DIRECT(mb_type)) {
        if (USES_LIST(top_type, list)) {
            const int b_xy  = h->mb2b_xy[top_xy] + 3 * b_stride;
            const int b8_xy = 4 * top_xy + 2;
            int (*ref2frm)[64] = (void*)(h->ref2frm[h->slice_table[top_xy] & (MAX_SLICES - 1)][0] + (MB_MBAFF ? 20 : 2));
            AV_COPY128(mv_dst - 1 * 8, s->current_picture.f.motion_val[list][b_xy + 0]);
            ref_cache[0 - 1 * 8] =
            ref_cache[1 - 1 * 8] = ref2frm[list][s->current_picture.f.ref_index[list][b8_xy + 0]];
            ref_cache[2 - 1 * 8] =
            ref_cache[3 - 1 * 8] = ref2frm[list][s->current_picture.f.ref_index[list][b8_xy + 1]];
        } else {
            AV_ZERO128(mv_dst - 1 * 8);
            AV_WN32A(&ref_cache[0 - 1 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        }

        if (!IS_INTERLACED(mb_type ^ left_type[LTOP])) {
            if (USES_LIST(left_type[LTOP], list)) {
                const int b_xy  = h->mb2b_xy[left_xy[LTOP]] + 3;
                const int b8_xy = 4 * left_xy[LTOP] + 1;
                int (*ref2frm)[64] =(void*)( h->ref2frm[h->slice_table[left_xy[LTOP]] & (MAX_SLICES - 1)][0] + (MB_MBAFF ? 20 : 2));
                AV_COPY32(mv_dst - 1 +  0, s->current_picture.f.motion_val[list][b_xy + b_stride * 0]);
                AV_COPY32(mv_dst - 1 +  8, s->current_picture.f.motion_val[list][b_xy + b_stride * 1]);
                AV_COPY32(mv_dst - 1 + 16, s->current_picture.f.motion_val[list][b_xy + b_stride * 2]);
                AV_COPY32(mv_dst - 1 + 24, s->current_picture.f.motion_val[list][b_xy + b_stride * 3]);
                ref_cache[-1 +  0] =
                ref_cache[-1 +  8] = ref2frm[list][s->current_picture.f.ref_index[list][b8_xy + 2 * 0]];
                ref_cache[-1 + 16] =
                ref_cache[-1 + 24] = ref2frm[list][s->current_picture.f.ref_index[list][b8_xy + 2 * 1]];
            } else {
                AV_ZERO32(mv_dst - 1 +  0);
                AV_ZERO32(mv_dst - 1 +  8);
                AV_ZERO32(mv_dst - 1 + 16);
                AV_ZERO32(mv_dst - 1 + 24);
                ref_cache[-1 +  0] =
                ref_cache[-1 +  8] =
                ref_cache[-1 + 16] =
                ref_cache[-1 + 24] = LIST_NOT_USED;
            }
        }
    }

    if (!USES_LIST(mb_type, list)) {
        fill_rectangle(mv_dst, 4, 4, 8, pack16to32(0, 0), 4);
        AV_WN32A(&ref_cache[0 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[1 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[2 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[3 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        return;
    }

    {
        int8_t *ref = &s->current_picture.f.ref_index[list][4 * mb_xy];
        int (*ref2frm)[64] = (void*)(h->ref2frm[h->slice_num & (MAX_SLICES - 1)][0] + (MB_MBAFF ? 20 : 2));
        uint32_t ref01 = (pack16to32(ref2frm[list][ref[0]], ref2frm[list][ref[1]]) & 0x00FF00FF) * 0x0101;
        uint32_t ref23 = (pack16to32(ref2frm[list][ref[2]], ref2frm[list][ref[3]]) & 0x00FF00FF) * 0x0101;
        AV_WN32A(&ref_cache[0 * 8], ref01);
        AV_WN32A(&ref_cache[1 * 8], ref01);
        AV_WN32A(&ref_cache[2 * 8], ref23);
        AV_WN32A(&ref_cache[3 * 8], ref23);
    }

    {
        int16_t(*mv_src)[2] = &s->current_picture.f.motion_val[list][4 * s->mb_x + 4 * s->mb_y * b_stride];
        AV_COPY128(mv_dst + 8 * 0, mv_src + 0 * b_stride);
        AV_COPY128(mv_dst + 8 * 1, mv_src + 1 * b_stride);
        AV_COPY128(mv_dst + 8 * 2, mv_src + 2 * b_stride);
        AV_COPY128(mv_dst + 8 * 3, mv_src + 3 * b_stride);
    }
}

/**
 *
 * @return non zero if the loop filter can be skipped
 */
static int fill_filter_caches(H264Context *h, int mb_type)
{
    MpegEncContext *const s = &h->s;
    const int mb_xy = h->mb_xy;
    int top_xy, left_xy[LEFT_MBS];
    int top_type, left_type[LEFT_MBS];
    uint8_t *nnz;
    uint8_t *nnz_cache;

    top_xy = mb_xy - (s->mb_stride << MB_FIELD);

    /* Wow, what a mess, why didn't they simplify the interlacing & intra
     * stuff, I can't imagine that these complex rules are worth it. */

    left_xy[LBOT] = left_xy[LTOP] = mb_xy - 1;
    if (FRAME_MBAFF) {
        const int left_mb_field_flag = IS_INTERLACED(s->current_picture.f.mb_type[mb_xy - 1]);
        const int curr_mb_field_flag = IS_INTERLACED(mb_type);
        if (s->mb_y & 1) {
            if (left_mb_field_flag != curr_mb_field_flag)
                left_xy[LTOP] -= s->mb_stride;
        } else {
            if (curr_mb_field_flag)
                top_xy += s->mb_stride &
                    (((s->current_picture.f.mb_type[top_xy] >> 7) & 1) - 1);
            if (left_mb_field_flag != curr_mb_field_flag)
                left_xy[LBOT] += s->mb_stride;
        }
    }

    h->top_mb_xy        = top_xy;
    h->left_mb_xy[LTOP] = left_xy[LTOP];
    h->left_mb_xy[LBOT] = left_xy[LBOT];
    {
        /* For sufficiently low qp, filtering wouldn't do anything.
         * This is a conservative estimate: could also check beta_offset
         * and more accurate chroma_qp. */
        int qp_thresh = h->qp_thresh; // FIXME strictly we should store qp_thresh for each mb of a slice
        int qp        = s->current_picture.f.qscale_table[mb_xy];
        if (qp <= qp_thresh &&
            (left_xy[LTOP] < 0 ||
             ((qp + s->current_picture.f.qscale_table[left_xy[LTOP]] + 1) >> 1) <= qp_thresh) &&
            (top_xy < 0 ||
             ((qp + s->current_picture.f.qscale_table[top_xy] + 1) >> 1) <= qp_thresh)) {
            if (!FRAME_MBAFF)
                return 1;
            if ((left_xy[LTOP] < 0 ||
                 ((qp + s->current_picture.f.qscale_table[left_xy[LBOT]] + 1) >> 1) <= qp_thresh) &&
                (top_xy < s->mb_stride ||
                 ((qp + s->current_picture.f.qscale_table[top_xy - s->mb_stride] + 1) >> 1) <= qp_thresh))
                return 1;
        }
    }

    top_type        = s->current_picture.f.mb_type[top_xy];
    left_type[LTOP] = s->current_picture.f.mb_type[left_xy[LTOP]];
    left_type[LBOT] = s->current_picture.f.mb_type[left_xy[LBOT]];
    if (h->deblocking_filter == 2) {
        if (h->slice_table[top_xy] != h->slice_num)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] != h->slice_num)
            left_type[LTOP] = left_type[LBOT] = 0;
    } else {
        if (h->slice_table[top_xy] == 0xFFFF)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] == 0xFFFF)
            left_type[LTOP] = left_type[LBOT] = 0;
    }
    h->top_type        = top_type;
    h->left_type[LTOP] = left_type[LTOP];
    h->left_type[LBOT] = left_type[LBOT];

    if (IS_INTRA(mb_type))
        return 0;

    fill_filter_caches_inter(h, s, mb_type, top_xy, left_xy,
                             top_type, left_type, mb_xy, 0);
    if (h->list_count == 2)
        fill_filter_caches_inter(h, s, mb_type, top_xy, left_xy,
                                 top_type, left_type, mb_xy, 1);

    nnz       = h->non_zero_count[mb_xy];
    nnz_cache = h->non_zero_count_cache;
    AV_COPY32(&nnz_cache[4 + 8 * 1], &nnz[0]);
    AV_COPY32(&nnz_cache[4 + 8 * 2], &nnz[4]);
    AV_COPY32(&nnz_cache[4 + 8 * 3], &nnz[8]);
    AV_COPY32(&nnz_cache[4 + 8 * 4], &nnz[12]);
    h->cbp = h->cbp_table[mb_xy];

    if (top_type) {
        nnz = h->non_zero_count[top_xy];
        AV_COPY32(&nnz_cache[4 + 8 * 0], &nnz[3 * 4]);
    }

    if (left_type[LTOP]) {
        nnz = h->non_zero_count[left_xy[LTOP]];
        nnz_cache[3 + 8 * 1] = nnz[3 + 0 * 4];
        nnz_cache[3 + 8 * 2] = nnz[3 + 1 * 4];
        nnz_cache[3 + 8 * 3] = nnz[3 + 2 * 4];
        nnz_cache[3 + 8 * 4] = nnz[3 + 3 * 4];
    }

    /* CAVLC 8x8dct requires NNZ values for residual decoding that differ
     * from what the loop filter needs */
    if (!CABAC && h->pps.transform_8x8_mode) {
        if (IS_8x8DCT(top_type)) {
            nnz_cache[4 + 8 * 0]     =
                nnz_cache[5 + 8 * 0] = (h->cbp_table[top_xy] & 0x4000) >> 12;
            nnz_cache[6 + 8 * 0]     =
                nnz_cache[7 + 8 * 0] = (h->cbp_table[top_xy] & 0x8000) >> 12;
        }
        if (IS_8x8DCT(left_type[LTOP])) {
            nnz_cache[3 + 8 * 1]     =
                nnz_cache[3 + 8 * 2] = (h->cbp_table[left_xy[LTOP]] & 0x2000) >> 12; // FIXME check MBAFF
        }
        if (IS_8x8DCT(left_type[LBOT])) {
            nnz_cache[3 + 8 * 3]     =
                nnz_cache[3 + 8 * 4] = (h->cbp_table[left_xy[LBOT]] & 0x8000) >> 12; // FIXME check MBAFF
        }

        if (IS_8x8DCT(mb_type)) {
            nnz_cache[scan8[0]] =
            nnz_cache[scan8[1]] =
            nnz_cache[scan8[2]] =
            nnz_cache[scan8[3]] = (h->cbp & 0x1000) >> 12;

            nnz_cache[scan8[0 + 4]] =
            nnz_cache[scan8[1 + 4]] =
            nnz_cache[scan8[2 + 4]] =
            nnz_cache[scan8[3 + 4]] = (h->cbp & 0x2000) >> 12;

            nnz_cache[scan8[0 + 8]] =
            nnz_cache[scan8[1 + 8]] =
            nnz_cache[scan8[2 + 8]] =
            nnz_cache[scan8[3 + 8]] = (h->cbp & 0x4000) >> 12;

            nnz_cache[scan8[0 + 12]] =
            nnz_cache[scan8[1 + 12]] =
            nnz_cache[scan8[2 + 12]] =
            nnz_cache[scan8[3 + 12]] = (h->cbp & 0x8000) >> 12;
        }
    }

    return 0;
}

static void loop_filter(H264Context *h, int start_x, int end_x)
{
    MpegEncContext *const s = &h->s;
    uint8_t *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize, mb_x, mb_y;
    const int end_mb_y       = s->mb_y + FRAME_MBAFF;
    const int old_slice_type = h->slice_type;
    const int pixel_shift    = h->pixel_shift;
    const int block_h        = 16 >> s->chroma_y_shift;

    if (h->deblocking_filter) {
        for (mb_x = start_x; mb_x < end_x; mb_x++)
            for (mb_y = end_mb_y - FRAME_MBAFF; mb_y <= end_mb_y; mb_y++) {
                int mb_xy, mb_type;
                mb_xy         = h->mb_xy = mb_x + mb_y * s->mb_stride;
                h->slice_num  = h->slice_table[mb_xy];
                mb_type       = s->current_picture.f.mb_type[mb_xy];
                h->list_count = h->list_counts[mb_xy];

                if (FRAME_MBAFF)
                    h->mb_mbaff               =
                    h->mb_field_decoding_flag = !!IS_INTERLACED(mb_type);

                s->mb_x = mb_x;
                s->mb_y = mb_y;
                dest_y  = s->current_picture.f.data[0] +
                          ((mb_x << pixel_shift) + mb_y * s->linesize) * 16;
                dest_cb = s->current_picture.f.data[1] +
                          (mb_x << pixel_shift) * (8 << CHROMA444) +
                          mb_y * s->uvlinesize * block_h;
                dest_cr = s->current_picture.f.data[2] +
                          (mb_x << pixel_shift) * (8 << CHROMA444) +
                          mb_y * s->uvlinesize * block_h;
                // FIXME simplify above

                if (MB_FIELD) {
                    linesize   = h->mb_linesize   = s->linesize   * 2;
                    uvlinesize = h->mb_uvlinesize = s->uvlinesize * 2;
                    if (mb_y & 1) { // FIXME move out of this function?
                        dest_y  -= s->linesize   * 15;
                        dest_cb -= s->uvlinesize * (block_h - 1);
                        dest_cr -= s->uvlinesize * (block_h - 1);
                    }
                } else {
                    linesize   = h->mb_linesize   = s->linesize;
                    uvlinesize = h->mb_uvlinesize = s->uvlinesize;
                }
                backup_mb_border(h, dest_y, dest_cb, dest_cr, linesize,
                                 uvlinesize, 0);
                if (fill_filter_caches(h, mb_type))
                    continue;
                h->chroma_qp[0] = get_chroma_qp(h, 0, s->current_picture.f.qscale_table[mb_xy]);
                h->chroma_qp[1] = get_chroma_qp(h, 1, s->current_picture.f.qscale_table[mb_xy]);

                if (FRAME_MBAFF) {
                    ff_h264_filter_mb(h, mb_x, mb_y, dest_y, dest_cb, dest_cr,
                                      linesize, uvlinesize);
                } else {
                    ff_h264_filter_mb_fast(h, mb_x, mb_y, dest_y, dest_cb,
                                           dest_cr, linesize, uvlinesize);
                }
            }
    }
    h->slice_type   = old_slice_type;
    s->mb_x         = end_x;
    s->mb_y         = end_mb_y - FRAME_MBAFF;
    h->chroma_qp[0] = get_chroma_qp(h, 0, s->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, s->qscale);
}

static void predict_field_decoding_flag(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    const int mb_xy = s->mb_x + s->mb_y * s->mb_stride;
    int mb_type     = (h->slice_table[mb_xy - 1] == h->slice_num) ?
                      s->current_picture.f.mb_type[mb_xy - 1] :
                      (h->slice_table[mb_xy - s->mb_stride] == h->slice_num) ?
                      s->current_picture.f.mb_type[mb_xy - s->mb_stride] : 0;
    h->mb_mbaff     = h->mb_field_decoding_flag = IS_INTERLACED(mb_type) ? 1 : 0;
}

/**
 * Draw edges and report progress for the last MB row.
 */
static void decode_finish_row(H264Context *h)
{
    MpegEncContext *const s = &h->s;
    int top            = 16 * (s->mb_y      >> FIELD_PICTURE);
    int pic_height     = 16 *  s->mb_height >> FIELD_PICTURE;
    int height         =  16      << FRAME_MBAFF;
    int deblock_border = (16 + 4) << FRAME_MBAFF;

    if (h->deblocking_filter) {
        if ((top + height) >= pic_height)
            height += deblock_border;
        top -= deblock_border;
    }

    if (top >= pic_height || (top + height) < h->emu_edge_height)
        return;

    height = FFMIN(height, pic_height - top);
    if (top < h->emu_edge_height) {
        height = top + height;
        top    = 0;
    }

    ff_draw_horiz_band(s, top, height);

    if (s->dropable)
        return;

    ff_thread_report_progress(&s->current_picture_ptr->f, top + height - 1,
                              s->picture_structure == PICT_BOTTOM_FIELD);
}

static int decode_slice(struct AVCodecContext *avctx, void *arg)
{
    H264Context *h = *(void **)arg;
    MpegEncContext *const s = &h->s;
    const int part_mask     = s->partitioned_frame ? (ER_AC_END | ER_AC_ERROR)
                                                   : 0x7F;
    int lf_x_start = s->mb_x;

    s->mb_skip_run = -1;

    h->is_complex = FRAME_MBAFF || s->picture_structure != PICT_FRAME ||
                    s->codec_id != AV_CODEC_ID_H264 ||
                    (CONFIG_GRAY && (s->flags & CODEC_FLAG_GRAY));

    if (h->pps.cabac) {
        /* realign */
        align_get_bits(&s->gb);

        /* init cabac */
        ff_init_cabac_states();
        ff_init_cabac_decoder(&h->cabac,
                              s->gb.buffer + get_bits_count(&s->gb) / 8,
                              (get_bits_left(&s->gb) + 7) / 8);

        ff_h264_init_cabac_states(h);

        for (;;) {
            // START_TIMER
            int ret = ff_h264_decode_mb_cabac(h);
            int eos;
            // STOP_TIMER("decode_mb_cabac")

            if (ret >= 0)
                ff_h264_hl_decode_mb(h);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF) {
                s->mb_y++;

                ret = ff_h264_decode_mb_cabac(h);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h);
                s->mb_y--;
            }
            eos = get_cabac_terminate(&h->cabac);

            if ((s->workaround_bugs & FF_BUG_TRUNCATED) &&
                h->cabac.bytestream > h->cabac.bytestream_end + 2) {
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x - 1,
                                s->mb_y, ER_MB_END & part_mask);
                if (s->mb_x >= lf_x_start)
                    loop_filter(h, lf_x_start, s->mb_x + 1);
                return 0;
            }
            if (h->cabac.bytestream > h->cabac.bytestream_end + 2 )
                av_log(h->s.avctx, AV_LOG_DEBUG, "bytestream overread %td\n", h->cabac.bytestream_end - h->cabac.bytestream);
            if (ret < 0 || h->cabac.bytestream > h->cabac.bytestream_end + 4) {
                av_log(h->s.avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d, bytestream (%td)\n",
                       s->mb_x, s->mb_y,
                       h->cabac.bytestream_end - h->cabac.bytestream);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x,
                                s->mb_y, ER_MB_ERROR & part_mask);
                return -1;
            }

            if (++s->mb_x >= s->mb_width) {
                loop_filter(h, lf_x_start, s->mb_x);
                s->mb_x = lf_x_start = 0;
                decode_finish_row(h);
                ++s->mb_y;
                if (FIELD_OR_MBAFF_PICTURE) {
                    ++s->mb_y;
                    if (FRAME_MBAFF && s->mb_y < s->mb_height)
                        predict_field_decoding_flag(h);
                }
            }

            if (eos || s->mb_y >= s->mb_height) {
                tprintf(s->avctx, "slice end %d %d\n",
                        get_bits_count(&s->gb), s->gb.size_in_bits);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x - 1,
                                s->mb_y, ER_MB_END & part_mask);
                if (s->mb_x > lf_x_start)
                    loop_filter(h, lf_x_start, s->mb_x);
                return 0;
            }
        }
    } else {
        for (;;) {
            int ret = ff_h264_decode_mb_cavlc(h);

            if (ret >= 0)
                ff_h264_hl_decode_mb(h);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF) {
                s->mb_y++;
                ret = ff_h264_decode_mb_cavlc(h);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h);
                s->mb_y--;
            }

            if (ret < 0) {
                av_log(h->s.avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x,
                                s->mb_y, ER_MB_ERROR & part_mask);
                return -1;
            }

            if (++s->mb_x >= s->mb_width) {
                loop_filter(h, lf_x_start, s->mb_x);
                s->mb_x = lf_x_start = 0;
                decode_finish_row(h);
                ++s->mb_y;
                if (FIELD_OR_MBAFF_PICTURE) {
                    ++s->mb_y;
                    if (FRAME_MBAFF && s->mb_y < s->mb_height)
                        predict_field_decoding_flag(h);
                }
                if (s->mb_y >= s->mb_height) {
                    tprintf(s->avctx, "slice end %d %d\n",
                            get_bits_count(&s->gb), s->gb.size_in_bits);

                    if (   get_bits_left(&s->gb) == 0
                        || get_bits_left(&s->gb) > 0 && !(s->avctx->err_recognition & AV_EF_AGGRESSIVE)) {
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y,
                                        s->mb_x - 1, s->mb_y,
                                        ER_MB_END & part_mask);

                        return 0;
                    } else {
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y,
                                        s->mb_x, s->mb_y,
                                        ER_MB_END & part_mask);

                        return -1;
                    }
                }
            }

            if (get_bits_left(&s->gb) <= 0 && s->mb_skip_run <= 0) {
                tprintf(s->avctx, "slice end %d %d\n",
                        get_bits_count(&s->gb), s->gb.size_in_bits);
                if (get_bits_left(&s->gb) == 0) {
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y,
                                    s->mb_x - 1, s->mb_y,
                                    ER_MB_END & part_mask);
                    if (s->mb_x > lf_x_start)
                        loop_filter(h, lf_x_start, s->mb_x);

                    return 0;
                } else {
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x,
                                    s->mb_y, ER_MB_ERROR & part_mask);

                    return -1;
                }
            }
        }
    }
}

/**
 * Call decode_slice() for each context.
 *
 * @param h h264 master context
 * @param context_count number of contexts to execute
 */
static int execute_decode_slices(H264Context *h, int context_count)
{
    MpegEncContext *const s     = &h->s;
    AVCodecContext *const avctx = s->avctx;
    H264Context *hx;
    int i;

    if (s->avctx->hwaccel ||
        s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        return 0;
    if (context_count == 1) {
        return decode_slice(avctx, &h);
    } else {
        for (i = 1; i < context_count; i++) {
            hx                    = h->thread_context[i];
            hx->s.err_recognition = avctx->err_recognition;
            hx->s.error_count     = 0;
            hx->x264_build        = h->x264_build;
        }

        avctx->execute(avctx, decode_slice, h->thread_context,
                       NULL, context_count, sizeof(void *));

        /* pull back stuff from slices to master context */
        hx                   = h->thread_context[context_count - 1];
        s->mb_x              = hx->s.mb_x;
        s->mb_y              = hx->s.mb_y;
        s->dropable          = hx->s.dropable;
        s->picture_structure = hx->s.picture_structure;
        for (i = 1; i < context_count; i++)
            h->s.error_count += h->thread_context[i]->s.error_count;
    }

    return 0;
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size)
{
    MpegEncContext *const s     = &h->s;
    AVCodecContext *const avctx = s->avctx;
    H264Context *hx; ///< thread context
    int buf_index;
    int context_count;
    int next_avc;
    int pass = !(avctx->active_thread_type & FF_THREAD_FRAME);
    int nals_needed = 0; ///< number of NALs that need decoding before the next frame thread starts
    int nal_index;

    h->nal_unit_type= 0;

    if(!s->slice_context_count)
         s->slice_context_count= 1;
    h->max_contexts = s->slice_context_count;
    if (!(s->flags2 & CODEC_FLAG2_CHUNKS)) {
        h->current_slice = 0;
        if (!s->first_field)
            s->current_picture_ptr = NULL;
        ff_h264_reset_sei(h);
    }

    for (; pass <= 1; pass++) {
        buf_index     = 0;
        context_count = 0;
        next_avc      = h->is_avc ? 0 : buf_size;
        nal_index     = 0;
        for (;;) {
            int consumed;
            int dst_length;
            int bit_length;
            const uint8_t *ptr;
            int i, nalsize = 0;
            int err;

            if (buf_index >= next_avc) {
                if (buf_index >= buf_size - h->nal_length_size)
                    break;
                nalsize = 0;
                for (i = 0; i < h->nal_length_size; i++)
                    nalsize = (nalsize << 8) | buf[buf_index++];
                if (nalsize <= 0 || nalsize > buf_size - buf_index) {
                    av_log(h->s.avctx, AV_LOG_ERROR,
                           "AVC: nal size %d\n", nalsize);
                    break;
                }
                next_avc = buf_index + nalsize;
            } else {
                // start code prefix search
                for (; buf_index + 3 < next_avc; buf_index++)
                    // This should always succeed in the first iteration.
                    if (buf[buf_index]     == 0 &&
                        buf[buf_index + 1] == 0 &&
                        buf[buf_index + 2] == 1)
                        break;

                if (buf_index + 3 >= buf_size) {
                    buf_index = buf_size;
                    break;
                }

                buf_index += 3;
                if (buf_index >= next_avc)
                    continue;
            }

            hx = h->thread_context[context_count];

            ptr = ff_h264_decode_nal(hx, buf + buf_index, &dst_length,
                                     &consumed, next_avc - buf_index);
            if (ptr == NULL || dst_length < 0) {
                buf_index = -1;
                goto end;
            }
            i = buf_index + consumed;
            if ((s->workaround_bugs & FF_BUG_AUTODETECT) && i + 3 < next_avc &&
                buf[i]     == 0x00 && buf[i + 1] == 0x00 &&
                buf[i + 2] == 0x01 && buf[i + 3] == 0xE0)
                s->workaround_bugs |= FF_BUG_TRUNCATED;

            if (!(s->workaround_bugs & FF_BUG_TRUNCATED))
                while(dst_length > 0 && ptr[dst_length - 1] == 0)
                    dst_length--;
            bit_length = !dst_length ? 0
                                     : (8 * dst_length -
                                        decode_rbsp_trailing(h, ptr + dst_length - 1));

            if (s->avctx->debug & FF_DEBUG_STARTCODE)
                av_log(h->s.avctx, AV_LOG_DEBUG, "NAL %d/%d at %d/%d length %d pass %d\n", hx->nal_unit_type, hx->nal_ref_idc, buf_index, buf_size, dst_length, pass);

            if (h->is_avc && (nalsize != consumed) && nalsize)
                av_log(h->s.avctx, AV_LOG_DEBUG,
                       "AVC: Consumed only %d bytes instead of %d\n",
                       consumed, nalsize);

            buf_index += consumed;
            nal_index++;

            if (pass == 0) {
                /* packets can sometimes contain multiple PPS/SPS,
                 * e.g. two PAFF field pictures in one packet, or a demuxer
                 * which splits NALs strangely if so, when frame threading we
                 * can't start the next thread until we've read all of them */
                switch (hx->nal_unit_type) {
                case NAL_SPS:
                case NAL_PPS:
                    nals_needed = nal_index;
                    break;
                case NAL_IDR_SLICE:
                case NAL_SLICE:
                    init_get_bits(&hx->s.gb, ptr, bit_length);
                    if (!get_ue_golomb(&hx->s.gb))
                        nals_needed = nal_index;
                }
                continue;
            }

            // FIXME do not discard SEI id
            if (avctx->skip_frame >= AVDISCARD_NONREF && h->nal_ref_idc == 0)
                continue;

again:
            err = 0;
            switch (hx->nal_unit_type) {
            case NAL_IDR_SLICE:
                if (h->nal_unit_type != NAL_IDR_SLICE) {
                    av_log(h->s.avctx, AV_LOG_ERROR,
                           "Invalid mix of idr and non-idr slices\n");
                    buf_index = -1;
                    goto end;
                }
                idr(h); // FIXME ensure we don't lose some frames if there is reordering
            case NAL_SLICE:
                init_get_bits(&hx->s.gb, ptr, bit_length);
                hx->intra_gb_ptr        =
                    hx->inter_gb_ptr    = &hx->s.gb;
                hx->s.data_partitioning = 0;

                if ((err = decode_slice_header(hx, h)))
                    break;

                if (h->sei_recovery_frame_cnt >= 0 && (h->frame_num != h->sei_recovery_frame_cnt || hx->slice_type_nos != AV_PICTURE_TYPE_I))
                    h->valid_recovery_point = 1;

                if (   h->sei_recovery_frame_cnt >= 0
                    && (   h->recovery_frame<0
                        || ((h->recovery_frame - h->frame_num) & ((1 << h->sps.log2_max_frame_num)-1)) > h->sei_recovery_frame_cnt)) {
                    h->recovery_frame = (h->frame_num + h->sei_recovery_frame_cnt) %
                                        (1 << h->sps.log2_max_frame_num);

                    if (!h->valid_recovery_point)
                        h->recovery_frame = h->frame_num;
                }

                s->current_picture_ptr->f.key_frame |=
                        (hx->nal_unit_type == NAL_IDR_SLICE);

                if (h->recovery_frame == h->frame_num) {
                    s->current_picture_ptr->sync |= 1;
                    h->recovery_frame = -1;
                }

                h->sync |= !!s->current_picture_ptr->f.key_frame;
                h->sync |= 3*!!(s->flags2 & CODEC_FLAG2_SHOW_ALL);
                s->current_picture_ptr->sync |= h->sync;

                if (h->current_slice == 1) {
                    if (!(s->flags2 & CODEC_FLAG2_CHUNKS))
                        decode_postinit(h, nal_index >= nals_needed);

                    if (s->avctx->hwaccel &&
                        s->avctx->hwaccel->start_frame(s->avctx, NULL, 0) < 0)
                        return -1;
                    if (CONFIG_H264_VDPAU_DECODER &&
                        s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
                        ff_vdpau_h264_picture_start(s);
                }

                if (hx->redundant_pic_count == 0 &&
                    (avctx->skip_frame < AVDISCARD_NONREF ||
                     hx->nal_ref_idc) &&
                    (avctx->skip_frame < AVDISCARD_BIDIR  ||
                     hx->slice_type_nos != AV_PICTURE_TYPE_B) &&
                    (avctx->skip_frame < AVDISCARD_NONKEY ||
                     hx->slice_type_nos == AV_PICTURE_TYPE_I) &&
                    avctx->skip_frame < AVDISCARD_ALL) {
                    if (avctx->hwaccel) {
                        if (avctx->hwaccel->decode_slice(avctx,
                                                         &buf[buf_index - consumed],
                                                         consumed) < 0)
                            return -1;
                    } else if (CONFIG_H264_VDPAU_DECODER &&
                               s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU) {
                        static const uint8_t start_code[] = {
                            0x00, 0x00, 0x01 };
                        ff_vdpau_add_data_chunk(s, start_code,
                                                sizeof(start_code));
                        ff_vdpau_add_data_chunk(s, &buf[buf_index - consumed],
                                                consumed);
                    } else
                        context_count++;
                }
                break;
            case NAL_DPA:
                init_get_bits(&hx->s.gb, ptr, bit_length);
                hx->intra_gb_ptr =
                hx->inter_gb_ptr = NULL;

                if ((err = decode_slice_header(hx, h)) < 0)
                    break;

                hx->s.data_partitioning = 1;
                break;
            case NAL_DPB:
                init_get_bits(&hx->intra_gb, ptr, bit_length);
                hx->intra_gb_ptr = &hx->intra_gb;
                break;
            case NAL_DPC:
                init_get_bits(&hx->inter_gb, ptr, bit_length);
                hx->inter_gb_ptr = &hx->inter_gb;

                av_log(h->s.avctx, AV_LOG_ERROR, "Partitioned H.264 support is incomplete\n");
                return AVERROR_PATCHWELCOME;

                if (hx->redundant_pic_count == 0 &&
                    hx->intra_gb_ptr &&
                    hx->s.data_partitioning &&
                    s->context_initialized &&
                    (avctx->skip_frame < AVDISCARD_NONREF || hx->nal_ref_idc) &&
                    (avctx->skip_frame < AVDISCARD_BIDIR  ||
                     hx->slice_type_nos != AV_PICTURE_TYPE_B) &&
                    (avctx->skip_frame < AVDISCARD_NONKEY ||
                     hx->slice_type_nos == AV_PICTURE_TYPE_I) &&
                    avctx->skip_frame < AVDISCARD_ALL)
                    context_count++;
                break;
            case NAL_SEI:
                init_get_bits(&s->gb, ptr, bit_length);
                ff_h264_decode_sei(h);
                break;
            case NAL_SPS:
                init_get_bits(&s->gb, ptr, bit_length);
                if (ff_h264_decode_seq_parameter_set(h) < 0 && (h->is_avc ? (nalsize != consumed) && nalsize : 1)) {
                    av_log(h->s.avctx, AV_LOG_DEBUG,
                           "SPS decoding failure, trying again with the complete NAL\n");
                    if (h->is_avc)
                        av_assert0(next_avc - buf_index + consumed == nalsize);
                    init_get_bits(&s->gb, &buf[buf_index + 1 - consumed],
                                  8*(next_avc - buf_index + consumed - 1));
                    ff_h264_decode_seq_parameter_set(h);
                }

                if (s->flags & CODEC_FLAG_LOW_DELAY ||
                    (h->sps.bitstream_restriction_flag &&
                     !h->sps.num_reorder_frames))
                    s->low_delay = 1;
                if (avctx->has_b_frames < 2)
                    avctx->has_b_frames = !s->low_delay;
                break;
            case NAL_PPS:
                init_get_bits(&s->gb, ptr, bit_length);
                ff_h264_decode_picture_parameter_set(h, bit_length);
                break;
            case NAL_AUD:
            case NAL_END_SEQUENCE:
            case NAL_END_STREAM:
            case NAL_FILLER_DATA:
            case NAL_SPS_EXT:
            case NAL_AUXILIARY_SLICE:
                break;
            default:
                av_log(avctx, AV_LOG_DEBUG, "Unknown NAL code: %d (%d bits)\n",
                       hx->nal_unit_type, bit_length);
            }

            if (context_count == h->max_contexts) {
                execute_decode_slices(h, context_count);
                context_count = 0;
            }

            if (err < 0)
                av_log(h->s.avctx, AV_LOG_ERROR, "decode_slice_header error\n");
            else if (err == 1) {
                /* Slice could not be decoded in parallel mode, copy down
                 * NAL unit stuff to context 0 and restart. Note that
                 * rbsp_buffer is not transferred, but since we no longer
                 * run in parallel mode this should not be an issue. */
                h->nal_unit_type = hx->nal_unit_type;
                h->nal_ref_idc   = hx->nal_ref_idc;
                hx               = h;
                goto again;
            }
        }
    }
    if (context_count)
        execute_decode_slices(h, context_count);

end:
    /* clean up */
    if (s->current_picture_ptr && s->current_picture_ptr->owner2 == s &&
        !s->dropable) {
        ff_thread_report_progress(&s->current_picture_ptr->f, INT_MAX,
                                  s->picture_structure == PICT_BOTTOM_FIELD);
    }

    return buf_index;
}

/**
 * Return the number of bytes consumed for building the current frame.
 */
static int get_consumed_bytes(MpegEncContext *s, int pos, int buf_size)
{
    if (pos == 0)
        pos = 1;          // avoid infinite loops (i doubt that is needed but ...)
    if (pos + 10 > buf_size)
        pos = buf_size;                   // oops ;)

    return pos;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    H264Context *h     = avctx->priv_data;
    MpegEncContext *s  = &h->s;
    AVFrame *pict      = data;
    int buf_index      = 0;
    Picture *out;
    int i, out_idx;

    s->flags  = avctx->flags;
    s->flags2 = avctx->flags2;

    /* end of stream, output what is still in the buffers */
    if (buf_size == 0) {
 out:

        s->current_picture_ptr = NULL;

        // FIXME factorize this with the output code below
        out     = h->delayed_pic[0];
        out_idx = 0;
        for (i = 1;
             h->delayed_pic[i] &&
             !h->delayed_pic[i]->f.key_frame &&
             !h->delayed_pic[i]->mmco_reset;
             i++)
            if (h->delayed_pic[i]->poc < out->poc) {
                out     = h->delayed_pic[i];
                out_idx = i;
            }

        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];

        if (out) {
            *data_size = sizeof(AVFrame);
            *pict      = out->f;
        }

        return buf_index;
    }
    if(h->is_avc && buf_size >= 9 && buf[0]==1 && buf[2]==0 && (buf[4]&0xFC)==0xFC && (buf[5]&0x1F) && buf[8]==0x67){
        int cnt= buf[5]&0x1f;
        const uint8_t *p= buf+6;
        while(cnt--){
            int nalsize= AV_RB16(p) + 2;
            if(nalsize > buf_size - (p-buf) || p[2]!=0x67)
                goto not_extra;
            p += nalsize;
        }
        cnt = *(p++);
        if(!cnt)
            goto not_extra;
        while(cnt--){
            int nalsize= AV_RB16(p) + 2;
            if(nalsize > buf_size - (p-buf) || p[2]!=0x68)
                goto not_extra;
            p += nalsize;
        }

        return ff_h264_decode_extradata(h, buf, buf_size);
    }
not_extra:

    buf_index = decode_nal_units(h, buf, buf_size);
    if (buf_index < 0)
        return -1;

    if (!s->current_picture_ptr && h->nal_unit_type == NAL_END_SEQUENCE) {
        av_assert0(buf_index <= buf_size);
        goto out;
    }

    if (!(s->flags2 & CODEC_FLAG2_CHUNKS) && !s->current_picture_ptr) {
        if (avctx->skip_frame >= AVDISCARD_NONREF ||
            buf_size >= 4 && !memcmp("Q264", buf, 4))
            return buf_size;
        av_log(avctx, AV_LOG_ERROR, "no frame!\n");
        return -1;
    }

    if (!(s->flags2 & CODEC_FLAG2_CHUNKS) ||
        (s->mb_y >= s->mb_height && s->mb_height)) {
        if (s->flags2 & CODEC_FLAG2_CHUNKS)
            decode_postinit(h, 1);

        field_end(h, 0);

        /* Wait for second field. */
        *data_size = 0;
        if (h->next_output_pic && (h->next_output_pic->sync || h->sync>1)) {
            *data_size = sizeof(AVFrame);
            *pict      = h->next_output_pic->f;
        }
    }

    assert(pict->data[0] || !*data_size);
    ff_print_debug_info(s, pict);

    return get_consumed_bytes(s, buf_index, buf_size);
}

av_cold void ff_h264_free_context(H264Context *h)
{
    int i;

    free_tables(h, 1); // FIXME cleanup init stuff perhaps

    for (i = 0; i < MAX_SPS_COUNT; i++)
        av_freep(h->sps_buffers + i);

    for (i = 0; i < MAX_PPS_COUNT; i++)
        av_freep(h->pps_buffers + i);
}

static av_cold int h264_decode_end(AVCodecContext *avctx)
{
    H264Context *h    = avctx->priv_data;
    MpegEncContext *s = &h->s;

    ff_h264_remove_all_refs(h);
    ff_h264_free_context(h);

    ff_MPV_common_end(s);

    // memset(h, 0, sizeof(H264Context));

    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_H264_BASELINE,             "Baseline"              },
    { FF_PROFILE_H264_CONSTRAINED_BASELINE, "Constrained Baseline"  },
    { FF_PROFILE_H264_MAIN,                 "Main"                  },
    { FF_PROFILE_H264_EXTENDED,             "Extended"              },
    { FF_PROFILE_H264_HIGH,                 "High"                  },
    { FF_PROFILE_H264_HIGH_10,              "High 10"               },
    { FF_PROFILE_H264_HIGH_10_INTRA,        "High 10 Intra"         },
    { FF_PROFILE_H264_HIGH_422,             "High 4:2:2"            },
    { FF_PROFILE_H264_HIGH_422_INTRA,       "High 4:2:2 Intra"      },
    { FF_PROFILE_H264_HIGH_444,             "High 4:4:4"            },
    { FF_PROFILE_H264_HIGH_444_PREDICTIVE,  "High 4:4:4 Predictive" },
    { FF_PROFILE_H264_HIGH_444_INTRA,       "High 4:4:4 Intra"      },
    { FF_PROFILE_H264_CAVLC_444,            "CAVLC 4:4:4"           },
    { FF_PROFILE_UNKNOWN },
};

static const AVOption h264_options[] = {
    {"is_avc", "is avc", offsetof(H264Context, is_avc), FF_OPT_TYPE_INT, {.dbl = 0}, 0, 1, 0},
    {"nal_length_size", "nal_length_size", offsetof(H264Context, nal_length_size), FF_OPT_TYPE_INT, {.dbl = 0}, 0, 4, 0},
    {NULL}
};

static const AVClass h264_class = {
    "H264 Decoder",
    av_default_item_name,
    h264_options,
    LIBAVUTIL_VERSION_INT,
};

static const AVClass h264_vdpau_class = {
    "H264 VDPAU Decoder",
    av_default_item_name,
    h264_options,
    LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_decoder = {
    .name                  = "h264",
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_H264,
    .priv_data_size        = sizeof(H264Context),
    .init                  = ff_h264_decode_init,
    .close                 = h264_decode_end,
    .decode                = decode_frame,
    .capabilities          = /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 |
                             CODEC_CAP_DELAY | CODEC_CAP_SLICE_THREADS |
                             CODEC_CAP_FRAME_THREADS,
    .flush                 = flush_dpb,
    .long_name             = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(decode_update_thread_context),
    .profiles              = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class            = &h264_class,
};

#if CONFIG_H264_VDPAU_DECODER
AVCodec ff_h264_vdpau_decoder = {
    .name           = "h264_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(H264Context),
    .init           = ff_h264_decode_init,
    .close          = h264_decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    .flush          = flush_dpb,
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (VDPAU acceleration)"),
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_VDPAU_H264,
                                                   PIX_FMT_NONE},
    .profiles       = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class     = &h264_vdpau_class,
};
#endif
