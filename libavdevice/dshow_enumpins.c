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

DECLARE_QUERYINTERFACE(libAVEnumPins,
    { {&IID_IUnknown,0}, {&IID_IEnumPins,0} })
DECLARE_ADDREF(libAVEnumPins)
DECLARE_RELEASE(libAVEnumPins)

long WINAPI
libAVEnumPins_Next(libAVEnumPins *this, unsigned long n, IPin **pins,
                   unsigned long *fetched)
{
    int count = 0;
    dshowdebug("libAVEnumPins_Next(%p)\n", this);
    if (!pins)
        return E_POINTER;
    if (!this->pos && n == 1) {
        libAVPin_AddRef(this->pin);
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
long WINAPI
libAVEnumPins_Skip(libAVEnumPins *this, unsigned long n)
{
    dshowdebug("libAVEnumPins_Skip(%p)\n", this);
    if (n) /* Any skip will always fall outside of the only valid pin. */
        return S_FALSE;
    return S_OK;
}
long WINAPI
libAVEnumPins_Reset(libAVEnumPins *this)
{
    dshowdebug("libAVEnumPins_Reset(%p)\n", this);
    this->pos = 0;
    return S_OK;
}
long WINAPI
libAVEnumPins_Clone(libAVEnumPins *this, libAVEnumPins **pins)
{
    libAVEnumPins *new;
    dshowdebug("libAVEnumPins_Clone(%p)\n", this);
    if (!pins)
        return E_POINTER;
    new = libAVEnumPins_Create(this->pin, this->filter);
    if (!new)
        return E_OUTOFMEMORY;
    new->pos = this->pos;
    *pins = new;
    return S_OK;
}

static int
libAVEnumPins_Setup(libAVEnumPins *this, libAVPin *pin, libAVFilter *filter)
{
    IEnumPinsVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, libAVEnumPins, QueryInterface);
    SETVTBL(vtbl, libAVEnumPins, AddRef);
    SETVTBL(vtbl, libAVEnumPins, Release);
    SETVTBL(vtbl, libAVEnumPins, Next);
    SETVTBL(vtbl, libAVEnumPins, Skip);
    SETVTBL(vtbl, libAVEnumPins, Reset);
    SETVTBL(vtbl, libAVEnumPins, Clone);

    this->pin = pin;
    this->filter = filter;
    libAVFilter_AddRef(this->filter);

    return 1;
}
DECLARE_CREATE(libAVEnumPins, libAVEnumPins_Setup(this, pin, filter),
               libAVPin *pin, libAVFilter *filter)
DECLARE_DESTROY(libAVEnumPins, nothing)
