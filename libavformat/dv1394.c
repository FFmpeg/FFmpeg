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
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>

#include "avformat.h"

#undef DV1394_DEBUG

#include "dv1394.h"

struct dv1394_data {
    int fd;
    int channel;
    int width, height;
    int frame_rate;
    int frame_size;
    int format;

    void *ring; /* Ring buffer */
    int index;  /* Current frame index */
    int avail;  /* Number of frames available for reading */
    int done;   /* Number of completed frames */

    int stream; /* Current stream. 0 - video, 1 - audio */
    int64_t pts;  /* Current timestamp */
};

static int dv1394_reset(struct dv1394_data *dv)
{
    struct dv1394_init init;

    init.channel     = dv->channel;
    init.api_version = DV1394_API_VERSION;
    init.n_frames    = DV1394_RING_FRAMES;
    init.format      = dv->format;

    if (ioctl(dv->fd, DV1394_INIT, &init) < 0)
        return -1;

    dv->avail  = dv->done = 0;
    dv->stream = 0;
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
    AVStream *vst, *ast;
    const char *video_device;

    vst = av_new_stream(context, 0);
    if (!vst)
        return -ENOMEM;
    ast = av_new_stream(context, 1);
    if (!ast) {
        av_free(vst);
        return -ENOMEM;
    }

    dv->width   = DV1394_WIDTH;
    dv->height  = DV1394_HEIGHT;

    if (ap->channel)
        dv->channel = ap->channel;
    else
        dv->channel = DV1394_DEFAULT_CHANNEL;

    /* FIXME: Need a format change parameter */
    dv->format = DV1394_NTSC;

    if (dv->format == DV1394_NTSC) {
        dv->frame_size = DV1394_NTSC_FRAME_SIZE;
        dv->frame_rate = 30;
    } else {
        dv->frame_size = DV1394_PAL_FRAME_SIZE;
        dv->frame_rate = 25;
    }

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

    dv->stream = 0;

    vst->codec.codec_type = CODEC_TYPE_VIDEO;
    vst->codec.codec_id   = CODEC_ID_DVVIDEO;
    vst->codec.width      = dv->width;
    vst->codec.height     = dv->height;
    vst->codec.frame_rate = dv->frame_rate;
    vst->codec.frame_rate_base = 1;
    vst->codec.bit_rate   = 25000000;  /* Consumer DV is 25Mbps */

    ast->codec.codec_type = CODEC_TYPE_AUDIO;
    ast->codec.codec_id   = CODEC_ID_DVAUDIO;
    ast->codec.channels   = 2;
    ast->codec.sample_rate= 48000;

    av_set_pts_info(context, 48, 1, 1000000);

    if (dv1394_start(dv) < 0)
        goto failed;

    return 0;

failed:
    close(dv->fd);
    av_free(vst);
    av_free(ast);
    return -EIO;
}

static void __destruct_pkt(struct AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
    return;
}

static inline int __get_frame(struct dv1394_data *dv, AVPacket *pkt)
{
    char *ptr = dv->ring + (dv->index * dv->frame_size);

    if (dv->stream) {
        dv->index = (dv->index + 1) % DV1394_RING_FRAMES;
        dv->done++; dv->avail--;
    } else {
        dv->pts = av_gettime() & ((1LL << 48) - 1);
    }

    av_init_packet(pkt);
    pkt->destruct = __destruct_pkt;
    pkt->data     = ptr;
    pkt->size     = dv->frame_size;
    pkt->pts      = dv->pts;
    pkt->stream_index = dv->stream;

    dv->stream ^= 1;

    return dv->frame_size;
}

static int dv1394_read_packet(AVFormatContext *context, AVPacket *pkt)
{
    struct dv1394_data *dv = context->priv_data;

    if (!dv->avail) {
        struct dv1394_status s;
        struct pollfd p;

        if (dv->done) {
            /* Request more frames */
            if (ioctl(dv->fd, DV1394_RECEIVE_FRAMES, dv->done) < 0) {
                /* This usually means that ring buffer overflowed.
                 * We have to reset :(.
                 */
  
                fprintf(stderr, "DV1394: Ring buffer overflow. Reseting ..\n");
 
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
        dv->done  = 0;

        if (s.dropped_frames) {
            fprintf(stderr, "DV1394: Frame drop detected (%d). Reseting ..\n",
                    s.dropped_frames);

            dv1394_reset(dv);
            dv1394_start(dv);
        }
    }

#ifdef DV1394_DEBUG
    fprintf(stderr, "index %d, avail %d, done %d\n", dv->index, dv->avail,
            dv->done);
#endif

    return __get_frame(dv, pkt);
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
