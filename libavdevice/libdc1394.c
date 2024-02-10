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

#include <dc1394/dc1394.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

#include "libavformat/avformat.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"

typedef struct dc1394_data {
    AVClass *class;
    dc1394_t *d;
    dc1394camera_t *camera;
    dc1394video_frame_t *frame;
    int current_frame;
    int  frame_rate;        /**< frames per 1000 seconds (fps * 1000) */
    char *video_size;       /**< String describing video size, set by a private option. */
    char *pixel_format;     /**< Set by a private option. */
    char *framerate;        /**< Set by a private option. */

    int size;
    int stream_index;
} dc1394_data;

static const struct dc1394_frame_format {
    int width;
    int height;
    enum AVPixelFormat pix_fmt;
    int frame_size_id;
} dc1394_frame_formats[] = {
    { 320, 240, AV_PIX_FMT_UYVY422,   DC1394_VIDEO_MODE_320x240_YUV422 },
    { 640, 480, AV_PIX_FMT_GRAY8,     DC1394_VIDEO_MODE_640x480_MONO8 },
    { 640, 480, AV_PIX_FMT_UYYVYY411, DC1394_VIDEO_MODE_640x480_YUV411 },
    { 640, 480, AV_PIX_FMT_UYVY422,   DC1394_VIDEO_MODE_640x480_YUV422 },
    { 0, 0, 0, 0 } /* gotta be the last one */
};

static const struct dc1394_frame_rate {
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

#define OFFSET(x) offsetof(dc1394_data, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size), AV_OPT_TYPE_STRING, {.str = "qvga"}, 0, 0, DEC },
    { "pixel_format", "", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = "uyvy422"}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "ntsc"}, 0, 0, DEC },
    { NULL },
};

static const AVClass libdc1394_class = {
    .class_name = "libdc1394 indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};


static inline int dc1394_read_common(AVFormatContext *c,
                                     const struct dc1394_frame_format **select_fmt, const struct dc1394_frame_rate **select_fps)
{
    dc1394_data* dc1394 = c->priv_data;
    AVStream* vst;
    const struct dc1394_frame_format *fmt;
    const struct dc1394_frame_rate *fps;
    enum AVPixelFormat pix_fmt;
    int width, height;
    AVRational framerate;
    int ret = 0;

    if ((pix_fmt = av_get_pix_fmt(dc1394->pixel_format)) == AV_PIX_FMT_NONE) {
        av_log(c, AV_LOG_ERROR, "No such pixel format: %s.\n", dc1394->pixel_format);
        ret = AVERROR(EINVAL);
        goto out;
    }

    if ((ret = av_parse_video_size(&width, &height, dc1394->video_size)) < 0) {
        av_log(c, AV_LOG_ERROR, "Could not parse video size '%s'.\n", dc1394->video_size);
        goto out;
    }
    if ((ret = av_parse_video_rate(&framerate, dc1394->framerate)) < 0) {
        av_log(c, AV_LOG_ERROR, "Could not parse framerate '%s'.\n", dc1394->framerate);
        goto out;
    }
    dc1394->frame_rate = av_rescale(1000, framerate.num, framerate.den);

    for (fmt = dc1394_frame_formats; fmt->width; fmt++)
         if (fmt->pix_fmt == pix_fmt && fmt->width == width && fmt->height == height)
             break;

    for (fps = dc1394_frame_rates; fps->frame_rate; fps++)
         if (fps->frame_rate == dc1394->frame_rate)
             break;

    if (!fps->frame_rate || !fmt->width) {
        av_log(c, AV_LOG_ERROR, "Can't find matching camera format for %s, %dx%d@%d:1000fps\n", av_get_pix_fmt_name(pix_fmt),
                                                                                                width, height, dc1394->frame_rate);
        ret = AVERROR(EINVAL);
        goto out;
    }

    /* create a video stream */
    vst = avformat_new_stream(c, NULL);
    if (!vst) {
        ret = AVERROR(ENOMEM);
        goto out;
    }
    avpriv_set_pts_info(vst, 64, 1, 1000);
    vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    vst->codecpar->width = fmt->width;
    vst->codecpar->height = fmt->height;
    vst->codecpar->format = fmt->pix_fmt;
    vst->avg_frame_rate = framerate;

    dc1394->current_frame = 0;
    dc1394->stream_index = vst->index;
    dc1394->size = av_image_get_buffer_size(fmt->pix_fmt,
                                            fmt->width, fmt->height, 1);

    vst->codecpar->bit_rate = av_rescale(dc1394->size * 8,
                                         fps->frame_rate, 1000);
    *select_fps = fps;
    *select_fmt = fmt;
out:
    return ret;
}

static int dc1394_read_header(AVFormatContext *c)
{
    dc1394_data* dc1394 = c->priv_data;
    dc1394camera_list_t *list;
    int res, i;
    const struct dc1394_frame_format *fmt = NULL;
    const struct dc1394_frame_rate *fps = NULL;

    if (dc1394_read_common(c, &fmt, &fps) != 0)
       return -1;

    /* Now let us prep the hardware. */
    dc1394->d = dc1394_new();
    if (dc1394_camera_enumerate(dc1394->d, &list) != DC1394_SUCCESS || !list) {
        av_log(c, AV_LOG_ERROR, "Unable to look for an IIDC camera.\n");
        goto out;
    }

    if (list->num == 0) {
        av_log(c, AV_LOG_ERROR, "No cameras found.\n");
        dc1394_camera_free_list(list);
        goto out;
    }

    /* FIXME: To select a specific camera I need to search in list its guid */
    dc1394->camera = dc1394_camera_new (dc1394->d, list->ids[0].guid);

    if (!dc1394->camera) {
         av_log(c, AV_LOG_ERROR, "Unable to open camera with guid 0x%"PRIx64"\n",
                list->ids[0].guid);
         dc1394_camera_free_list(list);
         goto out;
    }

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

static int dc1394_read_packet(AVFormatContext *c, AVPacket *pkt)
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
        pkt->data = (uint8_t *)dc1394->frame->image;
        pkt->size = dc1394->frame->image_bytes;
        pkt->pts = dc1394->current_frame * 1000000 / dc1394->frame_rate;
        pkt->flags |= AV_PKT_FLAG_KEY;
        pkt->stream_index = dc1394->stream_index;
    } else {
        av_log(c, AV_LOG_ERROR, "DMA capture failed\n");
        return AVERROR_INVALIDDATA;
    }

    return pkt->size;
}

static int dc1394_close(AVFormatContext * context)
{
    struct dc1394_data *dc1394 = context->priv_data;

    dc1394_video_set_transmission(dc1394->camera, DC1394_OFF);
    dc1394_capture_stop(dc1394->camera);
    dc1394_camera_free(dc1394->camera);
    dc1394_free(dc1394->d);

    return 0;
}

const FFInputFormat ff_libdc1394_demuxer = {
    .p.name         = "libdc1394",
    .p.long_name    = NULL_IF_CONFIG_SMALL("dc1394 v.2 A/V grab"),
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &libdc1394_class,
    .priv_data_size = sizeof(struct dc1394_data),
    .read_header    = dc1394_read_header,
    .read_packet    = dc1394_read_packet,
    .read_close     = dc1394_close,
};
