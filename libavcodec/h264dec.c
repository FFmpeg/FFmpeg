/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG-4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/display.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timer.h"
#include "internal.h"
#include "bytestream.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "h264.h"
#include "h264dec.h"
#include "h2645_parse.h"
#include "h264data.h"
#include "h264chroma.h"
#include "h264_mvpred.h"
#include "h264_ps.h"
#include "golomb.h"
#include "mathops.h"
#include "me_cmp.h"
#include "mpegutils.h"
#include "profiles.h"
#include "rectangle.h"
#include "thread.h"

#include <assert.h>

const uint16_t ff_h264_mb_sizes[4] = { 256, 384, 512, 768 };

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
    assert(ref >= 0);
    /* FIXME: It is possible albeit uncommon that slice references
     * differ between slices. We take the easy approach and ignore
     * it for now. If this turns out to have any relevance in
     * practice then correct remapping should be added. */
    if (ref >= sl->ref_count[0])
        ref = 0;
    fill_rectangle(&h->cur_pic.ref_index[0][4 * sl->mb_xy],
                   2, 2, 2, ref, 1);
    fill_rectangle(&sl->ref_cache[0][scan8[0]], 4, 4, 8, ref, 1);
    fill_rectangle(sl->mv_cache[0][scan8[0]], 4, 4, 8,
                   pack16to32((*mv)[0][0][0], (*mv)[0][0][1]), 4);
    assert(!FRAME_MBAFF(h));
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
    const int row_mb_num = h->mb_stride * 2 * h->nb_slice_ctx;
    int x, y;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->intra4x4_pred_mode,
                      row_mb_num * 8 * sizeof(uint8_t), fail)
    h->slice_ctx[0].intra4x4_pred_mode = h->intra4x4_pred_mode;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->non_zero_count,
                      big_mb_num * 48 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->slice_table_base,
                      (big_mb_num + h->mb_stride) * sizeof(*h->slice_table_base), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->cbp_table,
                      big_mb_num * sizeof(uint16_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->chroma_pred_mode_table,
                      big_mb_num * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mvd_table[0],
                      16 * row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mvd_table[1],
                      16 * row_mb_num * sizeof(uint8_t), fail);
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

static int h264_init_context(AVCodecContext *avctx, H264Context *h)
{
    int i;

    h->avctx                 = avctx;

    h->picture_structure     = PICT_FRAME;
    h->workaround_bugs       = avctx->workaround_bugs;
    h->flags                 = avctx->flags;
    h->poc.prev_poc_msb      = 1 << 16;
    h->recovery_frame        = -1;
    h->frame_recovered       = 0;

    h->next_outputed_poc = INT_MIN;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;

    ff_h264_sei_uninit(&h->sei);

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    h->nb_slice_ctx = (avctx->active_thread_type & FF_THREAD_SLICE) ? avctx->thread_count : 1;
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

    for (i = 0; i < h->nb_slice_ctx; i++)
        h->slice_ctx[i].h264 = h;

    return 0;
}

