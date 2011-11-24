/*
 * unbuffered I/O
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

#include <unistd.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "os_support.h"
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "url.h"

static URLProtocol *first_protocol = NULL;

URLProtocol *ffurl_protocol_next(URLProtocol *prev)
{
    return prev ? prev->next : first_protocol;
}

/** @name Logging context. */
/*@{*/
static const char *urlcontext_to_name(void *ptr)
{
    URLContext *h = (URLContext *)ptr;
    if(h->prot) return h->prot->name;
    else        return "NULL";
}

static void *urlcontext_child_next(void *obj, void *prev)
{
    URLContext *h = obj;
    if (!prev && h->priv_data && h->prot->priv_data_class)
        return h->priv_data;
    return NULL;
}

static const AVClass *urlcontext_child_class_next(const AVClass *prev)
{
    URLProtocol *p = NULL;

    /* find the protocol that corresponds to prev */
    while (prev && (p = ffurl_protocol_next(p)))
        if (p->priv_data_class == prev)
            break;

    /* find next protocol with priv options */
    while (p = ffurl_protocol_next(p))
        if (p->priv_data_class)
            return p->priv_data_class;
    return NULL;

}

static const AVOption options[] = {{NULL}};
const AVClass ffurl_context_class = {
    .class_name     = "URLContext",
    .item_name      = urlcontext_to_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .child_next     = urlcontext_child_next,
    .child_class_next = urlcontext_child_class_next,
};
/*@}*/


#if FF_API_OLD_INTERRUPT_CB
static int default_interrupt_cb(void);
int (*url_interrupt_cb)(void) = default_interrupt_cb;
#endif

URLProtocol *av_protocol_next(URLProtocol *p)
{
    return ffurl_protocol_next(p);
}

const char *avio_enum_protocols(void **opaque, int output)
{
    URLProtocol **p = opaque;
    *p = ffurl_protocol_next(*p);
    if (!*p) return NULL;
    if ((output && (*p)->url_write) || (!output && (*p)->url_read))
        return (*p)->name;
    return avio_enum_protocols(opaque, output);
}

int ffurl_register_protocol(URLProtocol *protocol, int size)
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

static int url_alloc_for_protocol (URLContext **puc, struct URLProtocol *up,
                                   const char *filename, int flags,
                                   const AVIOInterruptCB *int_cb)
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
    uc->av_class = &ffurl_context_class;
    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, filename);
    uc->prot = up;
    uc->flags = flags;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (up->priv_data_size) {
        uc->priv_data = av_mallocz(up->priv_data_size);
        if (up->priv_data_class) {
            char *start = strchr(uc->filename, ',');
            *(const AVClass**)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
            if(start){
                int ret= 0;
                char *p= start;
                char sep= *++p;
                char *key, *val;
                p++;
                while(ret >= 0 && (key= strchr(p, sep)) && p<key && (val = strchr(key+1, sep))){
                    *val= *key= 0;
                    ret= av_opt_set(uc->priv_data, p, key+1, 0);
                    if (ret == AVERROR_OPTION_NOT_FOUND)
                        av_log(uc, AV_LOG_ERROR, "Key '%s' not found.\n", p);
                    *val= *key= sep;
                    p= val+1;
                }
                if(ret<0 || p!=key){
                    av_log(uc, AV_LOG_ERROR, "Error parsing options string %s\n", start);
                    av_freep(&uc->priv_data);
                    av_freep(&uc);
                    goto fail;
                }
                memmove(start, key+1, strlen(key));
            }
        }
    }
    if (int_cb)
        uc->interrupt_callback = *int_cb;

    *puc = uc;
    return 0;
 fail:
    *puc = NULL;
#if CONFIG_NETWORK
    ff_network_close();
#endif
    return err;
}

int ffurl_connect(URLContext* uc, AVDictionary **options)
{
    int err =
#if !FF_API_OLD_AVIO
        uc->prot->url_open2 ? uc->prot->url_open2(uc, uc->filename, uc->flags, options) :
#endif
        uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err)
        return err;
    uc->is_connected = 1;
    //We must be careful here as ffurl_seek() could be slow, for example for http
    if(   (uc->flags & AVIO_FLAG_WRITE)
       || !strcmp(uc->prot->name, "file"))
        if(!uc->is_streamed && ffurl_seek(uc, 0, SEEK_SET) < 0)
            uc->is_streamed= 1;
    return 0;
}

