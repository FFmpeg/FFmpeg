/*
 * Input async protocol.
 * Copyright (c) 2015 Zhang Rui <bbcallen@gmail.com>
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
 *
 * Based on libavformat/cache.c by Michael Niedermayer
 */

 /**
 * @TODO
 *      support timeout
 *      support work with concatdec, hls
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "url.h"
#include <stdint.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#define BUFFER_CAPACITY         (4 * 1024 * 1024)
#define READ_BACK_CAPACITY      (4 * 1024 * 1024)
#define SHORT_SEEK_THRESHOLD    (256 * 1024)

typedef struct RingBuffer
{
    AVFifo       *fifo;
    int           read_back_capacity;

    int           read_pos;
} RingBuffer;

typedef struct Context {
    AVClass        *class;
    URLContext     *inner;

    int             seek_request;
    int64_t         seek_pos;
    int             seek_whence;
    int             seek_completed;
    int64_t         seek_ret;

    int             inner_io_error;
    int             io_error;
    int             io_eof_reached;

    int64_t         logical_pos;
    int64_t         logical_size;
    RingBuffer      ring;

    pthread_cond_t  cond_wakeup_main;
    pthread_cond_t  cond_wakeup_background;
    pthread_mutex_t mutex;
    pthread_t       async_buffer_thread;

    int             abort_request;
    AVIOInterruptCB interrupt_callback;
} Context;

static int ring_init(RingBuffer *ring, unsigned int capacity, int read_back_capacity)
{
    memset(ring, 0, sizeof(RingBuffer));
    ring->fifo = av_fifo_alloc2(capacity + read_back_capacity, 1, 0);
    if (!ring->fifo)
        return AVERROR(ENOMEM);

    ring->read_back_capacity = read_back_capacity;
    return 0;
}

static void ring_destroy(RingBuffer *ring)
{
    av_fifo_freep2(&ring->fifo);
}

static void ring_reset(RingBuffer *ring)
{
    av_fifo_reset2(ring->fifo);
    ring->read_pos = 0;
}

static int ring_size(RingBuffer *ring)
{
    return av_fifo_can_read(ring->fifo) - ring->read_pos;
}

static int ring_space(RingBuffer *ring)
{
    return av_fifo_can_write(ring->fifo);
}

static int ring_read(RingBuffer *ring, void *dest, int buf_size)
{
    int ret = 0;

    av_assert2(buf_size <= ring_size(ring));
    if (dest)
        ret = av_fifo_peek(ring->fifo, dest, buf_size, ring->read_pos);
    ring->read_pos += buf_size;

    if (ring->read_pos > ring->read_back_capacity) {
        av_fifo_drain2(ring->fifo, ring->read_pos - ring->read_back_capacity);
        ring->read_pos = ring->read_back_capacity;
    }

    return ret;
}

static int wrapped_url_read(void *src, void *dst, size_t *size)
{
    URLContext *h   = src;
    Context    *c   = h->priv_data;
    int         ret;

    ret = ffurl_read(c->inner, dst, *size);
    *size             = ret > 0 ? ret : 0;
    c->inner_io_error = ret < 0 ? ret : 0;

    return c->inner_io_error;
}

static int ring_write(RingBuffer *ring, URLContext *h, size_t size)
{
    av_assert2(size <= ring_space(ring));
    return av_fifo_write_from_cb(ring->fifo, wrapped_url_read, h, &size);
}

static int ring_size_of_read_back(RingBuffer *ring)
{
    return ring->read_pos;
}

static int ring_drain(RingBuffer *ring, int offset)
{
    av_assert2(offset >= -ring_size_of_read_back(ring));
    av_assert2(offset <= ring_size(ring));
    ring->read_pos += offset;
    return 0;
}

static int async_check_interrupt(void *arg)
{
    URLContext *h   = arg;
    Context    *c   = h->priv_data;

    if (c->abort_request)
        return 1;

    if (ff_check_interrupt(&c->interrupt_callback))
        c->abort_request = 1;

    return c->abort_request;
}

static void *async_buffer_task(void *arg)
{
    URLContext   *h    = arg;
    Context      *c    = h->priv_data;
    RingBuffer   *ring = &c->ring;
    int           ret  = 0;
    int64_t       seek_ret;

    while (1) {
        int fifo_space, to_copy;

        pthread_mutex_lock(&c->mutex);
        if (async_check_interrupt(h)) {
            c->io_eof_reached = 1;
            c->io_error       = AVERROR_EXIT;
            pthread_cond_signal(&c->cond_wakeup_main);
            pthread_mutex_unlock(&c->mutex);
            break;
        }

        if (c->seek_request) {
            seek_ret = ffurl_seek(c->inner, c->seek_pos, c->seek_whence);
            if (seek_ret >= 0) {
                c->io_eof_reached = 0;
                c->io_error       = 0;
                ring_reset(ring);
            }

            c->seek_completed = 1;
            c->seek_ret       = seek_ret;
            c->seek_request   = 0;


            pthread_cond_signal(&c->cond_wakeup_main);
            pthread_mutex_unlock(&c->mutex);
            continue;
        }

        fifo_space = ring_space(ring);
        if (c->io_eof_reached || fifo_space <= 0) {
            pthread_cond_signal(&c->cond_wakeup_main);
            pthread_cond_wait(&c->cond_wakeup_background, &c->mutex);
            pthread_mutex_unlock(&c->mutex);
            continue;
        }
        pthread_mutex_unlock(&c->mutex);

        to_copy = FFMIN(4096, fifo_space);
        ret = ring_write(ring, h, to_copy);

        pthread_mutex_lock(&c->mutex);
        if (ret <= 0) {
            c->io_eof_reached = 1;
            if (c->inner_io_error < 0)
                c->io_error = c->inner_io_error;
        }

        pthread_cond_signal(&c->cond_wakeup_main);
        pthread_mutex_unlock(&c->mutex);
    }

    return NULL;
}

static int async_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    Context         *c = h->priv_data;
    int              ret;
    AVIOInterruptCB  interrupt_callback = {.callback = async_check_interrupt, .opaque = h};

    av_strstart(arg, "async:", &arg);

    ret = ring_init(&c->ring, BUFFER_CAPACITY, READ_BACK_CAPACITY);
    if (ret < 0)
        goto fifo_fail;

    /* wrap interrupt callback */
    c->interrupt_callback = h->interrupt_callback;
    ret = ffurl_open_whitelist(&c->inner, arg, flags, &interrupt_callback, options, h->protocol_whitelist, h->protocol_blacklist, h);
    if (ret != 0) {
        av_log(h, AV_LOG_ERROR, "ffurl_open failed : %s, %s\n", av_err2str(ret), arg);
        goto url_fail;
    }

    c->logical_size = ffurl_size(c->inner);
    h->is_streamed  = c->inner->is_streamed;

    ret = pthread_mutex_init(&c->mutex, NULL);
    if (ret != 0) {
        ret = AVERROR(ret);
        av_log(h, AV_LOG_ERROR, "pthread_mutex_init failed : %s\n", av_err2str(ret));
        goto mutex_fail;
    }

    ret = pthread_cond_init(&c->cond_wakeup_main, NULL);
    if (ret != 0) {
        ret = AVERROR(ret);
        av_log(h, AV_LOG_ERROR, "pthread_cond_init failed : %s\n", av_err2str(ret));
        goto cond_wakeup_main_fail;
    }

    ret = pthread_cond_init(&c->cond_wakeup_background, NULL);
    if (ret != 0) {
        ret = AVERROR(ret);
        av_log(h, AV_LOG_ERROR, "pthread_cond_init failed : %s\n", av_err2str(ret));
        goto cond_wakeup_background_fail;
    }

    ret = pthread_create(&c->async_buffer_thread, NULL, async_buffer_task, h);
    if (ret) {
        ret = AVERROR(ret);
        av_log(h, AV_LOG_ERROR, "pthread_create failed : %s\n", av_err2str(ret));
        goto thread_fail;
    }

    return 0;

