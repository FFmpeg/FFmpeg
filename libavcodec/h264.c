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

#include "libavutil/avassert.h"
#include "libavutil/display.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timer.h"
#include "internal.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "h264.h"
#include "h264data.h"
#include "h264chroma.h"
#include "h264_mvpred.h"
#include "golomb.h"
#include "mathops.h"
#include "me_cmp.h"
#include "mpegutils.h"
#include "profiles.h"
#include "rectangle.h"
#include "svq3.h"
#include "thread.h"
#include "vdpau_compat.h"

static int h264_decode_end(AVCodecContext *avctx);

const uint16_t ff_h264_mb_sizes[4] = { 256, 384, 512, 768 };

int avpriv_h264_has_num_reorder_frames(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    return h ? h->sps.num_reorder_frames : 0;
}

static void h264_er_decode_mb(void *opaque, int ref, int mv_dir, int mv_type,
                              int (*mv)[2][4][2],
                              int mb_x, int mb_y, int mb_intra, int mb_skipped)
{
    H264Context *h = opaque;
    H264SliceContext *sl = &h->slice_ctx[0];

    sl->mb_x = mb_x;
    sl->mb_y = mb_y;
    sl->mb_xy = mb_x + mb_y * h->mb_stride;
    memset(sl->non_zero_count_cache, 0, sizeof(sl->non_zero_count_cache));
    av_assert1(ref >= 0);
    /* FIXME: It is possible albeit uncommon that slice references
     * differ between slices. We take the easy approach and ignore
     * it for now. If this turns out to have any relevance in
     * practice then correct remapping should be added. */
    if (ref >= sl->ref_count[0])
        ref = 0;
    if (!sl->ref_list[0][ref].data[0]) {
        av_log(h->avctx, AV_LOG_DEBUG, "Reference not available for error concealing\n");
        ref = 0;
    }
    if ((sl->ref_list[0][ref].reference&3) != 3) {
        av_log(h->avctx, AV_LOG_DEBUG, "Reference invalid\n");
        return;
    }
    fill_rectangle(&h->cur_pic.ref_index[0][4 * sl->mb_xy],
                   2, 2, 2, ref, 1);
    fill_rectangle(&sl->ref_cache[0][scan8[0]], 4, 4, 8, ref, 1);
    fill_rectangle(sl->mv_cache[0][scan8[0]], 4, 4, 8,
                   pack16to32((*mv)[0][0][0], (*mv)[0][0][1]), 4);
    sl->mb_mbaff =
    sl->mb_field_decoding_flag = 0;
    ff_h264_hl_decode_mb(h, &h->slice_ctx[0]);
}

void ff_h264_draw_horiz_band(const H264Context *h, H264SliceContext *sl,
                             int y, int height)
{
    AVCodecContext *avctx = h->avctx;
    const AVFrame   *src  = h->cur_pic.f;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    int vshift = desc->log2_chroma_h;
    const int field_pic = h->picture_structure != PICT_FRAME;
    if (field_pic) {
        height <<= 1;
        y      <<= 1;
    }

    height = FFMIN(height, avctx->height - y);

    if (field_pic && h->first_field && !(avctx->slice_flags & SLICE_FLAG_ALLOW_FIELD))
        return;

    if (avctx->draw_horiz_band) {
        int offset[AV_NUM_DATA_POINTERS];
        int i;

        offset[0] = y * src->linesize[0];
        offset[1] =
        offset[2] = (y >> vshift) * src->linesize[1];
        for (i = 3; i < AV_NUM_DATA_POINTERS; i++)
            offset[i] = 0;

        emms_c();

        avctx->draw_horiz_band(avctx, src, offset,
                               y, h->picture_structure, height);
    }
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(const H264Context *h, H264SliceContext *sl)
{
    static const int8_t top[12] = {
        -1, 0, LEFT_DC_PRED, -1, -1, -1, -1, -1, 0
    };
    static const int8_t left[12] = {
        0, -1, TOP_DC_PRED, 0, -1, -1, -1, 0, -1, DC_128_PRED
    };
    int i;

    if (!(sl->top_samples_available & 0x8000)) {
        for (i = 0; i < 4; i++) {
            int status = top[sl->intra4x4_pred_mode_cache[scan8[0] + i]];
            if (status < 0) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "top block unavailable for requested intra4x4 mode %d at %d %d\n",
                       status, sl->mb_x, sl->mb_y);
                return AVERROR_INVALIDDATA;
            } else if (status) {
                sl->intra4x4_pred_mode_cache[scan8[0] + i] = status;
            }
        }
    }

    if ((sl->left_samples_available & 0x8888) != 0x8888) {
        static const int mask[4] = { 0x8000, 0x2000, 0x80, 0x20 };
        for (i = 0; i < 4; i++)
            if (!(sl->left_samples_available & mask[i])) {
                int status = left[sl->intra4x4_pred_mode_cache[scan8[0] + 8 * i]];
                if (status < 0) {
                    av_log(h->avctx, AV_LOG_ERROR,
                           "left block unavailable for requested intra4x4 mode %d at %d %d\n",
                           status, sl->mb_x, sl->mb_y);
                    return AVERROR_INVALIDDATA;
                } else if (status) {
                    sl->intra4x4_pred_mode_cache[scan8[0] + 8 * i] = status;
                }
            }
    }

    return 0;
} // FIXME cleanup like ff_h264_check_intra_pred_mode

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(const H264Context *h, H264SliceContext *sl,
                                  int mode, int is_chroma)
{
    static const int8_t top[4]  = { LEFT_DC_PRED8x8, 1, -1, -1 };
    static const int8_t left[5] = { TOP_DC_PRED8x8, -1,  2, -1, DC_128_PRED8x8 };

    if (mode > 3U) {
        av_log(h->avctx, AV_LOG_ERROR,
               "out of range intra chroma pred mode at %d %d\n",
               sl->mb_x, sl->mb_y);
        return AVERROR_INVALIDDATA;
    }

    if (!(sl->top_samples_available & 0x8000)) {
        mode = top[mode];
        if (mode < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "top block unavailable for requested intra mode at %d %d\n",
                   sl->mb_x, sl->mb_y);
            return AVERROR_INVALIDDATA;
        }
    }

    if ((sl->left_samples_available & 0x8080) != 0x8080) {
        mode = left[mode];
        if (mode < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "left block unavailable for requested intra mode at %d %d\n",
                   sl->mb_x, sl->mb_y);
            return AVERROR_INVALIDDATA;
        }
        if (is_chroma && (sl->left_samples_available & 0x8080)) {
            // mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode = ALZHEIMER_DC_L0T_PRED8x8 +
                   (!(sl->left_samples_available & 0x8000)) +
                   2 * (mode == DC_128_PRED8x8);
        }
    }

    return mode;
}

