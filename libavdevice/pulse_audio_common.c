/*
 * Pulseaudio common
 * Copyright (c) 2014 Lukasz Marek
 * Copyright (c) 2011 Luca Barbato <lu_zero@gentoo.org>
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

#include "pulse_audio_common.h"
#include "libavutil/attributes.h"
#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/avassert.h"

pa_sample_format_t av_cold ff_codec_id_to_pulse_format(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_PCM_U8:    return PA_SAMPLE_U8;
    case AV_CODEC_ID_PCM_ALAW:  return PA_SAMPLE_ALAW;
    case AV_CODEC_ID_PCM_MULAW: return PA_SAMPLE_ULAW;
    case AV_CODEC_ID_PCM_S16LE: return PA_SAMPLE_S16LE;
    case AV_CODEC_ID_PCM_S16BE: return PA_SAMPLE_S16BE;
    case AV_CODEC_ID_PCM_F32LE: return PA_SAMPLE_FLOAT32LE;
    case AV_CODEC_ID_PCM_F32BE: return PA_SAMPLE_FLOAT32BE;
    case AV_CODEC_ID_PCM_S32LE: return PA_SAMPLE_S32LE;
    case AV_CODEC_ID_PCM_S32BE: return PA_SAMPLE_S32BE;
    case AV_CODEC_ID_PCM_S24LE: return PA_SAMPLE_S24LE;
    case AV_CODEC_ID_PCM_S24BE: return PA_SAMPLE_S24BE;
    default:                    return PA_SAMPLE_INVALID;
    }
}

enum PulseAudioContextState {
    PULSE_CONTEXT_INITIALIZING,
    PULSE_CONTEXT_READY,
    PULSE_CONTEXT_FINISHED
};

typedef struct PulseAudioDeviceList {
    AVDeviceInfoList *devices;
    int error_code;
    int output;
    char *default_device;
} PulseAudioDeviceList;

static void pa_state_cb(pa_context *c, void *userdata)
{
    enum PulseAudioContextState *context_state = userdata;

    switch  (pa_context_get_state(c)) {
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        *context_state = PULSE_CONTEXT_FINISHED;
        break;
    case PA_CONTEXT_READY:
        *context_state = PULSE_CONTEXT_READY;
        break;
    default:
        break;
    }
}

void ff_pulse_audio_disconnect_context(pa_mainloop **pa_ml, pa_context **pa_ctx)
{
    av_assert0(pa_ml);
    av_assert0(pa_ctx);

    if (*pa_ctx) {
        pa_context_set_state_callback(*pa_ctx, NULL, NULL);
        pa_context_disconnect(*pa_ctx);
        pa_context_unref(*pa_ctx);
    }
    if (*pa_ml)
        pa_mainloop_free(*pa_ml);
    *pa_ml = NULL;
    *pa_ctx = NULL;
}

int ff_pulse_audio_connect_context(pa_mainloop **pa_ml, pa_context **pa_ctx,
                                   const char *server, const char *description)
{
    int ret;
    pa_mainloop_api *pa_mlapi = NULL;
    enum PulseAudioContextState context_state = PULSE_CONTEXT_INITIALIZING;

    av_assert0(pa_ml);
    av_assert0(pa_ctx);

    *pa_ml = NULL;
    *pa_ctx = NULL;

    if (!(*pa_ml = pa_mainloop_new()))
        return AVERROR(ENOMEM);
    if (!(pa_mlapi = pa_mainloop_get_api(*pa_ml))) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    if (!(*pa_ctx = pa_context_new(pa_mlapi, description))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    pa_context_set_state_callback(*pa_ctx, pa_state_cb, &context_state);
    if (pa_context_connect(*pa_ctx, server, 0, NULL) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    while (context_state == PULSE_CONTEXT_INITIALIZING)
        pa_mainloop_iterate(*pa_ml, 1, NULL);
    if (context_state == PULSE_CONTEXT_FINISHED) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    return 0;

  fail:
    ff_pulse_audio_disconnect_context(pa_ml, pa_ctx);
    return ret;
}

static void pulse_add_detected_device(PulseAudioDeviceList *info,
                                      const char *name, const char *description)
{
    int ret;
    AVDeviceInfo *new_device = NULL;

    if (info->error_code)
        return;

    new_device = av_mallocz(sizeof(AVDeviceInfo));
    if (!new_device) {
        info->error_code = AVERROR(ENOMEM);
        return;
    }

    new_device->device_description = av_strdup(description);
    new_device->device_name = av_strdup(name);

    if (!new_device->device_description || !new_device->device_name) {
        info->error_code = AVERROR(ENOMEM);
        goto fail;
    }

    if ((ret = av_dynarray_add_nofree(&info->devices->devices,
                                      &info->devices->nb_devices, new_device)) < 0) {
        info->error_code = ret;
        goto fail;
    }
    return;

  fail:
    av_free(new_device->device_description);
    av_free(new_device->device_name);
    av_free(new_device);

}

static void pulse_audio_source_device_cb(pa_context *c, const pa_source_info *dev,
                                         int eol, void *userdata)
{
    if (!eol)
        pulse_add_detected_device(userdata, dev->name, dev->description);
}

static void pulse_audio_sink_device_cb(pa_context *c, const pa_sink_info *dev,
                                       int eol, void *userdata)
{
    if (!eol)
        pulse_add_detected_device(userdata, dev->name, dev->description);
}

static void pulse_server_info_cb(pa_context *c, const pa_server_info *i, void *userdata)
{
    PulseAudioDeviceList *info = userdata;
    if (info->output)
        info->default_device = av_strdup(i->default_sink_name);
    else
        info->default_device = av_strdup(i->default_source_name);
    if (!info->default_device)
        info->error_code = AVERROR(ENOMEM);
}

int ff_pulse_audio_get_devices(AVDeviceInfoList *devices, const char *server, int output)
{
    pa_mainloop *pa_ml = NULL;
    pa_operation *pa_op = NULL;
    pa_context *pa_ctx = NULL;
    enum pa_operation_state op_state;
    PulseAudioDeviceList dev_list = { 0 };
    int i;

    dev_list.output = output;
    dev_list.devices = devices;
    if (!devices)
        return AVERROR(EINVAL);
    devices->nb_devices = 0;
    devices->devices = NULL;

    if ((dev_list.error_code = ff_pulse_audio_connect_context(&pa_ml, &pa_ctx, server, "Query devices")) < 0)
        goto fail;

    if (output)
        pa_op = pa_context_get_sink_info_list(pa_ctx, pulse_audio_sink_device_cb, &dev_list);
    else
        pa_op = pa_context_get_source_info_list(pa_ctx, pulse_audio_source_device_cb, &dev_list);
    while ((op_state = pa_operation_get_state(pa_op)) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(pa_ml, 1, NULL);
    if (op_state != PA_OPERATION_DONE)
        dev_list.error_code = AVERROR_EXTERNAL;
    pa_operation_unref(pa_op);
    if (dev_list.error_code < 0)
        goto fail;

    pa_op = pa_context_get_server_info(pa_ctx, pulse_server_info_cb, &dev_list);
    while ((op_state = pa_operation_get_state(pa_op)) == PA_OPERATION_RUNNING)
        pa_mainloop_iterate(pa_ml, 1, NULL);
    if (op_state != PA_OPERATION_DONE)
        dev_list.error_code = AVERROR_EXTERNAL;
    pa_operation_unref(pa_op);
    if (dev_list.error_code < 0)
        goto fail;

    devices->default_device = -1;
    for (i = 0; i < devices->nb_devices; i++) {
        if (!strcmp(devices->devices[i]->device_name, dev_list.default_device)) {
            devices->default_device = i;
            break;
        }
    }

  fail:
    av_free(dev_list.default_device);
    ff_pulse_audio_disconnect_context(&pa_ml, &pa_ctx);
    return dev_list.error_code;
}
