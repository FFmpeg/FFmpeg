/*
 * libcurl based HTTP(S) protocol
 * Copyright (C) 2026 Kacper Michajłow
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

#include "config_components.h"

#include <curl/curl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"

#include "avformat.h"
#include "http.h"
#include "internal.h"
#include "url.h"
#include "version.h"

#define DEFAULT_USER_AGENT "Lavf/" AV_STRINGIFY(LIBAVFORMAT_VERSION)
#define CURL_DEFAULT_BUFFER_SIZE (4 << 20)

/* Blocking waits wake up this often so url_read()/open can poll the interrupt
 * callback. */
#define CURL_WAIT_US 100000

typedef struct CurlContext CurlContext;

enum cmd_kind {
    CMD_ADD,     /* add the easy handle to the multi and start the transfer */
    CMD_REMOVE,  /* remove the easy handle from the multi */
    CMD_UNPAUSE, /* resume a transfer paused because the FIFO was full */
    CMD_SEEK,    /* restart the transfer at a new byte offset */
};

typedef struct CurlCmd {
    enum cmd_kind   kind;
    CurlContext    *ctx;
    int64_t         pos;    /* CMD_SEEK target offset */
    int             sync;   /* caller waits for completion */
    int             done;
    struct CurlCmd *next;
} CurlCmd;

typedef struct CurlLoop {
    AVFormatContext *avfc;  /* owning context (if any) */

    pthread_t       thread;
    CURLM          *multi;
    CURLSH         *share;   /* shared cookies/HSTS */

    pthread_mutex_t mutex;   /* guards the command queue, exit and cmd->done */
    pthread_cond_t  cond;    /* signaled when a sync command completes */
    CurlCmd        *cmd_head, *cmd_tail;
    int             exit;

    /* Connection statistics (updated by loop thread) */
    int64_t        total_bytes;
    int64_t        total_time_us;
    int            num_connections;
    int            num_redirects;
    int            num_requests;
    int            num_errors;
} CurlLoop;

struct CurlContext {
    const AVClass  *class;
    URLContext     *h;

    CurlLoop       *loop;
    int             private_loop;  /* loop is owned by this context (not shared) */
    CURL           *easy;
    struct curl_slist *header_list;

    /* AVOptions. */
    char           *user_agent;
    char           *referer;
    char           *headers;
    char           *http_proxy;
    char           *cookies;
    char           *ca_file;
    char           *cert_file;
    char           *key_file;
    char           *location;   /* effective URL after redirects (output) */
    int64_t         off;        /* initial byte offset */
    int64_t         end_off;    /* exclusive upper byte bound (0 = none) */
    int             tls_verify;
    int             seekable_opt;
    int             connect_timeout;
    int             max_redirects;
    int             multiple_requests;
    int             http_version;
    int64_t         buffer_size;
    int64_t         request_size;
    int64_t         initial_request_size;
    int64_t         short_seek_size;
    int             max_retries;

    int64_t         logical_pos; /* next byte url_read() will return, caller side */

    /* Producer bookkeeping, touched only by the loop thread. */
    int             active;          /* currently added to the multi */
    int64_t         request_start;   /* absolute offset the current request began at */
    int64_t         request_received;/* bytes delivered in the current request */
    int64_t         request_end;     /* expected end of request, or -1 if unknown */
    int             retry_count;     /* consecutive recoverable failures */
    int             is_initial;      /* using reduced request size */
    int             seek_queued;     /* soft seeking; drain remaining bytes until done */

    /* Per-response-block header scratch, loop thread only. */
    int             hdr_accept_ranges;
    int             hdr_compressed;
    int64_t         hdr_content_start; /* inclusive start, or -1 */
    int64_t         hdr_content_end;   /* inclusive end,   or -1 */
    int64_t         hdr_content_total; /* if known, or -1 */

    /* Probe result. Set by the loop thread, read by url_open() once probed. */
    int             probed;
    int             stream_ok;
    int             seekable;
    int64_t         content_size;

    /* Shared transfer state, guarded by mutex. */
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    AVFifo         *fifo;
    int             paused;      /* write callback paused, FIFO was full */
    int             status;      /* current stream status (AVERROR code) */
    int             aborted;     /* transfer should stop (open was interrupted) */
};

/* Guards lazy creation of a format context's shared loop. */
static AVMutex curl_loop_lock = AV_MUTEX_INITIALIZER;

static int curlcode_to_averror(CURLcode code)
{
    switch (code) {
    case CURLE_OK:                       return 0;
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL:     return AVERROR(EINVAL);
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_RESOLVE_HOST:     return AVERROR(EHOSTUNREACH);
    case CURLE_COULDNT_CONNECT:          return AVERROR(ECONNREFUSED);
    case CURLE_OPERATION_TIMEDOUT:       return AVERROR(ETIMEDOUT);
    case CURLE_LOGIN_DENIED:
    case CURLE_REMOTE_ACCESS_DENIED:     return AVERROR(EACCES);
    case CURLE_OUT_OF_MEMORY:            return AVERROR(ENOMEM);
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:       return AVERROR_INVALIDDATA;
    default:                             return AVERROR(EIO);
    }
}

