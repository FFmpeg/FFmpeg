/*
 * ColorMatrix v2.2 for Avisynth 2.5.x
 *
 * Copyright (C) 2006-2007 Kevin Stone
 *
 * ColorMatrix 1.x is Copyright (C) Wilbert Dijkhof
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * OUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file
 * ColorMatrix 2.0 is based on the original ColorMatrix filter by Wilbert
 * Dijkhof.  It adds the ability to convert between any of: Rec.709, FCC,
 * Rec.601, and SMPTE 240M. It also makes pre and post clipping optional,
 * adds an option to use scaled or non-scaled coefficients, and more...
 */

#include <float.h>
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"

#define NS(n) n < 0 ? (int)(n*65536.0-0.5+DBL_EPSILON) : (int)(n*65536.0+0.5)
#define CB(n) av_clip_uint8(n)

static const double yuv_coeff[4][3][3] = {
    { { +0.7152, +0.0722, +0.2126 }, // Rec.709 (0)
      { -0.3850, +0.5000, -0.1150 },
      { -0.4540, -0.0460, +0.5000 } },
    { { +0.5900, +0.1100, +0.3000 }, // FCC (1)
      { -0.3310, +0.5000, -0.1690 },
      { -0.4210, -0.0790, +0.5000 } },
    { { +0.5870, +0.1140, +0.2990 }, // Rec.601 (ITU-R BT.470-2/SMPTE 170M) (2)
      { -0.3313, +0.5000, -0.1687 },
      { -0.4187, -0.0813, +0.5000 } },
    { { +0.7010, +0.0870, +0.2120 }, // SMPTE 240M (3)
      { -0.3840, +0.5000, -0.1160 },
      { -0.4450, -0.0550, +0.5000 } },
};

typedef struct {
    int yuv_convert[16][3][3];
    int interlaced;
    int source, dest, mode;
    char src[256];
    char dst[256];
    int hsub, vsub;
} ColorMatrixContext;

#define ma m[0][0]
#define mb m[0][1]
#define mc m[0][2]
#define md m[1][0]
#define me m[1][1]
#define mf m[1][2]
#define mg m[2][0]
#define mh m[2][1]
#define mi m[2][2]

#define ima im[0][0]
#define imb im[0][1]
#define imc im[0][2]
#define imd im[1][0]
#define ime im[1][1]
#define imf im[1][2]
#define img im[2][0]
#define imh im[2][1]
#define imi im[2][2]

static void inverse3x3(double im[3][3], const double m[3][3])
{
    double det = ma * (me * mi - mf * mh) - mb * (md * mi - mf * mg) + mc * (md * mh - me * mg);
    det = 1.0 / det;
    ima = det * (me * mi - mf * mh);
    imb = det * (mc * mh - mb * mi);
    imc = det * (mb * mf - mc * me);
    imd = det * (mf * mg - md * mi);
    ime = det * (ma * mi - mc * mg);
    imf = det * (mc * md - ma * mf);
    img = det * (md * mh - me * mg);
    imh = det * (mb * mg - ma * mh);
    imi = det * (ma * me - mb * md);
}

static void solve_coefficients(double cm[3][3], double rgb[3][3], const double yuv[3][3])
{
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            cm[i][j] = yuv[i][0] * rgb[0][j] + yuv[i][1] * rgb[1][j] + yuv[i][2] * rgb[2][j];
}

static void calc_coefficients(AVFilterContext *ctx)
{
    ColorMatrixContext *color = ctx->priv;
    double rgb_coeffd[4][3][3];
    double yuv_convertd[16][3][3];
    int v = 0;
    int i, j, k;

    for (i = 0; i < 4; i++)
        inverse3x3(rgb_coeffd[i], yuv_coeff[i]);
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            solve_coefficients(yuv_convertd[v], rgb_coeffd[i], yuv_coeff[j]);
            for (k = 0; k < 3; k++) {
                color->yuv_convert[v][k][0] = NS(yuv_convertd[v][k][0]);
                color->yuv_convert[v][k][1] = NS(yuv_convertd[v][k][1]);
                color->yuv_convert[v][k][2] = NS(yuv_convertd[v][k][2]);
            }
            if (color->yuv_convert[v][0][0] != 65536 || color->yuv_convert[v][1][0] != 0 ||
                color->yuv_convert[v][2][0] != 0) {
                av_log(ctx, AV_LOG_ERROR, "error calculating conversion coefficients\n");
            }
            v++;
        }
    }
}

