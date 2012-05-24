/*
 * Directshow capture interface
 * Copyright (c) 2010 Ramiro Polla
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

#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "avdevice.h"
#include "dshow_capture.h"

struct dshow_ctx {
    const AVClass *class;

    IGraphBuilder *graph;

    char *device_name[2];
    int video_device_number;
    int audio_device_number;

    int   list_options;
    int   list_devices;

    IBaseFilter *device_filter[2];
    IPin        *device_pin[2];
    libAVFilter *capture_filter[2];
    libAVPin    *capture_pin[2];

    HANDLE mutex;
    HANDLE event;
    AVPacketList *pktl;

    unsigned int curbufsize;
    unsigned int video_frame_num;

    IMediaControl *control;

    char *video_size;
    char *framerate;

    int requested_width;
    int requested_height;
    AVRational requested_framerate;

    int sample_rate;
    int sample_size;
    int channels;
};

static enum PixelFormat dshow_pixfmt(DWORD biCompression, WORD biBitCount)
{
    switch(biCompression) {
    case MKTAG('U', 'Y', 'V', 'Y'):
        return PIX_FMT_UYVY422;
    case MKTAG('Y', 'U', 'Y', '2'):
        return PIX_FMT_YUYV422;
    case MKTAG('I', '4', '2', '0'):
        return PIX_FMT_YUV420P;
    case BI_BITFIELDS:
    case BI_RGB:
        switch(biBitCount) { /* 1-8 are untested */
            case 1:
                return PIX_FMT_MONOWHITE;
            case 4:
                return PIX_FMT_RGB4;
            case 8:
                return PIX_FMT_RGB8;
            case 16:
                return PIX_FMT_RGB555;
            case 24:
                return PIX_FMT_BGR24;
            case 32:
                return PIX_FMT_RGB32;
        }
    }
    return PIX_FMT_NONE;
}

static enum CodecID dshow_codecid(DWORD biCompression)
{
    switch(biCompression) {
    case MKTAG('d', 'v', 's', 'd'):
        return CODEC_ID_DVVIDEO;
    case MKTAG('M', 'J', 'P', 'G'):
    case MKTAG('m', 'j', 'p', 'g'):
        return CODEC_ID_MJPEG;
    }
    return CODEC_ID_NONE;
}

static int
dshow_read_close(AVFormatContext *s)
{
    struct dshow_ctx *ctx = s->priv_data;
    AVPacketList *pktl;

    if (ctx->control) {
        IMediaControl_Stop(ctx->control);
        IMediaControl_Release(ctx->control);
    }

    if (ctx->graph) {
        IEnumFilters *fenum;
        int r;
        r = IGraphBuilder_EnumFilters(ctx->graph, &fenum);
        if (r == S_OK) {
            IBaseFilter *f;
            IEnumFilters_Reset(fenum);
            while (IEnumFilters_Next(fenum, 1, &f, NULL) == S_OK) {
                if (IGraphBuilder_RemoveFilter(ctx->graph, f) == S_OK)
                    IEnumFilters_Reset(fenum); /* When a filter is removed,
                                                * the list must be reset. */
                IBaseFilter_Release(f);
            }
            IEnumFilters_Release(fenum);
        }
        IGraphBuilder_Release(ctx->graph);
    }

    if (ctx->capture_pin[VideoDevice])
        libAVPin_Release(ctx->capture_pin[VideoDevice]);
    if (ctx->capture_pin[AudioDevice])
        libAVPin_Release(ctx->capture_pin[AudioDevice]);
    if (ctx->capture_filter[VideoDevice])
        libAVFilter_Release(ctx->capture_filter[VideoDevice]);
    if (ctx->capture_filter[AudioDevice])
        libAVFilter_Release(ctx->capture_filter[AudioDevice]);

    if (ctx->device_pin[VideoDevice])
        IPin_Release(ctx->device_pin[VideoDevice]);
    if (ctx->device_pin[AudioDevice])
        IPin_Release(ctx->device_pin[AudioDevice]);
    if (ctx->device_filter[VideoDevice])
        IBaseFilter_Release(ctx->device_filter[VideoDevice]);
    if (ctx->device_filter[AudioDevice])
        IBaseFilter_Release(ctx->device_filter[AudioDevice]);

    if (ctx->device_name[0])
        av_free(ctx->device_name[0]);
    if (ctx->device_name[1])
        av_free(ctx->device_name[1]);

    if(ctx->mutex)
        CloseHandle(ctx->mutex);
    if(ctx->event)
        CloseHandle(ctx->event);

    pktl = ctx->pktl;
    while (pktl) {
        AVPacketList *next = pktl->next;
        av_destruct_packet(&pktl->pkt);
        av_free(pktl);
        pktl = next;
    }

    return 0;
}

