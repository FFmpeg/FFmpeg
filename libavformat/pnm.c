/*
 * PNM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard.
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
#include "avformat.h"

static inline int pnm_space(int c)  
{
    return (c == ' ' || c == '\n' || c == '\r' || c == '\t');
}

static void pnm_get(ByteIOContext *f, char *str, int buf_size) 
{
    char *s;
    int c;
    
    /* skip spaces and comments */
    for(;;) {
        c = url_fgetc(f);
        if (c == '#')  {
            do  {
                c = url_fgetc(f);
            } while (c != '\n' && c != URL_EOF);
        } else if (!pnm_space(c)) {
            break;
        }
    }
    
    s = str;
    while (c != URL_EOF && !pnm_space(c)) {
        if ((s - str)  < buf_size - 1)
            *s++ = c;
        c = url_fgetc(f);
    }
    *s = '\0';
}

static int pnm_read1(ByteIOContext *f, 
                     int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque,
                     int allow_yuv)
{
    int i, n, linesize, h;
    char buf1[32];
    unsigned char *ptr;
    AVImageInfo info1, *info = &info1;
    int ret;

    pnm_get(f, buf1, sizeof(buf1));
    if (!strcmp(buf1, "P4")) {
        info->pix_fmt = PIX_FMT_MONOWHITE;
    } else if (!strcmp(buf1, "P5")) {
        if (allow_yuv) 
            info->pix_fmt = PIX_FMT_YUV420P;
        else
            info->pix_fmt = PIX_FMT_GRAY8;
    } else if (!strcmp(buf1, "P6")) {
        info->pix_fmt = PIX_FMT_RGB24;
    } else {
        return AVERROR_INVALIDDATA;
    }
    pnm_get(f, buf1, sizeof(buf1));
    info->width = atoi(buf1);
    if (info->width <= 0)
        return AVERROR_INVALIDDATA;
    pnm_get(f, buf1, sizeof(buf1));
    info->height = atoi(buf1);
    if (info->height <= 0)
        return AVERROR_INVALIDDATA;
    if (info->pix_fmt != PIX_FMT_MONOWHITE) {
        pnm_get(f, buf1, sizeof(buf1));
    }

    /* more check if YUV420 */
    if (info->pix_fmt == PIX_FMT_YUV420P) {
        if ((info->width & 1) != 0)
            return AVERROR_INVALIDDATA;
        h = (info->height * 2);
        if ((h % 3) != 0)
            return AVERROR_INVALIDDATA;
        h /= 3;
        info->height = h;
    }
    
    ret = alloc_cb(opaque, info);
    if (ret)
        return ret;
    
    switch(info->pix_fmt) {
    default:
        return AVERROR_INVALIDDATA;
    case PIX_FMT_RGB24:
        n = info->width * 3;
        goto do_read;
    case PIX_FMT_GRAY8:
        n = info->width;
        goto do_read;
    case PIX_FMT_MONOWHITE:
        n = (info->width + 7) >> 3;
    do_read:
        ptr = info->pict.data[0];
        linesize = info->pict.linesize[0];
        for(i = 0; i < info->height; i++) {
            get_buffer(f, ptr, n);
            ptr += linesize;
        }
        break;
    case PIX_FMT_YUV420P:
        {
            unsigned char *ptr1, *ptr2;

            n = info->width;
            ptr = info->pict.data[0];
            linesize = info->pict.linesize[0];
            for(i = 0; i < info->height; i++) {
                get_buffer(f, ptr, n);
                ptr += linesize;
            }
            ptr1 = info->pict.data[1];
            ptr2 = info->pict.data[2];
            n >>= 1;
            h = info->height >> 1;
            for(i = 0; i < h; i++) {
                get_buffer(f, ptr1, n);
                get_buffer(f, ptr2, n);
                ptr1 += info->pict.linesize[1];
                ptr2 += info->pict.linesize[2];
            }
        }
        break;
    }
    return 0;
}

static int pnm_read(ByteIOContext *f, 
                    int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    return pnm_read1(f, alloc_cb, opaque, 0);
}

static int pgmyuv_read(ByteIOContext *f, 
                       int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    return pnm_read1(f, alloc_cb, opaque, 1);
}