static int is_recoverable(CURLcode code)
{
    switch (code) {
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_GOT_NOTHING:
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_HTTP2:
    case CURLE_HTTP2_STREAM:
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* curl callbacks (run on the loop thread)                                   */
/* ------------------------------------------------------------------------- */

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    CurlContext *c = userdata;
    size_t bytes = size * nmemb;
    size_t space;

    pthread_mutex_lock(&c->mutex);

    if (c->aborted || !c->stream_ok) {
        pthread_mutex_unlock(&c->mutex);
        return CURL_WRITEFUNC_ERROR;
    }

    if (c->seek_queued) {
        pthread_mutex_unlock(&c->mutex);
        return bytes; /* discard */
    }

    space = av_fifo_can_write(c->fifo);
    if (space < bytes) {
        /* pause the transfer and wait for the consumer to drain. */
        c->paused = 1;
        pthread_mutex_unlock(&c->mutex);
        return CURL_WRITEFUNC_PAUSE;
    }

    av_fifo_write(c->fifo, ptr, bytes);
    c->paused = 0;
    c->request_received += bytes;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mutex);

    return bytes;
}

static int64_t parse_offset(const char *s)
{
    int64_t v = strtoll(s, NULL, 10);
    return v < 0 ? -1 : v;
}

/* "bytes $from-$to/$document_size" */
static void parse_content_range(CurlContext *c, const char *v)
{
    while (av_isspace(*v))
        v++;

    if (av_strncasecmp(v, "bytes ", 6))
        return;

    const char *range = v + 6, *end;
    if (range[0] != '*') {
        c->hdr_content_start = parse_offset(range);
        if ((end = strchr(range, '-')))
            c->hdr_content_end = parse_offset(end + 1);
    }

    const char *slash = strchr(range, '/');
    if (slash && slash[1] != '*')
        c->hdr_content_total = parse_offset(slash + 1);
}

static size_t header_callback(char *ptr, size_t size, size_t nitems, void *userdata)
{
    CurlContext *c = userdata;
    size_t len = size * nitems;
    size_t n = len;
    long status = 0;

    if (av_strncasecmp(ptr, "HTTP/", 5) == 0) {
        c->hdr_accept_ranges = 0;
        c->hdr_compressed    = 0;
        c->hdr_content_start = -1;
        c->hdr_content_end   = -1;
        c->hdr_content_total = -1;
        return len;
    }
    if (av_strncasecmp(ptr, "Accept-Ranges:", 14) == 0) {
        c->hdr_accept_ranges = !!av_stristr(ptr + 14, "bytes");
        return len;
    }
    if (av_strncasecmp(ptr, "Content-Encoding:", 17) == 0) {
        c->hdr_compressed = !av_stristr(ptr + 17, "identity");
        return len;
    }
    if (av_strncasecmp(ptr, "Content-Range:", 14) == 0) {
        parse_content_range(c, ptr + 14);
        return len;
    }

    /* Otherwise act only on the blank line that terminates the header block. */
    while (n && (ptr[n - 1] == '\r' || ptr[n - 1] == '\n'))
        n--;
    if (n)
        return len;

    curl_easy_getinfo(c->easy, CURLINFO_RESPONSE_CODE, &status);

    /* Interim (1xx) and redirect (3xx) responses produce an intermediate header
     * block, wait for the final one. */
    if (status < 200 || (status >= 300 && status < 400))
        return len;

    pthread_mutex_lock(&c->mutex);
    if (status >= 200 && status < 300) {
        int64_t content_start = status == 206 ? c->hdr_content_start : 0;
        /* The reply must start at the offset we requested: for follow-up
         * requests always, for the initial one when an explicit nonzero
         * offset was requested. */
        if ((c->probed ? c->seekable : c->off > 0) &&
            content_start != c->request_start) {
            av_log(c->h, AV_LOG_ERROR, "Server sent back unexpected reply "
                   "with offset %"PRId64" (expected %"PRId64")\n",
                   content_start, c->request_start);
            c->loop->num_errors++;
            c->stream_ok = 0;
            if (!c->status)
                c->status = AVERROR(EIO);
            pthread_cond_broadcast(&c->cond);
            pthread_mutex_unlock(&c->mutex);
            return len;
        }

        c->stream_ok = 1;
        /* Capture the post-redirect URL, this is exposed as "location" AVOption
         * for compatibility with http.c. */
        if (!c->probed) {
            const char *eff = NULL;
            if (curl_easy_getinfo(c->easy, CURLINFO_EFFECTIVE_URL, &eff) == CURLE_OK
                && eff) {
                char *dup = av_strdup(eff);
                if (dup) {
                    av_free(c->location);
                    c->location = dup;
                }
            }
        }
        /* A compressed body is addressed in encoded form, so byte offsets are
         * meaningless: not seekable. Note that we prefer compression over
         * seekability, servers don't offer media in compressed form, so it
         * gives us free compression for other payloads like text playlist. */
        c->seekable = !c->hdr_compressed &&
                      (status == 206 || c->hdr_accept_ranges);
        if (!c->hdr_compressed) {
            int64_t total = c->hdr_content_total;
            if (total < 0 && status != 206) {
                curl_off_t cl = -1;
                if (curl_easy_getinfo(c->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                                      &cl) == CURLE_OK && cl >= 0)
                    total = cl;
            }
            /* Don't unlearn a known size when a reply omits it. */
            if (total >= 0)
                c->content_size = total;
        }
        if (c->seekable) {
            if (c->hdr_content_end >= 0)
                c->request_end = c->hdr_content_end;
            else
                c->request_end = c->content_size > 0 ? c->content_size - 1 : -1;
        }
        /* Apply the user override on every reply so re-evaluation of a
         * follow-up reply doesn't clobber it. */
        if (c->seekable_opt >= 0)
            c->seekable = c->seekable_opt;
    } else {
        c->loop->num_errors++;
        c->stream_ok = 0;
        if (!c->status)
            c->status = ff_http_averror(status, AVERROR(EIO));
    }
    c->probed = 1;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mutex);

    return len;
}

