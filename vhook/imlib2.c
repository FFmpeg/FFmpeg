/*
 * imlib2 based hook 
 * Copyright (c) 2002 Philip Gladstone
 * 
 * This module implements a text overlay for a video image. Currently it
 * supports a fixed overlay or reading the text from a file. The string
 * is passed through strftime so that it is easy to imprint the date and
 * time onto the image.
 *
 * Options:
 *
 * -c <color>           The color of the text
 * -F <fontname>        The font face and size
 * -t <text>            The text
 * -f <filename>        The filename to read text from
 * -x <num>             X coordinate to start text
 * -y <num>             Y coordinate to start text
 *
 * This module is very much intended as an example of what could be done.
 * For example, you could overlay an image (even semi-transparent) like
 * TV stations do. You can manipulate the image using imlib2 functions
 * in any way. 
 *
 * One caution is that this is an expensive process -- in particular the
 * conversion of the image into RGB and back is time consuming. For some
 * special cases -- e.g. painting black text -- it would be faster to paint
 * the text into a bitmap and then combine it directly into the YUV
 * image. However, this code is fast enough to handle 10 fps of 320x240 on a 
 * 900MHz Duron in maybe 15% of the CPU.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "framehook.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#undef time
#include <sys/time.h>
#include <time.h>
#include <X11/Xlib.h>
#include <Imlib2.h>                                                             

typedef struct {
    int dummy;
    Imlib_Font fn;
    char *text;
    char *file;
    int r, g, b;
    int x;
    int y;
    struct _CachedImage *cache;
} ContextInfo;

typedef struct _CachedImage {
    struct _CachedImage *next;
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
    if (ctx)
        av_free(ctx);
}

int Configure(void **ctxp, int argc, char *argv[])
{
    int c;
    ContextInfo *ci;
    char *font = "LucidaSansDemiBold/16";
    char *fp = getenv("FONTPATH");
    char *color = 0;
    FILE *f;

    *ctxp = av_mallocz(sizeof(ContextInfo));
    ci = (ContextInfo *) *ctxp;

    optind = 0;

    if (fp)
        imlib_add_path_to_font_path(fp);

    while ((c = getopt(argc, argv, "c:f:F:t:x:y:")) > 0) {
        switch (c) {
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
                ci->x = atoi(optarg);
                break;
            case 'y':
                ci->y = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unrecognized argument '%s'\n", argv[optind]);
                return -1;
        }
    }

    ci->fn = imlib_load_font(font);
    if (!ci->fn) {
        fprintf(stderr, "Failed to load font '%s'\n", font);
        return -1;
    }
    imlib_context_set_font(ci->fn);
    imlib_context_set_direction(IMLIB_TEXT_TO_RIGHT);                           

    if (color) {
        char buff[256];
        int done = 0;

        f = fopen("/usr/lib/X11/rgb.txt", "r");
        if (!f) {
            fprintf(stderr, "Failed to find rgb.txt\n");
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
            fprintf(stderr, "Unable to find color '%s' in rgb.txt\n", color);
            return -1;
        }
    }
    imlib_context_set_color(ci->r, ci->g, ci->b, 255);
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

    if (pix_fmt != PIX_FMT_RGBA32) {
        avpicture_fill(&picture1, (uint8_t *) data, PIX_FMT_RGBA32, width, height);
        if (img_convert(&picture1, PIX_FMT_RGBA32, 
                        picture, pix_fmt, width, height) < 0) {
            goto done;
        }
    } else {
        av_abort();
    }

    imlib_image_set_has_alpha(0);

    {
        int wid, hig, h_a, v_a;                                                   
        char buff[1000];
        char tbuff[1000];
        char *tbp = ci->text;
        time_t now = time(0);
        char *p, *q;
        int x, y;

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

        strftime(buff, sizeof(buff), tbp ? tbp : "[No data]", localtime(&now));

        x = ci->x;
        y = ci->y;

        for (p = buff; p; p = q) {
            q = strchr(p, '\n');
            if (q)
                *q++ = 0;

            imlib_text_draw_with_return_metrics(x, y, p, &wid, &hig, &h_a, &v_a);
            y += v_a;
        }
    }

    if (pix_fmt != PIX_FMT_RGBA32) {
        if (img_convert(picture, pix_fmt, 
                        &picture1, PIX_FMT_RGBA32, width, height) < 0) {
        }
    }

done:
    ;
}

