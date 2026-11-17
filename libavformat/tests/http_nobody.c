/*
 * Copyright (c) 2026 Jun Zhao
 *
 * Regression tests for HTTP / httpproxy premature-end handling (issue #26071).
 * Expected output lives in tests/ref/fate/http-head-no-body and
 * tests/ref/fate/httpproxy-tunnel-eof.
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#if HAVE_PTHREADS
#include <pthread.h>

#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/time.h"
#include "libavformat/avio.h"
#include "libavformat/network.h"
#include "libavformat/url.h"

#if !HAVE_WINSOCK2_H && HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#if HAVE_WINSOCK2_H
#define SHUT_WRITE SD_SEND
#define SOCKLEN int
#else
#define SHUT_WRITE SHUT_WR
#define SOCKLEN socklen_t
#endif

#define BODY_SIZE  1150
#define TIMEOUT_MS 5000
#define TIMEOUT_US (TIMEOUT_MS * 1000LL)
#define MARK       "TUNNEL_PREFIX_BYTES_0123456789_ABCDEFGHIJ"
#define GET_HEADER "HTTP/1.1 200 OK\r\nContent-Length: 1150\r\nConnection: close\r\n\r\n"

/* HEAD reply declaring a resource length while legitimately sending no body. */
static const char head_reply[] =
    "HTTP/1.1 206 Partial Content\r\n"
    "Content-Length: 1150\r\n"
    "Content-Range: bytes 0-1149/1150\r\n"
    "Accept-Ranges: bytes\r\n"
    "Connection: close\r\n\r\n";

static const char lie_reply[]     = GET_HEADER;
static const char connect_reply[] = "HTTP/1.1 200 OK\r\nconnection: close\r\n\r\n";

/* CONNECT 200 header and initial tunnel payload sent together in an attempt
 * to exercise forwarding of bytes buffered during header parsing, while still
 * verifying full payload delivery across short transfers. */
static const char coalesced_reply[] = "HTTP/1.1 200 OK\r\n\r\n" MARK;

static char body_reply[sizeof(GET_HEADER) - 1 + BODY_SIZE];

enum { GROUP_HTTP = 1, GROUP_PROXY = 2 };
enum { PRINT_NONE, PRINT_ENDS, PRINT_TEXT };

typedef struct TestCase {
    const char *name;
    int group;
    const char *url_fmt;
    const char *method;
    const char *reply;
    int reply_len;
    int payload_print;
} TestCase;

typedef struct TestServer {
    int fd, port, reply_len, failed;
    const char *reply;
    FFNetWakeup wakeup;
    pthread_t thread;
} TestServer;