const uint8_t *ff_h264_decode_nal(H264Context *h, H264SliceContext *sl,
                                  const uint8_t *src,
                                  int *dst_length, int *consumed, int length)
{
    int i, si, di;
    uint8_t *dst;

    // src[0]&0x80; // forbidden bit
    h->nal_ref_idc   = src[0] >> 5;
    h->nal_unit_type = src[0] & 0x1F;

    src++;
    length--;

#define STARTCODE_TEST                                                  \
    if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {         \
        if (src[i + 2] != 3 && src[i + 2] != 0) {                       \
            /* startcode, so we must be past the end */                 \
            length = i;                                                 \
        }                                                               \
        break;                                                          \
    }

#if HAVE_FAST_UNALIGNED
#define FIND_FIRST_ZERO                                                 \
    if (i > 0 && !src[i])                                               \
        i--;                                                            \
    while (src[i])                                                      \
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

    av_fast_padded_malloc(&sl->rbsp_buffer, &sl->rbsp_buffer_size, length+MAX_MBPAIR_SIZE);
    dst = sl->rbsp_buffer;

    if (!dst)
        return NULL;

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        if(h->avctx->flags2 & AV_CODEC_FLAG2_FAST){
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
        } else if (src[si] == 0 && src[si + 1] == 0 && src[si + 2] != 0) {
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
    memset(dst + di, 0, AV_INPUT_BUFFER_PADDING_SIZE);

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

    ff_tlog(h->avctx, "rbsp trailing %X\n", v);

    for (r = 1; r < 9; r++) {
        if (v & 1)
            return r;
        v >>= 1;
    }
    return 0;
}

void ff_h264_free_tables(H264Context *h)
{
    int i;

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

    av_buffer_pool_uninit(&h->qscale_table_pool);
    av_buffer_pool_uninit(&h->mb_type_pool);
    av_buffer_pool_uninit(&h->motion_val_pool);
    av_buffer_pool_uninit(&h->ref_index_pool);

    for (i = 0; i < h->nb_slice_ctx; i++) {
        H264SliceContext *sl = &h->slice_ctx[i];

        av_freep(&sl->dc_val_base);
        av_freep(&sl->er.mb_index2xy);
        av_freep(&sl->er.error_status_table);
        av_freep(&sl->er.er_temp_buffer);

        av_freep(&sl->bipred_scratchpad);
        av_freep(&sl->edge_emu_buffer);
        av_freep(&sl->top_borders[0]);
        av_freep(&sl->top_borders[1]);

        sl->bipred_scratchpad_allocated = 0;
        sl->edge_emu_buffer_allocated   = 0;
        sl->top_borders_allocated[0]    = 0;
        sl->top_borders_allocated[1]    = 0;
    }
}

int ff_h264_alloc_tables(H264Context *h)
{
    const int big_mb_num = h->mb_stride * (h->mb_height + 1);
    const int row_mb_num = 2*h->mb_stride*FFMAX(h->avctx->thread_count, 1);
    int x, y;

    FF_ALLOCZ_ARRAY_OR_GOTO(h->avctx, h->intra4x4_pred_mode,
                      row_mb_num, 8 * sizeof(uint8_t), fail)
    h->slice_ctx[0].intra4x4_pred_mode = h->intra4x4_pred_mode;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->non_zero_count,
                      big_mb_num * 48 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->slice_table_base,
                      (big_mb_num + h->mb_stride) * sizeof(*h->slice_table_base), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->cbp_table,
                      big_mb_num * sizeof(uint16_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->chroma_pred_mode_table,
                      big_mb_num * sizeof(uint8_t), fail)
    FF_ALLOCZ_ARRAY_OR_GOTO(h->avctx, h->mvd_table[0],
                      row_mb_num, 16 * sizeof(uint8_t), fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(h->avctx, h->mvd_table[1],
                      row_mb_num, 16 * sizeof(uint8_t), fail);
    h->slice_ctx[0].mvd_table[0] = h->mvd_table[0];
    h->slice_ctx[0].mvd_table[1] = h->mvd_table[1];

    FF_ALLOCZ_OR_GOTO(h->avctx, h->direct_table,
                      4 * big_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->list_counts,
                      big_mb_num * sizeof(uint8_t), fail)

    memset(h->slice_table_base, -1,
           (big_mb_num + h->mb_stride) * sizeof(*h->slice_table_base));
    h->slice_table = h->slice_table_base + h->mb_stride * 2 + 1;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->mb2b_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mb2br_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    for (y = 0; y < h->mb_height; y++)
        for (x = 0; x < h->mb_width; x++) {
            const int mb_xy = x + y * h->mb_stride;
            const int b_xy  = 4 * x + 4 * y * h->b_stride;

            h->mb2b_xy[mb_xy]  = b_xy;
            h->mb2br_xy[mb_xy] = 8 * (FMO ? mb_xy : (mb_xy % (2 * h->mb_stride)));
        }

    if (!h->dequant4_coeff[0])
        ff_h264_init_dequant_tables(h);

    return 0;

fail:
    ff_h264_free_tables(h);
    return AVERROR(ENOMEM);
}

/**
 * Init context
 * Allocate buffers which are not shared amongst multiple threads.
 */
int ff_h264_slice_context_init(H264Context *h, H264SliceContext *sl)
{
    ERContext *er = &sl->er;
    int mb_array_size = h->mb_height * h->mb_stride;
    int y_size  = (2 * h->mb_width + 1) * (2 * h->mb_height + 1);
    int c_size  = h->mb_stride * (h->mb_height + 1);
    int yc_size = y_size + 2   * c_size;
    int x, y, i;

    sl->ref_cache[0][scan8[5]  + 1] =
    sl->ref_cache[0][scan8[7]  + 1] =
    sl->ref_cache[0][scan8[13] + 1] =
    sl->ref_cache[1][scan8[5]  + 1] =
    sl->ref_cache[1][scan8[7]  + 1] =
    sl->ref_cache[1][scan8[13] + 1] = PART_NOT_AVAILABLE;

    if (sl != h->slice_ctx) {
        memset(er, 0, sizeof(*er));
    } else
    if (CONFIG_ERROR_RESILIENCE) {

        /* init ER */
        er->avctx          = h->avctx;
        er->decode_mb      = h264_er_decode_mb;
        er->opaque         = h;
        er->quarter_sample = 1;

        er->mb_num      = h->mb_num;
        er->mb_width    = h->mb_width;
        er->mb_height   = h->mb_height;
        er->mb_stride   = h->mb_stride;
        er->b8_stride   = h->mb_width * 2 + 1;

        // error resilience code looks cleaner with this
        FF_ALLOCZ_OR_GOTO(h->avctx, er->mb_index2xy,
                          (h->mb_num + 1) * sizeof(int), fail);

        for (y = 0; y < h->mb_height; y++)
            for (x = 0; x < h->mb_width; x++)
                er->mb_index2xy[x + y * h->mb_width] = x + y * h->mb_stride;

        er->mb_index2xy[h->mb_height * h->mb_width] = (h->mb_height - 1) *
                                                      h->mb_stride + h->mb_width;

        FF_ALLOCZ_OR_GOTO(h->avctx, er->error_status_table,
                          mb_array_size * sizeof(uint8_t), fail);

        FF_ALLOC_OR_GOTO(h->avctx, er->er_temp_buffer,
                         h->mb_height * h->mb_stride, fail);

        FF_ALLOCZ_OR_GOTO(h->avctx, sl->dc_val_base,
                          yc_size * sizeof(int16_t), fail);
        er->dc_val[0] = sl->dc_val_base + h->mb_width * 2 + 2;
        er->dc_val[1] = sl->dc_val_base + y_size + h->mb_stride + 1;
        er->dc_val[2] = er->dc_val[1] + c_size;
        for (i = 0; i < yc_size; i++)
            sl->dc_val_base[i] = 1024;
    }

    return 0;

fail:
    return AVERROR(ENOMEM); // ff_h264_free_tables will clean up for us
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size,
                            int parse_extradata);

int ff_h264_decode_extradata(H264Context *h, const uint8_t *buf, int size)
{
    AVCodecContext *avctx = h->avctx;
    int ret;

    if (!buf || size <= 0)
        return -1;

    if (buf[0] == 1) {
        int i, cnt, nalsize;
        const unsigned char *p = buf;

        h->is_avc = 1;

        if (size < 7) {
            av_log(avctx, AV_LOG_ERROR,
                   "avcC %d too short\n", size);
            return AVERROR_INVALIDDATA;
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
                return AVERROR_INVALIDDATA;
            ret = decode_nal_units(h, p, nalsize, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding sps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(nalsize > size - (p-buf))
                return AVERROR_INVALIDDATA;
            ret = decode_nal_units(h, p, nalsize, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding pps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Store right nal length size that will be used to parse all other nals
        h->nal_length_size = (buf[4] & 0x03) + 1;
    } else {
        h->is_avc = 0;
        ret = decode_nal_units(h, buf, size, 1);
        if (ret < 0)
            return ret;
    }
    return size;
}

static int h264_init_context(AVCodecContext *avctx, H264Context *h)
{
    int i;

    h->avctx                 = avctx;
    h->backup_width          = -1;
    h->backup_height         = -1;
    h->backup_pix_fmt        = AV_PIX_FMT_NONE;
    h->dequant_coeff_pps     = -1;
    h->current_sps_id        = -1;
    h->cur_chroma_format_idc = -1;

    h->picture_structure     = PICT_FRAME;
    h->slice_context_count   = 1;
    h->workaround_bugs       = avctx->workaround_bugs;
    h->flags                 = avctx->flags;
    h->prev_poc_msb          = 1 << 16;
    h->x264_build            = -1;
    h->recovery_frame        = -1;
    h->frame_recovered       = 0;
    h->prev_frame_num        = -1;
    h->sei_fpa.frame_packing_arrangement_cancel_flag = -1;

    h->next_outputed_poc = INT_MIN;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;

    ff_h264_reset_sei(h);

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    h->nb_slice_ctx = (avctx->active_thread_type & FF_THREAD_SLICE) ?  H264_MAX_THREADS : 1;
    h->slice_ctx = av_mallocz_array(h->nb_slice_ctx, sizeof(*h->slice_ctx));
    if (!h->slice_ctx) {
        h->nb_slice_ctx = 0;
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        h->DPB[i].f = av_frame_alloc();
        if (!h->DPB[i].f)
            return AVERROR(ENOMEM);
    }

    h->cur_pic.f = av_frame_alloc();
    if (!h->cur_pic.f)
        return AVERROR(ENOMEM);

    h->last_pic_for_ec.f = av_frame_alloc();
    if (!h->last_pic_for_ec.f)
        return AVERROR(ENOMEM);

    for (i = 0; i < h->nb_slice_ctx; i++)
        h->slice_ctx[i].h264 = h;

    return 0;
}

static AVOnce h264_vlc_init = AV_ONCE_INIT;

av_cold int ff_h264_decode_init(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int ret;

    ret = h264_init_context(avctx, h);
    if (ret < 0)
        return ret;

    /* set defaults */
    if (!avctx->has_b_frames)
        h->low_delay = 1;

    ret = ff_thread_once(&h264_vlc_init, ff_h264_decode_init_vlc);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "pthread_once has failed.");
        return AVERROR_UNKNOWN;
    }

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        if (avctx->ticks_per_frame == 1) {
            if(h->avctx->time_base.den < INT_MAX/2) {
                h->avctx->time_base.den *= 2;
            } else
                h->avctx->time_base.num /= 2;
        }
        avctx->ticks_per_frame = 2;
    }

    if (avctx->extradata_size > 0 && avctx->extradata) {
        ret = ff_h264_decode_extradata(h, avctx->extradata, avctx->extradata_size);
        if (ret < 0) {
            h264_decode_end(avctx);
            return ret;
        }
    }

    if (h->sps.bitstream_restriction_flag &&
        h->avctx->has_b_frames < h->sps.num_reorder_frames) {
        h->avctx->has_b_frames = h->sps.num_reorder_frames;
        h->low_delay           = 0;
    }

    avctx->internal->allocate_progress = 1;

    ff_h264_flush_change(h);

    if (h->enable_er < 0 && (avctx->active_thread_type & FF_THREAD_SLICE))
        h->enable_er = 0;

    if (h->enable_er && (avctx->active_thread_type & FF_THREAD_SLICE)) {
        av_log(avctx, AV_LOG_WARNING,
               "Error resilience with slice threads is enabled. It is unsafe and unsupported and may crash. "
               "Use it at your own risk\n");
    }

    return 0;
}

#if HAVE_THREADS
static int decode_init_thread_copy(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int ret;

    if (!avctx->internal->is_copy)
        return 0;

    memset(h, 0, sizeof(*h));

    ret = h264_init_context(avctx, h);
    if (ret < 0)
        return ret;

    h->context_initialized = 0;

    return 0;
}
#endif

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
    H264Picture *out = h->cur_pic_ptr;
    H264Picture *cur = h->cur_pic_ptr;
    int i, pics, out_of_order, out_idx;

    h->cur_pic_ptr->f->pict_type = h->pict_type;

    if (h->next_output_pic)
        return;

    if (cur->field_poc[0] == INT_MAX || cur->field_poc[1] == INT_MAX) {
        /* FIXME: if we have two PAFF fields in one packet, we can't start
         * the next thread here. If we have one field per packet, we can.
         * The check in decode_nal_units() is not good enough to find this
         * yet, so we assume the worst for now. */
        // if (setup_finished)
        //    ff_thread_finish_setup(h->avctx);
        if (cur->field_poc[0] == INT_MAX && cur->field_poc[1] == INT_MAX)
            return;
        if (h->avctx->hwaccel || h->missing_fields <=1)
            return;
    }

    cur->f->interlaced_frame = 0;
    cur->f->repeat_pict      = 0;

    /* Signal interlacing information externally. */
    /* Prioritize picture timing SEI information over used
     * decoding process if it exists. */

    if (h->sps.pic_struct_present_flag) {
        switch (h->sei_pic_struct) {
        case SEI_PIC_STRUCT_FRAME:
            break;
        case SEI_PIC_STRUCT_TOP_FIELD:
        case SEI_PIC_STRUCT_BOTTOM_FIELD:
            cur->f->interlaced_frame = 1;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM:
        case SEI_PIC_STRUCT_BOTTOM_TOP:
            if (FIELD_OR_MBAFF_PICTURE(h))
                cur->f->interlaced_frame = 1;
            else
                // try to flag soft telecine progressive
                cur->f->interlaced_frame = h->prev_interlaced_frame;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
        case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
            /* Signal the possibility of telecined film externally
             * (pic_struct 5,6). From these hints, let the applications
             * decide if they apply deinterlacing. */
            cur->f->repeat_pict = 1;
            break;
        case SEI_PIC_STRUCT_FRAME_DOUBLING:
            cur->f->repeat_pict = 2;
            break;
        case SEI_PIC_STRUCT_FRAME_TRIPLING:
            cur->f->repeat_pict = 4;
            break;
        }

        if ((h->sei_ct_type & 3) &&
            h->sei_pic_struct <= SEI_PIC_STRUCT_BOTTOM_TOP)
            cur->f->interlaced_frame = (h->sei_ct_type & (1 << 1)) != 0;
    } else {
        /* Derive interlacing flag from used decoding process. */
        cur->f->interlaced_frame = FIELD_OR_MBAFF_PICTURE(h);
    }
    h->prev_interlaced_frame = cur->f->interlaced_frame;

    if (cur->field_poc[0] != cur->field_poc[1]) {
        /* Derive top_field_first from field pocs. */
        cur->f->top_field_first = cur->field_poc[0] < cur->field_poc[1];
    } else {
        if (h->sps.pic_struct_present_flag) {
            /* Use picture timing SEI information. Even if it is a
             * information of a past frame, better than nothing. */
            if (h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM ||
                h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM_TOP)
                cur->f->top_field_first = 1;
            else
                cur->f->top_field_first = 0;
        } else if (cur->f->interlaced_frame) {
            /* Default to top field first when pic_struct_present_flag
             * is not set but interlaced frame detected */
            cur->f->top_field_first = 1;
        } else {
            /* Most likely progressive */
            cur->f->top_field_first = 0;
        }
    }

    if (h->sei_frame_packing_present &&
        h->frame_packing_arrangement_type >= 0 &&
        h->frame_packing_arrangement_type <= 6 &&
        h->content_interpretation_type > 0 &&
        h->content_interpretation_type < 3) {
        AVStereo3D *stereo = av_stereo3d_create_side_data(cur->f);
        if (stereo) {
        switch (h->frame_packing_arrangement_type) {
        case 0:
            stereo->type = AV_STEREO3D_CHECKERBOARD;
            break;
        case 1:
            stereo->type = AV_STEREO3D_COLUMNS;
            break;
        case 2:
            stereo->type = AV_STEREO3D_LINES;
            break;
        case 3:
            if (h->quincunx_subsampling)
                stereo->type = AV_STEREO3D_SIDEBYSIDE_QUINCUNX;
            else
                stereo->type = AV_STEREO3D_SIDEBYSIDE;
            break;
        case 4:
            stereo->type = AV_STEREO3D_TOPBOTTOM;
            break;
        case 5:
            stereo->type = AV_STEREO3D_FRAMESEQUENCE;
            break;
        case 6:
            stereo->type = AV_STEREO3D_2D;
            break;
        }

        if (h->content_interpretation_type == 2)
            stereo->flags = AV_STEREO3D_FLAG_INVERT;
        }
    }

    if (h->sei_display_orientation_present &&
        (h->sei_anticlockwise_rotation || h->sei_hflip || h->sei_vflip)) {
        double angle = h->sei_anticlockwise_rotation * 360 / (double) (1 << 16);
        AVFrameSideData *rotation = av_frame_new_side_data(cur->f,
                                                           AV_FRAME_DATA_DISPLAYMATRIX,
                                                           sizeof(int32_t) * 9);
        if (rotation) {
            av_display_rotation_set((int32_t *)rotation->data, angle);
            av_display_matrix_flip((int32_t *)rotation->data,
                                   h->sei_hflip, h->sei_vflip);
        }
    }

    if (h->sei_reguserdata_afd_present) {
        AVFrameSideData *sd = av_frame_new_side_data(cur->f, AV_FRAME_DATA_AFD,
                                                     sizeof(uint8_t));

        if (sd) {
            *sd->data = h->active_format_description;
            h->sei_reguserdata_afd_present = 0;
        }
    }

    if (h->a53_caption) {
        AVFrameSideData *sd = av_frame_new_side_data(cur->f,
                                                     AV_FRAME_DATA_A53_CC,
                                                     h->a53_caption_size);
        if (sd)
            memcpy(sd->data, h->a53_caption, h->a53_caption_size);
        av_freep(&h->a53_caption);
        h->a53_caption_size = 0;
        h->avctx->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
    }

    cur->mmco_reset = h->mmco_reset;
    h->mmco_reset = 0;

    // FIXME do something with unavailable reference frames

    /* Sort B-frames into display order */
    if (h->sps.bitstream_restriction_flag ||
        h->avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
        h->avctx->has_b_frames = FFMAX(h->avctx->has_b_frames, h->sps.num_reorder_frames);
    }
    h->low_delay = !h->avctx->has_b_frames;

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
    if(   cur->f->pict_type == AV_PICTURE_TYPE_B
       || (h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > INT_MIN && h->last_pocs[MAX_DELAYED_PIC_COUNT-1] - h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > 2))
        out_of_order = FFMAX(out_of_order, 1);
    if (out_of_order == MAX_DELAYED_PIC_COUNT) {
        av_log(h->avctx, AV_LOG_VERBOSE, "Invalid POC %d<%d\n", cur->poc, h->last_pocs[0]);
        for (i = 1; i < MAX_DELAYED_PIC_COUNT; i++)
            h->last_pocs[i] = INT_MIN;
        h->last_pocs[0] = cur->poc;
        cur->mmco_reset = 1;
    } else if(h->avctx->has_b_frames < out_of_order && !h->sps.bitstream_restriction_flag){
        av_log(h->avctx, AV_LOG_INFO, "Increasing reorder buffer to %d\n", out_of_order);
        h->avctx->has_b_frames = out_of_order;
        h->low_delay = 0;
    }

    pics = 0;
    while (h->delayed_pic[pics])
        pics++;

    av_assert0(pics <= MAX_DELAYED_PIC_COUNT);

    h->delayed_pic[pics++] = cur;
    if (cur->reference == 0)
        cur->reference = DELAYED_PIC_REF;

    out     = h->delayed_pic[0];
    out_idx = 0;
    for (i = 1; h->delayed_pic[i] &&
                !h->delayed_pic[i]->f->key_frame &&
                !h->delayed_pic[i]->mmco_reset;
         i++)
        if (h->delayed_pic[i]->poc < out->poc) {
            out     = h->delayed_pic[i];
            out_idx = i;
        }
    if (h->avctx->has_b_frames == 0 &&
        (h->delayed_pic[0]->f->key_frame || h->delayed_pic[0]->mmco_reset))
        h->next_outputed_poc = INT_MIN;
    out_of_order = out->poc < h->next_outputed_poc;

    if (out_of_order || pics > h->avctx->has_b_frames) {
        out->reference &= ~DELAYED_PIC_REF;
        // for frame threading, the owner must be the second field's thread or
        // else the first thread can release the picture and reuse it unsafely
        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];
    }
    if (!out_of_order && pics > h->avctx->has_b_frames) {
        h->next_output_pic = out;
        if (out_idx == 0 && h->delayed_pic[0] && (h->delayed_pic[0]->f->key_frame || h->delayed_pic[0]->mmco_reset)) {
            h->next_outputed_poc = INT_MIN;
        } else
            h->next_outputed_poc = out->poc;
    } else {
        av_log(h->avctx, AV_LOG_DEBUG, "no picture %s\n", out_of_order ? "ooo" : "");
    }

    if (h->next_output_pic) {
        if (h->next_output_pic->recovered) {
            // We have reached an recovery point and all frames after it in
            // display order are "recovered".
            h->frame_recovered |= FRAME_RECOVERED_SEI;
        }
        h->next_output_pic->recovered |= !!(h->frame_recovered & FRAME_RECOVERED_SEI);
    }

    if (setup_finished && !h->avctx->hwaccel) {
        ff_thread_finish_setup(h->avctx);

        if (h->avctx->active_thread_type & FF_THREAD_FRAME)
            h->setup_finished = 1;
    }
}

