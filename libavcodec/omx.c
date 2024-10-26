/*
 * OMX Video encoder
 * Copyright (C) 2011 Martin Storsjo
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

#if CONFIG_OMX_RPI
#define OMX_SKIP64BIT
#endif

#include <dlfcn.h>
#include <OMX_Core.h>
#include <OMX_Component.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "h264.h"
#include "pthread_internal.h"

#ifdef OMX_SKIP64BIT
static OMX_TICKS to_omx_ticks(int64_t value)
{
    OMX_TICKS s;
    s.nLowPart  = value & 0xffffffff;
    s.nHighPart = value >> 32;
    return s;
}
static int64_t from_omx_ticks(OMX_TICKS value)
{
    return (((int64_t)value.nHighPart) << 32) | value.nLowPart;
}
#else
#define to_omx_ticks(x) (x)
#define from_omx_ticks(x) (x)
#endif

#define INIT_STRUCT(x) do {                                               \
        x.nSize = sizeof(x);                                              \
        x.nVersion = s->version;                                          \
    } while (0)
#define CHECK(x) do {                                                     \
        if (x != OMX_ErrorNone) {                                         \
            av_log(avctx, AV_LOG_ERROR,                                   \
                   "err %x (%d) on line %d\n", x, x, __LINE__);           \
            return AVERROR_UNKNOWN;                                       \
        }                                                                 \
    } while (0)

typedef struct OMXContext {
    void *lib;
    void *lib2;
    OMX_ERRORTYPE (*ptr_Init)(void);
    OMX_ERRORTYPE (*ptr_Deinit)(void);
    OMX_ERRORTYPE (*ptr_ComponentNameEnum)(OMX_STRING, OMX_U32, OMX_U32);
    OMX_ERRORTYPE (*ptr_GetHandle)(OMX_HANDLETYPE*, OMX_STRING, OMX_PTR, OMX_CALLBACKTYPE*);
    OMX_ERRORTYPE (*ptr_FreeHandle)(OMX_HANDLETYPE);
    OMX_ERRORTYPE (*ptr_GetComponentsOfRole)(OMX_STRING, OMX_U32*, OMX_U8**);
    OMX_ERRORTYPE (*ptr_GetRolesOfComponent)(OMX_STRING, OMX_U32*, OMX_U8**);
    void (*host_init)(void);
} OMXContext;

static av_cold void *dlsym_prefixed(void *handle, const char *symbol, const char *prefix)
{
    char buf[50];
    snprintf(buf, sizeof(buf), "%s%s", prefix ? prefix : "", symbol);
    return dlsym(handle, buf);
}

static av_cold int omx_try_load(OMXContext *s, void *logctx,
                                const char *libname, const char *prefix,
                                const char *libname2)
{
    if (libname2) {
        s->lib2 = dlopen(libname2, RTLD_NOW | RTLD_GLOBAL);
        if (!s->lib2) {
            av_log(logctx, AV_LOG_WARNING, "%s not found\n", libname2);
            return AVERROR_ENCODER_NOT_FOUND;
        }
        s->host_init = dlsym(s->lib2, "bcm_host_init");
        if (!s->host_init) {
            av_log(logctx, AV_LOG_WARNING, "bcm_host_init not found\n");
            dlclose(s->lib2);
            s->lib2 = NULL;
            return AVERROR_ENCODER_NOT_FOUND;
        }
    }
    s->lib = dlopen(libname, RTLD_NOW | RTLD_GLOBAL);
    if (!s->lib) {
        av_log(logctx, AV_LOG_WARNING, "%s not found\n", libname);
        return AVERROR_ENCODER_NOT_FOUND;
    }
    s->ptr_Init                = dlsym_prefixed(s->lib, "OMX_Init", prefix);
    s->ptr_Deinit              = dlsym_prefixed(s->lib, "OMX_Deinit", prefix);
    s->ptr_ComponentNameEnum   = dlsym_prefixed(s->lib, "OMX_ComponentNameEnum", prefix);
    s->ptr_GetHandle           = dlsym_prefixed(s->lib, "OMX_GetHandle", prefix);
    s->ptr_FreeHandle          = dlsym_prefixed(s->lib, "OMX_FreeHandle", prefix);
    s->ptr_GetComponentsOfRole = dlsym_prefixed(s->lib, "OMX_GetComponentsOfRole", prefix);
    s->ptr_GetRolesOfComponent = dlsym_prefixed(s->lib, "OMX_GetRolesOfComponent", prefix);
    if (!s->ptr_Init || !s->ptr_Deinit || !s->ptr_ComponentNameEnum ||
        !s->ptr_GetHandle || !s->ptr_FreeHandle ||
        !s->ptr_GetComponentsOfRole || !s->ptr_GetRolesOfComponent) {
        av_log(logctx, AV_LOG_WARNING, "Not all functions found in %s\n", libname);
        dlclose(s->lib);
        s->lib = NULL;
        if (s->lib2)
            dlclose(s->lib2);
        s->lib2 = NULL;
        return AVERROR_ENCODER_NOT_FOUND;
    }
    return 0;
}

static av_cold OMXContext *omx_init(void *logctx, const char *libname, const char *prefix)
{
    static const char * const libnames[] = {
#if CONFIG_OMX_RPI
        "/opt/vc/lib/libopenmaxil.so", "/opt/vc/lib/libbcm_host.so",
#else
        "libOMX_Core.so", NULL,
        "libOmxCore.so", NULL,
#endif
        NULL
    };
    const char* const* nameptr;
    int ret = AVERROR_ENCODER_NOT_FOUND;
    OMXContext *omx_context;

    omx_context = av_mallocz(sizeof(*omx_context));
    if (!omx_context)
        return NULL;
    if (libname) {
        ret = omx_try_load(omx_context, logctx, libname, prefix, NULL);
        if (ret < 0) {
            av_free(omx_context);
            return NULL;
        }
    } else {
        for (nameptr = libnames; *nameptr; nameptr += 2)
            if (!(ret = omx_try_load(omx_context, logctx, nameptr[0], prefix, nameptr[1])))
                break;
        if (!*nameptr) {
            av_free(omx_context);
            return NULL;
        }
    }

    if (omx_context->host_init)
        omx_context->host_init();
    omx_context->ptr_Init();
    return omx_context;
}

static av_cold void omx_deinit(OMXContext *omx_context)
{
    if (!omx_context)
        return;
    omx_context->ptr_Deinit();
    dlclose(omx_context->lib);
    av_free(omx_context);
}

typedef struct OMXCodecContext {
    const AVClass *class;
    char *libname;
    char *libprefix;
    OMXContext *omx_context;

    AVCodecContext *avctx;

    char component_name[OMX_MAX_STRINGNAME_SIZE];
    OMX_VERSIONTYPE version;
    OMX_HANDLETYPE handle;
    int in_port, out_port;
    OMX_COLOR_FORMATTYPE color_format;
    int stride, plane_size;

    int num_in_buffers, num_out_buffers;
    OMX_BUFFERHEADERTYPE **in_buffer_headers;
    OMX_BUFFERHEADERTYPE **out_buffer_headers;
    int num_free_in_buffers;
    OMX_BUFFERHEADERTYPE **free_in_buffers;
    int num_done_out_buffers;
    OMX_BUFFERHEADERTYPE **done_out_buffers;
    pthread_mutex_t input_mutex;
    pthread_cond_t input_cond;
    pthread_mutex_t output_mutex;
    pthread_cond_t output_cond;

    pthread_mutex_t state_mutex;
    pthread_cond_t state_cond;
    OMX_STATETYPE state;
    OMX_ERRORTYPE error;

    unsigned mutex_cond_inited_cnt;

    int eos_sent, got_eos;

    uint8_t *output_buf;
    int output_buf_size;

    int input_zerocopy;
    int profile;
} OMXCodecContext;

#define NB_MUTEX_CONDS 6
#define OFF(field) offsetof(OMXCodecContext, field)
DEFINE_OFFSET_ARRAY(OMXCodecContext, omx_codec_context, mutex_cond_inited_cnt,
                    (OFF(input_mutex), OFF(output_mutex), OFF(state_mutex)),
                    (OFF(input_cond),  OFF(output_cond),  OFF(state_cond)));

static void append_buffer(pthread_mutex_t *mutex, pthread_cond_t *cond,
                          int* array_size, OMX_BUFFERHEADERTYPE **array,
                          OMX_BUFFERHEADERTYPE *buffer)
{
    pthread_mutex_lock(mutex);
    array[(*array_size)++] = buffer;
    pthread_cond_broadcast(cond);
    pthread_mutex_unlock(mutex);
}

static OMX_BUFFERHEADERTYPE *get_buffer(pthread_mutex_t *mutex, pthread_cond_t *cond,
                                        int* array_size, OMX_BUFFERHEADERTYPE **array,
                                        int wait)
{
    OMX_BUFFERHEADERTYPE *buffer;
    pthread_mutex_lock(mutex);
    if (wait) {
        while (!*array_size)
           pthread_cond_wait(cond, mutex);
    }
    if (*array_size > 0) {
        buffer = array[0];
        (*array_size)--;
        memmove(&array[0], &array[1], (*array_size) * sizeof(OMX_BUFFERHEADERTYPE*));
    } else {
        buffer = NULL;
    }
    pthread_mutex_unlock(mutex);
    return buffer;
}

static OMX_ERRORTYPE event_handler(OMX_HANDLETYPE component, OMX_PTR app_data, OMX_EVENTTYPE event,
                                   OMX_U32 data1, OMX_U32 data2, OMX_PTR event_data)
{
    OMXCodecContext *s = app_data;
    // This uses casts in the printfs, since OMX_U32 actually is a typedef for
    // unsigned long in official header versions (but there are also modified
    // versions where it is something else).
    switch (event) {
    case OMX_EventError:
        pthread_mutex_lock(&s->state_mutex);
        av_log(s->avctx, AV_LOG_ERROR, "OMX error %"PRIx32"\n", (uint32_t) data1);
        s->error = data1;
        pthread_cond_broadcast(&s->state_cond);
        pthread_mutex_unlock(&s->state_mutex);
        break;
    case OMX_EventCmdComplete:
        if (data1 == OMX_CommandStateSet) {
            pthread_mutex_lock(&s->state_mutex);
            s->state = data2;
            av_log(s->avctx, AV_LOG_VERBOSE, "OMX state changed to %"PRIu32"\n", (uint32_t) data2);
            pthread_cond_broadcast(&s->state_cond);
            pthread_mutex_unlock(&s->state_mutex);
        } else if (data1 == OMX_CommandPortDisable) {
            av_log(s->avctx, AV_LOG_VERBOSE, "OMX port %"PRIu32" disabled\n", (uint32_t) data2);
        } else if (data1 == OMX_CommandPortEnable) {
            av_log(s->avctx, AV_LOG_VERBOSE, "OMX port %"PRIu32" enabled\n", (uint32_t) data2);
        } else {
            av_log(s->avctx, AV_LOG_VERBOSE, "OMX command complete, command %"PRIu32", value %"PRIu32"\n",
                                             (uint32_t) data1, (uint32_t) data2);
        }
        break;
    case OMX_EventPortSettingsChanged:
        av_log(s->avctx, AV_LOG_VERBOSE, "OMX port %"PRIu32" settings changed\n", (uint32_t) data1);
        break;
    default:
        av_log(s->avctx, AV_LOG_VERBOSE, "OMX event %d %"PRIx32" %"PRIx32"\n",
                                         event, (uint32_t) data1, (uint32_t) data2);
        break;
    }
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE empty_buffer_done(OMX_HANDLETYPE component, OMX_PTR app_data,
                                       OMX_BUFFERHEADERTYPE *buffer)
{
    OMXCodecContext *s = app_data;
    if (s->input_zerocopy) {
        if (buffer->pAppPrivate) {
            if (buffer->pOutputPortPrivate)
                av_free(buffer->pAppPrivate);
            else
                av_frame_free((AVFrame**)&buffer->pAppPrivate);
            buffer->pAppPrivate = NULL;
        }
    }
    append_buffer(&s->input_mutex, &s->input_cond,
                  &s->num_free_in_buffers, s->free_in_buffers, buffer);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE fill_buffer_done(OMX_HANDLETYPE component, OMX_PTR app_data,
                                      OMX_BUFFERHEADERTYPE *buffer)
{
    OMXCodecContext *s = app_data;
    append_buffer(&s->output_mutex, &s->output_cond,
                  &s->num_done_out_buffers, s->done_out_buffers, buffer);
    return OMX_ErrorNone;
}

static const OMX_CALLBACKTYPE callbacks = {
    event_handler,
    empty_buffer_done,
    fill_buffer_done
};

static av_cold int find_component(OMXContext *omx_context, void *logctx,
                                  const char *role, char *str, int str_size)
{
    OMX_U32 i, num = 0;
    char **components;
    int ret = 0;

#if CONFIG_OMX_RPI
    if (av_strstart(role, "video_encoder.", NULL)) {
        av_strlcpy(str, "OMX.broadcom.video_encode", str_size);
        return 0;
    }
#endif
    omx_context->ptr_GetComponentsOfRole((OMX_STRING) role, &num, NULL);
    if (!num) {
        av_log(logctx, AV_LOG_WARNING, "No component for role %s found\n", role);
        return AVERROR_ENCODER_NOT_FOUND;
    }
    components = av_calloc(num, sizeof(*components));
    if (!components)
        return AVERROR(ENOMEM);
    for (i = 0; i < num; i++) {
        components[i] = av_mallocz(OMX_MAX_STRINGNAME_SIZE);
        if (!components[i]) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }
    omx_context->ptr_GetComponentsOfRole((OMX_STRING) role, &num, (OMX_U8**) components);
    av_strlcpy(str, components[0], str_size);
end:
    for (i = 0; i < num; i++)
        av_free(components[i]);
    av_free(components);
    return ret;
}

static av_cold int wait_for_state(OMXCodecContext *s, OMX_STATETYPE state)
{
    int ret = 0;
    pthread_mutex_lock(&s->state_mutex);
    while (s->state != state && s->error == OMX_ErrorNone)
        pthread_cond_wait(&s->state_cond, &s->state_mutex);
    if (s->error != OMX_ErrorNone)
        ret = AVERROR_ENCODER_NOT_FOUND;
    pthread_mutex_unlock(&s->state_mutex);
    return ret;
}

static av_cold int omx_component_init(AVCodecContext *avctx, const char *role)
{
    OMXCodecContext *s = avctx->priv_data;
    OMX_PARAM_COMPONENTROLETYPE role_params = { 0 };
    OMX_PORT_PARAM_TYPE video_port_params = { 0 };
    OMX_PARAM_PORTDEFINITIONTYPE in_port_params = { 0 }, out_port_params = { 0 };
    OMX_VIDEO_PARAM_PORTFORMATTYPE video_port_format = { 0 };
    OMX_VIDEO_PARAM_BITRATETYPE vid_param_bitrate = { 0 };
    OMX_ERRORTYPE err;
    int i;

    s->version.s.nVersionMajor = 1;
    s->version.s.nVersionMinor = 1;
    s->version.s.nRevision     = 2;

    err = s->omx_context->ptr_GetHandle(&s->handle, s->component_name, s, (OMX_CALLBACKTYPE*) &callbacks);
    if (err != OMX_ErrorNone) {
        av_log(avctx, AV_LOG_ERROR, "OMX_GetHandle(%s) failed: %x\n", s->component_name, err);
        return AVERROR_UNKNOWN;
    }

    // This one crashes the mediaserver on qcom, if used over IOMX
    INIT_STRUCT(role_params);
    av_strlcpy(role_params.cRole, role, sizeof(role_params.cRole));
    // Intentionally ignore errors on this one
    OMX_SetParameter(s->handle, OMX_IndexParamStandardComponentRole, &role_params);

    INIT_STRUCT(video_port_params);
    err = OMX_GetParameter(s->handle, OMX_IndexParamVideoInit, &video_port_params);
    CHECK(err);

    s->in_port = s->out_port = -1;
    for (i = 0; i < video_port_params.nPorts; i++) {
        int port = video_port_params.nStartPortNumber + i;
        OMX_PARAM_PORTDEFINITIONTYPE port_params = { 0 };
        INIT_STRUCT(port_params);
        port_params.nPortIndex = port;
        err = OMX_GetParameter(s->handle, OMX_IndexParamPortDefinition, &port_params);
        if (err != OMX_ErrorNone) {
            av_log(avctx, AV_LOG_WARNING, "port %d error %x\n", port, err);
            break;
        }
        if (port_params.eDir == OMX_DirInput && s->in_port < 0) {
            in_port_params = port_params;
            s->in_port = port;
        } else if (port_params.eDir == OMX_DirOutput && s->out_port < 0) {
            out_port_params = port_params;
            s->out_port = port;
        }
    }
    if (s->in_port < 0 || s->out_port < 0) {
        av_log(avctx, AV_LOG_ERROR, "No in or out port found (in %d out %d)\n", s->in_port, s->out_port);
        return AVERROR_UNKNOWN;
    }

    s->color_format = 0;
    for (i = 0; ; i++) {
        INIT_STRUCT(video_port_format);
        video_port_format.nIndex = i;
        video_port_format.nPortIndex = s->in_port;
        if (OMX_GetParameter(s->handle, OMX_IndexParamVideoPortFormat, &video_port_format) != OMX_ErrorNone)
            break;
        if (video_port_format.eColorFormat == OMX_COLOR_FormatYUV420Planar ||
            video_port_format.eColorFormat == OMX_COLOR_FormatYUV420PackedPlanar) {
            s->color_format = video_port_format.eColorFormat;
            break;
        }
    }
    if (s->color_format == 0) {
        av_log(avctx, AV_LOG_ERROR, "No supported pixel formats (%d formats available)\n", i);
        return AVERROR_UNKNOWN;
    }

    in_port_params.bEnabled   = OMX_TRUE;
    in_port_params.bPopulated = OMX_FALSE;
    in_port_params.eDomain    = OMX_PortDomainVideo;

    in_port_params.format.video.pNativeRender         = NULL;
    in_port_params.format.video.bFlagErrorConcealment = OMX_FALSE;
    in_port_params.format.video.eColorFormat          = s->color_format;
    s->stride     = avctx->width;
    s->plane_size = avctx->height;
    // If specific codecs need to manually override the stride/plane_size,
    // that can be done here.
    in_port_params.format.video.nStride      = s->stride;
    in_port_params.format.video.nSliceHeight = s->plane_size;
    in_port_params.format.video.nFrameWidth  = avctx->width;
    in_port_params.format.video.nFrameHeight = avctx->height;
    if (avctx->framerate.den > 0 && avctx->framerate.num > 0)
        in_port_params.format.video.xFramerate = (1LL << 16) * avctx->framerate.num / avctx->framerate.den;
    else
        in_port_params.format.video.xFramerate = (1LL << 16) * avctx->time_base.den / avctx->time_base.num;

    err = OMX_SetParameter(s->handle, OMX_IndexParamPortDefinition, &in_port_params);
    CHECK(err);
    err = OMX_GetParameter(s->handle, OMX_IndexParamPortDefinition, &in_port_params);
    CHECK(err);
    s->stride         = in_port_params.format.video.nStride;
    s->plane_size     = in_port_params.format.video.nSliceHeight;
    s->num_in_buffers = in_port_params.nBufferCountActual;

    err = OMX_GetParameter(s->handle, OMX_IndexParamPortDefinition, &out_port_params);
    out_port_params.bEnabled   = OMX_TRUE;
    out_port_params.bPopulated = OMX_FALSE;
    out_port_params.eDomain    = OMX_PortDomainVideo;
    out_port_params.format.video.pNativeRender = NULL;
    out_port_params.format.video.nFrameWidth   = avctx->width;
    out_port_params.format.video.nFrameHeight  = avctx->height;
    out_port_params.format.video.nStride       = 0;
    out_port_params.format.video.nSliceHeight  = 0;
    out_port_params.format.video.nBitrate      = avctx->bit_rate;
    out_port_params.format.video.xFramerate    = in_port_params.format.video.xFramerate;
    out_port_params.format.video.bFlagErrorConcealment  = OMX_FALSE;
    if (avctx->codec->id == AV_CODEC_ID_MPEG4)
        out_port_params.format.video.eCompressionFormat = OMX_VIDEO_CodingMPEG4;
    else if (avctx->codec->id == AV_CODEC_ID_H264)
        out_port_params.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;

    err = OMX_SetParameter(s->handle, OMX_IndexParamPortDefinition, &out_port_params);
    CHECK(err);
    err = OMX_GetParameter(s->handle, OMX_IndexParamPortDefinition, &out_port_params);
    CHECK(err);
    s->num_out_buffers = out_port_params.nBufferCountActual;

    INIT_STRUCT(vid_param_bitrate);
    vid_param_bitrate.nPortIndex     = s->out_port;
    vid_param_bitrate.eControlRate   = OMX_Video_ControlRateVariable;
    vid_param_bitrate.nTargetBitrate = avctx->bit_rate;
    err = OMX_SetParameter(s->handle, OMX_IndexParamVideoBitrate, &vid_param_bitrate);
    if (err != OMX_ErrorNone)
        av_log(avctx, AV_LOG_WARNING, "Unable to set video bitrate parameter\n");

    if (avctx->codec->id == AV_CODEC_ID_H264) {
        OMX_VIDEO_PARAM_AVCTYPE avc = { 0 };
        INIT_STRUCT(avc);
        avc.nPortIndex = s->out_port;
        err = OMX_GetParameter(s->handle, OMX_IndexParamVideoAvc, &avc);
        CHECK(err);
        avc.nBFrames = 0;
        avc.nPFrames = avctx->gop_size - 1;
        switch (s->profile == AV_PROFILE_UNKNOWN ? avctx->profile : s->profile) {
        case AV_PROFILE_H264_BASELINE:
            avc.eProfile = OMX_VIDEO_AVCProfileBaseline;
            break;
        case AV_PROFILE_H264_MAIN:
            avc.eProfile = OMX_VIDEO_AVCProfileMain;
            break;
        case AV_PROFILE_H264_HIGH:
            avc.eProfile = OMX_VIDEO_AVCProfileHigh;
            break;
        default:
            break;
        }
        err = OMX_SetParameter(s->handle, OMX_IndexParamVideoAvc, &avc);
        CHECK(err);
    }

    err = OMX_SendCommand(s->handle, OMX_CommandStateSet, OMX_StateIdle, NULL);
    CHECK(err);

    s->in_buffer_headers  = av_mallocz(sizeof(OMX_BUFFERHEADERTYPE*) * s->num_in_buffers);
    s->free_in_buffers    = av_mallocz(sizeof(OMX_BUFFERHEADERTYPE*) * s->num_in_buffers);
    s->out_buffer_headers = av_mallocz(sizeof(OMX_BUFFERHEADERTYPE*) * s->num_out_buffers);
    s->done_out_buffers   = av_mallocz(sizeof(OMX_BUFFERHEADERTYPE*) * s->num_out_buffers);
    if (!s->in_buffer_headers || !s->free_in_buffers || !s->out_buffer_headers || !s->done_out_buffers)
        return AVERROR(ENOMEM);
    for (i = 0; i < s->num_in_buffers && err == OMX_ErrorNone; i++) {
        if (s->input_zerocopy)
            err = OMX_UseBuffer(s->handle, &s->in_buffer_headers[i], s->in_port, s, in_port_params.nBufferSize, NULL);
        else
            err = OMX_AllocateBuffer(s->handle, &s->in_buffer_headers[i],  s->in_port,  s, in_port_params.nBufferSize);
        if (err == OMX_ErrorNone)
            s->in_buffer_headers[i]->pAppPrivate = s->in_buffer_headers[i]->pOutputPortPrivate = NULL;
    }
    CHECK(err);
    s->num_in_buffers = i;
    for (i = 0; i < s->num_out_buffers && err == OMX_ErrorNone; i++)
        err = OMX_AllocateBuffer(s->handle, &s->out_buffer_headers[i], s->out_port, s, out_port_params.nBufferSize);
    CHECK(err);
    s->num_out_buffers = i;

    if (wait_for_state(s, OMX_StateIdle) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Didn't get OMX_StateIdle\n");
        return AVERROR_UNKNOWN;
    }
    err = OMX_SendCommand(s->handle, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    CHECK(err);
    if (wait_for_state(s, OMX_StateExecuting) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Didn't get OMX_StateExecuting\n");
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < s->num_out_buffers && err == OMX_ErrorNone; i++)
        err = OMX_FillThisBuffer(s->handle, s->out_buffer_headers[i]);
    if (err != OMX_ErrorNone) {
        for (; i < s->num_out_buffers; i++)
            s->done_out_buffers[s->num_done_out_buffers++] = s->out_buffer_headers[i];
    }
    for (i = 0; i < s->num_in_buffers; i++)
        s->free_in_buffers[s->num_free_in_buffers++] = s->in_buffer_headers[i];
    return err != OMX_ErrorNone ? AVERROR_UNKNOWN : 0;
}

static av_cold void cleanup(OMXCodecContext *s)
{
    int executing;

    /* If the mutexes/condition variables have not been properly initialized,
     * nothing has been initialized and locking the mutex might be unsafe. */
    if (s->mutex_cond_inited_cnt == NB_MUTEX_CONDS) {
        pthread_mutex_lock(&s->state_mutex);
        executing = s->state == OMX_StateExecuting;
        pthread_mutex_unlock(&s->state_mutex);

        if (executing) {
            OMX_SendCommand(s->handle, OMX_CommandStateSet, OMX_StateIdle, NULL);
            wait_for_state(s, OMX_StateIdle);
            OMX_SendCommand(s->handle, OMX_CommandStateSet, OMX_StateLoaded, NULL);
            for (int i = 0; i < s->num_in_buffers; i++) {
                OMX_BUFFERHEADERTYPE *buffer = get_buffer(&s->input_mutex, &s->input_cond,
                                                        &s->num_free_in_buffers, s->free_in_buffers, 1);
                if (s->input_zerocopy)
                    buffer->pBuffer = NULL;
                OMX_FreeBuffer(s->handle, s->in_port, buffer);
            }
            for (int i = 0; i < s->num_out_buffers; i++) {
                OMX_BUFFERHEADERTYPE *buffer = get_buffer(&s->output_mutex, &s->output_cond,
                                                        &s->num_done_out_buffers, s->done_out_buffers, 1);
                OMX_FreeBuffer(s->handle, s->out_port, buffer);
            }
            wait_for_state(s, OMX_StateLoaded);
        }
        if (s->handle) {
            s->omx_context->ptr_FreeHandle(s->handle);
            s->handle = NULL;
        }

        omx_deinit(s->omx_context);
        s->omx_context = NULL;
        av_freep(&s->in_buffer_headers);
        av_freep(&s->out_buffer_headers);
        av_freep(&s->free_in_buffers);
        av_freep(&s->done_out_buffers);
        av_freep(&s->output_buf);
    }
    ff_pthread_free(s, omx_codec_context_offsets);
}