static char *dup_wchar_to_utf8(wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}

static int shall_we_drop(AVFormatContext *s)
{
    struct dshow_ctx *ctx = s->priv_data;
    const uint8_t dropscore[] = {62, 75, 87, 100};
    const int ndropscores = FF_ARRAY_ELEMS(dropscore);
    unsigned int buffer_fullness = (ctx->curbufsize*100)/s->max_picture_buffer;

    if(dropscore[++ctx->video_frame_num%ndropscores] <= buffer_fullness) {
        av_log(s, AV_LOG_ERROR,
              "real-time buffer %d%% full! frame dropped!\n", buffer_fullness);
        return 1;
    }

    return 0;
}

static void
callback(void *priv_data, int index, uint8_t *buf, int buf_size, int64_t time)
{
    AVFormatContext *s = priv_data;
    struct dshow_ctx *ctx = s->priv_data;
    AVPacketList **ppktl, *pktl_next;

//    dump_videohdr(s, vdhdr);

    if(shall_we_drop(s))
        return;

    WaitForSingleObject(ctx->mutex, INFINITE);

    pktl_next = av_mallocz(sizeof(AVPacketList));
    if(!pktl_next)
        goto fail;

    if(av_new_packet(&pktl_next->pkt, buf_size) < 0) {
        av_free(pktl_next);
        goto fail;
    }

    pktl_next->pkt.stream_index = index;
    pktl_next->pkt.pts = time;
    memcpy(pktl_next->pkt.data, buf, buf_size);

    for(ppktl = &ctx->pktl ; *ppktl ; ppktl = &(*ppktl)->next);
    *ppktl = pktl_next;

    ctx->curbufsize += buf_size;

    SetEvent(ctx->event);
    ReleaseMutex(ctx->mutex);

    return;
fail:
    ReleaseMutex(ctx->mutex);
    return;
}

/**
 * Cycle through available devices using the device enumerator devenum,
 * retrieve the device with type specified by devtype and return the
 * pointer to the object found in *pfilter.
 * If pfilter is NULL, list all device names.
 */
