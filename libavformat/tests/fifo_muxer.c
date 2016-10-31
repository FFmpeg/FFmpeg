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
#include "libavutil/avassert.h"
#include "libavformat/avformat.h"
#include "libavformat/url.h"

#define MAX_TST_PACKETS 128
#define SLEEPTIME_50_MS 50000
#define SLEEPTIME_10_MS 10000

/* Implementation of mock muxer to simulate real muxer failures */

/* This is structure of data sent in packets to
 * failing muxer */
typedef struct FailingMuxerPacketData {
    int ret;             /* return value of write_packet call*/
    int recover_after;   /* set ret to zero after this number of recovery attempts */
    unsigned sleep_time; /* sleep for this long in write_packet to simulate long I/O operation */
} FailingMuxerPacketData;


typedef struct FailingMuxerContext {
    AVClass *class;
    int write_header_ret;
    int write_trailer_ret;
    /* If non-zero, summary of processed packets will be printed in deinit */
    int print_deinit_summary;

    int flush_count;
    int pts_written[MAX_TST_PACKETS];
    int pts_written_nr;
} FailingMuxerContext;

static int failing_write_header(AVFormatContext *avf)
{
    FailingMuxerContext *ctx = avf->priv_data;
    return ctx->write_header_ret;
}

static int failing_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    FailingMuxerContext *ctx = avf->priv_data;
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

static int failing_write_trailer(AVFormatContext *avf)
{
    FailingMuxerContext *ctx = avf->priv_data;
    return ctx->write_trailer_ret;
}

