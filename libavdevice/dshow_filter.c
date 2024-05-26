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

DECLARE_QUERYINTERFACE(filter, DShowFilter,
    { {&IID_IUnknown,0}, {&IID_IBaseFilter,0} })
DECLARE_ADDREF(filter, DShowFilter)
DECLARE_RELEASE(filter, DShowFilter)

long ff_dshow_filter_GetClassID(DShowFilter *this, CLSID *id)
{
    dshowdebug("ff_dshow_filter_GetClassID(%p)\n", this);
    /* I'm not creating a ClassID just for this. */
    return E_FAIL;
}
long ff_dshow_filter_Stop(DShowFilter *this)
{
    dshowdebug("ff_dshow_filter_Stop(%p)\n", this);
    this->state = State_Stopped;
    return S_OK;
}
long ff_dshow_filter_Pause(DShowFilter *this)
{
    dshowdebug("ff_dshow_filter_Pause(%p)\n", this);
    this->state = State_Paused;
    return S_OK;
}
long ff_dshow_filter_Run(DShowFilter *this, REFERENCE_TIME start)
{
    dshowdebug("ff_dshow_filter_Run(%p) %"PRId64"\n", this, start);
    this->state = State_Running;
    this->start_time = start;
    return S_OK;
}
long ff_dshow_filter_GetState(DShowFilter *this, DWORD ms, FILTER_STATE *state)
{
    dshowdebug("ff_dshow_filter_GetState(%p)\n", this);
    if (!state)
        return E_POINTER;
    *state = this->state;
    return S_OK;
}
long ff_dshow_filter_SetSyncSource(DShowFilter *this, IReferenceClock *clock)
{
    dshowdebug("ff_dshow_filter_SetSyncSource(%p)\n", this);

    if (this->clock != clock) {
        if (this->clock)
            IReferenceClock_Release(this->clock);
        this->clock = clock;
        if (clock)
            IReferenceClock_AddRef(clock);
    }

    return S_OK;
}
long ff_dshow_filter_GetSyncSource(DShowFilter *this, IReferenceClock **clock)
{
    dshowdebug("ff_dshow_filter_GetSyncSource(%p)\n", this);

    if (!clock)
        return E_POINTER;
    if (this->clock)
        IReferenceClock_AddRef(this->clock);
    *clock = this->clock;

    return S_OK;
}
long ff_dshow_filter_EnumPins(DShowFilter *this, IEnumPins **enumpin)
{
    DShowEnumPins *new;
    dshowdebug("ff_dshow_filter_EnumPins(%p)\n", this);

    if (!enumpin)
        return E_POINTER;
    new = ff_dshow_enumpins_Create(this->pin, this);
    if (!new)
        return E_OUTOFMEMORY;

    *enumpin = (IEnumPins *) new;
    return S_OK;
}
long ff_dshow_filter_FindPin(DShowFilter *this, const wchar_t *id, IPin **pin)
{
    DShowPin *found = NULL;
    dshowdebug("ff_dshow_filter_FindPin(%p)\n", this);

    if (!id || !pin)
        return E_POINTER;
    if (!wcscmp(id, L"In")) {
        found = this->pin;
        ff_dshow_pin_AddRef(found);
    }
    *pin = (IPin *) found;
    if (!found)
        return VFW_E_NOT_FOUND;

    return S_OK;
}
long ff_dshow_filter_QueryFilterInfo(DShowFilter *this, FILTER_INFO *info)
{
    dshowdebug("ff_dshow_filter_QueryFilterInfo(%p)\n", this);

    if (!info)
        return E_POINTER;
    if (this->info.pGraph)
        IFilterGraph_AddRef(this->info.pGraph);
    *info = this->info;

    return S_OK;
}
long ff_dshow_filter_JoinFilterGraph(DShowFilter *this, IFilterGraph *graph,
                            const wchar_t *name)
{
    dshowdebug("ff_dshow_filter_JoinFilterGraph(%p)\n", this);

    this->info.pGraph = graph;
    if (name)
        wcscpy_s(this->info.achName, sizeof(this->info.achName) / sizeof(wchar_t), name);

    return S_OK;
}
long ff_dshow_filter_QueryVendorInfo(DShowFilter *this, wchar_t **info)
{
    dshowdebug("ff_dshow_filter_QueryVendorInfo(%p)\n", this);

    if (!info)
        return E_POINTER;
    return E_NOTIMPL; /* don't have to do anything here */
}

static int
ff_dshow_filter_Setup(DShowFilter *this, void *priv_data, void *callback,
                  enum dshowDeviceType type)
{
    IBaseFilterVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, filter, QueryInterface);
    SETVTBL(vtbl, filter, AddRef);
    SETVTBL(vtbl, filter, Release);
    SETVTBL(vtbl, filter, GetClassID);
    SETVTBL(vtbl, filter, Stop);
    SETVTBL(vtbl, filter, Pause);
    SETVTBL(vtbl, filter, Run);
    SETVTBL(vtbl, filter, GetState);
    SETVTBL(vtbl, filter, SetSyncSource);
    SETVTBL(vtbl, filter, GetSyncSource);
    SETVTBL(vtbl, filter, EnumPins);
    SETVTBL(vtbl, filter, FindPin);
    SETVTBL(vtbl, filter, QueryFilterInfo);
    SETVTBL(vtbl, filter, JoinFilterGraph);
    SETVTBL(vtbl, filter, QueryVendorInfo);

    this->pin = ff_dshow_pin_Create(this);

    this->priv_data = priv_data;
    this->callback  = callback;
    this->type      = type;

    return 1;
}
static int ff_dshow_filter_Cleanup(DShowFilter *this)
{
    ff_dshow_pin_Release(this->pin);
    return 1;
}
DECLARE_CREATE(filter, DShowFilter, ff_dshow_filter_Setup(this, priv_data, callback, type),
               void *priv_data, void *callback, enum dshowDeviceType type)
DECLARE_DESTROY(filter, DShowFilter, ff_dshow_filter_Cleanup)
