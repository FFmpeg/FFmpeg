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

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "libavformat/avio.h"
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
    METADATAF_SAME_STR,
    METADATAF_STARTS_WITH,
    METADATAF_LESS,
    METADATAF_EQUAL,
    METADATAF_GREATER,
    METADATAF_EXPR,
    METADATAF_ENDS_WITH,
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
    int function;

    char *expr_str;
    AVExpr *expr;
    double var_values[VAR_VARS_NB];

    AVIOContext* avio_context;
    char *file_str;

    int (*compare)(struct MetadataContext *s,
                   const char *value1, const char *value2);
    void (*print)(AVFilterContext *ctx, const char *msg, ...) av_printf_format(2, 3);

    int direct;    // reduces buffering when printing to user-supplied URL
} MetadataContext;

#define OFFSET(x) offsetof(MetadataContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS) \
static const AVOption filt_name##_options[] = { \
    { "mode", "set a mode of operation", OFFSET(mode),   AV_OPT_TYPE_INT,    {.i64 = 0 }, 0, METADATA_NB-1, FLAGS, "mode" }, \
    {   "select", "select frame",        0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_SELECT }, 0, 0, FLAGS, "mode" }, \
    {   "add",    "add new metadata",    0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_ADD },    0, 0, FLAGS, "mode" }, \
    {   "modify", "modify metadata",     0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_MODIFY }, 0, 0, FLAGS, "mode" }, \
    {   "delete", "delete metadata",     0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_DELETE }, 0, 0, FLAGS, "mode" }, \
    {   "print",  "print metadata",      0,              AV_OPT_TYPE_CONST,  {.i64 = METADATA_PRINT },  0, 0, FLAGS, "mode" }, \
    { "key",   "set metadata key",       OFFSET(key),    AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "value", "set metadata value",     OFFSET(value),  AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "function", "function for comparing values", OFFSET(function), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, METADATAF_NB-1, FLAGS, "function" }, \
    {   "same_str",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_SAME_STR },    0, 3, FLAGS, "function" }, \
    {   "starts_with", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_STARTS_WITH }, 0, 0, FLAGS, "function" }, \
    {   "less",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_LESS    },     0, 3, FLAGS, "function" }, \
    {   "equal",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_EQUAL   },     0, 3, FLAGS, "function" }, \
    {   "greater",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_GREATER },     0, 3, FLAGS, "function" }, \
    {   "expr",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_EXPR    },     0, 3, FLAGS, "function" }, \
    {   "ends_with",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = METADATAF_ENDS_WITH },   0, 0, FLAGS, "function" }, \
    { "expr", "set expression for expr function", OFFSET(expr_str), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, FLAGS }, \
    { "file", "set file where to print metadata information", OFFSET(file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS }, \
    { "direct", "reduce buffering when printing to user-set file or pipe", OFFSET(direct), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS }, \
    { NULL } \
}

static int same_str(MetadataContext *s, const char *value1, const char *value2)
{
    return !strcmp(value1, value2);
}

static int starts_with(MetadataContext *s, const char *value1, const char *value2)
{
    return !strncmp(value1, value2, strlen(value2));
}

static int ends_with(MetadataContext *s, const char *value1, const char *value2)
{
    const int len1 = strlen(value1);
    const int len2 = strlen(value2);

    return !strncmp(value1 + FFMAX(len1 - len2, 0), value2, len2);
}

static int equal(MetadataContext *s, const char *value1, const char *value2)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return fabsf(f1 - f2) < FLT_EPSILON;
}

static int less(MetadataContext *s, const char *value1, const char *value2)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return (f1 - f2) < FLT_EPSILON;
}

static int greater(MetadataContext *s, const char *value1, const char *value2)
{
    float f1, f2;

    if (sscanf(value1, "%f", &f1) + sscanf(value2, "%f", &f2) != 2)
        return 0;

    return (f2 - f1) < FLT_EPSILON;
}

static int parse_expr(MetadataContext *s, const char *value1, const char *value2)
{
    double f1, f2;

    if (sscanf(value1, "%lf", &f1) + sscanf(value2, "%lf", &f2) != 2)
        return 0;

    s->var_values[VAR_VALUE1] = f1;
    s->var_values[VAR_VALUE2] = f2;

    return av_expr_eval(s->expr, s->var_values, NULL);
}

static void print_log(AVFilterContext *ctx, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);
    if (msg)
        av_vlog(ctx, AV_LOG_INFO, msg, argument_list);
    va_end(argument_list);
}

