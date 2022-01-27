/*
 * Copyright (c) 2002 The FFmpeg Project
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

#include "avcodec.h"
#include "idctdsp.h"
#include "mpegvideo.h"
#include "msmpeg4data.h"
#include "wmv2.h"


av_cold void ff_wmv2_common_init(MpegEncContext *s)
{
    WMV2Context *const w = s->private_ctx;

    ff_blockdsp_init(&s->bdsp, s->avctx);
    ff_wmv2dsp_init(&w->wdsp);
    s->idsp.perm_type = w->wdsp.idct_perm;
    ff_init_scantable_permutation(s->idsp.idct_permutation,
                                  w->wdsp.idct_perm);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable,
                      ff_wmv1_scantable[1]);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_h_scantable,
                      ff_wmv1_scantable[2]);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_v_scantable,
                      ff_wmv1_scantable[3]);
    ff_init_scantable(s->idsp.idct_permutation, &s->inter_scantable,
                      ff_wmv1_scantable[0]);
    s->idsp.idct_put = w->wdsp.idct_put;
    s->idsp.idct_add = w->wdsp.idct_add;
    s->idsp.idct     = NULL;
}

void ff_mspel_motion(MpegEncContext *s, uint8_t *dest_y,
                     uint8_t *dest_cb, uint8_t *dest_cr,
                     uint8_t **ref_picture, op_pixels_func (*pix_op)[4],
                     int motion_x, int motion_y, int h)
{
    WMV2Context *const w = s->private_ctx;
    uint8_t *ptr;
    int dxy, mx, my, src_x, src_y, v_edge_pos;
    ptrdiff_t offset, linesize, uvlinesize;
    int emu = 0;

    dxy   = ((motion_y & 1) << 1) | (motion_x & 1);
    dxy   = 2 * dxy + w->hshift;
    src_x = s->mb_x * 16 + (motion_x >> 1);
    src_y = s->mb_y * 16 + (motion_y >> 1);

    /* WARNING: do no forget half pels */
    v_edge_pos = s->v_edge_pos;
    src_x      = av_clip(src_x, -16, s->width);
    src_y      = av_clip(src_y, -16, s->height);

    if (src_x <= -16 || src_x >= s->width)
        dxy &= ~3;
    if (src_y <= -16 || src_y >= s->height)
        dxy &= ~4;

    linesize   = s->linesize;
    uvlinesize = s->uvlinesize;
    ptr        = ref_picture[0] + (src_y * linesize) + src_x;

    if (src_x < 1 || src_y < 1 || src_x + 17 >= s->h_edge_pos ||
        src_y + h + 1 >= v_edge_pos) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr - 1 - s->linesize,
                                 s->linesize, s->linesize, 19, 19,
                                 src_x - 1, src_y - 1,
                                 s->h_edge_pos, s->v_edge_pos);
        ptr = s->sc.edge_emu_buffer + 1 + s->linesize;
        emu = 1;
    }

    w->wdsp.put_mspel_pixels_tab[dxy](dest_y,                    ptr,                    linesize);
    w->wdsp.put_mspel_pixels_tab[dxy](dest_y     + 8,            ptr     + 8,            linesize);
    w->wdsp.put_mspel_pixels_tab[dxy](dest_y     + 8 * linesize, ptr     + 8 * linesize, linesize);
    w->wdsp.put_mspel_pixels_tab[dxy](dest_y + 8 + 8 * linesize, ptr + 8 + 8 * linesize, linesize);

    if (s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    dxy = 0;
    if ((motion_x & 3) != 0)
        dxy |= 1;
    if ((motion_y & 3) != 0)
        dxy |= 2;
    mx = motion_x >> 2;
    my = motion_y >> 2;

    src_x = s->mb_x * 8 + mx;
    src_y = s->mb_y * 8 + my;
    src_x = av_clip(src_x, -8, s->width >> 1);
    if (src_x == (s->width >> 1))
        dxy &= ~1;
    src_y = av_clip(src_y, -8, s->height >> 1);
    if (src_y == (s->height >> 1))
        dxy &= ~2;
    offset = (src_y * uvlinesize) + src_x;
    ptr    = ref_picture[1] + offset;
    if (emu) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 s->uvlinesize, s->uvlinesize,
                                 9, 9,
                                 src_x, src_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ptr = s->sc.edge_emu_buffer;
    }
    pix_op[1][dxy](dest_cb, ptr, uvlinesize, h >> 1);

    ptr = ref_picture[2] + offset;
    if (emu) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 s->uvlinesize, s->uvlinesize,
                                 9, 9,
                                 src_x, src_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ptr = s->sc.edge_emu_buffer;
    }
    pix_op[1][dxy](dest_cr, ptr, uvlinesize, h >> 1);
}
