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
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    int coefs[4][512*16];
    uint32_t *line;
    uint16_t *frame_prev[3];
    int hsub, vsub;
} HQDN3DContext;

static inline uint32_t lowpass(unsigned int prev, unsigned int cur, int *coef)
{
    int dmul = prev-cur;
    unsigned int d = (dmul+0x10007FF)>>12; // 0x1000 to convert to unsigned, 7FF for rounding
    return cur + coef[d];
}

static void denoise_temporal(uint8_t *src, uint8_t *dst,
                             uint16_t *frame_ant,
                             int w, int h, int sstride, int dstride,
                             int *temporal)
{
    long x, y;
    uint32_t pixel;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            pixel = lowpass(frame_ant[x]<<8, src[x]<<16, temporal);
            frame_ant[x] = ((pixel+0x1000007F)>>8);
            dst[x]= ((pixel+0x10007FFF)>>16);
        }
        src += sstride;
        dst += dstride;
        frame_ant += w;
    }
}

static void denoise_spatial(uint8_t *src, uint8_t *dst,
                            uint32_t *line_ant,
                            int w, int h, int sstride, int dstride,
                            int *horizontal, int *vertical)
{
    long x, y;
    long sline_offs = 0, dline_offs = 0;
    uint32_t pixel_ant;
    uint32_t pixel;

    /* First pixel has no left nor top neighbor. */
    pixel = line_ant[0] = pixel_ant = src[0]<<16;
    dst[0]= ((pixel+0x10007FFF)>>16);

    /* First line has no top neighbor, only left. */
    for (x = 1; x < w; x++) {
        pixel = line_ant[x] = lowpass(pixel_ant, src[x]<<16, horizontal);
        dst[x]= ((pixel+0x10007FFF)>>16);
    }

    for (y = 1; y < h; y++) {
        uint32_t pixel_ant;
        sline_offs += sstride, dline_offs += dstride;
        /* First pixel on each line doesn't have previous pixel */
        pixel_ant = src[sline_offs]<<16;
        pixel = line_ant[0] = lowpass(line_ant[0], pixel_ant, vertical);
        dst[dline_offs]= ((pixel+0x10007FFF)>>16);

        for (x = 1; x < w; x++) {
            uint32_t pixel;
            /* The rest are normal */
            pixel_ant = lowpass(pixel_ant, src[sline_offs+x]<<16, horizontal);
            pixel = line_ant[x] = lowpass(line_ant[x], pixel_ant, vertical);
            dst[dline_offs+x]= ((pixel+0x10007FFF)>>16);
        }
    }
}

static void denoise(uint8_t *src, uint8_t *dst,
                    uint32_t *line_ant, uint16_t **frame_ant_ptr,
                    int w, int h, int sstride, int dstride,
                    int *horizontal, int *vertical, int *temporal)
{
    long x, y;
    long sline_offs = 0, dline_offs = 0;
    uint32_t pixel_ant;
    uint32_t pixel;
    uint16_t *frame_ant = *frame_ant_ptr;

    if (!frame_ant) {
        *frame_ant_ptr = frame_ant = av_malloc(w*h*sizeof(uint16_t));
        for (y = 0; y < h; y++) {
            uint16_t *frame_dst = frame_ant+y*w;
            uint8_t *frame_src = src+y*sstride;
            for (x = 0; x < w; x++)
                frame_dst[x] = frame_src[x]<<8;
        }
    }

    if (!horizontal[0] && !vertical[0]) {
        denoise_temporal(src, dst, frame_ant,
                         w, h, sstride, dstride, temporal);
        return;
    }
    if (!temporal[0]) {
        denoise_spatial(src, dst, line_ant,
                        w, h, sstride, dstride, horizontal, vertical);
        return;
    }

    /* First pixel has no left nor top neighbor. Only previous frame */
    line_ant[0] = pixel_ant = src[0]<<16;
    pixel = lowpass(frame_ant[0]<<8, pixel_ant, temporal);
    frame_ant[0] = ((pixel+0x1000007F)>>8);
    dst[0]= ((pixel+0x10007FFF)>>16);

    /* First line has no top neighbor. Only left one for each pixel and
     * last frame */
    for (x = 1; x < w; x++) {
        line_ant[x] = pixel_ant = lowpass(pixel_ant, src[x]<<16, horizontal);
        pixel = lowpass(frame_ant[x]<<8, pixel_ant, temporal);
        frame_ant[x] = ((pixel+0x1000007F)>>8);
        dst[x]= ((pixel+0x10007FFF)>>16);
    }

    for (y = 1; y < h; y++) {
        uint32_t pixel_ant;
        uint16_t *line_prev = frame_ant+y*w;
        sline_offs += sstride, dline_offs += dstride;
        /* First pixel on each line doesn't have previous pixel */
        pixel_ant = src[sline_offs]<<16;
        line_ant[0] = lowpass(line_ant[0], pixel_ant, vertical);
        pixel = lowpass(line_prev[0]<<8, line_ant[0], temporal);
        line_prev[0] = ((pixel+0x1000007F)>>8);
        dst[dline_offs]= ((pixel+0x10007FFF)>>16);

        for (x = 1; x < w; x++) {
            uint32_t pixel;
            /* The rest are normal */
            pixel_ant = lowpass(pixel_ant, src[sline_offs+x]<<16, horizontal);
            line_ant[x] = lowpass(line_ant[x], pixel_ant, vertical);
            pixel = lowpass(line_prev[x]<<8, line_ant[x], temporal);
            line_prev[x] = ((pixel+0x1000007F)>>8);
            dst[dline_offs+x]= ((pixel+0x10007FFF)>>16);
        }
    }
}