static int xferinfo_callback(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow)
{
    CurlContext *c = userdata;
    int aborted;
    pthread_mutex_lock(&c->mutex);
    aborted = c->aborted;
    pthread_mutex_unlock(&c->mutex);
    return aborted; /* non-zero aborts the transfer */
}

/* (Re)issue the request for the current offset and add it to the multi. Loop
 * thread only. */
static void start_request(CurlContext *c)
{
    if (!c->probed || c->seekable) {
        int64_t start = c->request_start;
        char range[48];
        int64_t request_size = c->request_size;
        if (c->is_initial && c->initial_request_size > 0)
            request_size = c->initial_request_size;
        if (request_size > 0 || c->end_off > 0) {
            int64_t end = INT64_MAX;
            if (request_size > 0 && start <= INT64_MAX - request_size)
                end = start + request_size - 1;
            if (c->content_size > 0)
                end = FFMIN(end, c->content_size - 1);
            if (c->end_off > 0)
                end = FFMIN(end, c->end_off - 1);
            snprintf(range, sizeof(range), "%"PRId64"-%"PRId64, start, end);
        } else {
            snprintf(range, sizeof(range), "%"PRId64"-", start);
        }
        curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    } else {
        curl_easy_setopt(c->easy, CURLOPT_RANGE, NULL);
    }
    c->loop->num_requests++;
    c->request_received = 0;
    c->request_end = -1;
    c->active = 1;
    CURLMcode res = curl_multi_add_handle(c->loop->multi, c->easy);
    if (res != CURLM_OK) {
        av_log(c->h, AV_LOG_ERROR, "curl_multi_add_handle: %s\n",
               curl_multi_strerror(res));
        c->active = 0;
        pthread_mutex_lock(&c->mutex);
        if (!c->status)
            c->status = AVERROR(EIO);
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
    }
}

static void update_statistics(CurlContext *c)
{
    CurlLoop *loop = c->loop;
    CURL *e = c->easy;

    curl_off_t recv = 0, time = 0;
    curl_easy_getinfo(e, CURLINFO_SIZE_DOWNLOAD_T,  &recv);
    curl_easy_getinfo(e, CURLINFO_TOTAL_TIME_T,     &time);

    if (recv) {
        av_log(c->h, AV_LOG_DEBUG, "%"PRId64" bytes received in %"PRId64" ms\n",
               (int64_t) recv, (int64_t) time / 1000);

        loop->total_bytes   += recv;
        loop->total_time_us += time;
    }

    long num_conns = 0, num_redirs = 0;
    curl_easy_getinfo(e, CURLINFO_NUM_CONNECTS,     &num_conns);
    curl_easy_getinfo(e, CURLINFO_REDIRECT_COUNT,   &num_redirs);
    loop->num_connections += (int) num_conns;
    loop->num_redirects   += (int) num_redirs;
}

