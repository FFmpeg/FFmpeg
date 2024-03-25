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

#include "libavutil/avstring.h"
#include "libavutil/display.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_h264.h"
#include "cbs_sei.h"
#include "h264.h"
#include "h264_levels.h"
#include "h2645data.h"
#include "sei.h"

enum {
    FLIP_HORIZONTAL = 1,
    FLIP_VERTICAL   = 2,
};

enum {
    LEVEL_UNSET = -2,
    LEVEL_AUTO  = -1,
};

typedef struct H264MetadataContext {
    CBSBSFContext common;

    int done_first_au;

    int aud;
    H264RawAUD aud_nal;

    AVRational sample_aspect_ratio;

    int overscan_appropriate_flag;

    int video_format;
    int video_full_range_flag;
    int colour_primaries;
    int transfer_characteristics;
    int matrix_coefficients;

    int chroma_sample_loc_type;

    AVRational tick_rate;
    int fixed_frame_rate_flag;
    int zero_new_constraint_set_flags;

    int crop_left;
    int crop_right;
    int crop_top;
    int crop_bottom;

    const char *sei_user_data;
    SEIRawUserDataUnregistered sei_user_data_payload;

    int delete_filler;

    int display_orientation;
    double rotate;
    int flip;
    H264RawSEIDisplayOrientation display_orientation_payload;

    int level;
} H264MetadataContext;


static int h264_metadata_insert_aud(AVBSFContext *bsf,
                                    CodedBitstreamFragment *au)
{
    H264MetadataContext *ctx = bsf->priv_data;
    int primary_pic_type_mask = 0xff;
    int err, i, j;

    static const int primary_pic_type_table[] = {
        0x084, // 2, 7
        0x0a5, // 0, 2, 5, 7
        0x0e7, // 0, 1, 2, 5, 6, 7
        0x210, // 4, 9
        0x318, // 3, 4, 8, 9
        0x294, // 2, 4, 7, 9
        0x3bd, // 0, 2, 3, 4, 5, 7, 8, 9
        0x3ff, // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
    };

    for (i = 0; i < au->nb_units; i++) {
        if (au->units[i].type == H264_NAL_SLICE ||
            au->units[i].type == H264_NAL_IDR_SLICE) {
            H264RawSlice *slice = au->units[i].content;
            for (j = 0; j < FF_ARRAY_ELEMS(primary_pic_type_table); j++) {
                if (!(primary_pic_type_table[j] &
                      (1 << slice->header.slice_type)))
                    primary_pic_type_mask &= ~(1 << j);
            }
        }
    }
    for (j = 0; j < FF_ARRAY_ELEMS(primary_pic_type_table); j++)
        if (primary_pic_type_mask & (1 << j))
            break;
    if (j >= FF_ARRAY_ELEMS(primary_pic_type_table)) {
        av_log(bsf, AV_LOG_ERROR, "No usable primary_pic_type: "
               "invalid slice types?\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->aud_nal = (H264RawAUD) {
        .nal_unit_header.nal_unit_type = H264_NAL_AUD,
        .primary_pic_type = j,
    };

    err = ff_cbs_insert_unit_content(au, 0, H264_NAL_AUD,
                                     &ctx->aud_nal, NULL);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to insert AUD.\n");
        return err;
    }

    return 0;
}

