/*
 * imlib2 based hook
 * Copyright (c) 2002 Philip Gladstone
 *
 * This module is very much intended as an example of what could be done.
 *
 * One caution is that this is an expensive process -- in particular the
 * conversion of the image into RGB and back is time consuming. For some
 * special cases -- e.g. painting black text -- it would be faster to paint
 * the text into a bitmap and then combine it directly into the YUV
 * image. However, this code is fast enough to handle 10 fps of 320x240 on a
 * 900MHz Duron in maybe 15% of the CPU.

 * See further statistics on Pentium4, 3GHz, FFMpeg is SVN-r6798
 * Input movie is 20.2 seconds of PAL DV on AVI
 * Output movie is DVD compliant VOB.
 *
   ffmpeg -i input.avi -target pal-dvd out.vob
   #   13.516s  just transcode
   ffmpeg -i input.avi -vhook /usr/local/bin/vhook/null.dll -target pal-dvd out.vob
   #   23.546s  transcode and img_convert
   ffmpeg -i input.avi -vhook \
     'vhook/imlib2.dll -c red -F Vera/20 -x 150-0.5*N -y 70+0.25*N -t Hello_person' \
     -target pal-dvd out.vob
   #   21.454s  transcode, img_convert and move text around
   ffmpeg -i input.avi -vhook \
     'vhook/imlib2.dll -x 150-0.5*N -y 70+0.25*N -i /usr/share/imlib2/data/images/bulb.png' \
     -target pal-dvd out.vob
   #   20.828s  transcode, img_convert and move image around
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

#include "libavformat/framehook.h"
#include "libswscale/swscale.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#undef time
#include <sys/time.h>
#include <time.h>
#include <Imlib2.h>
#include "libavcodec/eval.h"

const char *const_names[]={
    "PI",
    "E",
    "N",  // frame number (starting at zero)
    "H",  // frame height
    "W",  // frame width
    "h",  // image height
    "w",  // image width
    "X",  // previous x
    "Y",  // previous y
    NULL
};

static int sws_flags = SWS_BICUBIC;

typedef struct {
    int dummy;
    Imlib_Font fn;
    char *text;
    char *file;
    int r, g, b, a;
    AVEvalExpr *eval_r, *eval_g, *eval_b, *eval_a;
    char *expr_R, *expr_G, *expr_B, *expr_A;
    int eval_colors;
    double x, y;
    char *fileImage;
    struct CachedImage *cache;
    Imlib_Image imageOverlaid;
    AVEvalExpr *eval_x, *eval_y;
    char *expr_x, *expr_y;
    int frame_number;
    int imageOverlaid_width, imageOverlaid_height;

    // This vhook first converts frame to RGB ...
    struct SwsContext *toRGB_convert_ctx;
    // ... and then converts back frame from RGB to initial format
    struct SwsContext *fromRGB_convert_ctx;
} ContextInfo;

typedef struct CachedImage {
    struct CachedImage *next;
    Imlib_Image image;
    int width;
    int height;
} CachedImage;

void Release(void *ctx)
{
    ContextInfo *ci;
    ci = (ContextInfo *) ctx;

    if (ci->cache) {
        imlib_context_set_image(ci->cache->image);
        imlib_free_image();
        av_free(ci->cache);
    }
    if (ctx) {
        if (ci->imageOverlaid) {
            imlib_context_set_image(ci->imageOverlaid);
            imlib_free_image();
        }
        ff_eval_free(ci->eval_x);
        ff_eval_free(ci->eval_y);
        ff_eval_free(ci->eval_r);
        ff_eval_free(ci->eval_g);
        ff_eval_free(ci->eval_b);
        ff_eval_free(ci->eval_a);

        av_free(ci->expr_x);
        av_free(ci->expr_y);
        av_free(ci->expr_R);
        av_free(ci->expr_G);
        av_free(ci->expr_B);
        av_free(ci->expr_A);
        sws_freeContext(ci->toRGB_convert_ctx);
        sws_freeContext(ci->fromRGB_convert_ctx);
        av_free(ctx);
    }
}

int Configure(void **ctxp, int argc, char *argv[])
{
    int c;
    ContextInfo *ci;
    char *rgbtxt = 0;
    const char *font = "LucidaSansDemiBold/16";
    char *fp = getenv("FONTPATH");
    char *color = 0;
    FILE *f;
    char *p;
    const char *error;

    *ctxp = av_mallocz(sizeof(ContextInfo));
    ci = (ContextInfo *) *ctxp;

    ci->x = 0.0;
    ci->y = 0.0;
    ci->expr_x = "0.0";
    ci->expr_y = "0.0";

    optind = 1;

    /* Use ':' to split FONTPATH */
    if (fp)
        while ((p = strchr(fp, ':'))) {
            *p = 0;
            imlib_add_path_to_font_path(fp);
            fp = p + 1;
        }
    if ((fp) && (*fp))
        imlib_add_path_to_font_path(fp);


    while ((c = getopt(argc, argv, "R:G:B:A:C:c:f:F:t:x:y:i:")) > 0) {
        switch (c) {
            case 'R':
                ci->expr_R = av_strdup(optarg);
                ci->eval_colors = 1;
                break;
            case 'G':
                ci->expr_G = av_strdup(optarg);
                ci->eval_colors = 1;
                break;
            case 'B':
                ci->expr_B = av_strdup(optarg);
                ci->eval_colors = 1;
                break;
            case 'A':
                ci->expr_A = av_strdup(optarg);
                break;
            case 'C':
                rgbtxt = optarg;
                break;
            case 'c':
                color = optarg;
                break;
            case 'F':
                font = optarg;
                break;
            case 't':
                ci->text = av_strdup(optarg);
                break;
            case 'f':
                ci->file = av_strdup(optarg);
                break;
            case 'x':
                ci->expr_x = av_strdup(optarg);
                break;
            case 'y':
                ci->expr_y = av_strdup(optarg);
                break;
            case 'i':
                ci->fileImage = av_strdup(optarg);
                break;
            case '?':
                av_log(NULL, AV_LOG_ERROR, "Unrecognized argument '%s'\n", argv[optind]);
                return -1;
        }
    }

    if (ci->eval_colors && !(ci->expr_R && ci->expr_G && ci->expr_B))
    {
        av_log(NULL, AV_LOG_ERROR, "You must specify expressions for all or no colors.\n");
        return -1;
    }

    if (ci->text || ci->file) {
        ci->fn = imlib_load_font(font);
        if (!ci->fn) {
            av_log(NULL, AV_LOG_ERROR, "Failed to load font '%s'\n", font);
            return -1;
        }
        imlib_context_set_font(ci->fn);
        imlib_context_set_direction(IMLIB_TEXT_TO_RIGHT);
    }

    if (color) {
        char buff[256];
        int done = 0;

        if (ci->eval_colors)
        {
            av_log(NULL, AV_LOG_ERROR, "You must not specify both a color name and expressions for the colors.\n");
            return -1;
        }

        if (rgbtxt)
            f = fopen(rgbtxt, "r");
        else
        {
            f = fopen("/usr/share/X11/rgb.txt", "r");
            if (!f)
                f = fopen("/usr/lib/X11/rgb.txt", "r");
        }
        if (!f) {
            av_log(NULL, AV_LOG_ERROR, "Failed to find RGB color names file\n");
            return -1;
        }
        while (fgets(buff, sizeof(buff), f)) {
            int r, g, b;
            char colname[80];

            if (sscanf(buff, "%d %d %d %64s", &r, &g, &b, colname) == 4 &&
                strcasecmp(colname, color) == 0) {
                ci->r = r;
                ci->g = g;
                ci->b = b;
                /* fprintf(stderr, "%s -> %d,%d,%d\n", colname, r, g, b); */
                done = 1;
                break;
            }
        }
        fclose(f);
        if (!done) {
            av_log(NULL, AV_LOG_ERROR, "Unable to find color '%s' in rgb.txt\n", color);
            return -1;
        }
    } else if (ci->eval_colors) {
        if (!(ci->eval_r = ff_parse(ci->expr_R, const_names, NULL, NULL, NULL, NULL, &error))){
            av_log(NULL, AV_LOG_ERROR, "Couldn't parse R expression '%s': %s\n", ci->expr_R, error);
            return -1;
        }
        if (!(ci->eval_g = ff_parse(ci->expr_G, const_names, NULL, NULL, NULL, NULL, &error))){
            av_log(NULL, AV_LOG_ERROR, "Couldn't parse G expression '%s': %s\n", ci->expr_G, error);
            return -1;
        }
        if (!(ci->eval_b = ff_parse(ci->expr_B, const_names, NULL, NULL, NULL, NULL, &error))){
            av_log(NULL, AV_LOG_ERROR, "Couldn't parse B expression '%s': %s\n", ci->expr_B, error);
            return -1;
        }
    }

    if (ci->expr_A) {
        if (!(ci->eval_a = ff_parse(ci->expr_A, const_names, NULL, NULL, NULL, NULL, &error))){
            av_log(NULL, AV_LOG_ERROR, "Couldn't parse A expression '%s': %s\n", ci->expr_A, error);
            return -1;
        }
    } else {
        ci->a = 255;
    }

    if (!(ci->eval_colors || ci->eval_a))
        imlib_context_set_color(ci->r, ci->g, ci->b, ci->a);

    /* load the image (for example, credits for a movie) */
    if (ci->fileImage) {
        ci->imageOverlaid = imlib_load_image_immediately(ci->fileImage);
        if (!(ci->imageOverlaid)){
            av_log(NULL, AV_LOG_ERROR, "Couldn't load image '%s'\n", ci->fileImage);
            return -1;
        }
        imlib_context_set_image(ci->imageOverlaid);
        ci->imageOverlaid_width  = imlib_image_get_width();
        ci->imageOverlaid_height = imlib_image_get_height();
    }

    if (!(ci->eval_x = ff_parse(ci->expr_x, const_names, NULL, NULL, NULL, NULL, &error))){
        av_log(NULL, AV_LOG_ERROR, "Couldn't parse x expression '%s': %s\n", ci->expr_x, error);
        return -1;
    }

    if (!(ci->eval_y = ff_parse(ci->expr_y, const_names, NULL, NULL, NULL, NULL, &error))){
        av_log(NULL, AV_LOG_ERROR, "Couldn't parse y expression '%s': %s\n", ci->expr_y, error);
        return -1;
    }

    return 0;
}

