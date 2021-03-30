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

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_h265.h"
#include "hevc.h"
#include "h265_profile_level.h"

enum {
    LEVEL_UNSET = -2,
    LEVEL_AUTO  = -1,
};

typedef struct H265MetadataContext {
    CBSBSFContext common;

    H265RawAUD aud_nal;

    int aud;

    AVRational sample_aspect_ratio;

    int video_format;
    int video_full_range_flag;
    int colour_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int chroma_sample_loc_type;

    AVRational tick_rate;
    int poc_proportional_to_timing_flag;
    int num_ticks_poc_diff_one;

    int crop_left;
    int crop_right;
    int crop_top;
    int crop_bottom;

    int level;
    int level_guess;
    int level_warned;
} H265MetadataContext;


static void h265_metadata_guess_level(AVBSFContext *bsf,
                                      const CodedBitstreamFragment *au)
{
    H265MetadataContext *ctx = bsf->priv_data;
    const H265LevelDescriptor *desc;
    const H265RawProfileTierLevel *ptl = NULL;
    const H265RawHRDParameters    *hrd = NULL;
    int64_t bit_rate = 0;
    int width = 0, height = 0;
    int tile_cols = 0, tile_rows = 0;
    int max_dec_pic_buffering = 0;
    int i;

    for (i = 0; i < au->nb_units; i++) {
        const CodedBitstreamUnit *unit = &au->units[i];

        if (unit->type == HEVC_NAL_VPS) {
            const H265RawVPS *vps = unit->content;

            ptl = &vps->profile_tier_level;
            max_dec_pic_buffering = vps->vps_max_dec_pic_buffering_minus1[0] + 1;

            if (vps->vps_num_hrd_parameters > 0)
                hrd = &vps->hrd_parameters[0];

        } else if (unit->type == HEVC_NAL_SPS) {
            const H265RawSPS *sps = unit->content;

            ptl = &sps->profile_tier_level;
            max_dec_pic_buffering = sps->sps_max_dec_pic_buffering_minus1[0] + 1;

            width  = sps->pic_width_in_luma_samples;
            height = sps->pic_height_in_luma_samples;

            if (sps->vui.vui_hrd_parameters_present_flag)
                hrd = &sps->vui.hrd_parameters;

        } else if (unit->type == HEVC_NAL_PPS) {
            const H265RawPPS *pps = unit->content;

            if (pps->tiles_enabled_flag) {
                tile_cols = pps->num_tile_columns_minus1 + 1;
                tile_rows = pps->num_tile_rows_minus1 + 1;
            }
        }
    }

    if (hrd) {
        if (hrd->nal_hrd_parameters_present_flag) {
            bit_rate = (hrd->nal_sub_layer_hrd_parameters[0].bit_rate_value_minus1[0] + 1) *
                       (INT64_C(1) << hrd->bit_rate_scale + 6);
        } else if (hrd->vcl_hrd_parameters_present_flag) {
            bit_rate = (hrd->vcl_sub_layer_hrd_parameters[0].bit_rate_value_minus1[0] + 1) *
                       (INT64_C(1) << hrd->bit_rate_scale + 6);
            // Adjust for VCL vs. NAL limits.
            bit_rate = bit_rate * 11 / 10;
        }
    }

    desc = ff_h265_guess_level(ptl, bit_rate, width, height,
                               0, tile_rows, tile_cols,
                               max_dec_pic_buffering);
    if (desc) {
        av_log(bsf, AV_LOG_DEBUG, "Stream appears to conform to "
               "level %s.\n", desc->name);
        ctx->level_guess = desc->level_idc;
    }
}

static void h265_metadata_update_level(AVBSFContext *bsf,
                                       uint8_t *level_idc)
{
    H265MetadataContext *ctx = bsf->priv_data;

    if (ctx->level != LEVEL_UNSET) {
        if (ctx->level == LEVEL_AUTO) {
            if (ctx->level_guess) {
                *level_idc = ctx->level_guess;
            } else {
                if (!ctx->level_warned) {
                    av_log(bsf, AV_LOG_WARNING, "Unable to determine level "
                           "of stream: using level 8.5.\n");
                    ctx->level_warned = 1;
                }
                *level_idc = 255;
            }
        } else {
            *level_idc = ctx->level;
        }
    }
}