/* Transfer finished (or failed) */
static void on_done(CurlContext *c, CURLcode code)
{
    int64_t received;
    int aborted;

    pthread_mutex_lock(&c->mutex);
    aborted  = c->aborted;
    received = c->request_received;
    /* Advance past delivered bytes so a retry or seek resumes at the right offset. */
    if (received > INT64_MAX - c->request_start) {
        if (!c->status)
            c->status = AVERROR(EIO);
        received = 0;
        aborted  = 1;
        pthread_cond_broadcast(&c->cond);
    }
    c->request_start    += received;
    c->request_received  = 0;
    pthread_mutex_unlock(&c->mutex);
    update_statistics(c);

    if (!c->probed) {
        /* Connection died before any usable header arrived. */
        pthread_mutex_lock(&c->mutex);
        c->probed    = 1;
        c->stream_ok = 0;
        if (!c->status)
            c->status = curlcode_to_averror(code);
        c->loop->num_errors++;
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    if (aborted)
        return;

    if (c->seek_queued) {
        /* previous soft seek drain finished; can start new request now */
        c->seek_queued = 0;
        start_request(c);
        return;
    }

    if (code == CURLE_OK && c->stream_ok) {
        c->retry_count = 0;
        int64_t file_end = c->content_size > 0 ? c->content_size - 1 : -1;
        if (c->end_off > 0)
            file_end = FFMIN(file_end, c->end_off - 1);
        if (c->seekable && c->request_end >= 0 && c->request_end < file_end) {
            c->is_initial = 0;
            start_request(c);
            return;
        }
        pthread_mutex_lock(&c->mutex);
        c->status = AVERROR_EOF;
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    if (c->stream_ok) {
        av_log(c->h, AV_LOG_WARNING, "%s\n", curl_easy_strerror(code));
        c->loop->num_errors++;
    }

    /* Resume seekable transfers after a recoverable error. */
    if (c->seekable && is_recoverable(code) &&
        c->retry_count < c->max_retries) {
        c->retry_count++;
        av_log(c->h, AV_LOG_WARNING, "Retrying (#%d) from %"PRId64"\n",
               c->retry_count, c->request_start);
        start_request(c);
        return;
    }

    /* Unhandled generic curl error */
    pthread_mutex_lock(&c->mutex);
    if (!c->status)
        c->status = curlcode_to_averror(code);
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

/* ------------------------------------------------------------------------- */
/* event loop thread + command queue                                         */
/* ------------------------------------------------------------------------- */

static int test_short_seek(CurlContext *c)
{
    if (c->seek_queued)
        return 1; /* short seek already queued */

    if (c->short_seek_size <= 0 || /* short seek disabled */
        c->request_end < 0)        /* content size not known */
        return 0;

    const int64_t last = c->request_end - c->request_start;
    return last - c->request_received < c->short_seek_size;
}

static void execute_command(CurlLoop *loop, CurlCmd *cmd)
{
    CurlContext *c = cmd->ctx;

    switch (cmd->kind) {
    case CMD_ADD:
        start_request(c);
        break;
    case CMD_REMOVE:
        if (c->active) {
            curl_multi_remove_handle(loop->multi, c->easy);
            update_statistics(c);
            c->active = 0;
        }
        break;
    case CMD_UNPAUSE:
        pthread_mutex_lock(&c->mutex);
        c->paused = 0;
        pthread_mutex_unlock(&c->mutex);
        curl_easy_pause(c->easy, CURLPAUSE_CONT);
        break;
    case CMD_SEEK:
        if (c->active && test_short_seek(c)) {
            c->seek_queued = 1;
        } else if (c->active) {
            curl_multi_remove_handle(loop->multi, c->easy);
            c->active = 0;
        }
        pthread_mutex_lock(&c->mutex);
        av_fifo_reset2(c->fifo);
        const int was_paused = c->paused;
        c->paused = 0;
        c->status = 0;
        pthread_mutex_unlock(&c->mutex);
        c->request_start    = cmd->pos;
        c->request_received = 0;
        c->retry_count      = 0;
        if (!c->seek_queued)
            start_request(c);
        else if (was_paused)
            curl_easy_pause(c->easy, CURLPAUSE_CONT);
        break;
    }
}

static void *curl_worker(void *arg)
{
    CurlLoop *loop = arg;

    ff_thread_setname("curl");

    while (1) {
        CurlCmd *cmd;
        CURLMsg *msg;
        int running = 0, left = 0, do_exit;

        pthread_mutex_lock(&loop->mutex);
        cmd = loop->cmd_head;
        if (cmd) {
            loop->cmd_head = cmd->next;
            if (!loop->cmd_head)
                loop->cmd_tail = NULL;
        }
        do_exit = loop->exit;
        pthread_mutex_unlock(&loop->mutex);

        if (cmd) {
            execute_command(loop, cmd);
            if (cmd->sync) {
                pthread_mutex_lock(&loop->mutex);
                cmd->done = 1;
                pthread_cond_broadcast(&loop->cond);
                pthread_mutex_unlock(&loop->mutex);
            } else {
                av_free(cmd);
            }
            continue; /* drain the whole queue before pumping curl */
        }

        if (do_exit)
            break;

        curl_multi_perform(loop->multi, &running);

        while ((msg = curl_multi_info_read(loop->multi, &left))) {
            CurlContext *c = NULL;
            if (msg->msg != CURLMSG_DONE)
                continue;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &c);
            curl_multi_remove_handle(loop->multi, msg->easy_handle);
            if (c) {
                c->active = 0;
                on_done(c, msg->data.result);
            }
        }

        curl_multi_poll(loop->multi, NULL, 0, 1000, NULL);
    }

    return NULL;
}

/* Dispatch a command to the loop. For sync commands the caller blocks until the
 * loop thread has executed it. Returns 0 or a negative AVERROR. */
static int curl_dispatch(CurlLoop *loop, enum cmd_kind kind, CurlContext *c,
                         int64_t pos, int sync)
{
    CurlCmd stackcmd = {0};
    CurlCmd *cmd = sync ? &stackcmd : av_mallocz(sizeof(*cmd));

    if (!cmd)
        return AVERROR(ENOMEM);

    cmd->kind = kind;
    cmd->ctx  = c;
    cmd->pos  = pos;
    cmd->sync = sync;

    pthread_mutex_lock(&loop->mutex);
    if (loop->cmd_tail)
        loop->cmd_tail->next = cmd;
    else
        loop->cmd_head = cmd;
    loop->cmd_tail = cmd;
    curl_multi_wakeup(loop->multi);
    if (sync) {
        while (!cmd->done)
            pthread_cond_wait(&loop->cond, &loop->mutex);
    }
    pthread_mutex_unlock(&loop->mutex);

    return 0;
}

static CurlLoop *curl_loop_create(AVFormatContext *avfc)
{
    CurlLoop *loop = av_mallocz(sizeof(*loop));
    if (!loop)
        return NULL;
    loop->avfc = avfc;

    if (pthread_mutex_init(&loop->mutex, NULL))
        goto fail;
    if (pthread_cond_init(&loop->cond, NULL)) {
        pthread_mutex_destroy(&loop->mutex);
        goto fail;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        goto fail2;

    loop->multi = curl_multi_init();
    if (!loop->multi)
        goto fail3;
    curl_multi_setopt(loop->multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    loop->share = curl_share_init();
    if (!loop->share)
        goto fail3;
    curl_share_setopt(loop->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(loop->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_HSTS);

    if (pthread_create(&loop->thread, NULL, curl_worker, loop))
        goto fail3;

    return loop;

fail3:
    curl_multi_cleanup(loop->multi);
    curl_share_cleanup(loop->share);
    curl_global_cleanup();
fail2:
    pthread_cond_destroy(&loop->cond);
    pthread_mutex_destroy(&loop->mutex);
fail:
    av_free(loop);
    return NULL;
}

static void print_statistics(CurlLoop *loop)
{
    AVFormatContext *avfc = loop->avfc;

    if (loop->total_bytes) {
        double time = loop->total_time_us / 1000000.0;
        double avg  = time ? loop->total_bytes / time : 0;
        av_log(avfc, AV_LOG_VERBOSE,
               "libcurl: Overall %"PRId64" bytes received in %.0f ms = %.0f kB/s\n",
               loop->total_bytes, time * 1e3, avg / 1e3);
    }

    if (loop->num_connections || loop->num_errors) {
        av_log(avfc, AV_LOG_VERBOSE,
               "libcurl: %d connections, %d redirects, %d requests, %d errors\n",
               loop->num_connections, loop->num_redirects, loop->num_requests,
               loop->num_errors);
    }
}

static void curl_loop_destroy(CurlLoop *loop)
{
    pthread_mutex_lock(&loop->mutex);
    loop->exit = 1;
    curl_multi_wakeup(loop->multi);
    pthread_mutex_unlock(&loop->mutex);

    pthread_join(loop->thread, NULL);
    print_statistics(loop);

    curl_multi_cleanup(loop->multi);
    curl_share_cleanup(loop->share);
    pthread_cond_destroy(&loop->cond);
    pthread_mutex_destroy(&loop->mutex);
    av_free(loop);

    /* Released after the thread is joined and the multi handle is gone. */
    curl_global_cleanup();
}

/* Attach a context to its event loop. With an owning AVFormatContext the loop is
 * created lazily, cached on it, and shared across the demuxer's transfers so curl
 * reuses connections; it is freed at format teardown. Without one the context
 * gets a private loop freed on close. */
static int curl_loop_attach(CurlContext *c, AVFormatContext *avfc)
{
    if (!avfc) {
        c->loop = curl_loop_create(NULL);
        c->private_loop = 1;
        return c->loop ? 0 : AVERROR(ENOMEM);
    }

    pthread_mutex_lock(&curl_loop_lock);
    c->loop = ffformatcontext(avfc)->curl_loop;
    if (!c->loop) {
        c->loop = curl_loop_create(avfc);
        ffformatcontext(avfc)->curl_loop = c->loop;
    }
    pthread_mutex_unlock(&curl_loop_lock);

    return c->loop ? 0 : AVERROR(ENOMEM);
}

void ff_curl_loop_free(struct CurlLoop **loop)
{
    if (loop && *loop) {
        curl_loop_destroy(*loop);
        *loop = NULL;
    }
}

/* ------------------------------------------------------------------------- */
/* URLProtocol callbacks                                                     */
/* ------------------------------------------------------------------------- */

static int libcurl_close(URLContext *h);

static int debug_callback(CURL *easy, curl_infotype type, char *data,
                          size_t size, void *userdata)
{
    CurlContext *c = userdata;
    const char *prefix, *p = data, *end = data + size;

    switch (type) {
    case CURLINFO_TEXT:       prefix = "* "; break;
    case CURLINFO_HEADER_IN:  prefix = "< "; break;
    case CURLINFO_HEADER_OUT: prefix = "> "; break;
    default:                  return 0;
    }

    /* Split multiline payload into each log. */
    while (p < end) {
        const char *nl = memchr(p, '\n', end - p);
        size_t len = (nl ? nl : end) - p;
        while (len && p[len - 1] == '\r')
            len--;
        av_log(c->h, AV_LOG_DEBUG, "%s%.*s\n", prefix, (int)len, p);
        if (!nl)
            break;
        p = nl + 1;
    }
    return 0;
}

/* Build the custom request header list from the referer and headers options. */
static struct curl_slist *build_headers(CurlContext *c)
{
    struct curl_slist *list = NULL;

    if (c->referer && c->referer[0]) {
        char *h = av_asprintf("Referer: %s", c->referer);
        if (h) {
            list = curl_slist_append(list, h);
            av_free(h);
        }
    }
    if (c->headers && c->headers[0]) {
        char *copy = av_strdup(c->headers);
        char *line, *saveptr = NULL;
        if (copy) {
            for (line = av_strtok(copy, "\r\n", &saveptr); line;
                 line = av_strtok(NULL, "\r\n", &saveptr))
                list = curl_slist_append(list, line);
            av_free(copy);
        }
    }
    return list;
}

static int setup_protocols(CurlContext *c)
{
    const char *wl = c->h->protocol_whitelist;
    const char *bl = c->h->protocol_blacklist;
    if (!wl && !bl)
        return 0;

    AVBPrint bp;
    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);

    curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    for (const char *const *p = info->protocols; *p; p++) {
        const char *proto = *p;
        if (av_strcasecmp(proto, "http") && av_strcasecmp(proto, "https"))
            continue; /* only http(s) are supported by libcurl.c at the moment */
        if (wl && av_match_list(proto, wl, ',') <= 0)
            continue;
        if (bl && av_match_list(proto, bl, ',') > 0)
            continue;
        if (bp.len)
            av_bprint_chars(&bp, ',', 1);
        av_bprintf(&bp, "%s", proto);
    }

    if (!av_bprint_is_complete(&bp)) {
        av_bprint_finalize(&bp, NULL);
        return AVERROR(ENOMEM);
    }

    if (!bp.len) {
        av_log(c->h, AV_LOG_ERROR, "Set of allowed protocols is empty.\n");
        av_bprint_finalize(&bp, NULL);
        return AVERROR(EINVAL);
    }

    curl_easy_setopt(c->easy, CURLOPT_PROTOCOLS_STR, bp.str);
    curl_easy_setopt(c->easy, CURLOPT_REDIR_PROTOCOLS_STR, bp.str);
    av_bprint_finalize(&bp, NULL);
    return 0;
}

static void setup_curl(CurlContext *c)
{
    CURL *e = c->easy;
    const char *url = c->h->filename;

    /* Drop an optional "libcurl:" prefix that forces this protocol. */
    av_strstart(url, "libcurl:", &url);

    curl_easy_setopt(e, CURLOPT_URL, url);
    curl_easy_setopt(e, CURLOPT_PRIVATE, c);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(e, CURLOPT_SHARE, c->loop->share);

    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, c);

    curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(e, CURLOPT_XFERINFODATA, c);

    if (av_log_get_level() >= AV_LOG_DEBUG) {
        curl_easy_setopt(e, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(e, CURLOPT_DEBUGFUNCTION, debug_callback);
        curl_easy_setopt(e, CURLOPT_DEBUGDATA, c);
    }

    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_MAXREDIRS, (long)c->max_redirects);
    curl_easy_setopt(e, CURLOPT_HTTP_VERSION, (long)c->http_version);
    curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, c->multiple_requests ? 1L : 0L);
    curl_easy_setopt(e, CURLOPT_FORBID_REUSE,  c->multiple_requests ? 0L : 1L);
    curl_easy_setopt(e, CURLOPT_HSTS_CTRL, (long)CURLHSTS_ENABLE);
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING,
                     c->off > 0 || c->end_off > 0 ? "identity" : "");
    if (c->connect_timeout > 0)
        curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS,
                         (long)c->connect_timeout * 1000);

    if (c->user_agent && c->user_agent[0])
        curl_easy_setopt(e, CURLOPT_USERAGENT, c->user_agent);
    if (c->http_proxy && c->http_proxy[0])
        curl_easy_setopt(e, CURLOPT_PROXY, c->http_proxy);

    curl_easy_setopt(e, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYPEER, c->tls_verify ? 1L : 0L);
    curl_easy_setopt(e, CURLOPT_SSL_VERIFYHOST, c->tls_verify ? 2L : 0L);
    if (c->ca_file)
        curl_easy_setopt(e, CURLOPT_CAINFO, c->ca_file);
    if (c->cert_file)
        curl_easy_setopt(e, CURLOPT_SSLCERT, c->cert_file);
    if (c->key_file)
        curl_easy_setopt(e, CURLOPT_SSLKEY, c->key_file);

    curl_easy_setopt(e, CURLOPT_COOKIEFILE, "");
    if (c->cookies && c->cookies[0]) {
        char *copy = av_strdup(c->cookies);
        char *line, *saveptr = NULL;
        if (copy) {
            for (line = av_strtok(copy, "\r\n", &saveptr); line;
                 line = av_strtok(NULL, "\r\n", &saveptr)) {
                char *sc = av_asprintf("Set-Cookie: %s", line);
                if (sc) {
                    curl_easy_setopt(e, CURLOPT_COOKIELIST, sc);
                    av_free(sc);
                }
            }
            av_free(copy);
        }
    }

    c->header_list = build_headers(c);
    if (c->header_list)
        curl_easy_setopt(e, CURLOPT_HTTPHEADER, c->header_list);
}

