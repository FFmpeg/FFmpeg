/*
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

#include "libavutil/avassert.h"
#include "avdevice.h"
#include "config.h"

unsigned avdevice_version(void)
{
    av_assert0(LIBAVDEVICE_VERSION_MICRO >= 100);
    return LIBAVDEVICE_VERSION_INT;
}

const char * avdevice_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char * avdevice_license(void)
{
#define LICENSE_PREFIX "libavdevice license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

static void *av_device_next(void *prev, int output,
                            AVClassCategory c1, AVClassCategory c2)
{
    const AVClass *pc;
    AVClassCategory category = AV_CLASS_CATEGORY_NA;
    do {
        if (output) {
            if (!(prev = av_oformat_next(prev)))
                break;
            pc = ((AVOutputFormat *)prev)->priv_class;
        } else {
            if (!(prev = av_iformat_next(prev)))
                break;
            pc = ((AVInputFormat *)prev)->priv_class;
        }
        if (!pc)
            continue;
        category = pc->category;
    } while (category != c1 && category != c2);
    return prev;
}

AVInputFormat *av_input_audio_device_next(AVInputFormat  *d)
{
    return av_device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
                          AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVInputFormat *av_input_video_device_next(AVInputFormat  *d)
{
    return av_device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
                          AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVOutputFormat *av_output_audio_device_next(AVOutputFormat *d)
{
    return av_device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
                          AV_CLASS_CATEGORY_DEVICE_OUTPUT);
}

AVOutputFormat *av_output_video_device_next(AVOutputFormat *d)
{
    return av_device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
                          AV_CLASS_CATEGORY_DEVICE_OUTPUT);
}

int avdevice_app_to_dev_control_message(struct AVFormatContext *s, enum AVAppToDevMessageType type,
                                        void *data, size_t data_size)
{
    if (!s->oformat || !s->oformat->control_message)
        return AVERROR(ENOSYS);
    return s->oformat->control_message(s, type, data, data_size);
}

int avdevice_dev_to_app_control_message(struct AVFormatContext *s, enum AVDevToAppMessageType type,
                                        void *data, size_t data_size)
{
    if (!s->control_message_cb)
        return AVERROR(ENOSYS);
    return s->control_message_cb(s, type, data, data_size);
}

int avdevice_list_devices(AVFormatContext *s, AVDeviceInfoList **device_list)
{
    int ret;
    av_assert0(s);
    av_assert0(device_list);
    av_assert0(s->oformat || s->iformat);
    if ((s->oformat && !s->oformat->get_device_list) ||
        (s->iformat && !s->iformat->get_device_list)) {
        *device_list = NULL;
        return AVERROR(ENOSYS);
    }
    *device_list = av_mallocz(sizeof(AVDeviceInfoList));
    if (!(*device_list))
        return AVERROR(ENOMEM);
    if (s->oformat)
        ret = s->oformat->get_device_list(s, *device_list);
    else
        ret = s->iformat->get_device_list(s, *device_list);
    if (ret < 0)
        avdevice_free_list_devices(device_list);
    return ret;
}

void avdevice_free_list_devices(AVDeviceInfoList **device_list)
{
    AVDeviceInfoList *list;
    AVDeviceInfo *dev;
    int i;

    av_assert0(device_list);
    list = *device_list;
    if (!list)
        return;

    for (i = 0; i < list->nb_devices; i++) {
        dev = list->devices[i];
        if (dev) {
            av_free(dev->device_name);
            av_free(dev->device_description);
            av_free(dev);
        }
    }
    av_free(list->devices);
    av_freep(device_list);
}
