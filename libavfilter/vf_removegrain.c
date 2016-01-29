/*
 * Copyright (c) 2012 Laurent de Soras
 * Copyright (c) 2013 Fredrik Mellbin
 * Copyright (c) 2015 Paul B Mahol
 * Copyright (c) 2015 James Darnley
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

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/qsort.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "removegrain.h"
#include "video.h"

#define OFFSET(x) offsetof(RemoveGrainContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption removegrain_options[] = {
    { "m0", "set mode for 1st plane", OFFSET(mode[0]), AV_OPT_TYPE_INT, {.i64=0}, 0, 24, FLAGS },
    { "m1", "set mode for 2nd plane", OFFSET(mode[1]), AV_OPT_TYPE_INT, {.i64=0}, 0, 24, FLAGS },
    { "m2", "set mode for 3rd plane", OFFSET(mode[2]), AV_OPT_TYPE_INT, {.i64=0}, 0, 24, FLAGS },
    { "m3", "set mode for 4th plane", OFFSET(mode[3]), AV_OPT_TYPE_INT, {.i64=0}, 0, 24, FLAGS },
    {NULL}
};

AVFILTER_DEFINE_CLASS(removegrain);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

#define REMOVE_GRAIN_SORT_AXIS       \
    const int ma1 = FFMAX(a1, a8);   \
    const int mi1 = FFMIN(a1, a8);   \
    const int ma2 = FFMAX(a2, a7);   \
    const int mi2 = FFMIN(a2, a7);   \
    const int ma3 = FFMAX(a3, a6);   \
    const int mi3 = FFMIN(a3, a6);   \
    const int ma4 = FFMAX(a4, a5);   \
    const int mi4 = FFMIN(a4, a5);

static int mode01(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int mi = FFMIN(FFMIN(FFMIN(a1, a2), FFMIN(a3, a4)), FFMIN(FFMIN(a5, a6), FFMIN(a7, a8)));
    const int ma = FFMAX(FFMAX(FFMAX(a1, a2), FFMAX(a3, a4)), FFMAX(FFMAX(a5, a6), FFMAX(a7, a8)));

    return av_clip(c, mi, ma);
}

static int cmp_int(const void *p1, const void *p2)
{
    int left  = *(const int *)p1;
    int right = *(const int *)p2;
    return FFDIFFSIGN(left, right);
}

static int mode02(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    int a[8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

    AV_QSORT(a, 8, int, cmp_int);

    return av_clip(c, a[2 - 1 ], a[7 - 1]);
}

static int mode03(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    int a[8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

    AV_QSORT(a, 8, int, cmp_int);

    return av_clip(c, a[3 - 1 ], a[6 - 1]);
}

static int mode04(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    int a[8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

    AV_QSORT(a, 8, int, cmp_int);

    return av_clip(c, a[4 - 1 ], a[5 - 1]);
}

static int mode05(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int c1 = FFABS(c - av_clip(c, mi1, ma1));
    const int c2 = FFABS(c - av_clip(c, mi2, ma2));
    const int c3 = FFABS(c - av_clip(c, mi3, ma3));
    const int c4 = FFABS(c - av_clip(c, mi4, ma4));

    const int mindiff = FFMIN(FFMIN(c1, c2), FFMIN(c3, c4));

    /* When adding SIMD notice the return order here: 4, 2, 3, 1. */
    if (mindiff == c4) {
        return av_clip(c, mi4, ma4);
    } else if (mindiff == c2) {
        return av_clip(c, mi2, ma2);
    } else if (mindiff == c3) {
        return av_clip(c, mi3, ma3);
    }

    return av_clip(c, mi1, ma1);
}

