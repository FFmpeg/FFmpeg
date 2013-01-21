/*
 * HTTP protocol for ffmpeg client
 * Copyright (c) 2000, 2001 Fabrice Bellard
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
#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "http.h"
#include "os_support.h"
#include "httpauth.h"
#include "url.h"
#include "libavutil/opt.h"

/* XXX: POST protocol is not completely implemented because ffmpeg uses
   only a subset of it. */

/* The IO buffer size is unrelated to the max URL size in itself, but needs
 * to be large enough to fit the full request headers (including long
 * path names).
 */
#define BUFFER_SIZE MAX_URL_SIZE
#define MAX_REDIRECTS 8

typedef struct {
    const AVClass *class;
    URLContext *hd;
    unsigned char buffer[BUFFER_SIZE], *buf_ptr, *buf_end;
    int line_count;
    int http_code;
    int64_t chunksize;      /**< Used if "Transfer-Encoding: chunked" otherwise -1. */
    char *content_type;
    char *user_agent;
    int64_t off, filesize;
    char location[MAX_URL_SIZE];
    HTTPAuthState auth_state;
    HTTPAuthState proxy_auth_state;
    char *headers;
    int willclose;          /**< Set if the server correctly handles Connection: close and will close the connection after feeding us the content. */
    int seekable;           /**< Control seekability, 0 = disable, 1 = enable, -1 = probe. */
    int chunked_post;
    int end_chunked_post;   /**< A flag which indicates if the end of chunked encoding has been sent. */
    int end_header;         /**< A flag which indicates we have finished to read POST reply. */
    int multiple_requests;  /**< A flag which indicates if we use persistent connections. */
    uint8_t *post_data;
    int post_datalen;
    int is_akamai;
    int rw_timeout;
    char *mime_type;
    char *cookies;          ///< holds newline (\n) delimited Set-Cookie header field values (without the "Set-Cookie: " field name)
} HTTPContext;