static int
dshow_cycle_devices(AVFormatContext *avctx, ICreateDevEnum *devenum,
                    enum dshowDeviceType devtype, IBaseFilter **pfilter)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    IEnumMoniker *classenum = NULL;
    IMoniker *m = NULL;
    const char *device_name = ctx->device_name[devtype];
    int skip = (devtype == VideoDevice) ? ctx->video_device_number
                                        : ctx->audio_device_number;
    int r;

    const GUID *device_guid[2] = { &CLSID_VideoInputDeviceCategory,
                                   &CLSID_AudioInputDeviceCategory };
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio";

    r = ICreateDevEnum_CreateClassEnumerator(devenum, device_guid[devtype],
                                             (IEnumMoniker **) &classenum, 0);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate %s devices.\n",
               devtypename);
        return AVERROR(EIO);
    }

    while (!device_filter && IEnumMoniker_Next(classenum, 1, &m, NULL) == S_OK) {
        IPropertyBag *bag = NULL;
        char *buf = NULL;
        VARIANT var;

        r = IMoniker_BindToStorage(m, 0, 0, &IID_IPropertyBag, (void *) &bag);
        if (r != S_OK)
            goto fail1;

        var.vt = VT_BSTR;
        r = IPropertyBag_Read(bag, L"FriendlyName", &var, NULL);
        if (r != S_OK)
            goto fail1;

        buf = dup_wchar_to_utf8(var.bstrVal);

        if (pfilter) {
            if (strcmp(device_name, buf))
                goto fail1;

            if (!skip--)
                IMoniker_BindToObject(m, 0, 0, &IID_IBaseFilter, (void *) &device_filter);
        } else {
            av_log(avctx, AV_LOG_INFO, " \"%s\"\n", buf);
        }

fail1:
        if (buf)
            av_free(buf);
        if (bag)
            IPropertyBag_Release(bag);
        IMoniker_Release(m);
    }

    IEnumMoniker_Release(classenum);

    if (pfilter) {
        if (!device_filter) {
            av_log(avctx, AV_LOG_ERROR, "Could not find %s device.\n",
                   devtypename);
            return AVERROR(EIO);
        }
        *pfilter = device_filter;
    }

    return 0;
}

/**
 * Cycle through available formats using the specified pin,
 * try to set parameters specified through AVOptions and if successful
 * return 1 in *pformat_set.
 * If pformat_set is NULL, list all pin capabilities.
 */
static void
dshow_cycle_formats(AVFormatContext *avctx, enum dshowDeviceType devtype,
                    IPin *pin, int *pformat_set)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IAMStreamConfig *config = NULL;
    AM_MEDIA_TYPE *type = NULL;
    int format_set = 0;
    void *caps = NULL;
    int i, n, size;

    if (IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **) &config) != S_OK)
        return;
    if (IAMStreamConfig_GetNumberOfCapabilities(config, &n, &size) != S_OK)
        goto end;

    caps = av_malloc(size);
    if (!caps)
        goto end;

    for (i = 0; i < n && !format_set; i++) {
        IAMStreamConfig_GetStreamCaps(config, i, &type, (void *) caps);

#if DSHOWDEBUG
        ff_print_AM_MEDIA_TYPE(type);
#endif

        if (devtype == VideoDevice) {
            VIDEO_STREAM_CONFIG_CAPS *vcaps = caps;
            BITMAPINFOHEADER *bih;
            int64_t *fr;
#if DSHOWDEBUG
            ff_print_VIDEO_STREAM_CONFIG_CAPS(vcaps);
#endif
            if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo)) {
                VIDEOINFOHEADER *v = (void *) type->pbFormat;
                fr = &v->AvgTimePerFrame;
                bih = &v->bmiHeader;
            } else if (IsEqualGUID(&type->formattype, &FORMAT_VideoInfo2)) {
                VIDEOINFOHEADER2 *v = (void *) type->pbFormat;
                fr = &v->AvgTimePerFrame;
                bih = &v->bmiHeader;
            } else {
                goto next;
            }
            if (!pformat_set) {
                av_log(avctx, AV_LOG_INFO, "  min s=%ldx%ld fps=%g max s=%ldx%ld fps=%g\n",
                       vcaps->MinOutputSize.cx, vcaps->MinOutputSize.cy,
                       1e7 / vcaps->MaxFrameInterval,
                       vcaps->MaxOutputSize.cx, vcaps->MaxOutputSize.cy,
                       1e7 / vcaps->MinFrameInterval);
                continue;
            }
            if (ctx->framerate) {
                int64_t framerate = ((int64_t) ctx->requested_framerate.den*10000000)
                                            /  ctx->requested_framerate.num;
                if (framerate > vcaps->MaxFrameInterval ||
                    framerate < vcaps->MinFrameInterval)
                    goto next;
                *fr = framerate;
            }
            if (ctx->video_size) {
                if (ctx->requested_width  > vcaps->MaxOutputSize.cx ||
                    ctx->requested_width  < vcaps->MinOutputSize.cx ||
                    ctx->requested_height > vcaps->MaxOutputSize.cy ||
                    ctx->requested_height < vcaps->MinOutputSize.cy)
                    goto next;
                bih->biWidth  = ctx->requested_width;
                bih->biHeight = ctx->requested_height;
            }
        } else {
            AUDIO_STREAM_CONFIG_CAPS *acaps = caps;
            WAVEFORMATEX *fx;
#if DSHOWDEBUG
            ff_print_AUDIO_STREAM_CONFIG_CAPS(acaps);
#endif
            if (IsEqualGUID(&type->formattype, &FORMAT_WaveFormatEx)) {
                fx = (void *) type->pbFormat;
            } else {
                goto next;
            }
            if (!pformat_set) {
                av_log(avctx, AV_LOG_INFO, "  min ch=%lu bits=%lu rate=%6lu max ch=%lu bits=%lu rate=%6lu\n",
                       acaps->MinimumChannels, acaps->MinimumBitsPerSample, acaps->MinimumSampleFrequency,
                       acaps->MaximumChannels, acaps->MaximumBitsPerSample, acaps->MaximumSampleFrequency);
                continue;
            }
            if (ctx->sample_rate) {
                if (ctx->sample_rate > acaps->MaximumSampleFrequency ||
                    ctx->sample_rate < acaps->MinimumSampleFrequency)
                    goto next;
                fx->nSamplesPerSec = ctx->sample_rate;
            }
            if (ctx->sample_size) {
                if (ctx->sample_size > acaps->MaximumBitsPerSample ||
                    ctx->sample_size < acaps->MinimumBitsPerSample)
                    goto next;
                fx->wBitsPerSample = ctx->sample_size;
            }
            if (ctx->channels) {
                if (ctx->channels > acaps->MaximumChannels ||
                    ctx->channels < acaps->MinimumChannels)
                    goto next;
                fx->nChannels = ctx->channels;
            }
        }
        if (IAMStreamConfig_SetFormat(config, type) != S_OK)
            goto next;
        format_set = 1;