static av_cold int h264_decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int i;

    ff_h264_free_tables(h);

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        ff_h264_unref_picture(h, &h->DPB[i]);
        av_frame_free(&h->DPB[i].f);
    }

    h->cur_pic_ptr = NULL;

    av_freep(&h->slice_ctx);
    h->nb_slice_ctx = 0;

    for (i = 0; i < MAX_SPS_COUNT; i++)
        av_buffer_unref(&h->ps.sps_list[i]);

    for (i = 0; i < MAX_PPS_COUNT; i++)
        av_buffer_unref(&h->ps.pps_list[i]);

    ff_h2645_packet_uninit(&h->pkt);

    ff_h264_unref_picture(h, &h->cur_pic);
    av_frame_free(&h->cur_pic.f);

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

    ret = ff_thread_once(&h264_vlc_init, ff_h264_decode_init_vlc);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "pthread_once has failed.");
        return AVERROR_UNKNOWN;
    }

    if (avctx->ticks_per_frame == 1)
        h->avctx->framerate.num *= 2;
    avctx->ticks_per_frame = 2;

    if (avctx->extradata_size > 0 && avctx->extradata) {
       ret = ff_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                      &h->ps, &h->is_avc, &h->nal_length_size,
                                      avctx->err_recognition, avctx);
       if (ret < 0) {
           h264_decode_end(avctx);
           return ret;
       }
    }

    if (h->ps.sps && h->ps.sps->bitstream_restriction_flag &&
        h->avctx->has_b_frames < h->ps.sps->num_reorder_frames) {
        h->avctx->has_b_frames = h->ps.sps->num_reorder_frames;
    }

    avctx->internal->allocate_progress = 1;

    if (h->enable_er) {
        av_log(avctx, AV_LOG_WARNING,
               "Error resilience is enabled. It is unsafe and unsupported and may crash. "
               "Use it at your own risk\n");
    }

    return 0;
}

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
    const SPS *sps = h->ps.sps;
    H264Picture *out = h->cur_pic_ptr;
    H264Picture *cur = h->cur_pic_ptr;
    int i, pics, out_of_order, out_idx;
    int invalid = 0, cnt = 0;

    if (h->next_output_pic)
        return;

    if (cur->field_poc[0] == INT_MAX || cur->field_poc[1] == INT_MAX) {
        /* FIXME: if we have two PAFF fields in one packet, we can't start
         * the next thread here. If we have one field per packet, we can.
         * The check in decode_nal_units() is not good enough to find this
         * yet, so we assume the worst for now. */
        // if (setup_finished)
        //    ff_thread_finish_setup(h->avctx);
        return;
    }

    // FIXME do something with unavailable reference frames

    /* Sort B-frames into display order */
    if (sps->bitstream_restriction_flag ||
        h->avctx->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
        h->avctx->has_b_frames = FFMAX(h->avctx->has_b_frames, sps->num_reorder_frames);
    }

    pics = 0;
    while (h->delayed_pic[pics])
        pics++;

    assert(pics <= MAX_DELAYED_PIC_COUNT);

    h->delayed_pic[pics++] = cur;
    if (cur->reference == 0)
        cur->reference = DELAYED_PIC_REF;

    /* Frame reordering. This code takes pictures from coding order and sorts
     * them by their incremental POC value into display order. It supports POC
     * gaps, MMCO reset codes and random resets.
     * A "display group" can start either with a IDR frame (f.key_frame = 1),
     * and/or can be closed down with a MMCO reset code. In sequences where
     * there is no delay, we can't detect that (since the frame was already
     * output to the user), so we also set h->mmco_reset to detect the MMCO
     * reset code.
     * FIXME: if we detect insufficient delays (as per h->avctx->has_b_frames),
     * we increase the delay between input and output. All frames affected by
     * the lag (e.g. those that should have been output before another frame
     * that we already returned to the user) will be dropped. This is a bug
     * that we will fix later. */
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++) {
        cnt     += out->poc < h->last_pocs[i];
        invalid += out->poc == INT_MIN;
    }
    if (!h->mmco_reset && !cur->f->key_frame &&
        cnt + invalid == MAX_DELAYED_PIC_COUNT && cnt > 0) {
        h->mmco_reset = 2;
        if (pics > 1)
            h->delayed_pic[pics - 2]->mmco_reset = 2;
    }
    if (h->mmco_reset || cur->f->key_frame) {
        for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
            h->last_pocs[i] = INT_MIN;
        cnt     = 0;
        invalid = MAX_DELAYED_PIC_COUNT;
    }
    out     = h->delayed_pic[0];
    out_idx = 0;
    for (i = 1; i < MAX_DELAYED_PIC_COUNT &&
                h->delayed_pic[i] &&
                !h->delayed_pic[i - 1]->mmco_reset &&
                !h->delayed_pic[i]->f->key_frame;
         i++)
        if (h->delayed_pic[i]->poc < out->poc) {
            out     = h->delayed_pic[i];
            out_idx = i;
        }
    if (h->avctx->has_b_frames == 0 &&
        (h->delayed_pic[0]->f->key_frame || h->mmco_reset))
        h->next_outputed_poc = INT_MIN;
    out_of_order = !out->f->key_frame && !h->mmco_reset &&
                   (out->poc < h->next_outputed_poc);

    if (sps->bitstream_restriction_flag &&
        h->avctx->has_b_frames >= sps->num_reorder_frames) {
    } else if (out_of_order && pics - 1 == h->avctx->has_b_frames &&
               h->avctx->has_b_frames < MAX_DELAYED_PIC_COUNT) {
        if (invalid + cnt < MAX_DELAYED_PIC_COUNT) {
            h->avctx->has_b_frames = FFMAX(h->avctx->has_b_frames, cnt);
        }
    } else if (!h->avctx->has_b_frames &&
               ((h->next_outputed_poc != INT_MIN &&
                 out->poc > h->next_outputed_poc + 2) ||
                cur->f->pict_type == AV_PICTURE_TYPE_B)) {
        h->avctx->has_b_frames++;
    }

    if (pics > h->avctx->has_b_frames) {
        out->reference &= ~DELAYED_PIC_REF;
        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];
    }
    memmove(h->last_pocs, &h->last_pocs[1],
            sizeof(*h->last_pocs) * (MAX_DELAYED_PIC_COUNT - 1));
    h->last_pocs[MAX_DELAYED_PIC_COUNT - 1] = cur->poc;
    if (!out_of_order && pics > h->avctx->has_b_frames) {
        h->next_output_pic = out;
        if (out->mmco_reset) {
            if (out_idx > 0) {
                h->next_outputed_poc                    = out->poc;
                h->delayed_pic[out_idx - 1]->mmco_reset = out->mmco_reset;
            } else {
                h->next_outputed_poc = INT_MIN;
            }
        } else {
            if (out_idx == 0 && pics > 1 && h->delayed_pic[0]->f->key_frame) {
                h->next_outputed_poc = INT_MIN;
            } else {
                h->next_outputed_poc = out->poc;
            }
        }
        h->mmco_reset = 0;
    } else {
        av_log(h->avctx, AV_LOG_DEBUG, "no picture\n");
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

/**
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h)
{
    ff_h264_remove_all_refs(h);
    h->poc.prev_frame_num        =
    h->poc.prev_frame_num_offset =
    h->poc.prev_poc_msb          =
    h->poc.prev_poc_lsb          = 0;
}

/* forget old pics after a seek */
void ff_h264_flush_change(H264Context *h)
{
    int i;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
    h->next_outputed_poc = INT_MIN;
    h->prev_interlaced_frame = 1;
    idr(h);
    if (h->cur_pic_ptr)
        h->cur_pic_ptr->reference = 0;
    h->first_field = 0;
    ff_h264_sei_uninit(&h->sei);
    h->recovery_frame = -1;
    h->frame_recovered = 0;
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

static int get_last_needed_nal(H264Context *h)
{
    int nals_needed = 0;
    int i;

    for (i = 0; i < h->pkt.nb_nals; i++) {
        H2645NAL *nal = &h->pkt.nals[i];
        GetBitContext gb;

        /* packets can sometimes contain multiple PPS/SPS,
         * e.g. two PAFF field pictures in one packet, or a demuxer
         * which splits NALs strangely if so, when frame threading we
         * can't start the next thread until we've read all of them */
        switch (nal->type) {
        case H264_NAL_SPS:
        case H264_NAL_PPS:
            nals_needed = i;
            break;
        case H264_NAL_DPA:
        case H264_NAL_IDR_SLICE:
        case H264_NAL_SLICE:
            init_get_bits(&gb, nal->data + 1, (nal->size - 1) * 8);
            if (!get_ue_golomb(&gb))
                nals_needed = i;
        }
    }

    return nals_needed;
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size)
{
    AVCodecContext *const avctx = h->avctx;
    unsigned context_count = 0;
    int nals_needed = 0; ///< number of NALs that need decoding before the next frame thread starts
    int i, ret = 0;

    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)) {
        h->current_slice = 0;
        if (!h->first_field)
            h->cur_pic_ptr = NULL;
        ff_h264_sei_uninit(&h->sei);
    }

    ret = ff_h2645_packet_split(&h->pkt, buf, buf_size, avctx, h->is_avc,
                                h->nal_length_size, avctx->codec_id);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Error splitting the input into NAL units.\n");
        return ret;
    }

    if (avctx->active_thread_type & FF_THREAD_FRAME)
        nals_needed = get_last_needed_nal(h);

    for (i = 0; i < h->pkt.nb_nals; i++) {
        H2645NAL *nal = &h->pkt.nals[i];
        H264SliceContext *sl = &h->slice_ctx[context_count];
        int err;

        if (avctx->skip_frame >= AVDISCARD_NONREF &&
            nal->ref_idc == 0 && nal->type != H264_NAL_SEI)
            continue;

        // FIXME these should stop being context-global variables
        h->nal_ref_idc   = nal->ref_idc;
        h->nal_unit_type = nal->type;

        err = 0;
        switch (nal->type) {
        case H264_NAL_IDR_SLICE:
            idr(h); // FIXME ensure we don't lose some frames if there is reordering
        case H264_NAL_SLICE:
            sl->gb = nal->gb;

            if ((err = ff_h264_decode_slice_header(h, sl, nal)))
                break;

            if (sl->redundant_pic_count > 0)
                break;

            if (h->current_slice == 1) {
                if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS))
                    decode_postinit(h, i >= nals_needed);
            }

            if ((avctx->skip_frame < AVDISCARD_NONREF || nal->ref_idc) &&
                (avctx->skip_frame < AVDISCARD_BIDIR  ||
                 sl->slice_type_nos != AV_PICTURE_TYPE_B) &&
                (avctx->skip_frame < AVDISCARD_NONKEY ||
                 h->cur_pic_ptr->f->key_frame) &&
                avctx->skip_frame < AVDISCARD_ALL) {
                if (avctx->hwaccel) {
                    ret = avctx->hwaccel->decode_slice(avctx, nal->raw_data, nal->raw_size);
                    if (ret < 0)
                        return ret;
                } else
                    context_count++;
            }
            break;
        case H264_NAL_DPA:
        case H264_NAL_DPB:
        case H264_NAL_DPC:
            avpriv_request_sample(avctx, "data partitioning");
            ret = AVERROR(ENOSYS);
            goto end;
            break;
        case H264_NAL_SEI:
            ret = ff_h264_sei_decode(&h->sei, &nal->gb, &h->ps, avctx);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                goto end;
            break;
        case H264_NAL_SPS:
            ret = ff_h264_decode_seq_parameter_set(&nal->gb, avctx, &h->ps);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                goto end;
            break;
        case H264_NAL_PPS:
            ret = ff_h264_decode_picture_parameter_set(&nal->gb, avctx, &h->ps,
                                                       nal->size_bits);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                goto end;
            break;
        case H264_NAL_AUD:
        case H264_NAL_END_SEQUENCE:
        case H264_NAL_END_STREAM:
        case H264_NAL_FILLER_DATA:
        case H264_NAL_SPS_EXT:
        case H264_NAL_AUXILIARY_SLICE:
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG, "Unknown NAL code: %d (%d bits)\n",
                   nal->type, nal->size_bits);
        }

        if (context_count == h->nb_slice_ctx) {
            ret = ff_h264_execute_decode_slices(h, context_count);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                goto end;
            context_count = 0;
        }

        if (err < 0) {
            av_log(h->avctx, AV_LOG_ERROR, "decode_slice_header error\n");
            sl->ref_count[0] = sl->ref_count[1] = sl->list_count = 0;
        }
    }
    if (context_count) {
        ret = ff_h264_execute_decode_slices(h, context_count);
        if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
            goto end;
    }

    ret = 0;