#define OFFSET(x) offsetof(HTTPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
{"seekable", "control seekability of connection", OFFSET(seekable), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, D },
{"chunked_post", "use chunked transfer-encoding for posts", OFFSET(chunked_post), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
{"headers", "set custom HTTP headers, can override built in default headers", OFFSET(headers), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
{"content_type", "force a content type", OFFSET(content_type), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
{"user-agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC},
{"multiple_requests", "use persistent connections", OFFSET(multiple_requests), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, D|E },
{"post_data", "set custom HTTP post data", OFFSET(post_data), AV_OPT_TYPE_BINARY, .flags = D|E },
{"timeout", "set timeout of socket I/O operations", OFFSET(rw_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, D|E },
{"mime_type", "set MIME type", OFFSET(mime_type), AV_OPT_TYPE_STRING, {0}, 0, 0, 0 },
{"cookies", "set cookies to be sent in applicable future requests, use newline delimited Set-Cookie HTTP field value syntax", OFFSET(cookies), AV_OPT_TYPE_STRING, {0}, 0, 0, 0 },
{NULL}
};
#define HTTP_CLASS(flavor)\
static const AVClass flavor ## _context_class = {\
    .class_name     = #flavor,\
    .item_name      = av_default_item_name,\
    .option         = options,\
    .version        = LIBAVUTIL_VERSION_INT,\
}

HTTP_CLASS(http);
HTTP_CLASS(https);

static int http_connect(URLContext *h, const char *path, const char *local_path,
                        const char *hoststr, const char *auth,
                        const char *proxyauth, int *new_location);

void ff_http_init_auth_state(URLContext *dest, const URLContext *src)
{
    memcpy(&((HTTPContext*)dest->priv_data)->auth_state,
           &((HTTPContext*)src->priv_data)->auth_state, sizeof(HTTPAuthState));
    memcpy(&((HTTPContext*)dest->priv_data)->proxy_auth_state,
           &((HTTPContext*)src->priv_data)->proxy_auth_state,
           sizeof(HTTPAuthState));
}

/* return non zero if error */
static int http_open_cnx(URLContext *h)
{
    const char *path, *proxy_path, *lower_proto = "tcp", *local_path;
    char hostname[1024], hoststr[1024], proto[10];
    char auth[1024], proxyauth[1024] = "";
    char path1[MAX_URL_SIZE];
    char buf[1024], urlbuf[MAX_URL_SIZE];
    int port, use_proxy, err, location_changed = 0, redirects = 0, attempts = 0;
    HTTPAuthType cur_auth_type, cur_proxy_auth_type;
    HTTPContext *s = h->priv_data;

    proxy_path = getenv("http_proxy");
    use_proxy = (proxy_path != NULL) && !getenv("no_proxy") &&
        av_strstart(proxy_path, "http://", NULL);

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    av_url_split(proto, sizeof(proto), auth, sizeof(auth),
                 hostname, sizeof(hostname), &port,
                 path1, sizeof(path1), s->location);
    ff_url_join(hoststr, sizeof(hoststr), NULL, NULL, hostname, port, NULL);

    if (!strcmp(proto, "https")) {
        lower_proto = "tls";
        use_proxy = 0;
        if (port < 0)
            port = 443;
    }
    if (port < 0)
        port = 80;

    if (path1[0] == '\0')
        path = "/";
    else
        path = path1;
    local_path = path;
    if (use_proxy) {
        /* Reassemble the request URL without auth string - we don't
         * want to leak the auth to the proxy. */
        ff_url_join(urlbuf, sizeof(urlbuf), proto, NULL, hostname, port, "%s",
                    path1);
        path = urlbuf;
        av_url_split(NULL, 0, proxyauth, sizeof(proxyauth),
                     hostname, sizeof(hostname), &port, NULL, 0, proxy_path);
    }

    ff_url_join(buf, sizeof(buf), lower_proto, NULL, hostname, port, NULL);

    if (!s->hd) {
        AVDictionary *opts = NULL;
        char opts_format[20];
        if (s->rw_timeout != -1) {
            snprintf(opts_format, sizeof(opts_format), "%d", s->rw_timeout);
            av_dict_set(&opts, "timeout", opts_format, 0);
        } /* if option is not given, don't pass it and let tcp use its own default */
        err = ffurl_open(&s->hd, buf, AVIO_FLAG_READ_WRITE,
                         &h->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (err < 0)
            goto fail;
    }

    cur_auth_type = s->auth_state.auth_type;
    cur_proxy_auth_type = s->auth_state.auth_type;
    if (http_connect(h, path, local_path, hoststr, auth, proxyauth, &location_changed) < 0)
        goto fail;
    attempts++;
    if (s->http_code == 401) {
        if ((cur_auth_type == HTTP_AUTH_NONE || s->auth_state.stale) &&
            s->auth_state.auth_type != HTTP_AUTH_NONE && attempts < 4) {
            ffurl_closep(&s->hd);
            goto redo;
        } else
            goto fail;
    }
    if (s->http_code == 407) {
        if ((cur_proxy_auth_type == HTTP_AUTH_NONE || s->proxy_auth_state.stale) &&
            s->proxy_auth_state.auth_type != HTTP_AUTH_NONE && attempts < 4) {
            ffurl_closep(&s->hd);
            goto redo;
        } else
            goto fail;
    }
    if ((s->http_code == 301 || s->http_code == 302 || s->http_code == 303 || s->http_code == 307)
        && location_changed == 1) {
        /* url moved, get next */
        ffurl_closep(&s->hd);
        if (redirects++ >= MAX_REDIRECTS)
            return AVERROR(EIO);
        /* Restart the authentication process with the new target, which
         * might use a different auth mechanism. */
        memset(&s->auth_state, 0, sizeof(s->auth_state));
        attempts = 0;
        location_changed = 0;
        goto redo;
    }
    return 0;
 fail:
    if (s->hd)
        ffurl_closep(&s->hd);
    return AVERROR(EIO);
}

int ff_http_do_new_request(URLContext *h, const char *uri)
{
    HTTPContext *s = h->priv_data;

    s->off = 0;
    av_strlcpy(s->location, uri, sizeof(s->location));

    return http_open_cnx(h);
}

static int http_open(URLContext *h, const char *uri, int flags)
{
    HTTPContext *s = h->priv_data;

    if( s->seekable == 1 )
        h->is_streamed = 0;
    else
        h->is_streamed = 1;

    s->filesize = -1;
    av_strlcpy(s->location, uri, sizeof(s->location));

    if (s->headers) {
        int len = strlen(s->headers);
        if (len < 2 || strcmp("\r\n", s->headers + len - 2))
            av_log(h, AV_LOG_WARNING, "No trailing CRLF found in HTTP header.\n");
    }

    return http_open_cnx(h);
}
static int http_getc(HTTPContext *s)
{
    int len;
    if (s->buf_ptr >= s->buf_end) {
        len = ffurl_read(s->hd, s->buffer, BUFFER_SIZE);
        if (len < 0) {
            return len;
        } else if (len == 0) {
            return -1;
        } else {
            s->buf_ptr = s->buffer;
            s->buf_end = s->buffer + len;
        }
    }
    return *s->buf_ptr++;
}

static int http_get_line(HTTPContext *s, char *line, int line_size)
{
    int ch;
    char *q;

    q = line;
    for(;;) {
        ch = http_getc(s);
        if (ch < 0)
            return ch;
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';

            return 0;
        } else {
            if ((q - line) < line_size - 1)
                *q++ = ch;
        }
    }
}

static int process_line(URLContext *h, char *line, int line_count,
                        int *new_location)
{
    HTTPContext *s = h->priv_data;
    char *tag, *p, *end;

    /* end of header */
    if (line[0] == '\0') {
        s->end_header = 1;
        return 0;
    }

    p = line;
    if (line_count == 0) {
        while (!isspace(*p) && *p != '\0')
            p++;
        while (isspace(*p))
            p++;
        s->http_code = strtol(p, &end, 10);

        av_dlog(NULL, "http_code=%d\n", s->http_code);

        /* error codes are 4xx and 5xx, but regard 401 as a success, so we
         * don't abort until all headers have been parsed. */
        if (s->http_code >= 400 && s->http_code < 600 && (s->http_code != 401
            || s->auth_state.auth_type != HTTP_AUTH_NONE) &&
            (s->http_code != 407 || s->proxy_auth_state.auth_type != HTTP_AUTH_NONE)) {
            end += strspn(end, SPACE_CHARS);
            av_log(h, AV_LOG_WARNING, "HTTP error %d %s\n",
                   s->http_code, end);
            return -1;
        }
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
        if (!av_strcasecmp(tag, "Location")) {
            av_strlcpy(s->location, p, sizeof(s->location));
            *new_location = 1;
        } else if (!av_strcasecmp (tag, "Content-Length") && s->filesize == -1) {
            s->filesize = strtoll(p, NULL, 10);
        } else if (!av_strcasecmp (tag, "Content-Range")) {
            /* "bytes $from-$to/$document_size" */
            const char *slash;
            if (!strncmp (p, "bytes ", 6)) {
                p += 6;
                s->off = strtoll(p, NULL, 10);
                if ((slash = strchr(p, '/')) && strlen(slash) > 0)
                    s->filesize = strtoll(slash+1, NULL, 10);
            }
            if (s->seekable == -1 && (!s->is_akamai || s->filesize != 2147483647))
                h->is_streamed = 0; /* we _can_ in fact seek */
        } else if (!av_strcasecmp(tag, "Accept-Ranges") && !strncmp(p, "bytes", 5) && s->seekable == -1) {
            h->is_streamed = 0;
        } else if (!av_strcasecmp (tag, "Transfer-Encoding") && !av_strncasecmp(p, "chunked", 7)) {
            s->filesize = -1;
            s->chunksize = 0;
        } else if (!av_strcasecmp (tag, "WWW-Authenticate")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!av_strcasecmp (tag, "Authentication-Info")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!av_strcasecmp (tag, "Proxy-Authenticate")) {
            ff_http_auth_handle_header(&s->proxy_auth_state, tag, p);
        } else if (!av_strcasecmp (tag, "Connection")) {
            if (!strcmp(p, "close"))
                s->willclose = 1;
        } else if (!av_strcasecmp (tag, "Server") && !av_strcasecmp (p, "AkamaiGHost")) {
            s->is_akamai = 1;
        } else if (!av_strcasecmp (tag, "Content-Type")) {
            av_free(s->mime_type); s->mime_type = av_strdup(p);
        } else if (!av_strcasecmp (tag, "Set-Cookie")) {
            if (!s->cookies) {
                if (!(s->cookies = av_strdup(p)))
                    return AVERROR(ENOMEM);
            } else {
                char *tmp = s->cookies;
                size_t str_size = strlen(tmp) + strlen(p) + 2;
                if (!(s->cookies = av_malloc(str_size))) {
                    s->cookies = tmp;
                    return AVERROR(ENOMEM);
                }
                snprintf(s->cookies, str_size, "%s\n%s", tmp, p);
                av_free(tmp);
            }
        }
    }
    return 1;
}

/**
 * Create a string containing cookie values for use as a HTTP cookie header
 * field value for a particular path and domain from the cookie values stored in
 * the HTTP protocol context. The cookie string is stored in *cookies.
 *
 * @return a negative value if an error condition occurred, 0 otherwise
 */
static int get_cookies(HTTPContext *s, char **cookies, const char *path,
                       const char *domain)
{
    // cookie strings will look like Set-Cookie header field values.  Multiple
    // Set-Cookie fields will result in multiple values delimited by a newline
    int ret = 0;
    char *next, *cookie, *set_cookies = av_strdup(s->cookies), *cset_cookies = set_cookies;

    if (!set_cookies) return AVERROR(EINVAL);

    *cookies = NULL;
    while ((cookie = av_strtok(set_cookies, "\n", &next))) {
        int domain_offset = 0;
        char *param, *next_param, *cdomain = NULL, *cpath = NULL, *cvalue = NULL;
        set_cookies = NULL;

        while ((param = av_strtok(cookie, "; ", &next_param))) {
            cookie = NULL;
            if        (!av_strncasecmp("path=",   param, 5)) {
                av_free(cpath);
                cpath = av_strdup(&param[5]);
            } else if (!av_strncasecmp("domain=", param, 7)) {
                av_free(cdomain);
                cdomain = av_strdup(&param[7]);
            } else if (!av_strncasecmp("secure",  param, 6) ||
                       !av_strncasecmp("comment", param, 7) ||
                       !av_strncasecmp("max-age", param, 7) ||
                       !av_strncasecmp("version", param, 7)) {
                // ignore Comment, Max-Age, Secure and Version
            } else {
                av_free(cvalue);
                cvalue = av_strdup(param);
            }
        }

        // ensure all of the necessary values are valid
        if (!cdomain || !cpath || !cvalue) {
            av_log(s, AV_LOG_WARNING,
                   "Invalid cookie found, no value, path or domain specified\n");
            goto done_cookie;
        }

        // check if the request path matches the cookie path
        if (av_strncasecmp(path, cpath, strlen(cpath)))
            goto done_cookie;

        // the domain should be at least the size of our cookie domain
        domain_offset = strlen(domain) - strlen(cdomain);
        if (domain_offset < 0)
            goto done_cookie;

        // match the cookie domain
        if (av_strcasecmp(&domain[domain_offset], cdomain))
            goto done_cookie;

        // cookie parameters match, so copy the value
        if (!*cookies) {
            if (!(*cookies = av_strdup(cvalue))) {
                ret = AVERROR(ENOMEM);
                goto done_cookie;
            }
        } else {
            char *tmp = *cookies;
            size_t str_size = strlen(cvalue) + strlen(*cookies) + 3;
            if (!(*cookies = av_malloc(str_size))) {
                ret = AVERROR(ENOMEM);
                goto done_cookie;
            }
            snprintf(*cookies, str_size, "%s; %s", tmp, cvalue);
            av_free(tmp);
        }

        done_cookie:
        av_free(cdomain);
        av_free(cpath);
        av_free(cvalue);
        if (ret < 0) {
            if (*cookies) av_freep(cookies);
            av_free(cset_cookies);
            return ret;
        }
    }

    av_free(cset_cookies);

    return 0;
}

static inline int has_header(const char *str, const char *header)
{
    /* header + 2 to skip over CRLF prefix. (make sure you have one!) */
    if (!str)
        return 0;
    return av_stristart(str, header + 2, NULL) || av_stristr(str, header);
}

static int http_read_header(URLContext *h, int *new_location)
{
    HTTPContext *s = h->priv_data;
    char line[MAX_URL_SIZE];
    int err = 0;

    s->chunksize = -1;

    for (;;) {
        if ((err = http_get_line(s, line, sizeof(line))) < 0)
            return err;

        av_dlog(NULL, "header='%s'\n", line);

        err = process_line(h, line, s->line_count, new_location);
        if (err < 0)
            return err;
        if (err == 0)
            break;
        s->line_count++;
    }

    return err;
}

static int http_connect(URLContext *h, const char *path, const char *local_path,
                        const char *hoststr, const char *auth,
                        const char *proxyauth, int *new_location)
{
    HTTPContext *s = h->priv_data;
    int post, err;
    char headers[4096] = "";
    char *authstr = NULL, *proxyauthstr = NULL;
    int64_t off = s->off;
    int len = 0;
    const char *method;


    /* send http header */
    post = h->flags & AVIO_FLAG_WRITE;

    if (s->post_data) {
        /* force POST method and disable chunked encoding when
         * custom HTTP post data is set */
        post = 1;
        s->chunked_post = 0;
    }

    method = post ? "POST" : "GET";
    authstr = ff_http_auth_create_response(&s->auth_state, auth, local_path,
                                           method);
    proxyauthstr = ff_http_auth_create_response(&s->proxy_auth_state, proxyauth,
                                                local_path, method);

    /* set default headers if needed */
    if (!has_header(s->headers, "\r\nUser-Agent: "))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "User-Agent: %s\r\n",
                           s->user_agent ? s->user_agent : LIBAVFORMAT_IDENT);
    if (!has_header(s->headers, "\r\nAccept: "))
        len += av_strlcpy(headers + len, "Accept: */*\r\n",
                          sizeof(headers) - len);
    // Note: we send this on purpose even when s->off is 0 when we're probing,
    // since it allows us to detect more reliably if a (non-conforming)
    // server supports seeking by analysing the reply headers.
    if (!has_header(s->headers, "\r\nRange: ") && !post && (s->off > 0 || s->seekable == -1))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Range: bytes=%"PRId64"-\r\n", s->off);

    if (!has_header(s->headers, "\r\nConnection: ")) {
        if (s->multiple_requests) {
            len += av_strlcpy(headers + len, "Connection: keep-alive\r\n",
                              sizeof(headers) - len);
        } else {
            len += av_strlcpy(headers + len, "Connection: close\r\n",
                              sizeof(headers) - len);
        }
    }

    if (!has_header(s->headers, "\r\nHost: "))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Host: %s\r\n", hoststr);
    if (!has_header(s->headers, "\r\nContent-Length: ") && s->post_data)
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Content-Length: %d\r\n", s->post_datalen);
    if (!has_header(s->headers, "\r\nContent-Type: ") && s->content_type)
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Content-Type: %s\r\n", s->content_type);
    if (!has_header(s->headers, "\r\nCookie: ") && s->cookies) {
        char *cookies = NULL;
        if (!get_cookies(s, &cookies, path, hoststr)) {
            len += av_strlcatf(headers + len, sizeof(headers) - len,
                               "Cookie: %s\r\n", cookies);
            av_free(cookies);
        }
    }

    /* now add in custom headers */
    if (s->headers)
        av_strlcpy(headers + len, s->headers, sizeof(headers) - len);

    snprintf(s->buffer, sizeof(s->buffer),
             "%s %s HTTP/1.1\r\n"
             "%s"
             "%s"
             "%s"
             "%s%s"
             "\r\n",
             method,
             path,
             post && s->chunked_post ? "Transfer-Encoding: chunked\r\n" : "",
             headers,
             authstr ? authstr : "",
             proxyauthstr ? "Proxy-" : "", proxyauthstr ? proxyauthstr : "");

    av_freep(&authstr);
    av_freep(&proxyauthstr);
    if ((err = ffurl_write(s->hd, s->buffer, strlen(s->buffer))) < 0)
        return err;

    if (s->post_data)
        if ((err = ffurl_write(s->hd, s->post_data, s->post_datalen)) < 0)
            return err;

    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->off = 0;
    s->filesize = -1;
    s->willclose = 0;
    s->end_chunked_post = 0;
    s->end_header = 0;
    if (post && !s->post_data) {
        /* Pretend that it did work. We didn't read any header yet, since
         * we've still to send the POST data, but the code calling this
         * function will check http_code after we return. */
        s->http_code = 200;
        return 0;
    }

    /* wait for header */
    err = http_read_header(h, new_location);
    if (err < 0)
        return err;

    return (off == s->off) ? 0 : -1;
}


static int http_buf_read(URLContext *h, uint8_t *buf, int size)
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
        if (!s->willclose && s->filesize >= 0 && s->off >= s->filesize)
            return AVERROR_EOF;
        len = ffurl_read(s->hd, buf, size);
    }
    if (len > 0) {
        s->off += len;
        if (s->chunksize > 0)
            s->chunksize -= len;
    }
    return len;
}

