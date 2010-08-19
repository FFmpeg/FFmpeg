/*
 * Copyright (C) 2003 Michael Niedermayer <michaelni@gmx.at>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdarg.h>

#undef HAVE_AV_CONFIG_H
#include "libavcore/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/avutil.h"
#include "libavutil/crc.h"
#include "libavutil/pixdesc.h"
#include "libavutil/lfg.h"
#include "swscale.h"

/* HACK Duplicated from swscale_internal.h.
 * Should be removed when a cleaner pixel format system exists. */
#define isGray(x)       (           \
           (x)==PIX_FMT_GRAY8       \
        || (x)==PIX_FMT_GRAY16BE    \
        || (x)==PIX_FMT_GRAY16LE    \
    )
#define hasChroma(x)   (!(          \
            isGray(x)               \
        || (x)==PIX_FMT_MONOBLACK   \
        || (x)==PIX_FMT_MONOWHITE   \
    ))
#define isALPHA(x)      (           \
           (x)==PIX_FMT_BGR32       \
        || (x)==PIX_FMT_BGR32_1     \
        || (x)==PIX_FMT_RGB32       \
        || (x)==PIX_FMT_RGB32_1     \
        || (x)==PIX_FMT_YUVA420P    \
    )

static uint64_t getSSD(uint8_t *src1, uint8_t *src2, int stride1, int stride2, int w, int h)
{
    int x,y;
    uint64_t ssd=0;

//printf("%d %d\n", w, h);

    for (y=0; y<h; y++) {
        for (x=0; x<w; x++) {
            int d= src1[x + y*stride1] - src2[x + y*stride2];
            ssd+= d*d;
//printf("%d", abs(src1[x + y*stride1] - src2[x + y*stride2])/26 );
        }
//printf("\n");
    }
    return ssd;
}

// test by ref -> src -> dst -> out & compare out against ref
// ref & out are YV12
static int doTest(uint8_t *ref[4], int refStride[4], int w, int h,
                  uint8_t *src[4], int srcStride[4],
                  enum PixelFormat srcFormat, enum PixelFormat dstFormat,
                  int srcW, int srcH, int dstW, int dstH, int flags)
{
    uint8_t *dst[4] = {0};
    uint8_t *out[4] = {0};
    int dstStride[4];
    int i;
    uint64_t ssdY, ssdU=0, ssdV=0, ssdA=0;
    struct SwsContext *dstContext = NULL, *outContext = NULL;
    uint32_t crc = 0;
    int res = 0;

    av_fill_image_linesizes(dstStride, dstFormat, dstW);
    for (i=0; i<4; i++) {
        /* Image buffers passed into libswscale can be allocated any way you
         * prefer, as long as they're aligned enough for the architecture, and
         * they're freed appropriately (such as using av_free for buffers
         * allocated with av_malloc). */
        /* An extra 16 bytes is being allocated because some scalers may write
         * out of bounds. */
        if (dstStride[i])
            dst[i]= av_mallocz(dstStride[i]*dstH+16);
        if (refStride[i])
            out[i]= av_mallocz(refStride[i]*h);
        if ((dstStride[i] && !dst[i]) || (refStride[i] && !out[i])) {
            perror("Malloc");
            res = -1;

            goto end;
        }
    }

    dstContext= sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat, flags, NULL, NULL, NULL);
    if (!dstContext) {
        fprintf(stderr, "Failed to get %s ---> %s\n",
                av_pix_fmt_descriptors[srcFormat].name,
                av_pix_fmt_descriptors[dstFormat].name);
        res = -1;

        goto end;
    }
    outContext= sws_getContext(dstW, dstH, dstFormat, w, h, PIX_FMT_YUVA420P, flags, NULL, NULL, NULL);
    if (!outContext) {
        fprintf(stderr, "Failed to get %s ---> %s\n",
                av_pix_fmt_descriptors[dstFormat].name,
                av_pix_fmt_descriptors[PIX_FMT_YUVA420P].name);
        res = -1;

        goto end;
    }
//    printf("test %X %X %X -> %X %X %X\n", (int)ref[0], (int)ref[1], (int)ref[2],
//        (int)src[0], (int)src[1], (int)src[2]);

    printf(" %s %dx%d -> %s %3dx%3d flags=%2d",
           av_pix_fmt_descriptors[srcFormat].name, srcW, srcH,
           av_pix_fmt_descriptors[dstFormat].name, dstW, dstH,
           flags);
    fflush(stdout);

    sws_scale(dstContext, src, srcStride, 0, srcH, dst, dstStride);
    sws_scale(outContext, dst, dstStride, 0, dstH, out, refStride);

    for (i = 0; i < 4 && dstStride[i]; i++) {
        crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), crc, dst[i], dstStride[i] * dstH);
    }

    ssdY= getSSD(ref[0], out[0], refStride[0], refStride[0], w, h);
    if (hasChroma(srcFormat) && hasChroma(dstFormat)) {
        //FIXME check that output is really gray
        ssdU= getSSD(ref[1], out[1], refStride[1], refStride[1], (w+1)>>1, (h+1)>>1);
        ssdV= getSSD(ref[2], out[2], refStride[2], refStride[2], (w+1)>>1, (h+1)>>1);
    }
    if (isALPHA(srcFormat) && isALPHA(dstFormat))
        ssdA= getSSD(ref[3], out[3], refStride[3], refStride[3], w, h);

    ssdY/= w*h;
    ssdU/= w*h/4;
    ssdV/= w*h/4;
    ssdA/= w*h;

    printf(" CRC=%08x SSD=%5"PRId64",%5"PRId64",%5"PRId64",%5"PRId64"\n",
           crc, ssdY, ssdU, ssdV, ssdA);

