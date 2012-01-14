/*
 * Copyright (C) 2003-2011 Michael Niedermayer <michaelni@gmx.at>
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
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/avutil.h"
#include "libavutil/crc.h"
#include "libavutil/pixdesc.h"
#include "libavutil/lfg.h"
#include "swscale.h"

/* HACK Duplicated from swscale_internal.h.
 * Should be removed when a cleaner pixel format system exists. */
#define isGray(x)                      \
    ((x) == PIX_FMT_GRAY8       ||     \
     (x) == PIX_FMT_Y400A       ||     \
     (x) == PIX_FMT_GRAY16BE    ||     \
     (x) == PIX_FMT_GRAY16LE)
#define hasChroma(x)                   \
    (!(isGray(x)                ||     \
       (x) == PIX_FMT_MONOBLACK ||     \
       (x) == PIX_FMT_MONOWHITE))
#define isALPHA(x)                     \
    ((x) == PIX_FMT_BGR32   ||         \
     (x) == PIX_FMT_BGR32_1 ||         \
     (x) == PIX_FMT_RGB32   ||         \
     (x) == PIX_FMT_RGB32_1 ||         \
     (x) == PIX_FMT_YUVA420P)

static uint64_t getSSD(uint8_t *src1, uint8_t *src2, int stride1,
                       int stride2, int w, int h)
{
    int x, y;
    uint64_t ssd = 0;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int d = src1[x + y * stride1] - src2[x + y * stride2];
            ssd += d * d;
        }
    }
    return ssd;
}

struct Results {
    uint64_t ssdY;
    uint64_t ssdU;
    uint64_t ssdV;
    uint64_t ssdA;
    uint32_t crc;
};

// test by ref -> src -> dst -> out & compare out against ref
// ref & out are YV12
static int doTest(uint8_t *ref[4], int refStride[4], int w, int h,
                  enum PixelFormat srcFormat, enum PixelFormat dstFormat,
                  int srcW, int srcH, int dstW, int dstH, int flags,
                  struct Results *r)
{
    static enum PixelFormat cur_srcFormat;
    static int cur_srcW, cur_srcH;
    static uint8_t *src[4];
    static int srcStride[4];
    uint8_t *dst[4] = { 0 };
    uint8_t *out[4] = { 0 };
    int dstStride[4];
    int i;
    uint64_t ssdY, ssdU = 0, ssdV = 0, ssdA = 0;
    struct SwsContext *dstContext = NULL, *outContext = NULL;
    uint32_t crc = 0;
    int res      = 0;

    if (cur_srcFormat != srcFormat || cur_srcW != srcW || cur_srcH != srcH) {
        struct SwsContext *srcContext = NULL;
        int p;

        for (p = 0; p < 4; p++)
            av_freep(&src[p]);

        av_image_fill_linesizes(srcStride, srcFormat, srcW);
        for (p = 0; p < 4; p++) {
            srcStride[p] = FFALIGN(srcStride[p], 16);
            if (srcStride[p])
                src[p] = av_mallocz(srcStride[p] * srcH + 16);
            if (srcStride[p] && !src[p]) {
                perror("Malloc");
                res = -1;
                goto end;
            }
        }
        srcContext = sws_getContext(w, h, PIX_FMT_YUVA420P, srcW, srcH,
                                    srcFormat, SWS_BILINEAR, NULL, NULL, NULL);
        if (!srcContext) {
            fprintf(stderr, "Failed to get %s ---> %s\n",
                    av_pix_fmt_descriptors[PIX_FMT_YUVA420P].name,
                    av_pix_fmt_descriptors[srcFormat].name);
            res = -1;
            goto end;
        }
        sws_scale(srcContext, ref, refStride, 0, h, src, srcStride);
        sws_freeContext(srcContext);

        cur_srcFormat = srcFormat;
        cur_srcW      = srcW;
        cur_srcH      = srcH;
    }

    av_image_fill_linesizes(dstStride, dstFormat, dstW);
    for (i = 0; i < 4; i++) {
        /* Image buffers passed into libswscale can be allocated any way you
         * prefer, as long as they're aligned enough for the architecture, and
         * they're freed appropriately (such as using av_free for buffers
         * allocated with av_malloc). */
        /* An extra 16 bytes is being allocated because some scalers may write
         * out of bounds. */
        dstStride[i] = FFALIGN(dstStride[i], 16);
        if (dstStride[i])
            dst[i] = av_mallocz(dstStride[i] * dstH + 16);
        if (dstStride[i] && !dst[i]) {
            perror("Malloc");
            res = -1;

            goto end;
        }
    }