static int mode06(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int d1 = ma1 - mi1;
    const int d2 = ma2 - mi2;
    const int d3 = ma3 - mi3;
    const int d4 = ma4 - mi4;

    const int cli1 = av_clip(c, mi1, ma1);
    const int cli2 = av_clip(c, mi2, ma2);
    const int cli3 = av_clip(c, mi3, ma3);
    const int cli4 = av_clip(c, mi4, ma4);

    const int c1 = av_clip_uint16((FFABS(c - cli1) << 1) + d1);
    const int c2 = av_clip_uint16((FFABS(c - cli2) << 1) + d2);
    const int c3 = av_clip_uint16((FFABS(c - cli3) << 1) + d3);
    const int c4 = av_clip_uint16((FFABS(c - cli4) << 1) + d4);

    const int mindiff = FFMIN(FFMIN(c1, c2), FFMIN(c3, c4));

    if (mindiff == c4) {
        return cli4;
    } else if (mindiff == c2) {
        return cli2;
    } else if (mindiff == c3) {
        return cli3;
    }

    return cli1;
}

static int mode07(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int d1 = ma1 - mi1;
    const int d2 = ma2 - mi2;
    const int d3 = ma3 - mi3;
    const int d4 = ma4 - mi4;

    const int cli1 = av_clip(c, mi1, ma1);
    const int cli2 = av_clip(c, mi2, ma2);
    const int cli3 = av_clip(c, mi3, ma3);
    const int cli4 = av_clip(c, mi4, ma4);

    const int c1 = FFABS(c - cli1) + d1;
    const int c2 = FFABS(c - cli2) + d2;
    const int c3 = FFABS(c - cli3) + d3;
    const int c4 = FFABS(c - cli4) + d4;

    const int mindiff = FFMIN(FFMIN(c1, c2), FFMIN(c3, c4));

    if (mindiff == c4) {
        return cli4;
    } else if (mindiff == c2) {
        return cli2;
    } else if (mindiff == c3) {
        return cli3;
    }

    return cli1;
}

static int mode08(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int d1 = ma1 - mi1;
    const int d2 = ma2 - mi2;
    const int d3 = ma3 - mi3;
    const int d4 = ma4 - mi4;

    const int cli1 = av_clip(c, mi1, ma1);
    const int cli2 = av_clip(c, mi2, ma2);
    const int cli3 = av_clip(c, mi3, ma3);
    const int cli4 = av_clip(c, mi4, ma4);

    const int c1 = av_clip_uint16(FFABS(c - cli1) + (d1 << 1));
    const int c2 = av_clip_uint16(FFABS(c - cli2) + (d2 << 1));
    const int c3 = av_clip_uint16(FFABS(c - cli3) + (d3 << 1));
    const int c4 = av_clip_uint16(FFABS(c - cli4) + (d4 << 1));

    const int mindiff = FFMIN(FFMIN(c1, c2), FFMIN(c3, c4));

    if (mindiff == c4) {
        return cli4;
    } else if (mindiff == c2) {
        return cli2;
    } else if (mindiff == c3) {
        return cli3;
    }

    return cli1;
}

static int mode09(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int d1 = ma1 - mi1;
    const int d2 = ma2 - mi2;
    const int d3 = ma3 - mi3;
    const int d4 = ma4 - mi4;

    const int mindiff = FFMIN(FFMIN(d1, d2), FFMIN(d3, d4));

    if (mindiff == d4) {
        return av_clip(c, mi4, ma4);
    } else if (mindiff == d2) {
        return av_clip(c, mi2, ma2);
    } else if (mindiff == d3) {
        return av_clip(c, mi3, ma3);
    }

    return av_clip(c, mi1, ma1);
}

static int mode10(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int d1 = FFABS(c - a1);
    const int d2 = FFABS(c - a2);
    const int d3 = FFABS(c - a3);
    const int d4 = FFABS(c - a4);
    const int d5 = FFABS(c - a5);
    const int d6 = FFABS(c - a6);
    const int d7 = FFABS(c - a7);
    const int d8 = FFABS(c - a8);

    const int mindiff = FFMIN(FFMIN(FFMIN(d1, d2), FFMIN(d3, d4)),
                              FFMIN(FFMIN(d5, d6), FFMIN(d7, d8)));

    if (mindiff == d7) return a7;
    if (mindiff == d8) return a8;
    if (mindiff == d6) return a6;
    if (mindiff == d2) return a2;
    if (mindiff == d3) return a3;
    if (mindiff == d1) return a1;
    if (mindiff == d5) return a5;

    return a4;
}

