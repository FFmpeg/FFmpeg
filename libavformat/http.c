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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __BEOS__
# include <arpa/inet.h>
#else
# include "barpainet.h"
#endif
#include <netdb.h>


/* XXX: POST protocol is not completly implemented because ffmpeg use
   only a subset of it */

//#define DEBUG

/* used for protocol handling */
#define BUFFER_SIZE 1024
#define URL_SIZE    4096

typedef struct {
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    char location[URL_SIZE];
} HTTPContext;

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth);
static int http_write(URLContext *h, uint8_t *buf, int size);
static char *b64_encode( unsigned char *src );


/* return non zero if error */
static int http_open(URLContext *h, const char *uri, int flags)
{
    const char *path, *proxy_path;
    char hostname[1024], hoststr[1024];
    char auth[1024];
    char path1[1024];
    char buf[1024];
    int port, use_proxy, err;
    HTTPContext *s;
    URLContext *hd = NULL;

    h->is_streamed = 1;

    s = av_malloc(sizeof(HTTPContext));
    if (!s) {
        return -ENOMEM;
    }
    h->priv_data = s;

    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") && 
        strstart(proxy_path, "http://", NULL);

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port, 
              path1, sizeof(path1), uri);
    if (port > 0) {
        snprintf(hoststr, sizeof(hoststr), "%s:%d", hostname, port);
    } else {
        pstrcpy(hoststr, sizeof(hoststr), hostname);
    }

    if (use_proxy) {
        url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port, 
                  NULL, 0, proxy_path);
        path = uri;
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
    if (http_connect(h, path, hoststr, auth) < 0)
        goto fail;
    if (s->http_code == 303 && s->location[0] != '\0') {
        /* url moved, get next */
        uri = s->location;
        url_close(hd);
        goto redo;
    }
    return 0;
 fail:
    if (hd)
        url_close(hd);
    av_free(s);
    return AVERROR_IO;
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

static int http_connect(URLContext *h, const char *path, const char *hoststr,
                        const char *auth)
{
    HTTPContext *s = h->priv_data;
    int post, err, ch;
    char line[1024], *q;


    /* send http header */
    post = h->flags & URL_WRONLY;

    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.0\r\n"
             "User-Agent: %s\r\n"
             "Accept: */*\r\n"
             "Host: %s\r\n"
             "Authorization: Basic %s\r\n"
             "\r\n",
             post ? "POST" : "GET",
             path,
             LIBAVFORMAT_IDENT,
             hoststr,
             b64_encode(auth));
    
    if (http_write(h, s->buffer, strlen(s->buffer)) < 0)
        return AVERROR_IO;
        
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
            return AVERROR_IO;
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

URLProtocol http_protocol = {
    "http",
    http_open,
    http_read,
    http_write,
    NULL, /* seek */
    http_close,
};

/*****************************************************************************
 * b64_encode: stolen from VLC's http.c
 *****************************************************************************/
                                                                                
static char *b64_encode( unsigned char *src )
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int len= strlen(src);
    char *ret, *dst;
    unsigned i_bits = 0;
    unsigned i_shift = 0;

    if(len < UINT_MAX/4){
        ret=dst= av_malloc( len * 4 / 3 + 12 );
    }else
        return NULL;

    for( ;; )
    {
        if( *src )
        {
            i_bits = ( i_bits << 8 )|( *src++ );
            i_shift += 8;
        }
        else if( i_shift > 0 )
        {
           i_bits <<= 6 - i_shift;
           i_shift = 6;
        }
        else
        {
            *dst++ = '=';
            break;
        }
                                                                                
        while( i_shift >= 6 )
        {
            i_shift -= 6;
            *dst++ = b64[(i_bits >> i_shift)&0x3f];
        }
    }
                                                                                
    *dst++ = '\0';
                                                                                
    return ret;
}