static void failing_deinit(AVFormatContext *avf)
{
    int i;
    FailingMuxerContext *ctx = avf->priv_data;

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
#define OFFSET(x) offsetof(FailingMuxerContext, x)
static const AVOption options[] = {
        {"write_header_ret", "write_header() return value", OFFSET(write_header_ret),
         AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
        {"write_trailer_ret", "write_trailer() return value", OFFSET(write_trailer_ret),
         AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
        {"print_deinit_summary", "print summary when deinitializing muxer", OFFSET(print_deinit_summary),
         AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
        {NULL}
    };

static const AVClass failing_muxer_class = {
    .class_name = "Failing test muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat tst_failing_muxer = {
    .name           = "fail",
    .long_name      = NULL_IF_CONFIG_SMALL("Failing test muxer"),
    .priv_data_size = sizeof(FailingMuxerContext),
    .write_header   = failing_write_header,
    .write_packet   = failing_write_packet,
    .write_trailer  = failing_write_trailer,
    .deinit         = failing_deinit,
    .priv_class     = &failing_muxer_class,
    .flags          = AVFMT_NOFILE | AVFMT_ALLOW_FLUSH,
};

static int prepare_packet(AVPacket *pkt,const FailingMuxerPacketData *pkt_data, int64_t pts)
{
    int ret;
    FailingMuxerPacketData *data = av_malloc(sizeof(*data));
    memcpy(data, pkt_data, sizeof(FailingMuxerPacketData));
    ret = av_packet_from_data(pkt, (uint8_t*) data, sizeof(*data));

    pkt->pts = pkt->dts = pts;
    pkt->duration = 1;

    return ret;
}

static int initialize_fifo_tst_muxer_chain(AVFormatContext **oc)
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
        ret = AVERROR(ENOMEM);
    }

    return ret;
}

static int fifo_basic_test(AVFormatContext *oc, AVDictionary **opts,
                             const FailingMuxerPacketData *pkt_data)
{
    int ret = 0, i;
    AVPacket pkt;

    av_init_packet(&pkt);


    ret = avformat_write_header(oc, opts);
    if (ret) {
        fprintf(stderr, "Unexpected write_header failure: %s\n",
                av_err2str(ret));
        goto fail;
    }

    for (i = 0; i < 15; i++ ) {
        ret = prepare_packet(&pkt, pkt_data, i);
        if (ret < 0) {
            fprintf(stderr, "Failed to prepare test packet: %s\n",
                    av_err2str(ret));
            goto write_trailer_and_fail;
        }
        ret = av_write_frame(oc, &pkt);
        av_packet_unref(&pkt);
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

static int fifo_write_header_err_tst(AVFormatContext *oc, AVDictionary **opts,
                                     const FailingMuxerPacketData *pkt_data)
{
    int ret = 0, i;
    AVPacket pkt;

    av_init_packet(&pkt);

    ret = avformat_write_header(oc, opts);
    if (ret) {
        fprintf(stderr, "Unexpected write_header failure: %s\n",
                av_err2str(ret));
        goto fail;
    }

    for (i = 0; i < MAX_TST_PACKETS; i++ ) {
        ret = prepare_packet(&pkt, pkt_data, i);
        if (ret < 0) {
            fprintf(stderr, "Failed to prepare test packet: %s\n",
                    av_err2str(ret));
            goto write_trailer_and_fail;
        }
        ret = av_write_frame(oc, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0) {
            break;
        }
    }

    if (!ret) {
        fprintf(stderr, "write_packet not failed when supposed to.\n");
        goto fail;
    } else if (ret != -1) {
        fprintf(stderr, "Unexpected write_packet error: %s\n", av_err2str(ret));
        goto fail;
    }

    ret = av_write_trailer(oc);
    if (ret < 0)
        fprintf(stderr, "Unexpected write_trailer error: %s\n", av_err2str(ret));

    return ret;
write_trailer_and_fail:
    av_write_trailer(oc);
fail:
    return ret;
}

static int fifo_overflow_drop_test(AVFormatContext *oc, AVDictionary **opts,
                                   const FailingMuxerPacketData *data)
{
    int ret = 0, i;
    int64_t write_pkt_start, write_pkt_end, duration;
    AVPacket pkt;

    av_init_packet(&pkt);

    ret = avformat_write_header(oc, opts);
    if (ret) {
        fprintf(stderr, "Unexpected write_header failure: %s\n",
                av_err2str(ret));
        return ret;
    }

    write_pkt_start = av_gettime_relative();
    for (i = 0; i < 6; i++ ) {
        ret = prepare_packet(&pkt, data, i);
        if (ret < 0) {
            fprintf(stderr, "Failed to prepare test packet: %s\n",
                    av_err2str(ret));
            goto fail;
        }
        ret = av_write_frame(oc, &pkt);
        av_packet_unref(&pkt);
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
    int (*test_func)(AVFormatContext *, AVDictionary **,const FailingMuxerPacketData *pkt_data);
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
    char buffer[BUFFER_SIZE];
    int ret, ret1;

    ret = initialize_fifo_tst_muxer_chain(&oc);
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
    ret1 = av_dict_set(&opts, "fifo_format", "fail", 0);
    if (ret < 0 || ret1 < 0) {
        fprintf(stderr, "Failed to set options for test muxer: %s\n",
                av_err2str(ret));
        goto end;
    }

    ret = test->test_func(oc, &opts, &test->pkt_data);

end:
    printf("%s: %s\n", test->test_name, ret < 0 ? "fail" : "ok");
    avformat_free_context(oc);
    av_dict_free(&opts);
    return ret;
}


const TestCase tests[] = {
        /* Simple test in packet-non-dropping mode, we expect to get on the output
         * exactly what was on input */
        {fifo_basic_test, "nonfail test", NULL,1, 0, 0, {0, 0, 0}},

        /* Test that we receive delayed write_header error from one of the write_packet
         * calls. */
        {fifo_write_header_err_tst, "write header error test", NULL, 0, -1, 0, {0, 0, 0}},

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

    av_register_all();
    av_register_output_format(&tst_failing_muxer);

    for (i = 0; tests[i].test_func; i++) {
        ret = run_test(&tests[i]);
        if (!ret_all && ret < 0)
            ret_all = ret;
    }

    return ret;
}
