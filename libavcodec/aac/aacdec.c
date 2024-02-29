/*
 * Common parts of the AAC decoders
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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

#include "libavcodec/aacsbr.h"
#include "libavcodec/aacdec.h"
#include "libavcodec/avcodec.h"
#include "libavutil/attributes.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/tx.h"

av_cold int ff_aac_decode_close(AVCodecContext *avctx)
{
    AACDecContext *ac = avctx->priv_data;
    int is_fixed = ac->is_fixed;
    void (*sbr_close)(ChannelElement *che) = is_fixed ? RENAME_FIXED(ff_aac_sbr_ctx_close)
                                                      : ff_aac_sbr_ctx_close;

    for (int type = 0; type < FF_ARRAY_ELEMS(ac->che); type++) {
        for (int i = 0; i < MAX_ELEM_ID; i++) {
            if (ac->che[type][i]) {
                sbr_close(ac->che[type][i]);
                av_freep(&ac->che[type][i]);
            }
        }
    }

    av_tx_uninit(&ac->mdct120);
    av_tx_uninit(&ac->mdct128);
    av_tx_uninit(&ac->mdct480);
    av_tx_uninit(&ac->mdct512);
    av_tx_uninit(&ac->mdct960);
    av_tx_uninit(&ac->mdct1024);
    av_tx_uninit(&ac->mdct_ltp);

    // Compiler will optimize this branch away.
    if (is_fixed)
        av_freep(&ac->RENAME_FIXED(fdsp));
    else
        av_freep(&ac->fdsp);

    return 0;
}
