/*
 * Intel MediaSDK QSV public API functions
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

#include "config.h"

#include <stddef.h>

#include "libavutil/mem.h"

#if CONFIG_QSV
#include "qsv.h"

AVQSVContext *av_qsv_alloc_context(void)
{
    return av_mallocz(sizeof(AVQSVContext));
}
#else

struct AVQSVContext *av_qsv_alloc_context(void);

struct AVQSVContext *av_qsv_alloc_context(void)
{
    return NULL;
}
#endif
