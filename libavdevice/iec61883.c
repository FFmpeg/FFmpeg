/*
 * Copyright (c) 2012 Georg Lippitsch <georg.lippitsch@gmx.at>
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

/**
 * @file
 * libiec61883 interface
 */

#include <sys/poll.h>
#include <libraw1394/raw1394.h>
#include <libavc1394/avc1394.h>
#include <libavc1394/rom1394.h>
#include <libiec61883/iec61883.h>
#include "libavformat/dv.h"
#include "libavformat/mpegts.h"
#include "libavutil/opt.h"
#include "avdevice.h"

#define THREADS HAVE_PTHREADS

#if THREADS
#include <pthread.h>
#endif

#define MOTDCT_SPEC_ID      0x00005068
#define IEC61883_AUTO       0
#define IEC61883_DV         1
#define IEC61883_HDV        2

/**
 * For DV, one packet corresponds exactly to one frame.
 * For HDV, these are MPEG2 transport stream packets.
 * The queue is implemented as linked list.
 */
typedef struct DVPacket {
    uint8_t *buf;                       ///< actual buffer data
    int len;                            ///< size of buffer allocated
    struct DVPacket *next;              ///< next DVPacket
} DVPacket;

struct iec61883_data {
    AVClass *class;
    raw1394handle_t raw1394;            ///< handle for libraw1394
    iec61883_dv_fb_t iec61883_dv;       ///< handle for libiec61883 when used with DV
    iec61883_mpeg2_t iec61883_mpeg2;    ///< handle for libiec61883 when used with HDV

    DVDemuxContext *dv_demux;           ///< generic DV muxing/demuxing context
    MpegTSContext *mpeg_demux;          ///< generic HDV muxing/demuxing context

    DVPacket *queue_first;              ///< first element of packet queue
    DVPacket *queue_last;               ///< last element of packet queue

    char *device_guid;                  ///< to select one of multiple DV devices

    int packets;                        ///< Number of packets queued
    int max_packets;                    ///< Max. number of packets in queue

    int bandwidth;                      ///< returned by libiec61883
    int channel;                        ///< returned by libiec61883
    int input_port;                     ///< returned by libiec61883
    int type;                           ///< Stream type, to distinguish DV/HDV
    int node;                           ///< returned by libiec61883
    int output_port;                    ///< returned by libiec61883
    int thread_loop;                    ///< Condition for thread while-loop
    int receiving;                      ///< True as soon data from device available
    int receive_error;                  ///< Set in receive task in case of error
    int eof;                            ///< True as soon as no more data available

    struct pollfd raw1394_poll;         ///< to poll for new data from libraw1394

    /** Parse function for DV/HDV differs, so this is set before packets arrive */
    int (*parse_queue)(struct iec61883_data *dv, AVPacket *pkt);

#if THREADS
    pthread_t receive_task_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
};

static int iec61883_callback(unsigned char *data, int length,
                             int complete, void *callback_data)
{
    struct iec61883_data *dv = callback_data;
    DVPacket *packet;
    int ret;

#if THREADS
    pthread_mutex_lock(&dv->mutex);
#endif

    if (dv->packets >= dv->max_packets) {
        av_log(NULL, AV_LOG_ERROR, "DV packet queue overrun, dropping.\n");
        ret = 0;
        goto exit;
    }

    packet = av_mallocz(sizeof(*packet));
    if (!packet) {
        ret = -1;
        goto exit;
    }

    packet->buf = av_malloc(length);
    if (!packet->buf) {
        ret = -1;
        goto exit;
    }
    packet->len = length;

    memcpy(packet->buf, data, length);

    if (dv->queue_first) {
        dv->queue_last->next = packet;
        dv->queue_last = packet;
    } else {
        dv->queue_first = packet;
        dv->queue_last = packet;
    }
    dv->packets++;

    ret = 0;

exit:
#if THREADS
    pthread_cond_broadcast(&dv->cond);
    pthread_mutex_unlock(&dv->mutex);
#endif
    return ret;
}