static int mode1112(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int sum = 4 * c + 2 * (a2 + a4 + a5 + a7) + a1 + a3 + a6 + a8;
    const int val = (sum + 8) >> 4;

    return val;
}

static int mode1314(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int d1 = FFABS(a1 - a8);
    const int d2 = FFABS(a2 - a7);
    const int d3 = FFABS(a3 - a6);

    const int mindiff = FFMIN(FFMIN(d1, d2), d3);

    if (mindiff == d2) {
        return (a2 + a7 + 1) >> 1;
    }
    if (mindiff == d3) {
        return (a3 + a6 + 1) >> 1;
    }

    return (a1 + a8 + 1) >> 1;
}

static int mode1516(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int d1 = FFABS(a1 - a8);
    const int d2 = FFABS(a2 - a7);
    const int d3 = FFABS(a3 - a6);

    const int mindiff = FFMIN(FFMIN(d1, d2), d3);
    const int average = (2 * (a2 + a7) + a1 + a3 + a6 + a8 + 4) >> 3;

    if (mindiff == d2) {
        return av_clip(average, FFMIN(a2, a7), FFMAX(a2, a7));
    }
    if (mindiff == d3) {
        return av_clip(average, FFMIN(a3, a6), FFMAX(a3, a6));
    }

    return av_clip(average, FFMIN(a1, a8), FFMAX(a1, a8));
}

static int mode17(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int l = FFMAX(FFMAX(mi1, mi2), FFMAX(mi3, mi4));
    const int u = FFMIN(FFMIN(ma1, ma2), FFMIN(ma3, ma4));

    return av_clip(c, FFMIN(l, u), FFMAX(l, u));
}

static int mode18(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int d1 = FFMAX(FFABS(c - a1), FFABS(c - a8));
    const int d2 = FFMAX(FFABS(c - a2), FFABS(c - a7));
    const int d3 = FFMAX(FFABS(c - a3), FFABS(c - a6));
    const int d4 = FFMAX(FFABS(c - a4), FFABS(c - a5));

    const int mindiff = FFMIN(FFMIN(d1, d2), FFMIN(d3, d4));

    if (mindiff == d4) {
        return av_clip(c, FFMIN(a4, a5), FFMAX(a4, a5));
    }
    if (mindiff == d2) {
        return av_clip(c, FFMIN(a2, a7), FFMAX(a2, a7));
    }
    if (mindiff == d3) {
        return av_clip(c, FFMIN(a3, a6), FFMAX(a3, a6));
    }

    return av_clip(c, FFMIN(a1, a8), FFMAX(a1, a8));
}

static int mode19(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int sum = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
    const int val = (sum + 4) >> 3;

    return val;
}

static int mode20(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int sum = a1 + a2 + a3 + a4 + c + a5 + a6 + a7 + a8;
    const int val = (sum + 4) / 9;

    return val;
}

static int mode21(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int l1l = (a1 + a8) >> 1;
    const int l2l = (a2 + a7) >> 1;
    const int l3l = (a3 + a6) >> 1;
    const int l4l = (a4 + a5) >> 1;

    const int l1h = (a1 + a8 + 1) >> 1;
    const int l2h = (a2 + a7 + 1) >> 1;
    const int l3h = (a3 + a6 + 1) >> 1;
    const int l4h = (a4 + a5 + 1) >> 1;

    const int mi = FFMIN(FFMIN(l1l, l2l), FFMIN(l3l, l4l));
    const int ma = FFMAX(FFMAX(l1h, l2h), FFMAX(l3h, l4h));

    return av_clip(c, mi, ma);
}

