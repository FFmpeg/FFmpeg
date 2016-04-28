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
    const AVClass *class;
    double rr, rg, rb, ra;
    double gr, gg, gb, ga;
    double br, bg, bb, ba;
    double ar, ag, ab, aa;

    int *lut[4][4];

    int *buffer;

    uint8_t rgba_map[4];
} ColorChannelMixerContext;

#define OFFSET(x) offsetof(ColorChannelMixerContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption colorchannelmixer_options[] = {
    { "rr", "set the red gain for the red channel",     OFFSET(rr), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "rg", "set the green gain for the red channel",   OFFSET(rg), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "rb", "set the blue gain for the red channel",    OFFSET(rb), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ra", "set the alpha gain for the red channel",   OFFSET(ra), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "gr", "set the red gain for the green channel",   OFFSET(gr), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "gg", "set the green gain for the green channel", OFFSET(gg), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "gb", "set the blue gain for the green channel",  OFFSET(gb), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ga", "set the alpha gain for the green channel", OFFSET(ga), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "br", "set the red gain for the blue channel",    OFFSET(br), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "bg", "set the green gain for the blue channel",  OFFSET(bg), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "bb", "set the blue gain for the blue channel",   OFFSET(bb), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { "ba", "set the alpha gain for the blue channel",  OFFSET(ba), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ar", "set the red gain for the alpha channel",   OFFSET(ar), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ag", "set the green gain for the alpha channel", OFFSET(ag), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "ab", "set the blue gain for the alpha channel",  OFFSET(ab), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -2, 2, FLAGS },
    { "aa", "set the alpha gain for the alpha channel", OFFSET(aa), AV_OPT_TYPE_DOUBLE, {.dbl=1}, -2, 2, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(colorchannelmixer);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
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
    ColorChannelMixerContext *s = ctx->priv;
    int i, j, size, *buffer;

    ff_fill_rgba_map(s->rgba_map, outlink->format);

    switch (outlink->format) {
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_BGR48:
    case AV_PIX_FMT_RGBA64:
    case AV_PIX_FMT_BGRA64:
        size = 65536;
        break;
    default:
        size = 256;
    }

    s->buffer = buffer = av_malloc(16 * size * sizeof(*s->buffer));
    if (!s->buffer)
        return AVERROR(ENOMEM);

    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++, buffer += size)
            s->lut[i][j] = buffer;

    for (i = 0; i < size; i++) {
        s->lut[R][R][i] = lrint(i * s->rr);
        s->lut[R][G][i] = lrint(i * s->rg);
        s->lut[R][B][i] = lrint(i * s->rb);
        s->lut[R][A][i] = lrint(i * s->ra);

        s->lut[G][R][i] = lrint(i * s->gr);
        s->lut[G][G][i] = lrint(i * s->gg);
        s->lut[G][B][i] = lrint(i * s->gb);
        s->lut[G][A][i] = lrint(i * s->ga);

        s->lut[B][R][i] = lrint(i * s->br);
        s->lut[B][G][i] = lrint(i * s->bg);
        s->lut[B][B][i] = lrint(i * s->bb);
        s->lut[B][A][i] = lrint(i * s->ba);

        s->lut[A][R][i] = lrint(i * s->ar);
        s->lut[A][G][i] = lrint(i * s->ag);
        s->lut[A][B][i] = lrint(i * s->ab);
        s->lut[A][A][i] = lrint(i * s->aa);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ColorChannelMixerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
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
    switch (outlink->format) {
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB24:
        for (i = 0; i < outlink->h; i++) {
            const uint8_t *src = srcrow;
            uint8_t *dst = dstrow;

            for (j = 0; j < outlink->w * 3; j += 3) {
                const uint8_t rin = src[j + roffset];
                const uint8_t gin = src[j + goffset];
                const uint8_t bin = src[j + boffset];

                dst[j + roffset] = av_clip_uint8(s->lut[R][R][rin] +
                                                 s->lut[R][G][gin] +
                                                 s->lut[R][B][bin]);
                dst[j + goffset] = av_clip_uint8(s->lut[G][R][rin] +
                                                 s->lut[G][G][gin] +
                                                 s->lut[G][B][bin]);
                dst[j + boffset] = av_clip_uint8(s->lut[B][R][rin] +
                                                 s->lut[B][G][gin] +
                                                 s->lut[B][B][bin]);
            }

            srcrow += in->linesize[0];
            dstrow += out->linesize[0];
        }
        break;
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGB0:
        for (i = 0; i < outlink->h; i++) {
            const uint8_t *src = srcrow;
            uint8_t *dst = dstrow;

            for (j = 0; j < outlink->w * 4; j += 4) {
                const uint8_t rin = src[j + roffset];
                const uint8_t gin = src[j + goffset];
                const uint8_t bin = src[j + boffset];

                dst[j + roffset] = av_clip_uint8(s->lut[R][R][rin] +
                                                 s->lut[R][G][gin] +
                                                 s->lut[R][B][bin]);
                dst[j + goffset] = av_clip_uint8(s->lut[G][R][rin] +
                                                 s->lut[G][G][gin] +
                                                 s->lut[G][B][bin]);
                dst[j + boffset] = av_clip_uint8(s->lut[B][R][rin] +
                                                 s->lut[B][G][gin] +
                                                 s->lut[B][B][bin]);
                if (in != out)
                    dst[j + aoffset] = 0;
            }

            srcrow += in->linesize[0];
            dstrow += out->linesize[0];
        }
        break;
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGBA:
        for (i = 0; i < outlink->h; i++) {
            const uint8_t *src = srcrow;
            uint8_t *dst = dstrow;

            for (j = 0; j < outlink->w * 4; j += 4) {
                const uint8_t rin = src[j + roffset];
                const uint8_t gin = src[j + goffset];
                const uint8_t bin = src[j + boffset];
                const uint8_t ain = src[j + aoffset];

                dst[j + roffset] = av_clip_uint8(s->lut[R][R][rin] +
                                                 s->lut[R][G][gin] +
                                                 s->lut[R][B][bin] +
                                                 s->lut[R][A][ain]);
                dst[j + goffset] = av_clip_uint8(s->lut[G][R][rin] +
                                                 s->lut[G][G][gin] +
                                                 s->lut[G][B][bin] +
                                                 s->lut[G][A][ain]);
                dst[j + boffset] = av_clip_uint8(s->lut[B][R][rin] +
                                                 s->lut[B][G][gin] +
                                                 s->lut[B][B][bin] +
                                                 s->lut[B][A][ain]);
                dst[j + aoffset] = av_clip_uint8(s->lut[A][R][rin] +
                                                 s->lut[A][G][gin] +
                                                 s->lut[A][B][bin] +
                                                 s->lut[A][A][ain]);
            }

            srcrow += in->linesize[0];
            dstrow += out->linesize[0];
        }
        break;
    case AV_PIX_FMT_BGR48:
    case AV_PIX_FMT_RGB48:
        for (i = 0; i < outlink->h; i++) {
            const uint16_t *src = (const uint16_t *)srcrow;
            uint16_t *dst = (uint16_t *)dstrow;

            for (j = 0; j < outlink->w * 3; j += 3) {
                const uint16_t rin = src[j + roffset];
                const uint16_t gin = src[j + goffset];
                const uint16_t bin = src[j + boffset];

                dst[j + roffset] = av_clip_uint16(s->lut[R][R][rin] +
                                                  s->lut[R][G][gin] +
                                                  s->lut[R][B][bin]);
                dst[j + goffset] = av_clip_uint16(s->lut[G][R][rin] +
                                                  s->lut[G][G][gin] +
                                                  s->lut[G][B][bin]);
                dst[j + boffset] = av_clip_uint16(s->lut[B][R][rin] +
                                                  s->lut[B][G][gin] +
                                                  s->lut[B][B][bin]);
            }

            srcrow += in->linesize[0];
            dstrow += out->linesize[0];
        }
        break;
    case AV_PIX_FMT_BGRA64:
    case AV_PIX_FMT_RGBA64:
        for (i = 0; i < outlink->h; i++) {
            const uint16_t *src = (const uint16_t *)srcrow;
            uint16_t *dst = (uint16_t *)dstrow;

            for (j = 0; j < outlink->w * 4; j += 4) {
                const uint16_t rin = src[j + roffset];
                const uint16_t gin = src[j + goffset];
                const uint16_t bin = src[j + boffset];
                const uint16_t ain = src[j + aoffset];

                dst[j + roffset] = av_clip_uint16(s->lut[R][R][rin] +
                                                  s->lut[R][G][gin] +
                                                  s->lut[R][B][bin] +
                                                  s->lut[R][A][ain]);
                dst[j + goffset] = av_clip_uint16(s->lut[G][R][rin] +
                                                  s->lut[G][G][gin] +
                                                  s->lut[G][B][bin] +
                                                  s->lut[G][A][ain]);
                dst[j + boffset] = av_clip_uint16(s->lut[B][R][rin] +
                                                  s->lut[B][G][gin] +
                                                  s->lut[B][B][bin] +
                                                  s->lut[B][A][ain]);
                dst[j + aoffset] = av_clip_uint16(s->lut[A][R][rin] +
                                                  s->lut[A][G][gin] +
                                                  s->lut[A][B][bin] +
                                                  s->lut[A][A][ain]);
            }

            srcrow += in->linesize[0];
            dstrow += out->linesize[0];
        }
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(ctx->outputs[0], out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ColorChannelMixerContext *s = ctx->priv;

    av_freep(&s->buffer);
}

static const AVFilterPad colorchannelmixer_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad colorchannelmixer_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_colorchannelmixer = {
    .name          = "colorchannelmixer",
    .description   = NULL_IF_CONFIG_SMALL("Adjust colors by mixing color channels."),
    .priv_size     = sizeof(ColorChannelMixerContext),
    .priv_class    = &colorchannelmixer_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = colorchannelmixer_inputs,
    .outputs       = colorchannelmixer_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