static void set_timeouts(int fd)
{
#if HAVE_WINSOCK2_H
    DWORD ms = TIMEOUT_MS;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv = { TIMEOUT_MS / 1000, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static int send_all(int fd, const char *buf, int len)
{
    for (int sent = 0; sent < len;) {
        int r = send(fd, buf + sent, len - sent, 0);
        if (r < 0 && ff_neterrno() == AVERROR(EINTR))
            continue;
        if (r <= 0)
            return -1;
        sent += r;
    }
    return 0;
}

static void *server_main(void *arg)
{
    TestServer *s = arg;
    int64_t deadline = av_gettime_relative() + TIMEOUT_US;
    char req[4096] = { 0 };
    int c, w, n = 0;

    do {
        int64_t rem_ms = (deadline - av_gettime_relative() + 999) / 1000;
        w = rem_ms > 0 ? ff_network_wait_fd_wakeup(s->fd, 0, (int)rem_ms, &s->wakeup)
                       : AVERROR(ETIMEDOUT);
    } while (w == AVERROR(EINTR));

    if (w < 0 || (c = accept(s->fd, NULL, NULL)) < 0) {
        s->failed = 1;
        return NULL;
    }
    set_timeouts(c);

    while (n < (int)sizeof(req) - 1 && !strstr(req, "\r\n\r\n")) {
        int r = recv(c, req + n, sizeof(req) - 1 - n, 0);
        if (r < 0 && ff_neterrno() == AVERROR(EINTR))
            continue;
        if (r <= 0)
            break;
        req[n += r] = 0;
    }

    if (!strstr(req, "\r\n\r\n") || send_all(c, s->reply, s->reply_len) < 0)
        s->failed = 1;
    shutdown(c, SHUT_WRITE);
    closesocket(c);
    return NULL;
}

static int server_start(TestServer *s, const char *reply, int reply_len)
{
    struct sockaddr_in addr = { .sin_family = AF_INET };
    SOCKLEN alen = sizeof(addr);
    int on = 1;

    memset(s, 0, sizeof(*s));
    s->fd        = -1;
    s->reply     = reply;
    s->reply_len = reply_len;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (ff_network_wakeup_open(&s->wakeup) < 0)
        return -1;
    if ((s->fd = ff_socket(AF_INET, SOCK_STREAM, 0, NULL)) < 0)
        goto fail;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    if (bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(s->fd, 1) < 0 ||
        getsockname(s->fd, (struct sockaddr *)&addr, &alen) < 0)
        goto fail;
    s->port = ntohs(addr.sin_port);

    if (!pthread_create(&s->thread, NULL, server_main, s))
        return 0;
fail:
    if (s->fd >= 0)
        closesocket(s->fd);
    ff_network_wakeup_close(&s->wakeup);
    return -1;
}

static void server_stop(TestServer *s)
{
    ff_network_wakeup_signal(&s->wakeup);
    pthread_join(s->thread, NULL);
    closesocket(s->fd);
    ff_network_wakeup_close(&s->wakeup);
}

static int check_deadline(void *opaque)
{
    return av_gettime_relative() > *(const int64_t *)opaque;
}

static void format_status(char *dst, size_t dst_len, int64_t val)
{
    if (val == AVERROR_EOF)
        snprintf(dst, dst_len, "EOF");
    else if (val == AVERROR(EIO))
        snprintf(dst, dst_len, "EIO");
    else if (val == AVERROR(ENOSYS))
        snprintf(dst, dst_len, "ENOSYS");
    else
        snprintf(dst, dst_len, "%"PRId64, val);
}

static void run_case(const TestCase *tc)
{
    TestServer srv;
    URLContext *uc = NULL;
    AVDictionary *opts = NULL;
    int64_t deadline = av_gettime_relative() + TIMEOUT_US, size_ret = 0;
    AVIOInterruptCB int_cb = { check_deadline, &deadline };
    char url[256], open_s[32], size_s[32], read_s[32];
    uint8_t buf[4096];
    int open_ret, read_ret = 0;

    if (server_start(&srv, tc->reply, tc->reply_len) < 0) {
        printf("%s: server_start failed\n", tc->name);
        return;
    }

    snprintf(url, sizeof(url), tc->url_fmt, srv.port);
    if (tc->group == GROUP_HTTP)
        av_dict_set(&opts, "http_proxy", "", 0);
    av_dict_set_int(&opts, "rw_timeout", TIMEOUT_US, 0);
    if (tc->method)
        av_dict_set(&opts, "method", tc->method, 0);

    open_ret = ffurl_open_whitelist(&uc, url, AVIO_FLAG_READ, &int_cb, &opts,
                                    "http,httpproxy,tcp", NULL, NULL);
    av_dict_free(&opts);
    if (open_ret >= 0) {
        size_ret = ffurl_seek(uc, 0, AVSEEK_SIZE);
        read_ret = ffurl_read_complete(uc, buf, sizeof(buf));
        ffurl_closep(&uc);
    }

    server_stop(&srv);

    format_status(open_s, sizeof(open_s), open_ret);
    format_status(size_s, sizeof(size_s), size_ret);
    format_status(read_s, sizeof(read_s), read_ret);
    printf("%s: open=%s size=%s read=%s server_failed=%d\n",
           tc->name, open_s, size_s, read_s, srv.failed);

    if (read_ret > 0 && tc->payload_print == PRINT_ENDS)
        printf("body ends: %c %c\n", buf[0], buf[read_ret - 1]);
    else if (read_ret > 0 && tc->payload_print == PRINT_TEXT)
        printf("residue: %.*s\n", read_ret, buf);
}

static const TestCase cases[] = {
    { "head",          GROUP_HTTP,  "http://127.0.0.1:%d/x",
      "HEAD", head_reply,      sizeof(head_reply) - 1,      PRINT_NONE },
    { "get_body",      GROUP_HTTP,  "http://127.0.0.1:%d/x",
      NULL,   body_reply,      sizeof(body_reply),          PRINT_ENDS },
    { "get_lie",       GROUP_HTTP,  "http://127.0.0.1:%d/x",
      NULL,   lie_reply,       sizeof(lie_reply) - 1,       PRINT_NONE },
    { "proxy_close",   GROUP_PROXY, "httpproxy://127.0.0.1:%d/example.com:443",
      NULL,   connect_reply,   sizeof(connect_reply) - 1,   PRINT_NONE },
    { "proxy_residue", GROUP_PROXY, "httpproxy://127.0.0.1:%d/example.com:443",
      NULL,   coalesced_reply, sizeof(coalesced_reply) - 1, PRINT_TEXT },
};

int main(int argc, char **argv)
{
    const char *group = argc > 1 ? argv[1] : "all";
    int mask = !strcmp(group, "all")   ? (GROUP_HTTP | GROUP_PROXY) :
               !strcmp(group, "http")  ? GROUP_HTTP :
               !strcmp(group, "proxy") ? GROUP_PROXY : 0;

    if (!mask) {
        fprintf(stderr, "Unknown test group: %s\n", group);
        return 1;
    }

    memcpy(body_reply, GET_HEADER, sizeof(GET_HEADER) - 1);
    memset(body_reply + sizeof(GET_HEADER) - 1, 'X', BODY_SIZE);

    if (ff_network_init() < 0)
        return 1;

    for (size_t i = 0; i < FF_ARRAY_ELEMS(cases); i++)
        if (cases[i].group & mask)
            run_case(&cases[i]);

    ff_network_close();
    return 0;
}
#else
int main(void)
{
    printf("SKIP  no pthreads, loopback server unavailable\n");
    return 0;
}
#endif
