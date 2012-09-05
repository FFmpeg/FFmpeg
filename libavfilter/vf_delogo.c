/*
 * Copyright (c) 2002 Jindrich Makovicka <makovick@gmail.com>
 * Copyright (c) 2011 Stefano Sabatini
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
 * Ported from MPlayer libmpcodecs/vf_delogo.c.
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
 * Apply a simple delogo algorithm to the image in dst and put the
 * result in src.
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
                         int w, int h,
                         int logo_x, int logo_y, int logo_w, int logo_h,
                         int band, int show, int direct)
{
    int x, y;
    int interp, dist;
    uint8_t *xdst, *xsrc;

    uint8_t *topleft, *botleft, *topright;
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

    dst += (logo_y1+1)*dst_linesize;
    src += (logo_y1+1)*src_linesize;

    if (!direct)
        av_image_copy_plane(dst, dst_linesize, src, src_linesize, w, h);

    for (y = logo_y1+1; y < logo_y2-1; y++) {
        for (x = logo_x1+1,
             xdst = dst+logo_x1+1,
             xsrc = src+logo_x1+1; x < logo_x2-1; x++, xdst++, xsrc++) {
            interp =
                (topleft[src_linesize*(y-logo_y  -yclipt)]   +
                 topleft[src_linesize*(y-logo_y-1-yclipt)]   +
                 topleft[src_linesize*(y-logo_y+1-yclipt)])  * (logo_w-(x-logo_x))/logo_w
                +
                (topright[src_linesize*(y-logo_y-yclipt)]    +
                 topright[src_linesize*(y-logo_y-1-yclipt)]  +
                 topright[src_linesize*(y-logo_y+1-yclipt)]) * (x-logo_x)/logo_w
                +
                (topleft[x-logo_x-xclipl]    +
                 topleft[x-logo_x-1-xclipl]  +
                 topleft[x-logo_x+1-xclipl]) * (logo_h-(y-logo_y))/logo_h
                +
                (botleft[x-logo_x-xclipl]    +
                 botleft[x-logo_x-1-xclipl]  +
                 botleft[x-logo_x+1-xclipl]) * (y-logo_y)/logo_h;
            interp /= 6;

            if (y >= logo_y+band && y < logo_y+logo_h-band &&
                x >= logo_x+band && x < logo_x+logo_w-band) {
                *xdst = interp;
            } else {
                dist = 0;
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
    {"x",    "set logo x position",       OFFSET(x),    AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS},
    {"y",    "set logo y position",       OFFSET(y),    AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS},
    {"w",    "set logo width",            OFFSET(w),    AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS},
    {"h",    "set logo height",           OFFSET(h),    AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, FLAGS},
    {"band", "set delogo area band size", OFFSET(band), AV_OPT_TYPE_INT, {.i64 =  4}, -1, INT_MAX, FLAGS},
    {"t",    "set delogo area band size", OFFSET(band), AV_OPT_TYPE_INT, {.i64 =  4}, -1, INT_MAX, FLAGS},
    {"show", "show delogo area",          OFFSET(show), AV_OPT_TYPE_INT, {.i64 =  0},  0,       1, FLAGS},
    {NULL},
};

AVFILTER_DEFINE_CLASS(delogo);

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV410P,  PIX_FMT_YUV440P,
        PIX_FMT_YUVA420P, PIX_FMT_GRAY8,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    DelogoContext *delogo = ctx->priv;
    int ret = 0;

    delogo->class = &delogo_class;
    av_opt_set_defaults(delogo);

    if (args)
        ret = sscanf(args, "%d:%d:%d:%d:%d",
                     &delogo->x, &delogo->y, &delogo->w, &delogo->h, &delogo->band);
    if (ret == 5) {
        if (delogo->band < 0)
            delogo->show = 1;
    } else if ((ret = (av_set_options_string(delogo, args, "=", ":"))) < 0)
        return ret;

#define CHECK_UNSET_OPT(opt)                                            \
    if (delogo->opt == -1) {                                            \
        av_log(delogo, AV_LOG_ERROR, "Option %s was not set.\n", #opt); \
        return AVERROR(EINVAL);                                         \
    }
    CHECK_UNSET_OPT(x);
    CHECK_UNSET_OPT(y);
    CHECK_UNSET_OPT(w);
    CHECK_UNSET_OPT(h);

    if (delogo->show)
        delogo->band = 4;

    av_log(ctx, AV_LOG_VERBOSE, "x:%d y:%d, w:%d h:%d band:%d show:%d\n",
           delogo->x, delogo->y, delogo->w, delogo->h, delogo->band, delogo->show);

    delogo->w += delogo->band*2;
    delogo->h += delogo->band*2;
    delogo->x -= delogo->band;
    delogo->y -= delogo->band;

    return 0;
}

static int null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    return 0;
}

static int end_frame(AVFilterLink *inlink)
{
    DelogoContext *delogo = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *inpicref  = inlink ->cur_buf;
    AVFilterBufferRef *outpicref = outlink->out_buf;
    int direct = inpicref->buf == outpicref->buf;
    int hsub0 = av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    int vsub0 = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;
    int plane;
    int ret;

    for (plane = 0; plane < 4 && inpicref->data[plane]; plane++) {
        int hsub = plane == 1 || plane == 2 ? hsub0 : 0;
        int vsub = plane == 1 || plane == 2 ? vsub0 : 0;

        apply_delogo(outpicref->data[plane], outpicref->linesize[plane],
                     inpicref ->data[plane], inpicref ->linesize[plane],
                     inlink->w>>hsub, inlink->h>>vsub,
                     delogo->x>>hsub, delogo->y>>vsub,
                     delogo->w>>hsub, delogo->h>>vsub,
                     delogo->band>>FFMIN(hsub, vsub),
                     delogo->show, direct);
    }

    if ((ret = ff_draw_slice(outlink, 0, inlink->h, 1)) < 0 ||
        (ret = ff_end_frame(outlink)) < 0)
        return ret;
    return 0;
}

AVFilter avfilter_vf_delogo = {
    .name          = "delogo",
    .description   = NULL_IF_CONFIG_SMALL("Remove logo from input video."),
    .priv_size     = sizeof(DelogoContext),
    .init          = init,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .get_video_buffer = ff_null_get_video_buffer,
                                          .start_frame      = ff_inplace_start_frame,
                                          .draw_slice       = null_draw_slice,
                                          .end_frame        = end_frame,
                                          .min_perms        = AV_PERM_WRITE | AV_PERM_READ },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO, },
                                        { .name = NULL}},
};
