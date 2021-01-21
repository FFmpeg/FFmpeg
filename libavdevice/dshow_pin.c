/*
 * DirectShow capture interface
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

#include <stddef.h>
#define imemoffset offsetof(DShowPin, imemvtbl)

DECLARE_QUERYINTERFACE(pin, DShowPin,
    { {&IID_IUnknown,0}, {&IID_IPin,0}, {&IID_IMemInputPin,imemoffset} })
DECLARE_ADDREF(pin, DShowPin)
DECLARE_RELEASE(pin, DShowPin)

long ff_dshow_pin_Connect(DShowPin *this, IPin *pin, const AM_MEDIA_TYPE *type)
{
    dshowdebug("ff_dshow_pin_Connect(%p, %p, %p)\n", this, pin, type);
    /* Input pins receive connections. */
    return S_FALSE;
}
long ff_dshow_pin_ReceiveConnection(DShowPin *this, IPin *pin,
                           const AM_MEDIA_TYPE *type)
{
    enum dshowDeviceType devtype = this->filter->type;
    dshowdebug("ff_dshow_pin_ReceiveConnection(%p)\n", this);

    if (!pin)
        return E_POINTER;
    if (this->connectedto)
        return VFW_E_ALREADY_CONNECTED;

    ff_print_AM_MEDIA_TYPE(type);
    if (devtype == VideoDevice) {
        if (!IsEqualGUID(&type->majortype, &MEDIATYPE_Video))
            return VFW_E_TYPE_NOT_ACCEPTED;
    } else {
        if (!IsEqualGUID(&type->majortype, &MEDIATYPE_Audio))
            return VFW_E_TYPE_NOT_ACCEPTED;
    }

    IPin_AddRef(pin);
    this->connectedto = pin;

    ff_copy_dshow_media_type(&this->type, type);

    return S_OK;
}
long ff_dshow_pin_Disconnect(DShowPin *this)
{
    dshowdebug("ff_dshow_pin_Disconnect(%p)\n", this);

    if (this->filter->state != State_Stopped)
        return VFW_E_NOT_STOPPED;
    if (!this->connectedto)
        return S_FALSE;
    IPin_Release(this->connectedto);
    this->connectedto = NULL;

    return S_OK;
}
long ff_dshow_pin_ConnectedTo(DShowPin *this, IPin **pin)
{
    dshowdebug("ff_dshow_pin_ConnectedTo(%p)\n", this);

    if (!pin)
        return E_POINTER;
    if (!this->connectedto)
        return VFW_E_NOT_CONNECTED;
    IPin_AddRef(this->connectedto);
    *pin = this->connectedto;

    return S_OK;
}
long ff_dshow_pin_ConnectionMediaType(DShowPin *this, AM_MEDIA_TYPE *type)
{
    dshowdebug("ff_dshow_pin_ConnectionMediaType(%p)\n", this);

    if (!type)
        return E_POINTER;
    if (!this->connectedto)
        return VFW_E_NOT_CONNECTED;

    return ff_copy_dshow_media_type(type, &this->type);
}
long ff_dshow_pin_QueryPinInfo(DShowPin *this, PIN_INFO *info)
{
    dshowdebug("ff_dshow_pin_QueryPinInfo(%p)\n", this);

    if (!info)
        return E_POINTER;

    if (this->filter)
        ff_dshow_filter_AddRef(this->filter);

    info->pFilter = (IBaseFilter *) this->filter;
    info->dir     = PINDIR_INPUT;
    wcscpy(info->achName, L"Capture");

    return S_OK;
}
long ff_dshow_pin_QueryDirection(DShowPin *this, PIN_DIRECTION *dir)
{
    dshowdebug("ff_dshow_pin_QueryDirection(%p)\n", this);
    if (!dir)
        return E_POINTER;
    *dir = PINDIR_INPUT;
    return S_OK;
}
long ff_dshow_pin_QueryId(DShowPin *this, wchar_t **id)
{
    dshowdebug("ff_dshow_pin_QueryId(%p)\n", this);

    if (!id)
        return E_POINTER;

    *id = wcsdup(L"libAV Pin");

    return S_OK;
}
long ff_dshow_pin_QueryAccept(DShowPin *this, const AM_MEDIA_TYPE *type)
{
    dshowdebug("ff_dshow_pin_QueryAccept(%p)\n", this);
    return S_FALSE;
}
long ff_dshow_pin_EnumMediaTypes(DShowPin *this, IEnumMediaTypes **enumtypes)
{
    const AM_MEDIA_TYPE *type = NULL;
    DShowEnumMediaTypes *new;
    dshowdebug("ff_dshow_pin_EnumMediaTypes(%p)\n", this);

    if (!enumtypes)
        return E_POINTER;
    new = ff_dshow_enummediatypes_Create(type);
    if (!new)
        return E_OUTOFMEMORY;

    *enumtypes = (IEnumMediaTypes *) new;
    return S_OK;
}
long ff_dshow_pin_QueryInternalConnections(DShowPin *this, IPin **pin,
                                  unsigned long *npin)
{
    dshowdebug("ff_dshow_pin_QueryInternalConnections(%p)\n", this);
    return E_NOTIMPL;
}
long ff_dshow_pin_EndOfStream(DShowPin *this)
{
    dshowdebug("ff_dshow_pin_EndOfStream(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long ff_dshow_pin_BeginFlush(DShowPin *this)
{
    dshowdebug("ff_dshow_pin_BeginFlush(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long ff_dshow_pin_EndFlush(DShowPin *this)
{
    dshowdebug("ff_dshow_pin_EndFlush(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long ff_dshow_pin_NewSegment(DShowPin *this, REFERENCE_TIME start, REFERENCE_TIME stop,
                    double rate)
{
    dshowdebug("ff_dshow_pin_NewSegment(%p)\n", this);
    /* I don't care. */
    return S_OK;
}

static int ff_dshow_pin_Setup(DShowPin *this, DShowFilter *filter)
{
    IPinVtbl *vtbl = this->vtbl;
    IMemInputPinVtbl *imemvtbl;

    if (!filter)
        return 0;

    imemvtbl = av_malloc(sizeof(IMemInputPinVtbl));
    if (!imemvtbl)
        return 0;

    SETVTBL(imemvtbl, meminputpin, QueryInterface);
    SETVTBL(imemvtbl, meminputpin, AddRef);
    SETVTBL(imemvtbl, meminputpin, Release);
    SETVTBL(imemvtbl, meminputpin, GetAllocator);
    SETVTBL(imemvtbl, meminputpin, NotifyAllocator);
    SETVTBL(imemvtbl, meminputpin, GetAllocatorRequirements);
    SETVTBL(imemvtbl, meminputpin, Receive);
    SETVTBL(imemvtbl, meminputpin, ReceiveMultiple);
    SETVTBL(imemvtbl, meminputpin, ReceiveCanBlock);

    this->imemvtbl = imemvtbl;

    SETVTBL(vtbl, pin, QueryInterface);
    SETVTBL(vtbl, pin, AddRef);
    SETVTBL(vtbl, pin, Release);
    SETVTBL(vtbl, pin, Connect);
    SETVTBL(vtbl, pin, ReceiveConnection);
    SETVTBL(vtbl, pin, Disconnect);
    SETVTBL(vtbl, pin, ConnectedTo);
    SETVTBL(vtbl, pin, ConnectionMediaType);
    SETVTBL(vtbl, pin, QueryPinInfo);
    SETVTBL(vtbl, pin, QueryDirection);
    SETVTBL(vtbl, pin, QueryId);
    SETVTBL(vtbl, pin, QueryAccept);
    SETVTBL(vtbl, pin, EnumMediaTypes);
    SETVTBL(vtbl, pin, QueryInternalConnections);
    SETVTBL(vtbl, pin, EndOfStream);
    SETVTBL(vtbl, pin, BeginFlush);
    SETVTBL(vtbl, pin, EndFlush);
    SETVTBL(vtbl, pin, NewSegment);

    this->filter = filter;

    return 1;
}

static void ff_dshow_pin_Free(DShowPin *this)
{
    if (!this)
        return;
    av_freep(&this->imemvtbl);
    if (this->type.pbFormat) {
        CoTaskMemFree(this->type.pbFormat);
        this->type.pbFormat = NULL;
    }
}
DECLARE_CREATE(pin, DShowPin, ff_dshow_pin_Setup(this, filter), DShowFilter *filter)
DECLARE_DESTROY(pin, DShowPin, ff_dshow_pin_Free)

/*****************************************************************************
 * DShowMemInputPin
 ****************************************************************************/
long ff_dshow_meminputpin_QueryInterface(DShowMemInputPin *this, const GUID *riid,
                                void **ppvObject)
{
    DShowPin *pin = (DShowPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("ff_dshow_meminputpin_QueryInterface(%p)\n", this);
    return ff_dshow_pin_QueryInterface(pin, riid, ppvObject);
}
unsigned long ff_dshow_meminputpin_AddRef(DShowMemInputPin *this)
{
    DShowPin *pin = (DShowPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("ff_dshow_meminputpin_AddRef(%p)\n", this);
    return ff_dshow_pin_AddRef(pin);
}
unsigned long ff_dshow_meminputpin_Release(DShowMemInputPin *this)
{
    DShowPin *pin = (DShowPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("ff_dshow_meminputpin_Release(%p)\n", this);
    return ff_dshow_pin_Release(pin);
}
long ff_dshow_meminputpin_GetAllocator(DShowMemInputPin *this, IMemAllocator **alloc)
{
    dshowdebug("ff_dshow_meminputpin_GetAllocator(%p)\n", this);
    return VFW_E_NO_ALLOCATOR;
}
long ff_dshow_meminputpin_NotifyAllocator(DShowMemInputPin *this, IMemAllocator *alloc,
                                 BOOL rdwr)
{
    dshowdebug("ff_dshow_meminputpin_NotifyAllocator(%p)\n", this);
    return S_OK;
}
long ff_dshow_meminputpin_GetAllocatorRequirements(DShowMemInputPin *this,
                                          ALLOCATOR_PROPERTIES *props)
{
    dshowdebug("ff_dshow_meminputpin_GetAllocatorRequirements(%p)\n", this);
    return E_NOTIMPL;
}
long ff_dshow_meminputpin_Receive(DShowMemInputPin *this, IMediaSample *sample)
{
    DShowPin *pin = (DShowPin *) ((uint8_t *) this - imemoffset);
    enum dshowDeviceType devtype = pin->filter->type;
    void *priv_data;
    AVFormatContext *s;
    uint8_t *buf;
    int buf_size; /* todo should be a long? */
    int index;
    int64_t curtime;
    int64_t orig_curtime;
    int64_t graphtime;
    const char *devtypename = (devtype == VideoDevice) ? "video" : "audio";
    IReferenceClock *clock = pin->filter->clock;
    int64_t dummy;
    struct dshow_ctx *ctx;


    dshowdebug("ff_dshow_meminputpin_Receive(%p)\n", this);

    if (!sample)
        return E_POINTER;

    IMediaSample_GetTime(sample, &orig_curtime, &dummy);
    orig_curtime += pin->filter->start_time;
    IReferenceClock_GetTime(clock, &graphtime);
    if (devtype == VideoDevice) {
        /* PTS from video devices is unreliable. */
        IReferenceClock_GetTime(clock, &curtime);
    } else {
        IMediaSample_GetTime(sample, &curtime, &dummy);
        if(curtime > 400000000000000000LL) {
            /* initial frames sometimes start < 0 (shown as a very large number here,
               like 437650244077016960 which FFmpeg doesn't like.
               TODO figure out math. For now just drop them. */
            av_log(NULL, AV_LOG_DEBUG,
                "dshow dropping initial (or ending) audio frame with odd PTS too high %"PRId64"\n", curtime);
            return S_OK;
        }
        curtime += pin->filter->start_time;
    }

    buf_size = IMediaSample_GetActualDataLength(sample);
    IMediaSample_GetPointer(sample, &buf);
    priv_data = pin->filter->priv_data;
    s = priv_data;
    ctx = s->priv_data;
    index = pin->filter->stream_index;

    av_log(NULL, AV_LOG_VERBOSE, "dshow passing through packet of type %s size %8d "
        "timestamp %"PRId64" orig timestamp %"PRId64" graph timestamp %"PRId64" diff %"PRId64" %s\n",
        devtypename, buf_size, curtime, orig_curtime, graphtime, graphtime - orig_curtime, ctx->device_name[devtype]);
    pin->filter->callback(priv_data, index, buf, buf_size, curtime, devtype);

    return S_OK;
}
long ff_dshow_meminputpin_ReceiveMultiple(DShowMemInputPin *this,
                                 IMediaSample **samples, long n, long *nproc)
{
    int i;
    dshowdebug("ff_dshow_meminputpin_ReceiveMultiple(%p)\n", this);

    for (i = 0; i < n; i++)
        ff_dshow_meminputpin_Receive(this, samples[i]);

    *nproc = n;
    return S_OK;
}
long ff_dshow_meminputpin_ReceiveCanBlock(DShowMemInputPin *this)
{
    dshowdebug("ff_dshow_meminputpin_ReceiveCanBlock(%p)\n", this);
    /* I swear I will not block. */
    return S_FALSE;
}

void ff_dshow_meminputpin_Destroy(DShowMemInputPin *this)
{
    DShowPin *pin = (DShowPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("ff_dshow_meminputpin_Destroy(%p)\n", this);
    ff_dshow_pin_Destroy(pin);
}
