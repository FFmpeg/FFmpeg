/*
 * Copyright (c) 2002 Jindrich Makovicka <makovick@gmail.com>
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2013 Jean Delvare <khali@linux-fr.org>
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
 * @file
 * A very simple tv station logo remover
 * Originally imported from MPlayer libmpcodecs/vf_delogo.c,
 * the algorithm was later improved.
 */

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

/**
 * Apply a simple delogo algorithm to the image in src and put the
 * result in dst.
 *
 * The algorithm is only applied to the region specified by the logo
 * parameters.
 *
 * @param w      width of the input image
 * @param h      height of the input image
 * @param logo_x x coordinate of the top left corner of the logo region
 * @param logo_y y coordinate of the top left corner of the logo region
 * @param logo_w width of the logo
 * @param logo_h height of the logo
 * @param band   the size of the band around the processed area
 * @param show   show a rectangle around the processed area, useful for
 *               parameters tweaking
 * @param direct if non-zero perform in-place processing
 */
static void apply_delogo(uint8_t *dst, int dst_linesize,
                         uint8_t *src, int src_linesize,
                         int w, int h, AVRational sar,
                         int logo_x, int logo_y, int logo_w, int logo_h,
                         unsigned int band, int show, int direct)
{
    int x, y;
    uint64_t interp, weightl, weightr, weightt, weightb;
    uint8_t *xdst, *xsrc;

    uint8_t *topleft, *botleft, *topright;
    unsigned int left_sample, right_sample;
    int xclipl, xclipr, yclipt, yclipb;
    int logo_x1, logo_x2, logo_y1, logo_y2;

    xclipl = FFMAX(-logo_x, 0);
    xclipr = FFMAX(logo_x+logo_w-w, 0);
    yclipt = FFMAX(-logo_y, 0);
    yclipb = FFMAX(logo_y+logo_h-h, 0);

    logo_x1 = logo_x + xclipl;
    logo_x2 = logo_x + logo_w - xclipr;
    logo_y1 = logo_y + yclipt;
    logo_y2 = logo_y + logo_h - yclipb;

    topleft  = src+logo_y1     * src_linesize+logo_x1;
    topright = src+logo_y1     * src_linesize+logo_x2-1;
    botleft  = src+(logo_y2-1) * src_linesize+logo_x1;

    if (!direct)
        av_image_copy_plane(dst, dst_linesize, src, src_linesize, w, h);

    dst += (logo_y1 + 1) * dst_linesize;
    src += (logo_y1 + 1) * src_linesize;

    for (y = logo_y1+1; y < logo_y2-1; y++) {
        left_sample = topleft[src_linesize*(y-logo_y1)]   +
                      topleft[src_linesize*(y-logo_y1-1)] +
                      topleft[src_linesize*(y-logo_y1+1)];
        right_sample = topright[src_linesize*(y-logo_y1)]   +
                       topright[src_linesize*(y-logo_y1-1)] +
                       topright[src_linesize*(y-logo_y1+1)];

        for (x = logo_x1+1,
             xdst = dst+logo_x1+1,
             xsrc = src+logo_x1+1; x < logo_x2-1; x++, xdst++, xsrc++) {

            /* Weighted interpolation based on relative distances, taking SAR into account */
            weightl = (uint64_t)              (logo_x2-1-x) * (y-logo_y1) * (logo_y2-1-y) * sar.den;
            weightr = (uint64_t)(x-logo_x1)                 * (y-logo_y1) * (logo_y2-1-y) * sar.den;
            weightt = (uint64_t)(x-logo_x1) * (logo_x2-1-x)               * (logo_y2-1-y) * sar.num;
            weightb = (uint64_t)(x-logo_x1) * (logo_x2-1-x) * (y-logo_y1)                 * sar.num;

            interp =
                left_sample * weightl
                +
                right_sample * weightr
                +
                (topleft[x-logo_x1]    +
                 topleft[x-logo_x1-1]  +
                 topleft[x-logo_x1+1]) * weightt
                +
                (botleft[x-logo_x1]    +
                 botleft[x-logo_x1-1]  +
                 botleft[x-logo_x1+1]) * weightb;
            interp /= (weightl + weightr + weightt + weightb) * 3U;

            if (y >= logo_y+band && y < logo_y+logo_h-band &&
                x >= logo_x+band && x < logo_x+logo_w-band) {
                *xdst = interp;
            } else {
                unsigned dist = 0;

                if      (x < logo_x+band)
                    dist = FFMAX(dist, logo_x-x+band);
                else if (x >= logo_x+logo_w-band)
                    dist = FFMAX(dist, x-(logo_x+logo_w-1-band));

                if      (y < logo_y+band)
                    dist = FFMAX(dist, logo_y-y+band);
                else if (y >= logo_y+logo_h-band)
                    dist = FFMAX(dist, y-(logo_y+logo_h-1-band));

                *xdst = (*xsrc*dist + interp*(band-dist))/band;
                if (show && (dist == band-1))
                    *xdst = 0;
            }
        }

        dst += dst_linesize;
        src += src_linesize;
    }
}

