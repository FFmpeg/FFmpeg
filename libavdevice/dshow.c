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

#include "dshow_capture.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavformat/riff.h"
#include "avdevice.h"
#include "libavcodec/raw.h"
#include "objidl.h"
#include "shlwapi.h"


static enum AVPixelFormat dshow_pixfmt(DWORD biCompression, WORD biBitCount)
{
    switch(biCompression) {
    case BI_BITFIELDS:
    case BI_RGB:
        switch(biBitCount) { /* 1-8 are untested */
            case 1:
                return AV_PIX_FMT_MONOWHITE;
            case 4:
                return AV_PIX_FMT_RGB4;
            case 8:
                return AV_PIX_FMT_RGB8;
            case 16:
                return AV_PIX_FMT_RGB555;
            case 24:
                return AV_PIX_FMT_BGR24;
            case 32:
                return AV_PIX_FMT_0RGB32;
        }
    }
    return avpriv_find_pix_fmt(avpriv_get_raw_pix_fmt_tags(), biCompression); // all others
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

    if (ctx->media_event)
        IMediaEvent_Release(ctx->media_event);

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
        av_freep(&ctx->device_name[0]);
    if (ctx->device_name[1])
        av_freep(&ctx->device_name[1]);

    if(ctx->mutex)
        CloseHandle(ctx->mutex);
    if(ctx->event[0])
        CloseHandle(ctx->event[0]);
    if(ctx->event[1])
        CloseHandle(ctx->event[1]);

    pktl = ctx->pktl;
    while (pktl) {
        AVPacketList *next = pktl->next;
        av_packet_unref(&pktl->pkt);
        av_free(pktl);
        pktl = next;
    }

    CoUninitialize();

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

static int shall_we_drop(AVFormatContext *s, int index, enum dshowDeviceType devtype)
{
    struct dshow_ctx *ctx = s->priv_data;
    static const uint8_t dropscore[] = {62, 75, 87, 100};
    const int ndropscores = FF_ARRAY_ELEMS(dropscore);
    unsigned int buffer_fullness = (ctx->curbufsize[index]*100)/s->max_picture_buffer;
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio";

    if(dropscore[++ctx->video_frame_num%ndropscores] <= buffer_fullness) {
        av_log(s, AV_LOG_ERROR,
              "real-time buffer [%s] [%s input] too full or near too full (%d%% of size: %d [rtbufsize parameter])! frame dropped!\n",
              ctx->device_name[devtype], devtypename, buffer_fullness, s->max_picture_buffer);
        return 1;
    }

    return 0;
}

static void
callback(void *priv_data, int index, uint8_t *buf, int buf_size, int64_t time, enum dshowDeviceType devtype)
{
    AVFormatContext *s = priv_data;
    struct dshow_ctx *ctx = s->priv_data;
    AVPacketList **ppktl, *pktl_next;

//    dump_videohdr(s, vdhdr);

    WaitForSingleObject(ctx->mutex, INFINITE);

    if(shall_we_drop(s, index, devtype))
        goto fail;

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
    ctx->curbufsize[index] += buf_size;

    SetEvent(ctx->event[1]);
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
                    enum dshowDeviceType devtype, enum dshowSourceFilterType sourcetype, IBaseFilter **pfilter)
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
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio only";
    const char *sourcetypename = (sourcetype == VideoSourceDevice) ? "video" : "audio";