static void *iec61883_receive_task(void *opaque)
{
    struct iec61883_data *dv = (struct iec61883_data *)opaque;
    int result;

#if THREADS
    while (dv->thread_loop)
#endif
    {
        while ((result = poll(&dv->raw1394_poll, 1, 200)) < 0) {
            if (!(errno == EAGAIN || errno == EINTR)) {
                av_log(NULL, AV_LOG_ERROR, "Raw1394 poll error occurred.\n");
                dv->receive_error = AVERROR(EIO);
                return NULL;
            }
        }
        if (result > 0 && ((dv->raw1394_poll.revents & POLLIN)
                           || (dv->raw1394_poll.revents & POLLPRI))) {
            dv->receiving = 1;
            raw1394_loop_iterate(dv->raw1394);
        } else if (dv->receiving) {
            av_log(NULL, AV_LOG_ERROR, "No more input data available\n");
#if THREADS
            pthread_mutex_lock(&dv->mutex);
            dv->eof = 1;
            pthread_cond_broadcast(&dv->cond);
            pthread_mutex_unlock(&dv->mutex);
#else
            dv->eof = 1;
#endif
            return NULL;
        }
    }

    return NULL;
}

static int iec61883_parse_queue_dv(struct iec61883_data *dv, AVPacket *pkt)
{
    DVPacket *packet;
    int size;

    size = avpriv_dv_get_packet(dv->dv_demux, pkt);
    if (size > 0)
        return size;

    packet = dv->queue_first;
    if (!packet)
        return -1;

    size = avpriv_dv_produce_packet(dv->dv_demux, pkt,
                                    packet->buf, packet->len, -1);
    pkt->destruct = av_destruct_packet;
    dv->queue_first = packet->next;
    av_free(packet);
    dv->packets--;

    if (size > 0)
        return size;

    return -1;
}

static int iec61883_parse_queue_hdv(struct iec61883_data *dv, AVPacket *pkt)
{
    DVPacket *packet;
    int size;

    while (dv->queue_first) {
        packet = dv->queue_first;
        size = ff_mpegts_parse_packet(dv->mpeg_demux, pkt, packet->buf,
                                      packet->len);
        dv->queue_first = packet->next;
        av_free(packet->buf);
        av_free(packet);
        dv->packets--;

        if (size > 0)
            return size;
    }

    return -1;
}