static av_cold int omx_encode_init(AVCodecContext *avctx)
{
    OMXCodecContext *s = avctx->priv_data;
    int ret = AVERROR_ENCODER_NOT_FOUND;
    const char *role;
    OMX_BUFFERHEADERTYPE *buffer;
    OMX_ERRORTYPE err;

    av_log(avctx, AV_LOG_WARNING,
           "The %s encoder is deprecated and will be removed in future versions\n",
           avctx->codec->name);

    /* cleanup relies on the mutexes/conditions being initialized first. */
    ret = ff_pthread_init(s, omx_codec_context_offsets);
    if (ret < 0)
        return ret;
    s->omx_context = omx_init(avctx, s->libname, s->libprefix);
    if (!s->omx_context)
        return AVERROR_ENCODER_NOT_FOUND;

    s->avctx = avctx;
    s->state = OMX_StateLoaded;
    s->error = OMX_ErrorNone;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_MPEG4:
        role = "video_encoder.mpeg4";
        break;
    case AV_CODEC_ID_H264:
        role = "video_encoder.avc";
        break;
    default:
        return AVERROR(ENOSYS);
    }

    if ((ret = find_component(s->omx_context, avctx, role, s->component_name, sizeof(s->component_name))) < 0)
        goto fail;

    av_log(avctx, AV_LOG_INFO, "Using %s\n", s->component_name);

    if ((ret = omx_component_init(avctx, role)) < 0)
        goto fail;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        while (1) {
            buffer = get_buffer(&s->output_mutex, &s->output_cond,
                                &s->num_done_out_buffers, s->done_out_buffers, 1);
            if (buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
                if ((ret = av_reallocp(&avctx->extradata, avctx->extradata_size + buffer->nFilledLen + AV_INPUT_BUFFER_PADDING_SIZE)) < 0) {
                    avctx->extradata_size = 0;
                    goto fail;
                }
                memcpy(avctx->extradata + avctx->extradata_size, buffer->pBuffer + buffer->nOffset, buffer->nFilledLen);
                avctx->extradata_size += buffer->nFilledLen;
                memset(avctx->extradata + avctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            }
            err = OMX_FillThisBuffer(s->handle, buffer);
            if (err != OMX_ErrorNone) {
                append_buffer(&s->output_mutex, &s->output_cond,
                              &s->num_done_out_buffers, s->done_out_buffers, buffer);
                av_log(avctx, AV_LOG_ERROR, "OMX_FillThisBuffer failed: %x\n", err);
                ret = AVERROR_UNKNOWN;
                goto fail;
            }
            if (avctx->codec->id == AV_CODEC_ID_H264) {
                // For H.264, the extradata can be returned in two separate buffers
                // (the videocore encoder on raspberry pi does this);
                // therefore check that we have got both SPS and PPS before continuing.
                int nals[32] = { 0 };
                int i;
                for (i = 0; i + 4 < avctx->extradata_size; i++) {
                     if (!avctx->extradata[i + 0] &&
                         !avctx->extradata[i + 1] &&
                         !avctx->extradata[i + 2] &&
                         avctx->extradata[i + 3] == 1) {
                         nals[avctx->extradata[i + 4] & 0x1f]++;
                     }
                }
                if (nals[H264_NAL_SPS] && nals[H264_NAL_PPS])
                    break;
            } else {
                if (avctx->extradata_size > 0)
                    break;
            }
        }
    }

    return 0;