static int mode22(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    const int l1 = (a1 + a8 + 1) >> 1;
    const int l2 = (a2 + a7 + 1) >> 1;
    const int l3 = (a3 + a6 + 1) >> 1;
    const int l4 = (a4 + a5 + 1) >> 1;

    const int mi = FFMIN(FFMIN(l1, l2), FFMIN(l3, l4));
    const int ma = FFMAX(FFMAX(l1, l2), FFMAX(l3, l4));

    return av_clip(c, mi, ma);
}

static int mode23(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int linediff1 = ma1 - mi1;
    const int linediff2 = ma2 - mi2;
    const int linediff3 = ma3 - mi3;
    const int linediff4 = ma4 - mi4;

    const int u1 = FFMIN(c - ma1, linediff1);
    const int u2 = FFMIN(c - ma2, linediff2);
    const int u3 = FFMIN(c - ma3, linediff3);
    const int u4 = FFMIN(c - ma4, linediff4);
    const int u = FFMAX(FFMAX(FFMAX(u1, u2), FFMAX(u3, u4)), 0);

    const int d1 = FFMIN(mi1 - c, linediff1);
    const int d2 = FFMIN(mi2 - c, linediff2);
    const int d3 = FFMIN(mi3 - c, linediff3);
    const int d4 = FFMIN(mi4 - c, linediff4);
    const int d = FFMAX(FFMAX(FFMAX(d1, d2), FFMAX(d3, d4)), 0);

    return c - u + d;  // This probably will never overflow.
}

static int mode24(int c, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8)
{
    REMOVE_GRAIN_SORT_AXIS

    const int linediff1 = ma1 - mi1;
    const int linediff2 = ma2 - mi2;
    const int linediff3 = ma3 - mi3;
    const int linediff4 = ma4 - mi4;

    const int tu1 = c - ma1;
    const int tu2 = c - ma2;
    const int tu3 = c - ma3;
    const int tu4 = c - ma4;

    const int u1 = FFMIN(tu1, linediff1 - tu1);
    const int u2 = FFMIN(tu2, linediff2 - tu2);
    const int u3 = FFMIN(tu3, linediff3 - tu3);
    const int u4 = FFMIN(tu4, linediff4 - tu4);
    const int u = FFMAX(FFMAX(FFMAX(u1, u2), FFMAX(u3, u4)), 0);

    const int td1 = mi1 - c;
    const int td2 = mi2 - c;
    const int td3 = mi3 - c;
    const int td4 = mi4 - c;

    const int d1 = FFMIN(td1, linediff1 - td1);
    const int d2 = FFMIN(td2, linediff2 - td2);
    const int d3 = FFMIN(td3, linediff3 - td3);
    const int d4 = FFMIN(td4, linediff4 - td4);
    const int d = FFMAX(FFMAX(FFMAX(d1, d2), FFMAX(d3, d4)), 0);

    return c - u + d;  // This probably will never overflow.
}

