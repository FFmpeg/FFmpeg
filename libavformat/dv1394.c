/*
 * Linux DV1394 interface
 * Copyright (c) 2003 Max Krasnyansky <maxk@qualcomm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>

#include "avformat.h"

#undef DV1394_DEBUG

#include "dv1394.h"

int dv1394_channel = DV1394_DEFAULT_CHANNEL;

struct dv1394_data {
    int fd;
    int channel;
    int width, height;
    int frame_rate;
    int frame_size;

    void *ring; /* Ring buffer */
    int index;  /* Current frame index */
    int avail;  /* Number of frames available for reading */
    int done;   /* Number of completed frames */
};

static int dv1394_reset(struct dv1394_data *dv)
{
    struct dv1394_init init;

    init.channel = dv->channel;
    init.api_version = DV1394_API_VERSION;
    init.n_frames = DV1394_RING_FRAMES;
    init.format = DV1394_NTSC;

    if (ioctl(dv->fd, DV1394_INIT, &init) < 0)
        return -1;

    dv->avail = 0;
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
    AVStream *st;
    const char *video_device;

    st = av_new_stream(context, 0);
    if (!st)
        return -ENOMEM;

    dv->width   = DV1394_WIDTH;
    dv->height  = DV1394_HEIGHT;

    if (ap->channel)
        dv->channel = ap->channel;
    else
        dv->channel = DV1394_DEFAULT_CHANNEL;

    dv->frame_rate = 30;

    dv->frame_size = DV1394_NTSC_FRAME_SIZE;

    /* Open and initialize DV1394 device */
    video_device = ap->device;
    if (!video_device)
        video_device = "/dev/dv1394/0";
    dv->fd = open(video_device, O_RDONLY);
    if (dv->fd < 0) {
        perror("Failed to open DV interface");
        goto failed;
    }

    if (dv1394_reset(dv) < 0) {
        perror("Failed to initialize DV interface");
        goto failed;
    }

    dv->ring = mmap(NULL, DV1394_NTSC_FRAME_SIZE * DV1394_RING_FRAMES,
                    PROT_READ, MAP_PRIVATE, dv->fd, 0);
    if (!dv->ring) {
        perror("Failed to mmap DV ring buffer");
        goto failed;
    }

    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id   = CODEC_ID_DVVIDEO;
    st->codec.width      = dv->width;
    st->codec.height     = dv->height;
    st->codec.frame_rate = dv->frame_rate * FRAME_RATE_BASE;

    st->codec.bit_rate   = 25000000;  /* Consumer DV is 25Mbps */

    av_set_pts_info(context, 48, 1, 1000000);

    if (dv1394_start(dv) < 0)
        goto failed;

    return 0;

failed:
    close(dv->fd);
    av_free(st);
    return -EIO;
}

static inline int __copy_frame(struct dv1394_data *dv, void *buf)
{
    char *ptr = dv->ring + (dv->index * dv->frame_size);

    memcpy(buf, ptr, dv->frame_size);

    dv->index = (dv->index + 1) % DV1394_RING_FRAMES;
    dv->avail--;
    dv->done++;

    return dv->frame_size;
}

static int dv1394_read_packet(AVFormatContext * context, AVPacket * pkt)
{
    struct dv1394_data *dv = context->priv_data;
    int len;

    if (!dv->avail) {
        struct dv1394_status s;
        struct pollfd p;
        p.fd = dv->fd;
        p.events = POLLIN | POLLERR | POLLHUP;

        /* Wait until more frames are available */
        if (poll(&p, 1, -1) < 0) {
            perror("Poll failed");
            return -EIO;
        }

        if (ioctl(dv->fd, DV1394_GET_STATUS, &s) < 0) {
            perror("Failed to get status");
            return -EIO;
        }
#ifdef DV1394_DEBUG
        fprintf(stderr, "DV1394: status\n"
                "\tactive_frame\t%d\n"
                "\tfirst_clear_frame\t%d\n"
                "\tn_clear_frames\t%d\n"
                "\tdropped_frames\t%d\n",
                s.active_frame, s.first_clear_frame,
                s.n_clear_frames, s.dropped_frames);
#endif

        dv->avail = s.n_clear_frames;
        dv->index = s.first_clear_frame;
        dv->done = 0;

        if (s.dropped_frames) {
            fprintf(stderr, "DV1394: Frame drop detected (%d). Reseting ..\n",
                    s.dropped_frames);

            dv1394_reset(dv);
            dv1394_start(dv);
        }
    }

    if (av_new_packet(pkt, dv->frame_size) < 0)
        return -EIO;

#ifdef DV1394_DEBUG
    fprintf(stderr, "index %d, avail %d, done %d\n", dv->index, dv->avail,
            dv->done);
#endif

    len = __copy_frame(dv, pkt->data);
    pkt->pts = av_gettime() & ((1LL << 48) - 1);

    if (!dv->avail && dv->done) {
        /* Request more frames */
        if (ioctl(dv->fd, DV1394_RECEIVE_FRAMES, dv->done) < 0) {
            /* This usually means that ring buffer overflowed.
             * We have to reset :(.
             */

            fprintf(stderr, "DV1394: Ring buffer overflow. Reseting ..\n");

            dv1394_reset(dv);
            dv1394_start(dv);
        }
    }

    return len;
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

    return 0;
}

static AVInputFormat dv1394_format = {
    .name           = "dv1394",
    .long_name      = "dv1394 A/V grab",
    .priv_data_size = sizeof(struct dv1394_data),
    .read_header    = dv1394_read_header,
    .read_packet    = dv1394_read_packet,
    .read_close     = dv1394_close,
    .flags          = AVFMT_NOFILE
};

int dv1394_init(void)
{
    av_register_input_format(&dv1394_format);
    return 0;
}