fail:
    return ret;
}


static int omx_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *frame, int *got_packet)
{
    OMXCodecContext *s = avctx->priv_data;
    int ret = 0;
    OMX_BUFFERHEADERTYPE* buffer;
    OMX_ERRORTYPE err;
    int had_partial = 0;

    if (frame) {
        uint8_t *dst[4];
        int linesize[4];
        int need_copy;
        buffer = get_buffer(&s->input_mutex, &s->input_cond,
                            &s->num_free_in_buffers, s->free_in_buffers, 1);

        buffer->nFilledLen = av_image_fill_arrays(dst, linesize, buffer->pBuffer, avctx->pix_fmt, s->stride, s->plane_size, 1);

        if (s->input_zerocopy) {
            uint8_t *src[4] = { NULL };
            int src_linesize[4];
            av_image_fill_arrays(src, src_linesize, frame->data[0], avctx->pix_fmt, s->stride, s->plane_size, 1);
            if (frame->linesize[0] == src_linesize[0] &&
                frame->linesize[1] == src_linesize[1] &&
                frame->linesize[2] == src_linesize[2] &&
                frame->data[1] == src[1] &&
                frame->data[2] == src[2]) {
                // If the input frame happens to have all planes stored contiguously,
                // with the right strides, just clone the frame and set the OMX
                // buffer header to point to it
                AVFrame *local = av_frame_clone(frame);
                if (!local) {
                    // Return the buffer to the queue so it's not lost
                    append_buffer(&s->input_mutex, &s->input_cond, &s->num_free_in_buffers, s->free_in_buffers, buffer);
                    return AVERROR(ENOMEM);
                } else {
                    buffer->pAppPrivate = local;
                    buffer->pOutputPortPrivate = NULL;
                    buffer->pBuffer = local->data[0];
                    need_copy = 0;
                }
            } else {
                // If not, we need to allocate a new buffer with the right
                // size and copy the input frame into it.
                uint8_t *buf = NULL;
                int image_buffer_size = av_image_get_buffer_size(avctx->pix_fmt, s->stride, s->plane_size, 1);
                if (image_buffer_size >= 0)
                    buf = av_malloc(image_buffer_size);
                if (!buf) {
                    // Return the buffer to the queue so it's not lost
                    append_buffer(&s->input_mutex, &s->input_cond, &s->num_free_in_buffers, s->free_in_buffers, buffer);
                    return AVERROR(ENOMEM);
                } else {
                    buffer->pAppPrivate = buf;
                    // Mark that pAppPrivate is an av_malloc'ed buffer, not an AVFrame
                    buffer->pOutputPortPrivate = (void*) 1;
                    buffer->pBuffer = buf;
                    need_copy = 1;
                    buffer->nFilledLen = av_image_fill_arrays(dst, linesize, buffer->pBuffer, avctx->pix_fmt, s->stride, s->plane_size, 1);
                }
            }
        } else {
            need_copy = 1;
        }
        if (need_copy)
            av_image_copy2(dst, linesize, frame->data, frame->linesize,
                           avctx->pix_fmt, avctx->width, avctx->height);
        buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;
        buffer->nOffset = 0;
        // Convert the timestamps to microseconds; some encoders can ignore
        // the framerate and do VFR bit allocation based on timestamps.
        buffer->nTimeStamp = to_omx_ticks(av_rescale_q(frame->pts, avctx->time_base, AV_TIME_BASE_Q));
        if (frame->pict_type == AV_PICTURE_TYPE_I) {
#if CONFIG_OMX_RPI
            OMX_CONFIG_BOOLEANTYPE config = {0, };
            INIT_STRUCT(config);
            config.bEnabled = OMX_TRUE;
            err = OMX_SetConfig(s->handle, OMX_IndexConfigBrcmVideoRequestIFrame, &config);
            if (err != OMX_ErrorNone) {
                av_log(avctx, AV_LOG_ERROR, "OMX_SetConfig(RequestIFrame) failed: %x\n", err);
            }
#else
            OMX_CONFIG_INTRAREFRESHVOPTYPE config = {0, };
            INIT_STRUCT(config);
            config.nPortIndex = s->out_port;
            config.IntraRefreshVOP = OMX_TRUE;
            err = OMX_SetConfig(s->handle, OMX_IndexConfigVideoIntraVOPRefresh, &config);
            if (err != OMX_ErrorNone) {
                av_log(avctx, AV_LOG_ERROR, "OMX_SetConfig(IntraVOPRefresh) failed: %x\n", err);
            }
#endif
        }
        err = OMX_EmptyThisBuffer(s->handle, buffer);
        if (err != OMX_ErrorNone) {
            append_buffer(&s->input_mutex, &s->input_cond, &s->num_free_in_buffers, s->free_in_buffers, buffer);
            av_log(avctx, AV_LOG_ERROR, "OMX_EmptyThisBuffer failed: %x\n", err);
            return AVERROR_UNKNOWN;
        }
    } else if (!s->eos_sent) {
        buffer = get_buffer(&s->input_mutex, &s->input_cond,
                            &s->num_free_in_buffers, s->free_in_buffers, 1);

        buffer->nFilledLen = 0;
        buffer->nFlags = OMX_BUFFERFLAG_EOS;
        buffer->pAppPrivate = buffer->pOutputPortPrivate = NULL;
        err = OMX_EmptyThisBuffer(s->handle, buffer);
        if (err != OMX_ErrorNone) {
            append_buffer(&s->input_mutex, &s->input_cond, &s->num_free_in_buffers, s->free_in_buffers, buffer);
            av_log(avctx, AV_LOG_ERROR, "OMX_EmptyThisBuffer failed: %x\n", err);
            return AVERROR_UNKNOWN;
        }
        s->eos_sent = 1;
    }

    while (!*got_packet && ret == 0 && !s->got_eos) {
        // If not flushing, just poll the queue if there's finished packets.
        // If flushing, do a blocking wait until we either get a completed
        // packet, or get EOS.
        buffer = get_buffer(&s->output_mutex, &s->output_cond,
                            &s->num_done_out_buffers, s->done_out_buffers,
                            !frame || had_partial);
        if (!buffer)
            break;

        if (buffer->nFlags & OMX_BUFFERFLAG_EOS)
            s->got_eos = 1;

        if (buffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG && avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
            if ((ret = av_reallocp(&avctx->extradata, avctx->extradata_size + buffer->nFilledLen + AV_INPUT_BUFFER_PADDING_SIZE)) < 0) {
                avctx->extradata_size = 0;
                goto end;
            }
            memcpy(avctx->extradata + avctx->extradata_size, buffer->pBuffer + buffer->nOffset, buffer->nFilledLen);
            avctx->extradata_size += buffer->nFilledLen;
            memset(avctx->extradata + avctx->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        } else {
            int newsize = s->output_buf_size + buffer->nFilledLen + AV_INPUT_BUFFER_PADDING_SIZE;
            if ((ret = av_reallocp(&s->output_buf, newsize)) < 0) {
                s->output_buf_size = 0;
                goto end;
            }
            memcpy(s->output_buf + s->output_buf_size, buffer->pBuffer + buffer->nOffset, buffer->nFilledLen);
            s->output_buf_size += buffer->nFilledLen;
            if (buffer->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) {
                memset(s->output_buf + s->output_buf_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
                if ((ret = av_packet_from_data(pkt, s->output_buf, s->output_buf_size)) < 0) {
                    av_freep(&s->output_buf);
                    s->output_buf_size = 0;
                    goto end;
                }
                s->output_buf = NULL;
                s->output_buf_size = 0;
                pkt->pts = av_rescale_q(from_omx_ticks(buffer->nTimeStamp), AV_TIME_BASE_Q, avctx->time_base);
                // We don't currently enable B-frames for the encoders, so set
                // pkt->dts = pkt->pts. (The calling code behaves worse if the encoder
                // doesn't set the dts).
                pkt->dts = pkt->pts;
                if (buffer->nFlags & OMX_BUFFERFLAG_SYNCFRAME)
                    pkt->flags |= AV_PKT_FLAG_KEY;
                *got_packet = 1;
            } else {
#if CONFIG_OMX_RPI
                had_partial = 1;
#endif
            }
        }
end:
        err = OMX_FillThisBuffer(s->handle, buffer);
        if (err != OMX_ErrorNone) {
            append_buffer(&s->output_mutex, &s->output_cond, &s->num_done_out_buffers, s->done_out_buffers, buffer);
            av_log(avctx, AV_LOG_ERROR, "OMX_FillThisBuffer failed: %x\n", err);
            ret = AVERROR_UNKNOWN;
        }
    }
    return ret;
}

static av_cold int omx_encode_end(AVCodecContext *avctx)
{
    OMXCodecContext *s = avctx->priv_data;

    cleanup(s);
    return 0;
}

#define OFFSET(x) offsetof(OMXCodecContext, x)
#define VDE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define VE  AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "omx_libname", "OpenMAX library name", OFFSET(libname), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VDE },
    { "omx_libprefix", "OpenMAX library prefix", OFFSET(libprefix), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VDE },
    { "zerocopy", "Try to avoid copying input frames if possible", OFFSET(input_zerocopy), AV_OPT_TYPE_INT, { .i64 = CONFIG_OMX_RPI }, 0, 1, VE },
    { "profile",  "Set the encoding profile", OFFSET(profile), AV_OPT_TYPE_INT,   { .i64 = AV_PROFILE_UNKNOWN },       AV_PROFILE_UNKNOWN, AV_PROFILE_H264_HIGH, VE, .unit = "profile" },
    { "baseline", "",                         0,               AV_OPT_TYPE_CONST, { .i64 = AV_PROFILE_H264_BASELINE }, 0, 0, VE, .unit = "profile" },
    { "main",     "",                         0,               AV_OPT_TYPE_CONST, { .i64 = AV_PROFILE_H264_MAIN },     0, 0, VE, .unit = "profile" },
    { "high",     "",                         0,               AV_OPT_TYPE_CONST, { .i64 = AV_PROFILE_H264_HIGH },     0, 0, VE, .unit = "profile" },
    { NULL }
};

static const enum AVPixelFormat omx_encoder_pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE
};

