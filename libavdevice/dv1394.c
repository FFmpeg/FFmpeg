/*
 * Linux DV1394 interface
 * Copyright (c) 2003 Max Krasnyansky <maxk@qualcomm.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "libavformat/dv.h"
#include "dv1394.h"

struct dv1394_data {
    AVClass *class;
    int fd;
    int channel;
    int format;

    uint8_t *ring; /* Ring buffer */
    int index;  /* Current frame index */
    int avail;  /* Number of frames available for reading */
    int done;   /* Number of completed frames */

    DVDemuxContext* dv_demux; /* Generic DV muxing/demuxing context */
};

/*
 * The trick here is to kludge around well known problem with kernel Ooopsing
 * when you try to capture PAL on a device node configure for NTSC. That's
 * why we have to configure the device node for PAL, and then read only NTSC
 * amount of data.
 */
static int dv1394_reset(struct dv1394_data *dv)
{
    struct dv1394_init init;

    init.channel     = dv->channel;
    init.api_version = DV1394_API_VERSION;
    init.n_frames    = DV1394_RING_FRAMES;
    init.format      = DV1394_PAL;

    if (ioctl(dv->fd, DV1394_INIT, &init) < 0)
        return -1;

    dv->avail  = dv->done = 0;
    return 0;
}

static int dv1394_start(struct dv1394_data *dv)
{
    /* Tell DV1394 driver to enable receiver */
    if (ioctl(dv->fd, DV1394_START_RECEIVE, 0) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to start receiver: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int dv1394_read_header(AVFormatContext * context)
{
    struct dv1394_data *dv = context->priv_data;

    dv->dv_demux = avpriv_dv_init_demux(context);
    if (!dv->dv_demux)
        goto failed;

    /* Open and initialize DV1394 device */
    dv->fd = avpriv_open(context->filename, O_RDONLY);
    if (dv->fd < 0) {
        av_log(context, AV_LOG_ERROR, "Failed to open DV interface: %s\n", strerror(errno));
        goto failed;
    }

    if (dv1394_reset(dv) < 0) {
        av_log(context, AV_LOG_ERROR, "Failed to initialize DV interface: %s\n", strerror(errno));
        goto failed;
    }

    dv->ring = mmap(NULL, DV1394_PAL_FRAME_SIZE * DV1394_RING_FRAMES,
                    PROT_READ, MAP_PRIVATE, dv->fd, 0);
    if (dv->ring == MAP_FAILED) {
        av_log(context, AV_LOG_ERROR, "Failed to mmap DV ring buffer: %s\n", strerror(errno));
        goto failed;
    }

    if (dv1394_start(dv) < 0)
        goto failed;

    return 0;

failed:
    close(dv->fd);
    return AVERROR(EIO);
}

static int dv1394_read_packet(AVFormatContext *context, AVPacket *pkt)
{
    struct dv1394_data *dv = context->priv_data;
    int size;

    size = avpriv_dv_get_packet(dv->dv_demux, pkt);
    if (size > 0)
        return size;

    if (!dv->avail) {
        struct dv1394_status s;
        struct pollfd p;

        if (dv->done) {
            /* Request more frames */
            if (ioctl(dv->fd, DV1394_RECEIVE_FRAMES, dv->done) < 0) {
                /* This usually means that ring buffer overflowed.
                 * We have to reset :(.
                 */

                av_log(context, AV_LOG_ERROR, "DV1394: Ring buffer overflow. Reseting ..\n");

                dv1394_reset(dv);
                dv1394_start(dv);
            }
            dv->done = 0;
        }

        /* Wait until more frames are available */
restart_poll:
        p.fd = dv->fd;
        p.events = POLLIN | POLLERR | POLLHUP;
        if (poll(&p, 1, -1) < 0) {
            if (errno == EAGAIN || errno == EINTR)
                goto restart_poll;
            av_log(context, AV_LOG_ERROR, "Poll failed: %s\n", strerror(errno));
            return AVERROR(EIO);
        }

        if (ioctl(dv->fd, DV1394_GET_STATUS, &s) < 0) {
            av_log(context, AV_LOG_ERROR, "Failed to get status: %s\n", strerror(errno));
            return AVERROR(EIO);
        }
        av_dlog(context, "DV1394: status\n"
                "\tactive_frame\t%d\n"
                "\tfirst_clear_frame\t%d\n"
                "\tn_clear_frames\t%d\n"
                "\tdropped_frames\t%d\n",
                s.active_frame, s.first_clear_frame,
                s.n_clear_frames, s.dropped_frames);

        dv->avail = s.n_clear_frames;
        dv->index = s.first_clear_frame;
        dv->done  = 0;

        if (s.dropped_frames) {
            av_log(context, AV_LOG_ERROR, "DV1394: Frame drop detected (%d). Reseting ..\n",
                    s.dropped_frames);

            dv1394_reset(dv);
            dv1394_start(dv);
        }
    }

    av_dlog(context, "index %d, avail %d, done %d\n", dv->index, dv->avail,
            dv->done);

    size = avpriv_dv_produce_packet(dv->dv_demux, pkt,
                             dv->ring + (dv->index * DV1394_PAL_FRAME_SIZE),
                             DV1394_PAL_FRAME_SIZE);
    dv->index = (dv->index + 1) % DV1394_RING_FRAMES;
    dv->done++; dv->avail--;

    return size;
}

static int dv1394_close(AVFormatContext * context)
{
    struct dv1394_data *dv = context->priv_data;

    /* Shutdown DV1394 receiver */
    if (ioctl(dv->fd, DV1394_SHUTDOWN, 0) < 0)
        av_log(context, AV_LOG_ERROR, "Failed to shutdown DV1394: %s\n", strerror(errno));

    /* Unmap ring buffer */
    if (munmap(dv->ring, DV1394_NTSC_FRAME_SIZE * DV1394_RING_FRAMES) < 0)
        av_log(context, AV_LOG_ERROR, "Failed to munmap DV1394 ring buffer: %s\n", strerror(errno));

    close(dv->fd);
    av_free(dv->dv_demux);

    return 0;
}

static const AVOption options[] = {
    { "standard", "", offsetof(struct dv1394_data, format), AV_OPT_TYPE_INT, {.i64 = DV1394_NTSC}, DV1394_NTSC, DV1394_PAL, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "PAL",      "", 0, AV_OPT_TYPE_CONST, {.i64 = DV1394_PAL},   0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "NTSC",     "", 0, AV_OPT_TYPE_CONST, {.i64 = DV1394_NTSC},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "channel",  "", offsetof(struct dv1394_data, channel), AV_OPT_TYPE_INT, {.i64 = DV1394_DEFAULT_CHANNEL}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass dv1394_class = {
    .class_name = "DV1394 indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_dv1394_demuxer = {
    .name           = "dv1394",
    .long_name      = NULL_IF_CONFIG_SMALL("DV1394 A/V grab"),
    .priv_data_size = sizeof(struct dv1394_data),
    .read_header    = dv1394_read_header,
    .read_packet    = dv1394_read_packet,
    .read_close     = dv1394_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &dv1394_class,
};
