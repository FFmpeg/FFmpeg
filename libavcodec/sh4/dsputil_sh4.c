/*
 *  sh4 dsputil
 *
 * Copyright (c) 2003 BERO <bero@geocities.co.jp>
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

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "sh4.h"

static void memzero_align8(void *dst,size_t size)
{
        int fpscr;
        fp_single_enter(fpscr);
        dst = (char *)dst + size;
        size /= 32;
        __asm__ volatile (
        " fldi0 fr0\n"
        " fldi0 fr1\n"
        " fschg\n"  // double
        "1: \n" \
        " dt %1\n"
        " fmov  dr0,@-%0\n"
        " fmov  dr0,@-%0\n"
        " fmov  dr0,@-%0\n"
        " bf.s 1b\n"
        " fmov  dr0,@-%0\n"
        " fschg" //back to single
        : "+r"(dst),"+r"(size) :: "memory" );
        fp_single_leave(fpscr);
}

static void clear_blocks_sh4(DCTELEM *blocks)
{
        memzero_align8(blocks,sizeof(DCTELEM)*6*64);
}

void idct_sh4(DCTELEM *block);
static void idct_put(uint8_t *dest, int line_size, DCTELEM *block)
{
        int i;
        uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
        idct_sh4(block);
        for(i=0;i<8;i++) {
                dest[0] = cm[block[0]];
                dest[1] = cm[block[1]];
                dest[2] = cm[block[2]];
                dest[3] = cm[block[3]];
                dest[4] = cm[block[4]];
                dest[5] = cm[block[5]];
                dest[6] = cm[block[6]];
                dest[7] = cm[block[7]];
                dest+=line_size;
                block+=8;
        }
}
static void idct_add(uint8_t *dest, int line_size, DCTELEM *block)
{
        int i;
        uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
        idct_sh4(block);
        for(i=0;i<8;i++) {
                dest[0] = cm[dest[0]+block[0]];
                dest[1] = cm[dest[1]+block[1]];
                dest[2] = cm[dest[2]+block[2]];
                dest[3] = cm[dest[3]+block[3]];
                dest[4] = cm[dest[4]+block[4]];
                dest[5] = cm[dest[5]+block[5]];
                dest[6] = cm[dest[6]+block[6]];
                dest[7] = cm[dest[7]+block[7]];
                dest+=line_size;
                block+=8;
        }
}

void dsputil_init_align(DSPContext* c, AVCodecContext *avctx);

void dsputil_init_sh4(DSPContext* c, AVCodecContext *avctx)
{
        const int idct_algo= avctx->idct_algo;
        dsputil_init_align(c,avctx);

        c->clear_blocks = clear_blocks_sh4;
        if(idct_algo==FF_IDCT_AUTO || idct_algo==FF_IDCT_SH4){
                c->idct_put = idct_put;
                c->idct_add = idct_add;
               c->idct     = idct_sh4;
                c->idct_permutation_type= FF_NO_IDCT_PERM;
        }
}
