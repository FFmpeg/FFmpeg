/*
 * unbuffered I/O
 * Copyright (c) 2001 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
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
    if (h->prot)
        return h->prot->name;
    else
        return "NULL";
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

static const AVOption options[] = { { NULL } };
const AVClass ffurl_context_class = {
    .class_name       = "URLContext",
    .item_name        = urlcontext_to_name,
    .option           = options,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = urlcontext_child_next,
    .child_class_next = urlcontext_child_class_next,
};
/*@}*/

const char *avio_enum_protocols(void **opaque, int output)
{
    URLProtocol *p;
    *opaque = ffurl_protocol_next(*opaque);
    if (!(p = *opaque))
        return NULL;
    if ((output && p->url_write) || (!output && p->url_read))
        return p->name;
    return avio_enum_protocols(opaque, output);
}

int ffurl_register_protocol(URLProtocol *protocol)
{
    URLProtocol **p;
    p = &first_protocol;
    while (*p != NULL)
        p = &(*p)->next;
    *p             = protocol;
    protocol->next = NULL;
    return 0;
}

static int url_alloc_for_protocol(URLContext **puc, struct URLProtocol *up,
                                  const char *filename, int flags,
                                  const AVIOInterruptCB *int_cb)
{
    URLContext *uc;
    int err;

#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK && !ff_network_init())
        return AVERROR(EIO);
#endif
    uc = av_mallocz(sizeof(URLContext) + strlen(filename) + 1);
    if (!uc) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    uc->av_class = &ffurl_context_class;
    uc->filename = (char *)&uc[1];
    strcpy(uc->filename, filename);
    uc->prot            = up;
    uc->flags           = flags;
    uc->is_streamed     = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (up->priv_data_size) {
        uc->priv_data = av_mallocz(up->priv_data_size);
        if (!uc->priv_data) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (up->priv_data_class) {
            *(const AVClass **)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }
    if (int_cb)
        uc->interrupt_callback = *int_cb;

    *puc = uc;
    return 0;
fail:
    *puc = NULL;
    if (uc)
        av_freep(&uc->priv_data);
    av_freep(&uc);
#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK)
        ff_network_close();
#endif
    return err;
}

int ffurl_connect(URLContext *uc, AVDictionary **options)
{
    int err =
        uc->prot->url_open2 ? uc->prot->url_open2(uc,
                                                  uc->filename,
                                                  uc->flags,
                                                  options) :
        uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err)
        return err;
    uc->is_connected = 1;
    /* We must be careful here as ffurl_seek() could be slow,
     * for example for http */
    if ((uc->flags & AVIO_FLAG_WRITE) || !strcmp(uc->prot->name, "file"))
        if (!uc->is_streamed && ffurl_seek(uc, 0, SEEK_SET) < 0)
            uc->is_streamed = 1;
    return 0;
}

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

    if (filename[proto_len] != ':' || is_dos_path(filename))
        strcpy(proto_str, "file");
    else
        av_strlcpy(proto_str, filename,
                   FFMIN(proto_len + 1, sizeof(proto_str)));

    av_strlcpy(proto_nested, proto_str, sizeof(proto_nested));
    if ((ptr = strchr(proto_nested, '+')))
        *ptr = '\0';

    while (up = ffurl_protocol_next(up)) {
        if (!strcmp(proto_str, up->name))
            return url_alloc_for_protocol(puc, up, filename, flags, int_cb);
        if (up->flags & URL_PROTOCOL_FLAG_NESTED_SCHEME &&
            !strcmp(proto_nested, up->name))
            return url_alloc_for_protocol(puc, up, filename, flags, int_cb);
    }
    *puc = NULL;
    return AVERROR_PROTOCOL_NOT_FOUND;
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

static inline int retry_transfer_wrapper(URLContext *h, uint8_t *buf,
                                         int size, int size_min,
                                         int (*transfer_func)(URLContext *h,
                                                              uint8_t *buf,
                                                              int size))
{
    int ret, len;
    int fast_retries = 5;

    len = 0;
    while (len < size_min) {
        ret = transfer_func(h, buf + len, size - len);
        if (ret == AVERROR(EINTR))
            continue;
        if (h->flags & AVIO_FLAG_NONBLOCK)
            return ret;
        if (ret == AVERROR(EAGAIN)) {
            ret = 0;
            if (fast_retries)
                fast_retries--;
            else
                av_usleep(1000);
        } else if (ret < 1)
            return (ret < 0 && ret != AVERROR_EOF) ? ret : len;
        if (ret)
            fast_retries = FFMAX(fast_retries, 2);
        len += ret;
        if (ff_check_interrupt(&h->interrupt_callback))
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

    return retry_transfer_wrapper(h, buf, size, size, h->prot->url_write);
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
    if (!h)
        return 0;     /* can happen when ffurl_open fails */

    if (h->is_connected && h->prot->url_close)
        ret = h->prot->url_close(h);
#if CONFIG_NETWORK
    if (h->prot->flags & URL_PROTOCOL_FLAG_NETWORK)
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

    size = ffurl_seek(h, 0, AVSEEK_SIZE);
    if (size < 0) {
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

int ffurl_get_multi_file_handle(URLContext *h, int **handles, int *numhandles)
{
    if (!h->prot->url_get_multi_file_handle) {
        if (!h->prot->url_get_file_handle)
            return AVERROR(ENOSYS);
        *handles = av_malloc(sizeof(*handles));
        if (!*handles)
            return AVERROR(ENOMEM);
        *numhandles = 1;
        *handles[0] = h->prot->url_get_file_handle(h);
        return 0;
    }
    return h->prot->url_get_multi_file_handle(h, handles, numhandles);
}

int ffurl_shutdown(URLContext *h, int flags)
{
    if (!h->prot->url_shutdown)
        return AVERROR(EINVAL);
    return h->prot->url_shutdown(h, flags);
}

int ff_check_interrupt(AVIOInterruptCB *cb)
{
    int ret;
    if (cb && cb->callback && (ret = cb->callback(cb->opaque)))
        return ret;
    return 0;
}
