/*
 * Copyright (c) 2012 Stefano Sabatini
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

#include "libavutil/pixfmt.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

enum SetFieldMode {
    MODE_AUTO = -1,
    MODE_BFF,
    MODE_TFF,
    MODE_PROG,
};

typedef struct SetParamsContext {
    const AVClass *class;
    int field_mode;
    int color_range;
    int color_primaries;
    int color_trc;
    int colorspace;
} SetParamsContext;

#define OFFSET(x) offsetof(SetParamsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption setparams_options[] = {
    {"field_mode", "select interlace mode", OFFSET(field_mode), AV_OPT_TYPE_INT, {.i64=MODE_AUTO}, -1, MODE_PROG, FLAGS, "mode"},
    {"auto", "keep the same input field",  0, AV_OPT_TYPE_CONST, {.i64=MODE_AUTO}, INT_MIN, INT_MAX, FLAGS, "mode"},
    {"bff",  "mark as bottom-field-first", 0, AV_OPT_TYPE_CONST, {.i64=MODE_BFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"tff",  "mark as top-field-first",    0, AV_OPT_TYPE_CONST, {.i64=MODE_TFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"prog", "mark as progressive",        0, AV_OPT_TYPE_CONST, {.i64=MODE_PROG}, INT_MIN, INT_MAX, FLAGS, "mode"},

    {"range", "select color range", OFFSET(color_range), AV_OPT_TYPE_INT, {.i64=-1},-1, AVCOL_RANGE_NB-1, FLAGS, "range"},
    {"auto",  "keep the same color range",   0, AV_OPT_TYPE_CONST, {.i64=-1},                       0, 0, FLAGS, "range"},
    {"unspecified",                  NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, FLAGS, "range"},
    {"unknown",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, FLAGS, "range"},
    {"limited",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"tv",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"mpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"full",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},
    {"pc",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},
    {"jpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},

    {"color_primaries", "select color primaries", OFFSET(color_primaries), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_PRI_NB-1, FLAGS, "color_primaries"},
    {"auto", "keep the same color primaries",  0, AV_OPT_TYPE_CONST, {.i64=-1},                     INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"bt709",                           NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT709},        INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"unknown",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_UNSPECIFIED},  INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"bt470m",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470M},       INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"bt470bg",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470BG},      INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"smpte170m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE170M},    INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"smpte240m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE240M},    INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"film",                            NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_FILM},         INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"bt2020",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT2020},       INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"smpte428",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE428},     INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"smpte431",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE431},     INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"smpte432",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE432},     INT_MIN, INT_MAX, FLAGS, "color_primaries"},
    {"jedec-p22",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_JEDEC_P22},    INT_MIN, INT_MAX, FLAGS, "color_primaries"},

    {"color_trc", "select color transfer", OFFSET(color_trc), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_TRC_NB-1, FLAGS, "color_trc"},
    {"auto", "keep the same color transfer",  0, AV_OPT_TYPE_CONST, {.i64=-1},                     INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt709",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT709},        INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"unknown",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_UNSPECIFIED},  INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt470m",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA22},      INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt470bg",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA28},      INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"smpte170m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE170M},    INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"smpte240m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE240M},    INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"linear",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_LINEAR},       INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"log100",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_LOG},          INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"log316",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_LOG_SQRT},     INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"iec61966-2-4",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_4}, INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt1361e",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT1361_ECG},   INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"iec61966-2-1",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_1}, INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt2020-10",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_10},    INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"bt2020-12",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_12},    INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"smpte2084",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE2084},    INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"smpte428",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE428},     INT_MIN, INT_MAX, FLAGS, "color_trc"},
    {"arib-std-b67",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_ARIB_STD_B67}, INT_MIN, INT_MAX, FLAGS, "color_trc"},

    {"colorspace", "select colorspace", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_SPC_NB-1, FLAGS, "colorspace"},
    {"auto", "keep the same colorspace",  0, AV_OPT_TYPE_CONST, {.i64=-1},                          INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"gbr",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_RGB},               INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"bt709",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT709},             INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"unknown",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_UNSPECIFIED},       INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"fcc",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_FCC},               INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"bt470bg",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT470BG},           INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"smpte170m",                  NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_SMPTE170M},         INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"smpte240m",                  NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_SMPTE240M},         INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"ycgco",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_YCGCO},             INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"bt2020nc",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT2020_NCL},        INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"bt2020c",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT2020_CL},         INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"smpte2085",                  NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_SMPTE2085},         INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"chroma-derived-nc",          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_CHROMA_DERIVED_NCL},INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"chroma-derived-c",           NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_CHROMA_DERIVED_CL}, INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {"ictcp",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_ICTCP},             INT_MIN, INT_MAX, FLAGS, "colorspace"},
    {NULL}
};

AVFILTER_DEFINE_CLASS(setparams);

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    SetParamsContext *s = ctx->priv;

    /* set field */
    if (s->field_mode == MODE_PROG) {
        frame->interlaced_frame = 0;
    } else if (s->field_mode != MODE_AUTO) {
        frame->interlaced_frame = 1;
        frame->top_field_first = s->field_mode;
    }

    /* set range */
    if (s->color_range >= 0)
        frame->color_range = s->color_range;

    /* set color prim, trc, space */
    if (s->color_primaries >= 0)
        frame->color_primaries = s->color_primaries;
    if (s->color_trc >= 0)
        frame->color_trc = s->color_trc;
    if (s->colorspace >= 0)
        frame->colorspace = s->colorspace;
    return ff_filter_frame(ctx->outputs[0], frame);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_setparams = {
    .name        = "setparams",
    .description = NULL_IF_CONFIG_SMALL("Force field, or color property for the output video frame."),
    .priv_size   = sizeof(SetParamsContext),
    .priv_class  = &setparams_class,
    .inputs      = inputs,
    .outputs     = outputs,
};