static int iec61883_read_header(AVFormatContext *context)
{
    struct iec61883_data *dv = context->priv_data;
    struct raw1394_portinfo pinf[16];
    rom1394_directory rom_dir;
    char *endptr;
    int inport;
    int nb_ports;
    int port = -1;
    int response;
    int i, j = 0;
    uint64_t guid = 0;

    dv->input_port = -1;
    dv->output_port = -1;
    dv->channel = -1;

    dv->raw1394 = raw1394_new_handle();

    if (!dv->raw1394) {
        av_log(context, AV_LOG_ERROR, "Failed to open IEEE1394 interface.\n");
        return AVERROR(EIO);
    }

    if ((nb_ports = raw1394_get_port_info(dv->raw1394, pinf, 16)) < 0) {
        av_log(context, AV_LOG_ERROR, "Failed to get number of IEEE1394 ports.\n");
        goto fail;
    }

    inport = strtol(context->filename, &endptr, 10);
    if (endptr != context->filename && *endptr == '\0') {
        av_log(context, AV_LOG_INFO, "Selecting IEEE1394 port: %d\n", inport);
        j = inport;
        nb_ports = inport + 1;
    } else if (strcmp(context->filename, "auto")) {
        av_log(context, AV_LOG_ERROR, "Invalid input \"%s\", you should specify "
               "\"auto\" for auto-detection, or the port number.\n", context->filename);
        goto fail;
    }

    if (dv->device_guid) {
        if (sscanf(dv->device_guid, "%llx", (long long unsigned int *)&guid) != 1) {
            av_log(context, AV_LOG_INFO, "Invalid dvguid parameter: %s\n",
                   dv->device_guid);
            goto fail;
        }
    }

    for (; j < nb_ports && port==-1; ++j) {
        raw1394_destroy_handle(dv->raw1394);

        if (!(dv->raw1394 = raw1394_new_handle_on_port(j))) {
            av_log(context, AV_LOG_ERROR, "Failed setting IEEE1394 port.\n");
            goto fail;
        }

        for (i=0; i<raw1394_get_nodecount(dv->raw1394); ++i) {

            /* Select device explicitly by GUID */

            if (guid > 1) {
                if (guid == rom1394_get_guid(dv->raw1394, i)) {
                    dv->node = i;
                    port = j;
                    break;
                }
            } else {

                /* Select first AV/C tape recorder player node */

                if (rom1394_get_directory(dv->raw1394, i, &rom_dir) < 0)
                    continue;
                if (((rom1394_get_node_type(&rom_dir) == ROM1394_NODE_TYPE_AVC) &&
                     avc1394_check_subunit_type(dv->raw1394, i, AVC1394_SUBUNIT_TYPE_VCR)) ||
                    (rom_dir.unit_spec_id == MOTDCT_SPEC_ID)) {
                    rom1394_free_directory(&rom_dir);
                    dv->node = i;
                    port = j;
                    break;
                }
                rom1394_free_directory(&rom_dir);
            }
        }
    }

    if (port == -1) {
        av_log(context, AV_LOG_ERROR, "No AV/C devices found.\n");
        goto fail;
    }

    /* Provide bus sanity for multiple connections */

    iec61883_cmp_normalize_output(dv->raw1394, 0xffc0 | dv->node);

    /* Find out if device is DV or HDV */

    if (dv->type == IEC61883_AUTO) {
        response = avc1394_transaction(dv->raw1394, dv->node,
                                       AVC1394_CTYPE_STATUS |
                                       AVC1394_SUBUNIT_TYPE_TAPE_RECORDER |
                                       AVC1394_SUBUNIT_ID_0 |
                                       AVC1394_VCR_COMMAND_OUTPUT_SIGNAL_MODE |
                                       0xFF, 2);
        response = AVC1394_GET_OPERAND0(response);
        dv->type = (response == 0x10 || response == 0x90 || response == 0x1A || response == 0x9A) ?
            IEC61883_HDV : IEC61883_DV;
    }

    /* Connect to device, and do initialization */

    dv->channel = iec61883_cmp_connect(dv->raw1394, dv->node, &dv->output_port,
                                       raw1394_get_local_id(dv->raw1394),
                                       &dv->input_port, &dv->bandwidth);

    if (dv->channel < 0)
        dv->channel = 63;

    if (!dv->max_packets)
        dv->max_packets = 100;

    if (dv->type == IEC61883_HDV) {

        /* Init HDV receive */

        avformat_new_stream(context, NULL);

        dv->mpeg_demux = ff_mpegts_parse_open(context);
        if (!dv->mpeg_demux)
            goto fail;

        dv->parse_queue = iec61883_parse_queue_hdv;

        dv->iec61883_mpeg2 = iec61883_mpeg2_recv_init(dv->raw1394,
                                                      (iec61883_mpeg2_recv_t)iec61883_callback,
                                                      dv);

        dv->max_packets *= 766;
    } else {

        /* Init DV receive */

        dv->dv_demux = avpriv_dv_init_demux(context);
        if (!dv->dv_demux)
            goto fail;

        dv->parse_queue = iec61883_parse_queue_dv;

        dv->iec61883_dv = iec61883_dv_fb_init(dv->raw1394, iec61883_callback, dv);
    }

    dv->raw1394_poll.fd = raw1394_get_fd(dv->raw1394);
    dv->raw1394_poll.events = POLLIN | POLLERR | POLLHUP | POLLPRI;

    /* Actually start receiving */

    if (dv->type == IEC61883_HDV)
        iec61883_mpeg2_recv_start(dv->iec61883_mpeg2, dv->channel);
    else
        iec61883_dv_fb_start(dv->iec61883_dv, dv->channel);

#if THREADS
    dv->thread_loop = 1;
    pthread_mutex_init(&dv->mutex, NULL);
    pthread_cond_init(&dv->cond, NULL);
    pthread_create(&dv->receive_task_thread, NULL, iec61883_receive_task, dv);
#endif

    return 0;

fail:
    raw1394_destroy_handle(dv->raw1394);
    return AVERROR(EIO);
}

