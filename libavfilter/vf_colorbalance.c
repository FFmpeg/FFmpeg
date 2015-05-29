/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

typedef struct {
    double shadows;
    double midtones;
    double highlights;
} Range;

typedef struct {
    const AVClass *class;
    Range cyan_red;
    Range magenta_green;
    Range yellow_blue;

    uint8_t lut[3][256];

    uint8_t rgba_map[4];
    int step;
} ColorBalanceContext;

#define OFFSET(x) offsetof(ColorBalanceContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption colorbalance_options[] = {
    { "rs", "set red shadows",      OFFSET(cyan_red.shadows),         AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "gs", "set green shadows",    OFFSET(magenta_green.shadows),    AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "bs", "set blue shadows",     OFFSET(yellow_blue.shadows),      AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "rm", "set red midtones",     OFFSET(cyan_red.midtones),        AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "gm", "set green midtones",   OFFSET(magenta_green.midtones),   AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "bm", "set blue midtones",    OFFSET(yellow_blue.midtones),     AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "rh", "set red highlights",   OFFSET(cyan_red.highlights),      AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "gh", "set green highlights", OFFSET(magenta_green.highlights), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { "bh", "set blue highlights",  OFFSET(yellow_blue.highlights),   AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorbalance);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ABGR,  AV_PIX_FMT_ARGB,
        AV_PIX_FMT_0BGR,  AV_PIX_FMT_0RGB,
        AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ColorBalanceContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    double *shadows, *midtones, *highlights, *buffer;
    int i, r, g, b;

    buffer = av_malloc(256 * 3 * sizeof(*buffer));
    if (!buffer)
        return AVERROR(ENOMEM);

    shadows    = buffer + 256 * 0;
    midtones   = buffer + 256 * 1;
    highlights = buffer + 256 * 2;

    for (i = 0; i < 256; i++) {
        double low = av_clipd((i - 85.0) / -64.0 + 0.5, 0, 1) * 178.5;
        double mid = av_clipd((i - 85.0) /  64.0 + 0.5, 0, 1) *
                     av_clipd((i + 85.0 - 255.0) / -64.0 + 0.5, 0, 1) * 178.5;

        shadows[i] = low;
        midtones[i] = mid;
        highlights[255 - i] = low;
    }

    for (i = 0; i < 256; i++) {
        r = g = b = i;

        r = av_clip_uint8(r + s->cyan_red.shadows         * shadows[r]);
        r = av_clip_uint8(r + s->cyan_red.midtones        * midtones[r]);
        r = av_clip_uint8(r + s->cyan_red.highlights      * highlights[r]);

        g = av_clip_uint8(g + s->magenta_green.shadows    * shadows[g]);
        g = av_clip_uint8(g + s->magenta_green.midtones   * midtones[g]);
        g = av_clip_uint8(g + s->magenta_green.highlights * highlights[g]);

        b = av_clip_uint8(b + s->yellow_blue.shadows      * shadows[b]);
        b = av_clip_uint8(b + s->yellow_blue.midtones     * midtones[b]);
        b = av_clip_uint8(b + s->yellow_blue.highlights   * highlights[b]);

        s->lut[R][i] = r;
        s->lut[G][i] = g;
        s->lut[B][i] = b;
    }

    av_free(buffer);

    ff_fill_rgba_map(s->rgba_map, outlink->format);
    s->step = av_get_padded_bits_per_pixel(desc) >> 3;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorBalanceContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
    const int step = s->step;
    const uint8_t *srcrow = in->data[0];
    uint8_t *dstrow;
    AVFrame *out;
    int i, j;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    dstrow = out->data[0];
    for (i = 0; i < outlink->h; i++) {
        const uint8_t *src = srcrow;
        uint8_t *dst = dstrow;

        for (j = 0; j < outlink->w * step; j += step) {
            dst[j + roffset] = s->lut[R][src[j + roffset]];
            dst[j + goffset] = s->lut[G][src[j + goffset]];
            dst[j + boffset] = s->lut[B][src[j + boffset]];
            if (in != out && step == 4)
                dst[j + aoffset] = src[j + aoffset];
        }

        srcrow += in->linesize[0];
        dstrow += out->linesize[0];
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(ctx->outputs[0], out);
}

static const AVFilterPad colorbalance_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad colorbalance_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_colorbalance = {
    .name          = "colorbalance",
    .description   = NULL_IF_CONFIG_SMALL("Adjust the color balance."),
    .priv_size     = sizeof(ColorBalanceContext),
    .priv_class    = &colorbalance_class,
    .query_formats = query_formats,
    .inputs        = colorbalance_inputs,
    .outputs       = colorbalance_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