static const char *color_modes[] = {"bt709", "FCC", "bt601", "smpte240m"};

static int get_color_mode_index(const char *name)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(color_modes); i++)
        if (!av_strcasecmp(color_modes[i], name))
            return i;
    return -1;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ColorMatrixContext *color = ctx->priv;

    if (!args)
        goto usage;
    if (sscanf(args, "%255[^:]:%255[^:]", color->src, color->dst) != 2) {
    usage:
        av_log(ctx, AV_LOG_ERROR, "usage: <src>:<dst>\n");
        av_log(ctx, AV_LOG_ERROR, "possible options: bt709,bt601,smpte240m,fcc\n");
        return -1;
    }

    color->source = get_color_mode_index(color->src);
    if (color->source < 0) {
        av_log(ctx, AV_LOG_ERROR, "unknown color space %s\n", color->src);
        return AVERROR(EINVAL);
    }

    color->dest = get_color_mode_index(color->dst);
    if (color->dest < 0) {
        av_log(ctx, AV_LOG_ERROR, "unknown color space %s\n", color->dst);
        return AVERROR(EINVAL);
    }

    if (color->source == color->dest) {
        av_log(ctx, AV_LOG_ERROR, "source and destination color space are identical\n");
        return AVERROR(EINVAL);
    }

    color->mode = color->source * 4 + color->dest;

    calc_coefficients(ctx);

    return 0;
}

static void process_frame_uyvy422(ColorMatrixContext *color,
                                  AVFilterBufferRef *dst, AVFilterBufferRef *src)
{
    const unsigned char *srcp = src->data[0];
    const int src_pitch = src->linesize[0];
    const int height = src->video->h;
    const int width = src->video->w*2;
    unsigned char *dstp = dst->data[0];
    const int dst_pitch = dst->linesize[0];
    const int c2 = color->yuv_convert[color->mode][0][1];
    const int c3 = color->yuv_convert[color->mode][0][2];
    const int c4 = color->yuv_convert[color->mode][1][1];
    const int c5 = color->yuv_convert[color->mode][1][2];
    const int c6 = color->yuv_convert[color->mode][2][1];
    const int c7 = color->yuv_convert[color->mode][2][2];
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x += 4) {
            const int u = srcp[x + 0] - 128;
            const int v = srcp[x + 2] - 128;
            const int uvval = c2 * u + c3 * v + 1081344;
            dstp[x + 0] = CB((c4 * u + c5 * v + 8421376) >> 16);
            dstp[x + 1] = CB((65536 * (srcp[x + 1] - 16) + uvval) >> 16);
            dstp[x + 2] = CB((c6 * u + c7 * v + 8421376) >> 16);
            dstp[x + 3] = CB((65536 * (srcp[x + 3] - 16) + uvval) >> 16);
        }
        srcp += src_pitch;
        dstp += dst_pitch;
    }
}

static void process_frame_yuv422p(ColorMatrixContext *color,
                                  AVFilterBufferRef *dst, AVFilterBufferRef *src)
{
    const unsigned char *srcpU = src->data[1];
    const unsigned char *srcpV = src->data[2];
    const unsigned char *srcpY = src->data[0];
    const int src_pitchY  = src->linesize[0];
    const int src_pitchUV = src->linesize[1];
    const int height = src->video->h;
    const int width = src->video->w;
    unsigned char *dstpU = dst->data[1];
    unsigned char *dstpV = dst->data[2];
    unsigned char *dstpY = dst->data[0];
    const int dst_pitchY  = dst->linesize[0];
    const int dst_pitchUV = dst->linesize[1];
    const int c2 = color->yuv_convert[color->mode][0][1];
    const int c3 = color->yuv_convert[color->mode][0][2];
    const int c4 = color->yuv_convert[color->mode][1][1];
    const int c5 = color->yuv_convert[color->mode][1][2];
    const int c6 = color->yuv_convert[color->mode][2][1];
    const int c7 = color->yuv_convert[color->mode][2][2];
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x += 2) {
            const int u = srcpU[x >> 1] - 128;
            const int v = srcpV[x >> 1] - 128;
            const int uvval = c2 * u + c3 * v + 1081344;
            dstpY[x + 0] = CB((65536 * (srcpY[x + 0] - 16) + uvval) >> 16);
            dstpY[x + 1] = CB((65536 * (srcpY[x + 1] - 16) + uvval) >> 16);
            dstpU[x >> 1] = CB((c4 * u + c5 * v + 8421376) >> 16);
            dstpV[x >> 1] = CB((c6 * u + c7 * v + 8421376) >> 16);
        }
        srcpY += src_pitchY;
        dstpY += dst_pitchY;
        srcpU += src_pitchUV;
        srcpV += src_pitchUV;
        dstpU += dst_pitchUV;
        dstpV += dst_pitchUV;
    }
}