next:
        if (type->pbFormat)
            CoTaskMemFree(type->pbFormat);
        CoTaskMemFree(type);
    }
end:
    IAMStreamConfig_Release(config);
    if (caps)
        av_free(caps);
    if (pformat_set)
        *pformat_set = format_set;
}

/**
 * Cycle through available pins using the device_filter device, of type
 * devtype, retrieve the first output pin and return the pointer to the
 * object found in *ppin.
 * If ppin is NULL, cycle through all pins listing audio/video capabilities.
 */
static int
dshow_cycle_pins(AVFormatContext *avctx, enum dshowDeviceType devtype,
                 IBaseFilter *device_filter, IPin **ppin)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IEnumPins *pins = 0;
    IPin *device_pin = NULL;
    IPin *pin;
    int r;

    const GUID *mediatype[2] = { &MEDIATYPE_Video, &MEDIATYPE_Audio };
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio";

    int set_format = (devtype == VideoDevice && (ctx->video_size || ctx->framerate))
                  || (devtype == AudioDevice && (ctx->channels || ctx->sample_rate));
    int format_set = 0;

    r = IBaseFilter_EnumPins(device_filter, &pins);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate pins.\n");
        return AVERROR(EIO);
    }

    if (!ppin) {
        av_log(avctx, AV_LOG_INFO, "DirectShow %s device options\n",
               devtypename);
    }
    while (!device_pin && IEnumPins_Next(pins, 1, &pin, NULL) == S_OK) {
        IKsPropertySet *p = NULL;
        IEnumMediaTypes *types = NULL;
        PIN_INFO info = {0};
        AM_MEDIA_TYPE *type;
        GUID category;
        DWORD r2;

        IPin_QueryPinInfo(pin, &info);
        IBaseFilter_Release(info.pFilter);

        if (info.dir != PINDIR_OUTPUT)
            goto next;
        if (IPin_QueryInterface(pin, &IID_IKsPropertySet, (void **) &p) != S_OK)
            goto next;
        if (IKsPropertySet_Get(p, &AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY,
                               NULL, 0, &category, sizeof(GUID), &r2) != S_OK)
            goto next;
        if (!IsEqualGUID(&category, &PIN_CATEGORY_CAPTURE))
            goto next;

        if (!ppin) {
            char *buf = dup_wchar_to_utf8(info.achName);
            av_log(avctx, AV_LOG_INFO, " Pin \"%s\"\n", buf);
            av_free(buf);
            dshow_cycle_formats(avctx, devtype, pin, NULL);
            goto next;
        }
        if (set_format) {
            dshow_cycle_formats(avctx, devtype, pin, &format_set);
            if (!format_set) {
                goto next;
            }
        }

        if (IPin_EnumMediaTypes(pin, &types) != S_OK)
            goto next;

        IEnumMediaTypes_Reset(types);
        while (!device_pin && IEnumMediaTypes_Next(types, 1, &type, NULL) == S_OK) {
            if (IsEqualGUID(&type->majortype, mediatype[devtype])) {
                device_pin = pin;
                goto next;
            }
            CoTaskMemFree(type);
        }

next:
        if (types)
            IEnumMediaTypes_Release(types);
        if (p)
            IKsPropertySet_Release(p);
        if (device_pin != pin)
            IPin_Release(pin);
    }

    IEnumPins_Release(pins);

    if (ppin) {
        if (set_format && !format_set) {
            av_log(avctx, AV_LOG_ERROR, "Could not set %s options\n", devtypename);
            return AVERROR(EIO);
        }
        if (!device_pin) {
            av_log(avctx, AV_LOG_ERROR,
                "Could not find output pin from %s capture device.\n", devtypename);
            return AVERROR(EIO);
        }
        *ppin = device_pin;
    }

    return 0;
}

