/*
 * XVideo Motion Compensation internal functions
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

#ifndef AVCODEC_XVMC_INTERNAL_H
#define AVCODEC_XVMC_INTERNAL_H

#include "avcodec.h"
#include "mpegvideo.h"
#include "version.h"

#if FF_API_XVMC

void ff_xvmc_init_block(MpegEncContext *s);
void ff_xvmc_pack_pblocks(MpegEncContext *s, int cbp);
int  ff_xvmc_field_start(MpegEncContext*s, AVCodecContext *avctx);
void ff_xvmc_field_end(MpegEncContext *s);
void ff_xvmc_decode_mb(MpegEncContext *s);

#endif /* FF_API_XVMC */

#endif /* AVCODEC_XVMC_INTERNAL_H */
