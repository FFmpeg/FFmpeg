/*
 * HTTP protocol for ffmpeg client
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


/* XXX: POST protocol is not completly implemented because ffmpeg use
   only a subset of it */

//#define DEBUG

/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096

typedef struct {
    int fd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    char location[URL_SIZE];
} HTTPContext;

static int http_connect(URLContext *h, const char *path);
static int http_write(URLContext *h, UINT8 *buf, int size);

/* return non zero if error */
static int http_open(URLContext *h, const char *uri, int flags)
{
    struct sockaddr_in dest_addr;
    const char *p, *path, *proxy_path;
    char hostname[1024], *q;
    int port, fd = -1, use_proxy;
    struct hostent *hp;
    HTTPContext *s;

    h->is_streamed = 1;

    s = av_malloc(sizeof(HTTPContext));
    if (!s) {
        return -ENOMEM;
    }
    h->priv_data = s;

    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") && (strncmp(proxy_path, "http://", 7) == 0);

    /* fill the dest addr */
 redo:
    if (use_proxy) {
        p = proxy_path;
    } else {
        p = uri;
    }
    if (!strstart(p, "http://", &p))
        goto fail;
    q = hostname;
    while (*p != ':' && *p != '/' && *p != '\0') {
        if ((q - hostname) < sizeof(hostname) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    port = 80;
    if (*p == ':') {
        p++;
        port = strtoul(p, (char **)&p, 10);
    }
    if (port <= 0)
        goto fail;
    if (use_proxy) {
        path = uri;
    } else {
        if (*p == '\0')
            path = "/";
        else
            path = p;
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if ((inet_aton(hostname, &dest_addr.sin_addr)) == 0) {
        hp = gethostbyname(hostname);
        if (!hp)
            goto fail;
        memcpy (&dest_addr.sin_addr, hp->h_addr, sizeof(dest_addr.sin_addr));
    }
    
    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto fail;

    if (connect(fd, (struct sockaddr *)&dest_addr, 
                sizeof(dest_addr)) < 0)
        goto fail;

    s->fd = fd;
    if (http_connect(h, path) < 0)
        goto fail;
    if (s->http_code == 303 && s->location[0] != '\0') {
        /* url moved, get next */
        uri = s->location;
        goto redo;
    }

    return 0;
 fail:
    if (fd >= 0)
        close(fd);
    av_free(s);
    return -EIO;
}

static int http_getc(HTTPContext *s)
{
    int len;
    if (s->buf_ptr >= s->buf_end) {
    redo:
        len = read(s->fd, s->buffer, BUFFER_SIZE);
        if (len < 0) {
            if (errno == EAGAIN || errno == EINTR)
                goto redo;
            return -EIO;
        } else if (len == 0) {
            return -1;
        } else {
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer + len;
        }
    }
    return *s->buf_ptr++;
}

static int process_line(HTTPContext *s, char *line, int line_count)
{
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
        }
    }
    return 1;
}

static int http_connect(URLContext *h, const char *path)
{
    HTTPContext *s = h->priv_data;
    int post, err, ch;
    char line[1024], *q;


    /* send http header */
    post = h->flags & URL_WRONLY;

    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.0\n"
             "User-Agent: FFmpeg %s\n"
             "Accept: */*\n"
             "\n",
             post ? "POST" : "GET",
             path,
             FFMPEG_VERSION
             );

    if (http_write(h, s->buffer, strlen(s->buffer)) < 0)
        return -EIO;
        
    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->location[0] = '\0';
    if (post) {
        sleep(1);
        return 0;
    }
    
    /* wait for header */
    q = line;
    for(;;) {
        ch = http_getc(s);
        if (ch < 0)
            return -EIO;
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';
#ifdef DEBUG
            printf("header='%s'\n", line);
#endif
            err = process_line(s, line, s->line_count);
            if (err < 0)
                return err;
            if (err == 0)
                return 0;
            s->line_count++;
            q = line;
        } else {
            if ((q - line) < sizeof(line) - 1)
                *q++ = ch;
        }
    }
}


static int http_read(URLContext *h, UINT8 *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int size1, len;

    size1 = size;
    while (size > 0) {
        /* read bytes from input buffer first */
        len = s->buf_end - s->buf_ptr;
        if (len > 0) {
            if (len > size)
                len = size;
            memcpy(buf, s->buf_ptr, len);
            s->buf_ptr += len;
        } else {
            len = read (s->fd, buf, size);
            if (len < 0) {
                if (errno != EINTR && errno != EAGAIN)
                    return -errno;
                else
                    continue;
            } else if (len == 0) {
                break;
            }
        }
        size -= len;
        buf += len;
    }
    return size1 - size;
}

/* used only when posting data */
static int http_write(URLContext *h, UINT8 *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int ret, size1;

    size1 = size;
    while (size > 0) {
        ret = write (s->fd, buf, size);
        if (ret < 0 && errno != EINTR && errno != EAGAIN)
            return -errno;
        size -= ret;
        buf += ret;
    }
    return size1 - size;
}

static int http_close(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    close(s->fd);
    return 0;
}

URLProtocol http_protocol = {
    "http",
    http_open,
    http_read,
    http_write,
    NULL, /* seek */
    http_close,
};
