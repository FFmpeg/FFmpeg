/*
 * Copyright (c) 2003 Daniel Moreno <comac AT comac DOT darktech DOT org>
 * Copyright (c) 2010 Baptiste Coudurier
 *
 * This file is part of Libav, ported from MPlayer.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * high quality 3d video denoiser, ported from MPlayer
 * libmpcodecs/vf_hqdn3d.c.
 */

#include "libavutil/pixdesc.h"
#include "avfilter.h"

typedef struct {
    int Coefs[4][512*16];
    unsigned int *Line;
    unsigned short *Frame[3];
    int hsub, vsub;
} HQDN3DContext;

static inline unsigned int LowPassMul(unsigned int PrevMul, unsigned int CurrMul, int *Coef)
{
    //    int dMul= (PrevMul&0xFFFFFF)-(CurrMul&0xFFFFFF);
    int dMul= PrevMul-CurrMul;
    unsigned int d=((dMul+0x10007FF)>>12);
    return CurrMul + Coef[d];
}

static void deNoiseTemporal(unsigned char *FrameSrc,
                            unsigned char *FrameDest,
                            unsigned short *FrameAnt,
                            int W, int H, int sStride, int dStride,
                            int *Temporal)
{
    long X, Y;
    unsigned int PixelDst;

    for (Y = 0; Y < H; Y++) {
        for (X = 0; X < W; X++) {
            PixelDst = LowPassMul(FrameAnt[X]<<8, FrameSrc[X]<<16, Temporal);
            FrameAnt[X] = ((PixelDst+0x1000007F)>>8);
            FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
        }
        FrameSrc  += sStride;
        FrameDest += dStride;
        FrameAnt += W;
    }
}

static void deNoiseSpacial(unsigned char *Frame,
                           unsigned char *FrameDest,
                           unsigned int *LineAnt,
                           int W, int H, int sStride, int dStride,
                           int *Horizontal, int *Vertical)
{
    long X, Y;
    long sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    unsigned int PixelDst;

    /* First pixel has no left nor top neighbor. */
    PixelDst = LineAnt[0] = PixelAnt = Frame[0]<<16;
    FrameDest[0]= ((PixelDst+0x10007FFF)>>16);

    /* First line has no top neighbor, only left. */
    for (X = 1; X < W; X++) {
        PixelDst = LineAnt[X] = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
        FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
    }

    for (Y = 1; Y < H; Y++) {
        unsigned int PixelAnt;
        sLineOffs += sStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs]<<16;
        PixelDst = LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
        FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)>>16);

        for (X = 1; X < W; X++) {
            unsigned int PixelDst;
            /* The rest are normal */
            PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
            PixelDst = LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
            FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)>>16);
        }
    }
}

static void deNoise(unsigned char *Frame,
                    unsigned char *FrameDest,
                    unsigned int *LineAnt,
                    unsigned short **FrameAntPtr,
                    int W, int H, int sStride, int dStride,
                    int *Horizontal, int *Vertical, int *Temporal)
{
    long X, Y;
    long sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    unsigned int PixelDst;
    unsigned short* FrameAnt=(*FrameAntPtr);

    if (!FrameAnt) {
        (*FrameAntPtr) = FrameAnt = av_malloc(W*H*sizeof(unsigned short));
        for (Y = 0; Y < H; Y++) {
            unsigned short* dst=&FrameAnt[Y*W];
            unsigned char* src=Frame+Y*sStride;
            for (X = 0; X < W; X++) dst[X]=src[X]<<8;
        }
    }

    if (!Horizontal[0] && !Vertical[0]) {
        deNoiseTemporal(Frame, FrameDest, FrameAnt,
                        W, H, sStride, dStride, Temporal);
        return;
    }
    if (!Temporal[0]) {
        deNoiseSpacial(Frame, FrameDest, LineAnt,
                       W, H, sStride, dStride, Horizontal, Vertical);
        return;
    }

    /* First pixel has no left nor top neighbor. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0]<<16;
    PixelDst = LowPassMul(FrameAnt[0]<<8, PixelAnt, Temporal);
    FrameAnt[0] = ((PixelDst+0x1000007F)>>8);
    FrameDest[0]= ((PixelDst+0x10007FFF)>>16);

    /* First line has no top neighbor. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++) {
        LineAnt[X] = PixelAnt = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
        PixelDst = LowPassMul(FrameAnt[X]<<8, PixelAnt, Temporal);
        FrameAnt[X] = ((PixelDst+0x1000007F)>>8);
        FrameDest[X]= ((PixelDst+0x10007FFF)>>16);
    }

    for (Y = 1; Y < H; Y++) {
        unsigned int PixelAnt;
        unsigned short* LinePrev=&FrameAnt[Y*W];
        sLineOffs += sStride, dLineOffs += dStride;
        /* First pixel on each line doesn't have previous pixel */
        PixelAnt = Frame[sLineOffs]<<16;
        LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
        PixelDst = LowPassMul(LinePrev[0]<<8, LineAnt[0], Temporal);
        LinePrev[0] = ((PixelDst+0x1000007F)>>8);
        FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)>>16);

        for (X = 1; X < W; X++) {
            unsigned int PixelDst;
            /* The rest are normal */
            PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
            LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
            PixelDst = LowPassMul(LinePrev[X]<<8, LineAnt[X], Temporal);
            LinePrev[X] = ((PixelDst+0x1000007F)>>8);
            FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)>>16);
        }
    }
}

