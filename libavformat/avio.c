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

/* needed for usleep() */
#define _XOPEN_SOURCE 600
#include <unistd.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "os_support.h"
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif

#if FF_API_URL_CLASS
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
        { "URLContext", urlcontext_to_name, options, LIBAVUTIL_VERSION_INT };
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

int av_register_protocol2(URLProtocol *protocol, int size)
{
    URLProtocol **p;
    if (size < sizeof(URLProtocol)) {
        URLProtocol* temp = av_mallocz(sizeof(URLProtocol));
        memcpy(temp, protocol, size);
        protocol = temp;
    }
    p = &first_protocol;
    while (*p != NULL) p = &(*p)->next;
    *p = protocol;
    protocol->next = NULL;
    return 0;
}

#if FF_API_REGISTER_PROTOCOL
/* The layout of URLProtocol as of when major was bumped to 52 */
struct URLProtocol_compat {
    const char *name;
    int (*url_open)(URLContext *h, const char *filename, int flags);
    int (*url_read)(URLContext *h, unsigned char *buf, int size);
    int (*url_write)(URLContext *h, unsigned char *buf, int size);
    int64_t (*url_seek)(URLContext *h, int64_t pos, int whence);
    int (*url_close)(URLContext *h);
    struct URLProtocol *next;
};

int av_register_protocol(URLProtocol *protocol)
{
    return av_register_protocol2(protocol, sizeof(struct URLProtocol_compat));
}

int register_protocol(URLProtocol *protocol)
{
    return av_register_protocol(protocol);
}
#endif

static int url_alloc_for_protocol (URLContext **puc, struct URLProtocol *up,
                                   const char *filename, int flags)
{
    URLContext *uc;
    int err;

#if CONFIG_NETWORK
    if (!ff_network_init())
        return AVERROR(EIO);
#endif
    uc = av_mallocz(sizeof(URLContext) + strlen(filename) + 1);
    if (!uc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
#if FF_API_URL_CLASS
    uc->av_class = &urlcontext_class;
#endif
    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, filename);
    uc->prot = up;
    uc->flags = flags;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (up->priv_data_size) {
        uc->priv_data = av_mallocz(up->priv_data_size);
        if (up->priv_data_class) {
            *(const AVClass**)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }

    *puc = uc;
    return 0;
 fail:
    *puc = NULL;
#if CONFIG_NETWORK
    ff_network_close();
#endif
    return err;
}

int url_connect(URLContext* uc)
{
    int err = uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err)
        return err;
    uc->is_connected = 1;
    //We must be careful here as url_seek() could be slow, for example for http
    if(   (uc->flags & (URL_WRONLY | URL_RDWR))
       || !strcmp(uc->prot->name, "file"))
        if(!uc->is_streamed && url_seek(uc, 0, SEEK_SET) < 0)
            uc->is_streamed= 1;
    return 0;
}

int url_open_protocol (URLContext **puc, struct URLProtocol *up,
                       const char *filename, int flags)
{
    int ret;

    ret = url_alloc_for_protocol(puc, up, filename, flags);
    if (ret)
        goto fail;
    ret = url_connect(*puc);
    if (!ret)
        return 0;
 fail:
    url_close(*puc);
    *puc = NULL;
    return ret;
}

#define URL_SCHEME_CHARS                        \
    "abcdefghijklmnopqrstuvwxyz"                \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                \
    "0123456789+-."

int url_alloc(URLContext **puc, const char *filename, int flags)
{
    URLProtocol *up;
    char proto_str[128];
    size_t proto_len = strspn(filename, URL_SCHEME_CHARS);

    if (filename[proto_len] != ':' || is_dos_path(filename))
        strcpy(proto_str, "file");
    else
        av_strlcpy(proto_str, filename, FFMIN(proto_len+1, sizeof(proto_str)));

    up = first_protocol;
    while (up != NULL) {
        if (!strcmp(proto_str, up->name))
            return url_alloc_for_protocol (puc, up, filename, flags);
        up = up->next;
    }
    *puc = NULL;
    return AVERROR(ENOENT);
}

int url_open(URLContext **puc, const char *filename, int flags)
{
    int ret = url_alloc(puc, filename, flags);
    if (ret)
        return ret;
    ret = url_connect(*puc);
    if (!ret)
        return 0;
    url_close(*puc);
    *puc = NULL;
    return ret;
}

int url_read(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    if (h->flags & URL_WRONLY)
        return AVERROR(EIO);
    ret = h->prot->url_read(h, buf, size);
    return ret;
}

int url_read_complete(URLContext *h, unsigned char *buf, int size)
{
    int ret, len;
    int fast_retries = 5;

    len = 0;
    while (len < size) {
        ret = url_read(h, buf+len, size-len);
        if (ret == AVERROR(EAGAIN)) {
            ret = 0;
            if (fast_retries)
                fast_retries--;
            else
                usleep(1000);
        } else if (ret < 1)
            return ret < 0 ? ret : len;
        if (ret)
           fast_retries = FFMAX(fast_retries, 2);
        len += ret;
    }
    return len;
}

int url_write(URLContext *h, const unsigned char *buf, int size)
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
        return AVERROR(ENOSYS);
    ret = h->prot->url_seek(h, pos, whence & ~AVSEEK_FORCE);
    return ret;
}

int url_close(URLContext *h)
{
    int ret = 0;
    if (!h) return 0; /* can happen when url_open fails */

    if (h->is_connected && h->prot->url_close)
        ret = h->prot->url_close(h);
#if CONFIG_NETWORK
    ff_network_close();
#endif
    if (h->prot->priv_data_size)
        av_free(h->priv_data);
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