static int h264_metadata_update_sps(AVBSFContext *bsf,
                                    H264RawSPS *sps)
{
    H264MetadataContext *ctx = bsf->priv_data;
    int need_vui = 0;
    int crop_unit_x, crop_unit_y;

    if (ctx->sample_aspect_ratio.num && ctx->sample_aspect_ratio.den) {
        int num, den, i;

        av_reduce(&num, &den, ctx->sample_aspect_ratio.num,
                  ctx->sample_aspect_ratio.den, 65535);

        for (i = 1; i < FF_ARRAY_ELEMS(ff_h2645_pixel_aspect); i++) {
            if (num == ff_h2645_pixel_aspect[i].num &&
                den == ff_h2645_pixel_aspect[i].den)
                break;
        }
        if (i == FF_ARRAY_ELEMS(ff_h2645_pixel_aspect)) {
            sps->vui.aspect_ratio_idc = 255;
            sps->vui.sar_width  = num;
            sps->vui.sar_height = den;
        } else {
            sps->vui.aspect_ratio_idc = i;
        }
        sps->vui.aspect_ratio_info_present_flag = 1;
        need_vui = 1;
    }

#define SET_VUI_FIELD(field) do { \
        if (ctx->field >= 0) { \
            sps->vui.field = ctx->field; \
            need_vui = 1; \
        } \
    } while (0)

    if (ctx->overscan_appropriate_flag >= 0) {
        SET_VUI_FIELD(overscan_appropriate_flag);
        sps->vui.overscan_info_present_flag = 1;
    }

    if (ctx->video_format             >= 0 ||
        ctx->video_full_range_flag    >= 0 ||
        ctx->colour_primaries         >= 0 ||
        ctx->transfer_characteristics >= 0 ||
        ctx->matrix_coefficients      >= 0) {

        SET_VUI_FIELD(video_format);

        SET_VUI_FIELD(video_full_range_flag);

        if (ctx->colour_primaries         >= 0 ||
            ctx->transfer_characteristics >= 0 ||
            ctx->matrix_coefficients      >= 0) {

            SET_VUI_FIELD(colour_primaries);
            SET_VUI_FIELD(transfer_characteristics);
            SET_VUI_FIELD(matrix_coefficients);

            sps->vui.colour_description_present_flag = 1;
        }
        sps->vui.video_signal_type_present_flag = 1;
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

        sps->vui.time_scale        = num;
        sps->vui.num_units_in_tick = den;

        sps->vui.timing_info_present_flag = 1;
        need_vui = 1;
    }
    SET_VUI_FIELD(fixed_frame_rate_flag);
    if (ctx->zero_new_constraint_set_flags) {
        sps->constraint_set4_flag = 0;
        sps->constraint_set5_flag = 0;
    }

    if (sps->separate_colour_plane_flag || sps->chroma_format_idc == 0) {
        crop_unit_x = 1;
        crop_unit_y = 2 - sps->frame_mbs_only_flag;
    } else {
        crop_unit_x = 1 + (sps->chroma_format_idc < 3);
        crop_unit_y = (1 + (sps->chroma_format_idc < 2)) *
                       (2 - sps->frame_mbs_only_flag);
    }
#define CROP(border, unit) do { \
        if (ctx->crop_ ## border >= 0) { \
            if (ctx->crop_ ## border % unit != 0) { \
                av_log(bsf, AV_LOG_ERROR, "Invalid value for crop_%s: " \
                       "must be a multiple of %d.\n", #border, unit); \
                return AVERROR(EINVAL); \
            } \
            sps->frame_crop_ ## border ## _offset = \
                  ctx->crop_ ## border / unit; \
            sps->frame_cropping_flag = 1; \
        } \
    } while (0)
    CROP(left,   crop_unit_x);
    CROP(right,  crop_unit_x);
    CROP(top,    crop_unit_y);
    CROP(bottom, crop_unit_y);
