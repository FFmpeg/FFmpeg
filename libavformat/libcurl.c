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
#include <stdlib.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"

#include "avformat.h"
#include "http.h"
#include "internal.h"
#include "url.h"

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
    pthread_t       thread;
    CURLM          *multi;

    pthread_mutex_t mutex;   /* guards the command queue, exit and cmd->done */
    pthread_cond_t  cond;    /* signaled when a sync command completes */
    CurlCmd        *cmd_head, *cmd_tail;
    int             exit;
} CurlLoop;

struct CurlContext {
    const AVClass  *class;
    URLContext     *h;

    CurlLoop       *loop;
    int             private_loop;  /* loop is owned by this context (not shared) */
    CURL           *easy;

    int64_t         buffer_size;
    int             max_retries;

    int64_t         logical_pos; /* next byte url_read() will return, caller side */

    /* Producer bookkeeping, touched only by the loop thread. */
    int             active;          /* currently added to the multi */
    uint64_t        request_start;   /* absolute offset the current request began at */
    uint64_t        request_received;/* bytes delivered in the current request */
    int             retry_count;     /* consecutive recoverable failures */

    /* Per-response-block header scratch, loop thread only. */
    int             hdr_accept_ranges;
    int             hdr_compressed;
    int64_t         hdr_content_total;

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
    int             eof;         /* producer delivered all data */
    int             error;       /* AVERROR for an unrecoverable failure, or 0 */
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

/* Parse the total length out of a "Content-Range: bytes a-b/total" value.
 * Returns the total, or -1 if unknown ("*") or unparsable. */
static int64_t parse_content_range_total(const char *v)
{
    const char *slash = strchr(v, '/');
    if (!slash || slash[1] == '*')
        return -1;
    return strtoll(slash + 1, NULL, 10);
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
        c->hdr_content_total = parse_content_range_total(ptr + 14);
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
        c->stream_ok = 1;
        /* A compressed body is addressed in encoded form, so byte offsets are
         * meaningless: not seekable. Note that we prefer compression over
         * seekability, servers doesn't offer media in compressed form, so it
         * gives us free compression for other payloads like text playlist. */
        c->seekable = !c->hdr_compressed &&
                      (status == 206 || c->hdr_accept_ranges);
        if (c->seekable) {
            int64_t total = c->hdr_content_total;
            if (total < 0 && status != 206) {
                curl_off_t cl = -1;
                if (curl_easy_getinfo(c->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                                      &cl) == CURLE_OK && cl >= 0)
                    total = cl;
            }
            c->content_size = total;
        }
    } else {
        c->stream_ok = 0;
        if (!c->error)
            c->error = ff_http_averror(status, AVERROR(EIO));
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
        char range[32];
        snprintf(range, sizeof(range), "%"PRIu64"-", c->request_start);
        curl_easy_setopt(c->easy, CURLOPT_RANGE, range);
    } else {
        curl_easy_setopt(c->easy, CURLOPT_RANGE, NULL);
    }
    c->request_received = 0;
    c->active = 1;
    CURLMcode res = curl_multi_add_handle(c->loop->multi, c->easy);
    if (res != CURLM_OK) {
        av_log(c->h, AV_LOG_ERROR, "curl_multi_add_handle: %s\n",
               curl_multi_strerror(res));
        c->active = 0;
        pthread_mutex_lock(&c->mutex);
        if (!c->error)
            c->error = AVERROR(EIO);
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
    }
}

/* Transfer finished (or failed) */
static void on_done(CurlContext *c, CURLcode code)
{
    int aborted;

    pthread_mutex_lock(&c->mutex);
    aborted = c->aborted;
    /* Advance past delivered bytes so a retry or seek resumes at the right offset. */
    c->request_start    += c->request_received;
    c->request_received  = 0;
    pthread_mutex_unlock(&c->mutex);

    if (!c->probed) {
        /* Connection died before any usable header arrived. */
        pthread_mutex_lock(&c->mutex);
        c->probed    = 1;
        c->stream_ok = 0;
        if (!c->error)
            c->error = curlcode_to_averror(code);
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    if (code == CURLE_OK && !aborted && c->stream_ok) {
        pthread_mutex_lock(&c->mutex);
        c->eof = 1;
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
        return;
    }

    /* Resume seekable transfers after a recoverable error. */
    if (!aborted && c->seekable && is_recoverable(code) &&
        c->retry_count < c->max_retries) {
        c->retry_count++;
        av_log(c->h, AV_LOG_WARNING, "%s, retrying (#%d) from %"PRIu64"\n",
               curl_easy_strerror(code), c->retry_count, c->request_start);
        start_request(c);
        return;
    }

    if (!aborted) {
        pthread_mutex_lock(&c->mutex);
        if (!c->error)
            c->error = curlcode_to_averror(code);
        pthread_cond_broadcast(&c->cond);
        pthread_mutex_unlock(&c->mutex);
    }
}

/* ------------------------------------------------------------------------- */
/* event loop thread + command queue                                         */
/* ------------------------------------------------------------------------- */

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
            c->active = 0;
        }
        break;
    case CMD_UNPAUSE:
        curl_easy_pause(c->easy, CURLPAUSE_CONT);
        break;
    case CMD_SEEK:
        if (c->active) {
            curl_multi_remove_handle(loop->multi, c->easy);
            c->active = 0;
        }
        pthread_mutex_lock(&c->mutex);
        av_fifo_reset2(c->fifo);
        c->paused = 0;
        c->eof    = 0;
        c->error  = 0;
        pthread_mutex_unlock(&c->mutex);
        c->request_start = cmd->pos;
        c->retry_count   = 0;
        start_request(c);
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

