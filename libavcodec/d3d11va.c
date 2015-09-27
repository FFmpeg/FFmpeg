/*
 * Direct3D11 HW acceleration
 *
 * copyright (c) 2015 Steve Lhomme
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stddef.h>

#include "config.h"

#if CONFIG_D3D11VA
#include "libavutil/error.h"
#include "libavutil/mem.h"

#include "d3d11va.h"

AVD3D11VAContext *av_d3d11va_alloc_context(void)
{
    AVD3D11VAContext* res = av_mallocz(sizeof(AVD3D11VAContext));
    if (!res)
        return NULL;
    res->context_mutex = INVALID_HANDLE_VALUE;
    return res;
}
#else
struct AVD3D11VAContext *av_d3d11va_alloc_context(void);

struct AVD3D11VAContext *av_d3d11va_alloc_context(void)
{
    return NULL;
}
#endif /* CONFIG_D3D11VA */
