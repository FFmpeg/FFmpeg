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

/**
 * @file
 * filter for manipulating frame side data
 */

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

enum SideDataMode {
    SIDEDATA_SELECT,
    SIDEDATA_DELETE,
    SIDEDATA_NB
};

typedef struct SideDataContext {
    const AVClass *class;

    int mode;
    int type;   // enum AVFrameSideDataType or -1 for delete side data mode
} SideDataContext;

#define OFFSET(x) offsetof(SideDataContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS) \
static const AVOption filt_name##_options[] = { \
    { "mode", "set a mode of operation", OFFSET(mode),   AV_OPT_TYPE_INT,    {.i64 = 0 }, 0, SIDEDATA_NB-1, FLAGS, .unit = "mode" }, \
    {   "select", "select frame",        0,              AV_OPT_TYPE_CONST,  {.i64 = SIDEDATA_SELECT }, 0, 0, FLAGS, .unit = "mode" }, \
    {   "delete", "delete side data",    0,              AV_OPT_TYPE_CONST,  {.i64 = SIDEDATA_DELETE }, 0, 0, FLAGS, .unit = "mode" }, \
    { "type",   "set side data type",    OFFSET(type),   AV_OPT_TYPE_INT,    {.i64 = -1 }, -1, INT_MAX, FLAGS, .unit = "type" }, \
    {   "PANSCAN",                    "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_PANSCAN                    }, 0, 0, FLAGS, .unit = "type" }, \
    {   "A53_CC",                     "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_A53_CC                     }, 0, 0, FLAGS, .unit = "type" }, \
    {   "STEREO3D",                   "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_STEREO3D                   }, 0, 0, FLAGS, .unit = "type" }, \
    {   "MATRIXENCODING",             "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_MATRIXENCODING             }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DOWNMIX_INFO",               "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DOWNMIX_INFO               }, 0, 0, FLAGS, .unit = "type" }, \
    {   "REPLAYGAIN",                 "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_REPLAYGAIN                 }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DISPLAYMATRIX",              "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DISPLAYMATRIX              }, 0, 0, FLAGS, .unit = "type" }, \
    {   "AFD",                        "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_AFD                        }, 0, 0, FLAGS, .unit = "type" }, \
    {   "MOTION_VECTORS",             "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_MOTION_VECTORS             }, 0, 0, FLAGS, .unit = "type" }, \
    {   "SKIP_SAMPLES",               "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_SKIP_SAMPLES               }, 0, 0, FLAGS, .unit = "type" }, \
    {   "AUDIO_SERVICE_TYPE",         "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_AUDIO_SERVICE_TYPE         }, 0, 0, FLAGS, .unit = "type" }, \
    {   "MASTERING_DISPLAY_METADATA", "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_MASTERING_DISPLAY_METADATA }, 0, 0, FLAGS, .unit = "type" }, \
    {   "GOP_TIMECODE",               "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_GOP_TIMECODE               }, 0, 0, FLAGS, .unit = "type" }, \
    {   "SPHERICAL",                  "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_SPHERICAL                  }, 0, 0, FLAGS, .unit = "type" }, \
    {   "CONTENT_LIGHT_LEVEL",        "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_CONTENT_LIGHT_LEVEL        }, 0, 0, FLAGS, .unit = "type" }, \
    {   "ICC_PROFILE",                "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_ICC_PROFILE                }, 0, 0, FLAGS, .unit = "type" }, \
    {   "S12M_TIMECOD",               "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_S12M_TIMECODE              }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DYNAMIC_HDR_PLUS",           "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DYNAMIC_HDR_PLUS           }, 0, 0, FLAGS, .unit = "type" }, \
    {   "REGIONS_OF_INTEREST",        "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_REGIONS_OF_INTEREST        }, 0, 0, FLAGS, .unit = "type" }, \
    {   "VIDEO_ENC_PARAMS",           "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_VIDEO_ENC_PARAMS           }, 0, 0, FLAGS, .unit = "type" }, \
    {   "SEI_UNREGISTERED",           "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_SEI_UNREGISTERED           }, 0, 0, FLAGS, .unit = "type" }, \
    {   "FILM_GRAIN_PARAMS",          "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_FILM_GRAIN_PARAMS          }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DETECTION_BOUNDING_BOXES",   "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DETECTION_BBOXES           }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DETECTION_BBOXES",           "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DETECTION_BBOXES           }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DOVI_RPU_BUFFER",            "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DOVI_RPU_BUFFER            }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DOVI_METADATA",              "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DOVI_METADATA              }, 0, 0, FLAGS, .unit = "type" }, \
    {   "DYNAMIC_HDR_VIVID",          "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_DYNAMIC_HDR_VIVID          }, 0, 0, FLAGS, .unit = "type" }, \
    {   "AMBIENT_VIEWING_ENVIRONMENT","", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT}, 0, 0, FLAGS, .unit = "type" }, \
    {   "VIDEO_HINT",                 "", 0,             AV_OPT_TYPE_CONST,  {.i64 = AV_FRAME_DATA_VIDEO_HINT                 }, 0, 0, FLAGS, .unit = "type" }, \
    { NULL } \
}

static av_cold int init(AVFilterContext *ctx)
{
    SideDataContext *s = ctx->priv;

    if (s->type == -1 && s->mode != SIDEDATA_DELETE) {
        av_log(ctx, AV_LOG_ERROR, "Side data type must be set\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    SideDataContext *s = ctx->priv;
    AVFrameSideData *sd = NULL;

    if (s->type != -1)
       sd = av_frame_get_side_data(frame, s->type);

    switch (s->mode) {
    case SIDEDATA_SELECT:
        if (sd) {
            return ff_filter_frame(outlink, frame);
        }
        break;
    case SIDEDATA_DELETE:
        if (s->type == -1) {
            while (frame->nb_side_data)
                av_frame_remove_side_data(frame, frame->side_data[0]->type);
        } else if (sd) {
            av_frame_remove_side_data(frame, s->type);
        }
        return ff_filter_frame(outlink, frame);
        break;
    default:
        av_assert0(0);
    };

    av_frame_free(&frame);

    return 0;
}

#if CONFIG_ASIDEDATA_FILTER

DEFINE_OPTIONS(asidedata, AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(asidedata);

static const AVFilterPad ainputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_af_asidedata = {
    .name          = "asidedata",
    .description   = NULL_IF_CONFIG_SMALL("Manipulate audio frame side data."),
    .priv_size     = sizeof(SideDataContext),
    .priv_class    = &asidedata_class,
    .init          = init,
    FILTER_INPUTS(ainputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                     AVFILTER_FLAG_METADATA_ONLY,
};
#endif /* CONFIG_ASIDEDATA_FILTER */

#if CONFIG_SIDEDATA_FILTER

DEFINE_OPTIONS(sidedata, AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(sidedata);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_sidedata = {
    .name        = "sidedata",
    .description = NULL_IF_CONFIG_SMALL("Manipulate video frame side data."),
    .priv_size   = sizeof(SideDataContext),
    .priv_class  = &sidedata_class,
    .init        = init,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    .flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                   AVFILTER_FLAG_METADATA_ONLY,
};
#endif /* CONFIG_SIDEDATA_FILTER */
