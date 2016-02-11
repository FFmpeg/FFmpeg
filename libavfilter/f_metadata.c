/*
 * Copyright (c) 2016 Paul B Mahol
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

/**
 * @file
 * filter for manipulating frame metadata
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixelutils.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum MetadataMode {
    METADATA_SELECT,
    METADATA_ADD,
    METADATA_MODIFY,
    METADATA_DELETE,
    METADATA_PRINT,
    METADATA_NB
};

enum MetadataFunction {
    METADATAF_STRING,
    METADATAF_LESS,
    METADATAF_EQUAL,
    METADATAF_GREATER,
    METADATAF_EXPR,
    METADATAF_NB
};

static const char *const var_names[] = {
    "VALUE1",
    "VALUE2",
    NULL
};

enum var_name {
    VAR_VALUE1,
    VAR_VALUE2,
    VAR_VARS_NB
};

typedef struct MetadataContext {
    const AVClass *class;

    int mode;
    char *key;
    char *value;
    int length;
    int function;

    char *expr_str;
    AVExpr *expr;
    double var_values[VAR_VARS_NB];

    int (*compare)(struct MetadataContext *s,
                   const char *value1, const char *value2, size_t length);
} MetadataContext;

#define OFFSET(x) offsetof(MetadataContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS)                            \
static const AVOption filt_name##_options[] = {                     \
    { "mode", "set a mode of operation", OFFSET(mode),   AV_OPT_TYPE_INT,    {.i64 = 0 }, 0, METADATA_NB-1, FLAGS, "mode" }, \
    {   "select", "select frame",        0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_SELECT }, 0, 0, FLAGS, "mode" }, \
    {   "add",    "add new metadata",    0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_ADD },    0, 0, FLAGS, "mode" }, \
    {   "modify", "modify metadata",     0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_MODIFY }, 0, 0, FLAGS, "mode" }, \
    {   "delete", "delete metadata",     0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_DELETE }, 0, 0, FLAGS, "mode" }, \
    {   "print",  "print metadata",      0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_PRINT },  0, 0, FLAGS, "mode" }, \
    { "key",   "set metadata key",       OFFSET(key),    AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "value", "set metadata value",     OFFSET(value),  AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "function", "function for comparing values", OFFSET(function), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, METADATAF_NB-1, FLAGS, "function" }, \
    {   "string",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_STRING  }, 0, 3, FLAGS, "function" }, \
    {   "less",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_LESS    }, 0, 3, FLAGS, "function" }, \
    {   "equal",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_EQUAL   }, 0, 3, FLAGS, "function" }, \
    {   "greater", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_GREATER }, 0, 3, FLAGS, "function" }, \
    {   "expr",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_EXPR    }, 0, 3, FLAGS, "function" }, \
    { "expr", "set expression for expr function", OFFSET(expr_str), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "length", "compare up to N chars for string function", OFFSET(length), AV_OPT_TYPE_INT,    {.i64 = INT_MAX }, 1, INT_MAX, FLAGS }, \
    { NULL }                                                            \
}

static int string(MetadataContext *s, const char *value1, const char *value2, size_t length)
{
    return !strncmp(value1, value2, length);
}

static int equal(MetadataContext *s, const char *value1, const char *value2, size_t length)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return f1 == f2;
}

static int less(MetadataContext *s, const char *value1, const char *value2, size_t length)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return f1 < f2;
}

static int greater(MetadataContext *s, const char *value1, const char *value2, size_t length)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return f1 > f2;
}

static int parse_expr(MetadataContext *s, const char *value1, const char *value2, size_t length)
{
    double f1, f2;

    if (sscanf(value1, "%lf", &f1) + sscanf(value2, "%lf", &f2) != 2)
        return 0;

    s->var_values[VAR_VALUE1] = f1;
    s->var_values[VAR_VALUE2] = f2;

    return av_expr_eval(s->expr, s->var_values, NULL);
}

static av_cold int init(AVFilterContext *ctx)
{
    MetadataContext *s = ctx->priv;
    int ret;

    if (!s->key && s->mode != METADATA_PRINT) {
        av_log(ctx, AV_LOG_WARNING, "Metadata key must be set\n");
        return AVERROR(EINVAL);
    }

    if ((s->mode == METADATA_MODIFY ||
        s->mode == METADATA_ADD) && !s->value) {
        av_log(ctx, AV_LOG_WARNING, "Missing metadata value\n");
        return AVERROR(EINVAL);
    }

    switch (s->function) {
    case METADATAF_STRING:
        s->compare = string;
        break;
    case METADATAF_LESS:
        s->compare = less;
        break;
    case METADATAF_EQUAL:
        s->compare = equal;
        break;
    case METADATAF_GREATER:
        s->compare = greater;
        break;
    case METADATAF_EXPR:
        s->compare = parse_expr;
        break;
    default:
        av_assert0(0);
    };

    if (s->function == METADATAF_EXPR) {
        if (!s->expr_str) {
            av_log(ctx, AV_LOG_WARNING, "expr option not set\n");
            return AVERROR(EINVAL);
        }
        if ((ret = av_expr_parse(&s->expr, s->expr_str,
                                 var_names, NULL, NULL, NULL, NULL, 0, ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error while parsing expression '%s'\n", s->expr_str);
            return ret;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    MetadataContext *s = ctx->priv;
    AVDictionary *metadata = av_frame_get_metadata(frame);
    AVDictionaryEntry *e;

    if (!metadata)
        return ff_filter_frame(outlink, frame);

    e = av_dict_get(metadata, !s->key ? "" : s->key, NULL,
                    !s->key ? AV_DICT_IGNORE_SUFFIX: 0);

    switch (s->mode) {
    case METADATA_SELECT:
        if (!s->value && e && e->value) {
            return ff_filter_frame(outlink, frame);
        } else if (s->value && e && e->value &&
                   s->compare(s, e->value, s->value, s->length)) {
            return ff_filter_frame(outlink, frame);
        }
        break;
    case METADATA_ADD:
        if (e && e->value) {
            ;
        } else {
            av_dict_set(&metadata, s->key, s->value, 0);
        }
        return ff_filter_frame(outlink, frame);
        break;
    case METADATA_MODIFY:
        if (e && e->value) {
            av_dict_set(&metadata, s->key, s->value, 0);
        }
        return ff_filter_frame(outlink, frame);
        break;
    case METADATA_PRINT:
        if (!s->key && e) {
            av_log(ctx, AV_LOG_INFO, "frame %"PRId64" pts %"PRId64"\n", inlink->frame_count, frame->pts);
            av_log(ctx, AV_LOG_INFO, "%s=%s\n", e->key, e->value);
            while ((e = av_dict_get(metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
                av_log(ctx, AV_LOG_INFO, "%s=%s\n", e->key, e->value);
            }
        } else if (e && e->value && (!s->value || (e->value && s->compare(s, e->value, s->value, s->length)))) {
            av_log(ctx, AV_LOG_INFO, "frame %"PRId64" pts %"PRId64"\n", inlink->frame_count, frame->pts);
            av_log(ctx, AV_LOG_INFO, "%s=%s\n", s->key, e->value);
        }
        return ff_filter_frame(outlink, frame);
        break;
    case METADATA_DELETE:
        if (e && e->value && s->value && s->compare(s, e->value, s->value, s->length)) {
            av_dict_set(&metadata, s->key, NULL, 0);
        } else if (e && e->value) {
            av_dict_set(&metadata, s->key, NULL, 0);
        }
        return ff_filter_frame(outlink, frame);
        break;
    default:
        av_assert0(0);
    };

    av_frame_free(&frame);

    return 0;
}

#if CONFIG_AMETADATA_FILTER

DEFINE_OPTIONS(ametadata, AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(ametadata);

static const AVFilterPad ainputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aoutputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_ametadata = {
    .name          = "ametadata",
    .description   = NULL_IF_CONFIG_SMALL("Manipulate audio frame metadata."),
    .priv_size     = sizeof(MetadataContext),
    .priv_class    = &ametadata_class,
    .init          = init,
    .query_formats = ff_query_formats_all,
    .inputs        = ainputs,
    .outputs       = aoutputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
#endif /* CONFIG_AMETADATA_FILTER */

#if CONFIG_METADATA_FILTER

DEFINE_OPTIONS(metadata, AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM);
AVFILTER_DEFINE_CLASS(metadata);

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

AVFilter ff_vf_metadata = {
    .name        = "metadata",
    .description = NULL_IF_CONFIG_SMALL("Manipulate video frame metadata."),
    .priv_size   = sizeof(MetadataContext),
    .priv_class  = &metadata_class,
    .init        = init,
    .inputs      = inputs,
    .outputs     = outputs,
    .flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
#endif /* CONFIG_METADATA_FILTER */
