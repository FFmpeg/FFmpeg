/*
 * FIFO pseudo-muxer
 * Copyright (c) 2016 Jan Sebechlebsky
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavformat/mux.h"
#include "libavformat/network.h"
#include "libavformat/url.h"

/*
 * Include fifo.c directly to override libavformat/fifo.c and
 * thereby prevent libavformat/fifo.o from being pulled in when linking.
 * This relies on libavformat always being linked statically to its
 * test tools (like this one).
 * Due to FIFO_TEST, our fifo muxer will include special handling
 * for tests, i.e. it allows to select the fifo_test muxer below
 * even though it is not accessible via the API.
 */
#define FIFO_TEST
#include "libavformat/fifo.c"

#define MAX_TST_PACKETS 128
#define SLEEPTIME_50_MS 50000
#define SLEEPTIME_10_MS 10000

/* This is structure of data sent in packets to
 * failing muxer */
typedef struct FailingMuxerPacketData {
    int ret;             /* return value of write_packet call*/
    int recover_after;   /* set ret to zero after this number of recovery attempts */
    unsigned sleep_time; /* sleep for this long in write_packet to simulate long I/O operation */
} FailingMuxerPacketData;

typedef struct FifoTestMuxerContext {
    AVClass *class;
    int write_header_ret;
    int write_trailer_ret;
    /* If non-zero, summary of processed packets will be printed in deinit */
    int print_deinit_summary;

    int flush_count;
    int pts_written[MAX_TST_PACKETS];
    int pts_written_nr;
} FifoTestMuxerContext;

static int fifo_test_header(AVFormatContext *avf)
{
    FifoTestMuxerContext *ctx = avf->priv_data;
    return ctx->write_header_ret;
}

static int fifo_test_packet(AVFormatContext *avf, AVPacket *pkt)
{
    FifoTestMuxerContext *ctx = avf->priv_data;
    int ret = 0;
    if (!pkt) {
        ctx->flush_count++;
    } else {
        FailingMuxerPacketData *data = (FailingMuxerPacketData*) pkt->data;

        if (!data->recover_after) {
            data->ret = 0;
        } else {
            data->recover_after--;
        }

        ret = data->ret;

        if (data->sleep_time) {
            int64_t slept = 0;
            while (slept < data->sleep_time) {
                if (ff_check_interrupt(&avf->interrupt_callback))
                    return AVERROR_EXIT;
                av_usleep(SLEEPTIME_10_MS);
                slept += SLEEPTIME_10_MS;
            }
        }

        if (!ret) {
            ctx->pts_written[ctx->pts_written_nr++] = pkt->pts;
            av_packet_unref(pkt);
        }
    }
    return ret;
}

static int fifo_test_trailer(AVFormatContext *avf)
{
    FifoTestMuxerContext *ctx = avf->priv_data;
    return ctx->write_trailer_ret;
}

static void failing_deinit(AVFormatContext *avf)
{
    int i;
    FifoTestMuxerContext *ctx = avf->priv_data;

    if (!ctx->print_deinit_summary)
        return;

    printf("flush count: %d\n", ctx->flush_count);
    printf("pts seen nr: %d\n", ctx->pts_written_nr);
    printf("pts seen: ");
    for (i = 0; i < ctx->pts_written_nr; ++i ) {
        printf(i ? ",%d" : "%d", ctx->pts_written[i]);
    }
    printf("\n");
}

