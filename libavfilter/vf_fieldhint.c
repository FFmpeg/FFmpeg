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

#include "libavutil/avassert.h"
#include "libavutil/file_open.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

enum HintModes {
    ABSOLUTE_HINT,
    RELATIVE_HINT,
    PATTERN_HINT,
    NB_HINTS,
};

typedef struct FieldHintContext {
    const AVClass *class;

    char *hint_file_str;
    FILE *hint;
    int mode;

    AVFrame *frame[3];

    int64_t line;
    int nb_planes;
    int eof;
    int planewidth[4];
    int planeheight[4];
} FieldHintContext;

#define OFFSET(x) offsetof(FieldHintContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fieldhint_options[] = {
    { "hint", "set hint file", OFFSET(hint_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "mode", "set hint mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=0}, 0, NB_HINTS-1, FLAGS, .unit = "mode" },
    {   "absolute", 0, 0, AV_OPT_TYPE_CONST, {.i64=ABSOLUTE_HINT}, 0, 0, FLAGS, .unit = "mode" },
    {   "relative", 0, 0, AV_OPT_TYPE_CONST, {.i64=RELATIVE_HINT}, 0, 0, FLAGS, .unit = "mode" },
    {   "pattern",  0, 0, AV_OPT_TYPE_CONST, {.i64=PATTERN_HINT},  0, 0, FLAGS, .unit = "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fieldhint);

static av_cold int init(AVFilterContext *ctx)
{
    FieldHintContext *s = ctx->priv;
    int ret;

    if (!s->hint_file_str) {
        av_log(ctx, AV_LOG_ERROR, "Hint file must be set.\n");
        return AVERROR(EINVAL);
    }
    s->hint = avpriv_fopen_utf8(s->hint_file_str, "r");
    if (!s->hint) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "%s: %s\n", s->hint_file_str, av_err2str(ret));
        return ret;
    }

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    int reject_flags = AV_PIX_FMT_FLAG_BITSTREAM |
                       AV_PIX_FMT_FLAG_HWACCEL   |
                       AV_PIX_FMT_FLAG_PAL;

    return ff_set_common_formats2(ctx, cfg_in, cfg_out,
                                  ff_formats_pixdesc_filter(0, reject_flags));
}

static int config_input(AVFilterLink *inlink)
{
    FieldHintContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->planewidth, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink *outl = ff_filter_link(outlink);
    FieldHintContext *s = ctx->priv;
    AVFrame *out, *top, *bottom;
    char buf[1024] = { 0 };
    int64_t tf, bf;
    int tfactor = 0, bfactor = 1;
    char hint = '=', field = '=';
    int p;

    av_frame_free(&s->frame[0]);
    s->frame[0] = s->frame[1];
    s->frame[1] = s->frame[2];
    s->frame[2] = in;
    if (!s->frame[1])
        return 0;
    else if (!s->frame[0]) {
        s->frame[0] = av_frame_clone(s->frame[1]);
        if (!s->frame[0])
            return AVERROR(ENOMEM);
    }

    while (1) {
        if (fgets(buf, sizeof(buf)-1, s->hint)) {
            s->line++;
            if (buf[0] == '#' || buf[0] == ';') {
                continue;
            } else if (sscanf(buf, "%"PRId64",%"PRId64" %c %c", &tf, &bf, &hint, &field) == 4) {
                ;
            } else if (sscanf(buf, "%"PRId64",%"PRId64" %c", &tf, &bf, &hint) == 3) {
                ;
            } else if (sscanf(buf, "%"PRId64",%"PRId64"", &tf, &bf) == 2) {
                ;
            } else {
                av_log(ctx, AV_LOG_ERROR, "Invalid entry at line %"PRId64".\n", s->line);
                return AVERROR_INVALIDDATA;
            }
            switch (s->mode) {
            case ABSOLUTE_HINT:
                if (tf > outl->frame_count_in + 1 || tf < FFMAX(0, outl->frame_count_in - 1) ||
                    bf > outl->frame_count_in + 1 || bf < FFMAX(0, outl->frame_count_in - 1)) {
                    av_log(ctx, AV_LOG_ERROR, "Out of range frames %"PRId64" and/or %"PRId64" on line %"PRId64" for %"PRId64". input frame.\n", tf, bf, s->line, inl->frame_count_out);
                    return AVERROR_INVALIDDATA;
                }
                break;
            case PATTERN_HINT:
            case RELATIVE_HINT:
                if (tf > 1 || tf < -1 ||
                    bf > 1 || bf < -1) {
                    av_log(ctx, AV_LOG_ERROR, "Out of range %"PRId64" and/or %"PRId64" on line %"PRId64" for %"PRId64". input frame.\n", tf, bf, s->line, inl->frame_count_out);
                    return AVERROR_INVALIDDATA;
                }
                break;
            default:
                return AVERROR_BUG;
            };
            break;
        } else {
            if (s->mode == PATTERN_HINT) {
                fseek(s->hint, 0, SEEK_SET);
                continue;
            }
            av_log(ctx, AV_LOG_ERROR, "Missing entry for %"PRId64". input frame.\n", inl->frame_count_out);
            return AVERROR_INVALIDDATA;
        }
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    av_frame_copy_props(out, s->frame[1]);

    switch (s->mode) {
    case ABSOLUTE_HINT:
        top    = s->frame[tf - outl->frame_count_in + 1];
        bottom = s->frame[bf - outl->frame_count_in + 1];
        break;
    case PATTERN_HINT:
    case RELATIVE_HINT:
        top    = s->frame[1 + tf];
        bottom = s->frame[1 + bf];
        break;
    default:
        av_assert0(0);
    }

    switch (field) {
    case 'b':
        tfactor = 1;
        top = bottom;
        break;
    case 't':
        bfactor = 0;
        bottom = top;
        break;
    case '=':
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Invalid field: %c.\n", field);
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    switch (hint) {
    case '+':
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        out->interlaced_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        out->flags |= AV_FRAME_FLAG_INTERLACED;
        break;
    case '-':
#if FF_API_INTERLACED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        out->interlaced_frame = 0;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        out->flags &= ~AV_FRAME_FLAG_INTERLACED;
        break;
    case '=':
        break;
    case 'b':
        tfactor = 1;
        top = bottom;
        break;
    case 't':
        bfactor = 0;
        bottom = top;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Invalid hint: %c.\n", hint);
        av_frame_free(&out);
        return AVERROR(EINVAL);
    }

    for (p = 0; p < s->nb_planes; p++) {
        av_image_copy_plane(out->data[p],
                            out->linesize[p] * 2,
                            top->data[p] + tfactor * top->linesize[p],
                            top->linesize[p] * 2,
                            s->planewidth[p],
                            (s->planeheight[p] + 1) / 2);
        av_image_copy_plane(out->data[p] + out->linesize[p],
                            out->linesize[p] * 2,
                            bottom->data[p] + bfactor * bottom->linesize[p],
                            bottom->linesize[p] * 2,
                            s->planewidth[p],
                            (s->planeheight[p] + 1) / 2);
    }

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FieldHintContext *s = ctx->priv;
    int ret;

    if (s->eof)
        return AVERROR_EOF;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->frame[2]) {
        AVFrame *next = av_frame_clone(s->frame[2]);
        if (!next)
            return AVERROR(ENOMEM);
        ret = filter_frame(ctx->inputs[0], next);
        s->eof = 1;
    }

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FieldHintContext *s = ctx->priv;

    if (s->hint)
        fclose(s->hint);
    s->hint = NULL;

    av_frame_free(&s->frame[0]);
    av_frame_free(&s->frame[1]);
    av_frame_free(&s->frame[2]);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_fieldhint = {
    .name          = "fieldhint",
    .description   = NULL_IF_CONFIG_SMALL("Field matching using hints."),
    .priv_size     = sizeof(FieldHintContext),
    .priv_class    = &fieldhint_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