static Imlib_Image get_cached_image(ContextInfo *ci, int width, int height)
{
    CachedImage *cache;

    for (cache = ci->cache; cache; cache = cache->next) {
        if (width == cache->width && height == cache->height)
            return cache->image;
    }

    return NULL;
}

static void put_cached_image(ContextInfo *ci, Imlib_Image image, int width, int height)
{
    CachedImage *cache = av_mallocz(sizeof(*cache));

    cache->image = image;
    cache->width = width;
    cache->height = height;
    cache->next = ci->cache;
    ci->cache = cache;
}

void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
    ContextInfo *ci = (ContextInfo *) ctx;
    AVPicture picture1;
    Imlib_Image image;
    DATA32 *data;

    image = get_cached_image(ci, width, height);

    if (!image) {
        image = imlib_create_image(width, height);
        put_cached_image(ci, image, width, height);
    }

    imlib_context_set_image(image);
    data = imlib_image_get_data();

    avpicture_fill(&picture1, (uint8_t *) data, PIX_FMT_RGB32, width, height);

    // if we already got a SWS context, let's realloc if is not re-useable
    ci->toRGB_convert_ctx = sws_getCachedContext(ci->toRGB_convert_ctx,
                                width, height, pix_fmt,
                                width, height, PIX_FMT_RGB32,
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

    imlib_image_set_has_alpha(0);

    {
        int wid, hig, h_a, v_a;
        char buff[1000];
        char tbuff[1000];
        const char *tbp = ci->text;
        time_t now = time(0);
        char *p, *q;
        int y;

        double const_values[]={
            M_PI,
            M_E,
            ci->frame_number,         // frame number (starting at zero)
            height,                   // frame height
            width,                    // frame width
            ci->imageOverlaid_height, // image height
            ci->imageOverlaid_width,  // image width
            ci->x,                    // previous x
            ci->y,                    // previous y
            0
        };

        if (ci->file) {
            int fd = open(ci->file, O_RDONLY);

            if (fd < 0) {
                tbp = "[File not found]";
            } else {
                int l = read(fd, tbuff, sizeof(tbuff) - 1);

                if (l >= 0) {
                    tbuff[l] = 0;
                    tbp = tbuff;
                } else {
                    tbp = "[I/O Error]";
                }
                close(fd);
            }
        }

        if (tbp)
            strftime(buff, sizeof(buff), tbp, localtime(&now));
        else if (!(ci->imageOverlaid))
            strftime(buff, sizeof(buff), "[No data]", localtime(&now));

        ci->x = ff_parse_eval(ci->eval_x, const_values, ci);
        ci->y = ff_parse_eval(ci->eval_y, const_values, ci);
        y = ci->y;

        if (ci->eval_a) {
            ci->a = ff_parse_eval(ci->eval_a, const_values, ci);
        }

        if (ci->eval_colors) {
            ci->r = ff_parse_eval(ci->eval_r, const_values, ci);
            ci->g = ff_parse_eval(ci->eval_g, const_values, ci);
            ci->b = ff_parse_eval(ci->eval_b, const_values, ci);
        }

        if (ci->eval_colors || ci->eval_a) {
            imlib_context_set_color(ci->r, ci->g, ci->b, ci->a);
        }

        if (!(ci->imageOverlaid))
        for (p = buff; p; p = q) {
            q = strchr(p, '\n');
            if (q)
                *q++ = 0;

            imlib_text_draw_with_return_metrics(ci->x, y, p, &wid, &hig, &h_a, &v_a);
            y += v_a;
        }

        if (ci->imageOverlaid) {
            imlib_context_set_image(image);
            imlib_blend_image_onto_image(ci->imageOverlaid, 0,
                0, 0, ci->imageOverlaid_width, ci->imageOverlaid_height,
                ci->x, ci->y, ci->imageOverlaid_width, ci->imageOverlaid_height);
        }

    }

    ci->fromRGB_convert_ctx = sws_getCachedContext(ci->fromRGB_convert_ctx,
                                    width, height, PIX_FMT_RGB32,
                                    width, height, pix_fmt,
                                    sws_flags, NULL, NULL, NULL);
    if (ci->fromRGB_convert_ctx == NULL) {
        av_log(NULL, AV_LOG_ERROR,
               "Cannot initialize the fromRGB conversion context\n");
        return;
    }
// img_convert parameters are          2 first destination, then 4 source
// sws_scale   parameters are context, 4 first source,      then 2 destination
    sws_scale(ci->fromRGB_convert_ctx,
             picture1.data, picture1.linesize, 0, height,
             picture->data, picture->linesize);

    ci->frame_number++;
}

