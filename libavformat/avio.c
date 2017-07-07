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

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/avassert.h"
#include "os_support.h"
#include "avformat.h"
#if CONFIG_NETWORK
#include "network.h"
#endif
#include "url.h"

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

#define OFFSET(x) offsetof(URLContext,x)
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"protocol_whitelist", "List of protocols that are allowed to be used", OFFSET(protocol_whitelist), AV_OPT_TYPE_STRING, { .str = NULL },  CHAR_MIN, CHAR_MAX, D },
    {"protocol_blacklist", "List of protocols that are not allowed to be used", OFFSET(protocol_blacklist), AV_OPT_TYPE_STRING, { .str = NULL },  CHAR_MIN, CHAR_MAX, D },
    {"rw_timeout", "Timeout for IO operations (in microseconds)", offsetof(URLContext, rw_timeout), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};

const AVClass ffurl_context_class = {
    .class_name       = "URLContext",
    .item_name        = urlcontext_to_name,
    .option           = options,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = urlcontext_child_next,
    .child_class_next = ff_urlcontext_child_class_next,
};
/*@}*/

static int url_alloc_for_protocol(URLContext **puc, const URLProtocol *up,
                                  const char *filename, int flags,
                                  const AVIOInterruptCB *int_cb)
{
    URLContext *uc;
    int err;

#if CONFIG_NETWORK
    if (up->flags & URL_PROTOCOL_FLAG_NETWORK && !ff_network_init())
        return AVERROR(EIO);
#endif
    if ((flags & AVIO_FLAG_READ) && !up->url_read) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for reading\n", up->name);
        return AVERROR(EIO);
    }
    if ((flags & AVIO_FLAG_WRITE) && !up->url_write) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to open the '%s' protocol for writing\n", up->name);
        return AVERROR(EIO);
    }
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
            int proto_len= strlen(up->name);
            char *start = strchr(uc->filename, ',');
            *(const AVClass **)uc->priv_data = up->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
            if(!strncmp(up->name, uc->filename, proto_len) && uc->filename + proto_len == start){
                int ret= 0;
                char *p= start;
                char sep= *++p;
                char *key, *val;
                p++;

                if (strcmp(up->name, "subfile"))
                    ret = AVERROR(EINVAL);

                while(ret >= 0 && (key= strchr(p, sep)) && p<key && (val = strchr(key+1, sep))){
                    *val= *key= 0;
                    if (strcmp(p, "start") && strcmp(p, "end")) {
                        ret = AVERROR_OPTION_NOT_FOUND;
                    } else
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
                    err = AVERROR(EINVAL);
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
    int err;
    AVDictionary *tmp_opts = NULL;
    AVDictionaryEntry *e;

    if (!options)
        options = &tmp_opts;

    // Check that URLContext was initialized correctly and lists are matching if set
    av_assert0(!(e=av_dict_get(*options, "protocol_whitelist", NULL, 0)) ||
               (uc->protocol_whitelist && !strcmp(uc->protocol_whitelist, e->value)));
    av_assert0(!(e=av_dict_get(*options, "protocol_blacklist", NULL, 0)) ||
               (uc->protocol_blacklist && !strcmp(uc->protocol_blacklist, e->value)));

    if (uc->protocol_whitelist && av_match_list(uc->prot->name, uc->protocol_whitelist, ',') <= 0) {
        av_log(uc, AV_LOG_ERROR, "Protocol '%s' not on whitelist '%s'!\n", uc->prot->name, uc->protocol_whitelist);
        return AVERROR(EINVAL);
    }

    if (uc->protocol_blacklist && av_match_list(uc->prot->name, uc->protocol_blacklist, ',') > 0) {
        av_log(uc, AV_LOG_ERROR, "Protocol '%s' on blacklist '%s'!\n", uc->prot->name, uc->protocol_blacklist);
        return AVERROR(EINVAL);
    }

    if (!uc->protocol_whitelist && uc->prot->default_whitelist) {
        av_log(uc, AV_LOG_DEBUG, "Setting default whitelist '%s'\n", uc->prot->default_whitelist);
        uc->protocol_whitelist = av_strdup(uc->prot->default_whitelist);
        if (!uc->protocol_whitelist) {
            return AVERROR(ENOMEM);
        }
    } else if (!uc->protocol_whitelist)
        av_log(uc, AV_LOG_DEBUG, "No default whitelist set\n"); // This should be an error once all declare a default whitelist

    if ((err = av_dict_set(options, "protocol_whitelist", uc->protocol_whitelist, 0)) < 0)
        return err;
    if ((err = av_dict_set(options, "protocol_blacklist", uc->protocol_blacklist, 0)) < 0)
        return err;

    err =
        uc->prot->url_open2 ? uc->prot->url_open2(uc,
                                                  uc->filename,
                                                  uc->flags,
                                                  options) :
        uc->prot->url_open(uc, uc->filename, uc->flags);

    av_dict_set(options, "protocol_whitelist", NULL, 0);
    av_dict_set(options, "protocol_blacklist", NULL, 0);

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

int ffurl_accept(URLContext *s, URLContext **c)
{
    av_assert0(!*c);
    if (s->prot->url_accept)
        return s->prot->url_accept(s, c);
    return AVERROR(EBADF);
}

int ffurl_handshake(URLContext *c)
{
    int ret;
    if (c->prot->url_handshake) {
        ret = c->prot->url_handshake(c);
        if (ret)
            return ret;
    }
    c->is_connected = 1;
    return 0;
}

#define URL_SCHEME_CHARS                        \
    "abcdefghijklmnopqrstuvwxyz"                \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"                \
    "0123456789+-."

static const struct URLProtocol *url_find_protocol(const char *filename)
{
    const URLProtocol **protocols;
    char proto_str[128], proto_nested[128], *ptr;
    size_t proto_len = strspn(filename, URL_SCHEME_CHARS);
    int i;

    if (filename[proto_len] != ':' &&
        (strncmp(filename, "subfile,", 8) || !strchr(filename + proto_len + 1, ':')) ||
        is_dos_path(filename))
        strcpy(proto_str, "file");
    else
        av_strlcpy(proto_str, filename,
                   FFMIN(proto_len + 1, sizeof(proto_str)));

    av_strlcpy(proto_nested, proto_str, sizeof(proto_nested));
    if ((ptr = strchr(proto_nested, '+')))
        *ptr = '\0';

    protocols = ffurl_get_protocols(NULL, NULL);
    if (!protocols)
        return NULL;
    for (i = 0; protocols[i]; i++) {
            const URLProtocol *up = protocols[i];
        if (!strcmp(proto_str, up->name)) {
            av_freep(&protocols);
            return up;
        }
        if (up->flags & URL_PROTOCOL_FLAG_NESTED_SCHEME &&
            !strcmp(proto_nested, up->name)) {
            av_freep(&protocols);
            return up;
        }
    }
    av_freep(&protocols);

    return NULL;
}

int ffurl_alloc(URLContext **puc, const char *filename, int flags,
                const AVIOInterruptCB *int_cb)
{
    const URLProtocol *p = NULL;

    p = url_find_protocol(filename);
    if (p)
       return url_alloc_for_protocol(puc, p, filename, flags, int_cb);

    *puc = NULL;
    if (av_strstart(filename, "https:", NULL))
        av_log(NULL, AV_LOG_WARNING, "https protocol not found, recompile FFmpeg with "
                                     "openssl, gnutls "
                                     "or securetransport enabled.\n");
    return AVERROR_PROTOCOL_NOT_FOUND;
}

int ffurl_open_whitelist(URLContext **puc, const char *filename, int flags,
                         const AVIOInterruptCB *int_cb, AVDictionary **options,
                         const char *whitelist, const char* blacklist,
                         URLContext *parent)
{
    AVDictionary *tmp_opts = NULL;
    AVDictionaryEntry *e;
    int ret = ffurl_alloc(puc, filename, flags, int_cb);
    if (ret < 0)
        return ret;
    if (parent)
        av_opt_copy(*puc, parent);
    if (options &&
        (ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;
    if (options && (*puc)->prot->priv_data_class &&
        (ret = av_opt_set_dict((*puc)->priv_data, options)) < 0)
        goto fail;

    if (!options)
        options = &tmp_opts;

    av_assert0(!whitelist ||
               !(e=av_dict_get(*options, "protocol_whitelist", NULL, 0)) ||
               !strcmp(whitelist, e->value));
    av_assert0(!blacklist ||
               !(e=av_dict_get(*options, "protocol_blacklist", NULL, 0)) ||
               !strcmp(blacklist, e->value));

    if ((ret = av_dict_set(options, "protocol_whitelist", whitelist, 0)) < 0)
        goto fail;

    if ((ret = av_dict_set(options, "protocol_blacklist", blacklist, 0)) < 0)
        goto fail;

    if ((ret = av_opt_set_dict(*puc, options)) < 0)
        goto fail;

    ret = ffurl_connect(*puc, options);

    if (!ret)
        return 0;
fail:
    ffurl_close(*puc);
    *puc = NULL;
    return ret;
}

int ffurl_open(URLContext **puc, const char *filename, int flags,
               const AVIOInterruptCB *int_cb, AVDictionary **options)
{
    return ffurl_open_whitelist(puc, filename, flags,
                                int_cb, options, NULL, NULL, NULL);
}

static inline int retry_transfer_wrapper(URLContext *h, uint8_t *buf,
                                         int size, int size_min,
                                         int (*transfer_func)(URLContext *h,
                                                              uint8_t *buf,
                                                              int size))
{
    int ret, len;
    int fast_retries = 5;
    int64_t wait_since = 0;

    len = 0;
    while (len < size_min) {
        if (ff_check_interrupt(&h->interrupt_callback))
            return AVERROR_EXIT;
        ret = transfer_func(h, buf + len, size - len);
        if (ret == AVERROR(EINTR))
            continue;
        if (h->flags & AVIO_FLAG_NONBLOCK)
            return ret;
        if (ret == AVERROR(EAGAIN)) {
            ret = 0;
            if (fast_retries) {
                fast_retries--;
            } else {
                if (h->rw_timeout) {
                    if (!wait_since)
                        wait_since = av_gettime_relative();
                    else if (av_gettime_relative() > wait_since + h->rw_timeout)
                        return AVERROR(EIO);
                }
                av_usleep(1000);
            }
        } else if (ret < 1)
            return (ret < 0 && ret != AVERROR_EOF) ? ret : len;
        if (ret) {
            fast_retries = FFMAX(fast_retries, 2);
            wait_since = 0;
        }
        len += ret;
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

    return retry_transfer_wrapper(h, (unsigned char *)buf, size, size,
                                  (int (*)(struct URLContext *, uint8_t *, int))
                                  h->prot->url_write);
}

int64_t ffurl_seek(URLContext *h, int64_t pos, int whence)
{
    int64_t ret;

    if (!h->prot->url_seek)
        return AVERROR(ENOSYS);
    ret = h->prot->url_seek(h, pos, whence & ~AVSEEK_FORCE);
    return ret;
}

int ffurl_closep(URLContext **hh)
{
    URLContext *h= *hh;
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
        av_freep(&h->priv_data);
    }
    av_opt_free(h);
    av_freep(hh);
    return ret;
}

int ffurl_close(URLContext *h)
{
    return ffurl_closep(&h);
}


const char *avio_find_protocol_name(const char *url)
{
    const URLProtocol *p = url_find_protocol(url);

    return p ? p->name : NULL;
}

int avio_check(const char *url, int flags)
{
    URLContext *h;
    int ret = ffurl_alloc(&h, url, flags, NULL);
    if (ret < 0)
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

int avpriv_io_move(const char *url_src, const char *url_dst)
{
    URLContext *h_src, *h_dst;
    int ret = ffurl_alloc(&h_src, url_src, AVIO_FLAG_READ_WRITE, NULL);
    if (ret < 0)
        return ret;
    ret = ffurl_alloc(&h_dst, url_dst, AVIO_FLAG_WRITE, NULL);
    if (ret < 0) {
        ffurl_close(h_src);
        return ret;
    }

    if (h_src->prot == h_dst->prot && h_src->prot->url_move)
        ret = h_src->prot->url_move(h_src, h_dst);
    else
        ret = AVERROR(ENOSYS);

    ffurl_close(h_src);
    ffurl_close(h_dst);
    return ret;
}

int avpriv_io_delete(const char *url)
{
    URLContext *h;
    int ret = ffurl_alloc(&h, url, AVIO_FLAG_WRITE, NULL);
    if (ret < 0)
        return ret;

    if (h->prot->url_delete)
        ret = h->prot->url_delete(h);
    else
        ret = AVERROR(ENOSYS);

    ffurl_close(h);
    return ret;
}

int avio_open_dir(AVIODirContext **s, const char *url, AVDictionary **options)
{
    URLContext *h = NULL;
    AVIODirContext *ctx = NULL;
    int ret;
    av_assert0(s);

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = ffurl_alloc(&h, url, AVIO_FLAG_READ, NULL)) < 0)
        goto fail;

    if (h->prot->url_open_dir && h->prot->url_read_dir && h->prot->url_close_dir) {
        if (options && h->prot->priv_data_class &&
            (ret = av_opt_set_dict(h->priv_data, options)) < 0)
            goto fail;
        ret = h->prot->url_open_dir(h);
    } else
        ret = AVERROR(ENOSYS);
    if (ret < 0)
        goto fail;

    h->is_connected = 1;
    ctx->url_context = h;
    *s = ctx;
    return 0;

  fail:
    av_free(ctx);
    *s = NULL;
    ffurl_close(h);
    return ret;
}

int avio_read_dir(AVIODirContext *s, AVIODirEntry **next)
{
    URLContext *h;
    int ret;

    if (!s || !s->url_context)
        return AVERROR(EINVAL);
    h = s->url_context;
    if ((ret = h->prot->url_read_dir(h, next)) < 0)
        avio_free_directory_entry(next);
    return ret;
}

int avio_close_dir(AVIODirContext **s)
{
    URLContext *h;

    av_assert0(s);
    if (!(*s) || !(*s)->url_context)
        return AVERROR(EINVAL);
    h = (*s)->url_context;
    h->prot->url_close_dir(h);
    ffurl_close(h);
    av_freep(s);
    *s = NULL;
    return 0;
}

void avio_free_directory_entry(AVIODirEntry **entry)
{
    if (!entry || !*entry)
        return;
    av_free((*entry)->name);
    av_freep(entry);
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
        *handles = av_malloc(sizeof(**handles));
        if (!*handles)
            return AVERROR(ENOMEM);
        *numhandles = 1;
        *handles[0] = h->prot->url_get_file_handle(h);
        return 0;
    }
    return h->prot->url_get_multi_file_handle(h, handles, numhandles);
}

int ffurl_get_short_seek(URLContext *h)
{
    if (!h->prot->url_get_short_seek)
        return AVERROR(ENOSYS);
    return h->prot->url_get_short_seek(h);
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