static void print_file(AVFilterContext *ctx, const char *msg, ...)
{
    MetadataContext *s = ctx->priv;
    va_list argument_list;

    va_start(argument_list, msg);
    if (msg) {
        char buf[128];
        vsnprintf(buf, sizeof(buf), msg, argument_list);
        avio_write(s->avio_context, buf, av_strnlen(buf, sizeof(buf)));
    }
    va_end(argument_list);
}

static av_cold int init(AVFilterContext *ctx)
{
    MetadataContext *s = ctx->priv;
    int ret;

    if (!s->key && s->mode != METADATA_PRINT && s->mode != METADATA_DELETE) {
        av_log(ctx, AV_LOG_WARNING, "Metadata key must be set\n");
        return AVERROR(EINVAL);
    }

    if ((s->mode == METADATA_MODIFY ||
        s->mode == METADATA_ADD) && !s->value) {
        av_log(ctx, AV_LOG_WARNING, "Missing metadata value\n");
        return AVERROR(EINVAL);
    }

    switch (s->function) {
    case METADATAF_SAME_STR:
        s->compare = same_str;
        break;
    case METADATAF_STARTS_WITH:
        s->compare = starts_with;
        break;
    case METADATAF_ENDS_WITH:
        s->compare = ends_with;
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

    if (s->mode == METADATA_PRINT && s->file_str) {
        s->print = print_file;
    } else {
        s->print = print_log;
    }

    s->avio_context = NULL;
    if (s->file_str) {
        if (!strcmp("-", s->file_str)) {
            ret = avio_open(&s->avio_context, "pipe:1", AVIO_FLAG_WRITE);
        } else {
            ret = avio_open(&s->avio_context, s->file_str, AVIO_FLAG_WRITE);
        }

        if (ret < 0) {
            char buf[128];
            av_strerror(ret, buf, sizeof(buf));
            av_log(ctx, AV_LOG_ERROR, "Could not open %s: %s\n",
                   s->file_str, buf);
            return ret;
        }

        if (s->direct)
            s->avio_context->direct = AVIO_FLAG_DIRECT;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MetadataContext *s = ctx->priv;

    av_expr_free(s->expr);
    s->expr = NULL;
    if (s->avio_context) {
        avio_closep(&s->avio_context);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    MetadataContext *s = ctx->priv;
    AVDictionary **metadata = &frame->metadata;
    AVDictionaryEntry *e;

    if (!*metadata && s->mode != METADATA_ADD)
        return ff_filter_frame(outlink, frame);

    e = av_dict_get(*metadata, !s->key ? "" : s->key, NULL,
                    !s->key ? AV_DICT_IGNORE_SUFFIX: 0);

    switch (s->mode) {
    case METADATA_SELECT:
        if (!s->value && e && e->value) {
            return ff_filter_frame(outlink, frame);
        } else if (s->value && e && e->value &&
                   s->compare(s, e->value, s->value)) {
            return ff_filter_frame(outlink, frame);
        }
        break;
    case METADATA_ADD:
        if (e && e->value) {
            ;
        } else {
            av_dict_set(metadata, s->key, s->value, 0);
        }
        return ff_filter_frame(outlink, frame);
    case METADATA_MODIFY:
        if (e && e->value) {
            av_dict_set(metadata, s->key, s->value, 0);
        }
        return ff_filter_frame(outlink, frame);
    case METADATA_PRINT:
        if (!s->key && e) {
            s->print(ctx, "frame:%-4"PRId64" pts:%-7s pts_time:%s\n",
                     inlink->frame_count_out, av_ts2str(frame->pts), av_ts2timestr(frame->pts, &inlink->time_base));
            s->print(ctx, "%s=%s\n", e->key, e->value);
            while ((e = av_dict_get(*metadata, "", e, AV_DICT_IGNORE_SUFFIX)) != NULL) {
                s->print(ctx, "%s=%s\n", e->key, e->value);
            }
        } else if (e && e->value && (!s->value || (e->value && s->compare(s, e->value, s->value)))) {
            s->print(ctx, "frame:%-4"PRId64" pts:%-7s pts_time:%s\n",
                     inlink->frame_count_out, av_ts2str(frame->pts), av_ts2timestr(frame->pts, &inlink->time_base));
            s->print(ctx, "%s=%s\n", s->key, e->value);
        }
        return ff_filter_frame(outlink, frame);
    case METADATA_DELETE:
        if (!s->key) {
            av_dict_free(metadata);
        } else if (e && e->value && (!s->value || s->compare(s, e->value, s->value))) {
            av_dict_set(metadata, s->key, NULL, 0);
        }
        return ff_filter_frame(outlink, frame);
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
    .uninit        = uninit,
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
    .uninit      = uninit,
    .inputs      = inputs,
    .outputs     = outputs,
    .flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
#endif /* CONFIG_METADATA_FILTER */
