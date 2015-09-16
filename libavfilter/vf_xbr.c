/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2011, 2012 Hyllian/Jararaca <sergiogdb@gmail.com>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
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

/**
 * @file
 * XBR Filter is used for depixelization of image.
 * This is based on Hyllian's xBR shader.
 *
 * @see http://www.libretro.com/forums/viewtopic.php?f=6&t=134
 * @see https://github.com/yoyofr/iFBA/blob/master/fba_src/src/intf/video/scalers/xbr.cpp
 */

#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "internal.h"

#define LB_MASK       0x00FEFEFE
#define RED_BLUE_MASK 0x00FF00FF
#define GREEN_MASK    0x0000FF00

typedef int (*xbrfunc_t)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

typedef struct {
    const AVClass *class;
    int n;
    xbrfunc_t func;
    uint32_t rgbtoyuv[1<<24];
} XBRContext;

typedef struct ThreadData {
    AVFrame *in, *out;
    const uint32_t *rgbtoyuv;
} ThreadData;

#define OFFSET(x) offsetof(XBRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption xbr_options[] = {
    { "n", "set scale factor", OFFSET(n), AV_OPT_TYPE_INT, {.i64 = 3}, 2, 4, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(xbr);

static uint32_t pixel_diff(uint32_t x, uint32_t y, const uint32_t *r2y)
{
#define YMASK 0xff0000
#define UMASK 0x00ff00
#define VMASK 0x0000ff
#define ABSDIFF(a,b) (abs((int)(a)-(int)(b)))

    uint32_t yuv1 = r2y[x & 0xffffff];
    uint32_t yuv2 = r2y[y & 0xffffff];

    return (ABSDIFF(yuv1 & YMASK, yuv2 & YMASK) >> 16) +
           (ABSDIFF(yuv1 & UMASK, yuv2 & UMASK) >>  8) +
            ABSDIFF(yuv1 & VMASK, yuv2 & VMASK);
}

#define ALPHA_BLEND_128_W(a, b) ((((a) & LB_MASK) >> 1) + (((b) & LB_MASK) >> 1))
#define ALPHA_BLEND_BASE(a, b, m, s) (  (RED_BLUE_MASK & (((a) & RED_BLUE_MASK) + (((((b) & RED_BLUE_MASK) - ((a) & RED_BLUE_MASK)) * (m)) >> (s)))) \
                                      | (GREEN_MASK    & (((a) & GREEN_MASK)    + (((((b) & GREEN_MASK)    - ((a) & GREEN_MASK))    * (m)) >> (s)))))
#define ALPHA_BLEND_32_W(a, b)  ALPHA_BLEND_BASE(a, b, 1, 3)
#define ALPHA_BLEND_64_W(a, b)  ALPHA_BLEND_BASE(a, b, 1, 2)
#define ALPHA_BLEND_192_W(a, b) ALPHA_BLEND_BASE(a, b, 3, 2)
#define ALPHA_BLEND_224_W(a, b) ALPHA_BLEND_BASE(a, b, 7, 3)

#define df(A, B) pixel_diff(A, B, r2y)
#define eq(A, B) (df(A, B) < 155)

#define FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,   \
              N0, N1, N2, N3) do {                                                                  \
    if (PE != PH && PE != PF) {                                                                     \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);          \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);          \
        if (e <= i) {                                                                               \
            const unsigned px = df(PE,PF) <= df(PE,PH) ? PF : PH;                                   \
            if (e < i && (!eq(PF,PB) && !eq(PH,PD) || eq(PE,PI)                                     \
                          && (!eq(PF,I4) && !eq(PH,I5))                                             \
                          || eq(PE,PG) || eq(PE,PC))) {                                             \
                const unsigned ke = df(PF,PG);                                                      \
                const unsigned ki = df(PH,PC);                                                      \
                const int left    = ke<<1 <= ki && PE != PG && PD != PG;                            \
                const int up      = ke >= ki<<1 && PE != PC && PB != PC;                            \
                if (left && up) {                                                                   \
                    E[N3] = ALPHA_BLEND_224_W(E[N3], px);                                           \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                           \
                    E[N1] = E[N2];                                                                  \
                } else if (left) {                                                                  \
                    E[N3] = ALPHA_BLEND_192_W(E[N3], px);                                           \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                           \
                } else if (up) {                                                                    \
                    E[N3] = ALPHA_BLEND_192_W(E[N3], px);                                           \
                    E[N1] = ALPHA_BLEND_64_W( E[N1], px);                                           \
                } else { /* diagonal */                                                             \
                    E[N3] = ALPHA_BLEND_128_W(E[N3], px);                                           \
                }                                                                                   \
            } else {                                                                                \
                E[N3] = ALPHA_BLEND_128_W(E[N3], px);                                               \
            }                                                                                       \
        }                                                                                           \
    }                                                                                               \
} while (0)