static void PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil, C;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0 - 0.00001);

    for (i = -255*16; i <= 255*16; i++) {
        Simil = 1.0 - FFABS(i) / (16*255.0);
        C = pow(Simil, Gamma) * 65536.0 * i / 16.0;
        Ct[16*256+i] = lrint(C);
    }

    Ct[0] = !!Dist25;
}

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    HQDN3DContext *hqdn3d = ctx->priv;
    double LumSpac, LumTmp, ChromSpac, ChromTmp;
    double Param1, Param2, Param3, Param4;

    LumSpac   = PARAM1_DEFAULT;
    ChromSpac = PARAM2_DEFAULT;
    LumTmp    = PARAM3_DEFAULT;
    ChromTmp  = LumTmp * ChromSpac / LumSpac;

    if (args) {
        switch (sscanf(args, "%lf:%lf:%lf:%lf",
                       &Param1, &Param2, &Param3, &Param4)) {
        case 1:
            LumSpac   = Param1;
            ChromSpac = PARAM2_DEFAULT * Param1 / PARAM1_DEFAULT;
            LumTmp    = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;
            ChromTmp  = LumTmp * ChromSpac / LumSpac;
            break;
        case 2:
            LumSpac   = Param1;
            ChromSpac = Param2;
            LumTmp    = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;
            ChromTmp  = LumTmp * ChromSpac / LumSpac;
            break;
        case 3:
            LumSpac   = Param1;
            ChromSpac = Param2;
            LumTmp    = Param3;
            ChromTmp  = LumTmp * ChromSpac / LumSpac;
            break;
        case 4:
            LumSpac   = Param1;
            ChromSpac = Param2;
            LumTmp    = Param3;
            ChromTmp  = Param4;
            break;
        }
    }

    av_log(ctx, AV_LOG_INFO, "ls:%lf cs:%lf lt:%lf ct:%lf\n",
           LumSpac, ChromSpac, LumTmp, ChromTmp);
    if (LumSpac < 0 || ChromSpac < 0 || isnan(ChromTmp)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid negative value for luma or chroma spatial strength, "
               "or resulting value for chroma temporal strength is nan.\n");
        return AVERROR(EINVAL);
    }

    PrecalcCoefs(hqdn3d->Coefs[0], LumSpac);
    PrecalcCoefs(hqdn3d->Coefs[1], LumTmp);
    PrecalcCoefs(hqdn3d->Coefs[2], ChromSpac);
    PrecalcCoefs(hqdn3d->Coefs[3], ChromTmp);

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    HQDN3DContext *hqdn3d = ctx->priv;

    av_freep(&hqdn3d->Line);
    av_freep(&hqdn3d->Frame[0]);
    av_freep(&hqdn3d->Frame[1]);
    av_freep(&hqdn3d->Frame[2]);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV411P, PIX_FMT_NONE
    };

    avfilter_set_common_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;

    hqdn3d->hsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    hqdn3d->vsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

    hqdn3d->Line = av_malloc(inlink->w * sizeof(*hqdn3d->Line));
    if (!hqdn3d->Line)
        return AVERROR(ENOMEM);

    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

static void end_frame(AVFilterLink *inlink)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *inpic  = inlink ->cur_buf;
    AVFilterBufferRef *outpic = outlink->out_buf;
    int cw = inpic->video->w >> hqdn3d->hsub;
    int ch = inpic->video->h >> hqdn3d->vsub;

    deNoise(inpic->data[0], outpic->data[0],
            hqdn3d->Line, &hqdn3d->Frame[0], inpic->video->w, inpic->video->h,
            inpic->linesize[0], outpic->linesize[0],
            hqdn3d->Coefs[0],
            hqdn3d->Coefs[0],
            hqdn3d->Coefs[1]);
    deNoise(inpic->data[1], outpic->data[1],
            hqdn3d->Line, &hqdn3d->Frame[1], cw, ch,
            inpic->linesize[1], outpic->linesize[1],
            hqdn3d->Coefs[2],
            hqdn3d->Coefs[2],
            hqdn3d->Coefs[3]);
    deNoise(inpic->data[2], outpic->data[2],
            hqdn3d->Line, &hqdn3d->Frame[2], cw, ch,
            inpic->linesize[2], outpic->linesize[2],
            hqdn3d->Coefs[2],
            hqdn3d->Coefs[2],
            hqdn3d->Coefs[3]);

    avfilter_draw_slice(outlink, 0, inpic->video->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(inpic);
    avfilter_unref_buffer(outpic);
}

AVFilter avfilter_vf_hqdn3d = {
    .name          = "hqdn3d",
    .description   = NULL_IF_CONFIG_SMALL("Apply a High Quality 3D Denoiser."),

    .priv_size     = sizeof(HQDN3DContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice       = null_draw_slice,
                                    .config_props     = config_input,
                                    .end_frame        = end_frame },
                                  { .name = NULL}},

    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO },
                                  { .name = NULL}},
};