    r = ICreateDevEnum_CreateClassEnumerator(devenum, device_guid[sourcetype],
                                             (IEnumMoniker **) &classenum, 0);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate %s devices (or none found).\n",
               devtypename);
        return AVERROR(EIO);
    }

    while (!device_filter && IEnumMoniker_Next(classenum, 1, &m, NULL) == S_OK) {
        IPropertyBag *bag = NULL;
        char *friendly_name = NULL;
        char *unique_name = NULL;
        VARIANT var;
        IBindCtx *bind_ctx = NULL;
        LPOLESTR olestr = NULL;
        LPMALLOC co_malloc = NULL;
        int i;

        r = CoGetMalloc(1, &co_malloc);
        if (r = S_OK)
            goto fail1;
        r = CreateBindCtx(0, &bind_ctx);
        if (r != S_OK)
            goto fail1;
        /* GetDisplayname works for both video and audio, DevicePath doesn't */
        r = IMoniker_GetDisplayName(m, bind_ctx, NULL, &olestr);
        if (r != S_OK)
            goto fail1;
        unique_name = dup_wchar_to_utf8(olestr);
        /* replace ':' with '_' since we use : to delineate between sources */
        for (i = 0; i < strlen(unique_name); i++) {
            if (unique_name[i] == ':')
                unique_name[i] = '_';
        }

        r = IMoniker_BindToStorage(m, 0, 0, &IID_IPropertyBag, (void *) &bag);
        if (r != S_OK)
            goto fail1;

        var.vt = VT_BSTR;
        r = IPropertyBag_Read(bag, L"FriendlyName", &var, NULL);
        if (r != S_OK)
            goto fail1;
        friendly_name = dup_wchar_to_utf8(var.bstrVal);

        if (pfilter) {
            if (strcmp(device_name, friendly_name) && strcmp(device_name, unique_name))
                goto fail1;

            if (!skip--) {
                r = IMoniker_BindToObject(m, 0, 0, &IID_IBaseFilter, (void *) &device_filter);
                if (r != S_OK) {
                    av_log(avctx, AV_LOG_ERROR, "Unable to BindToObject for %s\n", device_name);
                    goto fail1;
                }
            }
        } else {
            av_log(avctx, AV_LOG_INFO, " \"%s\"\n", friendly_name);
            av_log(avctx, AV_LOG_INFO, "    Alternative name \"%s\"\n", unique_name);
        }

fail1:
        if (olestr && co_malloc)
            IMalloc_Free(co_malloc, olestr);
        if (bind_ctx)
            IBindCtx_Release(bind_ctx);
        av_free(friendly_name);
        av_free(unique_name);
        if (bag)
            IPropertyBag_Release(bag);
        IMoniker_Release(m);
    }

    IEnumMoniker_Release(classenum);

    if (pfilter) {
        if (!device_filter) {
            av_log(avctx, AV_LOG_ERROR, "Could not find %s device with name [%s] among source devices of type %s.\n",
                   devtypename, device_name, sourcetypename);
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
    int i, n, size, r;

    if (IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **) &config) != S_OK)
        return;
    if (IAMStreamConfig_GetNumberOfCapabilities(config, &n, &size) != S_OK)
        goto end;

    caps = av_malloc(size);
    if (!caps)
        goto end;

    for (i = 0; i < n && !format_set; i++) {
        r = IAMStreamConfig_GetStreamCaps(config, i, &type, (void *) caps);
        if (r != S_OK)
            goto next;
#if DSHOWDEBUG
        ff_print_AM_MEDIA_TYPE(type);
#endif

        if (devtype == VideoDevice) {
            VIDEO_STREAM_CONFIG_CAPS *vcaps = caps;
            BITMAPINFOHEADER *bih;
            int64_t *fr;
            const AVCodecTag *const tags[] = { avformat_get_riff_video_tags(), NULL };
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
                enum AVPixelFormat pix_fmt = dshow_pixfmt(bih->biCompression, bih->biBitCount);
                if (pix_fmt == AV_PIX_FMT_NONE) {
                    enum AVCodecID codec_id = av_codec_get_id(tags, bih->biCompression);
                    AVCodec *codec = avcodec_find_decoder(codec_id);
                    if (codec_id == AV_CODEC_ID_NONE || !codec) {
                        av_log(avctx, AV_LOG_INFO, "  unknown compression type 0x%X", (int) bih->biCompression);
                    } else {
                        av_log(avctx, AV_LOG_INFO, "  vcodec=%s", codec->name);
                    }
                } else {
                    av_log(avctx, AV_LOG_INFO, "  pixel_format=%s", av_get_pix_fmt_name(pix_fmt));
                }
                av_log(avctx, AV_LOG_INFO, "  min s=%ldx%ld fps=%g max s=%ldx%ld fps=%g\n",
                       vcaps->MinOutputSize.cx, vcaps->MinOutputSize.cy,
                       1e7 / vcaps->MaxFrameInterval,
                       vcaps->MaxOutputSize.cx, vcaps->MaxOutputSize.cy,
                       1e7 / vcaps->MinFrameInterval);
                continue;
            }
            if (ctx->video_codec_id != AV_CODEC_ID_RAWVIDEO) {
                if (ctx->video_codec_id != av_codec_get_id(tags, bih->biCompression))
                    goto next;
            }
            if (ctx->pixel_format != AV_PIX_FMT_NONE &&
                ctx->pixel_format != dshow_pixfmt(bih->biCompression, bih->biBitCount)) {
                goto next;
            }
            if (ctx->framerate) {
                int64_t framerate = ((int64_t) ctx->requested_framerate.den*10000000)
                                            /  ctx->requested_framerate.num;
                if (framerate > vcaps->MaxFrameInterval ||
                    framerate < vcaps->MinFrameInterval)
                    goto next;
                *fr = framerate;
            }
            if (ctx->requested_width && ctx->requested_height) {
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
    av_free(caps);
    if (pformat_set)
        *pformat_set = format_set;
}

/**
 * Set audio device buffer size in milliseconds (which can directly impact
 * latency, depending on the device).
 */
static int
dshow_set_audio_buffer_size(AVFormatContext *avctx, IPin *pin)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IAMBufferNegotiation *buffer_negotiation = NULL;
    ALLOCATOR_PROPERTIES props = { -1, -1, -1, -1 };
    IAMStreamConfig *config = NULL;
    AM_MEDIA_TYPE *type = NULL;
    int ret = AVERROR(EIO);

    if (IPin_QueryInterface(pin, &IID_IAMStreamConfig, (void **) &config) != S_OK)
        goto end;
    if (IAMStreamConfig_GetFormat(config, &type) != S_OK)
        goto end;
    if (!IsEqualGUID(&type->formattype, &FORMAT_WaveFormatEx))
        goto end;

    props.cbBuffer = (((WAVEFORMATEX *) type->pbFormat)->nAvgBytesPerSec)
                   * ctx->audio_buffer_size / 1000;

    if (IPin_QueryInterface(pin, &IID_IAMBufferNegotiation, (void **) &buffer_negotiation) != S_OK)
        goto end;
    if (IAMBufferNegotiation_SuggestAllocatorProperties(buffer_negotiation, &props) != S_OK)
        goto end;

    ret = 0;

end:
    if (buffer_negotiation)
        IAMBufferNegotiation_Release(buffer_negotiation);
    if (type) {
        if (type->pbFormat)
            CoTaskMemFree(type->pbFormat);
        CoTaskMemFree(type);
    }
    if (config)
        IAMStreamConfig_Release(config);

    return ret;
}