static void precalc_coefs(int *ct, double dist25)
{
    int i;
    double gamma, simil, C;

    gamma = log(0.25) / log(1.0 - dist25/255.0 - 0.00001);

    for (i = -255*16; i <= 255*16; i++) {
        simil = 1.0 - FFABS(i) / (16*255.0);
        C = pow(simil, gamma) * 65536.0 * i / 16.0;
        ct[16*256+i] = lrint(C);
    }

    ct[0] = !!dist25;
}

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0

static int init(AVFilterContext *ctx, const char *args)
{
    HQDN3DContext *hqdn3d = ctx->priv;
    double lum_spac, lum_tmp, chrom_spac, chrom_tmp;
    double param1, param2, param3, param4;

    lum_spac   = PARAM1_DEFAULT;
    chrom_spac = PARAM2_DEFAULT;
    lum_tmp    = PARAM3_DEFAULT;
    chrom_tmp  = lum_tmp * chrom_spac / lum_spac;

    if (args) {
        switch (sscanf(args, "%lf:%lf:%lf:%lf",
                       &param1, &param2, &param3, &param4)) {
        case 1:
            lum_spac   = param1;
            chrom_spac = PARAM2_DEFAULT * param1 / PARAM1_DEFAULT;
            lum_tmp    = PARAM3_DEFAULT * param1 / PARAM1_DEFAULT;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 2:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = PARAM3_DEFAULT * param1 / PARAM1_DEFAULT;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 3:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = param3;
            chrom_tmp  = lum_tmp * chrom_spac / lum_spac;
            break;
        case 4:
            lum_spac   = param1;
            chrom_spac = param2;
            lum_tmp    = param3;
            chrom_tmp  = param4;
            break;
        }
    }

    av_log(ctx, AV_LOG_VERBOSE, "ls:%lf cs:%lf lt:%lf ct:%lf\n",
           lum_spac, chrom_spac, lum_tmp, chrom_tmp);
    if (lum_spac < 0 || chrom_spac < 0 || isnan(chrom_tmp)) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid negative value for luma or chroma spatial strength, "
               "or resulting value for chroma temporal strength is nan.\n");
        return AVERROR(EINVAL);
    }

    precalc_coefs(hqdn3d->coefs[0], lum_spac);
    precalc_coefs(hqdn3d->coefs[1], lum_tmp);
    precalc_coefs(hqdn3d->coefs[2], chrom_spac);
    precalc_coefs(hqdn3d->coefs[3], chrom_tmp);

    return 0;
}

static void uninit(AVFilterContext *ctx)
{
    HQDN3DContext *hqdn3d = ctx->priv;

    av_freep(&hqdn3d->line);
    av_freep(&hqdn3d->frame_prev[0]);
    av_freep(&hqdn3d->frame_prev[1]);
    av_freep(&hqdn3d->frame_prev[2]);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P, PIX_FMT_YUV422P, PIX_FMT_YUV411P, PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;

    hqdn3d->hsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_w;
    hqdn3d->vsub = av_pix_fmt_descriptors[inlink->format].log2_chroma_h;

    hqdn3d->line = av_malloc(inlink->w * sizeof(*hqdn3d->line));
    if (!hqdn3d->line)
        return AVERROR(ENOMEM);

    return 0;
}

static int null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
    return 0;
}

static int end_frame(AVFilterLink *inlink)
{
    HQDN3DContext *hqdn3d = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *inpic  = inlink ->cur_buf;
    AVFilterBufferRef *outpic = outlink->out_buf;
    int cw = inpic->video->w >> hqdn3d->hsub;
    int ch = inpic->video->h >> hqdn3d->vsub;
    int ret;

    denoise(inpic->data[0], outpic->data[0],
            hqdn3d->line, &hqdn3d->frame_prev[0], inpic->video->w, inpic->video->h,
            inpic->linesize[0], outpic->linesize[0],
            hqdn3d->coefs[0],
            hqdn3d->coefs[0],
            hqdn3d->coefs[1]);
    denoise(inpic->data[1], outpic->data[1],
            hqdn3d->line, &hqdn3d->frame_prev[1], cw, ch,
            inpic->linesize[1], outpic->linesize[1],
            hqdn3d->coefs[2],
            hqdn3d->coefs[2],
            hqdn3d->coefs[3]);
    denoise(inpic->data[2], outpic->data[2],
            hqdn3d->line, &hqdn3d->frame_prev[2], cw, ch,
            inpic->linesize[2], outpic->linesize[2],
            hqdn3d->coefs[2],
            hqdn3d->coefs[2],
            hqdn3d->coefs[3]);

    if ((ret = ff_draw_slice(outlink, 0, inpic->video->h, 1)) < 0 ||
        (ret = ff_end_frame(outlink)) < 0)
        return ret;
    return 0;
}

AVFilter avfilter_vf_hqdn3d = {
    .name          = "hqdn3d",
    .description   = NULL_IF_CONFIG_SMALL("Apply a High Quality 3D Denoiser."),

    .priv_size     = sizeof(HQDN3DContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO,
                                          .draw_slice       = null_draw_slice,
                                          .config_props     = config_input,
                                          .end_frame        = end_frame },
                                        { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name             = "default",
                                          .type             = AVMEDIA_TYPE_VIDEO },
                                        { .name = NULL}},
};