int ff_pred_weight_table(H264Context *h, H264SliceContext *sl)
{
    int list, i;
    int luma_def, chroma_def;

    sl->use_weight             = 0;
    sl->use_weight_chroma      = 0;
    sl->luma_log2_weight_denom = get_ue_golomb(&sl->gb);
    if (h->sps.chroma_format_idc)
        sl->chroma_log2_weight_denom = get_ue_golomb(&sl->gb);

    if (sl->luma_log2_weight_denom > 7U) {
        av_log(h->avctx, AV_LOG_ERROR, "luma_log2_weight_denom %d is out of range\n", sl->luma_log2_weight_denom);
        sl->luma_log2_weight_denom = 0;
    }
    if (sl->chroma_log2_weight_denom > 7U) {
        av_log(h->avctx, AV_LOG_ERROR, "chroma_log2_weight_denom %d is out of range\n", sl->chroma_log2_weight_denom);
        sl->chroma_log2_weight_denom = 0;
    }

    luma_def   = 1 << sl->luma_log2_weight_denom;
    chroma_def = 1 << sl->chroma_log2_weight_denom;

    for (list = 0; list < 2; list++) {
        sl->luma_weight_flag[list]   = 0;
        sl->chroma_weight_flag[list] = 0;
        for (i = 0; i < sl->ref_count[list]; i++) {
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag = get_bits1(&sl->gb);
            if (luma_weight_flag) {
                sl->luma_weight[i][list][0] = get_se_golomb(&sl->gb);
                sl->luma_weight[i][list][1] = get_se_golomb(&sl->gb);
                if (sl->luma_weight[i][list][0] != luma_def ||
                    sl->luma_weight[i][list][1] != 0) {
                    sl->use_weight             = 1;
                    sl->luma_weight_flag[list] = 1;
                }
            } else {
                sl->luma_weight[i][list][0] = luma_def;
                sl->luma_weight[i][list][1] = 0;
            }

            if (h->sps.chroma_format_idc) {
                chroma_weight_flag = get_bits1(&sl->gb);
                if (chroma_weight_flag) {
                    int j;
                    for (j = 0; j < 2; j++) {
                        sl->chroma_weight[i][list][j][0] = get_se_golomb(&sl->gb);
                        sl->chroma_weight[i][list][j][1] = get_se_golomb(&sl->gb);
                        if (sl->chroma_weight[i][list][j][0] != chroma_def ||
                            sl->chroma_weight[i][list][j][1] != 0) {
                            sl->use_weight_chroma        = 1;
                            sl->chroma_weight_flag[list] = 1;
                        }
                    }
                } else {
                    int j;
                    for (j = 0; j < 2; j++) {
                        sl->chroma_weight[i][list][j][0] = chroma_def;
                        sl->chroma_weight[i][list][j][1] = 0;
                    }
                }
            }
        }
        if (sl->slice_type_nos != AV_PICTURE_TYPE_B)
            break;
    }
    sl->use_weight = sl->use_weight || sl->use_weight_chroma;
    return 0;
}

