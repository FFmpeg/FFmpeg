/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, H.264 and VC-1.
 *
 * Copyright (c) 2008 NVIDIA.
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

#include <limits.h>
#include "avcodec.h"
#include "h264.h"

#undef NDEBUG
#include <assert.h>

#include "vdpau.h"
#include "vdpau_internal.h"

/**
 * \addtogroup VDPAU_Decoding
 *
 * @{
 */

void ff_vdpau_h264_set_reference_frames(H264Context *h)
{
    MpegEncContext * s = &h->s;
    struct vdpau_render_state * render, * render_ref;
    VdpReferenceFrameH264 * rf, * rf2;
    Picture * pic;
    int i, list, pic_frame_idx;

    render = (struct vdpau_render_state*)s->current_picture_ptr->data[0];
    assert(render);

    rf = &render->info.h264.referenceFrames[0];
#define H264_RF_COUNT FF_ARRAY_ELEMS(render->info.h264.referenceFrames)

    for (list = 0; list < 2; ++list) {
        Picture **lp = list ? h->long_ref : h->short_ref;
        int ls = list ? h->long_ref_count : h->short_ref_count;

        for (i = 0; i < ls; ++i) {
            pic = lp[i];
            if (!pic || !pic->reference)
                continue;
            pic_frame_idx = pic->long_ref ? pic->pic_id : pic->frame_num;

            render_ref = (struct vdpau_render_state*)pic->data[0];
            assert(render_ref);

            rf2 = &render->info.h264.referenceFrames[0];
            while (rf2 != rf) {
                if (
                    (rf2->surface == render_ref->surface)
                    && (rf2->is_long_term == pic->long_ref)
                    && (rf2->frame_idx == pic_frame_idx)
                )
                    break;
                ++rf2;
            }
            if (rf2 != rf) {
                rf2->top_is_reference    |= (pic->reference & PICT_TOP_FIELD)    ? VDP_TRUE : VDP_FALSE;
                rf2->bottom_is_reference |= (pic->reference & PICT_BOTTOM_FIELD) ? VDP_TRUE : VDP_FALSE;
                continue;
            }

            if (rf >= &render->info.h264.referenceFrames[H264_RF_COUNT])
                continue;

            rf->surface             = render_ref->surface;
            rf->is_long_term        = pic->long_ref;
            rf->top_is_reference    = (pic->reference & PICT_TOP_FIELD)    ? VDP_TRUE : VDP_FALSE;
            rf->bottom_is_reference = (pic->reference & PICT_BOTTOM_FIELD) ? VDP_TRUE : VDP_FALSE;
            rf->field_order_cnt[0]  = pic->field_poc[0];
            rf->field_order_cnt[1]  = pic->field_poc[1];
            rf->frame_idx           = pic_frame_idx;

            ++rf;
        }
    }

    for (; rf < &render->info.h264.referenceFrames[H264_RF_COUNT]; ++rf) {
        rf->surface             = VDP_INVALID_HANDLE;
        rf->is_long_term        = 0;
        rf->top_is_reference    = 0;
        rf->bottom_is_reference = 0;
        rf->field_order_cnt[0]  = 0;
        rf->field_order_cnt[1]  = 0;
        rf->frame_idx           = 0;
    }
}

void ff_vdpau_h264_add_data_chunk(H264Context *h, const uint8_t *buf, int buf_size)
{
    MpegEncContext * s = &h->s;
    struct vdpau_render_state * render;

    render = (struct vdpau_render_state*)s->current_picture_ptr->data[0];
    assert(render);

    render->bitstream_buffers= av_fast_realloc(
        render->bitstream_buffers,
        &render->bitstream_buffers_allocated,
        sizeof(*render->bitstream_buffers)*(render->bitstream_buffers_used + 1)
    );

    render->bitstream_buffers[render->bitstream_buffers_used].struct_version  = VDP_BITSTREAM_BUFFER_VERSION;
    render->bitstream_buffers[render->bitstream_buffers_used].bitstream       = buf;
    render->bitstream_buffers[render->bitstream_buffers_used].bitstream_bytes = buf_size;
    render->bitstream_buffers_used++;
}

void ff_vdpau_h264_picture_complete(H264Context *h)
{
    MpegEncContext * s = &h->s;
    struct vdpau_render_state * render;

    render = (struct vdpau_render_state*)s->current_picture_ptr->data[0];
    assert(render);

    render->info.h264.slice_count = h->slice_num;
    if (render->info.h264.slice_count < 1)
        return;

    for (int i = 0; i < 2; ++i) {
        int foc = s->current_picture_ptr->field_poc[i];
        if (foc == INT_MAX)
            foc = 0;
        render->info.h264.field_order_cnt[i] = foc;
    }

    render->info.h264.is_reference                           = s->current_picture_ptr->reference ? VDP_TRUE : VDP_FALSE;
    render->info.h264.frame_num                              = h->frame_num;
    render->info.h264.field_pic_flag                         = s->picture_structure != PICT_FRAME;
    render->info.h264.bottom_field_flag                      = s->picture_structure == PICT_BOTTOM_FIELD;
    render->info.h264.num_ref_frames                         = h->sps.ref_frame_count;
    render->info.h264.mb_adaptive_frame_field_flag           = h->sps.mb_aff;
    render->info.h264.constrained_intra_pred_flag            = h->pps.constrained_intra_pred;
    render->info.h264.weighted_pred_flag                     = h->pps.weighted_pred;
    render->info.h264.weighted_bipred_idc                    = h->pps.weighted_bipred_idc;
    render->info.h264.frame_mbs_only_flag                    = h->sps.frame_mbs_only_flag;
    render->info.h264.transform_8x8_mode_flag                = h->pps.transform_8x8_mode;
    render->info.h264.chroma_qp_index_offset                 = h->pps.chroma_qp_index_offset[0];
    render->info.h264.second_chroma_qp_index_offset          = h->pps.chroma_qp_index_offset[1];
    render->info.h264.pic_init_qp_minus26                    = h->pps.init_qp - 26;
    render->info.h264.num_ref_idx_l0_active_minus1           = h->pps.ref_count[0] - 1;
    render->info.h264.num_ref_idx_l1_active_minus1           = h->pps.ref_count[1] - 1;
    render->info.h264.log2_max_frame_num_minus4              = h->sps.log2_max_frame_num - 4;
    render->info.h264.pic_order_cnt_type                     = h->sps.poc_type;
    render->info.h264.log2_max_pic_order_cnt_lsb_minus4      = h->sps.log2_max_poc_lsb - 4;
    render->info.h264.delta_pic_order_always_zero_flag       = h->sps.delta_pic_order_always_zero_flag;
    render->info.h264.direct_8x8_inference_flag              = h->sps.direct_8x8_inference_flag;
    render->info.h264.entropy_coding_mode_flag               = h->pps.cabac;
    render->info.h264.pic_order_present_flag                 = h->pps.pic_order_present;
    render->info.h264.deblocking_filter_control_present_flag = h->pps.deblocking_filter_parameters_present;
    render->info.h264.redundant_pic_cnt_present_flag         = h->pps.redundant_pic_cnt_present;
    memcpy(render->info.h264.scaling_lists_4x4, h->pps.scaling_matrix4, sizeof(render->info.h264.scaling_lists_4x4));
    memcpy(render->info.h264.scaling_lists_8x8, h->pps.scaling_matrix8, sizeof(render->info.h264.scaling_lists_8x8));

    ff_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used = 0;
}

/* @}*/
