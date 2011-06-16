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

#include "libavformat/timefilter.h"

#include "avdevice.h"
#include "dshow.h"

struct dshow_ctx {
    IGraphBuilder *graph;

    char *device_name[2];

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

    TimeFilter *timefilter;
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
    if (ctx->graph)
        IGraphBuilder_Release(ctx->graph);

    /* FIXME remove filters from graph */
    /* FIXME disconnect pins */
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

static int
dshow_open_device(AVFormatContext *avctx, ICreateDevEnum *devenum,
                  enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    IEnumMoniker *classenum = NULL;
    IGraphBuilder *graph = ctx->graph;
    IEnumPins *pins = 0;
    IMoniker *m = NULL;
    IPin *device_pin = NULL;
    libAVPin *capture_pin = NULL;
    libAVFilter *capture_filter = NULL;
    const char *device_name = ctx->device_name[devtype];
    int ret = AVERROR(EIO);
    IPin *pin;
    int r, i;

    const GUID *device_guid[2] = { &CLSID_VideoInputDeviceCategory,
                                   &CLSID_AudioInputDeviceCategory };
    const GUID *mediatype[2] = { &MEDIATYPE_Video, &MEDIATYPE_Audio };
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio";
    const wchar_t *filter_name[2] = { L"Audio capture filter", L"Video capture filter" };

    r = ICreateDevEnum_CreateClassEnumerator(devenum, device_guid[devtype],
                                             (IEnumMoniker **) &classenum, 0);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate %s devices.\n",
               devtypename);
        goto error;
    }

    while (IEnumMoniker_Next(classenum, 1, &m, NULL) == S_OK && !device_filter) {
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

        if (strcmp(device_name, buf))
            goto fail1;

        IMoniker_BindToObject(m, 0, 0, &IID_IBaseFilter, (void *) &device_filter);

fail1:
        if (buf)
            av_free(buf);
        if (bag)
            IPropertyBag_Release(bag);
        IMoniker_Release(m);
    }

    if (!device_filter) {
        av_log(avctx, AV_LOG_ERROR, "Could not find %s device.\n",
               devtypename);
        goto error;
    }
    ctx->device_filter [devtype] = device_filter;

    r = IGraphBuilder_AddFilter(graph, device_filter, NULL);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not add device filter to graph.\n");
        goto error;
    }

    r = IBaseFilter_EnumPins(device_filter, &pins);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate pins.\n");
        goto error;
    }

    i = 0;
    while (IEnumPins_Next(pins, 1, &pin, NULL) == S_OK && !device_pin) {
        IKsPropertySet *p = NULL;
        IEnumMediaTypes *types;
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

        if (IPin_EnumMediaTypes(pin, &types) != S_OK)
            goto next;

        IEnumMediaTypes_Reset(types);
        while (IEnumMediaTypes_Next(types, 1, &type, NULL) == S_OK && !device_pin) {
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

    if (!device_pin) {
        av_log(avctx, AV_LOG_ERROR,
               "Could not find output pin from %s capture device.\n", devtypename);
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
    if (pins)
        IEnumPins_Release(pins);
    if (classenum)
        IEnumMoniker_Release(classenum);

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

static enum SampleFormat sample_fmt_bits_per_sample(int bits)
{
    switch (bits) {
    case 8:  return AV_SAMPLE_FMT_U8;
    case 16: return AV_SAMPLE_FMT_S16;
    case 32: return AV_SAMPLE_FMT_S32;
    default: return AV_SAMPLE_FMT_NONE; /* Should never happen. */
    }
}

static int
dshow_add_device(AVFormatContext *avctx, AVFormatParameters *ap,
                 enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    AM_MEDIA_TYPE type;
    AVCodecContext *codec;
    AVStream *st;
    int ret = AVERROR(EIO);

    st = av_new_stream(avctx, devtype);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    ctx->capture_filter[devtype]->stream_index = st->index;

    libAVPin_ConnectionMediaType(ctx->capture_pin[devtype], &type);

    codec = st->codec;
    if (devtype == VideoDevice) {
        BITMAPINFOHEADER *bih = NULL;

        if (IsEqualGUID(&type.formattype, &FORMAT_VideoInfo)) {
            VIDEOINFOHEADER *v = (void *) type.pbFormat;
            bih = &v->bmiHeader;
        } else if (IsEqualGUID(&type.formattype, &FORMAT_VideoInfo2)) {
            VIDEOINFOHEADER2 *v = (void *) type.pbFormat;
            bih = &v->bmiHeader;
        }
        if (!bih) {
            av_log(avctx, AV_LOG_ERROR, "Could not get media type.\n");
            goto error;
        }

        codec->time_base  = ap->time_base;
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
            if (bih->biCompression == BI_RGB) {
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

    av_set_pts_info(st, 64, 1, 10000000);

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

static int dshow_read_header(AVFormatContext *avctx, AVFormatParameters *ap)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IGraphBuilder *graph = NULL;
    ICreateDevEnum *devenum = NULL;
    IMediaControl *control = NULL;
    int ret = AVERROR(EIO);
    int r;

    if (!parse_device_name(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Malformed dshow input string.\n");
        goto error;
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

    if (ctx->device_name[VideoDevice]) {
        ret = dshow_open_device(avctx, devenum, VideoDevice);
        if (ret < 0)
            goto error;
        ret = dshow_add_device(avctx, ap, VideoDevice);
        if (ret < 0)
            goto error;
    }
    if (ctx->device_name[AudioDevice]) {
        ret = dshow_open_device(avctx, devenum, AudioDevice);
        if (ret < 0)
            goto error;
        ret = dshow_add_device(avctx, ap, AudioDevice);
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

AVInputFormat ff_dshow_demuxer = {
    "dshow",
    NULL_IF_CONFIG_SMALL("DirectShow capture"),
    sizeof(struct dshow_ctx),
    NULL,
    dshow_read_header,
    dshow_read_packet,
    dshow_read_close,
    .flags = AVFMT_NOFILE,
};