thread_fail:
    pthread_cond_destroy(&c->cond_wakeup_background);
cond_wakeup_background_fail:
    pthread_cond_destroy(&c->cond_wakeup_main);
cond_wakeup_main_fail:
    pthread_mutex_destroy(&c->mutex);
mutex_fail:
    ffurl_closep(&c->inner);
url_fail:
    ring_destroy(&c->ring);
fifo_fail:
    return ret;
}

static int async_close(URLContext *h)
{
    Context *c = h->priv_data;
    int      ret;

    pthread_mutex_lock(&c->mutex);
    c->abort_request = 1;
    pthread_cond_signal(&c->cond_wakeup_background);
    pthread_mutex_unlock(&c->mutex);

    ret = pthread_join(c->async_buffer_thread, NULL);
    if (ret != 0)
        av_log(h, AV_LOG_ERROR, "pthread_join(): %s\n", av_err2str(ret));

    pthread_cond_destroy(&c->cond_wakeup_background);
    pthread_cond_destroy(&c->cond_wakeup_main);
    pthread_mutex_destroy(&c->mutex);
    ffurl_closep(&c->inner);
    ring_destroy(&c->ring);

    return 0;
}

static int async_read_internal(URLContext *h, void *dest, int size)
{
    Context      *c       = h->priv_data;
    RingBuffer   *ring    = &c->ring;
    int     read_complete = !dest;
    int           to_read = size;
    int           ret     = 0;

    pthread_mutex_lock(&c->mutex);

    while (to_read > 0) {
        int fifo_size, to_copy;
        if (async_check_interrupt(h)) {
            ret = AVERROR_EXIT;
            break;
        }
        fifo_size = ring_size(ring);
        to_copy   = FFMIN(to_read, fifo_size);
        if (to_copy > 0) {
            ring_read(ring, dest, to_copy);
            if (dest)
                dest = (uint8_t *)dest + to_copy;
            c->logical_pos += to_copy;
            to_read        -= to_copy;
            ret             = size - to_read;

            if (to_read <= 0 || !read_complete)
                break;
        } else if (c->io_eof_reached) {
            if (ret <= 0) {
                if (c->io_error)
                    ret = c->io_error;
                else
                    ret = AVERROR_EOF;
            }
            break;
        }
        pthread_cond_signal(&c->cond_wakeup_background);
        pthread_cond_wait(&c->cond_wakeup_main, &c->mutex);
    }

    pthread_cond_signal(&c->cond_wakeup_background);
    pthread_mutex_unlock(&c->mutex);

    return ret;
}