#undef CROP

    if (ctx->level != LEVEL_UNSET) {
        int level_idc;

        if (ctx->level == LEVEL_AUTO) {
            const H264LevelDescriptor *desc;
            int64_t bit_rate;
            int width, height, dpb_frames;
            int framerate;

            if (sps->vui.nal_hrd_parameters_present_flag) {
                bit_rate = (sps->vui.nal_hrd_parameters.bit_rate_value_minus1[0] + 1) *
                    (INT64_C(1) << (sps->vui.nal_hrd_parameters.bit_rate_scale + 6));
            } else if (sps->vui.vcl_hrd_parameters_present_flag) {
                bit_rate = (sps->vui.vcl_hrd_parameters.bit_rate_value_minus1[0] + 1) *
                    (INT64_C(1) << (sps->vui.vcl_hrd_parameters.bit_rate_scale + 6));
                // Adjust for VCL vs. NAL limits.
                bit_rate = bit_rate * 6 / 5;
            } else {
                bit_rate = 0;
            }

            // Don't use max_dec_frame_buffering if it is only inferred.
            dpb_frames = sps->vui.bitstream_restriction_flag ?
                sps->vui.max_dec_frame_buffering : H264_MAX_DPB_FRAMES;

            width  = 16 * (sps->pic_width_in_mbs_minus1 + 1);
            height = 16 * (sps->pic_height_in_map_units_minus1 + 1) *
                (2 - sps->frame_mbs_only_flag);

            if (sps->vui.timing_info_present_flag)
                framerate = sps->vui.time_scale / sps->vui.num_units_in_tick / 2;
            else
                framerate = 0;

            desc = ff_h264_guess_level(sps->profile_idc, bit_rate, framerate,
                                       width, height, dpb_frames);
            if (desc) {
                level_idc = desc->level_idc;
            } else {
                av_log(bsf, AV_LOG_WARNING, "Stream does not appear to "
                       "conform to any level: using level 6.2.\n");
                level_idc = 62;
            }
        } else {
            level_idc = ctx->level;
        }

        if (level_idc == 9) {
            if (sps->profile_idc == 66 ||
                sps->profile_idc == 77 ||
                sps->profile_idc == 88) {
                sps->level_idc = 11;
                sps->constraint_set3_flag = 1;
            } else {
                sps->level_idc = 9;
            }
        } else {
            sps->level_idc = level_idc;
        }
    }

    if (need_vui)
        sps->vui_parameters_present_flag = 1;

    return 0;
}