static int h265_metadata_update_vps(AVBSFContext *bsf,
                                    H265RawVPS *vps)
{
    H265MetadataContext *ctx = bsf->priv_data;

    if (ctx->tick_rate.num && ctx->tick_rate.den) {
        int num, den;

        av_reduce(&num, &den, ctx->tick_rate.num, ctx->tick_rate.den,
                  UINT32_MAX > INT_MAX ? UINT32_MAX : INT_MAX);

        vps->vps_time_scale        = num;
        vps->vps_num_units_in_tick = den;

        vps->vps_timing_info_present_flag = 1;

        if (ctx->num_ticks_poc_diff_one > 0) {
            vps->vps_num_ticks_poc_diff_one_minus1 =
                ctx->num_ticks_poc_diff_one - 1;
            vps->vps_poc_proportional_to_timing_flag = 1;
        } else if (ctx->num_ticks_poc_diff_one == 0) {
            vps->vps_poc_proportional_to_timing_flag = 0;
        }
    }

    h265_metadata_update_level(bsf, &vps->profile_tier_level.general_level_idc);

    return 0;
}

static int h265_metadata_update_sps(AVBSFContext *bsf,
                                    H265RawSPS *sps)
{
    H265MetadataContext *ctx = bsf->priv_data;
    int need_vui = 0;
    int crop_unit_x, crop_unit_y;

    if (ctx->sample_aspect_ratio.num && ctx->sample_aspect_ratio.den) {
        // Table E-1.
        static const AVRational sar_idc[] = {
            {   0,  0 }, // Unspecified (never written here).
            {   1,  1 }, {  12, 11 }, {  10, 11 }, {  16, 11 },
            {  40, 33 }, {  24, 11 }, {  20, 11 }, {  32, 11 },
            {  80, 33 }, {  18, 11 }, {  15, 11 }, {  64, 33 },
            { 160, 99 }, {   4,  3 }, {   3,  2 }, {   2,  1 },
        };
        int num, den, i;

        av_reduce(&num, &den, ctx->sample_aspect_ratio.num,
                  ctx->sample_aspect_ratio.den, 65535);

        for (i = 1; i < FF_ARRAY_ELEMS(sar_idc); i++) {
            if (num == sar_idc[i].num &&
                den == sar_idc[i].den)
                break;
        }
        if (i == FF_ARRAY_ELEMS(sar_idc)) {
            sps->vui.aspect_ratio_idc = 255;
            sps->vui.sar_width  = num;
            sps->vui.sar_height = den;
        } else {
            sps->vui.aspect_ratio_idc = i;
        }
        sps->vui.aspect_ratio_info_present_flag = 1;
        need_vui = 1;
    }

#define SET_OR_INFER(field, value, present_flag, infer) do { \
        if (value >= 0) { \
            field = value; \
            need_vui = 1; \
        } else if (!present_flag) \
            field = infer; \
    } while (0)

    if (ctx->video_format             >= 0 ||
        ctx->video_full_range_flag    >= 0 ||
        ctx->colour_primaries         >= 0 ||
        ctx->transfer_characteristics >= 0 ||
        ctx->matrix_coefficients      >= 0) {

        SET_OR_INFER(sps->vui.video_format, ctx->video_format,
                     sps->vui.video_signal_type_present_flag, 5);

        SET_OR_INFER(sps->vui.video_full_range_flag,
                     ctx->video_full_range_flag,
                     sps->vui.video_signal_type_present_flag, 0);

        if (ctx->colour_primaries         >= 0 ||
            ctx->transfer_characteristics >= 0 ||
            ctx->matrix_coefficients      >= 0) {

            SET_OR_INFER(sps->vui.colour_primaries,
                         ctx->colour_primaries,
                         sps->vui.colour_description_present_flag, 2);

            SET_OR_INFER(sps->vui.transfer_characteristics,
                         ctx->transfer_characteristics,
                         sps->vui.colour_description_present_flag, 2);

            SET_OR_INFER(sps->vui.matrix_coefficients,
                         ctx->matrix_coefficients,
                         sps->vui.colour_description_present_flag, 2);

            sps->vui.colour_description_present_flag = 1;
        }
        sps->vui.video_signal_type_present_flag = 1;
        need_vui = 1;
    }

    if (ctx->chroma_sample_loc_type >= 0) {
        sps->vui.chroma_sample_loc_type_top_field =
            ctx->chroma_sample_loc_type;
        sps->vui.chroma_sample_loc_type_bottom_field =
            ctx->chroma_sample_loc_type;
        sps->vui.chroma_loc_info_present_flag = 1;
        need_vui = 1;
    }

    if (ctx->tick_rate.num && ctx->tick_rate.den) {
        int num, den;

        av_reduce(&num, &den, ctx->tick_rate.num, ctx->tick_rate.den,
                  UINT32_MAX > INT_MAX ? UINT32_MAX : INT_MAX);

        sps->vui.vui_time_scale        = num;
        sps->vui.vui_num_units_in_tick = den;

        sps->vui.vui_timing_info_present_flag = 1;
        need_vui = 1;

        if (ctx->num_ticks_poc_diff_one > 0) {
            sps->vui.vui_num_ticks_poc_diff_one_minus1 =
                ctx->num_ticks_poc_diff_one - 1;
            sps->vui.vui_poc_proportional_to_timing_flag = 1;
        } else if (ctx->num_ticks_poc_diff_one == 0) {
            sps->vui.vui_poc_proportional_to_timing_flag = 0;
        }
    }

    if (sps->separate_colour_plane_flag || sps->chroma_format_idc == 0) {
        crop_unit_x = 1;
        crop_unit_y = 1;
    } else {
        crop_unit_x = 1 + (sps->chroma_format_idc < 3);
        crop_unit_y = 1 + (sps->chroma_format_idc < 2);
    }
