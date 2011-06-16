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

#include "dshow.h"

#include <stddef.h>
#define imemoffset offsetof(libAVPin, imemvtbl)

DECLARE_QUERYINTERFACE(libAVPin,
    { {&IID_IUnknown,0}, {&IID_IPin,0}, {&IID_IMemInputPin,imemoffset} })
DECLARE_ADDREF(libAVPin)
DECLARE_RELEASE(libAVPin)

long WINAPI
libAVPin_Connect(libAVPin *this, IPin *pin, const AM_MEDIA_TYPE *type)
{
    dshowdebug("libAVPin_Connect(%p, %p, %p)\n", this, pin, type);
    /* Input pins receive connections. */
    return S_FALSE;
}
long WINAPI
libAVPin_ReceiveConnection(libAVPin *this, IPin *pin,
                           const AM_MEDIA_TYPE *type)
{
    enum dshowDeviceType devtype = this->filter->type;
    dshowdebug("libAVPin_ReceiveConnection(%p)\n", this);

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
long WINAPI
libAVPin_Disconnect(libAVPin *this)
{
    dshowdebug("libAVPin_Disconnect(%p)\n", this);

    if (this->filter->state != State_Stopped)
        return VFW_E_NOT_STOPPED;
    if (!this->connectedto)
        return S_FALSE;
    this->connectedto = NULL;

    return S_OK;
}
long WINAPI
libAVPin_ConnectedTo(libAVPin *this, IPin **pin)
{
    dshowdebug("libAVPin_ConnectedTo(%p)\n", this);

    if (!pin)
        return E_POINTER;
    if (!this->connectedto)
        return VFW_E_NOT_CONNECTED;
    IPin_AddRef(this->connectedto);
    *pin = this->connectedto;

    return S_OK;
}
long WINAPI
libAVPin_ConnectionMediaType(libAVPin *this, AM_MEDIA_TYPE *type)
{
    dshowdebug("libAVPin_ConnectionMediaType(%p)\n", this);

    if (!type)
        return E_POINTER;
    if (!this->connectedto)
        return VFW_E_NOT_CONNECTED;

    return ff_copy_dshow_media_type(type, &this->type);
}
long WINAPI
libAVPin_QueryPinInfo(libAVPin *this, PIN_INFO *info)
{
    dshowdebug("libAVPin_QueryPinInfo(%p)\n", this);

    if (!info)
        return E_POINTER;

    if (this->filter)
        libAVFilter_AddRef(this->filter);

    info->pFilter = (IBaseFilter *) this->filter;
    info->dir     = PINDIR_INPUT;
    wcscpy(info->achName, L"Capture");

    return S_OK;
}
long WINAPI
libAVPin_QueryDirection(libAVPin *this, PIN_DIRECTION *dir)
{
    dshowdebug("libAVPin_QueryDirection(%p)\n", this);
    if (!dir)
        return E_POINTER;
    *dir = PINDIR_INPUT;
    return S_OK;
}
long WINAPI
libAVPin_QueryId(libAVPin *this, wchar_t **id)
{
    dshowdebug("libAVPin_QueryId(%p)\n", this);

    if (!id)
        return E_POINTER;

    *id = wcsdup(L"libAV Pin");

    return S_OK;
}
long WINAPI
libAVPin_QueryAccept(libAVPin *this, const AM_MEDIA_TYPE *type)
{
    dshowdebug("libAVPin_QueryAccept(%p)\n", this);
    return S_FALSE;
}
long WINAPI
libAVPin_EnumMediaTypes(libAVPin *this, IEnumMediaTypes **enumtypes)
{
    const AM_MEDIA_TYPE *type = NULL;
    libAVEnumMediaTypes *new;
    dshowdebug("libAVPin_EnumMediaTypes(%p)\n", this);

    if (!enumtypes)
        return E_POINTER;
    new = libAVEnumMediaTypes_Create(type);
    if (!new)
        return E_OUTOFMEMORY;

    *enumtypes = (IEnumMediaTypes *) new;
    return S_OK;
}
long WINAPI
libAVPin_QueryInternalConnections(libAVPin *this, IPin **pin,
                                  unsigned long *npin)
{
    dshowdebug("libAVPin_QueryInternalConnections(%p)\n", this);
    return E_NOTIMPL;
}
long WINAPI
libAVPin_EndOfStream(libAVPin *this)
{
    dshowdebug("libAVPin_EndOfStream(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long WINAPI
libAVPin_BeginFlush(libAVPin *this)
{
    dshowdebug("libAVPin_BeginFlush(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long WINAPI
libAVPin_EndFlush(libAVPin *this)
{
    dshowdebug("libAVPin_EndFlush(%p)\n", this);
    /* I don't care. */
    return S_OK;
}
long WINAPI
libAVPin_NewSegment(libAVPin *this, REFERENCE_TIME start, REFERENCE_TIME stop,
                    double rate)
{
    dshowdebug("libAVPin_NewSegment(%p)\n", this);
    /* I don't care. */
    return S_OK;
}

static int
libAVPin_Setup(libAVPin *this, libAVFilter *filter)
{
    IPinVtbl *vtbl = this->vtbl;
    IMemInputPinVtbl *imemvtbl;

    if (!filter)
        return 0;

    imemvtbl = av_malloc(sizeof(IMemInputPinVtbl));
    if (!imemvtbl)
        return 0;

    SETVTBL(imemvtbl, libAVMemInputPin, QueryInterface);
    SETVTBL(imemvtbl, libAVMemInputPin, AddRef);
    SETVTBL(imemvtbl, libAVMemInputPin, Release);
    SETVTBL(imemvtbl, libAVMemInputPin, GetAllocator);
    SETVTBL(imemvtbl, libAVMemInputPin, NotifyAllocator);
    SETVTBL(imemvtbl, libAVMemInputPin, GetAllocatorRequirements);
    SETVTBL(imemvtbl, libAVMemInputPin, Receive);
    SETVTBL(imemvtbl, libAVMemInputPin, ReceiveMultiple);
    SETVTBL(imemvtbl, libAVMemInputPin, ReceiveCanBlock);

    this->imemvtbl = imemvtbl;

    SETVTBL(vtbl, libAVPin, QueryInterface);
    SETVTBL(vtbl, libAVPin, AddRef);
    SETVTBL(vtbl, libAVPin, Release);
    SETVTBL(vtbl, libAVPin, Connect);
    SETVTBL(vtbl, libAVPin, ReceiveConnection);
    SETVTBL(vtbl, libAVPin, Disconnect);
    SETVTBL(vtbl, libAVPin, ConnectedTo);
    SETVTBL(vtbl, libAVPin, ConnectionMediaType);
    SETVTBL(vtbl, libAVPin, QueryPinInfo);
    SETVTBL(vtbl, libAVPin, QueryDirection);
    SETVTBL(vtbl, libAVPin, QueryId);
    SETVTBL(vtbl, libAVPin, QueryAccept);
    SETVTBL(vtbl, libAVPin, EnumMediaTypes);
    SETVTBL(vtbl, libAVPin, QueryInternalConnections);
    SETVTBL(vtbl, libAVPin, EndOfStream);
    SETVTBL(vtbl, libAVPin, BeginFlush);
    SETVTBL(vtbl, libAVPin, EndFlush);
    SETVTBL(vtbl, libAVPin, NewSegment);

    this->filter = filter;

    return 1;
}
DECLARE_CREATE(libAVPin, libAVPin_Setup(this, filter), libAVFilter *filter)
DECLARE_DESTROY(libAVPin, nothing)

/*****************************************************************************
 * libAVMemInputPin
 ****************************************************************************/
long WINAPI
libAVMemInputPin_QueryInterface(libAVMemInputPin *this, const GUID *riid,
                                void **ppvObject)
{
    libAVPin *pin = (libAVPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("libAVMemInputPin_QueryInterface(%p)\n", this);
    return libAVPin_QueryInterface(pin, riid, ppvObject);
}
unsigned long WINAPI
libAVMemInputPin_AddRef(libAVMemInputPin *this)
{
    libAVPin *pin = (libAVPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("libAVMemInputPin_AddRef(%p)\n", this);
    return libAVPin_AddRef(pin);
}
unsigned long WINAPI
libAVMemInputPin_Release(libAVMemInputPin *this)
{
    libAVPin *pin = (libAVPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("libAVMemInputPin_Release(%p)\n", this);
    return libAVPin_Release(pin);
}
long WINAPI
libAVMemInputPin_GetAllocator(libAVMemInputPin *this, IMemAllocator **alloc)
{
    dshowdebug("libAVMemInputPin_GetAllocator(%p)\n", this);
    return VFW_E_NO_ALLOCATOR;
}
long WINAPI
libAVMemInputPin_NotifyAllocator(libAVMemInputPin *this, IMemAllocator *alloc,
                                 WINBOOL rdwr)
{
    dshowdebug("libAVMemInputPin_NotifyAllocator(%p)\n", this);
    return S_OK;
}
long WINAPI
libAVMemInputPin_GetAllocatorRequirements(libAVMemInputPin *this,
                                          ALLOCATOR_PROPERTIES *props)
{
    dshowdebug("libAVMemInputPin_GetAllocatorRequirements(%p)\n", this);
    return E_NOTIMPL;
}
long WINAPI
libAVMemInputPin_Receive(libAVMemInputPin *this, IMediaSample *sample)
{
    libAVPin *pin = (libAVPin *) ((uint8_t *) this - imemoffset);
    enum dshowDeviceType devtype = pin->filter->type;
    void *priv_data;
    uint8_t *buf;
    int buf_size;
    int index;
    int64_t curtime;

    dshowdebug("libAVMemInputPin_Receive(%p)\n", this);

    if (!sample)
        return E_POINTER;

    if (devtype == VideoDevice) {
        /* PTS from video devices is unreliable. */
        IReferenceClock *clock = pin->filter->clock;
        IReferenceClock_GetTime(clock, &curtime);
    } else {
        int64_t dummy;
        IMediaSample_GetTime(sample, &curtime, &dummy);
        curtime += pin->filter->start_time;
    }

    buf_size = IMediaSample_GetActualDataLength(sample);
    IMediaSample_GetPointer(sample, &buf);
    priv_data = pin->filter->priv_data;
    index = pin->filter->stream_index;

    pin->filter->callback(priv_data, index, buf, buf_size, curtime);

    return S_OK;
}
long WINAPI
libAVMemInputPin_ReceiveMultiple(libAVMemInputPin *this,
                                 IMediaSample **samples, long n, long *nproc)
{
    int i;
    dshowdebug("libAVMemInputPin_ReceiveMultiple(%p)\n", this);

    for (i = 0; i < n; i++)
        libAVMemInputPin_Receive(this, samples[i]);

    *nproc = n;
    return S_OK;
}
long WINAPI
libAVMemInputPin_ReceiveCanBlock(libAVMemInputPin *this)
{
    dshowdebug("libAVMemInputPin_ReceiveCanBlock(%p)\n", this);
    /* I swear I will not block. */
    return S_FALSE;
}

void
libAVMemInputPin_Destroy(libAVMemInputPin *this)
{
    libAVPin *pin = (libAVPin *) ((uint8_t *) this - imemoffset);
    dshowdebug("libAVMemInputPin_Destroy(%p)\n", this);
    return libAVPin_Destroy(pin);
}
