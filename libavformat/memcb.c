/*
 * Buffered mem io for ffmpeg system
 * Copyright (c) 2010 avcoder <ffmpeg@gmail.com>
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

#include "libavformat/url.h"
#include "memcb.h"

//demo code
#if 0
    char url[4096];
    MemCallBackContext *ctx;
    ctx = memcb_new();
    ctx->init = NULL;
    ctx->url_read = my_read_fun;
    ctx->url_write = my_write_fun;
    ctx->free = NULL;
    ctx->priv_data = my_membuf;
    memcb_geturl(ctx, url);
    //now the demo input file is mmcb://123421321, can use for io_open
    //not need to free the MemCallBackContext, because of it will be freed at mempool_close
#endif

static char *ulltoa(char *buf, uint64_t val)
{
    char tmp[0x20];
    char *cp = tmp;
    char *rp = buf;
    do{
        *cp++ = (char) (val%10ULL) + '0';
        val /= 10ULL;
    }while (val);
    do{
        *buf++ = *(--cp);
    }while (cp != tmp);
    *buf = 0x00;
    return rp;
}

MemCallBackContext *memcb_new() {return av_malloc(sizeof(MemCallBackContext));}

void memcb_geturl(MemCallBackContext *ctx, char *pathbuf)
{
	int64_t address = (int64_t)(intptr_t)ctx;
	strcpy(pathbuf, "mmcb://");
	ulltoa(pathbuf+7, address);
}

static int memcb_open(URLContext *h, const char *uri, int flags)
{
    MemCallBackContext *mc;
    /* skip the identifier: 'memp://' */
    int64_t address = atoll(uri + 7);
    mc = (MemCallBackContext*)(intptr_t)address;
    mc->flags = flags;
    h->priv_data = mc;
    h->is_streamed = 1;
    if (mc->url_init) mc->url_init(mc);
    //h->max_packet_size = 2048;
    //h->rw_timeout = 100000000;
    return 0;
}

static int memcb_read(URLContext *h, unsigned char *buf, int size)
{
    MemCallBackContext *mc = (MemCallBackContext *)h->priv_data;
    if ((mc->flags != AVIO_FLAG_READ) && (mc->flags != AVIO_FLAG_READ_WRITE))
        return AVERROR(ENOSYS);
    size = mc->url_read(mc, buf, size);
    if (size==0) return AVERROR(EAGAIN);
    return size;
}

static int memcb_write(URLContext *h, const unsigned char *buf, int size)
{
    MemCallBackContext *mc = (MemCallBackContext *)h->priv_data;
    if ((mc->flags != AVIO_FLAG_WRITE) && (mc->flags != AVIO_FLAG_READ_WRITE))
        return AVERROR(ENOSYS);
    size = mc->url_write(mc, buf, size);
    if (size==0) return AVERROR(EAGAIN);
    if (size<0) return AVERROR(EIO);
    return size;
}

static int memcb_close(URLContext *h)
{
	MemCallBackContext *mc = (MemCallBackContext *)h->priv_data;
    if (mc->url_free) mc->url_free(mc);
    av_free(h->priv_data);
    return 0;
}

static int memcb_get_handle(URLContext *h)
{
    return (int)(intptr_t)h->priv_data;
}

URLProtocol ff_memcb_protocol = {
    .name                = "mmcb",
    .url_open            = memcb_open,
    .url_read            = memcb_read,
    .url_write           = memcb_write,
    //.url_seek            = memcb_seek,
    .url_close           = memcb_close,
    .url_get_file_handle = memcb_get_handle
};