/**
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h)
{
    int i;
    ff_h264_remove_all_refs(h);
    h->prev_frame_num        =
    h->prev_frame_num_offset = 0;
    h->prev_poc_msb          = 1<<16;
    h->prev_poc_lsb          = 0;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
}

/* forget old pics after a seek */
void ff_h264_flush_change(H264Context *h)
{
    int i, j;

    h->next_outputed_poc = INT_MIN;
    h->prev_interlaced_frame = 1;
    idr(h);

    h->prev_frame_num = -1;
    if (h->cur_pic_ptr) {
        h->cur_pic_ptr->reference = 0;
        for (j=i=0; h->delayed_pic[i]; i++)
            if (h->delayed_pic[i] != h->cur_pic_ptr)
                h->delayed_pic[j++] = h->delayed_pic[i];
        h->delayed_pic[j] = NULL;
    }
    ff_h264_unref_picture(h, &h->last_pic_for_ec);

    h->first_field = 0;
    ff_h264_reset_sei(h);
    h->recovery_frame = -1;
    h->frame_recovered = 0;
    h->current_slice = 0;
    h->mmco_reset = 1;
    for (i = 0; i < h->nb_slice_ctx; i++)
        h->slice_ctx[i].list_count = 0;
}

/* forget old pics after a seek */
static void flush_dpb(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int i;

    memset(h->delayed_pic, 0, sizeof(h->delayed_pic));

    ff_h264_flush_change(h);

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++)
        ff_h264_unref_picture(h, &h->DPB[i]);
    h->cur_pic_ptr = NULL;
    ff_h264_unref_picture(h, &h->cur_pic);

    h->mb_y = 0;

    ff_h264_free_tables(h);
    h->context_initialized = 0;
}