end:
    /* clean up */
    if (h->cur_pic_ptr && !h->droppable) {
        ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                  h->picture_structure == PICT_BOTTOM_FIELD);
    }

    return (ret < 0) ? ret : buf_size;
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

static int output_frame(H264Context *h, AVFrame *dst, AVFrame *src)
{
    int i;
    int ret = av_frame_ref(dst, src);
    if (ret < 0)
        return ret;

    if (!h->ps.sps || !h->ps.sps->crop)
        return 0;

    for (i = 0; i < 3; i++) {
        int hshift = (i > 0) ? h->chroma_x_shift : 0;
        int vshift = (i > 0) ? h->chroma_y_shift : 0;
        int off    = ((h->ps.sps->crop_left >> hshift) << h->pixel_shift) +
                     (h->ps.sps->crop_top >> vshift) * dst->linesize[i];
        dst->data[i] += off;
    }
    return 0;
}

static int h264_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    H264Context *h     = avctx->priv_data;
    AVFrame *pict      = data;
    int buf_index      = 0;
    int ret;
    const uint8_t *new_extradata;
    int new_extradata_size;

    h->flags = avctx->flags;
    h->setup_finished = 0;

    /* end of stream, output what is still in the buffers */
out:
    if (buf_size == 0) {
        H264Picture *out;
        int i, out_idx;

        h->cur_pic_ptr = NULL;

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
            ret = output_frame(h, pict, out->f);
            if (ret < 0)
                return ret;
            *got_frame = 1;
        }

        return buf_index;
    }

    new_extradata_size = 0;
    new_extradata = av_packet_get_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA,
                                            &new_extradata_size);
    if (new_extradata_size > 0 && new_extradata) {
        ret = ff_h264_decode_extradata(new_extradata, new_extradata_size,
                                       &h->ps, &h->is_avc, &h->nal_length_size,
                                       avctx->err_recognition, avctx);
        if (ret < 0)
            return ret;
    }

    buf_index = decode_nal_units(h, buf, buf_size);
    if (buf_index < 0)
        return AVERROR_INVALIDDATA;

    if (!h->cur_pic_ptr && h->nal_unit_type == H264_NAL_END_SEQUENCE) {
        buf_size = 0;
        goto out;
    }

    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS) && !h->cur_pic_ptr) {
        if (avctx->skip_frame >= AVDISCARD_NONREF)
            return 0;
        av_log(avctx, AV_LOG_ERROR, "no frame!\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(avctx->flags2 & AV_CODEC_FLAG2_CHUNKS) ||
        (h->mb_y >= h->mb_height && h->mb_height)) {
        if (avctx->flags2 & AV_CODEC_FLAG2_CHUNKS)
            decode_postinit(h, 1);

        ff_h264_field_end(h, &h->slice_ctx[0], 0);

        *got_frame = 0;
        if (h->next_output_pic && ((avctx->flags & AV_CODEC_FLAG_OUTPUT_CORRUPT) ||
                                   h->next_output_pic->recovered)) {
            if (!h->next_output_pic->recovered)
                h->next_output_pic->f->flags |= AV_FRAME_FLAG_CORRUPT;

            ret = output_frame(h, pict, h->next_output_pic->f);
            if (ret < 0)
                return ret;
            *got_frame = 1;
        }
    }

    assert(pict->buf[0] || !*got_frame);

    return get_consumed_bytes(buf_index, buf_size);
}

#define OFFSET(x) offsetof(H264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption h264_options[] = {
    { "enable_er", "Enable error resilience on damaged frames (unsafe)", OFFSET(enable_er), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, VD },
    { NULL },
};

static const AVClass h264_class = {
    .class_name = "h264",
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