/**
 * Pops up a user dialog allowing them to adjust properties for the given filter, if possible.
 */
void
dshow_show_filter_properties(IBaseFilter *device_filter, AVFormatContext *avctx) {
    ISpecifyPropertyPages *property_pages = NULL;
    IUnknown *device_filter_iunknown = NULL;
    HRESULT hr;
    FILTER_INFO filter_info = {0}; /* a warning on this line is false positive GCC bug 53119 AFAICT */
    CAUUID ca_guid = {0};

    hr  = IBaseFilter_QueryInterface(device_filter, &IID_ISpecifyPropertyPages, (void **)&property_pages);
    if (hr != S_OK) {
        av_log(avctx, AV_LOG_WARNING, "requested filter does not have a property page to show");
        goto end;
    }
    hr = IBaseFilter_QueryFilterInfo(device_filter, &filter_info);
    if (hr != S_OK) {
        goto fail;
    }
    hr = IBaseFilter_QueryInterface(device_filter, &IID_IUnknown, (void **)&device_filter_iunknown);
    if (hr != S_OK) {
        goto fail;
    }
    hr = ISpecifyPropertyPages_GetPages(property_pages, &ca_guid);
    if (hr != S_OK) {
        goto fail;
    }
    hr = OleCreatePropertyFrame(NULL, 0, 0, filter_info.achName, 1, &device_filter_iunknown, ca_guid.cElems,
        ca_guid.pElems, 0, 0, NULL);
    if (hr != S_OK) {
        goto fail;
    }
    goto end;
fail:
    av_log(avctx, AV_LOG_ERROR, "Failure showing property pages for filter");
end:
    if (property_pages)
        ISpecifyPropertyPages_Release(property_pages);
    if (device_filter_iunknown)
        IUnknown_Release(device_filter_iunknown);
    if (filter_info.pGraph)
        IFilterGraph_Release(filter_info.pGraph);
    if (ca_guid.pElems)
        CoTaskMemFree(ca_guid.pElems);
}

/**
 * Cycle through available pins using the device_filter device, of type
 * devtype, retrieve the first output pin and return the pointer to the
 * object found in *ppin.
 * If ppin is NULL, cycle through all pins listing audio/video capabilities.
 */
