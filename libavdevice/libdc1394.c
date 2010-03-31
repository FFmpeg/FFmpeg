/*
 * IIDC1394 grab interface (uses libdc1394 and libraw1394)
 * Copyright (c) 2004 Roman Shaposhnik
 * Copyright (c) 2008 Alessandro Sappia
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
#include "libavformat/avformat.h"

#if HAVE_LIBDC1394_2
#include <dc1394/dc1394.h>
#elif HAVE_LIBDC1394_1
#include <libraw1394/raw1394.h>
#include <libdc1394/dc1394_control.h>

#define DC1394_VIDEO_MODE_320x240_YUV422 MODE_320x240_YUV422
#define DC1394_VIDEO_MODE_640x480_YUV411 MODE_640x480_YUV411
#define DC1394_VIDEO_MODE_640x480_YUV422 MODE_640x480_YUV422
#define DC1394_FRAMERATE_1_875 FRAMERATE_1_875
#define DC1394_FRAMERATE_3_75  FRAMERATE_3_75
#define DC1394_FRAMERATE_7_5   FRAMERATE_7_5
#define DC1394_FRAMERATE_15    FRAMERATE_15
#define DC1394_FRAMERATE_30    FRAMERATE_30
#define DC1394_FRAMERATE_60    FRAMERATE_60
#define DC1394_FRAMERATE_120   FRAMERATE_120
#define DC1394_FRAMERATE_240   FRAMERATE_240
#endif

#undef free

typedef struct dc1394_data {
#if HAVE_LIBDC1394_1
    raw1394handle_t handle;
    dc1394_cameracapture camera;
#elif HAVE_LIBDC1394_2
    dc1394_t *d;
    dc1394camera_t *camera;
    dc1394video_frame_t *frame;
#endif
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
    { 320, 240, PIX_FMT_UYVY422, DC1394_VIDEO_MODE_320x240_YUV422 },
    { 640, 480, PIX_FMT_UYYVYY411, DC1394_VIDEO_MODE_640x480_YUV411 },
    { 640, 480, PIX_FMT_UYVY422, DC1394_VIDEO_MODE_640x480_YUV422 },
    { 0, 0, 0, 0 } /* gotta be the last one */
};

struct dc1394_frame_rate {
    int frame_rate;
    int frame_rate_id;
} dc1394_frame_rates[] = {
    {  1875, DC1394_FRAMERATE_1_875 },
    {  3750, DC1394_FRAMERATE_3_75  },
    {  7500, DC1394_FRAMERATE_7_5   },
    { 15000, DC1394_FRAMERATE_15    },
    { 30000, DC1394_FRAMERATE_30    },
    { 60000, DC1394_FRAMERATE_60    },
    {120000, DC1394_FRAMERATE_120   },
    {240000, DC1394_FRAMERATE_240    },
    { 0, 0 } /* gotta be the last one */
};

static inline int dc1394_read_common(AVFormatContext *c, AVFormatParameters *ap,
                                     struct dc1394_frame_format **select_fmt, struct dc1394_frame_rate **select_fps)
{
    dc1394_data* dc1394 = c->priv_data;
    AVStream* vst;
    struct dc1394_frame_format *fmt;
    struct dc1394_frame_rate *fps;
    enum PixelFormat pix_fmt = ap->pix_fmt == PIX_FMT_NONE ? PIX_FMT_UYVY422 : ap->pix_fmt; /* defaults */
    int width                = !ap->width ? 320 : ap->width;
    int height               = !ap->height ? 240 : ap->height;
    int frame_rate           = !ap->time_base.num ? 30000 : av_rescale(1000, ap->time_base.den, ap->time_base.num);

    for (fmt = dc1394_frame_formats; fmt->width; fmt++)
         if (fmt->pix_fmt == pix_fmt && fmt->width == width && fmt->height == height)
             break;

    for (fps = dc1394_frame_rates; fps->frame_rate; fps++)
         if (fps->frame_rate == frame_rate)
             break;

    if (!fps->frame_rate || !fmt->width) {
        av_log(c, AV_LOG_ERROR, "Can't find matching camera format for %s, %dx%d@%d:1000fps\n", avcodec_get_pix_fmt_name(pix_fmt),
                                                                                                width, height, frame_rate);
        goto out;
    }

    /* create a video stream */
    vst = av_new_stream(c, 0);
    if (!vst)
        goto out;
    av_set_pts_info(vst, 64, 1, 1000);
    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
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
    dc1394->packet.flags |= AV_PKT_FLAG_KEY;

    dc1394->current_frame = 0;
    dc1394->fps = fps->frame_rate;

    vst->codec->bit_rate = av_rescale(dc1394->packet.size * 8, fps->frame_rate, 1000);
    *select_fps = fps;
    *select_fmt = fmt;
    return 0;
out:
    return -1;
}