#define FILT3(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,   \
              N0, N1, N2, N3, N4, N5, N6, N7, N8) do {                                              \
    if (PE != PH && PE != PF) {                                                                     \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);          \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);          \
        if (e <= i) {                                                                               \
            const unsigned px = df(PE,PF) <= df(PE,PH) ? PF : PH;                                   \
            if (e < i && (!eq(PF,PB) && !eq(PF,PC) || !eq(PH,PD) && !eq(PH,PG) || eq(PE,PI)         \
                          && (!eq(PF,F4) && !eq(PF,I4) || !eq(PH,H5) && !eq(PH,I5))                 \
                          || eq(PE,PG) || eq(PE,PC))) {                                             \
                const unsigned ke = df(PF,PG);                                                      \
                const unsigned ki = df(PH,PC);                                                      \
                const int left    = ke<<1 <= ki && PE != PG && PD != PG;                            \
                const int up      = ke >= ki<<1 && PE != PC && PB != PC;                            \
                if (left && up) {                                                                   \
                    E[N7] = ALPHA_BLEND_192_W(E[N7], px);                                           \
                    E[N6] = ALPHA_BLEND_64_W( E[N6], px);                                           \
                    E[N5] = E[N7];                                                                  \
                    E[N2] = E[N6];                                                                  \
                    E[N8] = px;                                                                     \
                } else if (left) {                                                                  \
                    E[N7] = ALPHA_BLEND_192_W(E[N7], px);                                           \
                    E[N5] = ALPHA_BLEND_64_W( E[N5], px);                                           \
                    E[N6] = ALPHA_BLEND_64_W( E[N6], px);                                           \
                    E[N8] = px;                                                                     \
                } else if (up) {                                                                    \
                    E[N5] = ALPHA_BLEND_192_W(E[N5], px);                                           \
                    E[N7] = ALPHA_BLEND_64_W( E[N7], px);                                           \
                    E[N2] = ALPHA_BLEND_64_W( E[N2], px);                                           \
                    E[N8] = px;                                                                     \
                } else { /* diagonal */                                                             \
                    E[N8] = ALPHA_BLEND_224_W(E[N8], px);                                           \
                    E[N5] = ALPHA_BLEND_32_W( E[N5], px);                                           \
                    E[N7] = ALPHA_BLEND_32_W( E[N7], px);                                           \
                }                                                                                   \
            } else {                                                                                \
                E[N8] = ALPHA_BLEND_128_W(E[N8], px);                                               \
            }                                                                                       \
        }                                                                                           \
    }                                                                                               \
} while (0)

#define FILT4(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1,   \
              N15, N14, N11, N3, N7, N10, N13, N12, N9, N6, N2, N1, N5, N8, N4, N0) do {            \
    if (PE != PH && PE != PF) {                                                                     \
        const unsigned e = df(PE,PC) + df(PE,PG) + df(PI,H5) + df(PI,F4) + (df(PH,PF)<<2);          \
        const unsigned i = df(PH,PD) + df(PH,I5) + df(PF,I4) + df(PF,PB) + (df(PE,PI)<<2);          \
        if (e <= i) {                                                                               \
            const unsigned px = df(PE,PF) <= df(PE,PH) ? PF : PH;                                   \
            if (e < i && (!eq(PF,PB) && !eq(PH,PD) || eq(PE,PI)                                     \
                          && (!eq(PF,I4) && !eq(PH,I5))                                             \
                          || eq(PE,PG) || eq(PE,PC))) {                                             \
                const unsigned ke = df(PF,PG);                                                      \
                const unsigned ki = df(PH,PC);                                                      \
                const int left    = ke<<1 <= ki && PE != PG && PD != PG;                            \
                const int up      = ke >= ki<<1 && PE != PC && PB != PC;                            \
                if (left && up) {                                                                   \
                    E[N13] = ALPHA_BLEND_192_W(E[N13], px);                                         \
                    E[N12] = ALPHA_BLEND_64_W( E[N12], px);                                         \
                    E[N15] = E[N14] = E[N11] = px;                                                  \
                    E[N10] = E[N3]  = E[N12];                                                       \
                    E[N7]  = E[N13];                                                                \
                } else if (left) {                                                                  \
                    E[N11] = ALPHA_BLEND_192_W(E[N11], px);                                         \
                    E[N13] = ALPHA_BLEND_192_W(E[N13], px);                                         \
                    E[N10] = ALPHA_BLEND_64_W( E[N10], px);                                         \
                    E[N12] = ALPHA_BLEND_64_W( E[N12], px);                                         \
                    E[N14] = px;                                                                    \
                    E[N15] = px;                                                                    \
                } else if (up) {                                                                    \
                    E[N14] = ALPHA_BLEND_192_W(E[N14], px);                                         \
                    E[N7 ] = ALPHA_BLEND_192_W(E[N7 ], px);                                         \
                    E[N10] = ALPHA_BLEND_64_W( E[N10], px);                                         \
                    E[N3 ] = ALPHA_BLEND_64_W( E[N3 ], px);                                         \
                    E[N11] = px;                                                                    \
                    E[N15] = px;                                                                    \
                } else { /* diagonal */                                                             \
                    E[N11] = ALPHA_BLEND_128_W(E[N11], px);                                         \
                    E[N14] = ALPHA_BLEND_128_W(E[N14], px);                                         \
                    E[N15] = px;                                                                    \
                }                                                                                   \
            } else {                                                                                \
                E[N15] = ALPHA_BLEND_128_W(E[N15], px);                                             \
            }                                                                                       \
        }                                                                                           \
    }                                                                                               \
} while (0)