#if CONFIG_SETRANGE_FILTER

static const AVOption setrange_options[] = {
    {"range", "select color range", OFFSET(color_range), AV_OPT_TYPE_INT, {.i64=-1},-1, AVCOL_RANGE_NB-1, FLAGS, "range"},
    {"auto",  "keep the same color range",   0, AV_OPT_TYPE_CONST, {.i64=-1},                       0, 0, FLAGS, "range"},
    {"unspecified",                  NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, FLAGS, "range"},
    {"unknown",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, FLAGS, "range"},
    {"limited",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"tv",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"mpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, FLAGS, "range"},
    {"full",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},
    {"pc",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},
    {"jpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, FLAGS, "range"},
    {NULL}
};

AVFILTER_DEFINE_CLASS(setrange);

static av_cold int init_setrange(AVFilterContext *ctx)
{
    SetParamsContext *s = ctx->priv;

    s->field_mode = MODE_AUTO;/* set field mode to auto */
    s->color_primaries = -1;
    s->color_trc       = -1;
    s->colorspace      = -1;
    return 0;
}

AVFilter ff_vf_setrange = {
    .name        = "setrange",
    .description = NULL_IF_CONFIG_SMALL("Force color range for the output video frame."),
    .priv_size   = sizeof(SetParamsContext),
    .init        = init_setrange,
    .priv_class  = &setrange_class,
    .inputs      = inputs,
    .outputs     = outputs,
};
#endif /* CONFIG_SETRANGE_FILTER */

#if CONFIG_SETFIELD_FILTER
static const AVOption setfield_options[] = {
    {"mode", "select interlace mode", OFFSET(field_mode), AV_OPT_TYPE_INT, {.i64=MODE_AUTO}, -1, MODE_PROG, FLAGS, "mode"},
    {"auto", "keep the same input field",  0, AV_OPT_TYPE_CONST, {.i64=MODE_AUTO}, INT_MIN, INT_MAX, FLAGS, "mode"},
    {"bff",  "mark as bottom-field-first", 0, AV_OPT_TYPE_CONST, {.i64=MODE_BFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"tff",  "mark as top-field-first",    0, AV_OPT_TYPE_CONST, {.i64=MODE_TFF},  INT_MIN, INT_MAX, FLAGS, "mode"},
    {"prog", "mark as progressive",        0, AV_OPT_TYPE_CONST, {.i64=MODE_PROG}, INT_MIN, INT_MAX, FLAGS, "mode"},
    {NULL}
};

AVFILTER_DEFINE_CLASS(setfield);

static av_cold int init_setfield(AVFilterContext *ctx)
{
    SetParamsContext *s = ctx->priv;

    s->color_range = -1;/* set range mode to auto */
    s->color_primaries = -1;
    s->color_trc       = -1;
    s->colorspace      = -1;
    return 0;
}

AVFilter ff_vf_setfield = {
    .name        = "setfield",
    .description = NULL_IF_CONFIG_SMALL("Force field for the output video frame."),
    .priv_size   = sizeof(SetParamsContext),
    .init        = init_setfield,
    .priv_class  = &setfield_class,
    .inputs      = inputs,
    .outputs     = outputs,
};
#endif /* CONFIG_SETFIELD_FILTER */