static const AVClass omx_mpeg4enc_class = {
    .class_name = "mpeg4_omx",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
const FFCodec ff_mpeg4_omx_encoder = {
    .p.name           = "mpeg4_omx",
    CODEC_LONG_NAME("OpenMAX IL MPEG-4 video encoder"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MPEG4,
    .priv_data_size   = sizeof(OMXCodecContext),
    .init             = omx_encode_init,
    FF_CODEC_ENCODE_CB(omx_encode_frame),
    .close            = omx_encode_end,
    .p.pix_fmts       = omx_encoder_pix_fmts,
    .color_ranges     = AVCOL_RANGE_MPEG,
    .p.capabilities   = AV_CODEC_CAP_DELAY,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class     = &omx_mpeg4enc_class,
};

static const AVClass omx_h264enc_class = {
    .class_name = "h264_omx",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
const FFCodec ff_h264_omx_encoder = {
    .p.name           = "h264_omx",
    CODEC_LONG_NAME("OpenMAX IL H.264 video encoder"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(OMXCodecContext),
    .init             = omx_encode_init,
    FF_CODEC_ENCODE_CB(omx_encode_frame),
    .close            = omx_encode_end,
    .p.pix_fmts       = omx_encoder_pix_fmts,
    .color_ranges     = AVCOL_RANGE_MPEG, /* FIXME: implement tagging */
    .p.capabilities   = AV_CODEC_CAP_DELAY,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
    .p.priv_class     = &omx_h264enc_class,
};
