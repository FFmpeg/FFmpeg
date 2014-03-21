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

#if CONFIG_ZLIB
#include <zlib.h>
#endif

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
    /* Used if "Transfer-Encoding: chunked" otherwise -1. */
    int64_t chunksize;
    int64_t off, end_off, filesize;
    char *location;
    HTTPAuthState auth_state;
    HTTPAuthState proxy_auth_state;
    char *headers;
    char *mime_type;
    char *user_agent;
    char *content_type;
    /* Set if the server correctly handles Connection: close and will close
     * the connection after feeding us the content. */
    int willclose;
    int seekable;           /**< Control seekability, 0 = disable, 1 = enable, -1 = probe. */
    int chunked_post;
    /* A flag which indicates if the end of chunked encoding has been sent. */
    int end_chunked_post;
    /* A flag which indicates we have finished to read POST reply. */
    int end_header;
    /* A flag which indicates if we use persistent connections. */
    int multiple_requests;
    uint8_t *post_data;
    int post_datalen;
    int is_akamai;
    int is_mediagateway;
    char *cookies;          ///< holds newline (\n) delimited Set-Cookie header field values (without the "Set-Cookie: " field name)
    int icy;
    /* how much data was read since the last ICY metadata packet */
    int icy_data_read;
    /* after how many bytes of read data a new metadata packet will be found */
    int icy_metaint;
    char *icy_metadata_headers;
    char *icy_metadata_packet;
#if CONFIG_ZLIB
    int compressed;
    z_stream inflate_stream;
    uint8_t *inflate_buffer;
#endif
    AVDictionary *chained_options;
    int send_expect_100;
} HTTPContext;