static int async_read(URLContext *h, unsigned char *buf, int size)
{
    return async_read_internal(h, buf, size);
}

static int64_t async_seek(URLContext *h, int64_t pos, int whence)
{
    Context      *c    = h->priv_data;
    RingBuffer   *ring = &c->ring;
    int64_t       ret;
    int64_t       new_logical_pos;
    int fifo_size;
    int fifo_size_of_read_back;

    if (whence == AVSEEK_SIZE) {
        av_log(h, AV_LOG_TRACE, "async_seek: AVSEEK_SIZE: %"PRId64"\n", (int64_t)c->logical_size);
        return c->logical_size;
    } else if (whence == SEEK_CUR) {
        av_log(h, AV_LOG_TRACE, "async_seek: %"PRId64"\n", pos);
        new_logical_pos = pos + c->logical_pos;
    } else if (whence == SEEK_SET){
        av_log(h, AV_LOG_TRACE, "async_seek: %"PRId64"\n", pos);
        new_logical_pos = pos;
    } else {
        return AVERROR(EINVAL);
    }
    if (new_logical_pos < 0)
        return AVERROR(EINVAL);

    fifo_size = ring_size(ring);
    fifo_size_of_read_back = ring_size_of_read_back(ring);
    if (new_logical_pos == c->logical_pos) {
        /* current position */
        return c->logical_pos;
    } else if ((new_logical_pos >= (c->logical_pos - fifo_size_of_read_back)) &&
               (new_logical_pos < (c->logical_pos + fifo_size + SHORT_SEEK_THRESHOLD))) {
        int pos_delta = (int)(new_logical_pos - c->logical_pos);
        /* fast seek */
        av_log(h, AV_LOG_TRACE, "async_seek: fask_seek %"PRId64" from %d dist:%d/%d\n",
                new_logical_pos, (int)c->logical_pos,
                (int)(new_logical_pos - c->logical_pos), fifo_size);

        if (pos_delta > 0) {
            // fast seek forwards
            async_read_internal(h, NULL, pos_delta);
        } else {
            // fast seek backwards
            ring_drain(ring, pos_delta);
            c->logical_pos = new_logical_pos;
        }

        return c->logical_pos;
    } else if (c->logical_size <= 0) {
        /* can not seek */
        return AVERROR(EINVAL);
    } else if (new_logical_pos > c->logical_size) {
        /* beyond end */
        return AVERROR(EINVAL);
    }

    pthread_mutex_lock(&c->mutex);

    c->seek_request   = 1;
    c->seek_pos       = new_logical_pos;
    c->seek_whence    = SEEK_SET;
    c->seek_completed = 0;
    c->seek_ret       = 0;

    while (1) {
        if (async_check_interrupt(h)) {
            ret = AVERROR_EXIT;
            break;
        }
        if (c->seek_completed) {
            if (c->seek_ret >= 0)
                c->logical_pos  = c->seek_ret;
            ret = c->seek_ret;
            break;
        }
        pthread_cond_signal(&c->cond_wakeup_background);
        pthread_cond_wait(&c->cond_wakeup_main, &c->mutex);
    }

    pthread_mutex_unlock(&c->mutex);

    return ret;
}

