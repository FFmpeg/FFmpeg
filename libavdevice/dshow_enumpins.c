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

DECLARE_QUERYINTERFACE(enumpins, DShowEnumPins,
    { {&IID_IUnknown,0}, {&IID_IEnumPins,0} })
DECLARE_ADDREF(enumpins, DShowEnumPins)
DECLARE_RELEASE(enumpins, DShowEnumPins)

long ff_dshow_enumpins_Next(DShowEnumPins *this, unsigned long n, IPin **pins,
                   unsigned long *fetched)
{
    int count = 0;
    dshowdebug("ff_dshow_enumpins_Next(%p)\n", this);
    if (!pins)
        return E_POINTER;
    if (!this->pos && n == 1) {
        ff_dshow_pin_AddRef(this->pin);
        *pins = (IPin *) this->pin;
        count = 1;
        this->pos = 1;
    }
    if (fetched)
        *fetched = count;
    if (!count)
        return S_FALSE;
    return S_OK;
}
long ff_dshow_enumpins_Skip(DShowEnumPins *this, unsigned long n)
{
    dshowdebug("ff_dshow_enumpins_Skip(%p)\n", this);
    if (n) /* Any skip will always fall outside of the only valid pin. */
        return S_FALSE;
    return S_OK;
}
long ff_dshow_enumpins_Reset(DShowEnumPins *this)
{
    dshowdebug("ff_dshow_enumpins_Reset(%p)\n", this);
    this->pos = 0;
    return S_OK;
}
long ff_dshow_enumpins_Clone(DShowEnumPins *this, DShowEnumPins **pins)
{
    DShowEnumPins *new;
    dshowdebug("ff_dshow_enumpins_Clone(%p)\n", this);
    if (!pins)
        return E_POINTER;
    new = ff_dshow_enumpins_Create(this->pin, this->filter);
    if (!new)
        return E_OUTOFMEMORY;
    new->pos = this->pos;
    *pins = new;
    return S_OK;
}

static int ff_dshow_enumpins_Setup(DShowEnumPins *this, DShowPin *pin, DShowFilter *filter)
{
    IEnumPinsVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, enumpins, QueryInterface);
    SETVTBL(vtbl, enumpins, AddRef);
    SETVTBL(vtbl, enumpins, Release);
    SETVTBL(vtbl, enumpins, Next);
    SETVTBL(vtbl, enumpins, Skip);
    SETVTBL(vtbl, enumpins, Reset);
    SETVTBL(vtbl, enumpins, Clone);

    this->pin = pin;
    this->filter = filter;
    ff_dshow_filter_AddRef(this->filter);

    return 1;
}
static int ff_dshow_enumpins_Cleanup(DShowEnumPins *this)
{
    ff_dshow_filter_Release(this->filter);
    return 1;
}
DECLARE_CREATE(enumpins, DShowEnumPins, ff_dshow_enumpins_Setup(this, pin, filter),
               DShowPin *pin, DShowFilter *filter)
DECLARE_DESTROY(enumpins, DShowEnumPins, ff_dshow_enumpins_Cleanup)