int ff_init_poc(H264Context *h, int pic_field_poc[2], int *pic_poc)
{
    const int max_frame_num = 1 << h->sps.log2_max_frame_num;
    int field_poc[2];

    h->frame_num_offset = h->prev_frame_num_offset;
    if (h->frame_num < h->prev_frame_num)
        h->frame_num_offset += max_frame_num;

    if (h->sps.poc_type == 0) {
        const int max_poc_lsb = 1 << h->sps.log2_max_poc_lsb;

        if (h->poc_lsb < h->prev_poc_lsb &&
            h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if (h->poc_lsb > h->prev_poc_lsb &&
                 h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
        field_poc[0] =
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if (h->picture_structure == PICT_FRAME)
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

        if (h->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    } else {
        int poc = 2 * (h->frame_num_offset + h->frame_num);

        if (!h->nal_ref_idc)
            poc--;

        field_poc[0] = poc;
        field_poc[1] = poc;
    }

    if (h->picture_structure != PICT_BOTTOM_FIELD)
        pic_field_poc[0] = field_poc[0];
    if (h->picture_structure != PICT_TOP_FIELD)
        pic_field_poc[1] = field_poc[1];
    *pic_poc = FFMIN(pic_field_poc[0], pic_field_poc[1]);

    return 0;
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

int ff_set_ref_count(H264Context *h, H264SliceContext *sl)
{
    int ref_count[2], list_count;
    int num_ref_idx_active_override_flag;

    // set defaults, might be overridden a few lines later
    ref_count[0] = h->pps.ref_count[0];
    ref_count[1] = h->pps.ref_count[1];

    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        unsigned max[2];
        max[0] = max[1] = h->picture_structure == PICT_FRAME ? 15 : 31;

        if (sl->slice_type_nos == AV_PICTURE_TYPE_B)
            sl->direct_spatial_mv_pred = get_bits1(&sl->gb);
        num_ref_idx_active_override_flag = get_bits1(&sl->gb);

        if (num_ref_idx_active_override_flag) {
            ref_count[0] = get_ue_golomb(&sl->gb) + 1;
            if (sl->slice_type_nos == AV_PICTURE_TYPE_B) {
                ref_count[1] = get_ue_golomb(&sl->gb) + 1;
            } else
                // full range is spec-ok in this case, even for frames
                ref_count[1] = 1;
        }

        if (ref_count[0]-1 > max[0] || ref_count[1]-1 > max[1]){
            av_log(h->avctx, AV_LOG_ERROR, "reference overflow %u > %u or %u > %u\n", ref_count[0]-1, max[0], ref_count[1]-1, max[1]);
            sl->ref_count[0] = sl->ref_count[1] = 0;
            sl->list_count   = 0;
            return AVERROR_INVALIDDATA;
        }

        if (sl->slice_type_nos == AV_PICTURE_TYPE_B)
            list_count = 2;
        else
            list_count = 1;
    } else {
        list_count   = 0;
        ref_count[0] = ref_count[1] = 0;
    }

    if (list_count   != sl->list_count   ||
        ref_count[0] != sl->ref_count[0] ||
        ref_count[1] != sl->ref_count[1]) {
        sl->ref_count[0] = ref_count[0];
        sl->ref_count[1] = ref_count[1];
        sl->list_count   = list_count;
        return 1;
    }

    return 0;
}

static const uint8_t start_code[] = { 0x00, 0x00, 0x01 };

static int get_bit_length(H264Context *h, const uint8_t *buf,
                          const uint8_t *ptr, int dst_length,
                          int i, int next_avc)
{
    if ((h->workaround_bugs & FF_BUG_AUTODETECT) && i + 3 < next_avc &&
        buf[i]     == 0x00 && buf[i + 1] == 0x00 &&
        buf[i + 2] == 0x01 && buf[i + 3] == 0xE0)
        h->workaround_bugs |= FF_BUG_TRUNCATED;

    if (!(h->workaround_bugs & FF_BUG_TRUNCATED))
        while (dst_length > 0 && ptr[dst_length - 1] == 0)
            dst_length--;

    if (!dst_length)
        return 0;

    return 8 * dst_length - decode_rbsp_trailing(h, ptr + dst_length - 1);
}

static int get_last_needed_nal(H264Context *h, const uint8_t *buf, int buf_size)
{
    int next_avc    = h->is_avc ? 0 : buf_size;
    int nal_index   = 0;
    int buf_index   = 0;
    int nals_needed = 0;
    int first_slice = 0;

    while(1) {
        GetBitContext gb;
        int nalsize = 0;
        int dst_length, bit_length, consumed;
        const uint8_t *ptr;

        if (buf_index >= next_avc) {
            nalsize = get_avc_nalsize(h, buf, buf_size, &buf_index);
            if (nalsize < 0)
                break;
            next_avc = buf_index + nalsize;
        } else {
            buf_index = find_start_code(buf, buf_size, buf_index, next_avc);
            if (buf_index >= buf_size)
                break;
            if (buf_index >= next_avc)
                continue;
        }

        ptr = ff_h264_decode_nal(h, &h->slice_ctx[0], buf + buf_index, &dst_length, &consumed,
                                 next_avc - buf_index);

        if (!ptr || dst_length < 0)
            return AVERROR_INVALIDDATA;

        buf_index += consumed;

        bit_length = get_bit_length(h, buf, ptr, dst_length,
                                    buf_index, next_avc);
        nal_index++;

        /* packets can sometimes contain multiple PPS/SPS,
         * e.g. two PAFF field pictures in one packet, or a demuxer
         * which splits NALs strangely if so, when frame threading we
         * can't start the next thread until we've read all of them */
        switch (h->nal_unit_type) {
        case NAL_SPS:
        case NAL_PPS:
            nals_needed = nal_index;
            break;
        case NAL_DPA:
        case NAL_IDR_SLICE:
        case NAL_SLICE:
            init_get_bits(&gb, ptr, bit_length);
            if (!get_ue_golomb_long(&gb) ||  // first_mb_in_slice
                !first_slice ||
                first_slice != h->nal_unit_type)
                nals_needed = nal_index;
            if (!first_slice)
                first_slice = h->nal_unit_type;
        }
    }

    return nals_needed;
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size,
                            int parse_extradata)
{
    AVCodecContext *const avctx = h->avctx;
    H264SliceContext *sl;
    int buf_index;
    unsigned context_count;
    int next_avc;
    int nals_needed = 0; ///< number of NALs that need decoding before the next frame thread starts
    int nal_index;
    int idr_cleared=0;
    int ret = 0;

    h->nal_unit_type= 0;

    if(!h->slice_context_count)
         h->slice_context_count= 1;
    h->max_contexts = h->slice_context_count;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)) {
        h->current_slice = 0;
        if (!h->first_field)
            h->cur_pic_ptr = NULL;
        ff_h264_reset_sei(h);
    }

    if (h->nal_length_size == 4) {
        if (buf_size > 8 && AV_RB32(buf) == 1 && AV_RB32(buf+5) > (unsigned)buf_size) {
            h->is_avc = 0;
        }else if(buf_size > 3 && AV_RB32(buf) > 1 && AV_RB32(buf) <= (unsigned)buf_size)
            h->is_avc = 1;
    }

    if (avctx->active_thread_type & FF_THREAD_FRAME)
        nals_needed = get_last_needed_nal(h, buf, buf_size);

    {
        buf_index     = 0;
        context_count = 0;
        next_avc      = h->is_avc ? 0 : buf_size;
        nal_index     = 0;
        for (;;) {
            int consumed;
            int dst_length;
            int bit_length;
            const uint8_t *ptr;
            int nalsize = 0;
            int err;

            if (buf_index >= next_avc) {
                nalsize = get_avc_nalsize(h, buf, buf_size, &buf_index);
                if (nalsize < 0)
                    break;
                next_avc = buf_index + nalsize;
            } else {
                buf_index = find_start_code(buf, buf_size, buf_index, next_avc);
                if (buf_index >= buf_size)
                    break;
                if (buf_index >= next_avc)
                    continue;
            }

            sl = &h->slice_ctx[context_count];

            ptr = ff_h264_decode_nal(h, sl, buf + buf_index, &dst_length,
                                     &consumed, next_avc - buf_index);
            if (!ptr || dst_length < 0) {
                ret = -1;
                goto end;
            }

            bit_length = get_bit_length(h, buf, ptr, dst_length,
                                        buf_index + consumed, next_avc);

            if (h->avctx->debug & FF_DEBUG_STARTCODE)
                av_log(h->avctx, AV_LOG_DEBUG,
                       "NAL %d/%d at %d/%d length %d\n",
                       h->nal_unit_type, h->nal_ref_idc, buf_index, buf_size, dst_length);

            if (h->is_avc && (nalsize != consumed) && nalsize)
                av_log(h->avctx, AV_LOG_DEBUG,
                       "AVC: Consumed only %d bytes instead of %d\n",
                       consumed, nalsize);

            buf_index += consumed;
            nal_index++;

            if (avctx->skip_frame >= AVDISCARD_NONREF &&
                h->nal_ref_idc == 0 &&
                h->nal_unit_type != NAL_SEI)
                continue;

again:
            /* Ignore per frame NAL unit type during extradata
             * parsing. Decoding slices is not possible in codec init
             * with frame-mt */
            if (parse_extradata) {
                switch (h->nal_unit_type) {
                case NAL_IDR_SLICE:
                case NAL_SLICE:
                case NAL_DPA:
                case NAL_DPB:
                case NAL_DPC:
                    av_log(h->avctx, AV_LOG_WARNING,
                           "Ignoring NAL %d in global header/extradata\n",
                           h->nal_unit_type);
                    // fall through to next case
                case NAL_AUXILIARY_SLICE:
                    h->nal_unit_type = NAL_FF_IGNORE;
                }
            }

            err = 0;

            switch (h->nal_unit_type) {
            case NAL_IDR_SLICE:
                if ((ptr[0] & 0xFC) == 0x98) {
                    av_log(h->avctx, AV_LOG_ERROR, "Invalid inter IDR frame\n");
                    h->next_outputed_poc = INT_MIN;
                    ret = -1;
                    goto end;
                }
                if (h->nal_unit_type != NAL_IDR_SLICE) {
                    av_log(h->avctx, AV_LOG_ERROR,
                           "Invalid mix of idr and non-idr slices\n");
                    ret = -1;
                    goto end;
                }
                if(!idr_cleared) {
                    if (h->current_slice && (avctx->active_thread_type & FF_THREAD_SLICE)) {
                        av_log(h, AV_LOG_ERROR, "invalid mixed IDR / non IDR frames cannot be decoded in slice multithreading mode\n");
                        ret = AVERROR_INVALIDDATA;
                        goto end;
                    }
                    idr(h); // FIXME ensure we don't lose some frames if there is reordering
                }
                idr_cleared = 1;
                h->has_recovery_point = 1;
            case NAL_SLICE:
                init_get_bits(&sl->gb, ptr, bit_length);

                if (   nals_needed >= nal_index
                    || (!(avctx->active_thread_type & FF_THREAD_FRAME) && !context_count))
                    h->au_pps_id = -1;

                if ((err = ff_h264_decode_slice_header(h, sl)))
                    break;

                if (h->sei_recovery_frame_cnt >= 0) {
                    if (h->frame_num != h->sei_recovery_frame_cnt || sl->slice_type_nos != AV_PICTURE_TYPE_I)
                        h->valid_recovery_point = 1;

                    if (   h->recovery_frame < 0
                        || av_mod_uintp2(h->recovery_frame - h->frame_num, h->sps.log2_max_frame_num) > h->sei_recovery_frame_cnt) {
                        h->recovery_frame = av_mod_uintp2(h->frame_num + h->sei_recovery_frame_cnt, h->sps.log2_max_frame_num);

                        if (!h->valid_recovery_point)
                            h->recovery_frame = h->frame_num;
                    }
                }

                h->cur_pic_ptr->f->key_frame |=
                    (h->nal_unit_type == NAL_IDR_SLICE);

                if (h->nal_unit_type == NAL_IDR_SLICE ||
                    h->recovery_frame == h->frame_num) {
                    h->recovery_frame         = -1;
                    h->cur_pic_ptr->recovered = 1;
                }
                // If we have an IDR, all frames after it in decoded order are
                // "recovered".
                if (h->nal_unit_type == NAL_IDR_SLICE)
                    h->frame_recovered |= FRAME_RECOVERED_IDR;
#if 1
                h->cur_pic_ptr->recovered |= h->frame_recovered;
#else
                h->cur_pic_ptr->recovered |= !!(h->frame_recovered & FRAME_RECOVERED_IDR);
#endif

                if (h->current_slice == 1) {
                    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS))
                        decode_postinit(h, nal_index >= nals_needed);

                    if (h->avctx->hwaccel &&
                        (ret = h->avctx->hwaccel->start_frame(h->avctx, buf, buf_size)) < 0)
                        goto end;
#if FF_API_CAP_VDPAU
                    if (CONFIG_H264_VDPAU_DECODER &&
                        h->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU)
                        ff_vdpau_h264_picture_start(h);
#endif
                }

                if (sl->redundant_pic_count == 0) {
                    if (avctx->hwaccel) {
                        ret = avctx->hwaccel->decode_slice(avctx,
                                                           &buf[buf_index - consumed],
                                                           consumed);
                        if (ret < 0)
                            goto end;
#if FF_API_CAP_VDPAU
                    } else if (CONFIG_H264_VDPAU_DECODER &&
                               h->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU) {
                        ff_vdpau_add_data_chunk(h->cur_pic_ptr->f->data[0],
                                                start_code,
                                                sizeof(start_code));
                        ff_vdpau_add_data_chunk(h->cur_pic_ptr->f->data[0],
                                                &buf[buf_index - consumed],
                                                consumed);
#endif
                    } else
                        context_count++;
                }
                break;
            case NAL_DPA:
            case NAL_DPB:
            case NAL_DPC:
                avpriv_request_sample(avctx, "data partitioning");
                break;
            case NAL_SEI:
                init_get_bits(&h->gb, ptr, bit_length);
                ret = ff_h264_decode_sei(h);
                if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                    goto end;
                break;
            case NAL_SPS:
                init_get_bits(&h->gb, ptr, bit_length);
                if (ff_h264_decode_seq_parameter_set(h, 0) >= 0)
                    break;
                if (h->is_avc ? nalsize : 1) {
                    av_log(h->avctx, AV_LOG_DEBUG,
                           "SPS decoding failure, trying again with the complete NAL\n");
                    if (h->is_avc)
                        av_assert0(next_avc - buf_index + consumed == nalsize);
                    if ((next_avc - buf_index + consumed - 1) >= INT_MAX/8)
                        break;
                    init_get_bits(&h->gb, &buf[buf_index + 1 - consumed],
                                  8*(next_avc - buf_index + consumed - 1));
                    if (ff_h264_decode_seq_parameter_set(h, 0) >= 0)
                        break;
                }
                init_get_bits(&h->gb, ptr, bit_length);
                ff_h264_decode_seq_parameter_set(h, 1);

                break;
            case NAL_PPS:
                init_get_bits(&h->gb, ptr, bit_length);
                ret = ff_h264_decode_picture_parameter_set(h, bit_length);
                if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                    goto end;
                break;
            case NAL_AUD:
            case NAL_END_SEQUENCE:
            case NAL_END_STREAM:
            case NAL_FILLER_DATA:
            case NAL_SPS_EXT:
            case NAL_AUXILIARY_SLICE:
                break;
            case NAL_FF_IGNORE:
                break;
            default:
                av_log(avctx, AV_LOG_DEBUG, "Unknown NAL code: %d (%d bits)\n",
                       h->nal_unit_type, bit_length);
            }

            if (context_count == h->max_contexts) {
                ret = ff_h264_execute_decode_slices(h, context_count);
                if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                    goto end;
                context_count = 0;
            }

            if (err < 0 || err == SLICE_SKIPED) {
                if (err < 0)
                    av_log(h->avctx, AV_LOG_ERROR, "decode_slice_header error\n");
                sl->ref_count[0] = sl->ref_count[1] = sl->list_count = 0;
            } else if (err == SLICE_SINGLETHREAD) {
                if (context_count > 1) {
                    ret = ff_h264_execute_decode_slices(h, context_count - 1);
                    if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                        goto end;
                    context_count = 0;
                }
                /* Slice could not be decoded in parallel mode, restart. Note
                 * that rbsp_buffer is not transferred, but since we no longer
                 * run in parallel mode this should not be an issue. */
                sl               = &h->slice_ctx[0];
                goto again;
            }
        }
    }
    if (context_count) {
        ret = ff_h264_execute_decode_slices(h, context_count);
        if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
            goto end;
    }

    ret = 0;
