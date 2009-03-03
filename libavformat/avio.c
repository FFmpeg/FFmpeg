/*
 * Unbuffered io for ffmpeg system
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/avstring.h"
#include "libavcodec/opt.h"
#include "os_support.h"
#include "avformat.h"

#if LIBAVFORMAT_VERSION_MAJOR >= 53
/** @name Logging context. */
/*@{*/
static const char *urlcontext_to_name(void *ptr)
{
    URLContext *h = (URLContext *)ptr;
    if(h->prot) return h->prot->name;
    else        return "NULL";
}
static const AVOption options[] = {{NULL}};
static const AVClass urlcontext_class =
        { "URLContext", urlcontext_to_name, options };
/*@}*/
#endif

static int default_interrupt_cb(void);

URLProtocol *first_protocol = NULL;
URLInterruptCB *url_interrupt_cb = default_interrupt_cb;

URLProtocol *av_protocol_next(URLProtocol *p)
{
    if(p) return p->next;
    else  return first_protocol;
}

int av_register_protocol(URLProtocol *protocol)
{
    URLProtocol **p;
    p = &first_protocol;
    while (*p != NULL) p = &(*p)->next;
    *p = protocol;
    protocol->next = NULL;
    return 0;
}

#if LIBAVFORMAT_VERSION_MAJOR < 53
int register_protocol(URLProtocol *protocol)
{
    return av_register_protocol(protocol);
}
#endif

int url_open_protocol (URLContext **puc, struct URLProtocol *up,
                       const char *filename, int flags)
{
    URLContext *uc;
    int err;

    uc = av_malloc(sizeof(URLContext) + strlen(filename) + 1);
    if (!uc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
#if LIBAVFORMAT_VERSION_MAJOR >= 53
    uc->av_class = &urlcontext_class;
#endif
    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, filename);
    uc->prot = up;
    uc->flags = flags;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    err = up->url_open(uc, filename, flags);
    if (err < 0) {
        av_free(uc);
        *puc = NULL;
        return err;
    }

    //We must be carefull here as url_seek() could be slow, for example for http
    if(   (flags & (URL_WRONLY | URL_RDWR))
       || !strcmp(up->name, "file"))
        if(!uc->is_streamed && url_seek(uc, 0, SEEK_SET) < 0)
            uc->is_streamed= 1;
    *puc = uc;
    return 0;
 fail:
    *puc = NULL;
    return err;
}

int url_open(URLContext **puc, const char *filename, int flags)
{
    URLProtocol *up;
    const char *p;
    char proto_str[128], *q;

    p = filename;
    q = proto_str;
    while (*p != '\0' && *p != ':') {
        /* protocols can only contain alphabetic chars */
        if (!isalpha(*p))
            goto file_proto;
        if ((q - proto_str) < sizeof(proto_str) - 1)
            *q++ = *p;
        p++;
    }
    /* if the protocol has length 1, we consider it is a dos drive */
    if (*p == '\0' || is_dos_path(filename)) {
    file_proto:
        strcpy(proto_str, "file");
    } else {
        *q = '\0';
    }

    up = first_protocol;
    while (up != NULL) {
        if (!strcmp(proto_str, up->name))
            return url_open_protocol (puc, up, filename, flags);
        up = up->next;
    }
    *puc = NULL;
    return AVERROR(ENOENT);
}

int url_read(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    if (h->flags & URL_WRONLY)
        return AVERROR(EIO);
    ret = h->prot->url_read(h, buf, size);
    return ret;
}

int url_write(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    if (!(h->flags & (URL_WRONLY | URL_RDWR)))
        return AVERROR(EIO);
    /* avoid sending too big packets */
    if (h->max_packet_size && size > h->max_packet_size)
        return AVERROR(EIO);
    ret = h->prot->url_write(h, buf, size);
    return ret;
}

int64_t url_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t ret;

    if (!h->prot->url_seek)
        return AVERROR(EPIPE);
    ret = h->prot->url_seek(h, pos, whence);
    return ret;
}

int url_close(URLContext *h)
{
    int ret = 0;
    if (!h) return 0; /* can happen when url_open fails */

    if (h->prot->url_close)
        ret = h->prot->url_close(h);
    av_free(h);
    return ret;
}

int url_exist(const char *filename)
{
    URLContext *h;
    if (url_open(&h, filename, URL_RDONLY) < 0)
        return 0;
    url_close(h);
    return 1;
}

int64_t url_filesize(URLContext *h)
{
    int64_t pos, size;

    size= url_seek(h, 0, AVSEEK_SIZE);
    if(size<0){
        pos = url_seek(h, 0, SEEK_CUR);
        if ((size = url_seek(h, -1, SEEK_END)) < 0)
            return size;
        size++;
        url_seek(h, pos, SEEK_SET);
    }
    return size;
}

int url_get_file_handle(URLContext *h)
{
    if (!h->prot->url_get_file_handle)
        return -1;
    return h->prot->url_get_file_handle(h);
}

int url_get_max_packet_size(URLContext *h)
{
    return h->max_packet_size;
}

void url_get_filename(URLContext *h, char *buf, int buf_size)
{
    av_strlcpy(buf, h->filename, buf_size);
}


static int default_interrupt_cb(void)
{
    return 0;
}

void url_set_interrupt_cb(URLInterruptCB *interrupt_cb)
{
    if (!interrupt_cb)
        interrupt_cb = default_interrupt_cb;
    url_interrupt_cb = interrupt_cb;
}

int av_url_read_pause(URLContext *h, int pause)
{
    if (!h->prot->url_read_pause)
        return AVERROR(ENOSYS);
    return h->prot->url_read_pause(h, pause);
}

int64_t av_url_read_seek(URLContext *h,
        int stream_index, int64_t timestamp, int flags)
{
    if (!h->prot->url_read_seek)
        return AVERROR(ENOSYS);
    return h->prot->url_read_seek(h, stream_index, timestamp, flags);
}
