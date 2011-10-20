/*
 * IIDC1394 grab interface (uses libdc1394 and libraw1394)
 * Copyright (c) 2004 Roman Shaposhnik
 * Copyright (c) 2008 Alessandro Sappia
 * Copyright (c) 2011 Martin Lambers
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
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avdevice.h"

#include <stdlib.h>
#include <string.h>
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"

#include <dc1394/dc1394.h>

#undef free

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

    AVPacket packet;
} dc1394_data;

/* The list of color codings that we support.
 * We assume big endian for the dc1394 16bit modes: libdc1394 never sets the
 * flag little_endian in dc1394video_frame_t. */
struct dc1394_color_coding {
    int pix_fmt;
    int score;
    uint32_t coding;
} dc1394_color_codings[] = {
    { PIX_FMT_GRAY16BE,  1000, DC1394_COLOR_CODING_MONO16 },
    { PIX_FMT_RGB48BE,   1100, DC1394_COLOR_CODING_RGB16  },
    { PIX_FMT_GRAY8,     1200, DC1394_COLOR_CODING_MONO8  },
    { PIX_FMT_RGB24,     1300, DC1394_COLOR_CODING_RGB8   },
    { PIX_FMT_UYYVYY411, 1400, DC1394_COLOR_CODING_YUV411 },
    { PIX_FMT_UYVY422,   1500, DC1394_COLOR_CODING_YUV422 },
    { PIX_FMT_NONE, 0, 0 } /* gotta be the last one */
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
};