static int config_input(AVFilterLink *inlink)
{
    RemoveGrainContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;

    for (i = 0; i < s->nb_planes; i++) {
        switch (s->mode[i]) {
        case 1:  s->rg[i] = mode01;   break;
        case 2:  s->rg[i] = mode02;   break;
        case 3:  s->rg[i] = mode03;   break;
        case 4:  s->rg[i] = mode04;   break;
        case 5:  s->rg[i] = mode05;   break;
        case 6:  s->rg[i] = mode06;   break;
        case 7:  s->rg[i] = mode07;   break;
        case 8:  s->rg[i] = mode08;   break;
        case 9:  s->rg[i] = mode09;   break;
        case 10: s->rg[i] = mode10;   break;
        case 11: s->rg[i] = mode1112; break;
        case 12: s->rg[i] = mode1112; break;
        case 13: s->skip_odd = 1;
                 s->rg[i] = mode1314; break;
        case 14: s->skip_even = 1;
                 s->rg[i] = mode1314; break;
        case 15: s->skip_odd = 1;
                 s->rg[i] = mode1516; break;
        case 16: s->skip_even = 1;
                 s->rg[i] = mode1516; break;
        case 17: s->rg[i] = mode17;   break;
        case 18: s->rg[i] = mode18;   break;
        case 19: s->rg[i] = mode19;   break;
        case 20: s->rg[i] = mode20;   break;
        case 21: s->rg[i] = mode21;   break;
        case 22: s->rg[i] = mode22;   break;
        case 23: s->rg[i] = mode23;   break;
        case 24: s->rg[i] = mode24;   break;
        }
    }

    if (ARCH_X86)
        ff_removegrain_init_x86(s);

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int plane;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    RemoveGrainContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const int i = td->plane;
    const int height = s->planeheight[i];
    const int om = in->linesize[i] - 1;
    const int o0 = in->linesize[i]    ;
    const int op = in->linesize[i] + 1;
    int start = (height *  jobnr   ) / nb_jobs;
    int end   = (height * (jobnr+1)) / nb_jobs;
    int x, y;

    start = FFMAX(1, start);
    end   = FFMIN(height-1, end);
    for (y = start; y < end; y++) {
        uint8_t *dst = out->data[i];
        uint8_t *src = in->data[i];

        src = in->data[i] + y * in->linesize[i];
        dst = out->data[i] + y * out->linesize[i];

        if (s->skip_even && !(y & 1)) {
            memcpy(dst, src, s->planewidth[i]);
            continue;
        }
        if (s->skip_odd && y & 1) {
            memcpy(dst, src, s->planewidth[i]);
            continue;
        }

        *dst++ = *src++;

        if (s->fl[i]) {
            int w_asm = (s->planewidth[i] - 2) & ~15;

            s->fl[i](dst, src, in->linesize[i], w_asm);

            x = 1 + w_asm;
            dst += w_asm;
            src += w_asm;
        } else
            x = 1;

        for (; x < s->planewidth[i] - 1; x++) {
            const int a1 = src[-op];
            const int a2 = src[-o0];
            const int a3 = src[-om];
            const int a4 = src[-1 ];
            const int c  = src[ 0 ];
            const int a5 = src[ 1 ];
            const int a6 = src[ om];
            const int a7 = src[ o0];
            const int a8 = src[ op];

            const int res = s->rg[i](c, a1, a2, a3, a4, a5, a6, a7, a8);

            *dst = res;
            dst++, src++;
        }
        dst[0] = src[0];
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    RemoveGrainContext *s = ctx->priv;
    ThreadData td;
    AVFrame *out;
    int i;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (i = 0; i < s->nb_planes; i++) {
        uint8_t *dst = out->data[i];
        uint8_t *src = in->data[i];

        if (s->mode[i] == 0) {
            av_image_copy_plane(dst, out->linesize[i],
                                src, in->linesize[i],
                                s->planewidth[i], s->planeheight[i]);
            continue;
        }

        memcpy(dst, src, s->planewidth[i]);

        td.in = in; td.out = out; td.plane = i;
        ctx->internal->execute(ctx, filter_slice, &td, NULL,
                               FFMIN(s->planeheight[i], ctx->graph->nb_threads));

        src = in->data[i] + (s->planeheight[i] - 1) * in->linesize[i];
        dst = out->data[i] + (s->planeheight[i] - 1) * out->linesize[i];
        memcpy(dst, src, s->planewidth[i]);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad removegrain_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad removegrain_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_removegrain = {
    .name          = "removegrain",
    .description   = NULL_IF_CONFIG_SMALL("Remove grain."),
    .priv_size     = sizeof(RemoveGrainContext),
    .query_formats = query_formats,
    .inputs        = removegrain_inputs,
    .outputs       = removegrain_outputs,
    .priv_class    = &removegrain_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