#define CROP(border, unit) do { \
        if (ctx->crop_ ## border >= 0) { \
            if (ctx->crop_ ## border % unit != 0) { \
                av_log(bsf, AV_LOG_ERROR, "Invalid value for crop_%s: " \
                       "must be a multiple of %d.\n", #border, unit); \
                return AVERROR(EINVAL); \
            } \
            sps->conf_win_ ## border ## _offset = \
                ctx->crop_ ## border / unit; \
            sps->conformance_window_flag = 1; \
        } \
    } while (0)
    CROP(left,   crop_unit_x);
    CROP(right,  crop_unit_x);
    CROP(top,    crop_unit_y);
    CROP(bottom, crop_unit_y);
#undef CROP

    if (need_vui)
        sps->vui_parameters_present_flag = 1;

    h265_metadata_update_level(bsf, &sps->profile_tier_level.general_level_idc);

    return 0;
}

static int h265_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                         CodedBitstreamFragment *au)
{
    H265MetadataContext *ctx = bsf->priv_data;
    int err, i;

    // If an AUD is present, it must be the first NAL unit.
    if (au->nb_units && au->units[0].type == HEVC_NAL_AUD) {
        if (ctx->aud == BSF_ELEMENT_REMOVE)
            ff_cbs_delete_unit(au, 0);
    } else {
        if (pkt && ctx->aud == BSF_ELEMENT_INSERT) {
            H265RawAUD *aud = &ctx->aud_nal;
            int pic_type = 0, temporal_id = 8, layer_id = 0;

            for (i = 0; i < au->nb_units; i++) {
                const H265RawNALUnitHeader *nal = au->units[i].content;
                if (!nal)
                    continue;
                if (nal->nuh_temporal_id_plus1 < temporal_id + 1)
                    temporal_id = nal->nuh_temporal_id_plus1 - 1;

                if (au->units[i].type <= HEVC_NAL_RSV_VCL31) {
                    const H265RawSlice *slice = au->units[i].content;
                    layer_id = nal->nuh_layer_id;
                    if (slice->header.slice_type == HEVC_SLICE_B &&
                        pic_type < 2)
                        pic_type = 2;
                    if (slice->header.slice_type == HEVC_SLICE_P &&
                        pic_type < 1)
                        pic_type = 1;
                }
            }

            aud->nal_unit_header = (H265RawNALUnitHeader) {
                .nal_unit_type         = HEVC_NAL_AUD,
                .nuh_layer_id          = layer_id,
                .nuh_temporal_id_plus1 = temporal_id + 1,
            };
            aud->pic_type = pic_type;

            err = ff_cbs_insert_unit_content(au, 0, HEVC_NAL_AUD, aud, NULL);
            if (err < 0) {
                av_log(bsf, AV_LOG_ERROR, "Failed to insert AUD.\n");
                return err;
            }
        }
    }

    if (ctx->level == LEVEL_AUTO && !ctx->level_guess)
        h265_metadata_guess_level(bsf, au);

    for (i = 0; i < au->nb_units; i++) {
        if (au->units[i].type == HEVC_NAL_VPS) {
            err = h265_metadata_update_vps(bsf, au->units[i].content);
            if (err < 0)
                return err;
        }
        if (au->units[i].type == HEVC_NAL_SPS) {
            err = h265_metadata_update_sps(bsf, au->units[i].content);
            if (err < 0)
                return err;
        }
    }

    return 0;
}