end:

#if CONFIG_ERROR_RESILIENCE
    sl = h->slice_ctx;
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
    if (!FIELD_PICTURE(h) && h->current_slice && !h->sps.new && h->enable_er) {
        int use_last_pic = h->last_pic_for_ec.f->buf[0] && !sl->ref_count[0];

        ff_h264_set_erpic(&sl->er.cur_pic, h->cur_pic_ptr);

        if (use_last_pic) {
            ff_h264_set_erpic(&sl->er.last_pic, &h->last_pic_for_ec);
            sl->ref_list[0][0].parent = &h->last_pic_for_ec;
            memcpy(sl->ref_list[0][0].data, h->last_pic_for_ec.f->data, sizeof(sl->ref_list[0][0].data));
            memcpy(sl->ref_list[0][0].linesize, h->last_pic_for_ec.f->linesize, sizeof(sl->ref_list[0][0].linesize));
            sl->ref_list[0][0].reference = h->last_pic_for_ec.reference;
        } else if (sl->ref_count[0]) {
            ff_h264_set_erpic(&sl->er.last_pic, sl->ref_list[0][0].parent);
        } else
            ff_h264_set_erpic(&sl->er.last_pic, NULL);

        if (sl->ref_count[1])
            ff_h264_set_erpic(&sl->er.next_pic, sl->ref_list[1][0].parent);

        sl->er.ref_count = sl->ref_count[0];

        ff_er_frame_end(&sl->er);
        if (use_last_pic)
            memset(&sl->ref_list[0][0], 0, sizeof(sl->ref_list[0][0]));
    }
