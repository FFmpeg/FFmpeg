/*
 * Copyright (c) 2010 Mark Heath mjpeg0 @ silicontrip dot org
 * Copyright (c) 2014 Clément Bœsch
 * Copyright (c) 2014 Dave Rice @dericed
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
#include "internal.h"

enum FilterMode {
    FILTER_NONE = -1,
    FILTER_TOUT,
    FILTER_VREP,
    FILTER_BRNG,
    FILT_NUMB
};

typedef struct {
    const AVClass *class;
    int chromah;    // height of chroma plane
    int chromaw;    // width of chroma plane
    int hsub;       // horizontal subsampling
    int vsub;       // vertical subsampling
    int fs;         // pixel count per frame
    int cfs;        // pixel count per frame of chroma planes
    enum FilterMode outfilter;
    int filters;
    AVFrame *frame_prev;
    char *vrep_line;
    uint8_t rgba_color[4];
    int yuv_color[3];
} SignalstatsContext;

#define OFFSET(x) offsetof(SignalstatsContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption signalstats_options[] = {
    {"stat", "set statistics filters", OFFSET(filters), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, INT_MAX, FLAGS, "filters"},
        {"tout", "analyze pixels for temporal outliers",                0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_TOUT}, 0, 0, FLAGS, "filters"},
        {"vrep", "analyze video lines for vertical line repitition",    0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_VREP}, 0, 0, FLAGS, "filters"},
        {"brng", "analyze for pixels outside of broadcast range",       0, AV_OPT_TYPE_CONST, {.i64=1<<FILTER_BRNG}, 0, 0, FLAGS, "filters"},
    {"out", "set video filter", OFFSET(outfilter), AV_OPT_TYPE_INT, {.i64=FILTER_NONE}, -1, FILT_NUMB-1, FLAGS, "out"},
        {"tout", "highlight pixels that depict temporal outliers",              0, AV_OPT_TYPE_CONST, {.i64=FILTER_TOUT}, 0, 0, FLAGS, "out"},
        {"vrep", "highlight video lines that depict vertical line repitition",  0, AV_OPT_TYPE_CONST, {.i64=FILTER_VREP}, 0, 0, FLAGS, "out"},
        {"brng", "highlight pixels that are outside of broadcast range",        0, AV_OPT_TYPE_CONST, {.i64=FILTER_BRNG}, 0, 0, FLAGS, "out"},
    {"c",     "set highlight color", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str="yellow"}, .flags=FLAGS},
    {"color", "set highlight color", OFFSET(rgba_color), AV_OPT_TYPE_COLOR, {.str="yellow"}, .flags=FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(signalstats);

static av_cold int init(AVFilterContext *ctx)
{
    uint8_t r, g, b;
    SignalstatsContext *s = ctx->priv;

    if (s->outfilter != FILTER_NONE)
        s->filters |= 1 << s->outfilter;

    r = s->rgba_color[0];
    g = s->rgba_color[1];
    b = s->rgba_color[2];
    s->yuv_color[0] = (( 66*r + 129*g +  25*b + (1<<7)) >> 8) +  16;
    s->yuv_color[1] = ((-38*r + -74*g + 112*b + (1<<7)) >> 8) + 128;
    s->yuv_color[2] = ((112*r + -94*g + -18*b + (1<<7)) >> 8) + 128;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SignalstatsContext *s = ctx->priv;
    av_frame_free(&s->frame_prev);
    av_freep(&s->vrep_line);
}

static int query_formats(AVFilterContext *ctx)
{
    // TODO: add more
    enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SignalstatsContext *s = ctx->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    s->chromaw = FF_CEIL_RSHIFT(inlink->w, s->hsub);
    s->chromah = FF_CEIL_RSHIFT(inlink->h, s->vsub);

    s->fs = inlink->w * inlink->h;
    s->cfs = s->chromaw * s->chromah;

    if (s->filters & 1<<FILTER_VREP) {
        s->vrep_line = av_malloc(inlink->h * sizeof(*s->vrep_line));
        if (!s->vrep_line)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void burn_frame(SignalstatsContext *s, AVFrame *f, int x, int y)
{
    const int chromax = x >> s->hsub;
    const int chromay = y >> s->vsub;
    f->data[0][y       * f->linesize[0] +       x] = s->yuv_color[0];
    f->data[1][chromay * f->linesize[1] + chromax] = s->yuv_color[1];
    f->data[2][chromay * f->linesize[2] + chromax] = s->yuv_color[2];
}

static int filter_brng(SignalstatsContext *s, const AVFrame *in, AVFrame *out, int y, int w, int h)
{
    int x, score = 0;
    const int yc = y >> s->vsub;
    const uint8_t *pluma    = &in->data[0][y  * in->linesize[0]];
    const uint8_t *pchromau = &in->data[1][yc * in->linesize[1]];
    const uint8_t *pchromav = &in->data[2][yc * in->linesize[2]];

    for (x = 0; x < w; x++) {
        const int xc = x >> s->hsub;
        const int luma    = pluma[x];
        const int chromau = pchromau[xc];
        const int chromav = pchromav[xc];
        const int filt = luma    < 16 || luma    > 235 ||
                         chromau < 16 || chromau > 240 ||
                         chromav < 16 || chromav > 240;
        score += filt;
        if (out && filt)
            burn_frame(s, out, x, y);
    }
    return score;
}

static int filter_tout_outlier(uint8_t x, uint8_t y, uint8_t z)
{
    return ((abs(x - y) + abs (z - y)) / 2) - abs(z - x) > 4; // make 4 configurable?
}

static int filter_tout(SignalstatsContext *s, const AVFrame *in, AVFrame *out, int y, int w, int h)
{
    const uint8_t *p = in->data[0];
    int lw = in->linesize[0];
    int x, score = 0, filt;

    if (y - 1 < 0 || y + 1 >= h)
        return 0;

    // detect two pixels above and below (to eliminate interlace artefacts)
    // should check that video format is infact interlaced.

#define FILTER(i, j) \
filter_tout_outlier(p[(y-j) * lw + x + i], \
                    p[    y * lw + x + i], \
                    p[(y+j) * lw + x + i])

#define FILTER3(j) (FILTER(-1, j) && FILTER(0, j) && FILTER(1, j))

    if (y - 2 >= 0 && y + 2 < h) {
        for (x = 1; x < w - 1; x++) {
            filt = FILTER3(2) && FILTER3(1);
            score += filt;
            if (filt && out)
                burn_frame(s, out, x, y);
        }
    } else {
        for (x = 1; x < w - 1; x++) {
            filt = FILTER3(1);
            score += filt;
            if (filt && out)
                burn_frame(s, out, x, y);
        }
    }
    return score;
}

#define VREP_START 4

static void filter_init_vrep(SignalstatsContext *s, const AVFrame *p, int w, int h)
{
    int i, y;
    int lw = p->linesize[0];

    for (y = VREP_START; y < h; y++) {
        int totdiff = 0;
        int y2lw = (y - VREP_START) * lw;
        int ylw = y * lw;

        for (i = 0; i < w; i++)
            totdiff += abs(p->data[0][y2lw + i] - p->data[0][ylw + i]);

        /* this value should be definable */
        s->vrep_line[y] = totdiff < w;
    }
}