#if HAVE_LIBDC1394_1
static int dc1394_v1_read_header(AVFormatContext *c, AVFormatParameters * ap)
{
    dc1394_data* dc1394 = c->priv_data;
    AVStream* vst;
    nodeid_t* camera_nodes;
    int res;
    struct dc1394_frame_format *fmt = NULL;
    struct dc1394_frame_rate *fps = NULL;

    if (dc1394_read_common(c,ap,&fmt,&fps) != 0)
        return -1;

    /* Now let us prep the hardware. */
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

static int dc1394_v1_read_packet(AVFormatContext *c, AVPacket *pkt)
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

static int dc1394_v1_close(AVFormatContext * context)
{
    struct dc1394_data *dc1394 = context->priv_data;

    dc1394_stop_iso_transmission(dc1394->handle, dc1394->camera.node);
    dc1394_dma_unlisten(dc1394->handle, &dc1394->camera);
    dc1394_dma_release_camera(dc1394->handle, &dc1394->camera);
    dc1394_destroy_handle(dc1394->handle);

    return 0;
}

#elif HAVE_LIBDC1394_2
static int dc1394_v2_read_header(AVFormatContext *c, AVFormatParameters * ap)
{
    dc1394_data* dc1394 = c->priv_data;
    dc1394camera_list_t *list;
    int res, i;
    struct dc1394_frame_format *fmt = NULL;
    struct dc1394_frame_rate *fps = NULL;

    if (dc1394_read_common(c,ap,&fmt,&fps) != 0)
       return -1;

    /* Now let us prep the hardware. */
    dc1394->d = dc1394_new();
    dc1394_camera_enumerate (dc1394->d, &list);
    if ( !list || list->num == 0) {
        av_log(c, AV_LOG_ERROR, "Unable to look for an IIDC camera\n\n");
        goto out;
    }

    /* FIXME: To select a specific camera I need to search in list its guid */
    dc1394->camera = dc1394_camera_new (dc1394->d, list->ids[0].guid);
    if (list->num > 1) {
        av_log(c, AV_LOG_INFO, "Working with the first camera found\n");
    }

    /* Freeing list of cameras */
    dc1394_camera_free_list (list);

    /* Select MAX Speed possible from the cam */
    if (dc1394->camera->bmode_capable>0) {
       dc1394_video_set_operation_mode(dc1394->camera, DC1394_OPERATION_MODE_1394B);
       i = DC1394_ISO_SPEED_800;
    } else {
       i = DC1394_ISO_SPEED_400;
    }

    for (res = DC1394_FAILURE; i >= DC1394_ISO_SPEED_MIN && res != DC1394_SUCCESS; i--) {
            res=dc1394_video_set_iso_speed(dc1394->camera, i);
    }
    if (res != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Couldn't set ISO Speed\n");
        goto out_camera;
    }

    if (dc1394_video_set_mode(dc1394->camera, fmt->frame_size_id) != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Couldn't set video format\n");
        goto out_camera;
    }

    if (dc1394_video_set_framerate(dc1394->camera,fps->frame_rate_id) != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Couldn't set framerate %d \n",fps->frame_rate);
        goto out_camera;
    }
    if (dc1394_capture_setup(dc1394->camera, 10, DC1394_CAPTURE_FLAGS_DEFAULT)!=DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Cannot setup camera \n");
        goto out_camera;
    }

    if (dc1394_video_set_transmission(dc1394->camera, DC1394_ON) !=DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Cannot start capture\n");
        goto out_camera;
    }
    return 0;

out_camera:
    dc1394_capture_stop(dc1394->camera);
    dc1394_video_set_transmission(dc1394->camera, DC1394_OFF);
    dc1394_camera_free (dc1394->camera);
out:
    dc1394_free(dc1394->d);
    return -1;
}

static int dc1394_v2_read_packet(AVFormatContext *c, AVPacket *pkt)
{
    struct dc1394_data *dc1394 = c->priv_data;
    int res;

    /* discard stale frame */
    if (dc1394->current_frame++) {
        if (dc1394_capture_enqueue(dc1394->camera, dc1394->frame) != DC1394_SUCCESS)
            av_log(c, AV_LOG_ERROR, "failed to release %d frame\n", dc1394->current_frame);
    }

    res = dc1394_capture_dequeue(dc1394->camera, DC1394_CAPTURE_POLICY_WAIT, &dc1394->frame);
    if (res == DC1394_SUCCESS) {
        dc1394->packet.data = (uint8_t *)(dc1394->frame->image);
        dc1394->packet.pts = (dc1394->current_frame  * 1000000) / (dc1394->fps);
        res = dc1394->frame->image_bytes;
    } else {
        av_log(c, AV_LOG_ERROR, "DMA capture failed\n");
        dc1394->packet.data = NULL;
        res = -1;
    }

    *pkt = dc1394->packet;
    return res;
}

static int dc1394_v2_close(AVFormatContext * context)
{
    struct dc1394_data *dc1394 = context->priv_data;

    dc1394_video_set_transmission(dc1394->camera, DC1394_OFF);
    dc1394_capture_stop(dc1394->camera);
    dc1394_camera_free(dc1394->camera);
    dc1394_free(dc1394->d);

    return 0;
}

AVInputFormat libdc1394_demuxer = {
    .name           = "libdc1394",
    .long_name      = NULL_IF_CONFIG_SMALL("dc1394 v.2 A/V grab"),
    .priv_data_size = sizeof(struct dc1394_data),
    .read_header    = dc1394_v2_read_header,
    .read_packet    = dc1394_v2_read_packet,
    .read_close     = dc1394_v2_close,
    .flags          = AVFMT_NOFILE
};

#endif
#if HAVE_LIBDC1394_1
AVInputFormat libdc1394_demuxer = {
    .name           = "libdc1394",
    .long_name      = NULL_IF_CONFIG_SMALL("dc1394 v.1 A/V grab"),
    .priv_data_size = sizeof(struct dc1394_data),
    .read_header    = dc1394_v1_read_header,
    .read_packet    = dc1394_v1_read_packet,
    .read_close     = dc1394_v1_close,
    .flags          = AVFMT_NOFILE
};
#endif