static int
dshow_cycle_pins(AVFormatContext *avctx, enum dshowDeviceType devtype,
                 enum dshowSourceFilterType sourcetype, IBaseFilter *device_filter, IPin **ppin)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IEnumPins *pins = 0;
    IPin *device_pin = NULL;
    IPin *pin;
    int r;

    const GUID *mediatype[2] = { &MEDIATYPE_Video, &MEDIATYPE_Audio };
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio only";
    const char *sourcetypename = (sourcetype == VideoSourceDevice) ? "video" : "audio";

    int set_format = (devtype == VideoDevice && (ctx->framerate ||
                                                (ctx->requested_width && ctx->requested_height) ||
                                                 ctx->pixel_format != AV_PIX_FMT_NONE ||
                                                 ctx->video_codec_id != AV_CODEC_ID_RAWVIDEO))
                  || (devtype == AudioDevice && (ctx->channels || ctx->sample_rate));
    int format_set = 0;
    int should_show_properties = (devtype == VideoDevice) ? ctx->show_video_device_dialog : ctx->show_audio_device_dialog;

    if (should_show_properties)
        dshow_show_filter_properties(device_filter, avctx);

    r = IBaseFilter_EnumPins(device_filter, &pins);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enumerate pins.\n");
        return AVERROR(EIO);
    }

    if (!ppin) {
        av_log(avctx, AV_LOG_INFO, "DirectShow %s device options (from %s devices)\n",
               devtypename, sourcetypename);
    }

    while (!device_pin && IEnumPins_Next(pins, 1, &pin, NULL) == S_OK) {
        IKsPropertySet *p = NULL;
        IEnumMediaTypes *types = NULL;
        PIN_INFO info = {0};
        AM_MEDIA_TYPE *type;
        GUID category;
        DWORD r2;
        char *name_buf = NULL;
        wchar_t *pin_id = NULL;
        char *pin_buf = NULL;
        char *desired_pin_name = devtype == VideoDevice ? ctx->video_pin_name : ctx->audio_pin_name;

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
        name_buf = dup_wchar_to_utf8(info.achName);

        r = IPin_QueryId(pin, &pin_id);
        if (r != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not query pin id\n");
            return AVERROR(EIO);
        }
        pin_buf = dup_wchar_to_utf8(pin_id);

        if (!ppin) {
            av_log(avctx, AV_LOG_INFO, " Pin \"%s\" (alternative pin name \"%s\")\n", name_buf, pin_buf);
            dshow_cycle_formats(avctx, devtype, pin, NULL);
            goto next;
        }

        if (desired_pin_name) {
            if(strcmp(name_buf, desired_pin_name) && strcmp(pin_buf, desired_pin_name)) {
                av_log(avctx, AV_LOG_DEBUG, "skipping pin \"%s\" (\"%s\") != requested \"%s\"\n",
                    name_buf, pin_buf, desired_pin_name);
                goto next;
            }
        }

        if (set_format) {
            dshow_cycle_formats(avctx, devtype, pin, &format_set);
            if (!format_set) {
                goto next;
            }
        }
        if (devtype == AudioDevice && ctx->audio_buffer_size) {
            if (dshow_set_audio_buffer_size(avctx, pin) < 0) {
                av_log(avctx, AV_LOG_ERROR, "unable to set audio buffer size %d to pin, using pin anyway...", ctx->audio_buffer_size);
            }
        }

        if (IPin_EnumMediaTypes(pin, &types) != S_OK)
            goto next;

        IEnumMediaTypes_Reset(types);
        /* in case format_set was not called, just verify the majortype */
        while (!device_pin && IEnumMediaTypes_Next(types, 1, &type, NULL) == S_OK) {
            if (IsEqualGUID(&type->majortype, mediatype[devtype])) {
                device_pin = pin;
                av_log(avctx, AV_LOG_DEBUG, "Selecting pin %s on %s\n", name_buf, devtypename);
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
        av_free(name_buf);
        av_free(pin_buf);
        if (pin_id)
            CoTaskMemFree(pin_id);
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
 * List options for device with type devtype, source filter type sourcetype
 *
 * @param devenum device enumerator used for accessing the device
 */
static int
dshow_list_device_options(AVFormatContext *avctx, ICreateDevEnum *devenum,
                          enum dshowDeviceType devtype, enum dshowSourceFilterType sourcetype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    int r;

    if ((r = dshow_cycle_devices(avctx, devenum, devtype, sourcetype, &device_filter)) < 0)
        return r;
    ctx->device_filter[devtype] = device_filter;
    if ((r = dshow_cycle_pins(avctx, devtype, sourcetype, device_filter, NULL)) < 0)
        return r;

    return 0;
}

static int
dshow_open_device(AVFormatContext *avctx, ICreateDevEnum *devenum,
                  enum dshowDeviceType devtype, enum dshowSourceFilterType sourcetype)
{
    struct dshow_ctx *ctx = avctx->priv_data;
    IBaseFilter *device_filter = NULL;
    IGraphBuilder *graph = ctx->graph;
    IPin *device_pin = NULL;
    libAVPin *capture_pin = NULL;
    libAVFilter *capture_filter = NULL;
    ICaptureGraphBuilder2 *graph_builder2 = NULL;
    int ret = AVERROR(EIO);
    int r;
    IStream *ifile_stream = NULL;
    IStream *ofile_stream = NULL;
    IPersistStream *pers_stream = NULL;

    const wchar_t *filter_name[2] = { L"Audio capture filter", L"Video capture filter" };


    if ( ((ctx->audio_filter_load_file) && (strlen(ctx->audio_filter_load_file)>0) && (sourcetype == AudioSourceDevice)) ||
            ((ctx->video_filter_load_file) && (strlen(ctx->video_filter_load_file)>0) && (sourcetype == VideoSourceDevice)) ) {
        HRESULT hr;
        char *filename = NULL;

        if (sourcetype == AudioSourceDevice)
            filename = ctx->audio_filter_load_file;
        else
            filename = ctx->video_filter_load_file;

        hr = SHCreateStreamOnFile ((LPCSTR) filename, STGM_READ, &ifile_stream);
        if (S_OK != hr) {
            av_log(avctx, AV_LOG_ERROR, "Could not open capture filter description file.\n");
            goto error;
        }

        hr = OleLoadFromStream(ifile_stream, &IID_IBaseFilter, (void **) &device_filter);
        if (hr != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not load capture filter from file.\n");
            goto error;
        }

        if (sourcetype == AudioSourceDevice)
            av_log(avctx, AV_LOG_INFO, "Audio-");
        else
            av_log(avctx, AV_LOG_INFO, "Video-");
        av_log(avctx, AV_LOG_INFO, "Capture filter loaded successfully from file \"%s\".\n", filename);
    } else {

        if ((r = dshow_cycle_devices(avctx, devenum, devtype, sourcetype, &device_filter)) < 0) {
            ret = r;
            goto error;
        }
    }

    ctx->device_filter [devtype] = device_filter;

    r = IGraphBuilder_AddFilter(graph, device_filter, NULL);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not add device filter to graph.\n");
        goto error;
    }

    if ((r = dshow_cycle_pins(avctx, devtype, sourcetype, device_filter, &device_pin)) < 0) {
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

    if ( ((ctx->audio_filter_save_file) && (strlen(ctx->audio_filter_save_file)>0) && (sourcetype == AudioSourceDevice)) ||
            ((ctx->video_filter_save_file) && (strlen(ctx->video_filter_save_file)>0) && (sourcetype == VideoSourceDevice)) ) {

        HRESULT hr;
        char *filename = NULL;

        if (sourcetype == AudioSourceDevice)
            filename = ctx->audio_filter_save_file;
        else
            filename = ctx->video_filter_save_file;

        hr = SHCreateStreamOnFile ((LPCSTR) filename, STGM_CREATE | STGM_READWRITE, &ofile_stream);
        if (S_OK != hr) {
            av_log(avctx, AV_LOG_ERROR, "Could not create capture filter description file.\n");
            goto error;
        }

        hr  = IBaseFilter_QueryInterface(device_filter, &IID_IPersistStream, (void **) &pers_stream);
        if (hr != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Query for IPersistStream failed.\n");
            goto error;
        }

        hr = OleSaveToStream(pers_stream, ofile_stream);
        if (hr != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not save capture filter \n");
            goto error;
        }

        hr = IStream_Commit(ofile_stream, STGC_DEFAULT);
        if (S_OK != hr) {
            av_log(avctx, AV_LOG_ERROR, "Could not commit capture filter data to file.\n");
            goto error;
        }

        if (sourcetype == AudioSourceDevice)
            av_log(avctx, AV_LOG_INFO, "Audio-");
        else
            av_log(avctx, AV_LOG_INFO, "Video-");
        av_log(avctx, AV_LOG_INFO, "Capture filter saved successfully to file \"%s\".\n", filename);
    }

    r = IGraphBuilder_AddFilter(graph, (IBaseFilter *) capture_filter,
                                filter_name[devtype]);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not add capture filter to graph\n");
        goto error;
    }

    libAVPin_AddRef(capture_filter->pin);
    capture_pin = capture_filter->pin;
    ctx->capture_pin[devtype] = capture_pin;

    r = CoCreateInstance(&CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER,
                         &IID_ICaptureGraphBuilder2, (void **) &graph_builder2);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not create CaptureGraphBuilder2\n");
        goto error;
    }
    ICaptureGraphBuilder2_SetFiltergraph(graph_builder2, graph);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not set graph for CaptureGraphBuilder2\n");
        goto error;
    }

    r = ICaptureGraphBuilder2_RenderStream(graph_builder2, NULL, NULL, (IUnknown *) device_pin, NULL /* no intermediate filter */,
        (IBaseFilter *) capture_filter); /* connect pins, optionally insert intermediate filters like crossbar if necessary */

    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not RenderStream to connect pins\n");
        goto error;
    }

    r = dshow_try_setup_crossbar_options(graph_builder2, device_filter, devtype, avctx);

    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not setup CrossBar\n");
        goto error;
    }

    ret = 0;

