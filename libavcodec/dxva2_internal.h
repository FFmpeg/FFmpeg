/*
 * DXVA2 HW acceleration
 *
 * copyright (c) 2010 Laurent Aimar
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

#ifndef AVCODEC_DXVA_INTERNAL_H
#define AVCODEC_DXVA_INTERNAL_H

#define _WIN32_WINNT 0x0600
#define COBJMACROS
#include "dxva2.h"
#include "avcodec.h"
#include "mpegvideo.h"

void *ff_dxva2_get_surface(const Picture *picture);

unsigned ff_dxva2_get_surface_index(const struct dxva_context *,
                                    const Picture *picture);

int ff_dxva2_commit_buffer(AVCodecContext *, struct dxva_context *,
                           DXVA2_DecodeBufferDesc *,
                           unsigned type, const void *data, unsigned size,
                           unsigned mb_count);


int ff_dxva2_common_end_frame(AVCodecContext *, MpegEncContext *,
                              const void *pp, unsigned pp_size,
                              const void *qm, unsigned qm_size,
                              int (*commit_bs_si)(AVCodecContext *,
                                                  DXVA2_DecodeBufferDesc *bs,
                                                  DXVA2_DecodeBufferDesc *slice));

#endif /* AVCODEC_DXVA_INTERNAL_H */