static int pnm_write(ByteIOContext *pb, AVImageInfo *info)
{
    int i, h, h1, c, n, linesize;
    char buf[100];
    uint8_t *ptr, *ptr1, *ptr2;

    h = info->height;
    h1 = h;
    switch(info->pix_fmt) {
    case PIX_FMT_MONOWHITE:
        c = '4';
        n = (info->width + 7) >> 3;
        break;
    case PIX_FMT_GRAY8:
        c = '5';
        n = info->width;
        break;
    case PIX_FMT_RGB24:
        c = '6';
        n = info->width * 3;
        break;
    case PIX_FMT_YUV420P:
        c = '5';
        n = info->width;
        h1 = (h * 3) / 2;
        break;
    default:
        return AVERROR_INVALIDDATA;
    }
    snprintf(buf, sizeof(buf), 
             "P%c\n%d %d\n",
             c, info->width, h1);
    put_buffer(pb, buf, strlen(buf));
    if (info->pix_fmt != PIX_FMT_MONOWHITE) {
        snprintf(buf, sizeof(buf), 
                 "%d\n", 255);
        put_buffer(pb, buf, strlen(buf));
    }
    
    ptr = info->pict.data[0];
    linesize = info->pict.linesize[0];
    for(i=0;i<h;i++) {
        put_buffer(pb, ptr, n);
        ptr += linesize;
    }
    
    if (info->pix_fmt == PIX_FMT_YUV420P) {
        h >>= 1;
        n >>= 1;
        ptr1 = info->pict.data[1];
        ptr2 = info->pict.data[2];
        for(i=0;i<h;i++) {
            put_buffer(pb, ptr1, n);
            put_buffer(pb, ptr2, n);
                ptr1 += info->pict.linesize[1];
                ptr2 += info->pict.linesize[2];
        }
    }
    put_flush_packet(pb);
    return 0;
}

