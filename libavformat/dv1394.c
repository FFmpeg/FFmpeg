/*
 * Linux DV1394 interface
 * Copyright (c) 2003 Max Krasnyansky <maxk@qualcomm.com>
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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>

#include "avformat.h"

#undef DV1394_DEBUG

#include "dv1394.h"
#include "dv.h"

struct dv1394_data {
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
        perror("Failed to start receiver");
        return -1;
    }
    return 0;
}

static int dv1394_read_header(AVFormatContext * context, AVFormatParameters * ap)
{
    struct dv1394_data *dv = context->priv_data;

    dv->dv_demux = dv_init_demux(context);
    if (!dv->dv_demux)
        goto failed;

    if (ap->standard && !strcasecmp(ap->standard, "pal"))
        dv->format = DV1394_PAL;
    else
        dv->format = DV1394_NTSC;

    if (ap->channel)
        dv->channel = ap->channel;
    else
        dv->channel = DV1394_DEFAULT_CHANNEL;

    /* Open and initialize DV1394 device */
    dv->fd = open(context->filename, O_RDONLY);
    if (dv->fd < 0) {
        perror("Failed to open DV interface");
        goto failed;
    }

    if (dv1394_reset(dv) < 0) {
        perror("Failed to initialize DV interface");
        goto failed;
    }

    dv->ring = mmap(NULL, DV1394_PAL_FRAME_SIZE * DV1394_RING_FRAMES,
                    PROT_READ, MAP_PRIVATE, dv->fd, 0);
    if (dv->ring == MAP_FAILED) {
        perror("Failed to mmap DV ring buffer");
        goto failed;
    }

    if (dv1394_start(dv) < 0)
        goto failed;

    return 0;

failed:
    close(dv->fd);
    return AVERROR_IO;
}

static int dv1394_read_packet(AVFormatContext *context, AVPacket *pkt)
{
    struct dv1394_data *dv = context->priv_data;
    int size;

    size = dv_get_packet(dv->dv_demux, pkt);
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
            perror("Poll failed");
            return AVERROR_IO;
        }

        if (ioctl(dv->fd, DV1394_GET_STATUS, &s) < 0) {
            perror("Failed to get status");
            return AVERROR_IO;
        }
#ifdef DV1394_DEBUG
        av_log(context, AV_LOG_DEBUG, "DV1394: status\n"
                "\tactive_frame\t%d\n"
                "\tfirst_clear_frame\t%d\n"
                "\tn_clear_frames\t%d\n"
                "\tdropped_frames\t%d\n",
                s.active_frame, s.first_clear_frame,
                s.n_clear_frames, s.dropped_frames);
#endif

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

#ifdef DV1394_DEBUG
    av_log(context, AV_LOG_DEBUG, "index %d, avail %d, done %d\n", dv->index, dv->avail,
            dv->done);
#endif

    size = dv_produce_packet(dv->dv_demux, pkt,
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
        perror("Failed to shutdown DV1394");

    /* Unmap ring buffer */
    if (munmap(dv->ring, DV1394_NTSC_FRAME_SIZE * DV1394_RING_FRAMES) < 0)
        perror("Failed to munmap DV1394 ring buffer");

    close(dv->fd);
    av_free(dv->dv_demux);

    return 0;
}

AVInputFormat dv1394_demuxer = {
    .name           = "dv1394",
    .long_name      = "dv1394 A/V grab",
    .priv_data_size = sizeof(struct dv1394_data),
    .read_header    = dv1394_read_header,
    .read_packet    = dv1394_read_packet,
    .read_close     = dv1394_close,
    .flags          = AVFMT_NOFILE
};
