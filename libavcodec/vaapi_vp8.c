/*
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

#include <va/va.h>
#include <va/va_dec_vp8.h>

#include "hwconfig.h"
#include "vaapi_decode.h"
#include "vp8.h"

static VASurfaceID vaapi_vp8_surface_id(VP8Frame *vf)
{
    if (vf)
        return ff_vaapi_get_surface_id(vf->tf.f);
    else
        return VA_INVALID_SURFACE;
}

static int vaapi_vp8_start_frame(AVCodecContext          *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t       size)
{
    const VP8Context *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->framep[VP56_FRAME_CURRENT]->hwaccel_picture_private;
    VAPictureParameterBufferVP8 pp;
    VAProbabilityDataBufferVP8 prob;
    VAIQMatrixBufferVP8 quant;
    int err, i, j, k;

    pic->output_surface = vaapi_vp8_surface_id(s->framep[VP56_FRAME_CURRENT]);

    pp = (VAPictureParameterBufferVP8) {
        .frame_width                     = avctx->width,
        .frame_height                    = avctx->height,

        .last_ref_frame                  = vaapi_vp8_surface_id(s->framep[VP56_FRAME_PREVIOUS]),
        .golden_ref_frame                = vaapi_vp8_surface_id(s->framep[VP56_FRAME_GOLDEN]),
        .alt_ref_frame                   = vaapi_vp8_surface_id(s->framep[VP56_FRAME_GOLDEN2]),
        .out_of_loop_frame               = VA_INVALID_SURFACE,

        .pic_fields.bits = {
            .key_frame                   = !s->keyframe,
            .version                     = s->profile,

            .segmentation_enabled        = s->segmentation.enabled,
            .update_mb_segmentation_map  = s->segmentation.update_map,
            .update_segment_feature_data = s->segmentation.update_feature_data,

            .filter_type                 = s->filter.simple,
            .sharpness_level             = s->filter.sharpness,

            .loop_filter_adj_enable      = s->lf_delta.enabled,
            .mode_ref_lf_delta_update    = s->lf_delta.update,

            .sign_bias_golden            = s->sign_bias[VP56_FRAME_GOLDEN],
            .sign_bias_alternate         = s->sign_bias[VP56_FRAME_GOLDEN2],

            .mb_no_coeff_skip            = s->mbskip_enabled,
            .loop_filter_disable         = s->filter.level == 0,
        },

        .prob_skip_false                 = s->prob->mbskip,
        .prob_intra                      = s->prob->intra,
        .prob_last                       = s->prob->last,
        .prob_gf                         = s->prob->golden,
    };

    for (i = 0; i < 3; i++)
        pp.mb_segment_tree_probs[i] = s->prob->segmentid[i];

    for (i = 0; i < 4; i++) {
        if (s->segmentation.enabled) {
            pp.loop_filter_level[i] = s->segmentation.filter_level[i];
            if (!s->segmentation.absolute_vals)
                pp.loop_filter_level[i] += s->filter.level;
        } else {
            pp.loop_filter_level[i] = s->filter.level;
        }
        pp.loop_filter_level[i] = av_clip_uintp2(pp.loop_filter_level[i], 6);
    }

    for (i = 0; i < 4; i++) {
        pp.loop_filter_deltas_ref_frame[i] = s->lf_delta.ref[i];
        pp.loop_filter_deltas_mode[i] = s->lf_delta.mode[i + 4];
    }

    if (s->keyframe) {
        static const uint8_t keyframe_y_mode_probs[4] = {
            145, 156, 163, 128
        };
        static const uint8_t keyframe_uv_mode_probs[3] = {
            142, 114, 183
        };
        memcpy(pp.y_mode_probs,  keyframe_y_mode_probs,  4);
        memcpy(pp.uv_mode_probs, keyframe_uv_mode_probs, 3);
    } else {
        for (i = 0; i < 4; i++)
            pp.y_mode_probs[i] = s->prob->pred16x16[i];
        for (i = 0; i < 3; i++)
            pp.uv_mode_probs[i] = s->prob->pred8x8c[i];
    }
    for (i = 0; i < 2; i++)
        for (j = 0; j < 19; j++)
            pp.mv_probs[i][j] = s->prob->mvc[i][j];

    pp.bool_coder_ctx.range = s->coder_state_at_header_end.range;
    pp.bool_coder_ctx.value = s->coder_state_at_header_end.value;
    pp.bool_coder_ctx.count = s->coder_state_at_header_end.bit_count;

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pp, sizeof(pp));
    if (err < 0)
        goto fail;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 8; j++) {
            static const int coeff_bands_inverse[8] = {
                0, 1, 2, 3, 5, 6, 4, 15
            };
            int coeff_pos = coeff_bands_inverse[j];

            for (k = 0; k < 3; k++) {
                memcpy(prob.dct_coeff_probs[i][j][k],
                       s->prob->token[i][coeff_pos][k], 11);
            }
        }
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAProbabilityBufferType,
                                            &prob, sizeof(prob));
    if (err < 0)
        goto fail;

    for (i = 0; i < 4; i++) {
        int base_qi = s->segmentation.base_quant[i];
        if (!s->segmentation.absolute_vals)
            base_qi += s->quant.yac_qi;

        quant.quantization_index[i][0] = av_clip_uintp2(base_qi,                       7);
        quant.quantization_index[i][1] = av_clip_uintp2(base_qi + s->quant.ydc_delta,  7);
        quant.quantization_index[i][2] = av_clip_uintp2(base_qi + s->quant.y2dc_delta, 7);
        quant.quantization_index[i][3] = av_clip_uintp2(base_qi + s->quant.y2ac_delta, 7);
        quant.quantization_index[i][4] = av_clip_uintp2(base_qi + s->quant.uvdc_delta, 7);
        quant.quantization_index[i][5] = av_clip_uintp2(base_qi + s->quant.uvac_delta, 7);
    }

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAIQMatrixBufferType,
                                            &quant, sizeof(quant));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

static int vaapi_vp8_end_frame(AVCodecContext *avctx)
{
    const VP8Context *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->framep[VP56_FRAME_CURRENT]->hwaccel_picture_private;

    return ff_vaapi_decode_issue(avctx, pic);
}

static int vaapi_vp8_decode_slice(AVCodecContext *avctx,
                                  const uint8_t  *buffer,
                                  uint32_t        size)
{
    const VP8Context *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->framep[VP56_FRAME_CURRENT]->hwaccel_picture_private;
    VASliceParameterBufferVP8 sp;
    int err, i;

    unsigned int header_size = 3 + 7 * s->keyframe;
    const uint8_t *data = buffer + header_size;
    unsigned int data_size = size - header_size;

    sp = (VASliceParameterBufferVP8) {
        .slice_data_size   = data_size,
        .slice_data_offset = 0,
        .slice_data_flag   = VA_SLICE_DATA_FLAG_ALL,

        .macroblock_offset = (8 * (s->coder_state_at_header_end.input - data) -
                              s->coder_state_at_header_end.bit_count - 8),
        .num_of_partitions = s->num_coeff_partitions + 1,
    };

    sp.partition_size[0] = s->header_partition_size - ((sp.macroblock_offset + 7) / 8);
    for (i = 0; i < 8; i++)
        sp.partition_size[i+1] = s->coeff_partition_size[i];

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic, &sp, sizeof(sp), data, data_size);
    if (err)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

const AVHWAccel ff_vp8_vaapi_hwaccel = {
    .name                 = "vp8_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_VP8,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_vp8_start_frame,
    .end_frame            = &vaapi_vp8_end_frame,
    .decode_slice         = &vaapi_vp8_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .frame_params         = &ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
