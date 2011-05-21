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

DECLARE_QUERYINTERFACE(libAVFilter,
    { {&IID_IUnknown,0}, {&IID_IBaseFilter,0} })
DECLARE_ADDREF(libAVFilter)
DECLARE_RELEASE(libAVFilter)

long WINAPI
libAVFilter_GetClassID(libAVFilter *this, CLSID *id)
{
    dshowdebug("libAVFilter_GetClassID(%p)\n", this);
    /* I'm not creating a ClassID just for this. */
    return E_FAIL;
}
long WINAPI
libAVFilter_Stop(libAVFilter *this)
{
    dshowdebug("libAVFilter_Stop(%p)\n", this);
    this->state = State_Stopped;
    return S_OK;
}
long WINAPI
libAVFilter_Pause(libAVFilter *this)
{
    dshowdebug("libAVFilter_Pause(%p)\n", this);
    this->state = State_Paused;
    return S_OK;
}
long WINAPI
libAVFilter_Run(libAVFilter *this, REFERENCE_TIME start)
{
    dshowdebug("libAVFilter_Run(%p) %"PRId64"\n", this, start);
    this->state = State_Running;
    this->start_time = start;
    return S_OK;
}
long WINAPI
libAVFilter_GetState(libAVFilter *this, DWORD ms, FILTER_STATE *state)
{
    dshowdebug("libAVFilter_GetState(%p)\n", this);
    if (!state)
        return E_POINTER;
    *state = this->state;
    return S_OK;
}
long WINAPI
libAVFilter_SetSyncSource(libAVFilter *this, IReferenceClock *clock)
{
    dshowdebug("libAVFilter_SetSyncSource(%p)\n", this);

    if (this->clock != clock) {
        if (this->clock)
            IReferenceClock_Release(this->clock);
        this->clock = clock;
        if (clock)
            IReferenceClock_AddRef(clock);
    }

    return S_OK;
}
long WINAPI
libAVFilter_GetSyncSource(libAVFilter *this, IReferenceClock **clock)
{
    dshowdebug("libAVFilter_GetSyncSource(%p)\n", this);

    if (!clock)
        return E_POINTER;
    if (this->clock)
        IReferenceClock_AddRef(this->clock);
    *clock = this->clock;

    return S_OK;
}
long WINAPI
libAVFilter_EnumPins(libAVFilter *this, IEnumPins **enumpin)
{
    libAVEnumPins *new;
    dshowdebug("libAVFilter_EnumPins(%p)\n", this);

    if (!enumpin)
        return E_POINTER;
    new = libAVEnumPins_Create(this->pin, this);
    if (!new)
        return E_OUTOFMEMORY;

    *enumpin = (IEnumPins *) new;
    return S_OK;
}
long WINAPI
libAVFilter_FindPin(libAVFilter *this, const wchar_t *id, IPin **pin)
{
    libAVPin *found = NULL;
    dshowdebug("libAVFilter_FindPin(%p)\n", this);

    if (!id || !pin)
        return E_POINTER;
    if (!wcscmp(id, L"In")) {
        found = this->pin;
        libAVPin_AddRef(found);
    }
    *pin = (IPin *) found;
    if (!found)
        return VFW_E_NOT_FOUND;

    return S_OK;
}
long WINAPI
libAVFilter_QueryFilterInfo(libAVFilter *this, FILTER_INFO *info)
{
    dshowdebug("libAVFilter_QueryFilterInfo(%p)\n", this);

    if (!info)
        return E_POINTER;
    if (this->info.pGraph)
        IFilterGraph_AddRef(this->info.pGraph);
    *info = this->info;

    return S_OK;
}
long WINAPI
libAVFilter_JoinFilterGraph(libAVFilter *this, IFilterGraph *graph,
                            const wchar_t *name)
{
    dshowdebug("libAVFilter_JoinFilterGraph(%p)\n", this);

    this->info.pGraph = graph;
    if (name)
        wcscpy(this->info.achName, name);

    return S_OK;
}
long WINAPI
libAVFilter_QueryVendorInfo(libAVFilter *this, wchar_t **info)
{
    dshowdebug("libAVFilter_QueryVendorInfo(%p)\n", this);

    if (!info)
        return E_POINTER;
    *info = wcsdup(L"libAV");

    return S_OK;
}

static int
libAVFilter_Setup(libAVFilter *this, void *priv_data, void *callback,
                  enum dshowDeviceType type)
{
    IBaseFilterVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, libAVFilter, QueryInterface);
    SETVTBL(vtbl, libAVFilter, AddRef);
    SETVTBL(vtbl, libAVFilter, Release);
    SETVTBL(vtbl, libAVFilter, GetClassID);
    SETVTBL(vtbl, libAVFilter, Stop);
    SETVTBL(vtbl, libAVFilter, Pause);
    SETVTBL(vtbl, libAVFilter, Run);
    SETVTBL(vtbl, libAVFilter, GetState);
    SETVTBL(vtbl, libAVFilter, SetSyncSource);
    SETVTBL(vtbl, libAVFilter, GetSyncSource);
    SETVTBL(vtbl, libAVFilter, EnumPins);
    SETVTBL(vtbl, libAVFilter, FindPin);
    SETVTBL(vtbl, libAVFilter, QueryFilterInfo);
    SETVTBL(vtbl, libAVFilter, JoinFilterGraph);
    SETVTBL(vtbl, libAVFilter, QueryVendorInfo);

    this->pin = libAVPin_Create(this);

    this->priv_data = priv_data;
    this->callback  = callback;
    this->type      = type;

    return 1;
}
DECLARE_CREATE(libAVFilter, libAVFilter_Setup(this, priv_data, callback, type),
               void *priv_data, void *callback, enum dshowDeviceType type)
DECLARE_DESTROY(libAVFilter, nothing)