static int http_read(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int err, new_location;

    if (!s->hd)
        return AVERROR_EOF;

    if (s->end_chunked_post && !s->end_header) {
        err = http_read_header(h, &new_location);
        if (err < 0)
            return err;
    }

    if (s->chunksize >= 0) {
        if (!s->chunksize) {
            char line[32];

            for(;;) {
                do {
                    if ((err = http_get_line(s, line, sizeof(line))) < 0)
                        return err;
                } while (!*line);    /* skip CR LF from last chunk */

                s->chunksize = strtoll(line, NULL, 16);

                av_dlog(NULL, "Chunked encoding data size: %"PRId64"'\n", s->chunksize);

                if (!s->chunksize)
                    return 0;
                break;
            }
        }
        size = FFMIN(size, s->chunksize);
    }
    return http_buf_read(h, buf, size);
}

/* used only when posting data */
static int http_write(URLContext *h, const uint8_t *buf, int size)
{
    char temp[11] = "";  /* 32-bit hex + CRLF + nul */
    int ret;
    char crlf[] = "\r\n";
    HTTPContext *s = h->priv_data;

    if (!s->chunked_post) {
        /* non-chunked data is sent without any special encoding */
        return ffurl_write(s->hd, buf, size);
    }

    /* silently ignore zero-size data since chunk encoding that would
     * signal EOF */
    if (size > 0) {
        /* upload data using chunked encoding */
        snprintf(temp, sizeof(temp), "%x\r\n", size);

        if ((ret = ffurl_write(s->hd, temp, strlen(temp))) < 0 ||
            (ret = ffurl_write(s->hd, buf, size)) < 0 ||
            (ret = ffurl_write(s->hd, crlf, sizeof(crlf) - 1)) < 0)
            return ret;
    }
    return size;
}

