/*
 * Fish Detector Hook
 * Copyright (c) 2002 Philip Gladstone
 *
 * This file implements a fish detector. It is used to see when a
 * goldfish passes in front of the camera. It does this by counting
 * the number of input pixels that fall within a particular HSV
 * range.
 *
 * It takes a multitude of arguments:
 *
 * -h <num>-<num>    the range of H values that are fish
 * -s <num>-<num>    the range of S values that are fish
 * -v <num>-<num>    the range of V values that are fish
 * -z                zap all non-fish values to black
 * -l <num>          limit the number of saved files to <num>
 * -i <num>          only check frames every <num> seconds
 * -t <num>          the threshold for the amount of fish pixels (range 0-1)
 * -d                turn debugging on
 * -D <directory>    where to put the fish images
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
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <dirent.h>

#include "libavformat/avformat.h"
#include "libavformat/framehook.h"
#include "libavcodec/dsputil.h"
#include "libswscale/swscale.h"
#undef fprintf

static int sws_flags = SWS_BICUBIC;

#define SCALEBITS 10
#define ONE_HALF  (1 << (SCALEBITS - 1))
#define FIX(x)    ((int) ((x) * (1<<SCALEBITS) + 0.5))

#define YUV_TO_RGB1_CCIR(cb1, cr1)\
{\
    cb = (cb1) - 128;\
    cr = (cr1) - 128;\
    r_add = FIX(1.40200*255.0/224.0) * cr + ONE_HALF;\
    g_add = - FIX(0.34414*255.0/224.0) * cb - FIX(0.71414*255.0/224.0) * cr + \
                    ONE_HALF;\
    b_add = FIX(1.77200*255.0/224.0) * cb + ONE_HALF;\
}

#define YUV_TO_RGB2_CCIR(r, g, b, y1)\
{\
    yt = ((y1) - 16) * FIX(255.0/219.0);\
    r = cm[(yt + r_add) >> SCALEBITS];\
    g = cm[(yt + g_add) >> SCALEBITS];\
    b = cm[(yt + b_add) >> SCALEBITS];\
}




typedef struct {
    int h;  /* 0 .. 360 */
    int s;  /* 0 .. 255 */
    int v;  /* 0 .. 255 */
} HSV;

typedef struct {
    int zapping;
    int threshold;
    HSV dark, bright;
    char *dir;
    int file_limit;
    int debug;
    int min_interval;
    int64_t next_pts;
    int inset;
    int min_width;
    struct SwsContext *toRGB_convert_ctx;
} ContextInfo;

static void dorange(const char *s, int *first, int *second, int maxval)
{
    sscanf(s, "%d-%d", first, second);
    if (*first > maxval)
        *first = maxval;
    if (*second > maxval)
        *second = maxval;
}

void Release(void *ctx)
{
    ContextInfo *ci;
    ci = (ContextInfo *) ctx;

    if (ctx) {
        sws_freeContext(ci->toRGB_convert_ctx);
        av_free(ctx);
    }
}

int Configure(void **ctxp, int argc, char *argv[])
{
    ContextInfo *ci;
    int c;

    *ctxp = av_mallocz(sizeof(ContextInfo));
    ci = (ContextInfo *) *ctxp;

    optind = 1;

    ci->dir = av_strdup("/tmp");
    ci->threshold = 100;
    ci->file_limit = 100;
    ci->min_interval = 1000000;
    ci->inset = 10;     /* Percent */

    while ((c = getopt(argc, argv, "w:i:dh:s:v:zl:t:D:")) > 0) {
        switch (c) {
            case 'h':
                dorange(optarg, &ci->dark.h, &ci->bright.h, 360);
                break;
            case 's':
                dorange(optarg, &ci->dark.s, &ci->bright.s, 255);
                break;
            case 'v':
                dorange(optarg, &ci->dark.v, &ci->bright.v, 255);
                break;
            case 'z':
                ci->zapping = 1;
                break;
            case 'l':
                ci->file_limit = atoi(optarg);
                break;
            case 'i':
                ci->min_interval = 1000000 * atof(optarg);
                break;
            case 't':
                ci->threshold = atof(optarg) * 1000;
                if (ci->threshold > 1000 || ci->threshold < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Invalid threshold value '%s' (range is 0-1)\n", optarg);
                    return -1;
                }
                break;
            case 'w':
                ci->min_width = atoi(optarg);
                break;
            case 'd':
                ci->debug++;
                break;
            case 'D':
                ci->dir = av_strdup(optarg);
                break;
            default:
                av_log(NULL, AV_LOG_ERROR, "Unrecognized argument '%s'\n", argv[optind]);
                return -1;
        }
    }

    av_log(NULL, AV_LOG_INFO, "Fish detector configured:\n");
    av_log(NULL, AV_LOG_INFO, "    HSV range: %d,%d,%d - %d,%d,%d\n",
                        ci->dark.h,
                        ci->dark.s,
                        ci->dark.v,
                        ci->bright.h,
                        ci->bright.s,
                        ci->bright.v);
    av_log(NULL, AV_LOG_INFO, "    Threshold is %d%% pixels\n", ci->threshold / 10);


    return 0;
}

static void get_hsv(HSV *hsv, int r, int g, int b)
{
    int i, v, x, f;

    x = (r < g) ? r : g;
    if (b < x)
        x = b;
    v = (r > g) ? r : g;
    if (b > v)
        v = b;

    if (v == x) {
        hsv->h = 0;
        hsv->s = 0;
        hsv->v = v;
        return;
    }

    if (r == v) {
        f = g - b;
        i = 0;
    } else if (g == v) {
        f = b - r;
        i = 2 * 60;
    } else {
        f = r - g;
        i = 4 * 60;
    }

    hsv->h = i + (60 * f) / (v - x);
    if (hsv->h < 0)
        hsv->h += 360;

    hsv->s = (255 * (v - x)) / v;
    hsv->v = v;

    return;
}

