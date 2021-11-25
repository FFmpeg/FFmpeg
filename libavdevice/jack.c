/*
 * JACK Audio Connection Kit input device
 * Copyright (c) 2009 Samalyse
 * Author: Olivier Guilyardi <olivier samalyse com>
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

#include "config.h"
#include <semaphore.h>
#include <jack/jack.h>

#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "timefilter.h"
#include "avdevice.h"

/**
 * Size of the internal FIFO buffers as a number of audio packets
 */
#define FIFO_PACKETS_NUM 16

typedef struct JackData {
    AVClass        *class;
    jack_client_t * client;
    int             activated;
    sem_t           packet_count;
    jack_nframes_t  sample_rate;
    jack_nframes_t  buffer_size;
    jack_port_t **  ports;
    int             nports;
    TimeFilter *    timefilter;
    AVFifoBuffer *  new_pkts;
    AVFifoBuffer *  filled_pkts;
    int             pkt_xrun;
    int             jack_xrun;
} JackData;

static int process_callback(jack_nframes_t nframes, void *arg)
{
    /* Warning: this function runs in realtime. One mustn't allocate memory here
     * or do any other thing that could block. */

    int i, j;
    JackData *self = arg;
    float * buffer;
    jack_nframes_t latency, cycle_delay;
    AVPacket pkt;
    float *pkt_data;
    double cycle_time;

    if (!self->client)
        return 0;

    /* The approximate delay since the hardware interrupt as a number of frames */
    cycle_delay = jack_frames_since_cycle_start(self->client);

    /* Retrieve filtered cycle time */
    cycle_time = ff_timefilter_update(self->timefilter,
                                      av_gettime() / 1000000.0 - (double) cycle_delay / self->sample_rate,
                                      self->buffer_size);

    /* Check if an empty packet is available, and if there's enough space to send it back once filled */
    if ((av_fifo_size(self->new_pkts) < sizeof(pkt)) || (av_fifo_space(self->filled_pkts) < sizeof(pkt))) {
        self->pkt_xrun = 1;
        return 0;
    }

    /* Retrieve empty (but allocated) packet */
    av_fifo_generic_read(self->new_pkts, &pkt, sizeof(pkt), NULL);

    pkt_data  = (float *) pkt.data;
    latency   = 0;

    /* Copy and interleave audio data from the JACK buffer into the packet */
    for (i = 0; i < self->nports; i++) {
        jack_latency_range_t range;
        jack_port_get_latency_range(self->ports[i], JackCaptureLatency, &range);
        latency += range.max;
        buffer = jack_port_get_buffer(self->ports[i], self->buffer_size);
        for (j = 0; j < self->buffer_size; j++)
            pkt_data[j * self->nports + i] = buffer[j];
    }

    /* Timestamp the packet with the cycle start time minus the average latency */
    pkt.pts = (cycle_time - (double) latency / (self->nports * self->sample_rate)) * 1000000.0;

    /* Send the now filled packet back, and increase packet counter */
    av_fifo_generic_write(self->filled_pkts, &pkt, sizeof(pkt), NULL);
    sem_post(&self->packet_count);

    return 0;
}

static void shutdown_callback(void *arg)
{
    JackData *self = arg;
    self->client = NULL;
}

static int xrun_callback(void *arg)
{
    JackData *self = arg;
    self->jack_xrun = 1;
    ff_timefilter_reset(self->timefilter);
    return 0;
}

static int supply_new_packets(JackData *self, AVFormatContext *context)
{
    AVPacket pkt;
    int test, pkt_size = self->buffer_size * self->nports * sizeof(float);

    /* Supply the process callback with new empty packets, by filling the new
     * packets FIFO buffer with as many packets as possible. process_callback()
     * can't do this by itself, because it can't allocate memory in realtime. */
    while (av_fifo_space(self->new_pkts) >= sizeof(pkt)) {
        if ((test = av_new_packet(&pkt, pkt_size)) < 0) {
            av_log(context, AV_LOG_ERROR, "Could not create packet of size %d\n", pkt_size);
            return test;
        }
        av_fifo_generic_write(self->new_pkts, &pkt, sizeof(pkt), NULL);
    }
    return 0;
}

static int start_jack(AVFormatContext *context)
{
    JackData *self = context->priv_data;
    jack_status_t status;
    int i, test;

    /* Register as a JACK client, using the context url as client name. */
    self->client = jack_client_open(context->url, JackNullOption, &status);
    if (!self->client) {
        av_log(context, AV_LOG_ERROR, "Unable to register as a JACK client\n");
        return AVERROR(EIO);
    }

    sem_init(&self->packet_count, 0, 0);

    self->sample_rate = jack_get_sample_rate(self->client);
    self->ports       = av_malloc_array(self->nports, sizeof(*self->ports));
    if (!self->ports)
        return AVERROR(ENOMEM);
    self->buffer_size = jack_get_buffer_size(self->client);

    /* Register JACK ports */
    for (i = 0; i < self->nports; i++) {
        char str[32];
        snprintf(str, sizeof(str), "input_%d", i + 1);
        self->ports[i] = jack_port_register(self->client, str,
                                            JACK_DEFAULT_AUDIO_TYPE,
                                            JackPortIsInput, 0);
        if (!self->ports[i]) {
            av_log(context, AV_LOG_ERROR, "Unable to register port %s:%s\n",
                   context->url, str);
            jack_client_close(self->client);
            return AVERROR(EIO);
        }
    }

    /* Register JACK callbacks */
    jack_set_process_callback(self->client, process_callback, self);
    jack_on_shutdown(self->client, shutdown_callback, self);
    jack_set_xrun_callback(self->client, xrun_callback, self);

    /* Create time filter */
    self->timefilter  = ff_timefilter_new (1.0 / self->sample_rate, self->buffer_size, 1.5);
    if (!self->timefilter) {
        jack_client_close(self->client);
        return AVERROR(ENOMEM);
    }

    /* Create FIFO buffers */
    self->filled_pkts = av_fifo_alloc_array(FIFO_PACKETS_NUM, sizeof(AVPacket));
    /* New packets FIFO with one extra packet for safety against underruns */
    self->new_pkts    = av_fifo_alloc_array((FIFO_PACKETS_NUM + 1), sizeof(AVPacket));
    if (!self->new_pkts) {
        jack_client_close(self->client);
        return AVERROR(ENOMEM);
    }
    if ((test = supply_new_packets(self, context))) {
        jack_client_close(self->client);
        return test;
    }

    return 0;

}

