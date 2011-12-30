/*
 * Input cache protocol.
 * Copyright (c) 2011 Michael Niedermayer
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
 *
 * Based on file.c by Fabrice Bellard
 */

/**
 * @TODO
 *      support non continuous caching
 *      support keeping files
 *      support filling with a background thread
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/file.h"
#include "avformat.h"
#include <fcntl.h>
#if HAVE_SETMODE
#include <io.h>
#endif
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "os_support.h"
#include "url.h"

typedef struct Context {
    int fd;
    int64_t end;
    int64_t pos;
    URLContext *inner;
} Context;

static int cache_open(URLContext *h, const char *arg, int flags)
{
    const char *buffername;
    Context *c= h->priv_data;

    av_strstart(arg, "cache:", &arg);

    c->fd = av_tempfile("ffcache", &buffername, 0, h);
    if (c->fd < 0){
        av_log(h, AV_LOG_ERROR, "Failed to create tempfile\n");
        return c->fd;
    }

    unlink(buffername);
    av_freep(&buffername);

    return ffurl_open(&c->inner, arg, flags, &h->interrupt_callback, NULL);
}

static int cache_read(URLContext *h, unsigned char *buf, int size)
{
    Context *c= h->priv_data;
    int r;

    if(c->pos<c->end){
        r = read(c->fd, buf, FFMIN(size, c->end - c->pos));
        if(r>0)
            c->pos += r;
        return (-1 == r)?AVERROR(errno):r;
    }else{
        r = ffurl_read(c->inner, buf, size);
        if(r > 0){
            int r2= write(c->fd, buf, r);
            av_assert0(r2==r); // FIXME handle cache failure
            c->pos += r;
            c->end += r;
        }
        return r;
    }
}

static int64_t cache_seek(URLContext *h, int64_t pos, int whence)
{
    Context *c= h->priv_data;

    if (whence == AVSEEK_SIZE) {
        pos= ffurl_seek(c->inner, pos, whence);
        if(pos <= 0){
            pos= ffurl_seek(c->inner, -1, SEEK_END);
            ffurl_seek(c->inner, c->end, SEEK_SET);
            if(pos <= 0)
                return c->end;
        }
        return pos;
    }

    pos= lseek(c->fd, pos, whence);
    if(pos<0){
        return pos;
    }else if(pos <= c->end){
        c->pos= pos;
        return pos;
    }else{
        lseek(c->fd, c->pos, SEEK_SET);
        return AVERROR(EPIPE);
    }
}

static int cache_close(URLContext *h)
{
    Context *c= h->priv_data;
    close(c->fd);
    ffurl_close(c->inner);

    return 0;
}

URLProtocol ff_cache_protocol = {
    .name                = "cache",
    .url_open            = cache_open,
    .url_read            = cache_read,
    .url_seek            = cache_seek,
    .url_close           = cache_close,
    .priv_data_size      = sizeof(Context),
};