typedef struct {
    const AVClass *class;
    int x, y, w, h, band, show;
}  DelogoContext;

#define OFFSET(x) offsetof(DelogoContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption delogo_options[]= {
    { "x",    "set logo x position",       OFFSET(x),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { "y",    "set logo y position",       OFFSET(y),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { "w",    "set logo width",            OFFSET(w),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { "h",    "set logo height",           OFFSET(h),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, FLAGS },
    { "band", "set delogo area band size", OFFSET(band), AV_OPT_TYPE_INT, { .i64 =  4 },  1, INT_MAX, FLAGS },
    { "t",    "set delogo area band size", OFFSET(band), AV_OPT_TYPE_INT, { .i64 =  4 },  1, INT_MAX, FLAGS },
    { "show", "show delogo area",          OFFSET(show), AV_OPT_TYPE_INT, { .i64 =  0 },  0, 1,       FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(delogo);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    DelogoContext *s = ctx->priv;

#define CHECK_UNSET_OPT(opt)                                            \
    if (s->opt == -1) {                                            \
        av_log(s, AV_LOG_ERROR, "Option %s was not set.\n", #opt); \
        return AVERROR(EINVAL);                                         \
    }
    CHECK_UNSET_OPT(x);
    CHECK_UNSET_OPT(y);
    CHECK_UNSET_OPT(w);
    CHECK_UNSET_OPT(h);

    av_log(ctx, AV_LOG_VERBOSE, "x:%d y:%d, w:%d h:%d band:%d show:%d\n",
           s->x, s->y, s->w, s->h, s->band, s->show);

    s->w += s->band*2;
    s->h += s->band*2;
    s->x -= s->band;
    s->y -= s->band;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    DelogoContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFrame *out;
    int hsub0 = desc->log2_chroma_w;
    int vsub0 = desc->log2_chroma_h;
    int direct = 0;
    int plane;
    AVRational sar;

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    }

    sar = in->sample_aspect_ratio;
    /* Assume square pixels if SAR is unknown */
    if (!sar.num)
        sar.num = sar.den = 1;

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        int hsub = plane == 1 || plane == 2 ? hsub0 : 0;
        int vsub = plane == 1 || plane == 2 ? vsub0 : 0;

        apply_delogo(out->data[plane], out->linesize[plane],
                     in ->data[plane], in ->linesize[plane],
                     FF_CEIL_RSHIFT(inlink->w, hsub),
                     FF_CEIL_RSHIFT(inlink->h, vsub),
                     sar, s->x>>hsub, s->y>>vsub,
                     /* Up and left borders were rounded down, inject lost bits
                      * into width and height to avoid error accumulation */
                     FF_CEIL_RSHIFT(s->w + (s->x & ((1<<hsub)-1)), hsub),
                     FF_CEIL_RSHIFT(s->h + (s->y & ((1<<vsub)-1)), vsub),
                     s->band>>FFMIN(hsub, vsub),
                     s->show, direct);
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_delogo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_delogo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_delogo = {
    .name          = "delogo",
    .description   = NULL_IF_CONFIG_SMALL("Remove logo from input video."),
    .priv_size     = sizeof(DelogoContext),
    .priv_class    = &delogo_class,
    .init          = init,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_delogo_inputs,
    .outputs   = avfilter_vf_delogo_outputs,
    .flags     = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