#define OFFSET(x) offsetof(Context, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    {NULL},
};

#undef D
#undef OFFSET

static const AVClass async_context_class = {
    .class_name = "Async",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_async_protocol = {
    .name                = "async",
    .url_open2           = async_open,
    .url_read            = async_read,
    .url_seek            = async_seek,
    .url_close           = async_close,
    .priv_data_size      = sizeof(Context),
    .priv_data_class     = &async_context_class,
};

#if 0

#define TEST_SEEK_POS    (1536)
#define TEST_STREAM_SIZE (2048)

typedef struct TestContext {
    AVClass        *class;
    int64_t         logical_pos;
    int64_t         logical_size;

    /* options */
    int             opt_read_error;
} TestContext;

static int async_test_open(URLContext *h, const char *arg, int flags, AVDictionary **options)
{
    TestContext *c = h->priv_data;
    c->logical_pos  = 0;
    c->logical_size = TEST_STREAM_SIZE;
    return 0;
}

static int async_test_close(URLContext *h)
{
    return 0;
}

static int async_test_read(URLContext *h, unsigned char *buf, int size)
{
    TestContext *c = h->priv_data;
    int          i;
    int          read_len = 0;

    if (c->opt_read_error)
        return c->opt_read_error;

    if (c->logical_pos >= c->logical_size)
        return AVERROR_EOF;

    for (i = 0; i < size; ++i) {
        buf[i] = c->logical_pos & 0xFF;

        c->logical_pos++;
        read_len++;

        if (c->logical_pos >= c->logical_size)
            break;
    }

    return read_len;
}

static int64_t async_test_seek(URLContext *h, int64_t pos, int whence)
{
    TestContext *c = h->priv_data;
    int64_t      new_logical_pos;

    if (whence == AVSEEK_SIZE) {
        return c->logical_size;
    } else if (whence == SEEK_CUR) {
        new_logical_pos = pos + c->logical_pos;
    } else if (whence == SEEK_SET){
        new_logical_pos = pos;
    } else {
        return AVERROR(EINVAL);
    }
    if (new_logical_pos < 0)
        return AVERROR(EINVAL);

    c->logical_pos = new_logical_pos;
    return new_logical_pos;
}

#define OFFSET(x) offsetof(TestContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption async_test_options[] = {
    { "async-test-read-error",      "cause read fail",
        OFFSET(opt_read_error),     AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, .flags = D },
    {NULL},
};