static void curl_cond_wait(CurlContext *c)
{
    int64_t t = av_gettime() + CURL_WAIT_US;
    struct timespec ts = { .tv_sec  = t / 1000000,
                           .tv_nsec = (t % 1000000) * 1000 };
    pthread_cond_timedwait(&c->cond, &c->mutex, &ts);
}

/* Block until the transfer has been probed, the stream errored, or the open was
 * interrupted. Returns 0, or a negative AVERROR. */
static int wait_for_probe(CurlContext *c)
{
    URLContext *h = c->h;
    int ret = 0;

    pthread_mutex_lock(&c->mutex);
    while (!c->probed && !c->status) {
        if (ff_check_interrupt(&h->interrupt_callback)) {
            c->aborted = 1;
            ret = AVERROR_EXIT;
            break;
        }
        curl_cond_wait(c);
    }
    if (!ret) {
        if (!c->stream_ok)
            ret = c->status ? c->status : AVERROR(EIO);
    }
    pthread_mutex_unlock(&c->mutex);

    return ret;
}

static int libcurl_open(URLContext *h, const char *url, int flags,
                        AVDictionary **options)
{
    /* Guard against non-thread-safe libcurl builds. This should never happen,
     * since libcurl is used only on platforms with thread support, and thread
     * safety is enabled unconditionally in libcurl when the platform supports
     * threads or atomics. */
    curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    if (!(info->features & CURL_VERSION_THREADSAFE))
        return AVERROR(ENOSYS);

    CurlContext *c = h->priv_data;
    const char *eff_url = h->filename;
    int ret;

    c->h = h;
    c->content_size = -1;
    c->request_start = c->off;
    c->request_end   = -1;
    c->logical_pos   = c->off;
    c->is_initial    = 1;

    /* Report the request URL until header_callback replaces it post-redirect. */
    av_strstart(eff_url, "libcurl:", &eff_url);
    av_freep(&c->location);
    c->location = av_strdup(eff_url);

    ret = pthread_mutex_init(&c->mutex, NULL);
    if (ret)
        return AVERROR(ret);
    ret = pthread_cond_init(&c->cond, NULL);
    if (ret) {
        pthread_mutex_destroy(&c->mutex);
        return AVERROR(ret);
    }

    c->fifo = av_fifo_alloc2(c->buffer_size, 1, 0);
    if (!c->fifo) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = curl_loop_attach(c, h->avfc);
    if (ret < 0)
        goto fail;

    c->easy = curl_easy_init();
    if (!c->easy) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = setup_protocols(c);
    if (ret < 0)
        goto fail;

    setup_curl(c);

    ret = curl_dispatch(c->loop, CMD_ADD, c, 0, 0);
    if (ret < 0)
        goto fail;

    ret = wait_for_probe(c);
    if (ret < 0)
        goto fail;

    pthread_mutex_lock(&c->mutex);
    h->is_streamed = !c->seekable;
    pthread_mutex_unlock(&c->mutex);

    return 0;

fail:
    libcurl_close(h);
    return ret;
}