static int pam_read(ByteIOContext *f, 
                    int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    int i, n, linesize, h, w, depth, maxval;
    char buf1[32], tuple_type[32];
    unsigned char *ptr;
    AVImageInfo info1, *info = &info1;
    int ret;

    pnm_get(f, buf1, sizeof(buf1));
    if (strcmp(buf1, "P7") != 0)
        return AVERROR_INVALIDDATA;
    w = -1;
    h = -1;
    maxval = -1;
    depth = -1;
    tuple_type[0] = '\0';
    for(;;) {
        pnm_get(f, buf1, sizeof(buf1));
        if (!strcmp(buf1, "WIDTH")) {
            pnm_get(f, buf1, sizeof(buf1));
            w = strtol(buf1, NULL, 10);
        } else if (!strcmp(buf1, "HEIGHT")) {
            pnm_get(f, buf1, sizeof(buf1));
            h = strtol(buf1, NULL, 10);
        } else if (!strcmp(buf1, "DEPTH")) {
            pnm_get(f, buf1, sizeof(buf1));
            depth = strtol(buf1, NULL, 10);
        } else if (!strcmp(buf1, "MAXVAL")) {
            pnm_get(f, buf1, sizeof(buf1));
            maxval = strtol(buf1, NULL, 10);
        } else if (!strcmp(buf1, "TUPLETYPE")) {
            pnm_get(f, buf1, sizeof(buf1));
            pstrcpy(tuple_type, sizeof(tuple_type), buf1);
        } else if (!strcmp(buf1, "ENDHDR")) {
            break;
        } else {
            return AVERROR_INVALIDDATA;
        }
    }
    /* check that all tags are present */
    if (w <= 0 || h <= 0 || maxval <= 0 || depth <= 0 || tuple_type[0] == '\0')
        return AVERROR_INVALIDDATA;
    info->width = w;
    info->height = h;
    if (depth == 1) {
        if (maxval == 1)
            info->pix_fmt = PIX_FMT_MONOWHITE;
        else 
            info->pix_fmt = PIX_FMT_GRAY8;
    } else if (depth == 3) {
        info->pix_fmt = PIX_FMT_RGB24;
    } else if (depth == 4) {
        info->pix_fmt = PIX_FMT_RGBA32;
    } else {
        return AVERROR_INVALIDDATA;
    }
    ret = alloc_cb(opaque, info);
    if (ret)
        return ret;
    
    switch(info->pix_fmt) {
    default:
        return AVERROR_INVALIDDATA;
    case PIX_FMT_RGB24:
        n = info->width * 3;
        goto do_read;
    case PIX_FMT_GRAY8:
        n = info->width;
        goto do_read;
    case PIX_FMT_MONOWHITE:
        n = (info->width + 7) >> 3;
    do_read:
        ptr = info->pict.data[0];
        linesize = info->pict.linesize[0];
        for(i = 0; i < info->height; i++) {
            get_buffer(f, ptr, n);
            ptr += linesize;
        }
        break;
    case PIX_FMT_RGBA32:
        ptr = info->pict.data[0];
        linesize = info->pict.linesize[0];
        for(i = 0; i < info->height; i++) {
            int j, r, g, b, a;

            for(j = 0;j < w; j++) {
                r = get_byte(f);
                g = get_byte(f);
                b = get_byte(f);
                a = get_byte(f);
                ((uint32_t *)ptr)[j] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            ptr += linesize;
        }
        break;
    }
    return 0;
}

static int pam_write(ByteIOContext *pb, AVImageInfo *info)
{
    int i, h, w, n, linesize, depth, maxval;
    const char *tuple_type;
    char buf[100];
    uint8_t *ptr;

    h = info->height;
    w = info->width;
    switch(info->pix_fmt) {
    case PIX_FMT_MONOWHITE:
        n = (info->width + 7) >> 3;
        depth = 1;
        maxval = 1;
        tuple_type = "BLACKANDWHITE";
        break;
    case PIX_FMT_GRAY8:
        n = info->width;
        depth = 1;
        maxval = 255;
        tuple_type = "GRAYSCALE";
        break;
    case PIX_FMT_RGB24:
        n = info->width * 3;
        depth = 3;
        maxval = 255;
        tuple_type = "RGB";
        break;
    case PIX_FMT_RGBA32:
        n = info->width * 4;
        depth = 4;
        maxval = 255;
        tuple_type = "RGB_ALPHA";
        break;
    default:
        return AVERROR_INVALIDDATA;
    }
    snprintf(buf, sizeof(buf), 
             "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLETYPE %s\nENDHDR\n",
             w, h, depth, maxval, tuple_type);
    put_buffer(pb, buf, strlen(buf));
    
    ptr = info->pict.data[0];
    linesize = info->pict.linesize[0];
    
    if (info->pix_fmt == PIX_FMT_RGBA32) {
        int j;
        unsigned int v;

        for(i=0;i<h;i++) {
            for(j=0;j<w;j++) {
                v = ((uint32_t *)ptr)[j];
                put_byte(pb, (v >> 16) & 0xff);
                put_byte(pb, (v >> 8) & 0xff);
                put_byte(pb, (v) & 0xff);
                put_byte(pb, (v >> 24) & 0xff);
            }
            ptr += linesize;
        }
    } else {
        for(i=0;i<h;i++) {
            put_buffer(pb, ptr, n);
            ptr += linesize;
        }
    }
    put_flush_packet(pb);
    return 0;
}

static int pnm_probe(AVProbeData *pd)
{
    const char *p = pd->buf;
    if (pd->buf_size >= 8 &&
        p[0] == 'P' &&
        p[1] >= '4' && p[1] <= '6' &&
        pnm_space(p[2]) )
        return AVPROBE_SCORE_MAX - 1; /* to permit pgmyuv probe */
    else
        return 0;
}

static int pgmyuv_probe(AVProbeData *pd)
{
    if (match_ext(pd->filename, "pgmyuv"))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int pam_probe(AVProbeData *pd)
{
    const char *p = pd->buf;
    if (pd->buf_size >= 8 &&
        p[0] == 'P' &&
        p[1] == '7' &&
        p[2] == '\n')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

AVImageFormat pnm_image_format = {
    "pnm",
    NULL,
    pnm_probe,
    pnm_read,
    0,
    NULL,
};

AVImageFormat pbm_image_format = {
    "pbm",
    "pbm",
    NULL,
    NULL,
    (1 << PIX_FMT_MONOWHITE),
    pnm_write,
};

AVImageFormat pgm_image_format = {
    "pgm",
    "pgm",
    NULL,
    NULL,
    (1 << PIX_FMT_GRAY8),
    pnm_write,
};

AVImageFormat ppm_image_format = {
    "ppm",
    "ppm",
    NULL,
    NULL,
    (1 << PIX_FMT_RGB24),
    pnm_write,
};

AVImageFormat pam_image_format = {
    "pam",
    "pam",
    pam_probe,
    pam_read,
    (1 << PIX_FMT_MONOWHITE) | (1 << PIX_FMT_GRAY8) | (1 << PIX_FMT_RGB24) | 
    (1 << PIX_FMT_RGBA32),
    pam_write,
};

AVImageFormat pgmyuv_image_format = {
    "pgmyuv",
    "pgmyuv",
    pgmyuv_probe,
    pgmyuv_read,
    (1 << PIX_FMT_YUV420P),
    pnm_write,
};
