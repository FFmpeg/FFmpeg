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
#include "libavutil/samplefmt.h"
#include "libavutil/pixfmt.h"
#include "libavcodec/avcodec.h"
#include "avdevice.h"
#include "internal.h"
#include "config.h"

#include "libavutil/ffversion.h"
const char av_device_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM
#define OFFSET(x) offsetof(AVDeviceCapabilitiesQuery, x)

const AVOption av_device_capabilities[] = {
    { "codec", "codec", OFFSET(codec), AV_OPT_TYPE_INT,
        {.i64 = AV_CODEC_ID_NONE}, AV_CODEC_ID_NONE, INT_MAX, E|D|A|V },
    { "sample_format", "sample format", OFFSET(sample_format), AV_OPT_TYPE_SAMPLE_FMT,
        {.i64 = AV_SAMPLE_FMT_NONE}, AV_SAMPLE_FMT_NONE, INT_MAX, E|D|A },
    { "sample_rate", "sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT,
        {.i64 = -1}, -1, INT_MAX, E|D|A },
    { "channels", "channels", OFFSET(channels), AV_OPT_TYPE_INT,
        {.i64 = -1}, -1, INT_MAX, E|D|A },
    { "channel_layout", "channel layout", OFFSET(channel_layout), AV_OPT_TYPE_CHANNEL_LAYOUT,
        {.i64 = -1}, -1, INT_MAX, E|D|A },
    { "pixel_format", "pixel format", OFFSET(pixel_format), AV_OPT_TYPE_PIXEL_FMT,
        {.i64 = AV_PIX_FMT_NONE}, AV_PIX_FMT_NONE, INT_MAX, E|D|V },
    { "window_size", "window size", OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE,
        {.str = NULL}, -1, INT_MAX, E|D|V },
    { "frame_size", "frame size", OFFSET(frame_width), AV_OPT_TYPE_IMAGE_SIZE,
        {.str = NULL}, -1, INT_MAX, E|D|V },
    { "fps", "fps", OFFSET(fps), AV_OPT_TYPE_RATIONAL,
        {.dbl = -1}, -1, INT_MAX, E|D|V },
    { NULL }
};

#undef E
#undef D
#undef A
#undef V
#undef OFFSET

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
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}

static void *device_next(void *prev, int output,
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
    return device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
                       AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVInputFormat *av_input_video_device_next(AVInputFormat  *d)
{
    return device_next(d, 0, AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
                       AV_CLASS_CATEGORY_DEVICE_INPUT);
}

AVOutputFormat *av_output_audio_device_next(AVOutputFormat *d)
{
    return device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
                       AV_CLASS_CATEGORY_DEVICE_OUTPUT);
}

AVOutputFormat *av_output_video_device_next(AVOutputFormat *d)
{
    return device_next(d, 1, AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
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

int avdevice_capabilities_create(AVDeviceCapabilitiesQuery **caps, AVFormatContext *s,
                                 AVDictionary **device_options)
{
    int ret;
    av_assert0(s && caps);
    av_assert0(s->iformat || s->oformat);
    if ((s->oformat && !s->oformat->create_device_capabilities) ||
        (s->iformat && !s->iformat->create_device_capabilities))
        return AVERROR(ENOSYS);
    *caps = av_mallocz(sizeof(**caps));
    if (!(*caps))
        return AVERROR(ENOMEM);
    (*caps)->device_context = s;
    if (((ret = av_opt_set_dict(s->priv_data, device_options)) < 0))
        goto fail;
    if (s->iformat) {
        if ((ret = s->iformat->create_device_capabilities(s, *caps)) < 0)
            goto fail;
    } else {
        if ((ret = s->oformat->create_device_capabilities(s, *caps)) < 0)
            goto fail;
    }
    av_opt_set_defaults(*caps);
    return 0;
  fail:
    av_freep(caps);
    return ret;
}

void avdevice_capabilities_free(AVDeviceCapabilitiesQuery **caps, AVFormatContext *s)
{
    if (!s || !caps || !(*caps))
        return;
    av_assert0(s->iformat || s->oformat);
    if (s->iformat) {
        if (s->iformat->free_device_capabilities)
            s->iformat->free_device_capabilities(s, *caps);
    } else {
        if (s->oformat->free_device_capabilities)
            s->oformat->free_device_capabilities(s, *caps);
    }
    av_freep(caps);
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
    /* no default device by default */
    (*device_list)->default_device = -1;
    if (s->oformat)
        ret = s->oformat->get_device_list(s, *device_list);
    else
        ret = s->iformat->get_device_list(s, *device_list);
    if (ret < 0)
        avdevice_free_list_devices(device_list);
    return ret;
}

static int list_devices_for_context(AVFormatContext *s, AVDictionary *options,
                                    AVDeviceInfoList **device_list)
{
    AVDictionary *tmp = NULL;
    int ret;

    av_dict_copy(&tmp, options, 0);
    if ((ret = av_opt_set_dict2(s, &tmp, AV_OPT_SEARCH_CHILDREN)) < 0)
        goto fail;
    ret = avdevice_list_devices(s, device_list);
  fail:
    av_dict_free(&tmp);
    avformat_free_context(s);
    return ret;
}

int avdevice_list_input_sources(AVInputFormat *device, const char *device_name,
                                AVDictionary *device_options, AVDeviceInfoList **device_list)
{
    AVFormatContext *s = NULL;
    int ret;

    if ((ret = ff_alloc_input_device_context(&s, device, device_name)) < 0)
        return ret;
    return list_devices_for_context(s, device_options, device_list);
}

int avdevice_list_output_sinks(AVOutputFormat *device, const char *device_name,
                               AVDictionary *device_options, AVDeviceInfoList **device_list)
{
    AVFormatContext *s = NULL;
    int ret;

    if ((ret = avformat_alloc_output_context2(&s, device, device_name, NULL)) < 0)
        return ret;
    return list_devices_for_context(s, device_options, device_list);
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
            av_freep(&dev->device_name);
            av_freep(&dev->device_description);
            av_free(dev);
        }
    }
    av_freep(&list->devices);
    av_freep(device_list);
}