static int libcurl_read(URLContext *h, unsigned char *buf, int size)
{
    CurlContext *c = h->priv_data;
    int nonblock = h->flags & AVIO_FLAG_NONBLOCK;
    int ret;

    pthread_mutex_lock(&c->mutex);
    while (1) {
        size_t avail = av_fifo_can_read(c->fifo);

        if (avail) {
            int n = FFMIN(avail, (size_t)size);
            int unpause;
            av_fifo_read(c->fifo, buf, n);
            /* Resume a paused transfer once the FIFO is at least half empty. */
            unpause = c->paused && av_fifo_can_write(c->fifo) * 2 >= c->buffer_size;
            c->logical_pos += n;
            pthread_mutex_unlock(&c->mutex);
            if (unpause)
                curl_dispatch(c->loop, CMD_UNPAUSE, c, 0, 0);
            return n;
        }
        if (c->status) {
            ret = c->status;
            break;
        }
        if (nonblock) {
            ret = AVERROR(EAGAIN);
            break;
        }
        curl_cond_wait(c);
        /* Return to the avio layer so it can poll the interrupt callback. */
        nonblock = 1;
    }
    pthread_mutex_unlock(&c->mutex);

    return ret;
}

static int64_t libcurl_seek(URLContext *h, int64_t pos, int whence)
{
    CurlContext *c = h->priv_data;
    int64_t newpos;

    pthread_mutex_lock(&c->mutex);
    const int64_t content_size = c->content_size;
    const int seekable = c->seekable;
    pthread_mutex_unlock(&c->mutex);

    if (whence == AVSEEK_SIZE)
        return content_size >= 0 ? content_size : AVERROR(ENOSYS);

    if (!seekable)
        return AVERROR(ENOSYS);

    switch (whence) {
    case SEEK_SET:
        newpos = pos;
        break;
    case SEEK_CUR:
        if (pos > INT64_MAX - c->logical_pos)
            return AVERROR(ERANGE);
        newpos = c->logical_pos + pos;
        break;
    case SEEK_END:
        if (content_size < 0)
            return AVERROR(ENOSYS);
        if (pos > INT64_MAX - content_size)
            return AVERROR(ERANGE);
        newpos = content_size + pos;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (newpos < 0)
        return AVERROR(EINVAL);

    if (newpos == c->logical_pos)
        return newpos;

    /* Restart the transfer at the new offset. Any failure of the new request
     * surfaces on the following url_read(). */
    curl_dispatch(c->loop, CMD_SEEK, c, newpos, 1);
    c->logical_pos = newpos;

    return newpos;
}

static int libcurl_close(URLContext *h)
{
    CurlContext *c = h->priv_data;

    if (c->loop) {
        if (c->easy) {
            /* Ensure the handle is out of the multi before we free it. */
            curl_dispatch(c->loop, CMD_REMOVE, c, 0, 1);
            curl_easy_cleanup(c->easy);
            c->easy = NULL;
        }
        /* A shared loop outlives the transfer for connection reuse. */
        if (c->private_loop)
            curl_loop_destroy(c->loop);
        c->loop = NULL;
    }

    if (c->header_list)
        curl_slist_free_all(c->header_list);
    av_fifo_freep2(&c->fifo);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->mutex);

    return 0;
}