#endif /* CONFIG_ERROR_RESILIENCE */
    /* clean up */
    if (h->cur_pic_ptr && !h->droppable) {
        ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                  h->picture_structure == PICT_BOTTOM_FIELD);
    }

    return (ret < 0) ? ret : buf_index;
}

/**
 * Return the number of bytes consumed for building the current frame.
 */
static int get_consumed_bytes(int pos, int buf_size)
{
    if (pos == 0)
        pos = 1;        // avoid infinite loops (I doubt that is needed but...)
    if (pos + 10 > buf_size)
        pos = buf_size; // oops ;)

    return pos;
}

static int output_frame(H264Context *h, AVFrame *dst, H264Picture *srcp)
{
    AVFrame *src = srcp->f;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src->format);
    int i;
    int ret = av_frame_ref(dst, src);
    if (ret < 0)
        return ret;

    av_dict_set(&dst->metadata, "stereo_mode", ff_h264_sei_stereo_mode(h), 0);

    h->backup_width   = h->avctx->width;
    h->backup_height  = h->avctx->height;
    h->backup_pix_fmt = h->avctx->pix_fmt;

    h->avctx->width   = dst->width;
    h->avctx->height  = dst->height;
    h->avctx->pix_fmt = dst->format;

    if (srcp->sei_recovery_frame_cnt == 0)
        dst->key_frame = 1;
    if (!srcp->crop)
        return 0;

    for (i = 0; i < desc->nb_components; i++) {
        int hshift = (i > 0) ? desc->log2_chroma_w : 0;
        int vshift = (i > 0) ? desc->log2_chroma_h : 0;
        int off    = ((srcp->crop_left >> hshift) << h->pixel_shift) +
                      (srcp->crop_top  >> vshift) * dst->linesize[i];
        dst->data[i] += off;
    }
    return 0;
}

static int is_extra(const uint8_t *buf, int buf_size)
{
    int cnt= buf[5]&0x1f;
    const uint8_t *p= buf+6;
    while(cnt--){
        int nalsize= AV_RB16(p) + 2;
        if(nalsize > buf_size - (p-buf) || (p[2] & 0x9F) != 7)
            return 0;
        p += nalsize;
    }
    cnt = *(p++);
    if(!cnt)
        return 0;
    while(cnt--){
        int nalsize= AV_RB16(p) + 2;
        if(nalsize > buf_size - (p-buf) || (p[2] & 0x9F) != 8)
            return 0;
        p += nalsize;
    }
    return 1;
}