static int dc1394_read_header(AVFormatContext *c, AVFormatParameters * ap)
{
    dc1394_data* dc1394 = c->priv_data;
    AVStream *vst;
    const struct dc1394_color_coding *cc;
    const struct dc1394_frame_rate *fr;
    dc1394camera_list_t *list;
    dc1394video_modes_t video_modes;
    dc1394video_mode_t video_mode;
    dc1394framerates_t frame_rates;
    dc1394framerate_t frame_rate;
    uint32_t dc1394_width, dc1394_height, dc1394_color_coding;
    int rate, best_rate;
    int score, max_score;
    int final_width, final_height, final_pix_fmt, final_frame_rate;
    int res, i, j;
    int ret=-1;

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

    /* Get the list of video modes supported by the camera. */
    res = dc1394_video_get_supported_modes (dc1394->camera, &video_modes);
    if (res != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Could not get video formats.\n");
        goto out_camera;
    }

    if (dc1394->pixel_format) {
        if ((ap->pix_fmt = av_get_pix_fmt(dc1394->pixel_format)) == PIX_FMT_NONE) {
            av_log(c, AV_LOG_ERROR, "No such pixel format: %s.\n", dc1394->pixel_format);
            ret = AVERROR(EINVAL);
            goto out;
        }
    }

    if (dc1394->video_size) {
        if ((ret = av_parse_video_size(&ap->width, &ap->height, dc1394->video_size)) < 0) {
            av_log(c, AV_LOG_ERROR, "Couldn't parse video size.\n");
            goto out;
        }
    }

    /* Choose the best mode. */
    rate = (ap->time_base.num ? av_rescale(1000, ap->time_base.den, ap->time_base.num) : -1);
    max_score = -1;
    for (i = 0; i < video_modes.num; i++) {
        if (video_modes.modes[i] == DC1394_VIDEO_MODE_EXIF
                || (video_modes.modes[i] >= DC1394_VIDEO_MODE_FORMAT7_MIN
                    && video_modes.modes[i] <= DC1394_VIDEO_MODE_FORMAT7_MAX)) {
            /* These modes are currently not supported as they would require
             * much more work. For the remaining modes, the functions
             * dc1394_get_image_size_from_video_mode and
             * dc1394_get_color_coding_from_video_mode do not need to query the
             * camera, and thus cannot fail. */
            continue;
        }
        dc1394_get_color_coding_from_video_mode (NULL, video_modes.modes[i],
                &dc1394_color_coding);
        for (cc = dc1394_color_codings; cc->pix_fmt != PIX_FMT_NONE; cc++)
            if (cc->coding == dc1394_color_coding)
                break;
        if (cc->pix_fmt == PIX_FMT_NONE) {
            /* We currently cannot handle this color coding. */
            continue;
        }
        /* Here we know that the mode is supported. Get its frame size and the list
         * of frame rates supported by the camera for this mode. This list is sorted
         * in ascending order according to libdc1394 example programs. */
        dc1394_get_image_size_from_video_mode (NULL, video_modes.modes[i],
                &dc1394_width, &dc1394_height);
        res = dc1394_video_get_supported_framerates (dc1394->camera, video_modes.modes[i],
                &frame_rates);
        if (res != DC1394_SUCCESS || frame_rates.num == 0) {
            av_log(c, AV_LOG_ERROR, "Cannot get frame rates for video mode.\n");
            goto out_camera;
        }
        /* Choose the best frame rate. */
        best_rate = -1;
        for (j = 0; j < frame_rates.num; j++) {
            for (fr = dc1394_frame_rates; fr->frame_rate; fr++) {
                if (fr->frame_rate_id == frame_rates.framerates[j]) {
                    break;
                }
            }
            if (!fr->frame_rate) {
                /* This frame rate is not supported. */
                continue;
            }
            best_rate = fr->frame_rate;
            frame_rate = fr->frame_rate_id;
            if (ap->time_base.num && rate == fr->frame_rate) {
                /* This is the requested frame rate. */
                break;
            }
        }
        if (best_rate == -1) {
            /* No supported rate found. */
            continue;
        }
        /* Here we know that both the mode and the rate are supported. Compute score. */
        if (ap->width && ap->height
                && (dc1394_width == ap->width && dc1394_height == ap->height)) {
            score = 110000;
        } else {
            score = dc1394_width * 10;  // 1600 - 16000
        }
        if (ap->pix_fmt == cc->pix_fmt) {
            score += 90000;
        } else {
            score += cc->score;         // 1000 - 1500
        }
        if (ap->time_base.num && rate == best_rate) {
            score += 70000;
        } else {
            score += best_rate / 1000;  // 1 - 240
        }
        if (score > max_score) {
            video_mode = video_modes.modes[i];
            final_width = dc1394_width;
            final_height = dc1394_height;
            final_pix_fmt = cc->pix_fmt;
            final_frame_rate = best_rate;
            max_score = score;
        }
    }
    if (max_score == -1) {
        av_log(c, AV_LOG_ERROR, "No suitable video mode / frame rate available.\n");
        goto out_camera;
    }
    if (ap->width && ap->height && !(ap->width == final_width && ap->height == final_height)) {
        av_log(c, AV_LOG_WARNING, "Requested frame size is not available, using fallback.\n");
    }
    if (ap->pix_fmt != PIX_FMT_NONE && ap->pix_fmt != final_pix_fmt) {
        av_log(c, AV_LOG_WARNING, "Requested pixel format is not supported, using fallback.\n");
    }
    if (ap->time_base.num && rate != final_frame_rate) {
        av_log(c, AV_LOG_WARNING, "Requested frame rate is not available, using fallback.\n");
    }

    /* create a video stream */
    vst = avformat_new_stream(c, NULL);
    if (!vst)
        goto out_camera;
    av_set_pts_info(vst, 64, 1, 1000);
    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id = CODEC_ID_RAWVIDEO;
    vst->codec->time_base.den = final_frame_rate;
    vst->codec->time_base.num = 1000;
    vst->codec->width = final_width;
    vst->codec->height = final_height;
    vst->codec->pix_fmt = final_pix_fmt;

    /* packet init */
    av_init_packet(&dc1394->packet);
    dc1394->packet.size = avpicture_get_size(final_pix_fmt, final_width, final_height);
    dc1394->packet.stream_index = vst->index;
    dc1394->packet.flags |= AV_PKT_FLAG_KEY;

    dc1394->current_frame = 0;
    dc1394->frame_rate = final_frame_rate;

    vst->codec->bit_rate = av_rescale(dc1394->packet.size * 8, final_frame_rate, 1000);

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

    if (dc1394_video_set_mode(dc1394->camera, video_mode) != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Couldn't set video format\n");
        goto out_camera;
    }

    if (dc1394_video_set_framerate(dc1394->camera, frame_rate) != DC1394_SUCCESS) {
        av_log(c, AV_LOG_ERROR, "Could not set framerate %d.\n", final_frame_rate);
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
    return ret;
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
        dc1394->packet.data = (uint8_t *)(dc1394->frame->image);
        dc1394->packet.pts = (dc1394->current_frame  * 1000000) / (dc1394->frame_rate);
        res = dc1394->frame->image_bytes;
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

    dc1394_video_set_transmission(dc1394->camera, DC1394_OFF);
    dc1394_capture_stop(dc1394->camera);
    dc1394_camera_free(dc1394->camera);
    dc1394_free(dc1394->d);

    return 0;
}

AVInputFormat ff_libdc1394_demuxer = {
    .name           = "libdc1394",
    .long_name      = NULL_IF_CONFIG_SMALL("dc1394 A/V grab"),
    .priv_data_size = sizeof(struct dc1394_data),
    .read_header    = dc1394_read_header,
    .read_packet    = dc1394_read_packet,
    .read_close     = dc1394_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &libdc1394_class,
};