#undef D
#undef OFFSET

static const AVClass async_test_context_class = {
    .class_name = "Async-Test",
    .item_name  = av_default_item_name,
    .option     = async_test_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_async_test_protocol = {
    .name                = "async-test",
    .url_open2           = async_test_open,
    .url_read            = async_test_read,
    .url_seek            = async_test_seek,
    .url_close           = async_test_close,
    .priv_data_size      = sizeof(TestContext),
    .priv_data_class     = &async_test_context_class,
};

int main(void)
{
    URLContext   *h = NULL;
    int           i;
    int           ret;
    int64_t       size;
    int64_t       pos;
    int64_t       read_len;
    unsigned char buf[4096];
    AVDictionary *opts = NULL;

    ffurl_register_protocol(&ff_async_protocol);
    ffurl_register_protocol(&ff_async_test_protocol);

    /*
     * test normal read
     */
    ret = ffurl_open_whitelist(&h, "async:async-test:", AVIO_FLAG_READ,
                               NULL, NULL, NULL, NULL, NULL);
    printf("open: %d\n", ret);

    size = ffurl_size(h);
    printf("size: %"PRId64"\n", size);

    pos = ffurl_seek(h, 0, SEEK_CUR);
    read_len = 0;
    while (1) {
        ret = ffurl_read(h, buf, sizeof(buf));
        if (ret == AVERROR_EOF) {
            printf("read-error: AVERROR_EOF at %"PRId64"\n", ffurl_seek(h, 0, SEEK_CUR));
            break;
        }
        else if (ret == 0)
            break;
        else if (ret < 0) {
            printf("read-error: %d at %"PRId64"\n", ret, ffurl_seek(h, 0, SEEK_CUR));
            goto fail;
        } else {
            for (i = 0; i < ret; ++i) {
                if (buf[i] != (pos & 0xFF)) {
                    printf("read-mismatch: actual %d, expecting %d, at %"PRId64"\n",
                           (int)buf[i], (int)(pos & 0xFF), pos);
                    break;
                }
                pos++;
            }
        }

        read_len += ret;
    }
    printf("read: %"PRId64"\n", read_len);

    /*
     * test normal seek
     */
    ret = ffurl_read(h, buf, 1);
    printf("read: %d\n", ret);

    pos = ffurl_seek(h, TEST_SEEK_POS, SEEK_SET);
    printf("seek: %"PRId64"\n", pos);

    read_len = 0;
    while (1) {
        ret = ffurl_read(h, buf, sizeof(buf));
        if (ret == AVERROR_EOF)
            break;
        else if (ret == 0)
            break;
        else if (ret < 0) {
            printf("read-error: %d at %"PRId64"\n", ret, ffurl_seek(h, 0, SEEK_CUR));
            goto fail;
        } else {
            for (i = 0; i < ret; ++i) {
                if (buf[i] != (pos & 0xFF)) {
                    printf("read-mismatch: actual %d, expecting %d, at %"PRId64"\n",
                           (int)buf[i], (int)(pos & 0xFF), pos);
                    break;
                }
                pos++;
            }
        }

        read_len += ret;
    }
    printf("read: %"PRId64"\n", read_len);

    ret = ffurl_read(h, buf, 1);
    printf("read: %d\n", ret);

    /*
     * test read error
     */
    ffurl_close(h);
    av_dict_set_int(&opts, "async-test-read-error", -10000, 0);
    ret = ffurl_open_whitelist(&h, "async:async-test:", AVIO_FLAG_READ,
                               NULL, &opts, NULL, NULL, NULL);
    printf("open: %d\n", ret);

    ret = ffurl_read(h, buf, 1);
    printf("read: %d\n", ret);

fail:
    av_dict_free(&opts);
    ffurl_close(h);
    return 0;
}

#endif