static int h264_metadata_handle_display_orientation(AVBSFContext *bsf,
                                                    AVPacket *pkt,
                                                    CodedBitstreamFragment *au,
                                                    int seek_point)
{
    H264MetadataContext *ctx = bsf->priv_data;
    SEIRawMessage *message;
    int err;

    message = NULL;
    while (ff_cbs_sei_find_message(ctx->common.output, au,
                                   SEI_TYPE_DISPLAY_ORIENTATION,
                                   &message) == 0) {
        H264RawSEIDisplayOrientation *disp = message->payload;
        double angle = disp->anticlockwise_rotation * 180.0 / 65536.0;
        int32_t *matrix;

        matrix = av_malloc(9 * sizeof(int32_t));
        if (!matrix)
            return AVERROR(ENOMEM);

        /* av_display_rotation_set() expects the angle in the clockwise
         * direction, hence the first minus.
         * The below code applies the flips after the rotation, yet
         * the H.2645 specs require flipping to be applied first.
         * Because of R O(phi) = O(-phi) R (where R is flipping around
         * an arbitatry axis and O(phi) is the proper rotation by phi)
         * we can create display matrices as desired by negating
         * the degree once for every flip applied. */
        angle = -angle * (1 - 2 * !!disp->hor_flip) * (1 - 2 * !!disp->ver_flip);

        av_display_rotation_set(matrix, angle);
        av_display_matrix_flip(matrix, disp->hor_flip, disp->ver_flip);

        // If there are multiple display orientation messages in an
        // access unit, then the last one added to the packet (i.e.
        // the first one in the access unit) will prevail.
        err = av_packet_add_side_data(pkt, AV_PKT_DATA_DISPLAYMATRIX,
                                      (uint8_t*)matrix,
                                      9 * sizeof(int32_t));
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to attach extracted "
                   "displaymatrix side data to packet.\n");
            av_free(matrix);
            return AVERROR(ENOMEM);
        }
    }

    if (ctx->display_orientation == BSF_ELEMENT_REMOVE ||
        ctx->display_orientation == BSF_ELEMENT_INSERT) {
        ff_cbs_sei_delete_message_type(ctx->common.output, au,
                                       SEI_TYPE_DISPLAY_ORIENTATION);
    }

    if (ctx->display_orientation == BSF_ELEMENT_INSERT) {
        H264RawSEIDisplayOrientation *disp =
            &ctx->display_orientation_payload;
        uint8_t *data;
        size_t size;
        int write = 0;

        data = av_packet_get_side_data(pkt, AV_PKT_DATA_DISPLAYMATRIX, &size);
        if (data && size >= 9 * sizeof(int32_t)) {
            int32_t matrix[9];
            double dmatrix[9];
            int hflip, vflip, i;
            double scale_x, scale_y, angle;

            memcpy(matrix, data, sizeof(matrix));

            for (i = 0; i < 9; i++)
                dmatrix[i] = matrix[i] / 65536.0;

            // Extract scale factors.
            scale_x = hypot(dmatrix[0], dmatrix[3]);
            scale_y = hypot(dmatrix[1], dmatrix[4]);

            // Select flips to make the main diagonal positive.
            hflip = dmatrix[0] < 0.0;
            vflip = dmatrix[4] < 0.0;
            if (hflip)
                scale_x = -scale_x;
            if (vflip)
                scale_y = -scale_y;

            // Rescale.
            for (i = 0; i < 9; i += 3) {
                dmatrix[i]     /= scale_x;
                dmatrix[i + 1] /= scale_y;
            }

            // Extract rotation.
            angle = atan2(dmatrix[3], dmatrix[0]);

            if (!(angle >= -M_PI && angle <= M_PI) ||
                matrix[2] != 0.0 || matrix[5] != 0.0 ||
                matrix[6] != 0.0 || matrix[7] != 0.0) {
                av_log(bsf, AV_LOG_WARNING, "Input display matrix is not "
                       "representable in H.264 parameters.\n");
            } else {
                disp->hor_flip = hflip;
                disp->ver_flip = vflip;
                disp->anticlockwise_rotation =
                    (uint16_t)rint((angle >= 0.0 ? angle
                                                 : angle + 2 * M_PI) *
                                   32768.0 / M_PI);
                write = 1;
            }
        }

        if (seek_point) {
            if (!isnan(ctx->rotate)) {
                disp->anticlockwise_rotation =
                    (uint16_t)rint((ctx->rotate >= 0.0 ? ctx->rotate
                                                       : ctx->rotate + 360.0) *
                                   65536.0 / 360.0);
                write = 1;
            }
            if (ctx->flip) {
                disp->hor_flip = !!(ctx->flip & FLIP_HORIZONTAL);
                disp->ver_flip = !!(ctx->flip & FLIP_VERTICAL);
                write = 1;
            }
        }

        if (write) {
            disp->display_orientation_repetition_period = 1;

            err = ff_cbs_sei_add_message(ctx->common.output, au, 1,
                                         SEI_TYPE_DISPLAY_ORIENTATION,
                                         disp, NULL);
            if (err < 0) {
                av_log(bsf, AV_LOG_ERROR, "Failed to add display orientation "
                       "SEI message to access unit.\n");
                return err;
            }
        }
    }

    return 0;
}

