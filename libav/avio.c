/*
 * Unbuffered io for ffmpeg system
 * Copyright (c) 2001 Gerard Lantau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "avformat.h"

URLProtocol *first_protocol = NULL;

int register_protocol(URLProtocol *protocol)
{
    URLProtocol **p;
    p = &first_protocol;
    while (*p != NULL) p = &(*p)->next;
    *p = protocol;
    protocol->next = NULL;
    return 0;
}

int url_open(URLContext **puc, const char *filename, int flags)
{
    URLContext *uc;
    URLProtocol *up;
    const char *p;
    char proto_str[128], *q;
    int err;

    p = filename;
    q = proto_str;
    while (*p != '\0' && *p != ':') {
        if ((q - proto_str) < sizeof(proto_str) - 1)
            *q++ = *p;
        p++;
    }
    /* if the protocol has length 1, we consider it is a dos drive */
    if (*p == '\0' || (q - proto_str) <= 1) {
        strcpy(proto_str, "file");
    } else {
        *q = '\0';
    }
    
    up = first_protocol;
    while (up != NULL) {
        if (!strcmp(proto_str, up->name))
            goto found;
        up = up->next;
    }
    return -ENOENT;
 found:
    uc = malloc(sizeof(URLContext));
    if (!uc)
        return -ENOMEM;
    uc->prot = up;
    uc->flags = flags;
    uc->is_streamed = 0; /* default = not streamed */
    uc->packet_size = 1; /* default packet size */
    err = up->url_open(uc, filename, flags);
    if (err < 0) {
        free(uc);
        *puc = NULL;
        return err;
    }
    *puc = uc;
    return 0;
}

int url_read(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    if (h->flags & URL_WRONLY)
        return -EIO;
    ret = h->prot->url_read(h, buf, size);
    return ret;
}

int url_write(URLContext *h, unsigned char *buf, int size)
{
    int ret;
    if (!(h->flags & URL_WRONLY))
        return -EIO;
    ret = h->prot->url_write(h, buf, size);
    return ret;
}

offset_t url_seek(URLContext *h, offset_t pos, int whence)
{
    offset_t ret;

    if (!h->prot->url_seek)
        return -EPIPE;
    ret = h->prot->url_seek(h, pos, whence);
    return ret;
}

int url_getformat(URLContext *h, URLFormat *f)
{
    memset(f, 0, sizeof(*f));
    if (!h->prot->url_getformat)
        return -ENODATA;
    return h->prot->url_getformat(h, f);
}

int url_close(URLContext *h)
{
    int ret;

    ret = h->prot->url_close(h);
    free(h);
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

offset_t url_filesize(URLContext *h)
{
    offset_t pos, size;
    
    pos = url_seek(h, 0, SEEK_CUR);
    size = url_seek(h, 0, SEEK_END);
    url_seek(h, pos, SEEK_SET);
    return size;
}