static CurlLoop *curl_loop_create(void)
{
    CurlLoop *loop = av_mallocz(sizeof(*loop));
    if (!loop)
        return NULL;

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

    if (pthread_create(&loop->thread, NULL, curl_worker, loop)) {
        curl_multi_cleanup(loop->multi);
        goto fail3;
    }

    return loop;

fail3:
    curl_global_cleanup();
fail2:
    pthread_cond_destroy(&loop->cond);
    pthread_mutex_destroy(&loop->mutex);
fail:
    av_free(loop);
    return NULL;
}

static void curl_loop_destroy(CurlLoop *loop)
{
    pthread_mutex_lock(&loop->mutex);
    loop->exit = 1;
    curl_multi_wakeup(loop->multi);
    pthread_mutex_unlock(&loop->mutex);

    pthread_join(loop->thread, NULL);

    curl_multi_cleanup(loop->multi);
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
        c->loop = curl_loop_create();
        c->private_loop = 1;
        return c->loop ? 0 : AVERROR(ENOMEM);
    }

    pthread_mutex_lock(&curl_loop_lock);
    c->loop = ffformatcontext(avfc)->curl_loop;
    if (!c->loop) {
        c->loop = curl_loop_create();
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

static void setup_curl(CurlContext *c)
{
    CURL *e = c->easy;
    const char *url = c->h->filename;

    /* Drop an optional "libcurl:" prefix that forces this protocol. */
    av_strstart(url, "libcurl:", &url);

    curl_easy_setopt(e, CURLOPT_URL, url);
    curl_easy_setopt(e, CURLOPT_PRIVATE, c);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, c);
    curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(e, CURLOPT_HEADERDATA, c);

    curl_easy_setopt(e, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(e, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(e, CURLOPT_XFERINFODATA, c);

    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");
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
    while (!c->probed && !c->error) {
        if (ff_check_interrupt(&h->interrupt_callback)) {
            c->aborted = 1;
            ret = AVERROR_EXIT;
            break;
        }
        curl_cond_wait(c);
    }
    if (!ret) {
        if (!c->stream_ok)
            ret = c->error ? c->error : AVERROR(EIO);
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
    int ret;

    c->h = h;
    c->content_size = -1;
    c->max_retries  = 5;
    if (c->buffer_size <= 0)
        c->buffer_size = CURL_DEFAULT_BUFFER_SIZE;

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
    setup_curl(c);

    ret = curl_dispatch(c->loop, CMD_ADD, c, 0, 0);
    if (ret < 0)
        goto fail;

    ret = wait_for_probe(c);
    if (ret < 0)
        goto fail;

    h->is_streamed = !c->seekable;

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
        if (c->error) {
            ret = c->error;
            break;
        }
        if (c->eof) {
            ret = AVERROR_EOF;
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

    if (whence == AVSEEK_SIZE)
        return c->content_size >= 0 ? c->content_size : AVERROR(ENOSYS);

    if (!c->seekable)
        return AVERROR(ENOSYS);

    switch (whence) {
    case SEEK_SET:
        newpos = pos;
        break;
    case SEEK_CUR:
        newpos = c->logical_pos + pos;
        break;
    case SEEK_END:
        if (c->content_size < 0)
            return AVERROR(ENOSYS);
        newpos = c->content_size + pos;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (newpos < 0)
        return AVERROR(EINVAL);

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

    av_fifo_freep2(&c->fifo);
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->mutex);

    return 0;
}

static const AVClass libcurl_context_class = {
    .class_name = "libcurl",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_libcurl_protocol = {
    .name            = "libcurl",
    .url_open2       = libcurl_open,
    .url_read        = libcurl_read,
    .url_seek        = libcurl_seek,
    .url_close       = libcurl_close,
    .priv_data_size  = sizeof(CurlContext),
    .priv_data_class = &libcurl_context_class,
    .flags           = URL_PROTOCOL_FLAG_NETWORK,
};
