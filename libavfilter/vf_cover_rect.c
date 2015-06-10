/*
 * Copyright (c) 2014-2015 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @todo switch to dualinput
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "internal.h"

#include "lavfutils.h"

enum mode {
    MODE_COVER,
    MODE_BLUR,
    NB_MODES
};

typedef struct CoverContext {
    AVClass *class;
    int mode;
    char *cover_filename;
    AVFrame *cover_frame;
    int width, height;
} CoverContext;

#define OFFSET(x) offsetof(CoverContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption cover_rect_options[] = {
    { "cover",  "cover bitmap filename",  OFFSET(cover_filename),  AV_OPT_TYPE_STRING, {.str = NULL}, .flags = FLAGS },
    { "mode", "set removal mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_BLUR}, 0, NB_MODES - 1, FLAGS, "mode" },
        { "cover", "cover area with bitmap", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_COVER}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "blur", "blur area", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_BLUR}, INT_MIN, INT_MAX, FLAGS, "mode" },
    { NULL }
};

static const AVClass cover_rect_class = {
    .class_name       = "cover_rect",
    .item_name        = av_default_item_name,
    .option           = cover_rect_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
};

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_NONE
    };

    return ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
}

static int config_input(AVFilterLink *inlink)
{
    return 0;
}

static void cover_rect(CoverContext *cover, AVFrame *in, int offx, int offy)
{
    int x, y, p;

    for (p = 0; p < 3; p++) {
        uint8_t *data = in->data[p] + (offx>>!!p) + (offy>>!!p) * in->linesize[p];
        const uint8_t *src = cover->cover_frame->data[p];
        int w = FF_CEIL_RSHIFT(cover->cover_frame->width , !!p);
        int h = FF_CEIL_RSHIFT(cover->cover_frame->height, !!p);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                data[x] = src[x];
            }
            data += in->linesize[p];
            src += cover->cover_frame->linesize[p];
        }
    }
}
static void blur(CoverContext *cover, AVFrame *in, int offx, int offy)
{
    int x, y, p;

    for (p=0; p<3; p++) {
        int ox = offx>>!!p;
        int oy = offy>>!!p;
        int stride = in->linesize[p];
        uint8_t *data = in->data[p] + ox + oy * stride;
        int w = FF_CEIL_RSHIFT(cover->width , !!p);
        int h = FF_CEIL_RSHIFT(cover->height, !!p);
        int iw = FF_CEIL_RSHIFT(in->width , !!p);
        int ih = FF_CEIL_RSHIFT(in->height, !!p);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                int c = 0;
                int s = 0;
                if (ox) {
                    int scale = 65536 / (x + 1);
                    s += data[-1 + y*stride] * scale;
                    c += scale;
                }
                if (oy) {
                    int scale = 65536 / (y + 1);
                    s += data[x - stride] * scale;
                    c += scale;
                }
                if (ox + w < iw) {
                    int scale = 65536 / (w - x);
                    s += data[w + y*stride] * scale;
                    c += scale;
                }
                if (oy + h < ih) {
                    int scale = 65536 / (h - y);
                    s += data[x + h*stride] * scale;
                    c += scale;
                }
                data[x + y*stride] = c ? (s + (c>>1)) / c : 0;
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    CoverContext *cover = ctx->priv;
    AVDictionaryEntry *ex, *ey, *ew, *eh;
    int x = -1, y = -1, w = -1, h = -1;
    char *xendptr = NULL, *yendptr = NULL, *wendptr = NULL, *hendptr = NULL;

    ex = av_dict_get(in->metadata, "lavfi.rect.x", NULL, AV_DICT_MATCH_CASE);
    ey = av_dict_get(in->metadata, "lavfi.rect.y", NULL, AV_DICT_MATCH_CASE);
    ew = av_dict_get(in->metadata, "lavfi.rect.w", NULL, AV_DICT_MATCH_CASE);
    eh = av_dict_get(in->metadata, "lavfi.rect.h", NULL, AV_DICT_MATCH_CASE);
    if (ex && ey && ew && eh) {
        x = strtol(ex->value, &xendptr, 10);
        y = strtol(ey->value, &yendptr, 10);
        w = strtol(ew->value, &wendptr, 10);
        h = strtol(eh->value, &hendptr, 10);
    }

    if (!xendptr || *xendptr || !yendptr || *yendptr ||
        !wendptr || *wendptr || !hendptr || !hendptr
    ) {
        return ff_filter_frame(ctx->outputs[0], in);
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    w = FFMIN(w, in->width  - x);
    h = FFMIN(h, in->height - y);

    if (w > in->width || h > in->height || w <= 0 || h <= 0)
        return AVERROR(EINVAL);

    if (cover->cover_frame) {
        if (w != cover->cover_frame->width || h != cover->cover_frame->height)
            return AVERROR(EINVAL);
    }

    cover->width  = w;
    cover->height = h;

    x = av_clip(x, 0, in->width  - w);
    y = av_clip(y, 0, in->height - h);

    av_frame_make_writable(in);

    if (cover->mode == MODE_BLUR) {
        blur (cover, in, x, y);
    } else {
        cover_rect(cover, in, x, y);
    }
    return ff_filter_frame(ctx->outputs[0], in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CoverContext *cover = ctx->priv;

    if (cover->cover_frame)
        av_freep(&cover->cover_frame->data[0]);
}

static av_cold int init(AVFilterContext *ctx)
{
    CoverContext *cover = ctx->priv;
    int ret;

    if (cover->mode == MODE_COVER) {
        if (!cover->cover_filename) {
            av_log(ctx, AV_LOG_ERROR, "cover filename not set\n");
            return AVERROR(EINVAL);
        }

        cover->cover_frame = av_frame_alloc();
        if (!cover->cover_frame)
            return AVERROR(ENOMEM);

        if ((ret = ff_load_image(cover->cover_frame->data, cover->cover_frame->linesize,
                                &cover->cover_frame->width, &cover->cover_frame->height,
                                &cover->cover_frame->format, cover->cover_filename, ctx)) < 0)
            return ret;

        if (cover->cover_frame->format != AV_PIX_FMT_YUV420P && cover->cover_frame->format != AV_PIX_FMT_YUVJ420P) {
            av_log(ctx, AV_LOG_ERROR, "cover image is not a YUV420 image\n");
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static const AVFilterPad cover_rect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad cover_rect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_cover_rect = {
    .name            = "cover_rect",
    .description     = NULL_IF_CONFIG_SMALL("Find and cover a user specified object"),
    .priv_size       = sizeof(CoverContext),
    .init            = init,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .inputs          = cover_rect_inputs,
    .outputs         = cover_rect_outputs,
    .priv_class      = &cover_rect_class,
};