static void process_frame_yuv420p(ColorMatrixContext *color,
                                  AVFilterBufferRef *dst, AVFilterBufferRef *src)
{
    const unsigned char *srcpU = src->data[1];
    const unsigned char *srcpV = src->data[2];
    const unsigned char *srcpY = src->data[0];
    const unsigned char *srcpN = src->data[0] + src->linesize[0];
    const int src_pitchY  = src->linesize[0];
    const int src_pitchUV = src->linesize[1];
    const int height = src->video->h;
    const int width = src->video->w;
    unsigned char *dstpU = dst->data[1];
    unsigned char *dstpV = dst->data[2];
    unsigned char *dstpY = dst->data[0];
    unsigned char *dstpN = dst->data[0] + dst->linesize[0];
    const int dst_pitchY  = dst->linesize[0];
    const int dst_pitchUV = dst->linesize[1];
    const int c2 = color->yuv_convert[color->mode][0][1];
    const int c3 = color->yuv_convert[color->mode][0][2];
    const int c4 = color->yuv_convert[color->mode][1][1];
    const int c5 = color->yuv_convert[color->mode][1][2];
    const int c6 = color->yuv_convert[color->mode][2][1];
    const int c7 = color->yuv_convert[color->mode][2][2];
    int x, y;

    for (y = 0; y < height; y += 2) {
        for (x = 0; x < width; x += 2) {
            const int u = srcpU[x >> 1] - 128;
            const int v = srcpV[x >> 1] - 128;
            const int uvval = c2 * u + c3 * v + 1081344;
            dstpY[x + 0] = CB((65536 * (srcpY[x + 0] - 16) + uvval) >> 16);
            dstpY[x + 1] = CB((65536 * (srcpY[x + 1] - 16) + uvval) >> 16);
            dstpN[x + 0] = CB((65536 * (srcpN[x + 0] - 16) + uvval) >> 16);
            dstpN[x + 1] = CB((65536 * (srcpN[x + 1] - 16) + uvval) >> 16);
            dstpU[x >> 1] = CB((c4 * u + c5 * v + 8421376) >> 16);
            dstpV[x >> 1] = CB((c6 * u + c7 * v + 8421376) >> 16);
        }
        srcpY += src_pitchY << 1;
        dstpY += dst_pitchY << 1;
        srcpN += src_pitchY << 1;
        dstpN += dst_pitchY << 1;
        srcpU += src_pitchUV;
        srcpV += src_pitchUV;
        dstpU += dst_pitchUV;
        dstpV += dst_pitchUV;
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ColorMatrixContext *color = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);

    color->hsub = pix_desc->log2_chroma_w;
    color->vsub = pix_desc->log2_chroma_h;

    av_log(ctx, AV_LOG_VERBOSE, "%s -> %s\n", color->src, color->dst);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_UYVY422,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFilterBufferRef *in)
{
    AVFilterContext *ctx = link->dst;
    ColorMatrixContext *color = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterBufferRef *out;

    out = ff_get_video_buffer(outlink, AV_PERM_WRITE, outlink->w, outlink->h);
    if (!out) {
        avfilter_unref_bufferp(&in);
        return AVERROR(ENOMEM);
    }
    avfilter_copy_buffer_ref_props(out, in);

    if (in->format == AV_PIX_FMT_YUV422P)
        process_frame_yuv422p(color, out, in);
    else if (in->format == AV_PIX_FMT_YUV420P)
        process_frame_yuv420p(color, out, in);
    else
        process_frame_uyvy422(color, out, in);

    avfilter_unref_bufferp(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad colormatrix_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input,
        .min_perms        = AV_PERM_READ,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad colormatrix_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_colormatrix = {
    .name          = "colormatrix",
    .description   = NULL_IF_CONFIG_SMALL("Color matrix conversion"),

    .priv_size     = sizeof(ColorMatrixContext),
    .init          = init,
    .query_formats = query_formats,
    .inputs        = colormatrix_inputs,
    .outputs       = colormatrix_outputs,
};
