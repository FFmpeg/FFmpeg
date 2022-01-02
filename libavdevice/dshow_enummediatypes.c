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

DECLARE_QUERYINTERFACE(enummediatypes, DShowEnumMediaTypes,
    { {&IID_IUnknown,0}, {&IID_IEnumMediaTypes,0} })
DECLARE_ADDREF(enummediatypes, DShowEnumMediaTypes)
DECLARE_RELEASE(enummediatypes, DShowEnumMediaTypes)

long WINAPI ff_dshow_enummediatypes_Next(DShowEnumMediaTypes *this, unsigned long n,
                         AM_MEDIA_TYPE **types, unsigned long *fetched)
{
    int count = 0;
    dshowdebug("ff_dshow_enummediatypes_Next(%p)\n", this);
    if (!types)
        return E_POINTER;
    if (!this->pos && n == 1) {
        if (!IsEqualGUID(&this->type.majortype, &GUID_NULL)) {
            AM_MEDIA_TYPE *type = av_malloc(sizeof(AM_MEDIA_TYPE));
            if (!type)
                return E_OUTOFMEMORY;
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
long WINAPI ff_dshow_enummediatypes_Skip(DShowEnumMediaTypes *this, unsigned long n)
{
    dshowdebug("ff_dshow_enummediatypes_Skip(%p)\n", this);
    if (n) /* Any skip will always fall outside of the only valid type. */
        return S_FALSE;
    return S_OK;
}
long WINAPI ff_dshow_enummediatypes_Reset(DShowEnumMediaTypes *this)
{
    dshowdebug("ff_dshow_enummediatypes_Reset(%p)\n", this);
    this->pos = 0;
    return S_OK;
}
long WINAPI ff_dshow_enummediatypes_Clone(DShowEnumMediaTypes *this, DShowEnumMediaTypes **enums)
{
    DShowEnumMediaTypes *new;
    dshowdebug("ff_dshow_enummediatypes_Clone(%p)\n", this);
    if (!enums)
        return E_POINTER;
    new = ff_dshow_enummediatypes_Create(&this->type);
    if (!new)
        return E_OUTOFMEMORY;
    new->pos = this->pos;
    *enums = new;
    return S_OK;
}

static int ff_dshow_enummediatypes_Setup(DShowEnumMediaTypes *this, const AM_MEDIA_TYPE *type)
{
    IEnumMediaTypesVtbl *vtbl = this->vtbl;
    SETVTBL(vtbl, enummediatypes, QueryInterface);
    SETVTBL(vtbl, enummediatypes, AddRef);
    SETVTBL(vtbl, enummediatypes, Release);
    SETVTBL(vtbl, enummediatypes, Next);
    SETVTBL(vtbl, enummediatypes, Skip);
    SETVTBL(vtbl, enummediatypes, Reset);
    SETVTBL(vtbl, enummediatypes, Clone);

    if (!type) {
        this->type.majortype = GUID_NULL;
    } else {
        ff_copy_dshow_media_type(&this->type, type);
    }

    return 1;
}
DECLARE_CREATE(enummediatypes, DShowEnumMediaTypes, ff_dshow_enummediatypes_Setup(this, type), const AM_MEDIA_TYPE *type)
DECLARE_DESTROY(enummediatypes, DShowEnumMediaTypes, nothing)