static int libcurl_get_short_seek(URLContext *h)
{
    CurlContext *c = h->priv_data;
    if (c->short_seek_size >= 1)
        return FFMIN(c->short_seek_size, INT_MAX);
    return AVERROR(ENOSYS);
}

#define OFFSET(x) offsetof(CurlContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, { .str = DEFAULT_USER_AGENT }, 0, 0, D },
    { "referer", "override Referer header", OFFSET(referer), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D },
    { "headers", "set custom HTTP headers, can override built in default headers", OFFSET(headers), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "http_proxy", "set HTTP proxy to tunnel through", OFFSET(http_proxy), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "cookies", "set cookies to be sent in applicable future requests, use newline delimited Set-Cookie HTTP field value syntax", OFFSET(cookies), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D },
    { "location", "the actual location of the data received", OFFSET(location), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "offset", "initial byte offset", OFFSET(off), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { "end_offset", "try to limit the request to bytes preceding this offset", OFFSET(end_off), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { "seekable", "control seekability of connection", OFFSET(seekable_opt), AV_OPT_TYPE_BOOL, { .i64 = -1 }, -1, 1, D },
    { "tls_verify", "verify the peer certificate and hostname", OFFSET(tls_verify), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, D | E },
    { "ca_file", "certificate authority bundle file", OFFSET(ca_file), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "cert_file", "client certificate file", OFFSET(cert_file), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "key_file", "client private key file", OFFSET(key_file), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D | E },
    { "connect_timeout", "connection timeout in seconds (0 = libcurl default)", OFFSET(connect_timeout), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX / 1000, D | E },
    { "max_redirects", "maximum number of redirects to follow", OFFSET(max_redirects), AV_OPT_TYPE_INT, { .i64 = 16 }, 0, INT_MAX, D },
    { "multiple_requests", "reuse the connection across requests (HTTP keep-alive)", OFFSET(multiple_requests), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, D | E },
    { "max_retries", "maximum number of retries after a recoverable error", OFFSET(max_retries), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, D },
    { "buffer_size", "receive buffer size in bytes", OFFSET(buffer_size), AV_OPT_TYPE_INT64, { .i64 = CURL_DEFAULT_BUFFER_SIZE }, CURL_MAX_WRITE_SIZE, INT_MAX, D },
    { "request_size", "split a transfer into ranged requests of at most this many bytes (0 = unlimited)", OFFSET(request_size), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { "initial_request_size", "size (in bytes) of initial requests made during probing / header parsing", OFFSET(initial_request_size), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { "http_version", "HTTP version to use", OFFSET(http_version), AV_OPT_TYPE_INT, { .i64 = CURL_HTTP_VERSION_NONE }, 0, INT_MAX, D, .unit = "http_version" },
        { "auto",              "negotiate the best supported version",  0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_NONE },                0, 0, D, .unit = "http_version" },
        { "1.0",               "HTTP/1.0",                              0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_1_0 },                 0, 0, D, .unit = "http_version" },
        { "1.1",               "HTTP/1.1",                              0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_1_1 },                 0, 0, D, .unit = "http_version" },
        { "2",                 "HTTP/2",                                0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_2 },                   0, 0, D, .unit = "http_version" },
        { "2tls",              "HTTP/2 over TLS only",                  0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_2TLS },                0, 0, D, .unit = "http_version" },
        { "2-prior-knowledge", "HTTP/2 without an upgrade handshake",   0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE },   0, 0, D, .unit = "http_version" },
        { "3",                 "HTTP/3, fall back to earlier versions", 0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_3 },                   0, 0, D, .unit = "http_version" },
        { "3only",             "HTTP/3 only",                           0, AV_OPT_TYPE_CONST, { .i64 = CURL_HTTP_VERSION_3ONLY },               0, 0, D, .unit = "http_version" },
    { "short_seek_size", "threshold to favor readahead over seek", OFFSET(short_seek_size), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, D },
    { NULL }
};

static const AVClass libcurl_context_class = {
    .class_name = "libcurl",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libcurl_protocol = {
    .name            = "libcurl",
    .url_open2       = libcurl_open,
    .url_read        = libcurl_read,
    .url_seek        = libcurl_seek,
    .url_close       = libcurl_close,
    .url_get_short_seek = libcurl_get_short_seek,
    .priv_data_size  = sizeof(CurlContext),
    .priv_data_class = &libcurl_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
    .default_whitelist = "http,https,libcurl",
};
