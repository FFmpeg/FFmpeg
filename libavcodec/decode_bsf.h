/*
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

#ifndef AVCODEC_DECODE_BSF_H
#define AVCODEC_DECODE_BSF_H

#include <stdint.h>

#include "avcodec.h"
#include "bsf.h"
#include "internal.h"

/**
 * Helper function for decoders that may use a BSF that changes extradata.
 * This function will get the extradata from the BSF.
 */
static inline void ff_decode_get_extradata(const AVCodecContext *avctx,
                                           const uint8_t **extradata,
                                           int *extradata_size)
{
    // Given that we unconditionally insert a null BSF when no BSF is
    // explicitly requested, we can just use the BSF's par_out here.
    *extradata      = avctx->internal->bsf->par_out->extradata;
    *extradata_size = avctx->internal->bsf->par_out->extradata_size;
}

#endif /* AVCODEC_DECODE_BSF_H */
