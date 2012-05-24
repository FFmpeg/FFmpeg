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

DECLARE_QUERYINTERFACE(libAVEnumMediaTypes,
    { {&IID_IUnknown,0}, {&IID_IEnumPins,0} })
DECLARE_ADDREF(libAVEnumMediaTypes)
DECLARE_RELEASE(libAVEnumMediaTypes)

long WINAPI
libAVEnumMediaTypes_Next(libAVEnumMediaTypes *this, unsigned long n,
                         AM_MEDIA_TYPE **types, unsigned long *fetched)
{
    int count = 0;
    dshowdebug("libAVEnumMediaTypes_Next(%p)\n", this);
    if (!types)
        return E_POINTER;
    if (!this->pos && n == 1) {
        if (!IsEqualGUID(&this->type.majortype, &GUID_NULL)) {
            AM_MEDIA_TYPE *type = av_malloc(sizeof(AM_MEDIA_TYPE));
            ff_copy_dshow_media_type(type, &this->type);
            *types = type;
            count = 1;
        }
        this->pos = 1;
    }
    if (fetched)
        *fetched = count;
    if (!count)
        return S_FALSE;
    return S_OK;
}
long WINAPI
libAVEnumMediaTypes_Skip(libAVEnumMediaTypes *this, unsigned long n)
{
    dshowdebug("libAVEnumMediaTypes_Skip(%p)\n", this);
    if (n) /* Any skip will always fall outside of the only valid type. */
        return S_FALSE;
    return S_OK;
}
long WINAPI
libAVEnumMediaTypes_Reset(libAVEnumMediaTypes *this)
{
    dshowdebug("libAVEnumMediaTypes_Reset(%p)\n", this);
    this->pos = 0;
    return S_OK;
}
long WINAPI
libAVEnumMediaTypes_Clone(libAVEnumMediaTypes *this, libAVEnumMediaTypes **enums)
{
    libAVEnumMediaTypes *new;
    dshowdebug("libAVEnumMediaTypes_Clone(%p)\n", this);
    if (!enums)
        return E_POINTER;
    new = libAVEnumMediaTypes_Create(&this->type);
    if (!new)
        return E_OUTOFMEMORY;
    new->pos = this->pos;
    *enums = new;
    return S_OK;
}

static int
libAVEnumMediaTypes_Setup(libAVEnumMediaTypes *this, const AM_MEDIA_TYPE *type)
{
    IEnumPinsVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, libAVEnumMediaTypes, QueryInterface);
    SETVTBL(vtbl, libAVEnumMediaTypes, AddRef);
    SETVTBL(vtbl, libAVEnumMediaTypes, Release);
    SETVTBL(vtbl, libAVEnumMediaTypes, Next);
    SETVTBL(vtbl, libAVEnumMediaTypes, Skip);
    SETVTBL(vtbl, libAVEnumMediaTypes, Reset);
    SETVTBL(vtbl, libAVEnumMediaTypes, Clone);

    if (!type) {
        this->type.majortype = GUID_NULL;
    } else {
        ff_copy_dshow_media_type(&this->type, type);
    }

    return 1;
}
DECLARE_CREATE(libAVEnumMediaTypes, libAVEnumMediaTypes_Setup(this, type), const AM_MEDIA_TYPE *type)
DECLARE_DESTROY(libAVEnumMediaTypes, nothing)
