/*
 * Copyright (c) 2011 Stefano Sabatini
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

/**
 * @file
 * filter for showing textual video frame information
 */

#include <inttypes.h>

#include "libavutil/bswap.h"
#include "libavutil/adler32.h"
#include "libavutil/display.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/hdr_dynamic_vivid_metadata.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timestamp.h"
#include "libavutil/timecode.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/video_enc_params.h"
#include "libavutil/detection_bbox.h"

#include "avfilter.h"
#include "internal.h"
#include "video.h"

typedef struct ShowInfoContext {
    const AVClass *class;
    int calculate_checksums;
} ShowInfoContext;

#define OFFSET(x) offsetof(ShowInfoContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption showinfo_options[] = {
    { "checksum", "calculate checksums", OFFSET(calculate_checksums), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showinfo);

static void dump_spherical(AVFilterContext *ctx, AVFrame *frame, const AVFrameSideData *sd)
{
    const AVSphericalMapping *spherical = (const AVSphericalMapping *)sd->data;
    double yaw, pitch, roll;

    av_log(ctx, AV_LOG_INFO, "spherical information: ");
    if (sd->size < sizeof(*spherical)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    if (spherical->projection == AV_SPHERICAL_EQUIRECTANGULAR)
        av_log(ctx, AV_LOG_INFO, "equirectangular ");
    else if (spherical->projection == AV_SPHERICAL_CUBEMAP)
        av_log(ctx, AV_LOG_INFO, "cubemap ");
    else if (spherical->projection == AV_SPHERICAL_EQUIRECTANGULAR_TILE)
        av_log(ctx, AV_LOG_INFO, "tiled equirectangular ");
    else {
        av_log(ctx, AV_LOG_WARNING, "unknown\n");
        return;
    }

    yaw = ((double)spherical->yaw) / (1 << 16);
    pitch = ((double)spherical->pitch) / (1 << 16);
    roll = ((double)spherical->roll) / (1 << 16);
    av_log(ctx, AV_LOG_INFO, "(%f/%f/%f) ", yaw, pitch, roll);

    if (spherical->projection == AV_SPHERICAL_EQUIRECTANGULAR_TILE) {
        size_t l, t, r, b;
        av_spherical_tile_bounds(spherical, frame->width, frame->height,
                                 &l, &t, &r, &b);
        av_log(ctx, AV_LOG_INFO,
               "[%"SIZE_SPECIFIER", %"SIZE_SPECIFIER", %"SIZE_SPECIFIER", %"SIZE_SPECIFIER"] ",
               l, t, r, b);
    } else if (spherical->projection == AV_SPHERICAL_CUBEMAP) {
        av_log(ctx, AV_LOG_INFO, "[pad %"PRIu32"] ", spherical->padding);
    }
}

static void dump_stereo3d(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const AVStereo3D *stereo;

    av_log(ctx, AV_LOG_INFO, "stereoscopic information: ");
    if (sd->size < sizeof(*stereo)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    stereo = (const AVStereo3D *)sd->data;

    av_log(ctx, AV_LOG_INFO, "type - %s", av_stereo3d_type_name(stereo->type));

    if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
        av_log(ctx, AV_LOG_INFO, " (inverted)");
}

static void dump_s12m_timecode(AVFilterContext *ctx, AVRational frame_rate, const AVFrameSideData *sd)
{
    const uint32_t *tc = (const uint32_t *)sd->data;

    if ((sd->size != sizeof(uint32_t) * 4) || (tc[0] > 3)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    for (int j = 1; j <= tc[0]; j++) {
        char tcbuf[AV_TIMECODE_STR_SIZE];
        av_timecode_make_smpte_tc_string2(tcbuf, frame_rate, tc[j], 0, 0);
        av_log(ctx, AV_LOG_INFO, "timecode - %s%s", tcbuf, j != tc[0]  ? ", " : "");
    }
}

static void dump_roi(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    int nb_rois;
    const AVRegionOfInterest *roi;
    uint32_t roi_size;

    roi = (const AVRegionOfInterest *)sd->data;
    roi_size = roi->self_size;
    if (!roi_size || sd->size % roi_size != 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid AVRegionOfInterest.self_size.\n");
        return;
    }
    nb_rois = sd->size / roi_size;

    av_log(ctx, AV_LOG_INFO, "Regions Of Interest(ROI) information:\n");
    for (int i = 0; i < nb_rois; i++) {
        roi = (const AVRegionOfInterest *)(sd->data + roi_size * i);
        av_log(ctx, AV_LOG_INFO, "index: %d, region: (%d, %d) -> (%d, %d), qp offset: %d/%d.\n",
               i, roi->left, roi->top, roi->right, roi->bottom, roi->qoffset.num, roi->qoffset.den);
    }
}

static void dump_detection_bbox(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    int nb_bboxes;
    const AVDetectionBBoxHeader *header;
    const AVDetectionBBox *bbox;

    header = (const AVDetectionBBoxHeader *)sd->data;
    nb_bboxes = header->nb_bboxes;
    av_log(ctx, AV_LOG_INFO, "detection bounding boxes:\n");
    av_log(ctx, AV_LOG_INFO, "source: %s\n", header->source);

    for (int i = 0; i < nb_bboxes; i++) {
        bbox = av_get_detection_bbox(header, i);
        av_log(ctx, AV_LOG_INFO, "index: %d,\tregion: (%d, %d) -> (%d, %d), label: %s, confidence: %d/%d.\n",
                                 i, bbox->x, bbox->y, bbox->x + bbox->w, bbox->y + bbox->h,
                                 bbox->detect_label, bbox->detect_confidence.num, bbox->detect_confidence.den);
        if (bbox->classify_count > 0) {
            for (int j = 0; j < bbox->classify_count; j++) {
                av_log(ctx, AV_LOG_INFO, "\t\tclassify:  label: %s, confidence: %d/%d.\n",
                       bbox->classify_labels[j], bbox->classify_confidences[j].num, bbox->classify_confidences[j].den);
            }
        }
    }
}

static void dump_mastering_display(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const AVMasteringDisplayMetadata *mastering_display;

    av_log(ctx, AV_LOG_INFO, "mastering display: ");
    if (sd->size < sizeof(*mastering_display)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    mastering_display = (const AVMasteringDisplayMetadata *)sd->data;

    av_log(ctx, AV_LOG_INFO, "has_primaries:%d has_luminance:%d "
           "r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f) "
           "min_luminance=%f, max_luminance=%f",
           mastering_display->has_primaries, mastering_display->has_luminance,
           av_q2d(mastering_display->display_primaries[0][0]),
           av_q2d(mastering_display->display_primaries[0][1]),
           av_q2d(mastering_display->display_primaries[1][0]),
           av_q2d(mastering_display->display_primaries[1][1]),
           av_q2d(mastering_display->display_primaries[2][0]),
           av_q2d(mastering_display->display_primaries[2][1]),
           av_q2d(mastering_display->white_point[0]), av_q2d(mastering_display->white_point[1]),
           av_q2d(mastering_display->min_luminance), av_q2d(mastering_display->max_luminance));
}

static void dump_dynamic_hdr_plus(AVFilterContext *ctx, AVFrameSideData *sd)
{
    AVDynamicHDRPlus *hdr_plus;

    av_log(ctx, AV_LOG_INFO, "HDR10+ metadata: ");
    if (sd->size < sizeof(*hdr_plus)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    hdr_plus = (AVDynamicHDRPlus *)sd->data;
    av_log(ctx, AV_LOG_INFO, "application version: %d, ", hdr_plus->application_version);
    av_log(ctx, AV_LOG_INFO, "num_windows: %d, ", hdr_plus->num_windows);
    for (int w = 1; w < hdr_plus->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &hdr_plus->params[w];
        av_log(ctx, AV_LOG_INFO, w > 1 ? ", window %d { " : "window %d { ", w);
        av_log(ctx, AV_LOG_INFO, "window_upper_left_corner: (%5.4f,%5.4f),",
               av_q2d(params->window_upper_left_corner_x),
               av_q2d(params->window_upper_left_corner_y));
        av_log(ctx, AV_LOG_INFO, "window_lower_right_corner: (%5.4f,%5.4f), ",
               av_q2d(params->window_lower_right_corner_x),
               av_q2d(params->window_lower_right_corner_y));
        av_log(ctx, AV_LOG_INFO, "window_upper_left_corner: (%5.4f, %5.4f), ",
               av_q2d(params->window_upper_left_corner_x),
               av_q2d(params->window_upper_left_corner_y));
        av_log(ctx, AV_LOG_INFO, "center_of_ellipse_x: (%d,%d), ",
               params->center_of_ellipse_x,
               params->center_of_ellipse_y);
        av_log(ctx, AV_LOG_INFO, "rotation_angle: %d, ",
               params->rotation_angle);
        av_log(ctx, AV_LOG_INFO, "semimajor_axis_internal_ellipse: %d, ",
               params->semimajor_axis_internal_ellipse);
        av_log(ctx, AV_LOG_INFO, "semimajor_axis_external_ellipse: %d, ",
               params->semimajor_axis_external_ellipse);
        av_log(ctx, AV_LOG_INFO, "semiminor_axis_external_ellipse: %d, ",
               params->semiminor_axis_external_ellipse);
        av_log(ctx, AV_LOG_INFO, "overlap_process_option: %d}",
               params->overlap_process_option);
    }
    av_log(ctx, AV_LOG_INFO, "targeted_system_display_maximum_luminance: %9.4f, ",
           av_q2d(hdr_plus->targeted_system_display_maximum_luminance));
    if (hdr_plus->targeted_system_display_actual_peak_luminance_flag) {
        av_log(ctx, AV_LOG_INFO, "targeted_system_display_actual_peak_luminance: {");
        for (int i = 0; i < hdr_plus->num_rows_targeted_system_display_actual_peak_luminance; i++) {
            av_log(ctx, AV_LOG_INFO, "(");
            for (int j = 0; j < hdr_plus->num_cols_targeted_system_display_actual_peak_luminance; j++) {
                av_log(ctx, AV_LOG_INFO, i ? ",%5.4f" : "%5.4f",
                       av_q2d(hdr_plus->targeted_system_display_actual_peak_luminance[i][j]));
            }
            av_log(ctx, AV_LOG_INFO, ")");
        }
        av_log(ctx, AV_LOG_INFO, "}, ");
    }

    for (int w = 0; w < hdr_plus->num_windows; w++) {
        AVHDRPlusColorTransformParams *params = &hdr_plus->params[w];
        av_log(ctx, AV_LOG_INFO, "window %d {maxscl: {", w);
        for (int i = 0; i < 3; i++) {
            av_log(ctx, AV_LOG_INFO, i ? ",%5.4f" : "%5.4f",av_q2d(params->maxscl[i]));
        }
        av_log(ctx, AV_LOG_INFO, "}, average_maxrgb: %5.4f, ",
               av_q2d(params->average_maxrgb));
        av_log(ctx, AV_LOG_INFO, "distribution_maxrgb: {");
        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            av_log(ctx, AV_LOG_INFO, "(%d,%5.4f)",
                   params->distribution_maxrgb[i].percentage,
                   av_q2d(params->distribution_maxrgb[i].percentile));
        }
        av_log(ctx, AV_LOG_INFO, "}, fraction_bright_pixels: %5.4f",
               av_q2d(params->fraction_bright_pixels));
        if (params->tone_mapping_flag) {
            av_log(ctx, AV_LOG_INFO, ", knee_point: (%5.4f,%5.4f), ", av_q2d(params->knee_point_x), av_q2d(params->knee_point_y));
            av_log(ctx, AV_LOG_INFO, "bezier_curve_anchors: {");
            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                av_log(ctx, AV_LOG_INFO, i ? ",%5.4f" : "%5.4f",
                       av_q2d(params->bezier_curve_anchors[i]));
            }
            av_log(ctx, AV_LOG_INFO, "}");
        }
        if (params->color_saturation_mapping_flag) {
            av_log(ctx, AV_LOG_INFO, ", color_saturation_weight: %5.4f",
                   av_q2d(params->color_saturation_weight));
        }
        av_log(ctx, AV_LOG_INFO, "}");
    }

    if (hdr_plus->mastering_display_actual_peak_luminance_flag) {
        av_log(ctx, AV_LOG_INFO, ", mastering_display_actual_peak_luminance: {");
        for (int i = 0; i < hdr_plus->num_rows_mastering_display_actual_peak_luminance; i++) {
            av_log(ctx, AV_LOG_INFO, "(");
            for (int j = 0; j <  hdr_plus->num_cols_mastering_display_actual_peak_luminance; j++) {
                av_log(ctx, AV_LOG_INFO, i ? ",%5.4f" : "%5.4f",
                       av_q2d(hdr_plus->mastering_display_actual_peak_luminance[i][j]));
            }
            av_log(ctx, AV_LOG_INFO, ")");
        }
        av_log(ctx, AV_LOG_INFO, "}");
    }
}

static void dump_dynamic_hdr_vivid(AVFilterContext *ctx, AVFrameSideData *sd)
{
    AVDynamicHDRVivid *hdr_vivid;

    av_log(ctx, AV_LOG_INFO, "HDR Vivid metadata: ");
    if (sd->size < sizeof(*hdr_vivid)) {
        av_log(ctx, AV_LOG_ERROR, "invalid hdr vivid data\n");
        return;
    }

    hdr_vivid = (AVDynamicHDRVivid *)sd->data;
    av_log(ctx, AV_LOG_INFO, "system_start_code: %d, ", hdr_vivid->system_start_code);
    av_log(ctx, AV_LOG_INFO, "num_windows: %d, ", hdr_vivid->num_windows);
    for (int w = 0; w < hdr_vivid->num_windows; w++) {
        const AVHDRVividColorTransformParams *params = &hdr_vivid->params[w];

        av_log(ctx, AV_LOG_INFO, "minimum_maxrgb[%d]: %.4f, ", w, av_q2d(params->minimum_maxrgb));
        av_log(ctx, AV_LOG_INFO, "average_maxrgb[%d]: %.4f, ", w, av_q2d(params->average_maxrgb));
        av_log(ctx, AV_LOG_INFO, "variance_maxrgb[%d]:%.4f, ", w, av_q2d(params->variance_maxrgb));
        av_log(ctx, AV_LOG_INFO, "maximum_maxrgb[%d]: %.4f, ", w, av_q2d(params->maximum_maxrgb));
    }

    for (int w = 0; w < hdr_vivid->num_windows; w++) {
        const AVHDRVividColorTransformParams *params = &hdr_vivid->params[w];

        av_log(ctx, AV_LOG_INFO, "tone_mapping_mode_flag[%d]: %d, ", w, params->tone_mapping_mode_flag);
        av_log(ctx, AV_LOG_INFO, "tone_mapping_param_num[%d]: %d, ", w, params->tone_mapping_param_num);
        if (params->tone_mapping_mode_flag) {
            for (int i = 0; i < params->tone_mapping_param_num; i++) {
                const AVHDRVividColorToneMappingParams *tm_params = &params->tm_params[i];

                av_log(ctx, AV_LOG_INFO, "targeted_system_display_maximum_luminance[%d][%d]: %.4f, ",
                       w, i, av_q2d(tm_params->targeted_system_display_maximum_luminance));
                av_log(ctx, AV_LOG_INFO, "base_enable_flag[%d][%d]: %d, ",
                       w, i, tm_params->base_enable_flag);
                if (tm_params->base_enable_flag) {
                    av_log(ctx, AV_LOG_INFO, "base_param_m_p[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_m_p));
                    av_log(ctx, AV_LOG_INFO, "base_param_m_m[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_m_m));
                    av_log(ctx, AV_LOG_INFO, "base_param_m_a[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_m_a));
                    av_log(ctx, AV_LOG_INFO, "base_param_m_b[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_m_b));
                    av_log(ctx, AV_LOG_INFO, "base_param_m_n[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_m_n));
                    av_log(ctx, AV_LOG_INFO, "base_param_k1[%d][%d]:  %d, ", w, i, tm_params->base_param_k1);
                    av_log(ctx, AV_LOG_INFO, "base_param_k2[%d][%d]:  %d, ", w, i, tm_params->base_param_k2);
                    av_log(ctx, AV_LOG_INFO, "base_param_k3[%d][%d]:  %d, ", w, i, tm_params->base_param_k3);
                    av_log(ctx, AV_LOG_INFO, "base_param_Delta_enable_mode[%d][%d]: %d, ", w, i,
                           tm_params->base_param_Delta_enable_mode);
                    av_log(ctx, AV_LOG_INFO, "base_param_Delta[%d][%d]: %.4f, ", w, i, av_q2d(tm_params->base_param_Delta));
                }
                av_log(ctx, AV_LOG_INFO, "3Spline_enable_flag[%d][%d]: %d, ",
                       w, i, tm_params->three_Spline_enable_flag);
                if (tm_params->three_Spline_enable_flag) {
                    av_log(ctx, AV_LOG_INFO, "3Spline_TH_mode[%d][%d]:  %d, ", w, i, tm_params->three_Spline_TH_mode);

                    for (int j = 0; j < tm_params->three_Spline_num; j++) {
                        av_log(ctx, AV_LOG_INFO, "3Spline_TH_enable_MB[%d][%d][%d]: %.4f, ",
                                w, i, j, av_q2d(tm_params->three_Spline_TH_enable_MB));
                        av_log(ctx, AV_LOG_INFO, "3Spline_TH_enable[%d][%d][%d]: %.4f, ",
                                w, i, j, av_q2d(tm_params->three_Spline_TH_enable));
                        av_log(ctx, AV_LOG_INFO, "3Spline_TH_Delta1[%d][%d][%d]: %.4f, ",
                                w, i, j, av_q2d(tm_params->three_Spline_TH_Delta1));
                        av_log(ctx, AV_LOG_INFO, "3Spline_TH_Delta2[%d][%d][%d]: %.4f, ",
                                w, i, j, av_q2d(tm_params->three_Spline_TH_Delta2));
                        av_log(ctx, AV_LOG_INFO, "3Spline_enable_Strength[%d][%d][%d]: %.4f, ",
                                w, i, j, av_q2d(tm_params->three_Spline_enable_Strength));
                    }
                }
            }
        }

        av_log(ctx, AV_LOG_INFO, "color_saturation_mapping_flag[%d]: %d",
                w, params->color_saturation_mapping_flag);
        if (params->color_saturation_mapping_flag) {
            av_log(ctx, AV_LOG_INFO, ", color_saturation_num[%d]: %d",
                   w, params->color_saturation_num);
            for (int i = 0; i < params->color_saturation_num; i++) {
                av_log(ctx, AV_LOG_INFO, ", color_saturation_gain[%d][%d]: %.4f",
                       w, i, av_q2d(params->color_saturation_gain[i]));
            }
        }
    }
}


static void dump_content_light_metadata(AVFilterContext *ctx, AVFrameSideData *sd)
{
    const AVContentLightMetadata *metadata = (const AVContentLightMetadata *)sd->data;

    av_log(ctx, AV_LOG_INFO, "Content Light Level information: "
           "MaxCLL=%d, MaxFALL=%d",
           metadata->MaxCLL, metadata->MaxFALL);
}

static void dump_video_enc_params(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const AVVideoEncParams *par = (const AVVideoEncParams *)sd->data;
    int plane, acdc;

    av_log(ctx, AV_LOG_INFO, "video encoding parameters: type %d; ", par->type);
    if (par->qp)
        av_log(ctx, AV_LOG_INFO, "qp=%d; ", par->qp);
    for (plane = 0; plane < FF_ARRAY_ELEMS(par->delta_qp); plane++)
        for (acdc = 0; acdc < FF_ARRAY_ELEMS(par->delta_qp[plane]); acdc++) {
            int delta_qp = par->delta_qp[plane][acdc];
            if (delta_qp)
                av_log(ctx, AV_LOG_INFO, "delta_qp[%d][%d]=%d; ",
                       plane, acdc, delta_qp);
        }
    if (par->nb_blocks)
        av_log(ctx, AV_LOG_INFO, "%u blocks; ", par->nb_blocks);
}

static void dump_sei_unregistered_metadata(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const int uuid_size = 16;
    const uint8_t *user_data = sd->data;
    int i;

    if (sd->size < uuid_size) {
        av_log(ctx, AV_LOG_ERROR, "invalid data(%"SIZE_SPECIFIER" < "
               "UUID(%d-bytes))\n", sd->size, uuid_size);
        return;
    }

    av_log(ctx, AV_LOG_INFO, "User Data Unregistered:\n");
    av_log(ctx, AV_LOG_INFO, "UUID=");
    for (i = 0; i < uuid_size; i++) {
        av_log(ctx, AV_LOG_INFO, "%02x", user_data[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9)
            av_log(ctx, AV_LOG_INFO, "-");
    }
    av_log(ctx, AV_LOG_INFO, "\n");

    av_log(ctx, AV_LOG_INFO, "User Data=");
    for (; i < sd->size; i++) {
        av_log(ctx, AV_LOG_INFO, "%02x", user_data[i]);
    }
    av_log(ctx, AV_LOG_INFO, "\n");
}

static void dump_sei_film_grain_params_metadata(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const AVFilmGrainParams *fgp = (const AVFilmGrainParams *)sd->data;
    const char *const film_grain_type_names[] = {
        [AV_FILM_GRAIN_PARAMS_NONE] = "none",
        [AV_FILM_GRAIN_PARAMS_AV1]  = "av1",
        [AV_FILM_GRAIN_PARAMS_H274] = "h274",
    };

    if (fgp->type >= FF_ARRAY_ELEMS(film_grain_type_names)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    av_log(ctx, AV_LOG_INFO, "film grain parameters: type %s; ", film_grain_type_names[fgp->type]);
    av_log(ctx, AV_LOG_INFO, "seed=%"PRIu64"; ", fgp->seed);

    switch (fgp->type) {
    case AV_FILM_GRAIN_PARAMS_NONE:
    case AV_FILM_GRAIN_PARAMS_AV1:
        return;
    case AV_FILM_GRAIN_PARAMS_H274: {
        const AVFilmGrainH274Params *h274 = &fgp->codec.h274;
        const char *color_range_str     = av_color_range_name(h274->color_range);
        const char *color_primaries_str = av_color_primaries_name(h274->color_primaries);
        const char *color_trc_str       = av_color_transfer_name(h274->color_trc);
        const char *colorspace_str      = av_color_space_name(h274->color_space);

        av_log(ctx, AV_LOG_INFO, "model_id=%d; ", h274->model_id);
        av_log(ctx, AV_LOG_INFO, "bit_depth_luma=%d; ", h274->bit_depth_luma);
        av_log(ctx, AV_LOG_INFO, "bit_depth_chroma=%d; ", h274->bit_depth_chroma);
        av_log(ctx, AV_LOG_INFO, "color_range=%s; ", color_range_str ? color_range_str : "unknown");
        av_log(ctx, AV_LOG_INFO, "color_primaries=%s; ", color_primaries_str ? color_primaries_str : "unknown");
        av_log(ctx, AV_LOG_INFO, "color_trc=%s; ", color_trc_str ? color_trc_str : "unknown");
        av_log(ctx, AV_LOG_INFO, "color_space=%s; ", colorspace_str ? colorspace_str : "unknown");
        av_log(ctx, AV_LOG_INFO, "blending_mode_id=%d; ", h274->blending_mode_id);
        av_log(ctx, AV_LOG_INFO, "log2_scale_factor=%d; ", h274->log2_scale_factor);

        for (int c = 0; c < 3; c++)
            if (h274->component_model_present[c] && (h274->num_model_values[c] > 6 ||
                                                     h274->num_intensity_intervals[c] < 1 ||
                                                     h274->num_intensity_intervals[c] > 256)) {
                av_log(ctx, AV_LOG_ERROR, "invalid data\n");
                return;
            }

        for (int c = 0; c < 3; c++) {
            if (!h274->component_model_present[c])
                continue;

            av_log(ctx, AV_LOG_INFO, "num_intensity_intervals[%d]=%u; ", c, h274->num_intensity_intervals[c]);
            av_log(ctx, AV_LOG_INFO, "num_model_values[%d]=%u; ", c, h274->num_model_values[c]);
            for (int i = 0; i < h274->num_intensity_intervals[c]; i++) {
                av_log(ctx, AV_LOG_INFO, "intensity_interval_lower_bound[%d][%d]=%u; ",
                                         c, i, h274->intensity_interval_lower_bound[c][i]);
                av_log(ctx, AV_LOG_INFO, "intensity_interval_upper_bound[%d][%d]=%u; ",
                                         c, i, h274->intensity_interval_upper_bound[c][i]);
                for (int j = 0; j < h274->num_model_values[c]; j++)
                    av_log(ctx, AV_LOG_INFO, "comp_model_value[%d][%d][%d]=%d; ",
                                             c, i, j, h274->comp_model_value[c][i][j]);
            }
        }
        break;
    }
    }
}

static void dump_dovi_metadata(AVFilterContext *ctx, const AVFrameSideData *sd)
{
    const AVDOVIMetadata *dovi = (AVDOVIMetadata *) sd->data;
    const AVDOVIRpuDataHeader *hdr = av_dovi_get_header(dovi);
    const AVDOVIDataMapping *mapping = av_dovi_get_mapping(dovi);
    const AVDOVIColorMetadata *color = av_dovi_get_color(dovi);

    av_log(ctx, AV_LOG_INFO, "Dolby Vision Metadata:\n");
    av_log(ctx, AV_LOG_INFO, "    rpu_type=%"PRIu8"; ", hdr->rpu_type);
    av_log(ctx, AV_LOG_INFO, "rpu_format=%"PRIu16"; ", hdr->rpu_format);
    av_log(ctx, AV_LOG_INFO, "vdr_rpu_profile=%"PRIu8"; ", hdr->vdr_rpu_profile);
    av_log(ctx, AV_LOG_INFO, "vdr_rpu_level=%"PRIu8"; ", hdr->vdr_rpu_level);
    av_log(ctx, AV_LOG_INFO, "chroma_resampling_explicit_filter_flag=%"PRIu8"; ", hdr->chroma_resampling_explicit_filter_flag);
    av_log(ctx, AV_LOG_INFO, "coef_data_type=%"PRIu8"; ", hdr->coef_data_type);
    av_log(ctx, AV_LOG_INFO, "coef_log2_denom=%"PRIu8"; ", hdr->coef_log2_denom);
    av_log(ctx, AV_LOG_INFO, "vdr_rpu_normalized_idc=%"PRIu8"; ", hdr->vdr_rpu_normalized_idc);
    av_log(ctx, AV_LOG_INFO, "bl_video_full_range_flag=%"PRIu8"; ", hdr->bl_video_full_range_flag);
    av_log(ctx, AV_LOG_INFO, "bl_bit_depth=%"PRIu8"; ", hdr->bl_bit_depth);
    av_log(ctx, AV_LOG_INFO, "el_bit_depth=%"PRIu8"; ", hdr->el_bit_depth);
    av_log(ctx, AV_LOG_INFO, "vdr_bit_depth=%"PRIu8"; ", hdr->vdr_bit_depth);
    av_log(ctx, AV_LOG_INFO, "spatial_resampling_filter_flag=%"PRIu8"; ", hdr->spatial_resampling_filter_flag);
    av_log(ctx, AV_LOG_INFO, "el_spatial_resampling_filter_flag=%"PRIu8"; ", hdr->el_spatial_resampling_filter_flag);
    av_log(ctx, AV_LOG_INFO, "disable_residual_flag=%"PRIu8"\n", hdr->disable_residual_flag);

    av_log(ctx, AV_LOG_INFO, "    data mapping: ");
    av_log(ctx, AV_LOG_INFO, "vdr_rpu_id=%"PRIu8"; ", mapping->vdr_rpu_id);
    av_log(ctx, AV_LOG_INFO, "mapping_color_space=%"PRIu8"; ", mapping->mapping_color_space);
    av_log(ctx, AV_LOG_INFO, "mapping_chroma_format_idc=%"PRIu8"; ", mapping->mapping_chroma_format_idc);
    av_log(ctx, AV_LOG_INFO, "nlq_method_idc=%d; ", (int) mapping->nlq_method_idc);
    av_log(ctx, AV_LOG_INFO, "num_x_partitions=%"PRIu32"; ", mapping->num_x_partitions);
    av_log(ctx, AV_LOG_INFO, "num_y_partitions=%"PRIu32"\n", mapping->num_y_partitions);

    for (int c = 0; c < 3; c++) {
        const AVDOVIReshapingCurve *curve = &mapping->curves[c];
        const AVDOVINLQParams *nlq = &mapping->nlq[c];
        av_log(ctx, AV_LOG_INFO, "      channel %d: ", c);
        av_log(ctx, AV_LOG_INFO, "pivots={ ");
        for (int i = 0; i < curve->num_pivots; i++)
            av_log(ctx, AV_LOG_INFO, "%"PRIu16" ", curve->pivots[i]);
        av_log(ctx, AV_LOG_INFO, "}; mapping_idc={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++)
            av_log(ctx, AV_LOG_INFO, "%d ", (int) curve->mapping_idc[i]);
        av_log(ctx, AV_LOG_INFO, "}; poly_order={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++)
            av_log(ctx, AV_LOG_INFO, "%"PRIu8" ", curve->poly_order[i]);
        av_log(ctx, AV_LOG_INFO, "}; poly_coef={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++) {
            av_log(ctx, AV_LOG_INFO, "{%"PRIi64", %"PRIi64", %"PRIi64"} ",
                   curve->poly_coef[i][0],
                   curve->poly_coef[i][1],
                   curve->poly_coef[i][2]);
        }

        av_log(ctx, AV_LOG_INFO, "}; mmr_order={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++)
            av_log(ctx, AV_LOG_INFO, "%"PRIu8" ", curve->mmr_order[i]);
        av_log(ctx, AV_LOG_INFO, "}; mmr_constant={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++)
            av_log(ctx, AV_LOG_INFO, "%"PRIi64" ", curve->mmr_constant[i]);
        av_log(ctx, AV_LOG_INFO, "}; mmr_coef={ ");
        for (int i = 0; i < curve->num_pivots - 1; i++) {
            av_log(ctx, AV_LOG_INFO, "{");
            for (int j = 0; j < curve->mmr_order[i]; j++) {
                for (int k = 0; k < 7; k++)
                    av_log(ctx, AV_LOG_INFO, "%"PRIi64" ", curve->mmr_coef[i][j][k]);
            }
            av_log(ctx, AV_LOG_INFO, "} ");
        }

        av_log(ctx, AV_LOG_INFO, "}; nlq_offset=%"PRIu16"; ", nlq->nlq_offset);
        av_log(ctx, AV_LOG_INFO, "vdr_in_max=%"PRIu64"; ", nlq->vdr_in_max);
        switch (mapping->nlq_method_idc) {
        case AV_DOVI_NLQ_LINEAR_DZ:
            av_log(ctx, AV_LOG_INFO, "linear_deadzone_slope=%"PRIu64"; ", nlq->linear_deadzone_slope);
            av_log(ctx, AV_LOG_INFO, "linear_deadzone_threshold=%"PRIu64"\n", nlq->linear_deadzone_threshold);
            break;
        }
    }

    av_log(ctx, AV_LOG_INFO, "    color metadata: ");
    av_log(ctx, AV_LOG_INFO, "dm_metadata_id=%"PRIu8"; ", color->dm_metadata_id);
    av_log(ctx, AV_LOG_INFO, "scene_refresh_flag=%"PRIu8"; ", color->scene_refresh_flag);
    av_log(ctx, AV_LOG_INFO, "ycc_to_rgb_matrix={ ");
    for (int i = 0; i < 9; i++)
        av_log(ctx, AV_LOG_INFO, "%f ", av_q2d(color->ycc_to_rgb_matrix[i]));
    av_log(ctx, AV_LOG_INFO, "}; ycc_to_rgb_offset={ ");
    for (int i = 0; i < 3; i++)
        av_log(ctx, AV_LOG_INFO, "%f ", av_q2d(color->ycc_to_rgb_offset[i]));
    av_log(ctx, AV_LOG_INFO, "}; rgb_to_lms_matrix={ ");
    for (int i = 0; i < 9; i++)
        av_log(ctx, AV_LOG_INFO, "%f ", av_q2d(color->rgb_to_lms_matrix[i]));
    av_log(ctx, AV_LOG_INFO, "}; signal_eotf=%"PRIu16"; ", color->signal_eotf);
    av_log(ctx, AV_LOG_INFO, "signal_eotf_param0=%"PRIu16"; ", color->signal_eotf_param0);
    av_log(ctx, AV_LOG_INFO, "signal_eotf_param1=%"PRIu16"; ", color->signal_eotf_param1);
    av_log(ctx, AV_LOG_INFO, "signal_eotf_param2=%"PRIu32"; ", color->signal_eotf_param2);
    av_log(ctx, AV_LOG_INFO, "signal_bit_depth=%"PRIu8"; ", color->signal_bit_depth);
    av_log(ctx, AV_LOG_INFO, "signal_color_space=%"PRIu8"; ", color->signal_color_space);
    av_log(ctx, AV_LOG_INFO, "signal_chroma_format=%"PRIu8"; ", color->signal_chroma_format);
    av_log(ctx, AV_LOG_INFO, "signal_full_range_flag=%"PRIu8"; ", color->signal_full_range_flag);
    av_log(ctx, AV_LOG_INFO, "source_min_pq=%"PRIu16"; ", color->source_min_pq);
    av_log(ctx, AV_LOG_INFO, "source_max_pq=%"PRIu16"; ", color->source_max_pq);
    av_log(ctx, AV_LOG_INFO, "source_diagonal=%"PRIu16"; ", color->source_diagonal);
}

static void dump_color_property(AVFilterContext *ctx, AVFrame *frame)
{
    const char *color_range_str     = av_color_range_name(frame->color_range);
    const char *colorspace_str      = av_color_space_name(frame->colorspace);
    const char *color_primaries_str = av_color_primaries_name(frame->color_primaries);
    const char *color_trc_str       = av_color_transfer_name(frame->color_trc);

    if (!color_range_str || frame->color_range == AVCOL_RANGE_UNSPECIFIED) {
        av_log(ctx, AV_LOG_INFO, "color_range:unknown");
    } else {
        av_log(ctx, AV_LOG_INFO, "color_range:%s", color_range_str);
    }

    if (!colorspace_str || frame->colorspace == AVCOL_SPC_UNSPECIFIED) {
        av_log(ctx, AV_LOG_INFO, " color_space:unknown");
    } else {
        av_log(ctx, AV_LOG_INFO, " color_space:%s", colorspace_str);
    }

    if (!color_primaries_str || frame->color_primaries == AVCOL_PRI_UNSPECIFIED) {
        av_log(ctx, AV_LOG_INFO, " color_primaries:unknown");
    } else {
        av_log(ctx, AV_LOG_INFO, " color_primaries:%s", color_primaries_str);
    }

    if (!color_trc_str || frame->color_trc == AVCOL_TRC_UNSPECIFIED) {
        av_log(ctx, AV_LOG_INFO, " color_trc:unknown");
    } else {
        av_log(ctx, AV_LOG_INFO, " color_trc:%s", color_trc_str);
    }
    av_log(ctx, AV_LOG_INFO, "\n");
}

static void update_sample_stats_8(const uint8_t *src, int len, int64_t *sum, int64_t *sum2)
{
    int i;

    for (i = 0; i < len; i++) {
        *sum += src[i];
        *sum2 += src[i] * src[i];
    }
}

static void update_sample_stats_16(int be, const uint8_t *src, int len, int64_t *sum, int64_t *sum2)
{
    const uint16_t *src1 = (const uint16_t *)src;
    int i;

    for (i = 0; i < len / 2; i++) {
        if ((HAVE_BIGENDIAN && !be) || (!HAVE_BIGENDIAN && be)) {
            *sum += av_bswap16(src1[i]);
            *sum2 += (uint32_t)av_bswap16(src1[i]) * (uint32_t)av_bswap16(src1[i]);
        } else {
            *sum += src1[i];
            *sum2 += (uint32_t)src1[i] * (uint32_t)src1[i];
        }
    }
}

static void update_sample_stats(int depth, int be, const uint8_t *src, int len, int64_t *sum, int64_t *sum2)
{
    if (depth <= 8)
        update_sample_stats_8(src, len, sum, sum2);
    else
        update_sample_stats_16(be, src, len, sum, sum2);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    ShowInfoContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    uint32_t plane_checksum[4] = {0}, checksum = 0;
    int64_t sum[4] = {0}, sum2[4] = {0};
    int32_t pixelcount[4] = {0};
    int bitdepth = desc->comp[0].depth;
    int be = desc->flags & AV_PIX_FMT_FLAG_BE;
    int i, plane, vsub = desc->log2_chroma_h;

    for (plane = 0; plane < 4 && s->calculate_checksums && frame->data[plane] && frame->linesize[plane]; plane++) {
        uint8_t *data = frame->data[plane];
        int h = plane == 1 || plane == 2 ? AV_CEIL_RSHIFT(inlink->h, vsub) : inlink->h;
        int linesize = av_image_get_linesize(frame->format, frame->width, plane);
        int width = linesize >> (bitdepth > 8);

        if (linesize < 0)
            return linesize;

        for (i = 0; i < h; i++) {
            plane_checksum[plane] = av_adler32_update(plane_checksum[plane], data, linesize);
            checksum = av_adler32_update(checksum, data, linesize);

            update_sample_stats(bitdepth, be, data, linesize, sum+plane, sum2+plane);
            pixelcount[plane] += width;
            data += frame->linesize[plane];
        }
    }

    av_log(ctx, AV_LOG_INFO,
           "n:%4"PRId64" pts:%7s pts_time:%-7s pos:%9"PRId64" "
           "fmt:%s sar:%d/%d s:%dx%d i:%c iskey:%d type:%c ",
           inlink->frame_count_out,
           av_ts2str(frame->pts), av_ts2timestr(frame->pts, &inlink->time_base), frame->pkt_pos,
           desc->name,
           frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den,
           frame->width, frame->height,
           !frame->interlaced_frame ? 'P' :         /* Progressive  */
           frame->top_field_first   ? 'T' : 'B',    /* Top / Bottom */
           frame->key_frame,
           av_get_picture_type_char(frame->pict_type));

    if (s->calculate_checksums) {
        av_log(ctx, AV_LOG_INFO,
               "checksum:%08"PRIX32" plane_checksum:[%08"PRIX32,
               checksum, plane_checksum[0]);

        for (plane = 1; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++)
            av_log(ctx, AV_LOG_INFO, " %08"PRIX32, plane_checksum[plane]);
        av_log(ctx, AV_LOG_INFO, "] mean:[");
        for (plane = 0; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++)
            av_log(ctx, AV_LOG_INFO, "%"PRId64" ", (sum[plane] + pixelcount[plane]/2) / pixelcount[plane]);
        av_log(ctx, AV_LOG_INFO, "\b] stdev:[");
        for (plane = 0; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++)
            av_log(ctx, AV_LOG_INFO, "%3.1f ",
                   sqrt((sum2[plane] - sum[plane]*(double)sum[plane]/pixelcount[plane])/pixelcount[plane]));
        av_log(ctx, AV_LOG_INFO, "\b]");
    }
    av_log(ctx, AV_LOG_INFO, "\n");

    for (i = 0; i < frame->nb_side_data; i++) {
        AVFrameSideData *sd = frame->side_data[i];

        av_log(ctx, AV_LOG_INFO, "  side data - ");
        switch (sd->type) {
        case AV_FRAME_DATA_PANSCAN:
            av_log(ctx, AV_LOG_INFO, "pan/scan");
            break;
        case AV_FRAME_DATA_A53_CC:
            av_log(ctx, AV_LOG_INFO, "A/53 closed captions "
                   "(%"SIZE_SPECIFIER" bytes)", sd->size);
            break;
        case AV_FRAME_DATA_SPHERICAL:
            dump_spherical(ctx, frame, sd);
            break;
        case AV_FRAME_DATA_STEREO3D:
            dump_stereo3d(ctx, sd);
            break;
        case AV_FRAME_DATA_S12M_TIMECODE: {
            dump_s12m_timecode(ctx, inlink->frame_rate, sd);
            break;
        }
        case AV_FRAME_DATA_DISPLAYMATRIX:
            av_log(ctx, AV_LOG_INFO, "displaymatrix: rotation of %.2f degrees",
                   av_display_rotation_get((int32_t *)sd->data));
            break;
        case AV_FRAME_DATA_AFD:
            av_log(ctx, AV_LOG_INFO, "afd: value of %"PRIu8, sd->data[0]);
            break;
        case AV_FRAME_DATA_REGIONS_OF_INTEREST:
            dump_roi(ctx, sd);
            break;
        case AV_FRAME_DATA_DETECTION_BBOXES:
            dump_detection_bbox(ctx, sd);
            break;
        case AV_FRAME_DATA_MASTERING_DISPLAY_METADATA:
            dump_mastering_display(ctx, sd);
            break;
        case AV_FRAME_DATA_DYNAMIC_HDR_PLUS:
            dump_dynamic_hdr_plus(ctx, sd);
            break;
        case AV_FRAME_DATA_DYNAMIC_HDR_VIVID:
            dump_dynamic_hdr_vivid(ctx, sd);
            break;
        case AV_FRAME_DATA_CONTENT_LIGHT_LEVEL:
            dump_content_light_metadata(ctx, sd);
            break;
        case AV_FRAME_DATA_GOP_TIMECODE: {
            char tcbuf[AV_TIMECODE_STR_SIZE];
            av_timecode_make_mpeg_tc_string(tcbuf, *(int64_t *)(sd->data));
            av_log(ctx, AV_LOG_INFO, "GOP timecode - %s", tcbuf);
            break;
        }
        case AV_FRAME_DATA_VIDEO_ENC_PARAMS:
            dump_video_enc_params(ctx, sd);
            break;
        case AV_FRAME_DATA_SEI_UNREGISTERED:
            dump_sei_unregistered_metadata(ctx, sd);
            break;
        case AV_FRAME_DATA_FILM_GRAIN_PARAMS:
            dump_sei_film_grain_params_metadata(ctx, sd);
            break;
        case AV_FRAME_DATA_DOVI_METADATA:
            dump_dovi_metadata(ctx, sd);
            break;
        default:
            av_log(ctx, AV_LOG_WARNING, "unknown side data type %d "
                   "(%"SIZE_SPECIFIER" bytes)\n", sd->type, sd->size);
            break;
        }

        av_log(ctx, AV_LOG_INFO, "\n");
    }

    dump_color_property(ctx, frame);

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static int config_props(AVFilterContext *ctx, AVFilterLink *link, int is_out)
{

    av_log(ctx, AV_LOG_INFO, "config %s time_base: %d/%d, frame_rate: %d/%d\n",
           is_out ? "out" : "in",
           link->time_base.num, link->time_base.den,
           link->frame_rate.num, link->frame_rate.den);

    return 0;
}

static int config_props_in(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    return config_props(ctx, link, 0);
}

static int config_props_out(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    return config_props(ctx, link, 1);
}

static const AVFilterPad avfilter_vf_showinfo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
        .config_props     = config_props_in,
    },
};

static const AVFilterPad avfilter_vf_showinfo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_out,
    },
};

const AVFilter ff_vf_showinfo = {
    .name        = "showinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each video frame."),
    FILTER_INPUTS(avfilter_vf_showinfo_inputs),
    FILTER_OUTPUTS(avfilter_vf_showinfo_outputs),
    .priv_size   = sizeof(ShowInfoContext),
    .priv_class  = &showinfo_class,
    .flags       = AVFILTER_FLAG_METADATA_ONLY,
};