    dstContext = sws_getContext(srcW, srcH, srcFormat, dstW, dstH, dstFormat,
                                flags, NULL, NULL, NULL);
    if (!dstContext) {
        fprintf(stderr, "Failed to get %s ---> %s\n",
                av_pix_fmt_descriptors[srcFormat].name,
                av_pix_fmt_descriptors[dstFormat].name);
        res = -1;
        goto end;
    }

    printf(" %s %dx%d -> %s %3dx%3d flags=%2d",
           av_pix_fmt_descriptors[srcFormat].name, srcW, srcH,
           av_pix_fmt_descriptors[dstFormat].name, dstW, dstH,
           flags);
    fflush(stdout);

    sws_scale(dstContext, src, srcStride, 0, srcH, dst, dstStride);

    for (i = 0; i < 4 && dstStride[i]; i++)
        crc = av_crc(av_crc_get_table(AV_CRC_32_IEEE), crc, dst[i],
                     dstStride[i] * dstH);

    if (r && crc == r->crc) {
        ssdY = r->ssdY;
        ssdU = r->ssdU;
        ssdV = r->ssdV;
        ssdA = r->ssdA;
    } else {
        for (i = 0; i < 4; i++) {
            refStride[i] = FFALIGN(refStride[i], 16);
            if (refStride[i])
                out[i] = av_mallocz(refStride[i] * h);
            if (refStride[i] && !out[i]) {
                perror("Malloc");
                res = -1;
                goto end;
            }
        }
        outContext = sws_getContext(dstW, dstH, dstFormat, w, h,
                                    PIX_FMT_YUVA420P, SWS_BILINEAR,
                                    NULL, NULL, NULL);
        if (!outContext) {
            fprintf(stderr, "Failed to get %s ---> %s\n",
                    av_pix_fmt_descriptors[dstFormat].name,
                    av_pix_fmt_descriptors[PIX_FMT_YUVA420P].name);
            res = -1;
            goto end;
        }
        sws_scale(outContext, dst, dstStride, 0, dstH, out, refStride);

        ssdY = getSSD(ref[0], out[0], refStride[0], refStride[0], w, h);
        if (hasChroma(srcFormat) && hasChroma(dstFormat)) {
            //FIXME check that output is really gray
            ssdU = getSSD(ref[1], out[1], refStride[1], refStride[1],
                          (w + 1) >> 1, (h + 1) >> 1);
            ssdV = getSSD(ref[2], out[2], refStride[2], refStride[2],
                          (w + 1) >> 1, (h + 1) >> 1);
        }
        if (isALPHA(srcFormat) && isALPHA(dstFormat))
            ssdA = getSSD(ref[3], out[3], refStride[3], refStride[3], w, h);

        ssdY /= w * h;
        ssdU /= w * h / 4;
        ssdV /= w * h / 4;
        ssdA /= w * h;

        sws_freeContext(outContext);

        for (i = 0; i < 4; i++)
            if (refStride[i])
                av_free(out[i]);
    }

    printf(" CRC=%08x SSD=%5"PRId64 ",%5"PRId64 ",%5"PRId64 ",%5"PRId64 "\n",
           crc, ssdY, ssdU, ssdV, ssdA);

end:
    sws_freeContext(dstContext);

    for (i = 0; i < 4; i++)
        if (dstStride[i])
            av_free(dst[i]);

    return res;
}

static void selfTest(uint8_t *ref[4], int refStride[4], int w, int h,
                     enum PixelFormat srcFormat_in,
                     enum PixelFormat dstFormat_in)
{
    const int flags[] = { SWS_FAST_BILINEAR, SWS_BILINEAR, SWS_BICUBIC,
                          SWS_X, SWS_POINT, SWS_AREA, 0 };
    const int srcW   = w;
    const int srcH   = h;
    const int dstW[] = { srcW - srcW / 3, srcW, srcW + srcW / 3, 0 };
    const int dstH[] = { srcH - srcH / 3, srcH, srcH + srcH / 3, 0 };
    enum PixelFormat srcFormat, dstFormat;

    for (srcFormat = srcFormat_in != PIX_FMT_NONE ? srcFormat_in : 0;
         srcFormat < PIX_FMT_NB; srcFormat++) {
        if (!sws_isSupportedInput(srcFormat) ||
            !sws_isSupportedOutput(srcFormat))
            continue;

        for (dstFormat = dstFormat_in != PIX_FMT_NONE ? dstFormat_in : 0;
             dstFormat < PIX_FMT_NB; dstFormat++) {
            int i, j, k;
            int res = 0;

            if (!sws_isSupportedInput(dstFormat) ||
                !sws_isSupportedOutput(dstFormat))
                continue;

            printf("%s -> %s\n",
                   av_pix_fmt_descriptors[srcFormat].name,
                   av_pix_fmt_descriptors[dstFormat].name);
            fflush(stdout);

            for (k = 0; flags[k] && !res; k++)
                for (i = 0; dstW[i] && !res; i++)
                    for (j = 0; dstH[j] && !res; j++)
                        res = doTest(ref, refStride, w, h,
                                     srcFormat, dstFormat,
                                     srcW, srcH, dstW[i], dstH[j], flags[k],
                                     NULL);
            if (dstFormat_in != PIX_FMT_NONE)
                break;
        }
        if (srcFormat_in != PIX_FMT_NONE)
            break;
    }
}