static av_always_inline void xbr_filter(const ThreadData *td, int jobnr, int nb_jobs, int n)
{
    int x, y;
    const AVFrame *input = td->in;
    AVFrame *output = td->out;
    const uint32_t *r2y = td->rgbtoyuv;
    const int slice_start = (input->height *  jobnr   ) / nb_jobs;
    const int slice_end   = (input->height * (jobnr+1)) / nb_jobs;
    const int nl = output->linesize[0] >> 2;
    const int nl1 = nl + nl;
    const int nl2 = nl1 + nl;

    for (y = slice_start; y < slice_end; y++) {

        uint32_t *E = (uint32_t *)(output->data[0] + y * output->linesize[0] * n);
        const uint32_t *sa2 = (uint32_t *)(input->data[0] + y * input->linesize[0] - 8); /* center */
        const uint32_t *sa1 = sa2 - (input->linesize[0]>>2); /* up x1 */
        const uint32_t *sa0 = sa1 - (input->linesize[0]>>2); /* up x2 */
        const uint32_t *sa3 = sa2 + (input->linesize[0]>>2); /* down x1 */
        const uint32_t *sa4 = sa3 + (input->linesize[0]>>2); /* down x2 */

        if (y <= 1) {
            sa0 = sa1;
            if (y == 0) {
                sa0 = sa1 = sa2;
            }
        }

        if (y >= input->height - 2) {
            sa4 = sa3;
            if (y == input->height - 1) {
                sa4 = sa3 = sa2;
            }
        }

        for (x = 0; x < input->width; x++) {
            const uint32_t B1 = sa0[2];
            const uint32_t PB = sa1[2];
            const uint32_t PE = sa2[2];
            const uint32_t PH = sa3[2];
            const uint32_t H5 = sa4[2];

            const int pprev = 2 - (x > 0);
            const uint32_t A1 = sa0[pprev];
            const uint32_t PA = sa1[pprev];
            const uint32_t PD = sa2[pprev];
            const uint32_t PG = sa3[pprev];
            const uint32_t G5 = sa4[pprev];

            const int pprev2 = pprev - (x > 1);
            const uint32_t A0 = sa1[pprev2];
            const uint32_t D0 = sa2[pprev2];
            const uint32_t G0 = sa3[pprev2];

            const int pnext = 3 - (x == input->width - 1);
            const uint32_t C1 = sa0[pnext];
            const uint32_t PC = sa1[pnext];
            const uint32_t PF = sa2[pnext];
            const uint32_t PI = sa3[pnext];
            const uint32_t I5 = sa4[pnext];

            const int pnext2 = pnext + 1 - (x >= input->width - 2);
            const uint32_t C4 = sa1[pnext2];
            const uint32_t F4 = sa2[pnext2];
            const uint32_t I4 = sa3[pnext2];

            if (n == 2) {
                E[0]  = E[1]      =     // 0, 1
                E[nl] = E[nl + 1] = PE; // 2, 3

                FILT2(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, 0, 1, nl, nl+1);
                FILT2(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, nl, 0, nl+1, 1);
                FILT2(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, nl+1, nl, 1, 0);
                FILT2(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, 1, nl+1, 0, nl);
            } else if (n == 3) {
                E[0]   = E[1]     = E[2]     =     // 0, 1, 2
                E[nl]  = E[nl+1]  = E[nl+2]  =     // 3, 4, 5
                E[nl1] = E[nl1+1] = E[nl1+2] = PE; // 6, 7, 8

                FILT3(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, 0, 1, 2, nl, nl+1, nl+2, nl1, nl1+1, nl1+2);
                FILT3(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, nl1, nl, 0, nl1+1, nl+1, 1, nl1+2, nl+2, 2);
                FILT3(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, nl1+2, nl1+1, nl1, nl+2, nl+1, nl, 2, 1, 0);
                FILT3(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, 2, nl+2, nl1+2, 1, nl+1, nl1+1, 0, nl, nl1);
            } else if (n == 4) {
                E[0]   = E[1]     = E[2]     = E[3]     =     //  0,  1,  2,  3
                E[nl]  = E[nl+1]  = E[nl+2]  = E[nl+3]  =     //  4,  5,  6,  7
                E[nl1] = E[nl1+1] = E[nl1+2] = E[nl1+3] =     //  8,  9, 10, 11
                E[nl2] = E[nl2+1] = E[nl2+2] = E[nl2+3] = PE; // 12, 13, 14, 15

                FILT4(PE, PI, PH, PF, PG, PC, PD, PB, PA, G5, C4, G0, D0, C1, B1, F4, I4, H5, I5, A0, A1, nl2+3, nl2+2, nl1+3, 3, nl+3, nl1+2, nl2+1, nl2, nl1+1, nl+2, 2, 1, nl+1, nl1, nl, 0);
                FILT4(PE, PC, PF, PB, PI, PA, PH, PD, PG, I4, A1, I5, H5, A0, D0, B1, C1, F4, C4, G5, G0, 3, nl+3, 2, 0, 1, nl+2, nl1+3, nl2+3, nl1+2, nl+1, nl, nl1, nl1+1, nl2+2, nl2+1, nl2);
                FILT4(PE, PA, PB, PD, PC, PG, PF, PH, PI, C1, G0, C4, F4, G5, H5, D0, A0, B1, A1, I4, I5, 0, 1, nl, nl2, nl1, nl+1, 2, 3, nl+2, nl1+1, nl2+1, nl2+2, nl1+2, nl+3, nl1+3, nl2+3);
                FILT4(PE, PG, PD, PH, PA, PI, PB, PF, PC, A0, I5, A1, B1, I4, F4, H5, G5, D0, G0, C1, C4, nl2, nl1, nl2+1, nl2+3, nl2+2, nl1+1, nl, 0, nl+1, nl1+2, nl1+3, nl+3, nl+2, 1, 2, 3);
            }

            sa0 += 1;
            sa1 += 1;
            sa2 += 1;
            sa3 += 1;
            sa4 += 1;

            E += n;
        }
    }
}