error:
    if (graph_builder2 != NULL)
        ICaptureGraphBuilder2_Release(graph_builder2);

    if (pers_stream)
        IPersistStream_Release(pers_stream);

    if (ifile_stream)
        IStream_Release(ifile_stream);

    if (ofile_stream)
        IStream_Release(ofile_stream);

    return ret;
}

static enum AVCodecID waveform_codec_id(enum AVSampleFormat sample_fmt)
{
    switch (sample_fmt) {
    case AV_SAMPLE_FMT_U8:  return AV_CODEC_ID_PCM_U8;
    case AV_SAMPLE_FMT_S16: return AV_CODEC_ID_PCM_S16LE;
    case AV_SAMPLE_FMT_S32: return AV_CODEC_ID_PCM_S32LE;
    default:                return AV_CODEC_ID_NONE; /* Should never happen. */
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
        codec->codec_tag  = bih->biCompression;
        codec->pix_fmt    = dshow_pixfmt(bih->biCompression, bih->biBitCount);
        if (bih->biCompression == MKTAG('H', 'D', 'Y', 'C')) {
            av_log(avctx, AV_LOG_DEBUG, "attempt to use full range for HDYC...\n");
            codec->color_range = AVCOL_RANGE_MPEG; // just in case it needs this...
        }
        if (codec->pix_fmt == AV_PIX_FMT_NONE) {
            const AVCodecTag *const tags[] = { avformat_get_riff_video_tags(), NULL };
            codec->codec_id = av_codec_get_id(tags, bih->biCompression);
            if (codec->codec_id == AV_CODEC_ID_NONE) {
                av_log(avctx, AV_LOG_ERROR, "Unknown compression type. "
                                 "Please report type 0x%X.\n", (int) bih->biCompression);
                return AVERROR_PATCHWELCOME;
            }
            codec->bits_per_coded_sample = bih->biBitCount;
        } else {
            codec->codec_id = AV_CODEC_ID_RAWVIDEO;
            if (bih->biCompression == BI_RGB || bih->biCompression == BI_BITFIELDS) {
                codec->bits_per_coded_sample = bih->biBitCount;
                codec->extradata = av_malloc(9 + AV_INPUT_BUFFER_PADDING_SIZE);
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
    IMediaEvent *media_event = NULL;
    HANDLE media_event_handle;
    HANDLE proc;
    int ret = AVERROR(EIO);
    int r;

    CoInitialize(0);

    if (!ctx->list_devices && !parse_device_name(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Malformed dshow input string.\n");
        goto error;
    }

    ctx->video_codec_id = avctx->video_codec_id ? avctx->video_codec_id
                                                : AV_CODEC_ID_RAWVIDEO;
    if (ctx->pixel_format != AV_PIX_FMT_NONE) {
        if (ctx->video_codec_id != AV_CODEC_ID_RAWVIDEO) {
            av_log(avctx, AV_LOG_ERROR, "Pixel format may only be set when "
                              "video codec is not set or set to rawvideo\n");
            ret = AVERROR(EINVAL);
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
        av_log(avctx, AV_LOG_INFO, "DirectShow video devices (some may be both video and audio devices)\n");
        dshow_cycle_devices(avctx, devenum, VideoDevice, VideoSourceDevice, NULL);
        av_log(avctx, AV_LOG_INFO, "DirectShow audio devices\n");
        dshow_cycle_devices(avctx, devenum, AudioDevice, AudioSourceDevice, NULL);
        ret = AVERROR_EXIT;
        goto error;
    }
    if (ctx->list_options) {
        if (ctx->device_name[VideoDevice])
            if ((r = dshow_list_device_options(avctx, devenum, VideoDevice, VideoSourceDevice))) {
                ret = r;
                goto error;
            }
        if (ctx->device_name[AudioDevice]) {
            if (dshow_list_device_options(avctx, devenum, AudioDevice, AudioSourceDevice)) {
                /* show audio options from combined video+audio sources as fallback */
                if ((r = dshow_list_device_options(avctx, devenum, AudioDevice, VideoSourceDevice))) {
                    ret = r;
                    goto error;
                }
            }
        }
    }
    if (ctx->device_name[VideoDevice]) {
        if ((r = dshow_open_device(avctx, devenum, VideoDevice, VideoSourceDevice)) < 0 ||
            (r = dshow_add_device(avctx, VideoDevice)) < 0) {
            ret = r;
            goto error;
        }
    }
    if (ctx->device_name[AudioDevice]) {
        if ((r = dshow_open_device(avctx, devenum, AudioDevice, AudioSourceDevice)) < 0 ||
            (r = dshow_add_device(avctx, AudioDevice)) < 0) {
            av_log(avctx, AV_LOG_INFO, "Searching for audio device within video devices for %s\n", ctx->device_name[AudioDevice]);
            /* see if there's a video source with an audio pin with the given audio name */
            if ((r = dshow_open_device(avctx, devenum, AudioDevice, VideoSourceDevice)) < 0 ||
                (r = dshow_add_device(avctx, AudioDevice)) < 0) {
                ret = r;
                goto error;
            }
        }
    }
    if (ctx->list_options) {
        /* allow it to list crossbar options in dshow_open_device */
        ret = AVERROR_EXIT;
        goto error;
    }
    ctx->curbufsize[0] = 0;
    ctx->curbufsize[1] = 0;
    ctx->mutex = CreateMutex(NULL, 0, NULL);
    if (!ctx->mutex) {
        av_log(avctx, AV_LOG_ERROR, "Could not create Mutex\n");
        goto error;
    }
    ctx->event[1] = CreateEvent(NULL, 1, 0, NULL);
    if (!ctx->event[1]) {
        av_log(avctx, AV_LOG_ERROR, "Could not create Event\n");
        goto error;
    }

    r = IGraphBuilder_QueryInterface(graph, &IID_IMediaControl, (void **) &control);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get media control.\n");
        goto error;
    }
    ctx->control = control;

    r = IGraphBuilder_QueryInterface(graph, &IID_IMediaEvent, (void **) &media_event);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get media event.\n");
        goto error;
    }
    ctx->media_event = media_event;

    r = IMediaEvent_GetEventHandle(media_event, (void *) &media_event_handle);
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get media event handle.\n");
        goto error;
    }
    proc = GetCurrentProcess();
    r = DuplicateHandle(proc, media_event_handle, proc, &ctx->event[0],
                        0, 0, DUPLICATE_SAME_ACCESS);
    if (!r) {
        av_log(avctx, AV_LOG_ERROR, "Could not duplicate media event handle.\n");
        goto error;
    }

    r = IMediaControl_Run(control);
    if (r == S_FALSE) {
        OAFilterState pfs;
        r = IMediaControl_GetState(control, 0, &pfs);
    }
    if (r != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not run graph (sometimes caused by a device already in use by other application)\n");
        goto error;
    }

    ret = 0;

error:

    if (devenum)
        ICreateDevEnum_Release(devenum);

    if (ret < 0)
        dshow_read_close(avctx);

    return ret;
}

/**
 * Checks media events from DirectShow and returns -1 on error or EOF. Also
 * purges all events that might be in the event queue to stop the trigger
 * of event notification.
 */
static int dshow_check_event_queue(IMediaEvent *media_event)
{
    LONG_PTR p1, p2;
    long code;
    int ret = 0;

    while (IMediaEvent_GetEvent(media_event, &code, &p1, &p2, 0) != E_ABORT) {
        if (code == EC_COMPLETE || code == EC_DEVICE_LOST || code == EC_ERRORABORT)
            ret = -1;
        IMediaEvent_FreeEventParams(media_event, code, p1, p2);
    }

    return ret;
}

static int dshow_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    struct dshow_ctx *ctx = s->priv_data;
    AVPacketList *pktl = NULL;

    while (!ctx->eof && !pktl) {
        WaitForSingleObject(ctx->mutex, INFINITE);
        pktl = ctx->pktl;
        if (pktl) {
            *pkt = pktl->pkt;
            ctx->pktl = ctx->pktl->next;
            av_free(pktl);
            ctx->curbufsize[pkt->stream_index] -= pkt->size;
        }
        ResetEvent(ctx->event[1]);
        ReleaseMutex(ctx->mutex);
        if (!pktl) {
            if (dshow_check_event_queue(ctx->media_event) < 0) {
                ctx->eof = 1;
            } else if (s->flags & AVFMT_FLAG_NONBLOCK) {
                return AVERROR(EAGAIN);
            } else {
                WaitForMultipleObjects(2, ctx->event, 0, INFINITE);
            }
        }
    }

    return ctx->eof ? AVERROR(EIO) : pkt->size;
}

#define OFFSET(x) offsetof(struct dshow_ctx, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "video_size", "set video size given a string such as 640x480 or hd720.", OFFSET(requested_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "pixel_format", "set video pixel format", OFFSET(pixel_format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_NONE}, -1, INT_MAX, DEC },
    { "framerate", "set video frame rate", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "sample_rate", "set audio sample rate", OFFSET(sample_rate), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "sample_size", "set audio sample size", OFFSET(sample_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 16, DEC },
    { "channels", "set number of audio channels, such as 1 or 2", OFFSET(channels), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "audio_buffer_size", "set audio device buffer latency size in milliseconds (default is the device's default)", OFFSET(audio_buffer_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "list_devices", "list available devices",                      OFFSET(list_devices), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, DEC },
    { "list_options", "list available options for specified device", OFFSET(list_options), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, DEC },
    { "video_device_number", "set video device number for devices with same name (starts at 0)", OFFSET(video_device_number), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "audio_device_number", "set audio device number for devices with same name (starts at 0)", OFFSET(audio_device_number), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, DEC },
    { "video_pin_name", "select video capture pin by name", OFFSET(video_pin_name),AV_OPT_TYPE_STRING, {.str = NULL},  0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "audio_pin_name", "select audio capture pin by name", OFFSET(audio_pin_name),AV_OPT_TYPE_STRING, {.str = NULL},  0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "crossbar_video_input_pin_number", "set video input pin number for crossbar device", OFFSET(crossbar_video_input_pin_number), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, DEC },
    { "crossbar_audio_input_pin_number", "set audio input pin number for crossbar device", OFFSET(crossbar_audio_input_pin_number), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, DEC },
    { "show_video_device_dialog",              "display property dialog for video capture device",                            OFFSET(show_video_device_dialog),              AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "show_audio_device_dialog",              "display property dialog for audio capture device",                            OFFSET(show_audio_device_dialog),              AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "show_video_crossbar_connection_dialog", "display property dialog for crossbar connecting pins filter on video device", OFFSET(show_video_crossbar_connection_dialog), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "show_audio_crossbar_connection_dialog", "display property dialog for crossbar connecting pins filter on audio device", OFFSET(show_audio_crossbar_connection_dialog), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "show_analog_tv_tuner_dialog",           "display property dialog for analog tuner filter",                             OFFSET(show_analog_tv_tuner_dialog),           AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "show_analog_tv_tuner_audio_dialog",     "display property dialog for analog tuner audio filter",                       OFFSET(show_analog_tv_tuner_audio_dialog),     AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
    { "audio_device_load", "load audio capture filter device (and properties) from file", OFFSET(audio_filter_load_file), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "audio_device_save", "save audio capture filter device (and properties) to file", OFFSET(audio_filter_save_file), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "video_device_load", "load video capture filter device (and properties) from file", OFFSET(video_filter_load_file), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "video_device_save", "save video capture filter device (and properties) to file", OFFSET(video_filter_save_file), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { NULL },
};

static const AVClass dshow_class = {
    .class_name = "dshow indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
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
