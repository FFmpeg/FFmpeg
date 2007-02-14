/*
 * IIDC1394 grab interface (uses libdc1394 and libraw1394)
 * Copyright (c) 2004 Roman Shaposhnik
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

#include "avformat.h"

#include <libraw1394/raw1394.h>
#include <libdc1394/dc1394_control.h>

#undef free

typedef struct dc1394_data {
    raw1394handle_t handle;
    dc1394_cameracapture camera;
    int current_frame;
    int fps;

    AVPacket packet;
} dc1394_data;

struct dc1394_frame_format {
    int width;
    int height;
    enum PixelFormat pix_fmt;
    int frame_size_id;
} dc1394_frame_formats[] = {
    { 320, 240, PIX_FMT_UYVY422, MODE_320x240_YUV422 },
    { 640, 480, PIX_FMT_UYYVYY411, MODE_640x480_YUV411 },
    { 640, 480, PIX_FMT_UYVY422, MODE_640x480_YUV422 },
    {   0,   0, 0, MODE_320x240_YUV422 } /* default -- gotta be the last one */
};

struct dc1394_frame_rate {
    int frame_rate;
    int frame_rate_id;
} dc1394_frame_rates[] = {
    {  1875, FRAMERATE_1_875 },
    {  3750, FRAMERATE_3_75  },
    {  7500, FRAMERATE_7_5   },
    { 15000, FRAMERATE_15    },
    { 30000, FRAMERATE_30    },
    { 60000, FRAMERATE_60    },
    {     0, FRAMERATE_30    } /* default -- gotta be the last one */
};

static int dc1394_read_header(AVFormatContext *c, AVFormatParameters * ap)
{
    dc1394_data* dc1394 = c->priv_data;
    AVStream* vst;
    nodeid_t* camera_nodes;
    int res;
    struct dc1394_frame_format *fmt;
    struct dc1394_frame_rate *fps;

    for (fmt = dc1394_frame_formats; fmt->width; fmt++)
         if (fmt->pix_fmt == ap->pix_fmt && fmt->width == ap->width && fmt->height == ap->height)
             break;

    for (fps = dc1394_frame_rates; fps->frame_rate; fps++)
         if (fps->frame_rate == av_rescale(1000, ap->time_base.den, ap->time_base.num))
             break;

    /* create a video stream */
    vst = av_new_stream(c, 0);
    if (!vst)
        return -1;
    av_set_pts_info(vst, 64, 1, 1000);
    vst->codec->codec_type = CODEC_TYPE_VIDEO;
    vst->codec->codec_id = CODEC_ID_RAWVIDEO;
    vst->codec->time_base.den = fps->frame_rate;
    vst->codec->time_base.num = 1000;
    vst->codec->width = fmt->width;
    vst->codec->height = fmt->height;
    vst->codec->pix_fmt = fmt->pix_fmt;

    /* packet init */
    av_init_packet(&dc1394->packet);
    dc1394->packet.size = avpicture_get_size(fmt->pix_fmt, fmt->width, fmt->height);
    dc1394->packet.stream_index = vst->index;
    dc1394->packet.flags |= PKT_FLAG_KEY;

    dc1394->current_frame = 0;
    dc1394->fps = fps->frame_rate;

    vst->codec->bit_rate = av_rescale(dc1394->packet.size * 8, fps->frame_rate, 1000);

    /* Now lets prep the hardware */
    dc1394->handle = dc1394_create_handle(0); /* FIXME: gotta have ap->port */
    if (!dc1394->handle) {
        av_log(c, AV_LOG_ERROR, "Can't acquire dc1394 handle on port %d\n", 0 /* ap->port */);
        goto out;
    }
    camera_nodes = dc1394_get_camera_nodes(dc1394->handle, &res, 1);
    if (!camera_nodes || camera_nodes[ap->channel] == DC1394_NO_CAMERA) {
        av_log(c, AV_LOG_ERROR, "There's no IIDC camera on the channel %d\n", ap->channel);
        goto out_handle;
    }
    res = dc1394_dma_setup_capture(dc1394->handle, camera_nodes[ap->channel],
                                   0,
                                   FORMAT_VGA_NONCOMPRESSED,
                                   fmt->frame_size_id,
                                   SPEED_400,
                                   fps->frame_rate_id, 8, 1,
                                   c->filename,
                                   &dc1394->camera);
    dc1394_free_camera_nodes(camera_nodes);
    if (res != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Can't prepare camera for the DMA capture\n");
        goto out_handle;
    }

    res = dc1394_start_iso_transmission(dc1394->handle, dc1394->camera.node);
    if (res != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Can't start isochronous transmission\n");
        goto out_handle_dma;
    }

    return 0;

out_handle_dma:
    dc1394_dma_unlisten(dc1394->handle, &dc1394->camera);
    dc1394_dma_release_camera(dc1394->handle, &dc1394->camera);
out_handle:
    dc1394_destroy_handle(dc1394->handle);
out:
    return -1;
}

static int dc1394_read_packet(AVFormatContext *c, AVPacket *pkt)
{
    struct dc1394_data *dc1394 = c->priv_data;
    int res;

    /* discard stale frame */
    if (dc1394->current_frame++) {
        if (dc1394_dma_done_with_buffer(&dc1394->camera) != DC1394_SUCCESS)
            av_log(c, AV_LOG_ERROR, "failed to release %d frame\n", dc1394->current_frame);
    }

    res = dc1394_dma_single_capture(&dc1394->camera);

    if (res == DC1394_SUCCESS) {
        dc1394->packet.data = (uint8_t *)(dc1394->camera.capture_buffer);
        dc1394->packet.pts = (dc1394->current_frame * 1000000) / dc1394->fps;
        res = dc1394->packet.size;
    } else {
        av_log(c, AV_LOG_ERROR, "DMA capture failed\n");
        dc1394->packet.data = NULL;
        res = -1;
    }

    *pkt = dc1394->packet;
    return res;
}

static int dc1394_close(AVFormatContext * context)
{
    struct dc1394_data *dc1394 = context->priv_data;

    dc1394_stop_iso_transmission(dc1394->handle, dc1394->camera.node);
    dc1394_dma_unlisten(dc1394->handle, &dc1394->camera);
    dc1394_dma_release_camera(dc1394->handle, &dc1394->camera);
    dc1394_destroy_handle(dc1394->handle);

    return 0;
}

AVInputFormat dc1394_demuxer = {
    .name           = "dc1394",
    .long_name      = "dc1394 A/V grab",
    .priv_data_size = sizeof(struct dc1394_data),
    .read_header    = dc1394_read_header,
    .read_packet    = dc1394_read_packet,
    .read_close     = dc1394_close,
    .flags          = AVFMT_NOFILE
};