end:

    sws_freeContext(dstContext);
    sws_freeContext(outContext);

    for (i=0; i<4; i++) {
        if (dstStride[i])
            av_free(dst[i]);
        if (refStride[i])
            av_free(out[i]);
    }

    return res;
}

static void selfTest(uint8_t *ref[4], int refStride[4], int w, int h)
{
    const int flags[] = { SWS_FAST_BILINEAR,
                          SWS_BILINEAR, SWS_BICUBIC,
                          SWS_X       , SWS_POINT  , SWS_AREA, 0 };
    const int srcW = w;
    const int srcH = h;
    const int dstW[] = { srcW - srcW/3, srcW, srcW + srcW/3, 0 };
    const int dstH[] = { srcH - srcH/3, srcH, srcH + srcH/3, 0 };
    enum PixelFormat srcFormat, dstFormat;

    for (srcFormat = 0; srcFormat < PIX_FMT_NB; srcFormat++) {
        if (!sws_isSupportedInput(srcFormat) || !sws_isSupportedOutput(srcFormat))
            continue;

        for (dstFormat = 0; dstFormat < PIX_FMT_NB; dstFormat++) {
            int i, j, k;
            int res = 0;

            if (!sws_isSupportedInput(dstFormat) || !sws_isSupportedOutput(dstFormat))
                continue;

            printf("%s -> %s\n",
                   av_pix_fmt_descriptors[srcFormat].name,
                   av_pix_fmt_descriptors[dstFormat].name);
            fflush(stdout);

            for (k = 0; flags[k] && !res; k++) {
                struct SwsContext *srcContext = NULL;
                uint8_t *src[4] = {0};
                int srcStride[4];
                int p;
                av_fill_image_linesizes(srcStride, srcFormat, srcW);
                for (p = 0; p < 4; p++) {
                    if (srcStride[p])
                        src[p] = av_mallocz(srcStride[p]*srcH+16);
                    if (srcStride[p] && !src[p]) {
                        perror("Malloc");
                        return;
                    }
                }
                srcContext = sws_getContext(w, h, PIX_FMT_YUVA420P, srcW, srcH,
                                            srcFormat, flags[k], NULL, NULL, NULL);
                if (!srcContext) {
                   fprintf(stderr, "Failed to get %s ---> %s\n",
                            av_pix_fmt_descriptors[PIX_FMT_YUVA420P].name,
                            av_pix_fmt_descriptors[srcFormat].name);
                   return;
                }
                sws_scale(srcContext, ref, refStride, 0, h, src, srcStride);

                for (i = 0; dstW[i] && !res; i++)
                    for (j = 0; dstH[j] && !res; j++)
                        res = doTest(ref, refStride, w, h, src, srcStride,
                                     srcFormat, dstFormat,
                                     srcW, srcH, dstW[i], dstH[j], flags[k]);

                sws_freeContext(srcContext);
                for (p = 0; p < 4; p++)
                    if (srcStride[p])
                        av_free(src[p]);
            }
        }
    }
}

#define W 96
#define H 96

int main(int argc, char **argv)
{
    uint8_t *rgb_data = av_malloc (W*H*4);
    uint8_t *rgb_src[3]= {rgb_data, NULL, NULL};
    int rgb_stride[3]={4*W, 0, 0};
    uint8_t *data = av_malloc (4*W*H);
    uint8_t *src[4]= {data, data+W*H, data+W*H*2, data+W*H*3};
    int stride[4]={W, W, W, W};
    int x, y;
    struct SwsContext *sws;
    AVLFG rand;

    if (!rgb_data || !data)
        return -1;

    sws= sws_getContext(W/12, H/12, PIX_FMT_RGB32, W, H, PIX_FMT_YUVA420P, SWS_BILINEAR, NULL, NULL, NULL);

    av_lfg_init(&rand, 1);

    for (y=0; y<H; y++) {
        for (x=0; x<W*4; x++) {
            rgb_data[ x + y*4*W]= av_lfg_get(&rand);
        }
    }
    sws_scale(sws, rgb_src, rgb_stride, 0, H, src, stride);
    sws_freeContext(sws);
    av_free(rgb_data);

    selfTest(src, stride, W, H);
    av_free(data);

    return 0;
}