#define OFFSET(x) offsetof(HTTPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define DEFAULT_USER_AGENT "Lavf/" AV_STRINGIFY(LIBAVFORMAT_VERSION)
static const AVOption options[] = {
{"seekable", "control seekability of connection", OFFSET(seekable), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 1, D },
{"chunked_post", "use chunked transfer-encoding for posts", OFFSET(chunked_post), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, E },
{"headers", "set custom HTTP headers, can override built in default headers", OFFSET(headers), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
{"content_type", "set a specific content type for the POST messages", OFFSET(content_type), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
{"user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = DEFAULT_USER_AGENT}, 0, 0, D },
{"user-agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = DEFAULT_USER_AGENT}, 0, 0, D },
{"multiple_requests", "use persistent connections", OFFSET(multiple_requests), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, D|E },
{"post_data", "set custom HTTP post data", OFFSET(post_data), AV_OPT_TYPE_BINARY, .flags = D|E },
{"mime_type", "export the MIME type", OFFSET(mime_type), AV_OPT_TYPE_STRING, {0}, 0, 0, AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY  },
{"cookies", "set cookies to be sent in applicable future requests, use newline delimited Set-Cookie HTTP field value syntax", OFFSET(cookies), AV_OPT_TYPE_STRING, {0}, 0, 0, D },
{"icy", "request ICY metadata", OFFSET(icy), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, D },
{"icy_metadata_headers", "return ICY metadata headers", OFFSET(icy_metadata_headers), AV_OPT_TYPE_STRING, {0}, 0, 0, AV_OPT_FLAG_EXPORT },
{"icy_metadata_packet", "return current ICY metadata packet", OFFSET(icy_metadata_packet), AV_OPT_TYPE_STRING, {0}, 0, 0, AV_OPT_FLAG_EXPORT },
{"auth_type", "HTTP authentication type", OFFSET(auth_state.auth_type), AV_OPT_TYPE_INT, {.i64 = HTTP_AUTH_NONE}, HTTP_AUTH_NONE, HTTP_AUTH_BASIC, D|E, "auth_type" },
{"none", "No auth method set, autodetect", 0, AV_OPT_TYPE_CONST, {.i64 = HTTP_AUTH_NONE}, 0, 0, D|E, "auth_type" },
{"basic", "HTTP basic authentication", 0, AV_OPT_TYPE_CONST, {.i64 = HTTP_AUTH_BASIC}, 0, 0, D|E, "auth_type" },
{"send_expect_100", "Force sending an Expect: 100-continue header for POST", OFFSET(send_expect_100), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, E },
{"location", "The actual location of the data received", OFFSET(location), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
{"offset", "initial byte offset", OFFSET(off), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, D },
{"end_offset", "try to limit the request to bytes preceding this offset", OFFSET(end_off), AV_OPT_TYPE_INT64, {.i64 = 0}, 0, INT64_MAX, D },
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
static int http_open_cnx(URLContext *h, AVDictionary **options)
{
    const char *path, *proxy_path, *lower_proto = "tcp", *local_path;
    char hostname[1024], hoststr[1024], proto[10];
    char auth[1024], proxyauth[1024] = "";
    char path1[MAX_URL_SIZE];
    char buf[1024], urlbuf[MAX_URL_SIZE];
    int port, use_proxy, err, location_changed = 0, redirects = 0, attempts = 0;
    HTTPAuthType cur_auth_type, cur_proxy_auth_type;
    HTTPContext *s = h->priv_data;

    /* fill the dest addr */
 redo:
    /* needed in any case to build the host string */
    av_url_split(proto, sizeof(proto), auth, sizeof(auth),
                 hostname, sizeof(hostname), &port,
                 path1, sizeof(path1), s->location);
    ff_url_join(hoststr, sizeof(hoststr), NULL, NULL, hostname, port, NULL);

    proxy_path = getenv("http_proxy");
    use_proxy = !ff_http_match_no_proxy(getenv("no_proxy"), hostname) &&
                proxy_path != NULL && av_strstart(proxy_path, "http://", NULL);

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
        err = ffurl_open(&s->hd, buf, AVIO_FLAG_READ_WRITE,
                         &h->interrupt_callback, options);
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
    AVDictionary *options = NULL;
    int ret;

    s->off = 0;
    s->icy_data_read = 0;
    av_free(s->location);
    s->location = av_strdup(uri);
    if (!s->location)
        return AVERROR(ENOMEM);

    av_dict_copy(&options, s->chained_options, 0);
    ret = http_open_cnx(h, &options);
    av_dict_free(&options);
    return ret;
}

static int http_open(URLContext *h, const char *uri, int flags,
                     AVDictionary **options)
{
    HTTPContext *s = h->priv_data;
    int ret;

    if( s->seekable == 1 )
        h->is_streamed = 0;
    else
        h->is_streamed = 1;

    s->filesize = -1;
    s->location = av_strdup(uri);
    if (!s->location)
        return AVERROR(ENOMEM);
    if (options)
        av_dict_copy(&s->chained_options, *options, 0);

    if (s->headers) {
        int len = strlen(s->headers);
        if (len < 2 || strcmp("\r\n", s->headers + len - 2))
            av_log(h, AV_LOG_WARNING, "No trailing CRLF found in HTTP header.\n");
    }

    ret = http_open_cnx(h, options);
    if (ret < 0)
        av_dict_free(&s->chained_options);
    return ret;
}
static int http_getc(HTTPContext *s)
{
    int len;
    if (s->buf_ptr >= s->buf_end) {
        len = ffurl_read(s->hd, s->buffer, BUFFER_SIZE);
        if (len < 0) {
            return len;
        } else if (len == 0) {
            return AVERROR_EOF;
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

static int check_http_code(URLContext *h, int http_code, const char *end)
{
    HTTPContext *s = h->priv_data;
    /* error codes are 4xx and 5xx, but regard 401 as a success, so we
     * don't abort until all headers have been parsed. */
    if (http_code >= 400 && http_code < 600 &&
        (http_code != 401 || s->auth_state.auth_type != HTTP_AUTH_NONE) &&
        (http_code != 407 || s->proxy_auth_state.auth_type != HTTP_AUTH_NONE)) {
        end += strspn(end, SPACE_CHARS);
        av_log(h, AV_LOG_WARNING, "HTTP error %d %s\n", http_code, end);
        return AVERROR(EIO);
    }
    return 0;
}

static int parse_location(HTTPContext *s, const char *p)
{
    char redirected_location[MAX_URL_SIZE], *new_loc;
    ff_make_absolute_url(redirected_location, sizeof(redirected_location),
                         s->location, p);
    new_loc = av_strdup(redirected_location);
    if (!new_loc)
        return AVERROR(ENOMEM);
    av_free(s->location);
    s->location = new_loc;
    return 0;
}

/* "bytes $from-$to/$document_size" */
static void parse_content_range(URLContext *h, const char *p)
{
    HTTPContext *s = h->priv_data;
    const char *slash;

    if (!strncmp(p, "bytes ", 6)) {
        p += 6;
        s->off = strtoll(p, NULL, 10);
        if ((slash = strchr(p, '/')) && strlen(slash) > 0)
            s->filesize = strtoll(slash+1, NULL, 10);
    }
    if (s->seekable == -1 && (!s->is_akamai || s->filesize != 2147483647))
        h->is_streamed = 0; /* we _can_ in fact seek */
}

static int parse_content_encoding(URLContext *h, const char *p)
{
    HTTPContext *s = h->priv_data;

    if (!av_strncasecmp(p, "gzip", 4) ||
        !av_strncasecmp(p, "deflate", 7)) {
#if CONFIG_ZLIB
        s->compressed = 1;
        inflateEnd(&s->inflate_stream);
        if (inflateInit2(&s->inflate_stream, 32 + 15) != Z_OK) {
            av_log(h, AV_LOG_WARNING, "Error during zlib initialisation: %s\n",
                   s->inflate_stream.msg);
            return AVERROR(ENOSYS);
        }
        if (zlibCompileFlags() & (1 << 17)) {
            av_log(h, AV_LOG_WARNING,
                   "Your zlib was compiled without gzip support.\n");
            return AVERROR(ENOSYS);
        }
#else
        av_log(h, AV_LOG_WARNING,
               "Compressed (%s) content, need zlib with gzip support\n", p);
        return AVERROR(ENOSYS);
#endif
    } else if (!av_strncasecmp(p, "identity", 8)) {
        // The normal, no-encoding case (although servers shouldn't include
        // the header at all if this is the case).
    } else {
        av_log(h, AV_LOG_WARNING, "Unknown content coding: %s\n", p);
    }
    return 0;
}

// Concat all Icy- header lines
static int parse_icy(HTTPContext *s, const char *tag, const char *p)
{
    int len = 4 + strlen(p) + strlen(tag);
    int is_first = !s->icy_metadata_headers;
    int ret;

    if (s->icy_metadata_headers)
        len += strlen(s->icy_metadata_headers);

    if ((ret = av_reallocp(&s->icy_metadata_headers, len)) < 0)
        return ret;

    if (is_first)
        *s->icy_metadata_headers = '\0';

    av_strlcatf(s->icy_metadata_headers, len, "%s: %s\n", tag, p);

    return 0;
}

static int process_line(URLContext *h, char *line, int line_count,
                        int *new_location)
{
    HTTPContext *s = h->priv_data;
    char *tag, *p, *end;
    int ret;

    /* end of header */
    if (line[0] == '\0') {
        s->end_header = 1;
        return 0;
    }

    p = line;
    if (line_count == 0) {
        while (!av_isspace(*p) && *p != '\0')
            p++;
        while (av_isspace(*p))
            p++;
        s->http_code = strtol(p, &end, 10);

        av_log(h, AV_LOG_DEBUG, "http_code=%d\n", s->http_code);

        if ((ret = check_http_code(h, s->http_code, end)) < 0)
            return ret;
    } else {
        while (*p != '\0' && *p != ':')
            p++;
        if (*p != ':')
            return 1;

        *p = '\0';
        tag = line;
        p++;
        while (av_isspace(*p))
            p++;
        if (!av_strcasecmp(tag, "Location")) {
            if ((ret = parse_location(s, p)) < 0)
                return ret;
            *new_location = 1;
        } else if (!av_strcasecmp(tag, "Content-Length") && s->filesize == -1) {
            s->filesize = strtoll(p, NULL, 10);
        } else if (!av_strcasecmp(tag, "Content-Range")) {
            parse_content_range(h, p);
        } else if (!av_strcasecmp(tag, "Accept-Ranges") &&
                   !strncmp(p, "bytes", 5) &&
                   s->seekable == -1) {
            h->is_streamed = 0;
        } else if (!av_strcasecmp(tag, "Transfer-Encoding") &&
                   !av_strncasecmp(p, "chunked", 7)) {
            s->filesize = -1;
            s->chunksize = 0;
        } else if (!av_strcasecmp(tag, "WWW-Authenticate")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!av_strcasecmp(tag, "Authentication-Info")) {
            ff_http_auth_handle_header(&s->auth_state, tag, p);
        } else if (!av_strcasecmp(tag, "Proxy-Authenticate")) {
            ff_http_auth_handle_header(&s->proxy_auth_state, tag, p);
        } else if (!av_strcasecmp(tag, "Connection")) {
            if (!strcmp(p, "close"))
                s->willclose = 1;
        } else if (!av_strcasecmp (tag, "Server")) {
            if (!av_strcasecmp (p, "AkamaiGHost")) {
                s->is_akamai = 1;
            } else if (!av_strncasecmp (p, "MediaGateway", 12)) {
                s->is_mediagateway = 1;
            }
        } else if (!av_strcasecmp (tag, "Content-Type")) {
            av_free(s->mime_type);
            s->mime_type = av_strdup(p);
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
        } else if (!av_strcasecmp (tag, "Icy-MetaInt")) {
            s->icy_metaint = strtoll(p, NULL, 10);
        } else if (!av_strncasecmp(tag, "Icy-", 4)) {
            if ((ret = parse_icy(s, tag, p)) < 0)
                return ret;
        } else if (!av_strcasecmp(tag, "Content-Encoding")) {
            if ((ret = parse_content_encoding(h, p)) < 0)
                return ret;
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
                // if the cookie specifies a sub-domain, skip the leading dot thereby
                // supporting URLs that point to sub-domains and the master domain
                int leading_dot = (param[7] == '.');
                av_free(cdomain);
                cdomain = av_strdup(&param[7+leading_dot]);
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
        if (!cdomain)
            cdomain = av_strdup(domain);

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

        av_log(h, AV_LOG_DEBUG, "header='%s'\n", line);

        err = process_line(h, line, s->line_count, new_location);
        if (err < 0)
            return err;
        if (err == 0)
            break;
        s->line_count++;
    }

    if (s->seekable == -1 && s->is_mediagateway && s->filesize == 2000000000)
        h->is_streamed = 1; /* we can in fact _not_ seek */

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
    int send_expect_100 = 0;


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
    if (post && !s->post_data) {
        send_expect_100 = s->send_expect_100;
        /* The user has supplied authentication but we don't know the auth type,
         * send Expect: 100-continue to get the 401 response including the
         * WWW-Authenticate header, or an 100 continue if no auth actually
         * is needed. */
        if (auth && *auth &&
            s->auth_state.auth_type == HTTP_AUTH_NONE &&
            s->http_code != 401)
            send_expect_100 = 1;
    }

    /* set default headers if needed */
    if (!has_header(s->headers, "\r\nUser-Agent: "))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "User-Agent: %s\r\n", s->user_agent);
    if (!has_header(s->headers, "\r\nAccept: "))
        len += av_strlcpy(headers + len, "Accept: */*\r\n",
                          sizeof(headers) - len);
    // Note: we send this on purpose even when s->off is 0 when we're probing,
    // since it allows us to detect more reliably if a (non-conforming)
    // server supports seeking by analysing the reply headers.
    if (!has_header(s->headers, "\r\nRange: ") && !post && (s->off > 0 || s->end_off || s->seekable == -1)) {
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Range: bytes=%"PRId64"-", s->off);
        if (s->end_off)
            len += av_strlcatf(headers + len, sizeof(headers) - len,
                               "%"PRId64, s->end_off - 1);
        len += av_strlcpy(headers + len, "\r\n",
                          sizeof(headers) - len);
    }
    if (send_expect_100 && !has_header(s->headers, "\r\nExpect: "))
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Expect: 100-continue\r\n");

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
        if (!get_cookies(s, &cookies, path, hoststr) && cookies) {
            len += av_strlcatf(headers + len, sizeof(headers) - len,
                               "Cookie: %s\r\n", cookies);
            av_free(cookies);
        }
    }
    if (!has_header(s->headers, "\r\nIcy-MetaData: ") && s->icy) {
        len += av_strlcatf(headers + len, sizeof(headers) - len,
                           "Icy-MetaData: %d\r\n", 1);
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

    av_log(h, AV_LOG_DEBUG, "request: %s\n", s->buffer);

    if ((err = ffurl_write(s->hd, s->buffer, strlen(s->buffer))) < 0)
        goto done;

    if (s->post_data)
        if ((err = ffurl_write(s->hd, s->post_data, s->post_datalen)) < 0)
            goto done;

    /* init input buffer */
    s->buf_ptr = s->buffer;
    s->buf_end = s->buffer;
    s->line_count = 0;
    s->off = 0;
    s->icy_data_read = 0;
    s->filesize = -1;
    s->willclose = 0;
    s->end_chunked_post = 0;
    s->end_header = 0;
    if (post && !s->post_data && !send_expect_100) {
        /* Pretend that it did work. We didn't read any header yet, since
         * we've still to send the POST data, but the code calling this
         * function will check http_code after we return. */
        s->http_code = 200;
        err = 0;
        goto done;
    }

    /* wait for header */
    err = http_read_header(h, new_location);
    if (err < 0)
        goto done;

    err = (off == s->off) ? 0 : -1;
done:
    av_freep(&authstr);
    av_freep(&proxyauthstr);
    return err;
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

#if CONFIG_ZLIB
#define DECOMPRESS_BUF_SIZE (256 * 1024)
static int http_buf_read_compressed(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;
    int ret;

    if (!s->inflate_buffer) {
        s->inflate_buffer = av_malloc(DECOMPRESS_BUF_SIZE);
        if (!s->inflate_buffer)
            return AVERROR(ENOMEM);
    }

    if (s->inflate_stream.avail_in == 0) {
        int read = http_buf_read(h, s->inflate_buffer, DECOMPRESS_BUF_SIZE);
        if (read <= 0)
            return read;
        s->inflate_stream.next_in  = s->inflate_buffer;
        s->inflate_stream.avail_in = read;
    }

    s->inflate_stream.avail_out = size;
    s->inflate_stream.next_out  = buf;

    ret = inflate(&s->inflate_stream, Z_SYNC_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END)
        av_log(h, AV_LOG_WARNING, "inflate return value: %d, %s\n", ret, s->inflate_stream.msg);

    return size - s->inflate_stream.avail_out;
}
#endif

static int http_read_stream(URLContext *h, uint8_t *buf, int size)
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
#if CONFIG_ZLIB
    if (s->compressed)
        return http_buf_read_compressed(h, buf, size);
#endif
    return http_buf_read(h, buf, size);
}

// Like http_read_stream(), but no short reads.
// Assumes partial reads are an error.
static int http_read_stream_all(URLContext *h, uint8_t *buf, int size)
{
    int pos = 0;
    while (pos < size) {
        int len = http_read_stream(h, buf + pos, size - pos);
        if (len < 0)
            return len;
        pos += len;
    }
    return pos;
}

static int store_icy(URLContext *h, int size)
{
    HTTPContext *s = h->priv_data;
    /* until next metadata packet */
    int remaining = s->icy_metaint - s->icy_data_read;

    if (remaining < 0)
        return AVERROR_INVALIDDATA;

    if (!remaining) {
        // The metadata packet is variable sized. It has a 1 byte header
        // which sets the length of the packet (divided by 16). If it's 0,
        // the metadata doesn't change. After the packet, icy_metaint bytes
        // of normal data follow.
        uint8_t ch;
        int len = http_read_stream_all(h, &ch, 1);
        if (len < 0)
            return len;
        if (ch > 0) {
            char data[255 * 16 + 1];
            int ret;
            len = ch * 16;
            ret = http_read_stream_all(h, data, len);
            if (ret < 0)
                return ret;
            data[len + 1] = 0;
            if ((ret = av_opt_set(s, "icy_metadata_packet", data, 0)) < 0)
                return ret;
        }
        s->icy_data_read = 0;
        remaining = s->icy_metaint;
    }

    return FFMIN(size, remaining);
}

static int http_read(URLContext *h, uint8_t *buf, int size)
{
    HTTPContext *s = h->priv_data;

    if (s->icy_metaint > 0) {
        size = store_icy(h, size);
        if (size < 0)
            return size;
    }

    size = http_read_stream(h, buf, size);
    if (size > 0)
        s->icy_data_read += size;
    return size;
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

#if CONFIG_ZLIB
    inflateEnd(&s->inflate_stream);
    av_freep(&s->inflate_buffer);
#endif

    if (!s->end_chunked_post) {
        /* Close the write direction by sending the end of chunked encoding. */
        ret = http_shutdown(h, h->flags);
    }

    if (s->hd)
        ffurl_closep(&s->hd);
    av_dict_free(&s->chained_options);
    return ret;
}

static int64_t http_seek(URLContext *h, int64_t off, int whence)
{
    HTTPContext *s = h->priv_data;
    URLContext *old_hd = s->hd;
    int64_t old_off = s->off;
    uint8_t old_buf[BUFFER_SIZE];
    int old_buf_size, ret;
    AVDictionary *options = NULL;

    if (whence == AVSEEK_SIZE)
        return s->filesize;
    else if ((whence == SEEK_CUR && off == 0) ||
             (whence == SEEK_SET && off == s->off))
        return s->off;
    else if ((s->filesize == -1 && whence == SEEK_END) || h->is_streamed)
        return AVERROR(ENOSYS);

    if (whence == SEEK_CUR)
        off += s->off;
    else if (whence == SEEK_END)
        off += s->filesize;
    else if (whence != SEEK_SET)
        return AVERROR(EINVAL);
    if (off < 0)
        return AVERROR(EINVAL);
    s->off = off;

    /* we save the old context in case the seek fails */
    old_buf_size = s->buf_end - s->buf_ptr;
    memcpy(old_buf, s->buf_ptr, old_buf_size);
    s->hd = NULL;

    /* if it fails, continue on old connection */
    av_dict_copy(&options, s->chained_options, 0);
    if ((ret = http_open_cnx(h, &options)) < 0) {
        av_dict_free(&options);
        memcpy(s->buffer, old_buf, old_buf_size);
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + old_buf_size;
        s->hd = old_hd;
        s->off = old_off;
        return ret;
    }
    av_dict_free(&options);
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
    .url_open2           = http_open,
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
    .url_open2           = http_open,
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
    ret = ffurl_open(&s->hd, lower_url, AVIO_FLAG_READ_WRITE,
                     &h->interrupt_callback, NULL);
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