/**
 * List options for device with type devtype.
 *
 * @param devenum device enumerator used for accessing the device
 */
static int
dshow_list_device_options(AVFormatContext *avctx, ICreateDevEnum *devenum,
                          enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    int r;

    if ((r = dshow_cycle_devices(avctx, devenum, devtype, &device_filter)) < 0)
        return r;
    ctx->device_filter[devtype] = device_filter;
    if ((r = dshow_cycle_pins(avctx, devtype, device_filter, NULL)) < 0)
        return r;

    return 0;
}

static int
dshow_open_device(AVFormatContext *avctx, ICreateDevEnum *devenum,
                  enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    IGraphBuilder *graph = ctx->graph;
    IPin *device_pin = NULL;
    libAVPin *capture_pin = NULL;
    libAVFilter *capture_filter = NULL;
    int ret = AVERROR(EIO);
    int r;

    const wchar_t *filter_name[2] = { L"Audio capture filter", L"Video capture filter" };

    if ((r = dshow_cycle_devices(avctx, devenum, devtype, &device_filter)) < 0) {
        ret = r;
        goto error;
    }

    ctx->device_filter [devtype] = device_filter;

    r = IGraphBuilder_AddFilter(graph, device_filter, NULL);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not add device filter to graph.\n");
        goto error;
    }

    if ((r = dshow_cycle_pins(avctx, devtype, device_filter, &device_pin)) < 0) {
        ret = r;
        goto error;
    }
    ctx->device_pin[devtype] = device_pin;

    capture_filter = libAVFilter_Create(avctx, callback, devtype);
    if (!capture_filter) {
        av_log(avctx, AV_LOG_ERROR, "Could not create grabber filter.\n");
        goto error;
    }
    ctx->capture_filter[devtype] = capture_filter;

    r = IGraphBuilder_AddFilter(graph, (IBaseFilter *) capture_filter,
                                filter_name[devtype]);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not add capture filter to graph\n");
        goto error;
    }

    libAVPin_AddRef(capture_filter->pin);
    capture_pin = capture_filter->pin;
    ctx->capture_pin[devtype] = capture_pin;

    r = IGraphBuilder_ConnectDirect(graph, device_pin, (IPin *) capture_pin, NULL);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not connect pins\n");
        goto error;
    }

    ret = 0;

