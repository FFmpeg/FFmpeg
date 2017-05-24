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

#include "hwaccel.h"
#include "vaapi_decode.h"
#include "vp9shared.h"

static VASurfaceID vaapi_vp9_surface_id(const VP9Frame *vf)
{
    if (vf)
        return ff_vaapi_get_surface_id(vf->tf.f);
    else
        return VA_INVALID_SURFACE;
}

static int vaapi_vp9_start_frame(AVCodecContext          *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t       size)
{
    const VP9SharedContext *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    VADecPictureParameterBufferVP9 pic_param;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    int err, i;

    pic->output_surface = vaapi_vp9_surface_id(&h->frames[CUR_FRAME]);

    pic_param = (VADecPictureParameterBufferVP9) {
        .frame_width                      = avctx->width,
        .frame_height                     = avctx->height,

        .pic_fields.bits = {
            .subsampling_x                = pixdesc->log2_chroma_w,
            .subsampling_y                = pixdesc->log2_chroma_h,
            .frame_type                   = !h->h.keyframe,
            .show_frame                   = !h->h.invisible,
            .error_resilient_mode         = h->h.errorres,
            .intra_only                   = h->h.intraonly,
            .allow_high_precision_mv      = h->h.keyframe ? 0 : h->h.highprecisionmvs,
            .mcomp_filter_type            = h->h.filtermode ^ (h->h.filtermode <= 1),
            .frame_parallel_decoding_mode = h->h.parallelmode,
            .reset_frame_context          = h->h.resetctx,
            .refresh_frame_context        = h->h.refreshctx,
            .frame_context_idx            = h->h.framectxid,

            .segmentation_enabled          = h->h.segmentation.enabled,
            .segmentation_temporal_update  = h->h.segmentation.temporal,
            .segmentation_update_map       = h->h.segmentation.update_map,

            .last_ref_frame                = h->h.refidx[0],
            .last_ref_frame_sign_bias      = h->h.signbias[0],
            .golden_ref_frame              = h->h.refidx[1],
            .golden_ref_frame_sign_bias    = h->h.signbias[1],
            .alt_ref_frame                 = h->h.refidx[2],
            .alt_ref_frame_sign_bias       = h->h.signbias[2],
            .lossless_flag                 = h->h.lossless,
        },

        .filter_level                      = h->h.filter.level,
        .sharpness_level                   = h->h.filter.sharpness,
        .log2_tile_rows                    = h->h.tiling.log2_tile_rows,
        .log2_tile_columns                 = h->h.tiling.log2_tile_cols,

        .frame_header_length_in_bytes      = h->h.uncompressed_header_size,
        .first_partition_size              = h->h.compressed_header_size,

        .profile                           = h->h.profile,
        .bit_depth                         = h->h.bpp,
    };

    for (i = 0; i < 7; i++)
        pic_param.mb_segment_tree_probs[i] = h->h.segmentation.prob[i];

    if (h->h.segmentation.temporal) {
        for (i = 0; i < 3; i++)
            pic_param.segment_pred_probs[i] = h->h.segmentation.pred_prob[i];
    } else {
        memset(pic_param.segment_pred_probs, 255, sizeof(pic_param.segment_pred_probs));
    }

    for (i = 0; i < 8; i++) {
        if (h->refs[i].f->buf[0])
            pic_param.reference_frames[i] = ff_vaapi_get_surface_id(h->refs[i].f);
        else
            pic_param.reference_frames[i] = VA_INVALID_ID;
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0) {
        ff_vaapi_decode_cancel(avctx, pic);
        return err;
    }

    return 0;
}

static int vaapi_vp9_end_frame(AVCodecContext *avctx)
{
    const VP9SharedContext *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->frames[CUR_FRAME].hwaccel_picture_private;

    return ff_vaapi_decode_issue(avctx, pic);
}

static int vaapi_vp9_decode_slice(AVCodecContext *avctx,
                                  const uint8_t  *buffer,
                                  uint32_t        size)
{
    const VP9SharedContext *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    VASliceParameterBufferVP9 slice_param;
    int err, i;

    slice_param = (VASliceParameterBufferVP9) {
        .slice_data_size   = size,
        .slice_data_offset = 0,
        .slice_data_flag   = VA_SLICE_DATA_FLAG_ALL,
    };

    for (i = 0; i < 8; i++) {
        slice_param.seg_param[i] = (VASegmentParameterVP9) {
            .segment_flags.fields = {
                .segment_reference_enabled = h->h.segmentation.feat[i].ref_enabled,
                .segment_reference         = h->h.segmentation.feat[i].ref_val,
                .segment_reference_skipped = h->h.segmentation.feat[i].skip_enabled,
            },

            .luma_dc_quant_scale           = h->h.segmentation.feat[i].qmul[0][0],
            .luma_ac_quant_scale           = h->h.segmentation.feat[i].qmul[0][1],
            .chroma_dc_quant_scale         = h->h.segmentation.feat[i].qmul[1][0],
            .chroma_ac_quant_scale         = h->h.segmentation.feat[i].qmul[1][1],
        };

        memcpy(slice_param.seg_param[i].filter_level, h->h.segmentation.feat[i].lflvl, sizeof(slice_param.seg_param[i].filter_level));
    }

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic,
                                            &slice_param, sizeof(slice_param),
                                            buffer, size);
    if (err) {
        ff_vaapi_decode_cancel(avctx, pic);
        return err;
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
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = ff_vaapi_decode_init,
    .uninit               = ff_vaapi_decode_uninit,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