static int filter_vrep(SignalstatsContext *s, const AVFrame *in, AVFrame *out, int y, int w, int h)
{
    int x, score = 0;

    if (y < VREP_START)
        return 0;

    for (x = 0; x < w; x++) {
        if (s->vrep_line[y]) {
            score++;
            if (out)
                burn_frame(s, out, x, y);
        }
    }
    return score;
}

static const struct {
    const char *name;
    void (*init)(SignalstatsContext *s, const AVFrame *p, int w, int h);
    int (*process)(SignalstatsContext *s, const AVFrame *in, AVFrame *out, int y, int w, int h);
} filters_def[] = {
    {"TOUT", NULL,              filter_tout},
    {"VREP", filter_init_vrep,  filter_vrep},
    {"BRNG", NULL,              filter_brng},
    {NULL}
};

#define DEPTH 256

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    SignalstatsContext *s = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFrame *out = in;
    int i, j;
    int  w = 0,  cw = 0, // in
        pw = 0, cpw = 0; // prev
    int yuv, yuvu, yuvv;
    int fil;
    char metabuf[128];
    unsigned int histy[DEPTH] = {0},
                 histu[DEPTH] = {0},
                 histv[DEPTH] = {0},
                 histhue[360] = {0},
                 histsat[DEPTH] = {0}; // limited to 8 bit data.
    int miny  = -1, minu  = -1, minv  = -1;
    int maxy  = -1, maxu  = -1, maxv  = -1;
    int lowy  = -1, lowu  = -1, lowv  = -1;
    int highy = -1, highu = -1, highv = -1;
    int minsat = -1, maxsat = -1, lowsat = -1, highsat = -1;
    int lowp, highp, clowp, chighp;
    int accy, accu, accv;
    int accsat, acchue = 0;
    int medhue, maxhue;
    int toty = 0, totu = 0, totv = 0, totsat=0;
    int tothue = 0;
    int dify = 0, difu = 0, difv = 0;

    int filtot[FILT_NUMB] = {0};
    AVFrame *prev;

    if (!s->frame_prev)
        s->frame_prev = av_frame_clone(in);

    prev = s->frame_prev;

    if (s->outfilter != FILTER_NONE)
        out = av_frame_clone(in);

    for (fil = 0; fil < FILT_NUMB; fil ++)
        if ((s->filters & 1<<fil) && filters_def[fil].init)
            filters_def[fil].init(s, in, link->w, link->h);

    // Calculate luma histogram and difference with previous frame or field.
    for (j = 0; j < link->h; j++) {
        for (i = 0; i < link->w; i++) {
            yuv = in->data[0][w + i];
            histy[yuv]++;
            dify += abs(in->data[0][w + i] - prev->data[0][pw + i]);
        }
        w  += in->linesize[0];
        pw += prev->linesize[0];
    }

    // Calculate chroma histogram and difference with previous frame or field.
    for (j = 0; j < s->chromah; j++) {
        for (i = 0; i < s->chromaw; i++) {
            int sat, hue;

            yuvu = in->data[1][cw+i];
            yuvv = in->data[2][cw+i];
            histu[yuvu]++;
            difu += abs(in->data[1][cw+i] - prev->data[1][cpw+i]);
            histv[yuvv]++;
            difv += abs(in->data[2][cw+i] - prev->data[2][cpw+i]);

            // int or round?
            sat = hypot(yuvu - 128, yuvv - 128);
            histsat[sat]++;
            hue = floor((180 / M_PI) * atan2f(yuvu-128, yuvv-128) + 180);
            histhue[hue]++;
        }
        cw  += in->linesize[1];
        cpw += prev->linesize[1];
    }

    for (j = 0; j < link->h; j++) {
        for (fil = 0; fil < FILT_NUMB; fil ++) {
            if (s->filters & 1<<fil) {
                AVFrame *dbg = out != in && s->outfilter == fil ? out : NULL;
                filtot[fil] += filters_def[fil].process(s, in, dbg, j, link->w, link->h);
            }
        }
    }

    // find low / high based on histogram percentile
    // these only need to be calculated once.

    lowp   = lrint(s->fs  * 10 / 100.);
    highp  = lrint(s->fs  * 90 / 100.);
    clowp  = lrint(s->cfs * 10 / 100.);
    chighp = lrint(s->cfs * 90 / 100.);

    accy = accu = accv = accsat = 0;
    for (fil = 0; fil < DEPTH; fil++) {
        if (miny   < 0 && histy[fil])   miny = fil;
        if (minu   < 0 && histu[fil])   minu = fil;
        if (minv   < 0 && histv[fil])   minv = fil;
        if (minsat < 0 && histsat[fil]) minsat = fil;

        if (histy[fil])   maxy   = fil;
        if (histu[fil])   maxu   = fil;
        if (histv[fil])   maxv   = fil;
        if (histsat[fil]) maxsat = fil;

        toty   += histy[fil]   * fil;
        totu   += histu[fil]   * fil;
        totv   += histv[fil]   * fil;
        totsat += histsat[fil] * fil;

        accy   += histy[fil];
        accu   += histu[fil];
        accv   += histv[fil];
        accsat += histsat[fil];

        if (lowy   == -1 && accy   >=  lowp) lowy   = fil;
        if (lowu   == -1 && accu   >= clowp) lowu   = fil;
        if (lowv   == -1 && accv   >= clowp) lowv   = fil;
        if (lowsat == -1 && accsat >= clowp) lowsat = fil;

        if (highy   == -1 && accy   >=  highp) highy   = fil;
        if (highu   == -1 && accu   >= chighp) highu   = fil;
        if (highv   == -1 && accv   >= chighp) highv   = fil;
        if (highsat == -1 && accsat >= chighp) highsat = fil;
    }

    maxhue = histhue[0];
    medhue = -1;
    for (fil = 0; fil < 360; fil++) {
        tothue += histhue[fil] * fil;
        acchue += histhue[fil];

        if (medhue == -1 && acchue > s->cfs / 2)
            medhue = fil;
        if (histhue[fil] > maxhue) {
            maxhue = histhue[fil];
        }
    }

    av_frame_free(&s->frame_prev);
    s->frame_prev = av_frame_clone(in);

