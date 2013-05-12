/*
 * Copyright (c) 2010 Gordon Schmidt <gordon.schmidt <at> s2000.tu-chemnitz.de>
 * Copyright (c) 2013 Paul B Mahol
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
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum StereoCode {
    ANAGLYPH_RC_GRAY,   // anaglyph red/cyan gray
    ANAGLYPH_RC_HALF,   // anaglyph red/cyan half colored
    ANAGLYPH_RC_COLOR,  // anaglyph red/cyan colored
    ANAGLYPH_RC_DUBOIS, // anaglyph red/cyan dubois
    ANAGLYPH_GM_GRAY,   // anaglyph green/magenta gray
    ANAGLYPH_GM_HALF,   // anaglyph green/magenta half colored
    ANAGLYPH_GM_COLOR,  // anaglyph green/magenta colored
    ANAGLYPH_GM_DUBOIS, // anaglyph green/magenta dubois
    ANAGLYPH_YB_GRAY,   // anaglyph yellow/blue gray
    ANAGLYPH_YB_HALF,   // anaglyph yellow/blue half colored
    ANAGLYPH_YB_COLOR,  // anaglyph yellow/blue colored
    ANAGLYPH_YB_DUBOIS, // anaglyph yellow/blue dubois
    ANAGLYPH_RB_GRAY,   // anaglyph red/blue gray
    ANAGLYPH_RG_GRAY,   // anaglyph red/green gray
    MONO_L,             // mono output for debugging (left eye only)
    MONO_R,             // mono output for debugging (right eye only)
    INTERLEAVE_ROWS_LR, // row-interleave (left eye has top row)
    INTERLEAVE_ROWS_RL, // row-interleave (right eye has top row)
    SIDE_BY_SIDE_LR,    // side by side parallel (left eye left, right eye right)
    SIDE_BY_SIDE_RL,    // side by side crosseye (right eye left, left eye right)
    SIDE_BY_SIDE_2_LR,  // side by side parallel with half width resolution
    SIDE_BY_SIDE_2_RL,  // side by side crosseye with half width resolution
    ABOVE_BELOW_LR,     // above-below (left eye above, right eye below)
    ABOVE_BELOW_RL,     // above-below (right eye above, left eye below)
    ABOVE_BELOW_2_LR,   // above-below with half height resolution
    ABOVE_BELOW_2_RL,   // above-below with half height resolution
    ALTERNATING_LR,     // alternating frames (left eye first, right eye second)
    ALTERNATING_RL,     // alternating frames (right eye first, left eye second)
    STEREO_CODE_COUNT   // TODO: needs autodetection
};

typedef struct StereoComponent {
    enum StereoCode format;
    int width, height;
    int off_left, off_right;
    int off_lstep, off_rstep;
    int row_left, row_right;
} StereoComponent;

static const int ana_coeff[][3][6] = {
  [ANAGLYPH_RB_GRAY]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471}},
  [ANAGLYPH_RG_GRAY]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471},
     {    0,     0,     0,     0,     0,     0}},
  [ANAGLYPH_RC_GRAY]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471},
     {    0,     0,     0, 19595, 38470,  7471}},
  [ANAGLYPH_RC_HALF]   =
    {{19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_RC_COLOR]  =
    {{65536,     0,     0,     0,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_RC_DUBOIS] =
    {{29891, 32800, 11559, -2849, -5763,  -102},
     {-2627, -2479, -1033, 24804, 48080, -1209},
     { -997, -1350,  -358, -4729, -7403, 80373}},
  [ANAGLYPH_GM_GRAY]   =
    {{    0,     0,     0, 19595, 38470,  7471},
     {19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0, 19595, 38470,  7471}},
  [ANAGLYPH_GM_HALF]   =
    {{    0,     0,     0, 65536,     0,     0},
     {19595, 38470,  7471,     0,     0,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_GM_COLOR]  =
    {{    0,     0,     0, 65536,     0,     0},
     {    0, 65536,     0,     0,     0,     0},
     {    0,     0,     0,     0,     0, 65536}},
  [ANAGLYPH_GM_DUBOIS]  =
    {{-4063,-10354, -2556, 34669, 46203,  1573},
     {18612, 43778,  9372, -1049,  -983, -4260},
     { -983, -1769,  1376,   590,  4915, 61407}},
  [ANAGLYPH_YB_GRAY]   =
    {{    0,     0,     0, 19595, 38470,  7471},
     {    0,     0,     0, 19595, 38470,  7471},
     {19595, 38470,  7471,     0,     0,     0}},
  [ANAGLYPH_YB_HALF]   =
    {{    0,     0,     0, 65536,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {19595, 38470,  7471,     0,     0,     0}},
  [ANAGLYPH_YB_COLOR]  =
    {{    0,     0,     0, 65536,     0,     0},
     {    0,     0,     0,     0, 65536,     0},
     {    0,     0, 65536,     0,     0,     0}},
  [ANAGLYPH_YB_DUBOIS] =
    {{65535,-12650,18451,   -987, -7590, -1049},
     {-1604, 56032, 4196,    370,  3826, -1049},
     {-2345,-10676, 1358,   5801, 11416, 56217}},
};

typedef struct Stereo3DContext {
    const AVClass *class;
    StereoComponent in, out;
    int width, height;
    int row_step;
    int ana_matrix[3][6];
    int nb_planes;
    int linesize[4];
    int pixstep[4];
    AVFrame *prev;
    double ts_unit;
} Stereo3DContext;

#define OFFSET(x) offsetof(Stereo3DContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption stereo3d_options[] = {
    { "in",    "set input format",  OFFSET(in.format),  AV_OPT_TYPE_INT, {.i64=SIDE_BY_SIDE_LR}, SIDE_BY_SIDE_LR, STEREO_CODE_COUNT-1, FLAGS, "in"},
    { "ab2l",  "above below half height left first",  0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_2_LR},  0, 0, FLAGS, "in" },
    { "ab2r",  "above below half height right first", 0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_2_RL},  0, 0, FLAGS, "in" },
    { "abl",   "above below left first",              0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_LR},    0, 0, FLAGS, "in" },
    { "abr",   "above below right first",             0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_RL},    0, 0, FLAGS, "in" },
    { "al",    "alternating frames left first",       0, AV_OPT_TYPE_CONST, {.i64=ALTERNATING_LR},    0, 0, FLAGS, "in" },
    { "ar",    "alternating frames right first",      0, AV_OPT_TYPE_CONST, {.i64=ALTERNATING_RL},    0, 0, FLAGS, "in" },
    { "sbs2l", "side by side half width left first",  0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_2_LR}, 0, 0, FLAGS, "in" },
    { "sbs2r", "side by side half width right first", 0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_2_RL}, 0, 0, FLAGS, "in" },
    { "sbsl",  "side by side left first",             0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_LR},   0, 0, FLAGS, "in" },
    { "sbsr",  "side by side right first",            0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_RL},   0, 0, FLAGS, "in" },
    { "out",   "set output format", OFFSET(out.format), AV_OPT_TYPE_INT, {.i64=ANAGLYPH_RC_DUBOIS}, 0, STEREO_CODE_COUNT-1, FLAGS, "out"},
    { "ab2l",  "above below half height left first",  0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_2_LR},   0, 0, FLAGS, "out" },
    { "ab2r",  "above below half height right first", 0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_2_RL},   0, 0, FLAGS, "out" },
    { "abl",   "above below left first",              0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_LR},     0, 0, FLAGS, "out" },
    { "abr",   "above below right first",             0, AV_OPT_TYPE_CONST, {.i64=ABOVE_BELOW_RL},     0, 0, FLAGS, "out" },
    { "agmc",  "anaglyph green magenta color",        0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_GM_COLOR},  0, 0, FLAGS, "out" },
    { "agmd",  "anaglyph green magenta dubois",       0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_GM_DUBOIS}, 0, 0, FLAGS, "out" },
    { "agmg",  "anaglyph green magenta gray",         0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_GM_GRAY},   0, 0, FLAGS, "out" },
    { "agmh",  "anaglyph green magenta half color",   0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_GM_HALF},   0, 0, FLAGS, "out" },
    { "al",    "alternating frames left first",       0, AV_OPT_TYPE_CONST, {.i64=ALTERNATING_LR},     0, 0, FLAGS, "out" },
    { "ar",    "alternating frames right first",      0, AV_OPT_TYPE_CONST, {.i64=ALTERNATING_RL},     0, 0, FLAGS, "out" },
    { "arbg",  "anaglyph red blue gray",              0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RB_GRAY},   0, 0, FLAGS, "out" },
    { "arcc",  "anaglyph red cyan color",             0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RC_COLOR},  0, 0, FLAGS, "out" },
    { "arcd",  "anaglyph red cyan dubois",            0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RC_DUBOIS}, 0, 0, FLAGS, "out" },
    { "arcg",  "anaglyph red cyan gray",              0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RC_GRAY},   0, 0, FLAGS, "out" },
    { "arch",  "anaglyph red cyan half color",        0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RC_HALF},   0, 0, FLAGS, "out" },
    { "argg",  "anaglyph red green gray",             0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_RG_GRAY},   0, 0, FLAGS, "out" },
    { "aybc",  "anaglyph yellow blue color",          0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_YB_COLOR},  0, 0, FLAGS, "out" },
    { "aybd",  "anaglyph yellow blue dubois",         0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_YB_DUBOIS}, 0, 0, FLAGS, "out" },
    { "aybg",  "anaglyph yellow blue gray",           0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_YB_GRAY},   0, 0, FLAGS, "out" },
    { "aybh",  "anaglyph yellow blue half color",     0, AV_OPT_TYPE_CONST, {.i64=ANAGLYPH_YB_HALF},   0, 0, FLAGS, "out" },
    { "irl",   "interleave rows left first",          0, AV_OPT_TYPE_CONST, {.i64=INTERLEAVE_ROWS_LR}, 0, 0, FLAGS, "out" },
    { "irr",   "interleave rows right first",         0, AV_OPT_TYPE_CONST, {.i64=INTERLEAVE_ROWS_RL}, 0, 0, FLAGS, "out" },
    { "ml",    "mono left",                           0, AV_OPT_TYPE_CONST, {.i64=MONO_L},             0, 0, FLAGS, "out" },
    { "mr",    "mono right",                          0, AV_OPT_TYPE_CONST, {.i64=MONO_R},             0, 0, FLAGS, "out" },
    { "sbs2l", "side by side half width left first",  0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_2_LR},  0, 0, FLAGS, "out" },
    { "sbs2r", "side by side half width right first", 0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_2_RL},  0, 0, FLAGS, "out" },
    { "sbsl",  "side by side left first",             0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_LR},    0, 0, FLAGS, "out" },
    { "sbsr",  "side by side right first",            0, AV_OPT_TYPE_CONST, {.i64=SIDE_BY_SIDE_RL},    0, 0, FLAGS, "out" },
    {NULL},
};

AVFILTER_DEFINE_CLASS(stereo3d);

static const enum AVPixelFormat anaglyph_pix_fmts[] = { AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE };
static const enum AVPixelFormat other_pix_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB48BE, AV_PIX_FMT_BGR48BE,
    AV_PIX_FMT_RGB48LE, AV_PIX_FMT_BGR48LE,
    AV_PIX_FMT_RGBA64BE, AV_PIX_FMT_BGRA64BE,
    AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_BGRA64LE,
    AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,  AV_PIX_FMT_ABGR,
    AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
    AV_PIX_FMT_0RGB,  AV_PIX_FMT_0BGR,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9BE,  AV_PIX_FMT_GBRP9LE,
    AV_PIX_FMT_GBRP10BE, AV_PIX_FMT_GBRP10LE,
    AV_PIX_FMT_GBRP12BE, AV_PIX_FMT_GBRP12LE,
    AV_PIX_FMT_GBRP14BE, AV_PIX_FMT_GBRP14LE,
    AV_PIX_FMT_GBRP16BE, AV_PIX_FMT_GBRP16LE,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV444P9LE,  AV_PIX_FMT_YUVA444P9LE,
    AV_PIX_FMT_YUV444P9BE,  AV_PIX_FMT_YUVA444P9BE,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUVA444P10LE,
    AV_PIX_FMT_YUV444P10BE, AV_PIX_FMT_YUVA444P10BE,
    AV_PIX_FMT_YUV444P12BE,  AV_PIX_FMT_YUV444P12LE,
    AV_PIX_FMT_YUV444P14BE,  AV_PIX_FMT_YUV444P14LE,
    AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUVA444P16LE,
    AV_PIX_FMT_YUV444P16BE, AV_PIX_FMT_YUVA444P16BE,
    AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    Stereo3DContext *s = ctx->priv;
    const enum AVPixelFormat *pix_fmts;

    switch (s->out.format) {
    case ANAGLYPH_GM_COLOR:
    case ANAGLYPH_GM_DUBOIS:
    case ANAGLYPH_GM_GRAY:
    case ANAGLYPH_GM_HALF:
    case ANAGLYPH_RB_GRAY:
    case ANAGLYPH_RC_COLOR:
    case ANAGLYPH_RC_DUBOIS:
    case ANAGLYPH_RC_GRAY:
    case ANAGLYPH_RC_HALF:
    case ANAGLYPH_RG_GRAY:
    case ANAGLYPH_YB_COLOR:
    case ANAGLYPH_YB_DUBOIS:
    case ANAGLYPH_YB_GRAY:
    case ANAGLYPH_YB_HALF:
        pix_fmts = anaglyph_pix_fmts;
        break;
    default:
        pix_fmts = other_pix_fmts;
    }

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    Stereo3DContext *s = ctx->priv;
    AVRational aspect = inlink->sample_aspect_ratio;
    AVRational fps = inlink->frame_rate;
    AVRational tb = inlink->time_base;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int ret;

    switch (s->in.format) {
    case SIDE_BY_SIDE_2_LR:
    case SIDE_BY_SIDE_LR:
    case SIDE_BY_SIDE_2_RL:
    case SIDE_BY_SIDE_RL:
        if (inlink->w & 1) {
            av_log(ctx, AV_LOG_ERROR, "width must be even\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    case ABOVE_BELOW_2_LR:
    case ABOVE_BELOW_LR:
    case ABOVE_BELOW_2_RL:
    case ABOVE_BELOW_RL:
        if (s->out.format == INTERLEAVE_ROWS_LR ||
            s->out.format == INTERLEAVE_ROWS_RL) {
            if (inlink->h & 3) {
                av_log(ctx, AV_LOG_ERROR, "height must be multiple of 4\n");
                return AVERROR_INVALIDDATA;
            }
        }
        if (inlink->h & 1) {
            av_log(ctx, AV_LOG_ERROR, "height must be even\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    }

    s->in.width     =
    s->width        = inlink->w;
    s->in.height    =
    s->height       = inlink->h;
    s->row_step     = 1;
    s->in.off_lstep =
    s->in.off_rstep =
    s->in.off_left  =
    s->in.off_right =
    s->in.row_left  =
    s->in.row_right = 0;

    switch (s->in.format) {
    case SIDE_BY_SIDE_2_LR:
        aspect.num     *= 2;
    case SIDE_BY_SIDE_LR:
        s->width        = inlink->w / 2;
        s->in.off_right = s->width;
        break;
    case SIDE_BY_SIDE_2_RL:
        aspect.num     *= 2;
    case SIDE_BY_SIDE_RL:
        s->width        = inlink->w / 2;
        s->in.off_left  = s->width;
        break;
    case ABOVE_BELOW_2_LR:
        aspect.den     *= 2;
    case ABOVE_BELOW_LR:
        s->in.row_right =
        s->height       = inlink->h / 2;
        break;
    case ABOVE_BELOW_2_RL:
        aspect.den     *= 2;
    case ABOVE_BELOW_RL:
        s->in.row_left  =
        s->height       = inlink->h / 2;
        break;
    case ALTERNATING_RL:
    case ALTERNATING_LR:
        outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;
        fps.den        *= 2;
        tb.num         *= 2;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "input format %d is not supported\n", s->in.format);
        return AVERROR(EINVAL);
    }

    s->out.width     = s->width;
    s->out.height    = s->height;
    s->out.off_lstep =
    s->out.off_rstep =
    s->out.off_left  =
    s->out.off_right =
    s->out.row_left  =
    s->out.row_right = 0;

    switch (s->out.format) {
    case ANAGLYPH_RB_GRAY:
    case ANAGLYPH_RG_GRAY:
    case ANAGLYPH_RC_GRAY:
    case ANAGLYPH_RC_HALF:
    case ANAGLYPH_RC_COLOR:
    case ANAGLYPH_RC_DUBOIS:
    case ANAGLYPH_GM_GRAY:
    case ANAGLYPH_GM_HALF:
    case ANAGLYPH_GM_COLOR:
    case ANAGLYPH_GM_DUBOIS:
    case ANAGLYPH_YB_GRAY:
    case ANAGLYPH_YB_HALF:
    case ANAGLYPH_YB_COLOR:
    case ANAGLYPH_YB_DUBOIS:
        memcpy(s->ana_matrix, ana_coeff[s->out.format], sizeof(s->ana_matrix));
        break;
    case SIDE_BY_SIDE_2_LR:
        aspect.den      *= 2;
    case SIDE_BY_SIDE_LR:
        s->out.width     = s->width * 2;
        s->out.off_right = s->width;
        break;
    case SIDE_BY_SIDE_2_RL:
        aspect.den      *= 2;
    case SIDE_BY_SIDE_RL:
        s->out.width     = s->width * 2;
        s->out.off_left  = s->width;
        break;
    case ABOVE_BELOW_2_LR:
        aspect.num      *= 2;
    case ABOVE_BELOW_LR:
        s->out.height    = s->height * 2;
        s->out.row_right = s->height;
        break;
    case ABOVE_BELOW_2_RL:
        aspect.num      *= 2;
    case ABOVE_BELOW_RL:
        s->out.height    = s->height * 2;
        s->out.row_left  = s->height;
        break;
    case INTERLEAVE_ROWS_LR:
        s->row_step      = 2;
        s->height        = s->height / 2;
        s->out.off_rstep =
        s->in.off_rstep  = 1;
        break;
    case INTERLEAVE_ROWS_RL:
        s->row_step      = 2;
        s->height        = s->height / 2;
        s->out.off_lstep =
        s->in.off_lstep  = 1;
        break;
    case MONO_R:
        s->in.off_left   = s->in.off_right;
        s->in.row_left   = s->in.row_right;
    case MONO_L:
        break;
    case ALTERNATING_RL:
    case ALTERNATING_LR:
        fps.num         *= 2;
        tb.den          *= 2;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "output format %d is not supported\n", s->out.format);
        return AVERROR(EINVAL);
    }

    outlink->w = s->out.width;
    outlink->h = s->out.height;
    outlink->frame_rate = fps;
    outlink->time_base = tb;
    outlink->sample_aspect_ratio = aspect;

    if ((ret = av_image_fill_linesizes(s->linesize, outlink->format, s->width)) < 0)
        return ret;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    av_image_fill_max_pixsteps(s->pixstep, NULL, desc);
    s->ts_unit = av_q2d(av_inv_q(av_mul_q(outlink->frame_rate, outlink->time_base)));

    return 0;
}

static inline uint8_t ana_convert(const int *coeff, uint8_t *left, uint8_t *right)
{
    int sum;

    sum  = coeff[0] * left[0] + coeff[3] * right[0]; //red in
    sum += coeff[1] * left[1] + coeff[4] * right[1]; //green in
    sum += coeff[2] * left[2] + coeff[5] * right[2]; //blue in

    return av_clip_uint8(sum >> 16);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx  = inlink->dst;
    Stereo3DContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out, *oleft, *oright, *ileft, *iright;
    int out_off_left[4], out_off_right[4];
    int in_off_left[4], in_off_right[4];
    int i;

    switch (s->in.format) {
    case ALTERNATING_LR:
    case ALTERNATING_RL:
        if (!s->prev) {
            s->prev = inpicref;
            return 0;
        }
        ileft  = s->prev;
        iright = inpicref;
        if (s->in.format == ALTERNATING_RL)
            FFSWAP(AVFrame *, ileft, iright);
        break;
    default:
        ileft = iright = inpicref;
    };

    out = oleft = oright = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&s->prev);
        av_frame_free(&inpicref);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, inpicref);

    if (s->out.format == ALTERNATING_LR ||
        s->out.format == ALTERNATING_RL) {
        oright = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!oright) {
            av_frame_free(&oleft);
            av_frame_free(&s->prev);
            av_frame_free(&inpicref);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(oright, inpicref);
    }

    for (i = 0; i < 4; i++) {
        in_off_left[i]   = (s->in.row_left   + s->in.off_lstep)  * ileft->linesize[i]  + s->in.off_left   * s->pixstep[i];
        in_off_right[i]  = (s->in.row_right  + s->in.off_rstep)  * iright->linesize[i] + s->in.off_right  * s->pixstep[i];
        out_off_left[i]  = (s->out.row_left  + s->out.off_lstep) * oleft->linesize[i]  + s->out.off_left  * s->pixstep[i];
        out_off_right[i] = (s->out.row_right + s->out.off_rstep) * oright->linesize[i] + s->out.off_right * s->pixstep[i];
    }

    switch (s->out.format) {
    case ALTERNATING_LR:
    case ALTERNATING_RL:
    case SIDE_BY_SIDE_LR:
    case SIDE_BY_SIDE_RL:
    case SIDE_BY_SIDE_2_LR:
    case SIDE_BY_SIDE_2_RL:
    case ABOVE_BELOW_LR:
    case ABOVE_BELOW_RL:
    case ABOVE_BELOW_2_LR:
    case ABOVE_BELOW_2_RL:
    case INTERLEAVE_ROWS_LR:
    case INTERLEAVE_ROWS_RL:
        for (i = 0; i < s->nb_planes; i++) {
            av_image_copy_plane(oleft->data[i] + out_off_left[i],
                                oleft->linesize[i] * s->row_step,
                                ileft->data[i] + in_off_left[i],
                                ileft->linesize[i] * s->row_step,
                                s->linesize[i], s->height);
            av_image_copy_plane(oright->data[i] + out_off_right[i],
                                oright->linesize[i] * s->row_step,
                                iright->data[i] + in_off_right[i],
                                iright->linesize[i] * s->row_step,
                                s->linesize[i], s->height);
        }
        break;
    case MONO_L:
        iright = ileft;
    case MONO_R:
        for (i = 0; i < s->nb_planes; i++) {
            av_image_copy_plane(out->data[i], out->linesize[i],
                                iright->data[i] + in_off_left[i],
                                iright->linesize[i],
                                s->linesize[i], s->height);
        }
        break;
    case ANAGLYPH_RB_GRAY:
    case ANAGLYPH_RG_GRAY:
    case ANAGLYPH_RC_GRAY:
    case ANAGLYPH_RC_HALF:
    case ANAGLYPH_RC_COLOR:
    case ANAGLYPH_RC_DUBOIS:
    case ANAGLYPH_GM_GRAY:
    case ANAGLYPH_GM_HALF:
    case ANAGLYPH_GM_COLOR:
    case ANAGLYPH_GM_DUBOIS:
    case ANAGLYPH_YB_GRAY:
    case ANAGLYPH_YB_HALF:
    case ANAGLYPH_YB_COLOR:
    case ANAGLYPH_YB_DUBOIS: {
        int i, x, y, il, ir, o;
        uint8_t *lsrc = ileft->data[0];
        uint8_t *rsrc = iright->data[0];
        uint8_t *dst = out->data[0];
        int out_width = s->out.width;
        int *ana_matrix[3];

        for (i = 0; i < 3; i++)
            ana_matrix[i] = s->ana_matrix[i];

        for (y = 0; y < s->out.height; y++) {
            o   = out->linesize[0] * y;
            il  = in_off_left[0]  + y * ileft->linesize[0];
            ir  = in_off_right[0] + y * iright->linesize[0];
            for (x = 0; x < out_width; x++, il += 3, ir += 3, o+= 3) {
                dst[o    ] = ana_convert(ana_matrix[0], lsrc + il, rsrc + ir);
                dst[o + 1] = ana_convert(ana_matrix[1], lsrc + il, rsrc + ir);
                dst[o + 2] = ana_convert(ana_matrix[2], lsrc + il, rsrc + ir);
            }
        }
        break;
    }
    default:
        av_assert0(0);
    }

    av_frame_free(&inpicref);
    av_frame_free(&s->prev);
    if (oright != oleft) {
        if (s->out.format == ALTERNATING_LR)
            FFSWAP(AVFrame *, oleft, oright);
        oright->pts = outlink->frame_count * s->ts_unit;
        ff_filter_frame(outlink, oright);
        out = oleft;
        oleft->pts = outlink->frame_count * s->ts_unit;
    } else if (s->in.format == ALTERNATING_LR ||
               s->in.format == ALTERNATING_RL) {
        out->pts = outlink->frame_count * s->ts_unit;
    }
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    Stereo3DContext *s = ctx->priv;

    av_frame_free(&s->prev);
}

static const AVFilterPad stereo3d_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad stereo3d_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter avfilter_vf_stereo3d = {
    .name          = "stereo3d",
    .description   = NULL_IF_CONFIG_SMALL("Convert video stereoscopic 3D view."),
    .priv_size     = sizeof(Stereo3DContext),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = stereo3d_inputs,
    .outputs       = stereo3d_outputs,
    .priv_class    = &stereo3d_class,
};