#if FF_API_OLD_AVIO
int url_open_protocol (URLContext **puc, struct URLProtocol *up,
                       const char *filename, int flags)
{
    int ret;

    ret = url_alloc_for_protocol(puc, up, filename, flags, NULL);
    if (ret)
        goto fail;
    ret = ffurl_connect(*puc, NULL);
    if (!ret)
        return 0;
 fail:
    ffurl_close(*puc);
    *puc = NULL;
    return ret;
}
int url_alloc(URLContext **puc, const char *filename, int flags)
{
    return ffurl_alloc(puc, filename, flags, NULL);
}
int url_connect(URLContext* uc)
{
    return ffurl_connect(uc, NULL);
}
int url_open(URLContext **puc, const char *filename, int flags)
{
    return ffurl_open(puc, filename, flags, NULL, NULL);
}
int url_read(URLContext *h, unsigned char *buf, int size)
{
    return ffurl_read(h, buf, size);
}
int url_read_complete(URLContext *h, unsigned char *buf, int size)
{
    return ffurl_read_complete(h, buf, size);
}
int url_write(URLContext *h, const unsigned char *buf, int size)
{
    return ffurl_write(h, buf, size);
}
int64_t url_seek(URLContext *h, int64_t pos, int whence)
{
    return ffurl_seek(h, pos, whence);
}
int url_close(URLContext *h)
{
    return ffurl_close(h);
}
int64_t url_filesize(URLContext *h)
{
    return ffurl_size(h);
}
int url_get_file_handle(URLContext *h)
{
    return ffurl_get_file_handle(h);
}
int url_get_max_packet_size(URLContext *h)
{
    return h->max_packet_size;
}
void url_get_filename(URLContext *h, char *buf, int buf_size)
{
    av_strlcpy(buf, h->filename, buf_size);
}
void url_set_interrupt_cb(URLInterruptCB *interrupt_cb)
{
    avio_set_interrupt_cb(interrupt_cb);
}
int av_register_protocol2(URLProtocol *protocol, int size)
{
    return ffurl_register_protocol(protocol, size);
}
#endif

#define URL_SCHEME_CHARS                        \
    "abcdefghijklmnopqrstuvwxyz"                \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                \
    "0123456789+-."

int ffurl_alloc(URLContext **puc, const char *filename, int flags,
                const AVIOInterruptCB *int_cb)
{
    URLProtocol *up = NULL;
    char proto_str[128], proto_nested[128], *ptr;
    size_t proto_len = strspn(filename, URL_SCHEME_CHARS);

    if (!first_protocol) {
        av_log(NULL, AV_LOG_WARNING, "No URL Protocols are registered. "
                                     "Missing call to av_register_all()?\n");
    }

    if (filename[proto_len] != ':' &&  filename[proto_len] != ',' || is_dos_path(filename))
        strcpy(proto_str, "file");
    else
        av_strlcpy(proto_str, filename, FFMIN(proto_len+1, sizeof(proto_str)));

    if ((ptr = strchr(proto_str, ',')))
        *ptr = '\0';
    av_strlcpy(proto_nested, proto_str, sizeof(proto_nested));
    if ((ptr = strchr(proto_nested, '+')))
        *ptr = '\0';

    while (up = ffurl_protocol_next(up)) {
        if (!strcmp(proto_str, up->name))
            return url_alloc_for_protocol (puc, up, filename, flags, int_cb);
        if (up->flags & URL_PROTOCOL_FLAG_NESTED_SCHEME &&
            !strcmp(proto_nested, up->name))
            return url_alloc_for_protocol (puc, up, filename, flags, int_cb);
    }
    *puc = NULL;
    return AVERROR(ENOENT);
}

int ffurl_open(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    int ret = ffurl_alloc(puc, filename, flags, int_cb);
    if (ret)
        return ret;
    if (options && (*puc)->prot->priv_data_class &&
        (ret = av_opt_set_dict((*puc)->priv_data, options)) < 0)
        goto fail;
    ret = ffurl_connect(*puc, options);
    if (!ret)
        return 0;
fail:
    ffurl_close(*puc);
    *puc = NULL;
    return ret;
}