#define SET_META(key, fmt, val) do {                                \
    snprintf(metabuf, sizeof(metabuf), fmt, val);                   \
    av_dict_set(&out->metadata, "lavfi.signalstats." key, metabuf, 0);   \
} while (0)

    SET_META("YMIN",    "%d", miny);
    SET_META("YLOW",    "%d", lowy);
    SET_META("YAVG",    "%g", 1.0 * toty / s->fs);
    SET_META("YHIGH",   "%d", highy);
    SET_META("YMAX",    "%d", maxy);

    SET_META("UMIN",    "%d", minu);
    SET_META("ULOW",    "%d", lowu);
    SET_META("UAVG",    "%g", 1.0 * totu / s->cfs);
    SET_META("UHIGH",   "%d", highu);
    SET_META("UMAX",    "%d", maxu);

    SET_META("VMIN",    "%d", minv);
    SET_META("VLOW",    "%d", lowv);
    SET_META("VAVG",    "%g", 1.0 * totv / s->cfs);
    SET_META("VHIGH",   "%d", highv);
    SET_META("VMAX",    "%d", maxv);

    SET_META("SATMIN",  "%d", minsat);
    SET_META("SATLOW",  "%d", lowsat);
    SET_META("SATAVG",  "%g", 1.0 * totsat / s->cfs);
    SET_META("SATHIGH", "%d", highsat);
    SET_META("SATMAX",  "%d", maxsat);

    SET_META("HUEMED",  "%d", medhue);
    SET_META("HUEAVG",  "%g", 1.0 * tothue / s->cfs);

    SET_META("YDIF",    "%g", 1.0 * dify / s->fs);
    SET_META("UDIF",    "%g", 1.0 * difu / s->cfs);
    SET_META("VDIF",    "%g", 1.0 * difv / s->cfs);

    for (fil = 0; fil < FILT_NUMB; fil ++) {
        if (s->filters & 1<<fil) {
            char metaname[128];
            snprintf(metabuf,  sizeof(metabuf),  "%g", 1.0 * filtot[fil] / s->fs);
            snprintf(metaname, sizeof(metaname), "lavfi.signalstats.%s", filters_def[fil].name);
            av_dict_set(&out->metadata, metaname, metabuf, 0);
        }
    }

    if (in != out)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad signalstats_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad signalstats_outputs[] = {
    {
        .name           = "default",
        .config_props   = config_props,
        .type           = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_signalstats = {
    .name          = "signalstats",
    .description   = "Generate statistics from video analysis.",
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(SignalstatsContext),
    .inputs        = signalstats_inputs,
    .outputs       = signalstats_outputs,
    .priv_class    = &signalstats_class,
};
