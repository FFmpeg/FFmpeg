/*
 * VP9 HW decode acceleration through VA API
 *
 * Copyright (C) 2015 Timo Rothenpieler <timo@rothenpieler.org>
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

#include "libavutil/pixdesc.h"
#include "vaapi_internal.h"
#include "vp9.h"

static void fill_picture_parameters(AVCodecContext                 *avctx,
                                    const VP9SharedContext         *h,
                                    VADecPictureParameterBufferVP9 *pp)
{
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    int i;

    pp->frame_width = avctx->width;
    pp->frame_height = avctx->height;

    pp->frame_header_length_in_bytes = h->h.uncompressed_header_size;
    pp->first_partition_size = h->h.compressed_header_size;

    pp->profile = h->h.profile;

    pp->filter_level = h->h.filter.level;
    pp->sharpness_level = h->h.filter.sharpness;
    pp->log2_tile_rows = h->h.tiling.log2_tile_rows;
    pp->log2_tile_columns = h->h.tiling.log2_tile_cols;

    pp->pic_fields.bits.subsampling_x = pixdesc->log2_chroma_w;
    pp->pic_fields.bits.subsampling_y = pixdesc->log2_chroma_h;
    pp->pic_fields.bits.frame_type = !h->h.keyframe;
    pp->pic_fields.bits.show_frame = !h->h.invisible;
    pp->pic_fields.bits.error_resilient_mode = h->h.errorres;
    pp->pic_fields.bits.intra_only = h->h.intraonly;
    pp->pic_fields.bits.allow_high_precision_mv = h->h.keyframe ? 0 : h->h.highprecisionmvs;
    pp->pic_fields.bits.mcomp_filter_type = h->h.filtermode ^ (h->h.filtermode <= 1);
    pp->pic_fields.bits.frame_parallel_decoding_mode = h->h.parallelmode;
    pp->pic_fields.bits.reset_frame_context = h->h.resetctx;
    pp->pic_fields.bits.refresh_frame_context = h->h.refreshctx;
    pp->pic_fields.bits.frame_context_idx = h->h.framectxid;

    pp->pic_fields.bits.segmentation_enabled = h->h.segmentation.enabled;
    pp->pic_fields.bits.segmentation_temporal_update = h->h.segmentation.temporal;
    pp->pic_fields.bits.segmentation_update_map = h->h.segmentation.update_map;

    pp->pic_fields.bits.last_ref_frame = h->h.refidx[0];
    pp->pic_fields.bits.last_ref_frame_sign_bias = h->h.signbias[0];
    pp->pic_fields.bits.golden_ref_frame = h->h.refidx[1];
    pp->pic_fields.bits.golden_ref_frame_sign_bias = h->h.signbias[1];
    pp->pic_fields.bits.alt_ref_frame = h->h.refidx[2];
    pp->pic_fields.bits.alt_ref_frame_sign_bias = h->h.signbias[2];
    pp->pic_fields.bits.lossless_flag = h->h.lossless;

    for (i = 0; i < 7; i++)
        pp->mb_segment_tree_probs[i] = h->h.segmentation.prob[i];

    if (h->h.segmentation.temporal) {
        for (i = 0; i < 3; i++)
            pp->segment_pred_probs[i] = h->h.segmentation.pred_prob[i];
    } else {
        memset(pp->segment_pred_probs, 255, sizeof(pp->segment_pred_probs));
    }

    for (i = 0; i < 8; i++) {
        if (h->refs[i].f->buf[0]) {
            pp->reference_frames[i] = ff_vaapi_get_surface_id(h->refs[i].f);
        } else {
            pp->reference_frames[i] = VA_INVALID_ID;
        }
    }
}

static int vaapi_vp9_start_frame(AVCodecContext          *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t       size)
{
    const VP9SharedContext *h = avctx->priv_data;
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    VADecPictureParameterBufferVP9 *pic_param;

    vactx->slice_param_size = sizeof(VASliceParameterBufferVP9);

    pic_param = ff_vaapi_alloc_pic_param(vactx, sizeof(VADecPictureParameterBufferVP9));
    if (!pic_param)
        return -1;
    fill_picture_parameters(avctx, h, pic_param);

    return 0;
}

static int vaapi_vp9_end_frame(AVCodecContext *avctx)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    const VP9SharedContext *h = avctx->priv_data;
    int ret;

    ret = ff_vaapi_commit_slices(vactx);
    if (ret < 0)
        goto finish;

    ret = ff_vaapi_render_picture(vactx, ff_vaapi_get_surface_id(h->frames[CUR_FRAME].tf.f));
    if (ret < 0)
        goto finish;

finish:
    ff_vaapi_common_end_frame(avctx);
    return ret;
}

static int vaapi_vp9_decode_slice(AVCodecContext *avctx,
                                  const uint8_t  *buffer,
                                  uint32_t        size)
{
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    const VP9SharedContext *h = avctx->priv_data;
    VASliceParameterBufferVP9 *slice_param;
    int i;

    slice_param = (VASliceParameterBufferVP9*)ff_vaapi_alloc_slice(vactx, buffer, size);
    if (!slice_param)
        return -1;

    for (i = 0; i < 8; i++) {
        slice_param->seg_param[i].segment_flags.fields.segment_reference_enabled = h->h.segmentation.feat[i].ref_enabled;
        slice_param->seg_param[i].segment_flags.fields.segment_reference = h->h.segmentation.feat[i].ref_val;
        slice_param->seg_param[i].segment_flags.fields.segment_reference_skipped = h->h.segmentation.feat[i].skip_enabled;

        memcpy(slice_param->seg_param[i].filter_level, h->h.segmentation.feat[i].lflvl, sizeof(slice_param->seg_param[i].filter_level));

        slice_param->seg_param[i].luma_dc_quant_scale = h->h.segmentation.feat[i].qmul[0][0];
        slice_param->seg_param[i].luma_ac_quant_scale = h->h.segmentation.feat[i].qmul[0][1];
        slice_param->seg_param[i].chroma_dc_quant_scale = h->h.segmentation.feat[i].qmul[1][0];
        slice_param->seg_param[i].chroma_ac_quant_scale = h->h.segmentation.feat[i].qmul[1][1];
    }

    return 0;
}

AVHWAccel ff_vp9_vaapi_hwaccel = {
    .name                 = "vp9_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_VP9,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = vaapi_vp9_start_frame,
    .end_frame            = vaapi_vp9_end_frame,
    .decode_slice         = vaapi_vp9_decode_slice,
    .init                 = ff_vaapi_context_init,
    .uninit               = ff_vaapi_context_fini,
    .priv_data_size       = sizeof(FFVAContext),
};
