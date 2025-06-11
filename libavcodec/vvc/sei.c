/*
 * VVC Supplementary Enhancement Information messages
 *
 * copyright (c) 2024 Wu Jianhua <toqsxw@outlook.com>
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

#include "sei.h"
#include "dec.h"
#include "libavutil/refstruct.h"

static int decode_film_grain_characteristics(H2645SEIFilmGrainCharacteristics *h, const SEIRawFilmGrainCharacteristics *s, const VVCFrameContext *fc)
{
    const VVCSPS *sps = fc->ps.sps;

    h->present = !s->fg_characteristics_cancel_flag;
    if (h->present) {
        h->model_id                                 = s->fg_model_id;
        h->separate_colour_description_present_flag = s->fg_separate_colour_description_present_flag;
        if (h->separate_colour_description_present_flag) {
            h->bit_depth_luma           =  s->fg_bit_depth_luma_minus8 + 8;
            h->bit_depth_chroma         =  s->fg_bit_depth_chroma_minus8 + 8;
            h->full_range               =  s->fg_full_range_flag;
            h->color_primaries          =  s->fg_colour_primaries;
            h->transfer_characteristics =  s->fg_transfer_characteristics;
            h->matrix_coeffs            =  s->fg_matrix_coeffs;
        }  else {
            if (!sps) {
                av_log(fc->log_ctx, AV_LOG_ERROR,
                    "No active SPS for film_grain_characteristics.\n");
                return AVERROR_INVALIDDATA;
            }
            h->bit_depth_luma           = sps->bit_depth;
            h->bit_depth_chroma         = sps->bit_depth;
            h->full_range               = sps->r->vui.vui_full_range_flag;
            h->color_primaries          = sps->r->vui.vui_colour_primaries;
            h->transfer_characteristics = sps->r->vui.vui_transfer_characteristics;
            h->matrix_coeffs            = sps->r->vui.vui_matrix_coeffs ;
        }

        h->blending_mode_id  =  s->fg_blending_mode_id;
        h->log2_scale_factor =  s->fg_log2_scale_factor;

        for (int c = 0; c < 3; c++) {
            h->comp_model_present_flag[c] = s->fg_comp_model_present_flag[c];
            if (h->comp_model_present_flag[c]) {
                h->num_intensity_intervals[c] = s->fg_num_intensity_intervals_minus1[c] + 1;
                h->num_model_values[c]        = s->fg_num_model_values_minus1[c] + 1;

                if (h->num_model_values[c] > 6)
                    return AVERROR_INVALIDDATA;

                for (int i = 0; i < h->num_intensity_intervals[c]; i++) {
                    h->intensity_interval_lower_bound[c][i] = s->fg_intensity_interval_lower_bound[c][i];
                    h->intensity_interval_upper_bound[c][i] = s->fg_intensity_interval_upper_bound[c][i];
                    for (int j = 0; j < h->num_model_values[c]; j++)
                        h->comp_model_value[c][i][j] = s->fg_comp_model_value[c][i][j];
                }
            }
        }

        h->persistence_flag = s->fg_characteristics_persistence_flag;
    }

    return 0;
}

static int decode_decoded_picture_hash(H274SEIPictureHash *h, const SEIRawDecodedPictureHash *s)
{
    h->present   = 1;
    h->hash_type = s->dph_sei_hash_type;
    if (h->hash_type == 0)
        memcpy(h->md5, s->dph_sei_picture_md5, sizeof(h->md5));
    else if (h->hash_type == 1)
        memcpy(h->crc, s->dph_sei_picture_crc, sizeof(h->crc));
    else if (h->hash_type == 2)
        memcpy(h->checksum, s->dph_sei_picture_checksum, sizeof(h->checksum));

    return 0;
}

static int decode_display_orientation(H2645SEIDisplayOrientation *h, const SEIRawDisplayOrientation *s)
{
    int degrees[] = { 0, 0x8000, 0x4000, 0xC000 };

    h->present = !s->display_orientation_cancel_flag;
    if (h->present) {
        if (s->display_orientation_transform_type > 7)
            return AVERROR_INVALIDDATA;

        h->vflip = 0;
        if (s->display_orientation_transform_type == 1 ||
            s->display_orientation_transform_type == 3 ||
            s->display_orientation_transform_type == 4 ||
            s->display_orientation_transform_type == 6) {
            h->hflip = 1;
        } else {
            h->hflip = 0;
        }
        h->anticlockwise_rotation = degrees[s->display_orientation_transform_type >> 1];
    }

    return 0;
}

static int decode_content_light_level_info(H2645SEIContentLight *h, const SEIRawContentLightLevelInfo *s)
{
    h->present                     = 1;
    h->max_content_light_level     = s->max_content_light_level;
    h->max_pic_average_light_level = s->max_pic_average_light_level;

    return 0;
}

static int decode_frame_field_info(H274SEIFrameFieldInfo *h, const SEIRawFrameFieldInformation *s)
{
    if (s->ffi_source_scan_type > 3)
        return AVERROR_INVALIDDATA;

    h->present = 1;
    if (s->ffi_field_pic_flag) {
        if (s->ffi_bottom_field_flag)
            h->picture_struct = AV_PICTURE_STRUCTURE_BOTTOM_FIELD;
        else
            h->picture_struct = AV_PICTURE_STRUCTURE_TOP_FIELD;
    } else {
        h->display_elemental_periods = s->ffi_display_elemental_periods_minus1 + 1;
    }

    h->source_scan_type = s->ffi_source_scan_type;
    h->duplicate_flag   = s->ffi_duplicate_flag;

    return 0;
}

static int decode_ambient_viewing_environment(H2645SEIAmbientViewingEnvironment *h, const SEIRawAmbientViewingEnvironment *s)
{
    h->present             = 1;
    h->ambient_illuminance = s->ambient_illuminance;
    h->ambient_light_x     = s->ambient_light_x;
    h->ambient_light_y     = s->ambient_light_y;

    return 0;
}

static int decode_mastering_display_colour_volume(H2645SEIMasteringDisplay *h, const SEIRawMasteringDisplayColourVolume *s)
{
    h->present = 1;

    for (int c = 0; c < 3; c++) {
        h->display_primaries[c][0] = s->display_primaries_x[c];
        h->display_primaries[c][1] = s->display_primaries_y[c];
    }

    h->white_point[0] = s->white_point_x;
    h->white_point[1] = s->white_point_y;

    h->max_luminance  = s->max_display_mastering_luminance;
    h->min_luminance  = s->min_display_mastering_luminance;

    return 0;
}

int ff_vvc_sei_decode(VVCSEI *s, const H266RawSEI *sei, const struct VVCFrameContext *fc)
{
    H2645SEI *c  = &s->common;

    if (!sei)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < sei->message_list.nb_messages; i++) {
        int ret = 0;
        SEIRawMessage *message = &sei->message_list.messages[i];
        void *payload          = message->payload;

        switch (message->payload_type) {
        case SEI_TYPE_FILM_GRAIN_CHARACTERISTICS:
            av_refstruct_unref(&c->film_grain_characteristics);
            c->film_grain_characteristics = av_refstruct_allocz(sizeof(*c->film_grain_characteristics));
            if (!c->film_grain_characteristics)
                return AVERROR(ENOMEM);
            ret = decode_film_grain_characteristics(c->film_grain_characteristics, payload, fc);
            break;

        case SEI_TYPE_DECODED_PICTURE_HASH:
            ret = decode_decoded_picture_hash(&s->picture_hash, payload);
            break;

        case SEI_TYPE_DISPLAY_ORIENTATION:
            ret = decode_display_orientation(&s->common.display_orientation, payload);
            break;

        case SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO:
            ret = decode_content_light_level_info(&s->common.content_light, payload);
            break;

        case SEI_TYPE_FRAME_FIELD_INFO:
            ret = decode_frame_field_info(&s->frame_field_info, payload);
            break;

        case SEI_TYPE_AMBIENT_VIEWING_ENVIRONMENT:
            ret = decode_ambient_viewing_environment(&s->common.ambient_viewing_environment, payload);
            break;

        case SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME:
            ret = decode_mastering_display_colour_volume(&s->common.mastering_display, payload);
            break;

        default:
            av_log(fc->log_ctx, AV_LOG_DEBUG, "Skipped %s SEI %d\n",
                sei->nal_unit_header.nal_unit_type == VVC_PREFIX_SEI_NUT ?
                    "PREFIX" : "SUFFIX", message->payload_type);
            return FF_H2645_SEI_MESSAGE_UNHANDLED;
        }

        if (ret == AVERROR(ENOMEM))
            return ret;
        if (ret < 0)
            av_log(fc->log_ctx, AV_LOG_WARNING, "Failure to parse %s SEI %d: %s\n",
                sei->nal_unit_header.nal_unit_type == VVC_PREFIX_SEI_NUT ?
                    "PREFIX" : "SUFFIX", message->payload_type, av_err2str(ret));
    }

    return 0;
}

int ff_vvc_sei_replace(VVCSEI *dst, const VVCSEI *src)
{
    dst->picture_hash.present = 0;        // drop hash
    dst->frame_field_info.present = 0;    // drop field info
    return ff_h2645_sei_ctx_replace(&dst->common, &src->common);
}

void ff_vvc_sei_reset(VVCSEI *s)
{
    ff_h2645_sei_reset(&s->common);
    s->picture_hash.present = 0;
    s->frame_field_info.present = 0;
}