static int http_shutdown(URLContext *h, int flags)
{
    int ret = 0;
    char footer[] = "0\r\n\r\n";
    HTTPContext *s = h->priv_data;

    /* signal end of chunked encoding if used */
    if ((flags & AVIO_FLAG_WRITE) && s->chunked_post) {
        ret = ffurl_write(s->hd, footer, sizeof(footer) - 1);
        ret = ret > 0 ? 0 : ret;
        s->end_chunked_post = 1;
    }

    return ret;
}

static int http_close(URLContext *h)
{
    int ret = 0;
    HTTPContext *s = h->priv_data;

    if (!s->end_chunked_post) {
        /* Close the write direction by sending the end of chunked encoding. */
        ret = http_shutdown(h, h->flags);
    }

    if (s->hd)
        ffurl_closep(&s->hd);
    return ret;
}

static int64_t http_seek(URLContext *h, int64_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    URLContext *old_hd = s->hd;
    int64_t old_off = s->off;
    uint8_t old_buf[BUFFER_SIZE];
    int old_buf_size;

    if (whence == AVSEEK_SIZE)
        return s->filesize;
    else if ((s->filesize == -1 && whence == SEEK_END) || h->is_streamed)
        return -1;

    /* we save the old context in case the seek fails */
    old_buf_size = s->buf_end - s->buf_ptr;
    memcpy(old_buf, s->buf_ptr, old_buf_size);
    s->hd = NULL;
    if (whence == SEEK_CUR)
        off += s->off;
    else if (whence == SEEK_END)
        off += s->filesize;
    s->off = off;

    /* if it fails, continue on old connection */
    if (http_open_cnx(h) < 0) {
        memcpy(s->buffer, old_buf, old_buf_size);
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + old_buf_size;
        s->hd = old_hd;
        s->off = old_off;
        return -1;
    }
    ffurl_close(old_hd);
    return off;
}