static const CBSBSFType h265_metadata_type = {
    .codec_id        = AV_CODEC_ID_HEVC,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &h265_metadata_update_fragment,
};

static int h265_metadata_init(AVBSFContext *bsf)
{
    return ff_cbs_bsf_generic_init(bsf, &h265_metadata_type);
}

#define OFFSET(x) offsetof(H265MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption h265_metadata_options[] = {
    BSF_ELEMENT_OPTIONS_PIR("aud", "Access Unit Delimiter NAL units",
                            aud, FLAGS),

    { "sample_aspect_ratio", "Set sample aspect ratio (table E-1)",
        OFFSET(sample_aspect_ratio), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, 65535, FLAGS },

    { "video_format", "Set video format (table E-2)",
        OFFSET(video_format), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 7, FLAGS },
    { "video_full_range_flag", "Set video full range flag",
        OFFSET(video_full_range_flag), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS },
    { "colour_primaries", "Set colour primaries (table E-3)",
        OFFSET(colour_primaries), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "transfer_characteristics", "Set transfer characteristics (table E-4)",
        OFFSET(transfer_characteristics), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },
    { "matrix_coefficients", "Set matrix coefficients (table E-5)",
        OFFSET(matrix_coefficients), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 255, FLAGS },

    { "chroma_sample_loc_type", "Set chroma sample location type (figure E-1)",
        OFFSET(chroma_sample_loc_type), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 6, FLAGS },

    { "tick_rate",
        "Set VPS and VUI tick rate (num_units_in_tick / time_scale)",
        OFFSET(tick_rate), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, UINT_MAX, FLAGS },
    { "num_ticks_poc_diff_one",
        "Set VPS and VUI number of ticks per POC increment",
        OFFSET(num_ticks_poc_diff_one), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, INT_MAX, FLAGS },

    { "crop_left", "Set left border crop offset",
        OFFSET(crop_left), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, HEVC_MAX_WIDTH, FLAGS },
    { "crop_right", "Set right border crop offset",
        OFFSET(crop_right), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, HEVC_MAX_WIDTH, FLAGS },
    { "crop_top", "Set top border crop offset",
        OFFSET(crop_top), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, HEVC_MAX_HEIGHT, FLAGS },
    { "crop_bottom", "Set bottom border crop offset",
        OFFSET(crop_bottom), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, HEVC_MAX_HEIGHT, FLAGS },

    { "level", "Set level (tables A.6 and A.7)",
        OFFSET(level), AV_OPT_TYPE_INT,
        { .i64 = LEVEL_UNSET }, LEVEL_UNSET, 0xff, FLAGS, "level" },
    { "auto", "Attempt to guess level from stream properties",
        0, AV_OPT_TYPE_CONST,
        { .i64 = LEVEL_AUTO }, .flags = FLAGS, .unit = "level" },
#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
        { .i64 = value },      .flags = FLAGS, .unit = "level"
    { LEVEL("1",    30) },
    { LEVEL("2",    60) },
    { LEVEL("2.1",  63) },
    { LEVEL("3",    90) },
    { LEVEL("3.1",  93) },
    { LEVEL("4",   120) },
    { LEVEL("4.1", 123) },
    { LEVEL("5",   150) },
    { LEVEL("5.1", 153) },
    { LEVEL("5.2", 156) },
    { LEVEL("6",   180) },
    { LEVEL("6.1", 183) },
    { LEVEL("6.2", 186) },
    { LEVEL("8.5", 255) },
#undef LEVEL

    { NULL }
};

static const AVClass h265_metadata_class = {
    .class_name = "h265_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = h265_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID h265_metadata_codec_ids[] = {
    AV_CODEC_ID_HEVC, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_hevc_metadata_bsf = {
    .name           = "hevc_metadata",
    .priv_data_size = sizeof(H265MetadataContext),
    .priv_class     = &h265_metadata_class,
    .init           = &h265_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
    .codec_ids      = h265_metadata_codec_ids,
};