error:
    return ret;
}

static enum CodecID waveform_codec_id(enum AVSampleFormat sample_fmt)
{
    switch (sample_fmt) {
    case AV_SAMPLE_FMT_U8:  return CODEC_ID_PCM_U8;
    case AV_SAMPLE_FMT_S16: return CODEC_ID_PCM_S16LE;
    case AV_SAMPLE_FMT_S32: return CODEC_ID_PCM_S32LE;
    default:                return CODEC_ID_NONE; /* Should never happen. */
    }
}

static enum AVSampleFormat sample_fmt_bits_per_sample(int bits)
{
    switch (bits) {
    case 8:  return AV_SAMPLE_FMT_U8;
    case 16: return AV_SAMPLE_FMT_S16;
    case 32: return AV_SAMPLE_FMT_S32;
    default: return AV_SAMPLE_FMT_NONE; /* Should never happen. */
    }
}

static int
dshow_add_device(AVFormatContext *avctx,
                 enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    AM_MEDIA_TYPE type;
    AVCodecContext *codec;
    AVStream *st;
    int ret = AVERROR(EIO);

    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    st->id = devtype;

    ctx->capture_filter[devtype]->stream_index = st->index;

    libAVPin_ConnectionMediaType(ctx->capture_pin[devtype], &type);

    codec = st->codec;
    if (devtype == VideoDevice) {
        BITMAPINFOHEADER *bih = NULL;
        AVRational time_base;

        if (IsEqualGUID(&type.formattype, &FORMAT_VideoInfo)) {
            VIDEOINFOHEADER *v = (void *) type.pbFormat;
            time_base = (AVRational) { v->AvgTimePerFrame, 10000000 };
            bih = &v->bmiHeader;
        } else if (IsEqualGUID(&type.formattype, &FORMAT_VideoInfo2)) {
            VIDEOINFOHEADER2 *v = (void *) type.pbFormat;
            time_base = (AVRational) { v->AvgTimePerFrame, 10000000 };
            bih = &v->bmiHeader;
        }
        if (!bih) {
            av_log(avctx, AV_LOG_ERROR, "Could not get media type.\n");
            goto error;
        }

        codec->time_base  = time_base;
        codec->codec_type = AVMEDIA_TYPE_VIDEO;
        codec->width      = bih->biWidth;
        codec->height     = bih->biHeight;
        codec->pix_fmt    = dshow_pixfmt(bih->biCompression, bih->biBitCount);
        if (codec->pix_fmt == PIX_FMT_NONE) {
            codec->codec_id = dshow_codecid(bih->biCompression);
            if (codec->codec_id == CODEC_ID_NONE) {
                av_log(avctx, AV_LOG_ERROR, "Unknown compression type. "
                                 "Please report verbose (-v 9) debug information.\n");
                dshow_read_close(avctx);
                return AVERROR_PATCHWELCOME;
            }
            codec->bits_per_coded_sample = bih->biBitCount;
        } else {
            codec->codec_id = CODEC_ID_RAWVIDEO;
            if (bih->biCompression == BI_RGB || bih->biCompression == BI_BITFIELDS) {
                codec->bits_per_coded_sample = bih->biBitCount;
                codec->extradata = av_malloc(9 + FF_INPUT_BUFFER_PADDING_SIZE);
                if (codec->extradata) {
                    codec->extradata_size = 9;
                    memcpy(codec->extradata, "BottomUp", 9);
                }
            }
        }
    } else {
        WAVEFORMATEX *fx = NULL;

        if (IsEqualGUID(&type.formattype, &FORMAT_WaveFormatEx)) {
            fx = (void *) type.pbFormat;
        }
        if (!fx) {
            av_log(avctx, AV_LOG_ERROR, "Could not get media type.\n");
            goto error;
        }

        codec->codec_type  = AVMEDIA_TYPE_AUDIO;
        codec->sample_fmt  = sample_fmt_bits_per_sample(fx->wBitsPerSample);
        codec->codec_id    = waveform_codec_id(codec->sample_fmt);
        codec->sample_rate = fx->nSamplesPerSec;
        codec->channels    = fx->nChannels;
    }

    avpriv_set_pts_info(st, 64, 1, 10000000);

    ret = 0;

error:
    return ret;
}