static int
http_get_file_handle(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    return ffurl_get_file_handle(s->hd);
}

#if CONFIG_HTTP_PROTOCOL
URLProtocol ff_http_protocol = {
    .name                = "http",
    .url_open            = http_open,
    .url_read            = http_read,
    .url_write           = http_write,
    .url_seek            = http_seek,
    .url_close           = http_close,
    .url_get_file_handle = http_get_file_handle,
    .url_shutdown        = http_shutdown,
    .priv_data_size      = sizeof(HTTPContext),
    .priv_data_class     = &http_context_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
#endif
#if CONFIG_HTTPS_PROTOCOL
URLProtocol ff_https_protocol = {
    .name                = "https",
    .url_open            = http_open,
    .url_read            = http_read,
    .url_write           = http_write,
    .url_seek            = http_seek,
    .url_close           = http_close,
    .url_get_file_handle = http_get_file_handle,
    .url_shutdown        = http_shutdown,
    .priv_data_size      = sizeof(HTTPContext),
    .priv_data_class     = &https_context_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
#endif

#if CONFIG_HTTPPROXY_PROTOCOL
static int http_proxy_close(URLContext *h)
{
    HTTPContext *s = h->priv_data;
    if (s->hd)
        ffurl_closep(&s->hd);
    return 0;
}

static int http_proxy_open(URLContext *h, const char *uri, int flags)
{
    HTTPContext *s = h->priv_data;
    char hostname[1024], hoststr[1024];
    char auth[1024], pathbuf[1024], *path;
    char lower_url[100];
    int port, ret = 0, attempts = 0;
    HTTPAuthType cur_auth_type;
    char *authstr;
    int new_loc;
    AVDictionary *opts = NULL;
    char opts_format[20];

    if( s->seekable == 1 )
        h->is_streamed = 0;
    else
        h->is_streamed = 1;

    av_url_split(NULL, 0, auth, sizeof(auth), hostname, sizeof(hostname), &port,
                 pathbuf, sizeof(pathbuf), uri);
    ff_url_join(hoststr, sizeof(hoststr), NULL, NULL, hostname, port, NULL);
    path = pathbuf;
    if (*path == '/')
        path++;

    ff_url_join(lower_url, sizeof(lower_url), "tcp", NULL, hostname, port,
                NULL);
redo:
    if (s->rw_timeout != -1) {
        snprintf(opts_format, sizeof(opts_format), "%d", s->rw_timeout);
        av_dict_set(&opts, "timeout", opts_format, 0);
    } /* if option is not given, don't pass it and let tcp use its own default */
    ret = ffurl_open(&s->hd, lower_url, AVIO_FLAG_READ_WRITE,
                     &h->interrupt_callback, &opts);
    av_dict_free(&opts);
    if (ret < 0)
        return ret;

    authstr = ff_http_auth_create_response(&s->proxy_auth_state, auth,
                                           path, "CONNECT");
    snprintf(s->buffer, sizeof(s->buffer),
             "CONNECT %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "%s%s"
             "\r\n",
             path,
             hoststr,
             authstr ? "Proxy-" : "", authstr ? authstr : "");
    av_freep(&authstr);

    if ((ret = ffurl_write(s->hd, s->buffer, strlen(s->buffer))) < 0)
        goto fail;

    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->filesize = -1;
    cur_auth_type = s->proxy_auth_state.auth_type;

    /* Note: This uses buffering, potentially reading more than the
     * HTTP header. If tunneling a protocol where the server starts
     * the conversation, we might buffer part of that here, too.
     * Reading that requires using the proper ffurl_read() function
     * on this URLContext, not using the fd directly (as the tls
     * protocol does). This shouldn't be an issue for tls though,
     * since the client starts the conversation there, so there
     * is no extra data that we might buffer up here.
     */
    ret = http_read_header(h, &new_loc);
    if (ret < 0)
        goto fail;

    attempts++;
    if (s->http_code == 407 &&
        (cur_auth_type == HTTP_AUTH_NONE || s->proxy_auth_state.stale) &&
        s->proxy_auth_state.auth_type != HTTP_AUTH_NONE && attempts < 2) {
        ffurl_closep(&s->hd);
        goto redo;
    }

    if (s->http_code < 400)
        return 0;
    ret = AVERROR(EIO);

fail:
    http_proxy_close(h);
    return ret;
}

static int http_proxy_write(URLContext *h, const uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    return ffurl_write(s->hd, buf, size);
}

URLProtocol ff_httpproxy_protocol = {
    .name                = "httpproxy",
    .url_open            = http_proxy_open,
    .url_read            = http_buf_read,
    .url_write           = http_proxy_write,
    .url_close           = http_proxy_close,
    .url_get_file_handle = http_get_file_handle,
    .priv_data_size      = sizeof(HTTPContext),
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
#endif
