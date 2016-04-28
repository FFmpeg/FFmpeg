/*
 * Copyright (C) 2016 foo86
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

#ifndef AVCODEC_DCADEC_H
#define AVCODEC_DCADEC_H

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"

#include "avcodec.h"
#include "get_bits.h"
#include "dca.h"
#include "dcadsp.h"
#include "dca_core.h"
#include "dca_exss.h"
#include "dca_xll.h"

#define DCA_BUFFER_PADDING_SIZE     1024

#define DCA_PACKET_CORE         0x01
#define DCA_PACKET_EXSS         0x02
#define DCA_PACKET_XLL          0x04
#define DCA_PACKET_RECOVERY     0x08

typedef struct DCAContext {
    const AVClass   *class;       ///< class for AVOptions
    AVCodecContext  *avctx;

    DCACoreDecoder core;  ///< Core decoder context
    DCAExssParser  exss;  ///< EXSS parser context
    DCAXllDecoder  xll;   ///< XLL decoder context

    DCADSPContext   dcadsp;

    uint8_t         *buffer;    ///< Packet buffer
    unsigned int    buffer_size;

    int     packet; ///< Packet flags

    int     core_residual_valid;    ///< Core valid for residual decoding

    int     request_channel_layout; ///< Converted from avctx.request_channel_layout
    int     core_only;              ///< Core only decoding flag
} DCAContext;

int ff_dca_set_channel_layout(AVCodecContext *avctx, int *ch_remap, int dca_mask);

int ff_dca_check_crc(GetBitContext *s, int p1, int p2);

void ff_dca_downmix_to_stereo_fixed(DCADSPContext *dcadsp, int32_t **samples,
                                    int *coeff_l, int nsamples, int ch_mask);
void ff_dca_downmix_to_stereo_float(AVFloatDSPContext *fdsp, float **samples,
                                    int *coeff_l, int nsamples, int ch_mask);

static inline int ff_dca_seek_bits(GetBitContext *s, int p)
{
    if (p < s->index || p > s->size_in_bits)
        return -1;
    s->index = p;
    return 0;
}

#endif