static int fileTest(uint8_t *ref[4], int refStride[4], int w, int h, FILE *fp,
                    enum PixelFormat srcFormat_in,
                    enum PixelFormat dstFormat_in)
{
    char buf[256];

    while (fgets(buf, sizeof(buf), fp)) {
        struct Results r;
        enum PixelFormat srcFormat;
        char srcStr[12];
        int srcW, srcH;
        enum PixelFormat dstFormat;
        char dstStr[12];
        int dstW, dstH;
        int flags;
        int ret;

        ret = sscanf(buf,
                     " %12s %dx%d -> %12s %dx%d flags=%d CRC=%x"
                     " SSD=%"PRId64 ", %"PRId64 ", %"PRId64 ", %"PRId64 "\n",
                     srcStr, &srcW, &srcH, dstStr, &dstW, &dstH,
                     &flags, &r.crc, &r.ssdY, &r.ssdU, &r.ssdV, &r.ssdA);
        if (ret != 12) {
            srcStr[0] = dstStr[0] = 0;
            ret       = sscanf(buf, "%12s -> %12s\n", srcStr, dstStr);
        }

        srcFormat = av_get_pix_fmt(srcStr);
        dstFormat = av_get_pix_fmt(dstStr);

        if (srcFormat == PIX_FMT_NONE || dstFormat == PIX_FMT_NONE) {
            fprintf(stderr, "malformed input file\n");
            return -1;
        }
        if ((srcFormat_in != PIX_FMT_NONE && srcFormat_in != srcFormat) ||
            (dstFormat_in != PIX_FMT_NONE && dstFormat_in != dstFormat))
            continue;
        if (ret != 12) {
            printf("%s", buf);
            continue;
        }

        doTest(ref, refStride, w, h,
               srcFormat, dstFormat,
               srcW, srcH, dstW, dstH, flags,
               &r);
    }

    return 0;
}

#define W 96
#define H 96

int main(int argc, char **argv)
{
    enum PixelFormat srcFormat = PIX_FMT_NONE;
    enum PixelFormat dstFormat = PIX_FMT_NONE;
    uint8_t *rgb_data   = av_malloc(W * H * 4);
    uint8_t *rgb_src[4] = { rgb_data, NULL, NULL, NULL };
    int rgb_stride[4]   = { 4 * W, 0, 0, 0 };
    uint8_t *data       = av_malloc(4 * W * H);
    uint8_t *src[4]     = { data, data + W * H, data + W * H * 2, data + W * H * 3 };
    int stride[4]       = { W, W, W, W };
    int x, y;
    struct SwsContext *sws;
    AVLFG rand;
    int res = -1;
    int i;

    if (!rgb_data || !data)
        return -1;

    sws = sws_getContext(W / 12, H / 12, PIX_FMT_RGB32, W, H,
                         PIX_FMT_YUVA420P, SWS_BILINEAR, NULL, NULL, NULL);

    av_lfg_init(&rand, 1);

    for (y = 0; y < H; y++)
        for (x = 0; x < W * 4; x++)
            rgb_data[ x + y * 4 * W] = av_lfg_get(&rand);
    sws_scale(sws, rgb_src, rgb_stride, 0, H, src, stride);
    sws_freeContext(sws);
    av_free(rgb_data);

    for (i = 1; i < argc; i += 2) {
        if (argv[i][0] != '-' || i + 1 == argc)
            goto bad_option;
        if (!strcmp(argv[i], "-ref")) {
            FILE *fp = fopen(argv[i + 1], "r");
            if (!fp) {
                fprintf(stderr, "could not open '%s'\n", argv[i + 1]);
                goto error;
            }
            res = fileTest(src, stride, W, H, fp, srcFormat, dstFormat);
            fclose(fp);
            goto end;
        } else if (!strcmp(argv[i], "-src")) {
            srcFormat = av_get_pix_fmt(argv[i + 1]);
            if (srcFormat == PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                return -1;
            }
        } else if (!strcmp(argv[i], "-dst")) {
            dstFormat = av_get_pix_fmt(argv[i + 1]);
            if (dstFormat == PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                return -1;
            }
        } else {
bad_option:
            fprintf(stderr, "bad option or argument missing (%s)\n", argv[i]);
            goto error;
        }
    }

    selfTest(src, stride, W, H, srcFormat, dstFormat);
end:
    res = 0;
error:
    av_free(data);

    return res;
}
