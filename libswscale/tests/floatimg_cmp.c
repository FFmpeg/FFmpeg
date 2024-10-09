/*
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
#include <math.h>
#include <inttypes.h>
#include <float.h>

#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/lfg.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

#include "libswscale/swscale.h"

#define DEFAULT_W 96
#define DEFAULT_H 96

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P16LE,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P9LE, AV_PIX_FMT_YUV444P10LE,
    AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV444P14LE,
    AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
    AV_PIX_FMT_RGB48LE,  AV_PIX_FMT_BGR48LE,
    AV_PIX_FMT_RGBA64LE, AV_PIX_FMT_BGRA64LE,
    AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_GBRP9LE,
    AV_PIX_FMT_GBRP10LE, AV_PIX_FMT_GBRAP10LE,
    AV_PIX_FMT_GBRP12LE, AV_PIX_FMT_GBRAP12LE,
    AV_PIX_FMT_GBRP14LE,
    AV_PIX_FMT_GBRP16LE,  AV_PIX_FMT_GBRAP16LE
};

const char *usage =  "floatimg_cmp -pixel_format <pix_fmt> -size <image_size> -ref <testfile>\n";

int main(int argc, char **argv)
{
    enum AVPixelFormat inFormat = AV_PIX_FMT_NONE;
    enum AVPixelFormat dstFormat = AV_PIX_FMT_NONE;
    const AVPixFmtDescriptor *desc;
    uint8_t *ptr;
    uint32_t *in, *out;

    uint8_t *rgbIn[4]  = {NULL, NULL, NULL, NULL};
    uint8_t *rgbOut[4] = {NULL, NULL, NULL, NULL};
    int rgbStride[4];

    uint8_t *dst[4] = {NULL, NULL, NULL, NULL};
    int dstStride[4];

    int i, x, y, p, size, count;
    int res = -1;
    int w = -1;
    int h = -1;
    union av_intfloat32 v0, v1;

    double sum;
    float minimum, maximum, diff;

    SwsContext *sws = NULL;
    AVLFG rand;
    FILE *fp = NULL;

    for (i = 1; i < argc; i += 2) {
        if (argv[i][0] != '-' || i + 1 == argc)
            goto bad_option;
        if (!strcmp(argv[i], "-ref")) {
            fp = fopen(argv[i + 1], "rb");
            if (!fp) {
                fprintf(stderr, "could not open '%s'\n", argv[i + 1]);
                goto end;
            }
        } else if (!strcmp(argv[i], "-size")) {
            res = av_parse_video_size(&w, &h, argv[i + 1]);
            if (res < 0) {
                fprintf(stderr, "invalid video size %s\n",  argv[i + 1]);
                goto end;
            }
        } else if (!strcmp(argv[i], "-pixel_format")) {
            inFormat = av_get_pix_fmt(argv[i + 1]);
            if (inFormat == AV_PIX_FMT_NONE) {
                fprintf(stderr, "invalid pixel format %s\n", argv[i + 1]);
                goto end;
            }
        } else {
bad_option:
            fprintf(stderr, "%s", usage);
            fprintf(stderr, "bad option or argument missing (%s)\n", argv[i]);
            goto end;
        };
    }

    if (!fp) {
        inFormat = AV_PIX_FMT_GBRPF32LE;
        w = DEFAULT_W;
        h = DEFAULT_H;
    }

    if (w <= 0 || h <= 0) {
        fprintf(stderr, "%s", usage);
        fprintf(stderr, "invalid -video_size\n");
        goto end;
    }

    if (inFormat == AV_PIX_FMT_NONE) {
        fprintf(stderr, "%s", usage);
        fprintf(stderr, "invalid input pixel format\n");
        goto end;
    }

    desc = av_pix_fmt_desc_get(inFormat);
    if (!(desc->flags & AV_PIX_FMT_FLAG_FLOAT)) {
        fprintf(stderr, "input pixel format not floating point.\n");
        goto end;
    }

    res = av_image_fill_linesizes(rgbStride, inFormat, w);
    if (res < 0) {
        fprintf(stderr, "av_image_fill_linesizes failed\n");
        goto end;
    }
    for (p = 0; p < 4; p++) {
        rgbStride[p] = FFALIGN(rgbStride[p], 16);
        if (rgbStride[p]) {
            rgbIn[p] = av_mallocz(rgbStride[p] * h + 16);
            rgbOut[p] = av_mallocz(rgbStride[p] * h + 16);
        }
        if (rgbStride[p] && (!rgbIn[p] || !rgbOut[p])) {
            goto end;
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(pix_fmts); i++) {
        dstFormat = pix_fmts[i];
        if (fp) {
            fseek(fp, 0, SEEK_SET);
            for (p = 0; p < 4; p++) {
                if (!rgbStride[p])
                    continue;

                ptr = rgbIn[p];
                for (y = 0; y < h; y++) {
                    size = fread(ptr, 1, w*4, fp);
                    if (size != w*4) {
                        fprintf(stderr, "read error: %d\n", size);
                        goto end;
                    }
                    ptr += rgbStride[p];
                }
            }
        } else {
            // fill src with random values between 0.0 - 1.0
            av_lfg_init(&rand, 1);
            for (p = 0; p < 4; p++) {
                if (!rgbStride[p])
                    continue;

                for (y = 0; y < h; y++) {
                    in = (uint32_t*)(rgbIn[p] + y * rgbStride[p]);
                    for (x = 0; x < w; x++) {
                        v0.f =  (float)av_lfg_get(&rand)/(float)(UINT32_MAX);
                        *in++ = AV_RL32(&v0.i);
                    }
                }
            }
        }

        // setup intermediate image
        for (p = 0; p < 4; p++) {
            av_freep(&dst[p]);
        }

        res = av_image_fill_linesizes(dstStride, dstFormat, w);
        if (res < 0) {
            fprintf(stderr, "av_image_fill_linesizes failed\n");
            goto end;
        }
        for (p = 0; p < 4; p++) {
            dstStride[p] = FFALIGN(dstStride[p], 16);
            if (dstStride[p]) {
                dst[p] = av_mallocz(dstStride[p] * h + 16);
            }
            if (dstStride[p] && !dst[p]) {
                goto end;
            }
        }

        // srcFormat -> dstFormat
        sws = sws_getContext(w, h, inFormat, w, h,
                            dstFormat, SWS_BILINEAR, NULL, NULL, NULL);
        if (!sws) {
            fprintf(stderr, "Failed to get %s -> %s\n", av_get_pix_fmt_name(inFormat), av_get_pix_fmt_name(dstFormat) );
            goto end;
        }

        res = sws_scale(sws, (const uint8_t *const *)rgbIn, rgbStride, 0, h, dst, dstStride);
        if (res < 0 || res != h) {
            fprintf(stderr, "sws_scale failed\n");
            res = -1;
            goto end;
        }
        sws_freeContext(sws);

        // dstFormat -> srcFormat
        sws = sws_getContext(w, h, dstFormat, w, h,
                            inFormat, SWS_BILINEAR, NULL, NULL, NULL);
        if(!sws) {
            fprintf(stderr, "Failed to get %s -> %s\n", av_get_pix_fmt_name(dstFormat), av_get_pix_fmt_name(inFormat) );
            goto end;
        }

        res = sws_scale(sws, (const uint8_t *const *)dst, dstStride, 0, h, rgbOut, rgbStride);
        if (res < 0 || res != h) {
            fprintf(stderr, "sws_scale failed\n");
            res = -1;
            goto end;
        }
        sws_freeContext(sws);
        sws = NULL;

        minimum = FLT_MAX;
        maximum = -FLT_MAX;
        count = 0;
        sum = 0.0;

        for (p = 0; p < 4; p++) {
            if (!rgbStride[p])
                continue;

            for (y = 0; y < h; y++) {
                in = (uint32_t*)(rgbIn[p] + y * rgbStride[p]);
                out = (uint32_t*)(rgbOut[p] + y * rgbStride[p]);
                for (x = 0; x < w; x++) {
                    if (desc->flags & AV_PIX_FMT_FLAG_BE) {
                        v0.i = AV_RB32(in);
                        v1.i = AV_RB32(out);
                    } else {
                        v0.i = AV_RL32(in);
                        v1.i = AV_RL32(out);
                    }

                    diff = fabsf(v0.f - v1.f);
                    sum += diff;
                    minimum = FFMIN(minimum, diff);
                    maximum = FFMAX(maximum, diff);

                    count++;
                    in++;
                    out++;
                }
            }
        }

        fprintf(stdout, "%s -> %s -> %s\n", av_get_pix_fmt_name(inFormat), av_get_pix_fmt_name(dstFormat), av_get_pix_fmt_name(inFormat) );
        fprintf(stdout, "avg diff: %f\nmin diff: %f\nmax diff: %f\n", sum / count, minimum, maximum);
        res = 0;
    }

end:
    sws_freeContext(sws);
    for (p = 0; p < 4; p++) {
        av_freep(&rgbIn[p]);
        av_freep(&rgbOut[p]);
        av_freep(&dst[p]);
    }
    if (fp)
        fclose(fp);

    return res;
}
