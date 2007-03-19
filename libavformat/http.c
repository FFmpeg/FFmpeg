/*
 * HTTP protocol for ffmpeg client
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "avformat.h"
#include <unistd.h>
#include "network.h"

#include "base64.h"

/* XXX: POST protocol is not completly implemented because ffmpeg use
   only a subset of it */

//#define DEBUG

/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096
#define MAX_REDIRECTS 8

typedef struct {
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    offset_t off, filesize;
    char location[URL_SIZE];
} HTTPContext;

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location);
static int http_write(URLContext *h, uint8_t *buf, int size);


/* return non zero if error */
static int http_open_cnx(URLContext *h)
{
    const char *path, *proxy_path;
    char hostname[1024], hoststr[1024];
    char auth[1024];
    char path1[1024];
    char buf[1024];
    int port, use_proxy, err, location_changed = 0, redirects = 0;
    HTTPContext *s = h->priv_data;
    URLContext *hd = NULL;

    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") &&
        strstart(proxy_path, "http://", NULL);

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
              path1, sizeof(path1), s->location);
    if (port > 0) {
        snprintf(hoststr, sizeof(hoststr), "%s:%d", hostname, port);
    } else {
        pstrcpy(hoststr, sizeof(hoststr), hostname);
    }

    if (use_proxy) {
        url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                  NULL, 0, proxy_path);
        path = s->location;
    } else {
        if (path1[0] == '\0')
            path = "/";
        else
            path = path1;
    }
    if (port < 0)
        port = 80;

    snprintf(buf, sizeof(buf), "tcp://%s:%d", hostname, port);
    err = url_open(&hd, buf, URL_RDWR);
    if (err < 0)
        goto fail;

    s->hd = hd;
    if (http_connect(h, path, hoststr, auth, &location_changed) < 0)
        goto fail;
    if ((s->http_code == 302 || s->http_code == 303) && location_changed == 1) {
        /* url moved, get next */
        url_close(hd);
        if (redirects++ >= MAX_REDIRECTS)
            return AVERROR_IO;
        location_changed = 0;
        goto redo;
    }
    return 0;
 fail:
    if (hd)
        url_close(hd);
    return AVERROR_IO;
}

static int http_open(URLContext *h, const char *uri, int flags)
{
    HTTPContext *s;
    int ret;

    h->is_streamed = 1;

    s = av_malloc(sizeof(HTTPContext));
    if (!s) {
        return AVERROR(ENOMEM);
    }
    h->priv_data = s;
    s->filesize = -1;
    s->off = 0;
    pstrcpy (s->location, URL_SIZE, uri);

    ret = http_open_cnx(h);
    if (ret != 0)
        av_free (s);
    return ret;
}
static int http_getc(HTTPContext *s)
{
    int len;
    if (s->buf_ptr >= s->buf_end) {
        len = url_read(s->hd, s->buffer, BUFFER_SIZE);
        if (len < 0) {
            return AVERROR_IO;
        } else if (len == 0) {
            return -1;
        } else {
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer + len;
        }
    }
    return *s->buf_ptr++;
}

static int process_line(URLContext *h, char *line, int line_count,
                        int *new_location)
{
    HTTPContext *s = h->priv_data;
    char *tag, *p;

    /* end of header */
    if (line[0] == '\0')
        return 0;

    p = line;
    if (line_count == 0) {
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        s->http_code = strtol(p, NULL, 10);
#ifdef DEBUG
        printf("http_code=%d\n", s->http_code);
#endif
        /* error codes are 4xx and 5xx */
        if (s->http_code >= 400 && s->http_code < 600)
            return -1;
    } else {
        while (*p != '\0' && *p != ':')
            p++;
        if (*p != ':')
            return 1;

        *p = '\0';
        tag = line;
        p++;
        while (isspace(*p))
            p++;
        if (!strcmp(tag, "Location")) {
            strcpy(s->location, p);
            *new_location = 1;
        } else if (!strcmp (tag, "Content-Length") && s->filesize == -1) {
            s->filesize = atoll(p);
        } else if (!strcmp (tag, "Content-Range")) {
            /* "bytes $from-$to/$document_size" */
            const char *slash;
            if (!strncmp (p, "bytes ", 6)) {
                p += 6;
                s->off = atoll(p);
                if ((slash = strchr(p, '/')) && strlen(slash) > 0)
                    s->filesize = atoll(slash+1);
            }
            h->is_streamed = 0; /* we _can_ in fact seek */
        }
    }
    return 1;
}

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth, int *new_location)
{
    HTTPContext *s = h->priv_data;
    int post, err, ch;
    char line[1024], *q;
    char *auth_b64;
    int auth_b64_len = strlen(auth)* 4 / 3 + 12;
    offset_t off = s->off;


    /* send http header */
    post = h->flags & URL_WRONLY;
    auth_b64 = av_malloc(auth_b64_len);
    av_base64_encode(auth_b64, auth_b64_len, (uint8_t *)auth, strlen(auth));
    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.1\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "Range: bytes=%"PRId64"-\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             post ? "POST" : "GET",
             path,
             LIBAVFORMAT_IDENT,
             s->off,
             hoststr,
             auth_b64);

    av_freep(&auth_b64);
    if (http_write(h, s->buffer, strlen(s->buffer)) < 0)
        return AVERROR_IO;

    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->off = 0;
    s->filesize = -1;
    if (post) {
        sleep(1);
        return 0;
    }

    /* wait for header */
    q = line;
    for(;;) {
        ch = http_getc(s);
        if (ch < 0)
            return AVERROR_IO;
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';
#ifdef DEBUG
            printf("header='%s'\n", line);
#endif
            err = process_line(h, line, s->line_count, new_location);
            if (err < 0)
                return err;
            if (err == 0)
                break;
            s->line_count++;
            q = line;
        } else {
            if ((q - line) < sizeof(line) - 1)
                *q++ = ch;
        }
    }

    return (off == s->off) ? 0 : -1;
}


static int http_read(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int len;

    /* read bytes from input buffer first */
    len = s->buf_end - s->buf_ptr;
    if (len > 0) {
        if (len > size)
            len = size;
        memcpy(buf, s->buf_ptr, len);
        s->buf_ptr += len;
    } else {
        len = url_read(s->hd, buf, size);
    }
    if (len > 0)
        s->off += len;
    return len;
}

/* used only when posting data */
static int http_write(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    return url_write(s->hd, buf, size);
}

static int http_close(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    url_close(s->hd);
    av_free(s);
    return 0;
}

static offset_t http_seek(URLContext *h, offset_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    URLContext *old_hd = s->hd;
    offset_t old_off = s->off;

    if (whence == AVSEEK_SIZE)
        return s->filesize;
    else if ((s->filesize == -1 && whence == SEEK_END) || h->is_streamed)
        return -1;

    /* we save the old context in case the seek fails */
    s->hd = NULL;
    if (whence == SEEK_CUR)
        off += s->off;
    else if (whence == SEEK_END)
        off += s->filesize;
    s->off = off;

    /* if it fails, continue on old connection */
    if (http_open_cnx(h) < 0) {
        s->hd = old_hd;
        s->off = old_off;
        return -1;
    }
    url_close(old_hd);
    return off;
}

URLProtocol http_protocol = {
    "http",
    http_open,
    http_read,
    http_write,
    http_seek,
    http_close,
};