static int iec61883_read_packet(AVFormatContext *context, AVPacket *pkt)
{
    struct iec61883_data *dv = context->priv_data;
    int size;

    /**
     * Try to parse frames from queue
     */

#if THREADS
    pthread_mutex_lock(&dv->mutex);
    while ((size = dv->parse_queue(dv, pkt)) == -1)
        if (!dv->eof)
            pthread_cond_wait(&dv->cond, &dv->mutex);
        else
            break;
    pthread_mutex_unlock(&dv->mutex);
#else
    int result;
    while ((size = dv->parse_queue(dv, pkt)) == -1) {
        iec61883_receive_task((void *)dv);
        if (dv->receive_error)
            return dv->receive_error;
    }
#endif

    return size;
}

static int iec61883_close(AVFormatContext *context)
{
    struct iec61883_data *dv = context->priv_data;

#if THREADS
    dv->thread_loop = 0;
    pthread_join(dv->receive_task_thread, NULL);
    pthread_cond_destroy(&dv->cond);
    pthread_mutex_destroy(&dv->mutex);
#endif

    if (dv->type == IEC61883_HDV) {
        iec61883_mpeg2_recv_stop(dv->iec61883_mpeg2);
        iec61883_mpeg2_close(dv->iec61883_mpeg2);
        ff_mpegts_parse_close(dv->mpeg_demux);
    } else {
        iec61883_dv_fb_stop(dv->iec61883_dv);
        iec61883_dv_fb_close(dv->iec61883_dv);
    }
    while (dv->queue_first) {
        DVPacket *packet = dv->queue_first;
        dv->queue_first = packet->next;
        av_free(packet->buf);
        av_free(packet);
    }

    iec61883_cmp_disconnect(dv->raw1394, dv->node, dv->output_port,
                            raw1394_get_local_id(dv->raw1394),
                            dv->input_port, dv->channel, dv->bandwidth);

    raw1394_destroy_handle(dv->raw1394);

    return 0;
}

static const AVOption options[] = {
    { "dvtype", "override autodetection of DV/HDV", offsetof(struct iec61883_data, type), AV_OPT_TYPE_INT, {.i64 = IEC61883_AUTO}, IEC61883_AUTO, IEC61883_HDV, AV_OPT_FLAG_DECODING_PARAM, "dvtype" },
    { "auto",   "auto detect DV/HDV", 0, AV_OPT_TYPE_CONST, {.i64 = IEC61883_AUTO}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "dvtype" },
    { "dv",     "force device being treated as DV device", 0, AV_OPT_TYPE_CONST, {.i64 = IEC61883_DV},   0, 0, AV_OPT_FLAG_DECODING_PARAM, "dvtype" },
    { "hdv" ,   "force device being treated as HDV device", 0, AV_OPT_TYPE_CONST, {.i64 = IEC61883_HDV},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "dvtype" },
    { "dvbuffer", "set queue buffer size (in packets)", offsetof(struct iec61883_data, max_packets), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "dvguid", "select one of multiple DV devices by its GUID", offsetof(struct iec61883_data, device_guid), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass iec61883_class = {
    .class_name = "iec61883 indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

AVInputFormat ff_iec61883_demuxer = {
    .name           = "iec61883",
    .long_name      = NULL_IF_CONFIG_SMALL("libiec61883 (new DV1394) A/V input device"),
    .priv_data_size = sizeof(struct iec61883_data),
    .read_header    = iec61883_read_header,
    .read_packet    = iec61883_read_packet,
    .read_close     = iec61883_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &iec61883_class,
};