static int h264_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                         CodedBitstreamFragment *au)
{
    H264MetadataContext *ctx = bsf->priv_data;
    int err, i, has_sps, seek_point;

    if (ctx->aud == BSF_ELEMENT_REMOVE) {
        for (i = au->nb_units - 1; i >= 0; i--) {
            if (au->units[i].type == H264_NAL_AUD)
                ff_cbs_delete_unit(au, i);
        }
    } else if (ctx->aud == BSF_ELEMENT_INSERT) {
        if (pkt) {
            err = h264_metadata_insert_aud(bsf, au);
            if (err < 0)
                return err;
        }
    }

    has_sps = 0;
    for (i = 0; i < au->nb_units; i++) {
        if (au->units[i].type == H264_NAL_SPS) {
            err = h264_metadata_update_sps(bsf, au->units[i].content);
            if (err < 0)
                return err;
            has_sps = 1;
        }
    }

    if (pkt) {
        // The current packet should be treated as a seek point for metadata
        // insertion if any of:
        // - It is the first packet in the stream.
        // - It contains an SPS, indicating that a sequence might start here.
        // - It is marked as containing a key frame.
        seek_point = !ctx->done_first_au || has_sps ||
            (pkt->flags & AV_PKT_FLAG_KEY);
    } else {
        seek_point = 0;
    }

    if (ctx->sei_user_data && seek_point) {
        err = ff_cbs_sei_add_message(ctx->common.output, au, 1,
                                     SEI_TYPE_USER_DATA_UNREGISTERED,
                                     &ctx->sei_user_data_payload, NULL);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to add user data SEI "
                   "message to access unit.\n");
            return err;
        }
    }

    if (ctx->delete_filler) {
        for (i = au->nb_units - 1; i >= 0; i--) {
            if (au->units[i].type == H264_NAL_FILLER_DATA) {
                ff_cbs_delete_unit(au, i);
                continue;
            }
        }

        ff_cbs_sei_delete_message_type(ctx->common.output, au,
                                       SEI_TYPE_FILLER_PAYLOAD);
    }

    if (pkt && ctx->display_orientation != BSF_ELEMENT_PASS) {
        err = h264_metadata_handle_display_orientation(bsf, pkt, au,
                                                       seek_point);
        if (err < 0)
            return err;
    }

    if (pkt)
        ctx->done_first_au = 1;

    return 0;
}

static const CBSBSFType h264_metadata_type = {
    .codec_id        = AV_CODEC_ID_H264,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &h264_metadata_update_fragment,
};

static int h264_metadata_init(AVBSFContext *bsf)
{
    H264MetadataContext *ctx = bsf->priv_data;

    if (ctx->sei_user_data) {
        SEIRawUserDataUnregistered *udu = &ctx->sei_user_data_payload;
        int i, j;

        // Parse UUID.  It must be a hex string of length 32, possibly
        // containing '-'s between hex digits (which we ignore).
        for (i = j = 0; j < 32 && i < 64 && ctx->sei_user_data[i]; i++) {
            int c, v;
            c = ctx->sei_user_data[i];
            if (c == '-') {
                continue;
            } else if (av_isxdigit(c)) {
                c = av_tolower(c);
                v = (c <= '9' ? c - '0' : c - 'a' + 10);
            } else {
                break;
            }
            if (j & 1)
                udu->uuid_iso_iec_11578[j / 2] |= v;
            else
                udu->uuid_iso_iec_11578[j / 2] = v << 4;
            ++j;
        }
        if (j == 32 && ctx->sei_user_data[i] == '+') {
            udu->data = (uint8_t*)ctx->sei_user_data + i + 1;
            udu->data_length = strlen(udu->data) + 1;
        } else {
            av_log(bsf, AV_LOG_ERROR, "Invalid user data: "
                   "must be \"UUID+string\".\n");
            return AVERROR(EINVAL);
        }
    }

    return ff_cbs_bsf_generic_init(bsf, &h264_metadata_type);
}