#define OFF(x) offsetof(FifoTestMuxerContext, x)
static const AVOption fifo_test_options[] = {
        {"write_header_ret", "write_header() return value", OFF(write_header_ret),
         AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
        {"write_trailer_ret", "write_trailer() return value", OFF(write_trailer_ret),
         AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
        {"print_deinit_summary", "print summary when deinitializing muxer", OFF(print_deinit_summary),
         AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
        {NULL}
    };

static const AVClass failing_muxer_class = {
    .class_name = "Fifo test muxer",
    .item_name  = av_default_item_name,
    .option     = fifo_test_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_fifo_test_muxer = {
    .p.name         = "fifo_test",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Fifo test muxer"),
    .priv_data_size = sizeof(FifoTestMuxerContext),
    .write_header   = fifo_test_header,
    .write_packet   = fifo_test_packet,
    .write_trailer  = fifo_test_trailer,
    .deinit         = failing_deinit,
    .p.priv_class   = &failing_muxer_class,
#if FF_API_ALLOW_FLUSH
    .p.flags        = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
#else
    .p.flags        = AVFMT_NOFILE,
#endif
    .flags_internal = FF_OFMT_FLAG_ALLOW_FLUSH,
};


static int prepare_packet(AVPacket *pkt, const FailingMuxerPacketData *pkt_data, int64_t pts)
{
    int ret = av_new_packet(pkt, sizeof(*pkt_data));
    if (ret < 0)
        return ret;
    memcpy(pkt->data, pkt_data, sizeof(*pkt_data));

    pkt->pts = pkt->dts = pts;
    pkt->duration = 1;

    return 0;
}

static int initialize_fifo_tst_muxer_chain(AVFormatContext **oc, AVPacket **pkt)
{
    int ret = 0;
    AVStream *s;

    ret = avformat_alloc_output_context2(oc, NULL, "fifo", "-");
    if (ret) {
        fprintf(stderr, "Failed to create format context: %s\n",
                av_err2str(ret));
        return EXIT_FAILURE;
    }

    s = avformat_new_stream(*oc, NULL);
    if (!s) {
        fprintf(stderr, "Failed to create stream: %s\n",
                av_err2str(ret));
        return AVERROR(ENOMEM);
    }

    *pkt = av_packet_alloc();
    if (!*pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static int fifo_basic_test(AVFormatContext *oc, AVDictionary **opts,
                           AVPacket *pkt, const FailingMuxerPacketData *pkt_data)
{
    int ret = 0, i;

    ret = avformat_write_header(oc, opts);
    if (ret) {
        fprintf(stderr, "Unexpected write_header failure: %s\n",
                av_err2str(ret));
        goto fail;
    }

    for (i = 0; i < 15; i++ ) {
        ret = prepare_packet(pkt, pkt_data, i);
        if (ret < 0) {
            fprintf(stderr, "Failed to prepare test packet: %s\n",
                    av_err2str(ret));
            goto write_trailer_and_fail;
        }
        ret = av_write_frame(oc, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            fprintf(stderr, "Unexpected write_frame error: %s\n",
                    av_err2str(ret));
            goto write_trailer_and_fail;
        }
    }

    ret = av_write_frame(oc, NULL);
    if (ret < 0) {
        fprintf(stderr, "Unexpected write_frame error during flushing: %s\n",
                av_err2str(ret));
        goto write_trailer_and_fail;
    }

    ret = av_write_trailer(oc);
    if (ret < 0) {
        fprintf(stderr, "Unexpected write_trailer error during flushing: %s\n",
                av_err2str(ret));
        goto fail;
    }

    return ret;
write_trailer_and_fail:
    av_write_trailer(oc);
fail:
    return ret;
}

static int fifo_overflow_drop_test(AVFormatContext *oc, AVDictionary **opts,
                                   AVPacket *pkt, const FailingMuxerPacketData *data)
{
    int ret = 0, i;
    int64_t write_pkt_start, write_pkt_end, duration;

    ret = avformat_write_header(oc, opts);
    if (ret) {
        fprintf(stderr, "Unexpected write_header failure: %s\n",
                av_err2str(ret));
        return ret;
    }

    write_pkt_start = av_gettime_relative();
    for (i = 0; i < 6; i++ ) {
        ret = prepare_packet(pkt, data, i);
        if (ret < 0) {
            fprintf(stderr, "Failed to prepare test packet: %s\n",
                    av_err2str(ret));
            goto fail;
        }
        ret = av_write_frame(oc, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            break;
        }
    }

    write_pkt_end = av_gettime_relative();
    duration = write_pkt_end - write_pkt_start;
    if (duration > (SLEEPTIME_50_MS*6)/2) {
        fprintf(stderr, "Writing packets to fifo muxer took too much time while testing"
                        "buffer overflow with drop_pkts_on_overflow was on.\n");
        ret = AVERROR_BUG;
        goto fail;
    }

    if (ret) {
        fprintf(stderr, "Unexpected write_packet error: %s\n", av_err2str(ret));
        goto fail;
    }

    ret = av_write_trailer(oc);
    if (ret < 0)
        fprintf(stderr, "Unexpected write_trailer error: %s\n", av_err2str(ret));

    return ret;
fail:
    av_write_trailer(oc);
    return ret;
}

typedef struct TestCase {
    int (*test_func)(AVFormatContext *, AVDictionary **,
                     AVPacket *, const FailingMuxerPacketData *pkt_data);
    const char *test_name;
    const char *options;

    uint8_t print_summary_on_deinit;
    int write_header_ret;
    int write_trailer_ret;

    FailingMuxerPacketData pkt_data;
} TestCase;


#define BUFFER_SIZE 64

static int run_test(const TestCase *test)
{
    AVDictionary *opts = NULL;
    AVFormatContext *oc = NULL;
    AVPacket *pkt = NULL;
    char buffer[BUFFER_SIZE];
    int ret, ret1;

    ret = initialize_fifo_tst_muxer_chain(&oc, &pkt);
    if (ret < 0) {
        fprintf(stderr, "Muxer initialization failed: %s\n", av_err2str(ret));
        goto end;
    }

    if (test->options) {
        ret = av_dict_parse_string(&opts, test->options, "=", ":", 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to parse options: %s\n", av_err2str(ret));
            goto end;
        }
    }

    snprintf(buffer, BUFFER_SIZE,
             "print_deinit_summary=%d:write_header_ret=%d:write_trailer_ret=%d",
             (int)test->print_summary_on_deinit, test->write_header_ret,
             test->write_trailer_ret);
    ret = av_dict_set(&opts, "format_opts", buffer, 0);
    ret1 = av_dict_set(&opts, "fifo_format", "fifo_test", 0);
    if (ret < 0 || ret1 < 0) {
        fprintf(stderr, "Failed to set options for test muxer: %s\n",
                av_err2str(ret));
        goto end;
    }

    ret = test->test_func(oc, &opts, pkt, &test->pkt_data);

end:
    printf("%s: %s\n", test->test_name, ret < 0 ? "fail" : "ok");
    avformat_free_context(oc);
    av_packet_free(&pkt);
    av_dict_free(&opts);
    return ret;
}


const TestCase tests[] = {
        /* Simple test in packet-non-dropping mode, we expect to get on the output
         * exactly what was on input */
        {fifo_basic_test, "nonfail test", NULL,1, 0, 0, {0, 0, 0}},

        /* Each write_packet will fail 3 times before operation is successful. If recovery
         * Since recovery is on, fifo muxer should not return any errors. */
        {fifo_basic_test, "recovery test", "attempt_recovery=1:recovery_wait_time=0",
         0, 0, 0, {AVERROR(ETIMEDOUT), 3, 0}},

        /* By setting low queue_size and sending packets with longer processing time,
         * this test will cause queue to overflow, since drop_pkts_on_overflow is off
         * by default, all packets should be processed and fifo should block on full
         * queue. */
        {fifo_basic_test, "overflow without packet dropping","queue_size=3",
         1, 0, 0, {0, 0, SLEEPTIME_10_MS}},

        /* The test as the upper one, except that drop_on_overflow is turned on. In this case
         * fifo should not block when the queue is full and slow down producer, so the test
         * measures time producer spends on write_packet calls which should be significantly
         * less than number_of_pkts * 50 MS.
         */
        {fifo_overflow_drop_test, "overflow with packet dropping", "queue_size=3:drop_pkts_on_overflow=1",
         0, 0, 0, {0, 0, SLEEPTIME_50_MS}},

        {NULL}
};

int main(int argc, char *argv[])
{
    int i, ret, ret_all = 0;

    for (i = 0; tests[i].test_func; i++) {
        ret = run_test(&tests[i]);
        if (!ret_all && ret < 0)
            ret_all = ret;
    }

    return ret;
}
