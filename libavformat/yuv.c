/*
 * .Y.U.V image format
 * Copyright (c) 2003 Fabrice Bellard.
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

static int sizes[][2] = {
    { 640, 480 },
    { 720, 480 },
    { 720, 576 },
    { 352, 288 },
    { 352, 240 },
    { 160, 128 },
    { 512, 384 },
    { 640, 352 },
    { 640, 240 },
};

static int infer_size(int *width_ptr, int *height_ptr, int size)
{
    int i;

    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        if ((sizes[i][0] * sizes[i][1]) == size) {
            *width_ptr = sizes[i][0];
            *height_ptr = sizes[i][1];
            return 0;
        }
    }
    return -1;
}

static int yuv_read(ByteIOContext *f,
                    int (*alloc_cb)(void *opaque, AVImageInfo *info), void *opaque)
{
    ByteIOContext pb1, *pb = &pb1;
    int img_size, ret;
    char fname[1024], *p;
    int size;
    URLContext *h;
    AVImageInfo info1, *info = &info1;
    
    img_size = url_fsize(f);

    /* XXX: hack hack */
    h = url_fileno(f);
    url_get_filename(h, fname, sizeof(fname));

    if (infer_size(&info->width, &info->height, img_size) < 0) {
        return AVERROR_IO;
    }
    info->pix_fmt = PIX_FMT_YUV420P;
    
    ret = alloc_cb(opaque, info);
    if (ret)
        return ret;
    
    size = info->width * info->height;
    
    p = strrchr(fname, '.');
    if (!p || p[1] != 'Y')
        return AVERROR_IO;

    get_buffer(f, info->pict.data[0], size);
    
    p[1] = 'U';
    if (url_fopen(pb, fname, URL_RDONLY) < 0)
        return AVERROR_IO;

    get_buffer(pb, info->pict.data[1], size / 4);
    url_fclose(pb);
    
    p[1] = 'V';
    if (url_fopen(pb, fname, URL_RDONLY) < 0)
        return AVERROR_IO;

    get_buffer(pb, info->pict.data[2], size / 4);
    url_fclose(pb);
    return 0;
}

static int yuv_write(ByteIOContext *pb2, AVImageInfo *info)
{
    ByteIOContext pb1, *pb;
    char fname[1024], *p;
    int i, j, width, height;
    uint8_t *ptr;
    URLContext *h;
    static const char *ext = "YUV";
    
    /* XXX: hack hack */
    h = url_fileno(pb2);
    url_get_filename(h, fname, sizeof(fname));

    p = strrchr(fname, '.');
    if (!p || p[1] != 'Y')
        return AVERROR_IO;

    width = info->width;
    height = info->height;

    for(i=0;i<3;i++) {
        if (i == 1) {
            width >>= 1;
            height >>= 1;
        }

        if (i >= 1) {
            pb = &pb1;
            p[1] = ext[i];
            if (url_fopen(pb, fname, URL_WRONLY) < 0)
                return AVERROR_IO;
        } else {
            pb = pb2;
        }
    
        ptr = info->pict.data[i];
        for(j=0;j<height;j++) {
            put_buffer(pb, ptr, width);
            ptr += info->pict.linesize[i];
        }
        put_flush_packet(pb);
        if (i >= 1) {
            url_fclose(pb);
        }
    }
    return 0;
}
    
static int yuv_probe(AVProbeData *pd)
{
    if (match_ext(pd->filename, "Y"))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

AVImageFormat yuv_image_format = {
    "yuv",
    "Y",
    yuv_probe,
    yuv_read,
    (1 << PIX_FMT_YUV420P),
    yuv_write,
};