static int parse_device_name(AVFormatContext *avctx)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    char **device_name = ctx->device_name;
    char *name = av_strdup(avctx->filename);
    char *tmp = name;
    int ret = 1;
    char *type;

    while ((type = strtok(tmp, "="))) {
        char *token = strtok(NULL, ":");
        tmp = NULL;

        if        (!strcmp(type, "video")) {
            device_name[0] = token;
        } else if (!strcmp(type, "audio")) {
            device_name[1] = token;
        } else {
            device_name[0] = NULL;
            device_name[1] = NULL;
            break;
        }
    }

    if (!device_name[0] && !device_name[1]) {
        ret = 0;
    } else {
        if (device_name[0])
            device_name[0] = av_strdup(device_name[0]);
        if (device_name[1])
            device_name[1] = av_strdup(device_name[1]);
    }

    av_free(name);
    return ret;
}

static int dshow_read_header(AVFormatContext *avctx)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IGraphBuilder *graph = NULL;
    ICreateDevEnum *devenum = NULL;
    IMediaControl *control = NULL;
    int ret = AVERROR(EIO);
    int r;

    if (!ctx->list_devices && !parse_device_name(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Malformed dshow input string.\n");
        goto error;
    }

    if (ctx->video_size) {
        r = av_parse_video_size(&ctx->requested_width, &ctx->requested_height, ctx->video_size);
        if (r < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not parse video size '%s'.\n", ctx->video_size);
            goto error;
        }
    }
    if (ctx->framerate) {
        r = av_parse_video_rate(&ctx->requested_framerate, ctx->framerate);
        if (r < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not parse framerate '%s'.\n", ctx->framerate);
            goto error;
        }
    }

    CoInitialize(0);

    r = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                         &IID_IGraphBuilder, (void **) &graph);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not create capture graph.\n");
        goto error;
    }
    ctx->graph = graph;

    r = CoCreateInstance(&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
                         &IID_ICreateDevEnum, (void **) &devenum);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate system devices.\n");
        goto error;
    }

    if (ctx->list_devices) {
        av_log(avctx, AV_LOG_INFO, "DirectShow video devices\n");
        dshow_cycle_devices(avctx, devenum, VideoDevice, NULL);
        av_log(avctx, AV_LOG_INFO, "DirectShow audio devices\n");
        dshow_cycle_devices(avctx, devenum, AudioDevice, NULL);
        ret = AVERROR_EXIT;
        goto error;
    }
    if (ctx->list_options) {
        if (ctx->device_name[VideoDevice])
            dshow_list_device_options(avctx, devenum, VideoDevice);
        if (ctx->device_name[AudioDevice])
            dshow_list_device_options(avctx, devenum, AudioDevice);
        ret = AVERROR_EXIT;
        goto error;
    }

    if (ctx->device_name[VideoDevice]) {
        ret = dshow_open_device(avctx, devenum, VideoDevice);
        if (ret < 0)
            goto error;
        ret = dshow_add_device(avctx, VideoDevice);
        if (ret < 0)
            goto error;
    }
    if (ctx->device_name[AudioDevice]) {
        ret = dshow_open_device(avctx, devenum, AudioDevice);
        if (ret < 0)
            goto error;
        ret = dshow_add_device(avctx, AudioDevice);
        if (ret < 0)
            goto error;
    }

    ctx->mutex = CreateMutex(NULL, 0, NULL);
    if (!ctx->mutex) {
        av_log(avctx, AV_LOG_ERROR, "Could not create Mutex\n");
        goto error;
    }
    ctx->event = CreateEvent(NULL, 1, 0, NULL);
    if (!ctx->event) {
        av_log(avctx, AV_LOG_ERROR, "Could not create Event\n");
        goto error;
    }

    r = IGraphBuilder_QueryInterface(graph, &IID_IMediaControl, (void **) &control);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get media control.\n");
        goto error;
    }
    ctx->control = control;

    r = IMediaControl_Run(control);
    if (r == S_FALSE) {
        OAFilterState pfs;
        r = IMediaControl_GetState(control, 0, &pfs);
    }
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not run filter\n");
        goto error;
    }

    ret = 0;