void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
    ContextInfo *ci = (ContextInfo *) ctx;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    int rowsize = picture->linesize[0];

#if 0
    av_log(NULL, AV_LOG_DEBUG, "pix_fmt = %d, width = %d, pts = %lld, ci->next_pts = %lld\n",
        pix_fmt, width, pts, ci->next_pts);
#endif

    if (pts < ci->next_pts)
        return;

    if (width < ci->min_width)
        return;

    ci->next_pts = pts + 1000000;

    if (pix_fmt == PIX_FMT_YUV420P) {
        uint8_t *y, *u, *v;
        int width2 = width >> 1;
        int inrange = 0;
        int pixcnt;
        int h;
        int h_start, h_end;
        int w_start, w_end;

        h_end = 2 * ((ci->inset * height) / 200);
        h_start = height - h_end;

        w_end = (ci->inset * width2) / 100;
        w_start = width2 - w_end;

        pixcnt = ((h_start - h_end) >> 1) * (w_start - w_end);

        y = picture->data[0] + h_end * picture->linesize[0] + w_end * 2;
        u = picture->data[1] + h_end * picture->linesize[1] / 2 + w_end;
        v = picture->data[2] + h_end * picture->linesize[2] / 2 + w_end;

        for (h = h_start; h > h_end; h -= 2) {
            int w;

            for (w = w_start; w > w_end; w--) {
                unsigned int r,g,b;
                HSV hsv;
                int cb, cr, yt, r_add, g_add, b_add;

                YUV_TO_RGB1_CCIR(u[0], v[0]);
                YUV_TO_RGB2_CCIR(r, g, b, y[0]);

                get_hsv(&hsv, r, g, b);

                if (ci->debug > 1)
                    av_log(NULL, AV_LOG_DEBUG, "(%d,%d,%d) -> (%d,%d,%d)\n",
                        r,g,b,hsv.h,hsv.s,hsv.v);


                if (hsv.h >= ci->dark.h && hsv.h <= ci->bright.h &&
                    hsv.s >= ci->dark.s && hsv.s <= ci->bright.s &&
                    hsv.v >= ci->dark.v && hsv.v <= ci->bright.v) {
                    inrange++;
                } else if (ci->zapping) {
                    y[0] = y[1] = y[rowsize] = y[rowsize + 1] = 16;
                    u[0] = 128;
                    v[0] = 128;
                }

                y+= 2;
                u++;
                v++;
            }

            y += picture->linesize[0] * 2 - (w_start - w_end) * 2;
            u += picture->linesize[1] - (w_start - w_end);
            v += picture->linesize[2] - (w_start - w_end);
        }

        if (ci->debug)
            av_log(NULL, AV_LOG_INFO, "Fish: Inrange=%d of %d = %d threshold\n", inrange, pixcnt, 1000 * inrange / pixcnt);

        if (inrange * 1000 / pixcnt >= ci->threshold) {
            /* Save to file */
            int size;
            char *buf;
            AVPicture picture1;
            static int frame_counter;
            static int foundfile;

            if ((frame_counter++ % 20) == 0) {
                /* Check how many files we have */
                DIR *d;

                foundfile = 0;

                d = opendir(ci->dir);
                if (d) {
                    struct dirent *dent;

                    while ((dent = readdir(d))) {
                        if (strncmp("fishimg", dent->d_name, 7) == 0) {
                            if (strcmp(".ppm", dent->d_name + strlen(dent->d_name) - 4) == 0) {
                                foundfile++;
                            }
                        }
                    }
                    closedir(d);
                }
            }

            if (foundfile < ci->file_limit) {
                FILE *f;
                char fname[256];

                size = avpicture_get_size(PIX_FMT_RGB24, width, height);
                buf = av_malloc(size);

                avpicture_fill(&picture1, buf, PIX_FMT_RGB24, width, height);

                // if we already got a SWS context, let's realloc if is not re-useable
                ci->toRGB_convert_ctx = sws_getCachedContext(ci->toRGB_convert_ctx,
                                            width, height, pix_fmt,
                                            width, height, PIX_FMT_RGB24,
                                            sws_flags, NULL, NULL, NULL);
                if (ci->toRGB_convert_ctx == NULL) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Cannot initialize the toRGB conversion context\n");
                    return;
                }
                // img_convert parameters are          2 first destination, then 4 source
                // sws_scale   parameters are context, 4 first source,      then 2 destination
                sws_scale(ci->toRGB_convert_ctx,
                              picture->data, picture->linesize, 0, height,
                              picture1.data, picture1.linesize);

                    /* Write out the PPM file */
                    snprintf(fname, sizeof(fname), "%s/fishimg%ld_%"PRId64".ppm", ci->dir, (long)(av_gettime() / 1000000), pts);
                    f = fopen(fname, "w");
                    if (f) {
                        fprintf(f, "P6 %d %d 255\n", width, height);
                        if (!fwrite(buf, width * height * 3, 1, f))
                            av_log(ctx, AV_LOG_ERROR, "Couldn't write to PPM file %s\n", fname);
                        fclose(f);
                    }

                av_free(buf);
                ci->next_pts = pts + ci->min_interval;
            }
        }
    }
}

