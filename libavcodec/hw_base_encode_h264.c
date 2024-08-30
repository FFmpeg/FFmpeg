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

#include "hw_base_encode_h264.h"

#include "h2645data.h"
#include "h264_levels.h"

#include "libavutil/pixdesc.h"

int ff_hw_base_encode_init_params_h264(FFHWBaseEncodeContext *base_ctx,
                                       AVCodecContext *avctx,
                                       FFHWBaseEncodeH264 *common,
                                       FFHWBaseEncodeH264Opts *opts)
{
    H264RawSPS *sps = &common->raw_sps;
    H264RawPPS *pps = &common->raw_pps;
    const AVPixFmtDescriptor *desc;
    int bit_depth;

    memset(sps, 0, sizeof(*sps));
    memset(pps, 0, sizeof(*pps));

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);
    if (desc->nb_components == 1 || desc->log2_chroma_w != 1 || desc->log2_chroma_h != 1) {
        av_log(avctx, AV_LOG_ERROR, "Chroma format of input pixel format "
                "%s is not supported.\n", desc->name);
        return AVERROR(EINVAL);
    }
    bit_depth = desc->comp[0].depth;

    sps->nal_unit_header.nal_ref_idc   = 3;
    sps->nal_unit_header.nal_unit_type = H264_NAL_SPS;

    sps->profile_idc = avctx->profile & 0xff;

    if (avctx->profile == AV_PROFILE_H264_CONSTRAINED_BASELINE ||
        avctx->profile == AV_PROFILE_H264_MAIN)
        sps->constraint_set1_flag = 1;

    if (avctx->profile == AV_PROFILE_H264_HIGH || avctx->profile == AV_PROFILE_H264_HIGH_10)
        sps->constraint_set3_flag = base_ctx->gop_size == 1;

    if (avctx->profile == AV_PROFILE_H264_MAIN ||
        avctx->profile == AV_PROFILE_H264_HIGH || avctx->profile == AV_PROFILE_H264_HIGH_10) {
        sps->constraint_set4_flag = 1;
        sps->constraint_set5_flag = base_ctx->b_per_p == 0;
    }

    if (base_ctx->gop_size == 1)
        common->dpb_frames = 0;
    else
        common->dpb_frames = 1 + base_ctx->max_b_depth;

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        sps->level_idc = avctx->level;
    } else {
        const H264LevelDescriptor *level;
        int framerate;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = avctx->framerate.num / avctx->framerate.den;
        else
            framerate = 0;

        level = ff_h264_guess_level(sps->profile_idc,
                                    opts->bit_rate,
                                    framerate,
                                    opts->mb_width  * 16,
                                    opts->mb_height * 16,
                                    common->dpb_frames);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            if (level->constraint_set3_flag)
                sps->constraint_set3_flag = 1;
            sps->level_idc = level->level_idc;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Stream will not conform "
                   "to any level: using level 6.2.\n");
            sps->level_idc = 62;
        }
    }

    sps->seq_parameter_set_id = 0;
    sps->chroma_format_idc    = 1;
    sps->bit_depth_luma_minus8 = bit_depth - 8;
    sps->bit_depth_chroma_minus8 = bit_depth - 8;

    sps->log2_max_frame_num_minus4 = 4;
    sps->pic_order_cnt_type        = base_ctx->max_b_depth ? 0 : 2;
    if (sps->pic_order_cnt_type == 0) {
        sps->log2_max_pic_order_cnt_lsb_minus4 = 4;
    }

    sps->max_num_ref_frames = common->dpb_frames;

    sps->pic_width_in_mbs_minus1        = opts->mb_width  - 1;
    sps->pic_height_in_map_units_minus1 = opts->mb_height - 1;

    sps->frame_mbs_only_flag = 1;
    sps->direct_8x8_inference_flag = 1;

    if (avctx->width  != 16 * opts->mb_width ||
        avctx->height != 16 * opts->mb_height) {
        sps->frame_cropping_flag = 1;

        sps->frame_crop_left_offset   = 0;
        sps->frame_crop_right_offset  =
            (16 * opts->mb_width - avctx->width) / 2;
        sps->frame_crop_top_offset    = 0;
        sps->frame_crop_bottom_offset =
            (16 * opts->mb_height - avctx->height) / 2;
    } else {
        sps->frame_cropping_flag = 0;
    }

    sps->vui_parameters_present_flag = 1;

    if (avctx->sample_aspect_ratio.num != 0 &&
        avctx->sample_aspect_ratio.den != 0) {
        int num, den, i;
        av_reduce(&num, &den, avctx->sample_aspect_ratio.num,
                  avctx->sample_aspect_ratio.den, 65535);
        for (i = 0; i < FF_ARRAY_ELEMS(ff_h2645_pixel_aspect); i++) {
            if (num == ff_h2645_pixel_aspect[i].num &&
                den == ff_h2645_pixel_aspect[i].den) {
                sps->vui.aspect_ratio_idc = i;
                break;
            }
        }
        if (i >= FF_ARRAY_ELEMS(ff_h2645_pixel_aspect)) {
            sps->vui.aspect_ratio_idc = 255;
            sps->vui.sar_width  = num;
            sps->vui.sar_height = den;
        }
        sps->vui.aspect_ratio_info_present_flag = 1;
    }

    // Unspecified video format, from table E-2.
    sps->vui.video_format             = 5;
    sps->vui.video_full_range_flag    =
        avctx->color_range == AVCOL_RANGE_JPEG;
    sps->vui.colour_primaries         = avctx->color_primaries;
    sps->vui.transfer_characteristics = avctx->color_trc;
    sps->vui.matrix_coefficients      = avctx->colorspace;
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
        avctx->colorspace      != AVCOL_SPC_UNSPECIFIED)
        sps->vui.colour_description_present_flag = 1;
    if (avctx->color_range     != AVCOL_RANGE_UNSPECIFIED ||
        sps->vui.colour_description_present_flag)
        sps->vui.video_signal_type_present_flag = 1;

    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED) {
        sps->vui.chroma_loc_info_present_flag = 1;
        sps->vui.chroma_sample_loc_type_top_field    =
        sps->vui.chroma_sample_loc_type_bottom_field =
            avctx->chroma_sample_location - 1;
    }

    sps->vui.timing_info_present_flag = 1;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        sps->vui.num_units_in_tick = avctx->framerate.den;
        sps->vui.time_scale        = 2 * avctx->framerate.num;
        sps->vui.fixed_frame_rate_flag = 1;
    } else {
        sps->vui.num_units_in_tick = avctx->time_base.num;
        sps->vui.time_scale        = 2 * avctx->time_base.den;
        sps->vui.fixed_frame_rate_flag = 0;
    }

    if (opts->flags & FF_HW_H264_SEI_TIMING) {
        H264RawHRD *hrd = &sps->vui.nal_hrd_parameters;
        H264RawSEIBufferingPeriod *bp = &common->sei_buffering_period;

        sps->vui.nal_hrd_parameters_present_flag = 1;

        hrd->cpb_cnt_minus1 = 0;

        // Try to scale these to a sensible range so that the
        // golomb encode of the value is not overlong.
        hrd->bit_rate_scale =
            av_clip_uintp2(av_log2(opts->bit_rate) - 15 - 6, 4);
        hrd->bit_rate_value_minus1[0] =
            (opts->bit_rate >> hrd->bit_rate_scale + 6) - 1;

        hrd->cpb_size_scale =
            av_clip_uintp2(av_log2(opts->hrd_buffer_size) - 15 - 4, 4);
        hrd->cpb_size_value_minus1[0] =
            (opts->hrd_buffer_size >> hrd->cpb_size_scale + 4) - 1;

        // CBR mode as defined for the HRD cannot be achieved without filler
        // data, so this flag cannot be set even with VAAPI CBR modes.
        hrd->cbr_flag[0] = 0;

        hrd->initial_cpb_removal_delay_length_minus1 = 23;
        hrd->cpb_removal_delay_length_minus1         = 23;
        hrd->dpb_output_delay_length_minus1          = 7;
        hrd->time_offset_length                      = 0;

        bp->seq_parameter_set_id = sps->seq_parameter_set_id;

        // This calculation can easily overflow 32 bits.
        bp->nal.initial_cpb_removal_delay[0] = 90000 *
            (uint64_t)opts->initial_buffer_fullness /
            opts->hrd_buffer_size;
        bp->nal.initial_cpb_removal_delay_offset[0] = 0;
    } else {
        sps->vui.nal_hrd_parameters_present_flag = 0;
        sps->vui.low_delay_hrd_flag = 1 - sps->vui.fixed_frame_rate_flag;
    }

    sps->vui.bitstream_restriction_flag    = 1;
    sps->vui.motion_vectors_over_pic_boundaries_flag = 1;
    sps->vui.log2_max_mv_length_horizontal = 15;
    sps->vui.log2_max_mv_length_vertical   = 15;
    sps->vui.max_num_reorder_frames        = base_ctx->max_b_depth;
    sps->vui.max_dec_frame_buffering       = base_ctx->max_b_depth + 1;

    pps->nal_unit_header.nal_ref_idc = 3;
    pps->nal_unit_header.nal_unit_type = H264_NAL_PPS;

    pps->pic_parameter_set_id = 0;
    pps->seq_parameter_set_id = 0;

    pps->entropy_coding_mode_flag =
        !(sps->profile_idc == AV_PROFILE_H264_BASELINE ||
          sps->profile_idc == AV_PROFILE_H264_EXTENDED ||
          sps->profile_idc == AV_PROFILE_H264_CAVLC_444);
    if (!opts->cabac && pps->entropy_coding_mode_flag)
        pps->entropy_coding_mode_flag = 0;

    pps->num_ref_idx_l0_default_active_minus1 = 0;
    pps->num_ref_idx_l1_default_active_minus1 = 0;

    pps->pic_init_qp_minus26 = opts->fixed_qp_idr - 26;

    if (sps->profile_idc == AV_PROFILE_H264_BASELINE ||
        sps->profile_idc == AV_PROFILE_H264_EXTENDED ||
        sps->profile_idc == AV_PROFILE_H264_MAIN) {
        pps->more_rbsp_data = 0;
    } else {
        pps->more_rbsp_data = 1;

        pps->transform_8x8_mode_flag = 1;
    }

    return 0;
}