static int h264_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    H264Context *h     = avctx->priv_data;
    AVFrame *pict      = data;
    int buf_index      = 0;
    H264Picture *out;
    int i, out_idx;
    int ret;

    h->flags = avctx->flags;
    h->setup_finished = 0;

    if (h->backup_width != -1) {
        avctx->width    = h->backup_width;
        h->backup_width = -1;
    }
    if (h->backup_height != -1) {
        avctx->height    = h->backup_height;
        h->backup_height = -1;
    }
    if (h->backup_pix_fmt != AV_PIX_FMT_NONE) {
        avctx->pix_fmt    = h->backup_pix_fmt;
        h->backup_pix_fmt = AV_PIX_FMT_NONE;
    }

    ff_h264_unref_picture(h, &h->last_pic_for_ec);

    /* end of stream, output what is still in the buffers */
    if (buf_size == 0) {
 out:

        h->cur_pic_ptr = NULL;
        h->first_field = 0;

        // FIXME factorize this with the output code below
        out     = h->delayed_pic[0];
        out_idx = 0;
        for (i = 1;
             h->delayed_pic[i] &&
             !h->delayed_pic[i]->f->key_frame &&
             !h->delayed_pic[i]->mmco_reset;
             i++)
            if (h->delayed_pic[i]->poc < out->poc) {
                out     = h->delayed_pic[i];
                out_idx = i;
            }

        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];

        if (out) {
            out->reference &= ~DELAYED_PIC_REF;
            ret = output_frame(h, pict, out);
            if (ret < 0)
                return ret;
            *got_frame = 1;
        }

        return buf_index;
    }
    if (h->is_avc && av_packet_get_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA, NULL)) {
        int side_size;
        uint8_t *side = av_packet_get_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA, &side_size);
        if (is_extra(side, side_size))
            ff_h264_decode_extradata(h, side, side_size);
    }
    if(h->is_avc && buf_size >= 9 && buf[0]==1 && buf[2]==0 && (buf[4]&0xFC)==0xFC && (buf[5]&0x1F) && buf[8]==0x67){
        if (is_extra(buf, buf_size))
            return ff_h264_decode_extradata(h, buf, buf_size);
    }

    buf_index = decode_nal_units(h, buf, buf_size, 0);
    if (buf_index < 0)
        return AVERROR_INVALIDDATA;

    if (!h->cur_pic_ptr && h->nal_unit_type == NAL_END_SEQUENCE) {
        av_assert0(buf_index <= buf_size);
        goto out;
    }

    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS) && !h->cur_pic_ptr) {
        if (avctx->skip_frame >= AVDISCARD_NONREF ||
            buf_size >= 4 && !memcmp("Q264", buf, 4))
            return buf_size;
        av_log(avctx, AV_LOG_ERROR, "no frame!\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS) ||
        (h->mb_y >= h->mb_height && h->mb_height)) {
        if (avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)
            decode_postinit(h, 1);

        if ((ret = ff_h264_field_end(h, &h->slice_ctx[0], 0)) < 0)
            return ret;

        /* Wait for second field. */
        *got_frame = 0;
        if (h->next_output_pic && ((avctx->flags & AV_CODEC_FLAG_OUTPUT_CORRUPT) ||
                                   (avctx->flags2 & AV_CODEC_FLAG2_SHOW_ALL) ||
                                   h->next_output_pic->recovered)) {
            if (!h->next_output_pic->recovered)
                h->next_output_pic->f->flags |= AV_FRAME_FLAG_CORRUPT;

            if (!h->avctx->hwaccel &&
                 (h->next_output_pic->field_poc[0] == INT_MAX ||
                  h->next_output_pic->field_poc[1] == INT_MAX)
            ) {
                int p;
                AVFrame *f = h->next_output_pic->f;
                int field = h->next_output_pic->field_poc[0] == INT_MAX;
                uint8_t *dst_data[4];
                int linesizes[4];
                const uint8_t *src_data[4];

                av_log(h->avctx, AV_LOG_DEBUG, "Duplicating field %d to fill missing\n", field);

                for (p = 0; p<4; p++) {
                    dst_data[p] = f->data[p] + (field^1)*f->linesize[p];
                    src_data[p] = f->data[p] +  field   *f->linesize[p];
                    linesizes[p] = 2*f->linesize[p];
                }

                av_image_copy(dst_data, linesizes, src_data, linesizes,
                              f->format, f->width, f->height>>1);
            }

            ret = output_frame(h, pict, h->next_output_pic);
            if (ret < 0)
                return ret;
            *got_frame = 1;
            if (CONFIG_MPEGVIDEO) {
                ff_print_debug_info2(h->avctx, pict, NULL,
                                    h->next_output_pic->mb_type,
                                    h->next_output_pic->qscale_table,
                                    h->next_output_pic->motion_val,
                                    &h->low_delay,
                                    h->mb_width, h->mb_height, h->mb_stride, 1);
            }
        }
    }

    av_assert0(pict->buf[0] || !*got_frame);

    ff_h264_unref_picture(h, &h->last_pic_for_ec);

    return get_consumed_bytes(buf_index, buf_size);
}

av_cold void ff_h264_free_context(H264Context *h)
{
    int i;

    ff_h264_free_tables(h);

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        ff_h264_unref_picture(h, &h->DPB[i]);
        av_frame_free(&h->DPB[i].f);
    }
    memset(h->delayed_pic, 0, sizeof(h->delayed_pic));

    h->cur_pic_ptr = NULL;

    for (i = 0; i < h->nb_slice_ctx; i++)
        av_freep(&h->slice_ctx[i].rbsp_buffer);
    av_freep(&h->slice_ctx);
    h->nb_slice_ctx = 0;

    h->a53_caption_size = 0;
    av_freep(&h->a53_caption);

    for (i = 0; i < MAX_SPS_COUNT; i++)
        av_freep(h->sps_buffers + i);

    for (i = 0; i < MAX_PPS_COUNT; i++)
        av_freep(h->pps_buffers + i);
}

static av_cold int h264_decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;

    ff_h264_remove_all_refs(h);
    ff_h264_free_context(h);

    ff_h264_unref_picture(h, &h->cur_pic);
    av_frame_free(&h->cur_pic.f);
    ff_h264_unref_picture(h, &h->last_pic_for_ec);
    av_frame_free(&h->last_pic_for_ec.f);

    return 0;
}

#define OFFSET(x) offsetof(H264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption h264_options[] = {
    {"is_avc", "is avc", offsetof(H264Context, is_avc), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, 0},
    {"nal_length_size", "nal_length_size", offsetof(H264Context, nal_length_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 4, 0},
    { "enable_er", "Enable error resilience on damaged frames (unsafe)", OFFSET(enable_er), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, VD },
    { NULL },
};

static const AVClass h264_class = {
    .class_name = "H264 Decoder",
    .item_name  = av_default_item_name,
    .option     = h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_decoder = {
    .name                  = "h264",
    .long_name             = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_H264,
    .priv_data_size        = sizeof(H264Context),
    .init                  = ff_h264_decode_init,
    .close                 = h264_decode_end,
    .decode                = h264_decode_frame,
    .capabilities          = /*AV_CODEC_CAP_DRAW_HORIZ_BAND |*/ AV_CODEC_CAP_DR1 |
                             AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SLICE_THREADS |
                             AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE,
    .flush                 = flush_dpb,
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(ff_h264_update_thread_context),
    .profiles              = NULL_IF_CONFIG_SMALL(ff_h264_profiles),
    .priv_class            = &h264_class,
};

#if CONFIG_H264_VDPAU_DECODER && FF_API_VDPAU
static const AVClass h264_vdpau_class = {
    .class_name = "H264 VDPAU Decoder",
    .item_name  = av_default_item_name,
    .option     = h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_vdpau_decoder = {
    .name           = "h264_vdpau",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (VDPAU acceleration)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(H264Context),
    .init           = ff_h264_decode_init,
    .close          = h264_decode_end,
    .decode         = h264_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HWACCEL_VDPAU,
    .flush          = flush_dpb,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_VDPAU_H264,
                                                     AV_PIX_FMT_NONE},
    .profiles       = NULL_IF_CONFIG_SMALL(ff_h264_profiles),
    .priv_class     = &h264_vdpau_class,
};
#endif
