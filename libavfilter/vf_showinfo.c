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
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timestamp.h"
#include "libavutil/timecode.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/video_enc_params.h"

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

    av_log(ctx, AV_LOG_INFO, "Regions Of Interest(RoI) information: ");
    for (int i = 0; i < nb_rois; i++) {
        roi = (const AVRegionOfInterest *)(sd->data + roi_size * i);
        av_log(ctx, AV_LOG_INFO, "index: %d, region: (%d, %d)/(%d, %d), qp offset: %d/%d.\n",
               i, roi->left, roi->top, roi->right, roi->bottom, roi->qoffset.num, roi->qoffset.den);
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

static void dump_content_light_metadata(AVFilterContext *ctx, const AVFrameSideData *sd)
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
        av_log(ctx, AV_LOG_ERROR, "invalid data(%d < UUID(%d-bytes))\n", sd->size, uuid_size);
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
            av_log(ctx, AV_LOG_INFO, "A/53 closed captions (%d bytes)", sd->size);
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
        case AV_FRAME_DATA_MASTERING_DISPLAY_METADATA:
            dump_mastering_display(ctx, sd);
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
        default:
            av_log(ctx, AV_LOG_WARNING, "unknown side data type %d (%d bytes)\n",
                   sd->type, sd->size);
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
    { NULL }
};

static const AVFilterPad avfilter_vf_showinfo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_out,
    },
    { NULL }
};

AVFilter ff_vf_showinfo = {
    .name        = "showinfo",
    .description = NULL_IF_CONFIG_SMALL("Show textual information for each video frame."),
    .inputs      = avfilter_vf_showinfo_inputs,
    .outputs     = avfilter_vf_showinfo_outputs,
    .priv_size   = sizeof(ShowInfoContext),
    .priv_class  = &showinfo_class,
};