error:

    if (ret < 0)
        dshow_read_close(avctx);

    if (devenum)
        ICreateDevEnum_Release(devenum);

    return ret;
}

static int dshow_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct dshow_ctx *ctx = s->priv_data;
    AVPacketList *pktl = NULL;

    while (!pktl) {
        WaitForSingleObject(ctx->mutex, INFINITE);
        pktl = ctx->pktl;
        if (ctx->pktl) {
            *pkt = ctx->pktl->pkt;
            ctx->pktl = ctx->pktl->next;
            av_free(pktl);
        }
        ResetEvent(ctx->event);
        ReleaseMutex(ctx->mutex);
        if (!pktl) {
            if (s->flags & AVFMT_FLAG_NONBLOCK) {
                return AVERROR(EAGAIN);
            } else {
                WaitForSingleObject(ctx->event, INFINITE);
            }
        }
    }

    ctx->curbufsize -= pkt->size;

    return pkt->size;
}

#define OFFSET(x) offsetof(struct dshow_ctx, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "video_size", "set video size given a string such as 640x480 or hd720.", OFFSET(video_size), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "framerate", "set video frame rate", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "sample_rate", "set audio sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, DEC },
    { "sample_size", "set audio sample size", OFFSET(sample_size), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 16, DEC },
    { "channels", "set number of audio channels, such as 1 or 2", OFFSET(channels), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, DEC },
    { "list_devices", "list available devices", OFFSET(list_devices), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1, DEC, "list_devices" },
    { "true", "", 0, AV_OPT_TYPE_CONST, {.dbl=1}, 0, 0, DEC, "list_devices" },
    { "false", "", 0, AV_OPT_TYPE_CONST, {.dbl=0}, 0, 0, DEC, "list_devices" },
    { "list_options", "list available options for specified device", OFFSET(list_options), AV_OPT_TYPE_INT, {.dbl=0}, 0, 1, DEC, "list_options" },
    { "true", "", 0, AV_OPT_TYPE_CONST, {.dbl=1}, 0, 0, DEC, "list_options" },
    { "false", "", 0, AV_OPT_TYPE_CONST, {.dbl=0}, 0, 0, DEC, "list_options" },
    { "video_device_number", "set video device number for devices with same name (starts at 0)", OFFSET(video_device_number), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, DEC },
    { "audio_device_number", "set audio device number for devices with same name (starts at 0)", OFFSET(audio_device_number), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass dshow_class = {
    .class_name = "DirectShow indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_dshow_demuxer = {
    .name           = "dshow",
    .long_name      = NULL_IF_CONFIG_SMALL("DirectShow capture"),
    .priv_data_size = sizeof(struct dshow_ctx),
    .read_header    = dshow_read_header,
    .read_packet    = dshow_read_packet,
    .read_close     = dshow_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &dshow_class,
};