#define XBR_FUNC(size) \
static int xbr##size##x(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{ \
    xbr_filter(arg, jobnr, nb_jobs, size); \
    return 0; \
}

XBR_FUNC(2)
XBR_FUNC(3)
XBR_FUNC(4)


static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    XBRContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    outlink->w = inlink->w * s->n;
    outlink->h = inlink->h * s->n;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_0RGB32, AV_PIX_FMT_NONE,
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    XBRContext *s = ctx->priv;
    ThreadData td;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);

    td.in = in;
    td.out = out;
    td.rgbtoyuv = s->rgbtoyuv;
    ctx->internal->execute(ctx, s->func, &td, NULL, FFMIN(inlink->h, ctx->graph->nb_threads));

    out->width  = outlink->w;
    out->height = outlink->h;

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int init(AVFilterContext *ctx)
{
    XBRContext *s = ctx->priv;
    static const xbrfunc_t xbrfuncs[] = {xbr2x, xbr3x, xbr4x};

    uint32_t c;
    int bg, rg, g;

    for (bg = -255; bg < 256; bg++) {
        for (rg = -255; rg < 256; rg++) {
            const uint32_t u = (uint32_t)((-169*rg + 500*bg)/1000) + 128;
            const uint32_t v = (uint32_t)(( 500*rg -  81*bg)/1000) + 128;
            int startg = FFMAX3(-bg, -rg, 0);
            int endg = FFMIN3(255-bg, 255-rg, 255);
            uint32_t y = (uint32_t)(( 299*rg + 1000*startg + 114*bg)/1000);
            c = bg + (rg<<16) + 0x010101 * startg;
            for (g = startg; g <= endg; g++) {
                s->rgbtoyuv[c] = ((y++) << 16) + (u << 8) + v;
                c+= 0x010101;
            }
        }
    }

    s->func = xbrfuncs[s->n - 2];
    return 0;
}

static const AVFilterPad xbr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad xbr_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_xbr = {
    .name          = "xbr",
    .description   = NULL_IF_CONFIG_SMALL("Scale the input using xBR algorithm."),
    .inputs        = xbr_inputs,
    .outputs       = xbr_outputs,
    .query_formats = query_formats,
    .priv_size     = sizeof(XBRContext),
    .priv_class    = &xbr_class,
    .init          = init,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