static void free_pkt_fifo(AVFifoBuffer **fifo)
{
    AVPacket pkt;
    while (av_fifo_size(*fifo)) {
        av_fifo_generic_read(*fifo, &pkt, sizeof(pkt), NULL);
        av_packet_unref(&pkt);
    }
    av_fifo_freep(fifo);
}

static void stop_jack(JackData *self)
{
    if (self->client) {
        if (self->activated)
            jack_deactivate(self->client);
        jack_client_close(self->client);
    }
    sem_destroy(&self->packet_count);
    free_pkt_fifo(&self->new_pkts);
    free_pkt_fifo(&self->filled_pkts);
    av_freep(&self->ports);
    ff_timefilter_destroy(self->timefilter);
}

static int audio_read_header(AVFormatContext *context)
{
    JackData *self = context->priv_data;
    AVStream *stream;
    int test;

    if ((test = start_jack(context)))
        return test;

    stream = avformat_new_stream(context, NULL);
    if (!stream) {
        stop_jack(self);
        return AVERROR(ENOMEM);
    }

    stream->codecpar->codec_type   = AVMEDIA_TYPE_AUDIO;
#if HAVE_BIGENDIAN
    stream->codecpar->codec_id     = AV_CODEC_ID_PCM_F32BE;
#else
    stream->codecpar->codec_id     = AV_CODEC_ID_PCM_F32LE;
#endif
    stream->codecpar->sample_rate  = self->sample_rate;
    stream->codecpar->channels     = self->nports;

    avpriv_set_pts_info(stream, 64, 1, 1000000);  /* 64 bits pts in us */
    return 0;
}

static int audio_read_packet(AVFormatContext *context, AVPacket *pkt)
{
    JackData *self = context->priv_data;
    struct timespec timeout = {0, 0};
    int test;

    /* Activate the JACK client on first packet read. Activating the JACK client
     * means that process_callback() starts to get called at regular interval.
     * If we activate it in audio_read_header(), we're actually reading audio data
     * from the device before instructed to, and that may result in an overrun. */
    if (!self->activated) {
        if (!jack_activate(self->client)) {
            self->activated = 1;
            av_log(context, AV_LOG_INFO,
                   "JACK client registered and activated (rate=%dHz, buffer_size=%d frames)\n",
                   self->sample_rate, self->buffer_size);
        } else {
            av_log(context, AV_LOG_ERROR, "Unable to activate JACK client\n");
            return AVERROR(EIO);
        }
    }

    /* Wait for a packet coming back from process_callback(), if one isn't available yet */
    timeout.tv_sec = av_gettime() / 1000000 + 2;
    if (sem_timedwait(&self->packet_count, &timeout)) {
        if (errno == ETIMEDOUT) {
            av_log(context, AV_LOG_ERROR,
                   "Input error: timed out when waiting for JACK process callback output\n");
        } else {
            char errbuf[128];
            int ret = AVERROR(errno);
            av_strerror(ret, errbuf, sizeof(errbuf));
            av_log(context, AV_LOG_ERROR, "Error while waiting for audio packet: %s\n",
                   errbuf);
        }
        if (!self->client)
            av_log(context, AV_LOG_ERROR, "Input error: JACK server is gone\n");

        return AVERROR(EIO);
    }

    if (self->pkt_xrun) {
        av_log(context, AV_LOG_WARNING, "Audio packet xrun\n");
        self->pkt_xrun = 0;
    }

    if (self->jack_xrun) {
        av_log(context, AV_LOG_WARNING, "JACK xrun\n");
        self->jack_xrun = 0;
    }

    /* Retrieve the packet filled with audio data by process_callback() */
    av_fifo_generic_read(self->filled_pkts, pkt, sizeof(*pkt), NULL);

    if ((test = supply_new_packets(self, context)))
        return test;

    return 0;
}

static int audio_read_close(AVFormatContext *context)
{
    JackData *self = context->priv_data;
    stop_jack(self);
    return 0;
}

#define OFFSET(x) offsetof(JackData, x)
static const AVOption options[] = {
    { "channels", "Number of audio channels.", OFFSET(nports), AV_OPT_TYPE_INT, { .i64 = 2 }, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass jack_indev_class = {
    .class_name     = "JACK indev",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
};

const AVInputFormat ff_jack_demuxer = {
    .name           = "jack",
    .long_name      = NULL_IF_CONFIG_SMALL("JACK Audio Connection Kit"),
    .priv_data_size = sizeof(JackData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = audio_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &jack_indev_class,
};
