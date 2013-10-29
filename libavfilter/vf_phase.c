/*
 * Copyright (c) 2004 Ville Saari
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum PhaseMode {
    PROGRESSIVE,
    TOP_FIRST,
    BOTTOM_FIRST,
    TOP_FIRST_ANALYZE,
    BOTTOM_FIRST_ANALYZE,
    ANALYZE,
    FULL_ANALYZE,
    AUTO,
    AUTO_ANALYZE
};

typedef struct PhaseContext {
    const AVClass *class;
    enum PhaseMode mode;
    AVFrame *frame;
    int nb_planes;
    int planeheight[4];
    int linesize[4];
} PhaseContext;

#define OFFSET(x) offsetof(PhaseContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, unit }

static const AVOption phase_options[] = {
    { "mode", "set phase mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=AUTO_ANALYZE}, PROGRESSIVE, AUTO_ANALYZE, FLAGS, "mode" },
    CONST("p", "progressive",          PROGRESSIVE,          "mode"),
    CONST("t", "top first",            TOP_FIRST,            "mode"),
    CONST("b", "bottom first",         BOTTOM_FIRST,         "mode"),
    CONST("T", "top first analyze",    TOP_FIRST_ANALYZE,    "mode"),
    CONST("B", "bottom first analyze", BOTTOM_FIRST_ANALYZE, "mode"),
    CONST("u", "analyze",              ANALYZE,              "mode"),
    CONST("U", "full analyze",         FULL_ANALYZE,         "mode"),
    CONST("a", "auto",                 AUTO,                 "mode"),
    CONST("A", "auto analyze",         AUTO_ANALYZE,         "mode"),
    { NULL }
};

AVFILTER_DEFINE_CLASS(phase);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    PhaseContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

/*
 * This macro interpolates the value of both fields at a point halfway
 * between lines and takes the squared difference. In field resolution
 * the point is a quarter pixel below a line in one field and a quarter
 * pixel above a line in other.
 *
 * (The result is actually multiplied by 25)
 */
#define DIFF(a, as, b, bs) (t = ((*a - b[bs]) << 2) + a[as << 1] - b[-bs], t * t)

/*
 * Find which field combination has the smallest average squared difference
 * between the fields.
 */
static enum PhaseMode analyze_plane(AVFilterContext *ctx, PhaseContext *s,
                                    AVFrame *old, AVFrame *new)
{
    double bdiff, tdiff, pdiff, scale;
    const int ns = new->linesize[0];
    const int os = old->linesize[0];
    uint8_t *nptr = new->data[0];
    uint8_t *optr = old->data[0];
    const int h = new->height;
    const int w = new->width;
    int bdif, tdif, pdif;
    enum PhaseMode mode = s->mode;
    uint8_t *end, *rend;
    int top, t;

    if (mode == AUTO) {
        mode = new->interlaced_frame ? new->top_field_first ?
               TOP_FIRST : BOTTOM_FIRST : PROGRESSIVE;
    } else if (mode == AUTO_ANALYZE) {
        mode = new->interlaced_frame ? new->top_field_first ?
               TOP_FIRST_ANALYZE : BOTTOM_FIRST_ANALYZE : FULL_ANALYZE;
    }

    if (mode <= BOTTOM_FIRST) {
        bdiff = pdiff = tdiff = 65536.0;
    } else {
        bdiff = pdiff = tdiff = 0.0;

        for (end = nptr + (h - 2) * ns, nptr += ns, optr += os, top = 0;
             nptr < end; nptr += ns - w, optr += os - w, top ^= 1) {
            pdif = tdif = bdif = 0;

            switch (mode) {
            case TOP_FIRST_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(nptr, ns, optr, os);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            case BOTTOM_FIRST_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(nptr, ns, optr, os);
                    }
                }
                break;
            case ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        tdif += DIFF(nptr, ns, optr, os);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        bdif += DIFF(nptr, ns, optr, os);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            case FULL_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(nptr, ns, optr, os);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(nptr, ns, optr, os);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            default:
                av_assert0(0);
            }

            pdiff += (double)pdif;
            tdiff += (double)tdif;
            bdiff += (double)bdif;
        }

        scale = 1.0 / (w * (h - 3)) / 25.0;
        pdiff *= scale;
        tdiff *= scale;
        bdiff *= scale;

        if (mode == TOP_FIRST_ANALYZE) {
            bdiff = 65536.0;
        } else if (mode == BOTTOM_FIRST_ANALYZE) {
            tdiff = 65536.0;
        } else if (mode == ANALYZE) {
            pdiff = 65536.0;
        }

        if (bdiff < pdiff && bdiff < tdiff) {
            mode = BOTTOM_FIRST;
        } else if (tdiff < pdiff && tdiff < bdiff) {
            mode = TOP_FIRST;
        } else {
            mode = PROGRESSIVE;
        }
    }

    av_log(ctx, AV_LOG_DEBUG, "mode=%c tdiff=%f bdiff=%f pdiff=%f\n",
           mode == BOTTOM_FIRST ? 'b' : mode == TOP_FIRST ? 't' : 'p',
           tdiff, bdiff, pdiff);
    return mode;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PhaseContext *s = ctx->priv;
    enum PhaseMode mode;
    int plane, top, y;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (!s->frame) {
        mode = PROGRESSIVE;
        s->frame = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->frame) {
            av_frame_free(&in);
            av_frame_free(&out);
            return AVERROR(ENOMEM);
        }
    } else {
        mode = analyze_plane(ctx, s, s->frame, in);
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        uint8_t *buf = s->frame->data[plane];
        uint8_t *from = in->data[plane];
        uint8_t *to = out->data[plane];

        for (y = 0, top = 1; y < s->planeheight[plane]; y++, top ^= 1) {
            memcpy(to, mode == (top ? BOTTOM_FIRST : TOP_FIRST) ? buf : from, s->linesize[plane]);
            memcpy(buf, from, s->linesize[plane]);

            buf += s->frame->linesize[plane];
            from += in->linesize[plane];
            to += out->linesize[plane];
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PhaseContext *s = ctx->priv;

    av_frame_free(&s->frame);
}

static const AVFilterPad phase_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad phase_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_phase = {
    .name          = "phase",
    .description   = NULL_IF_CONFIG_SMALL("Phase shift fields."),
    .priv_size     = sizeof(PhaseContext),
    .priv_class    = &phase_class,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = phase_inputs,
    .outputs       = phase_outputs,
};