static inline int retry_transfer_wrapper(URLContext *h, unsigned char *buf, int size, int size_min,
                                         int (*transfer_func)(URLContext *h, unsigned char *buf, int size))
{
    int ret, len;
    int fast_retries = 5;

    len = 0;
    while (len < size_min) {
        ret = transfer_func(h, buf+len, size-len);
        if (ret == AVERROR(EINTR))
            continue;
        if (h->flags & AVIO_FLAG_NONBLOCK)
            return ret;
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
        if (len < size && ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
    }
    return len;
}

int ffurl_read(URLContext *h, unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_READ))
        return AVERROR(EIO);
    return retry_transfer_wrapper(h, buf, size, 1, h->prot->url_read);
}

int ffurl_read_complete(URLContext *h, unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_READ))
        return AVERROR(EIO);
    return retry_transfer_wrapper(h, buf, size, size, h->prot->url_read);
}

int ffurl_write(URLContext *h, const unsigned char *buf, int size)
{
    if (!(h->flags & AVIO_FLAG_WRITE))
        return AVERROR(EIO);
    /* avoid sending too big packets */
    if (h->max_packet_size && size > h->max_packet_size)
        return AVERROR(EIO);

    return retry_transfer_wrapper(h, buf, size, size, (void*)h->prot->url_write);
}

int64_t ffurl_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t ret;

    if (!h->prot->url_seek)
        return AVERROR(ENOSYS);
    ret = h->prot->url_seek(h, pos, whence & ~AVSEEK_FORCE);
    return ret;
}

int ffurl_close(URLContext *h)
{
    int ret = 0;
    if (!h) return 0; /* can happen when ffurl_open fails */

    if (h->is_connected && h->prot->url_close)
        ret = h->prot->url_close(h);
#if CONFIG_NETWORK
    ff_network_close();
#endif
    if (h->prot->priv_data_size) {
        if (h->prot->priv_data_class)
            av_opt_free(h->priv_data);
        av_free(h->priv_data);
    }
    av_free(h);
    return ret;
}

#if FF_API_OLD_AVIO
int url_exist(const char *filename)
{
    URLContext *h;
    if (ffurl_open(&h, filename, AVIO_FLAG_READ, NULL, NULL) < 0)
        return 0;
    ffurl_close(h);
    return 1;
}
#endif

int avio_check(const char *url, int flags)
{
    URLContext *h;
    int ret = ffurl_alloc(&h, url, flags, NULL);
    if (ret)
        return ret;

    if (h->prot->url_check) {
        ret = h->prot->url_check(h, flags);
    } else {
        ret = ffurl_connect(h, NULL);
        if (ret >= 0)
            ret = flags;
    }

    ffurl_close(h);
    return ret;
}

int64_t ffurl_size(URLContext *h)
{
    int64_t pos, size;

    size= ffurl_seek(h, 0, AVSEEK_SIZE);
    if(size<0){
        pos = ffurl_seek(h, 0, SEEK_CUR);
        if ((size = ffurl_seek(h, -1, SEEK_END)) < 0)
            return size;
        size++;
        ffurl_seek(h, pos, SEEK_SET);
    }
    return size;
}

int ffurl_get_file_handle(URLContext *h)
{
    if (!h->prot->url_get_file_handle)
        return -1;
    return h->prot->url_get_file_handle(h);
}

#if FF_API_OLD_INTERRUPT_CB
static int default_interrupt_cb(void)
{
    return 0;
}

void avio_set_interrupt_cb(int (*interrupt_cb)(void))
{
    if (!interrupt_cb)
        interrupt_cb = default_interrupt_cb;
    url_interrupt_cb = interrupt_cb;
}
#endif

int ff_check_interrupt(AVIOInterruptCB *cb)
{
    int ret;
    if (cb && cb->callback && (ret = cb->callback(cb->opaque)))
        return ret;
#if FF_API_OLD_INTERRUPT_CB
    return url_interrupt_cb();
#else
    return 0;
#endif
}

#if FF_API_OLD_AVIO
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
#endif
