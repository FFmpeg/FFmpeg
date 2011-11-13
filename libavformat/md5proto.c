/*
 * Copyright (c) 2010 Mans Rullgard
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
#include "libavutil/avstring.h"
#include "libavutil/md5.h"
#include "libavutil/mem.h"
#include "libavutil/error.h"
#include "avformat.h"
#include "avio.h"
#include "url.h"

#define PRIV_SIZE 128

static int md5_open(URLContext *h, const char *filename, int flags)
{
    if (PRIV_SIZE < av_md5_size) {
        av_log(NULL, AV_LOG_ERROR, "Insuffient size for MD5 context\n");
        return -1;
    }

    if (!flags & AVIO_FLAG_WRITE)
        return AVERROR(EINVAL);

    av_md5_init(h->priv_data);

    return 0;
}

static int md5_write(URLContext *h, const unsigned char *buf, int size)
{
    av_md5_update(h->priv_data, buf, size);
    return size;
}

static int md5_close(URLContext *h)
{
    const char *filename = h->filename;
    uint8_t md5[16], buf[64];
    URLContext *out;
    int i, err = 0;

    av_md5_final(h->priv_data, md5);
    for (i = 0; i < sizeof(md5); i++)
        snprintf(buf + i*2, 3, "%02x", md5[i]);
    buf[i*2] = '\n';

    av_strstart(filename, "md5:", &filename);

    if (*filename) {
        err = ffurl_open(&out, filename, AVIO_FLAG_WRITE,
                         &h->interrupt_callback, NULL);
        if (err)
            return err;
        err = ffurl_write(out, buf, i*2+1);
        ffurl_close(out);
    } else {
        if (fwrite(buf, 1, i*2+1, stdout) < i*2+1)
            err = AVERROR(errno);
    }

    return err;
}

static int md5_get_handle(URLContext *h)
{
    return (intptr_t)h->priv_data;
}

URLProtocol ff_md5_protocol = {
    .name                = "md5",
    .url_open            = md5_open,
    .url_write           = md5_write,
    .url_close           = md5_close,
    .url_get_file_handle = md5_get_handle,
    .priv_data_size      = PRIV_SIZE,
};