#define OFFSET(x) offsetof(H264MetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption h264_metadata_options[] = {
    BSF_ELEMENT_OPTIONS_PIR("aud", "Access Unit Delimiter NAL units",
                            aud, FLAGS),

    { "sample_aspect_ratio", "Set sample aspect ratio (table E-1)",
        OFFSET(sample_aspect_ratio), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, 65535, FLAGS },

    { "overscan_appropriate_flag", "Set VUI overscan appropriate flag",
        OFFSET(overscan_appropriate_flag), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS },

    { "video_format", "Set video format (table E-2)",
        OFFSET(video_format), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 7, FLAGS},
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
        { .i64 = -1 }, -1, 5, FLAGS },

    { "tick_rate", "Set VUI tick rate (time_scale / num_units_in_tick)",
        OFFSET(tick_rate), AV_OPT_TYPE_RATIONAL,
        { .dbl = 0.0 }, 0, UINT_MAX, FLAGS },
    { "fixed_frame_rate_flag", "Set VUI fixed frame rate flag",
        OFFSET(fixed_frame_rate_flag), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, 1, FLAGS },
    { "zero_new_constraint_set_flags", "Set constraint_set4_flag / constraint_set5_flag to zero",
        OFFSET(zero_new_constraint_set_flags), AV_OPT_TYPE_BOOL,
        { .i64 = 0 }, 0, 1, FLAGS },

    { "crop_left", "Set left border crop offset",
        OFFSET(crop_left), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, H264_MAX_WIDTH, FLAGS },
    { "crop_right", "Set right border crop offset",
        OFFSET(crop_right), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, H264_MAX_WIDTH, FLAGS },
    { "crop_top", "Set top border crop offset",
        OFFSET(crop_top), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, H264_MAX_HEIGHT, FLAGS },
    { "crop_bottom", "Set bottom border crop offset",
        OFFSET(crop_bottom), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, H264_MAX_HEIGHT, FLAGS },

    { "sei_user_data", "Insert SEI user data (UUID+string)",
        OFFSET(sei_user_data), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS },

    { "delete_filler", "Delete all filler (both NAL and SEI)",
        OFFSET(delete_filler), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS},

    BSF_ELEMENT_OPTIONS_PIRE("display_orientation",
                             "Display orientation SEI",
                             display_orientation, FLAGS),

    { "rotate", "Set rotation in display orientation SEI (anticlockwise angle in degrees)",
        OFFSET(rotate), AV_OPT_TYPE_DOUBLE,
        { .dbl = NAN }, -360.0, +360.0, FLAGS },
    { "flip", "Set flip in display orientation SEI",
        OFFSET(flip), AV_OPT_TYPE_FLAGS,
        { .i64 = 0 }, 0, FLIP_HORIZONTAL | FLIP_VERTICAL, FLAGS, .unit = "flip" },
    { "horizontal", "Set hor_flip",
        0, AV_OPT_TYPE_CONST,
        { .i64 = FLIP_HORIZONTAL }, .flags = FLAGS, .unit = "flip" },
    { "vertical",   "Set ver_flip",
        0, AV_OPT_TYPE_CONST,
        { .i64 = FLIP_VERTICAL },   .flags = FLAGS, .unit = "flip" },

    { "level", "Set level (table A-1)",
        OFFSET(level), AV_OPT_TYPE_INT,
        { .i64 = LEVEL_UNSET }, LEVEL_UNSET, 0xff, FLAGS, .unit = "level" },
    { "auto", "Attempt to guess level from stream properties",
        0, AV_OPT_TYPE_CONST,
        { .i64 = LEVEL_AUTO }, .flags = FLAGS, .unit = "level" },
#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
        { .i64 = value },      .flags = FLAGS, .unit = "level"
    { LEVEL("1",   10) },
    { LEVEL("1b",   9) },
    { LEVEL("1.1", 11) },
    { LEVEL("1.2", 12) },
    { LEVEL("1.3", 13) },
    { LEVEL("2",   20) },
    { LEVEL("2.1", 21) },
    { LEVEL("2.2", 22) },
    { LEVEL("3",   30) },
    { LEVEL("3.1", 31) },
    { LEVEL("3.2", 32) },
    { LEVEL("4",   40) },
    { LEVEL("4.1", 41) },
    { LEVEL("4.2", 42) },
    { LEVEL("5",   50) },
    { LEVEL("5.1", 51) },
    { LEVEL("5.2", 52) },
    { LEVEL("6",   60) },
    { LEVEL("6.1", 61) },
    { LEVEL("6.2", 62) },
#undef LEVEL

    { NULL }
};

static const AVClass h264_metadata_class = {
    .class_name = "h264_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = h264_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID h264_metadata_codec_ids[] = {
    AV_CODEC_ID_H264, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_h264_metadata_bsf = {
    .p.name         = "h264_metadata",
    .p.codec_ids    = h264_metadata_codec_ids,
    .p.priv_class   = &h264_metadata_class,
    .priv_data_size = sizeof(H264MetadataContext),
    .init           = &h264_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
