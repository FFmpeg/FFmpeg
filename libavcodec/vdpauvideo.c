/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, H.264 and VC-1.
 *
 * Copyright (c) 2008 NVIDIA
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
#include "vc1.h"

#undef NDEBUG
#include <assert.h>

#include "vdpau.h"
#include "vdpau_internal.h"

/**
 * \addtogroup VDPAU_Decoding
 *
 * @{
 */

void ff_vdpau_h264_set_reference_frames(MpegEncContext *s)
{
    H264Context *h = s->avctx->priv_data;
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

void ff_vdpau_add_data_chunk(MpegEncContext *s,
                             const uint8_t *buf, int buf_size)
{
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

void ff_vdpau_h264_picture_complete(MpegEncContext *s)
{
    H264Context *h = s->avctx->priv_data;
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

void ff_vdpau_mpeg_picture_complete(MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count)
{
    struct vdpau_render_state * render, * last, * next;
    int i;

    render = (struct vdpau_render_state*)s->current_picture_ptr->data[0];
    assert(render);

    /* fill VdpPictureInfoMPEG1Or2 struct */
    render->info.mpeg.picture_structure          = s->picture_structure;
    render->info.mpeg.picture_coding_type        = s->pict_type;
    render->info.mpeg.intra_dc_precision         = s->intra_dc_precision;
    render->info.mpeg.frame_pred_frame_dct       = s->frame_pred_frame_dct;
    render->info.mpeg.concealment_motion_vectors = s->concealment_motion_vectors;
    render->info.mpeg.intra_vlc_format           = s->intra_vlc_format;
    render->info.mpeg.alternate_scan             = s->alternate_scan;
    render->info.mpeg.q_scale_type               = s->q_scale_type;
    render->info.mpeg.top_field_first            = s->top_field_first;
    render->info.mpeg.full_pel_forward_vector    = s->full_pel[0]; // MPEG-1 only.  Set 0 for MPEG-2
    render->info.mpeg.full_pel_backward_vector   = s->full_pel[1]; // MPEG-1 only.  Set 0 for MPEG-2
    render->info.mpeg.f_code[0][0]               = s->mpeg_f_code[0][0]; // For MPEG-1 fill both horiz. & vert.
    render->info.mpeg.f_code[0][1]               = s->mpeg_f_code[0][1];
    render->info.mpeg.f_code[1][0]               = s->mpeg_f_code[1][0];
    render->info.mpeg.f_code[1][1]               = s->mpeg_f_code[1][1];
    for (i = 0; i < 64; ++i) {
        render->info.mpeg.intra_quantizer_matrix[i]     = s->intra_matrix[i];
        render->info.mpeg.non_intra_quantizer_matrix[i] = s->inter_matrix[i];
    }

    render->info.mpeg.forward_reference          = VDP_INVALID_HANDLE;
    render->info.mpeg.backward_reference         = VDP_INVALID_HANDLE;

    switch(s->pict_type){
    case  FF_B_TYPE:
        next = (struct vdpau_render_state*)s->next_picture.data[0];
        assert(next);
        render->info.mpeg.backward_reference     = next->surface;
        // no return here, going to set forward prediction
    case  FF_P_TYPE:
        last = (struct vdpau_render_state*)s->last_picture.data[0];
        if (!last) // FIXME: Does this test make sense?
            last = render; // predict second field from the first
        render->info.mpeg.forward_reference      = last->surface;
    }

    ff_vdpau_add_data_chunk(s, buf, buf_size);

    render->info.mpeg.slice_count                = slice_count;

    if (slice_count)
        ff_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used               = 0;
}

void ff_vdpau_vc1_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                 int buf_size)
{
    VC1Context *v = s->avctx->priv_data;
    struct vdpau_render_state * render, * last, * next;

    render = (struct vdpau_render_state*)s->current_picture.data[0];
    assert(render);

    /*  fill LvPictureInfoVC1 struct */
    render->info.vc1.frame_coding_mode  = v->fcm;
    render->info.vc1.postprocflag       = v->postprocflag;
    render->info.vc1.pulldown           = v->broadcast;
    render->info.vc1.interlace          = v->interlace;
    render->info.vc1.tfcntrflag         = v->tfcntrflag;
    render->info.vc1.finterpflag        = v->finterpflag;
    render->info.vc1.psf                = v->psf;
    render->info.vc1.dquant             = v->dquant;
    render->info.vc1.panscan_flag       = v->panscanflag;
    render->info.vc1.refdist_flag       = v->refdist_flag;
    render->info.vc1.quantizer          = v->quantizer_mode;
    render->info.vc1.extended_mv        = v->extended_mv;
    render->info.vc1.extended_dmv       = v->extended_dmv;
    render->info.vc1.overlap            = v->overlap;
    render->info.vc1.vstransform        = v->vstransform;
    render->info.vc1.loopfilter         = v->s.loop_filter;
    render->info.vc1.fastuvmc           = v->fastuvmc;
    render->info.vc1.range_mapy_flag    = v->range_mapy_flag;
    render->info.vc1.range_mapy         = v->range_mapy;
    render->info.vc1.range_mapuv_flag   = v->range_mapuv_flag;
    render->info.vc1.range_mapuv        = v->range_mapuv;
    /* Specific to simple/main profile only */
    render->info.vc1.multires           = v->multires;
    render->info.vc1.syncmarker         = v->s.resync_marker;
    render->info.vc1.rangered           = v->rangered;
    render->info.vc1.maxbframes         = v->s.max_b_frames;

    render->info.vc1.deblockEnable      = v->postprocflag & 1;
    render->info.vc1.pquant             = v->pq;

    render->info.vc1.forward_reference  = VDP_INVALID_HANDLE;
    render->info.vc1.backward_reference = VDP_INVALID_HANDLE;

    if (v->bi_type)
        render->info.vc1.picture_type = 4;
    else
        render->info.vc1.picture_type = s->pict_type - 1 + s->pict_type / 3;

    switch(s->pict_type){
    case  FF_B_TYPE:
        next = (struct vdpau_render_state*)s->next_picture.data[0];
        assert(next);
        render->info.vc1.backward_reference = next->surface;
        // no break here, going to set forward prediction
    case  FF_P_TYPE:
        last = (struct vdpau_render_state*)s->last_picture.data[0];
        if (!last) // FIXME: Does this test make sense?
            last = render; // predict second field from the first
        render->info.vc1.forward_reference = last->surface;
    }

    ff_vdpau_add_data_chunk(s, buf, buf_size);

    render->info.vc1.slice_count          = 1;

    ff_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used        = 0;
}

/* @}*/
