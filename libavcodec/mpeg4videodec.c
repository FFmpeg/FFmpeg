/*
 * MPEG-4 decoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2010 Michael Niedermayer <michaelni@gmx.at>
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

#define UNCHECKED_BITSTREAM_READER 1

#include "config_components.h"

#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "codec_internal.h"
#include "error_resilience.h"
#include "hwconfig.h"
#include "idctdsp.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "mpegvideodec.h"
#include "mpegvideo_unquantize.h"
#include "mpeg4video.h"
#include "mpeg4videodata.h"
#include "mpeg4videodec.h"
#include "mpeg4videodefs.h"
#include "h263.h"
#include "h263data.h"
#include "h263dec.h"
#include "internal.h"
#include "profiles.h"
#include "qpeldsp.h"
#include "threadprogress.h"
#include "unary.h"

#if 0 //3IV1 is quite rare and it slows things down a tiny bit
#define IS_3IV1 (s->codec_tag == AV_RL32("3IV1"))
#else
#define IS_3IV1 0
#endif

/* The defines below define the number of bits that are read at once for
 * reading vlc values. Changing these may improve speed and data cache needs
 * be aware though that decreasing them may need the number of stages that is
 * passed to get_vlc* to be increased. */
#define SPRITE_TRAJ_VLC_BITS 6
#define DC_VLC_BITS 9
#define MB_TYPE_B_VLC_BITS 4
#define STUDIO_INTRA_BITS 9

static VLCElem dc_lum[512], dc_chrom[512];
static VLCElem sprite_trajectory[128];
static VLCElem mb_type_b_vlc[16];
static const VLCElem *studio_intra_tab[12];
static VLCElem studio_luma_dc[528];
static VLCElem studio_chroma_dc[528];

static const uint8_t mpeg4_block_count[4] = { 0, 6, 8, 12 };

static const int16_t mb_type_b_map[4] = {
    MB_TYPE_DIRECT2     | MB_TYPE_BIDIR_MV,
    MB_TYPE_BIDIR_MV    | MB_TYPE_16x16,
    MB_TYPE_BACKWARD_MV | MB_TYPE_16x16,
    MB_TYPE_FORWARD_MV  | MB_TYPE_16x16,
};

static inline Mpeg4DecContext *h263_to_mpeg4(H263DecContext *h)
{
    av_assert2(h->c.codec_id == AV_CODEC_ID_MPEG4 && h->c.avctx->priv_data == h);
    return (Mpeg4DecContext*)h;
}

static void gmc1_motion(MpegEncContext *s, const Mpeg4DecContext *ctx,
                        uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                        uint8_t *const *ref_picture)
{
    const uint8_t *ptr;
    int src_x, src_y, motion_x, motion_y;
    ptrdiff_t offset, linesize, uvlinesize;
    int emu = 0;

    motion_x   = ctx->sprite_offset[0][0];
    motion_y   = ctx->sprite_offset[0][1];
    src_x      = s->mb_x * 16 + (motion_x >> (ctx->sprite_warping_accuracy + 1));
    src_y      = s->mb_y * 16 + (motion_y >> (ctx->sprite_warping_accuracy + 1));
    motion_x *= 1 << (3 - ctx->sprite_warping_accuracy);
    motion_y *= 1 << (3 - ctx->sprite_warping_accuracy);
    src_x      = av_clip(src_x, -16, s->width);
    if (src_x == s->width)
        motion_x = 0;
    src_y = av_clip(src_y, -16, s->height);
    if (src_y == s->height)
        motion_y = 0;

    linesize   = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0] + src_y * linesize + src_x;

    if ((unsigned)src_x >= FFMAX(s->h_edge_pos - 17, 0) ||
        (unsigned)src_y >= FFMAX(s->v_edge_pos - 17, 0)) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 linesize, linesize,
                                 17, 17,
                                 src_x, src_y,
                                 s->h_edge_pos, s->v_edge_pos);
        ptr = s->sc.edge_emu_buffer;
    }

    if ((motion_x | motion_y) & 7) {
        ctx->mdsp.gmc1(dest_y, ptr, linesize, 16,
                       motion_x & 15, motion_y & 15, 128 - s->no_rounding);
        ctx->mdsp.gmc1(dest_y + 8, ptr + 8, linesize, 16,
                       motion_x & 15, motion_y & 15, 128 - s->no_rounding);
    } else {
        int dxy;

        dxy = ((motion_x >> 3) & 1) | ((motion_y >> 2) & 2);
        if (s->no_rounding) {
            s->hdsp.put_no_rnd_pixels_tab[0][dxy](dest_y, ptr, linesize, 16);
        } else {
            s->hdsp.put_pixels_tab[0][dxy](dest_y, ptr, linesize, 16);
        }
    }

    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    motion_x   = ctx->sprite_offset[1][0];
    motion_y   = ctx->sprite_offset[1][1];
    src_x      = s->mb_x * 8 + (motion_x >> (ctx->sprite_warping_accuracy + 1));
    src_y      = s->mb_y * 8 + (motion_y >> (ctx->sprite_warping_accuracy + 1));
    motion_x  *= 1 << (3 - ctx->sprite_warping_accuracy);
    motion_y  *= 1 << (3 - ctx->sprite_warping_accuracy);
    src_x      = av_clip(src_x, -8, s->width >> 1);
    if (src_x == s->width >> 1)
        motion_x = 0;
    src_y = av_clip(src_y, -8, s->height >> 1);
    if (src_y == s->height >> 1)
        motion_y = 0;

    offset = (src_y * uvlinesize) + src_x;
    ptr    = ref_picture[1] + offset;
    if ((unsigned)src_x >= FFMAX((s->h_edge_pos >> 1) - 9, 0) ||
        (unsigned)src_y >= FFMAX((s->v_edge_pos >> 1) - 9, 0)) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 uvlinesize, uvlinesize,
                                 9, 9,
                                 src_x, src_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ptr = s->sc.edge_emu_buffer;
        emu = 1;
    }
    ctx->mdsp.gmc1(dest_cb, ptr, uvlinesize, 8,
                   motion_x & 15, motion_y & 15, 128 - s->no_rounding);

    ptr = ref_picture[2] + offset;
    if (emu) {
        s->vdsp.emulated_edge_mc(s->sc.edge_emu_buffer, ptr,
                                 uvlinesize, uvlinesize,
                                 9, 9,
                                 src_x, src_y,
                                 s->h_edge_pos >> 1, s->v_edge_pos >> 1);
        ptr = s->sc.edge_emu_buffer;
    }
    ctx->mdsp.gmc1(dest_cr, ptr, uvlinesize, 8,
                   motion_x & 15, motion_y & 15, 128 - s->no_rounding);
}

static void gmc_motion(MpegEncContext *s, const Mpeg4DecContext *ctx,
                       uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                       uint8_t *const *ref_picture)
{
    const uint8_t *ptr;
    int linesize, uvlinesize;
    const int a = ctx->sprite_warping_accuracy;
    int ox, oy;

    linesize   = s->linesize;
    uvlinesize = s->uvlinesize;

    ptr = ref_picture[0];

    ox = ctx->sprite_offset[0][0] + ctx->sprite_delta[0][0] * s->mb_x * 16 +
         ctx->sprite_delta[0][1] * s->mb_y * 16;
    oy = ctx->sprite_offset[0][1] + ctx->sprite_delta[1][0] * s->mb_x * 16 +
         ctx->sprite_delta[1][1] * s->mb_y * 16;

    ctx->mdsp.gmc(dest_y, ptr, linesize, 16,
                  ox, oy,
                  ctx->sprite_delta[0][0], ctx->sprite_delta[0][1],
                  ctx->sprite_delta[1][0], ctx->sprite_delta[1][1],
                  a + 1, (1 << (2 * a + 1)) - s->no_rounding,
                  s->h_edge_pos, s->v_edge_pos);
    ctx->mdsp.gmc(dest_y + 8, ptr, linesize, 16,
                  ox + ctx->sprite_delta[0][0] * 8,
                  oy + ctx->sprite_delta[1][0] * 8,
                  ctx->sprite_delta[0][0], ctx->sprite_delta[0][1],
                  ctx->sprite_delta[1][0], ctx->sprite_delta[1][1],
                  a + 1, (1 << (2 * a + 1)) - s->no_rounding,
                  s->h_edge_pos, s->v_edge_pos);

    if (CONFIG_GRAY && s->avctx->flags & AV_CODEC_FLAG_GRAY)
        return;

    ox = ctx->sprite_offset[1][0] + ctx->sprite_delta[0][0] * s->mb_x * 8 +
         ctx->sprite_delta[0][1] * s->mb_y * 8;
    oy = ctx->sprite_offset[1][1] + ctx->sprite_delta[1][0] * s->mb_x * 8 +
         ctx->sprite_delta[1][1] * s->mb_y * 8;

    ptr = ref_picture[1];
    ctx->mdsp.gmc(dest_cb, ptr, uvlinesize, 8,
                  ox, oy,
                  ctx->sprite_delta[0][0], ctx->sprite_delta[0][1],
                  ctx->sprite_delta[1][0], ctx->sprite_delta[1][1],
                  a + 1, (1 << (2 * a + 1)) - s->no_rounding,
                  (s->h_edge_pos + 1) >> 1, (s->v_edge_pos + 1) >> 1);

    ptr = ref_picture[2];
    ctx->mdsp.gmc(dest_cr, ptr, uvlinesize, 8,
                  ox, oy,
                  ctx->sprite_delta[0][0], ctx->sprite_delta[0][1],
                  ctx->sprite_delta[1][0], ctx->sprite_delta[1][1],
                  a + 1, (1 << (2 * a + 1)) - s->no_rounding,
                  (s->h_edge_pos + 1) >> 1, (s->v_edge_pos + 1) >> 1);
}

void ff_mpeg4_mcsel_motion(MpegEncContext *s,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           uint8_t *const *ref_picture)
{
    const Mpeg4DecContext *const ctx = (Mpeg4DecContext*)s;

    if (ctx->real_sprite_warping_points == 1) {
        gmc1_motion(s, ctx, dest_y, dest_cb, dest_cr,
                    ref_picture);
    } else {
        gmc_motion(s, ctx, dest_y, dest_cb, dest_cr,
                    ref_picture);
    }
}

void ff_mpeg4_decode_studio(MpegEncContext *s, uint8_t *dest_y, uint8_t *dest_cb,
                            uint8_t *dest_cr, int block_size, int uvlinesize,
                            int dct_linesize, int dct_offset)
{
    Mpeg4DecContext *const ctx = (Mpeg4DecContext*)s;
    const int act_block_size = block_size * 2;

    if (ctx->dpcm_direction == 0) {
        s->idsp.idct_put(dest_y,                               dct_linesize, (int16_t*)ctx->block32[0]);
        s->idsp.idct_put(dest_y              + act_block_size, dct_linesize, (int16_t*)ctx->block32[1]);
        s->idsp.idct_put(dest_y + dct_offset,                  dct_linesize, (int16_t*)ctx->block32[2]);
        s->idsp.idct_put(dest_y + dct_offset + act_block_size, dct_linesize, (int16_t*)ctx->block32[3]);

        dct_linesize = uvlinesize << s->interlaced_dct;
        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

        s->idsp.idct_put(dest_cb,              dct_linesize, (int16_t*)ctx->block32[4]);
        s->idsp.idct_put(dest_cr,              dct_linesize, (int16_t*)ctx->block32[5]);
        s->idsp.idct_put(dest_cb + dct_offset, dct_linesize, (int16_t*)ctx->block32[6]);
        s->idsp.idct_put(dest_cr + dct_offset, dct_linesize, (int16_t*)ctx->block32[7]);
        if (!s->chroma_x_shift){ //Chroma444
            s->idsp.idct_put(dest_cb + act_block_size,              dct_linesize, (int16_t*)ctx->block32[8]);
            s->idsp.idct_put(dest_cr + act_block_size,              dct_linesize, (int16_t*)ctx->block32[9]);
            s->idsp.idct_put(dest_cb + act_block_size + dct_offset, dct_linesize, (int16_t*)ctx->block32[10]);
            s->idsp.idct_put(dest_cr + act_block_size + dct_offset, dct_linesize, (int16_t*)ctx->block32[11]);
        }
    } else if (ctx->dpcm_direction == 1) {
        uint16_t *dest_pcm[3] = {(uint16_t*)dest_y, (uint16_t*)dest_cb, (uint16_t*)dest_cr};
        int linesize[3] = {dct_linesize, uvlinesize, uvlinesize};
        for (int i = 0; i < 3; i++) {
            const uint16_t *src = ctx->dpcm_macroblock[i];
            int vsub = i ? s->chroma_y_shift : 0;
            int hsub = i ? s->chroma_x_shift : 0;
            int lowres = s->avctx->lowres;
            int step = 1 << lowres;
            for (int h = 0; h < (16 >> (vsub + lowres)); h++){
                for (int w = 0, idx = 0; w < (16 >> (hsub + lowres)); w++, idx += step)
                    dest_pcm[i][w] = src[idx];
                dest_pcm[i] += linesize[i] / 2;
                src         += (16 >> hsub) * step;
            }
        }
    } else {
        uint16_t *dest_pcm[3] = {(uint16_t*)dest_y, (uint16_t*)dest_cb, (uint16_t*)dest_cr};
        int linesize[3] = {dct_linesize, uvlinesize, uvlinesize};
        av_assert2(ctx->dpcm_direction == -1);
        for (int i = 0; i < 3; i++) {
            const uint16_t *src = ctx->dpcm_macroblock[i];
            int vsub = i ? s->chroma_y_shift : 0;
            int hsub = i ? s->chroma_x_shift : 0;
            int lowres = s->avctx->lowres;
            int step = 1 << lowres;
            dest_pcm[i] += (linesize[i] / 2) * ((16 >> vsub + lowres) - 1);
            for (int h = (16 >> (vsub + lowres)) - 1; h >= 0; h--){
                for (int w = (16 >> (hsub + lowres)) - 1, idx = 0; w >= 0; w--, idx += step)
                    dest_pcm[i][w] = src[idx];
                src += step * (16 >> hsub);
                dest_pcm[i] -= linesize[i] / 2;
            }
        }
    }
}

/**
 * Predict the ac.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir the ac prediction direction
 */
void ff_mpeg4_pred_ac(H263DecContext *const h, int16_t *block, int n, int dir)
{
    int i;
    int16_t *ac_val, *ac_val1;
    int8_t *const qscale_table = h->c.cur_pic.qscale_table;

    /* find prediction */
    ac_val  = &h->c.ac_val[0][0] + h->c.block_index[n] * 16;
    ac_val1 = ac_val;
    if (h->c.ac_pred) {
        if (dir == 0) {
            const int xy = h->c.mb_x - 1 + h->c.mb_y * h->c.mb_stride;
            /* left prediction */
            ac_val -= 16;

            if (h->c.mb_x == 0 || h->c.qscale == qscale_table[xy] ||
                n == 1 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++)
                    block[h->c.idsp.idct_permutation[i << 3]] += ac_val[i];
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++)
                    block[h->c.idsp.idct_permutation[i << 3]] += ROUNDED_DIV(ac_val[i] * qscale_table[xy], h->c.qscale);
            }
        } else {
            const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride - h->c.mb_stride;
            /* top prediction */
            ac_val -= 16 * h->c.block_wrap[n];

            if (h->c.mb_y == 0 || h->c.qscale == qscale_table[xy] ||
                n == 2 || n == 3) {
                /* same qscale */
                for (i = 1; i < 8; i++)
                    block[h->c.idsp.idct_permutation[i]] += ac_val[i + 8];
            } else {
                /* different qscale, we must rescale */
                for (i = 1; i < 8; i++)
                    block[h->c.idsp.idct_permutation[i]] += ROUNDED_DIV(ac_val[i + 8] * qscale_table[xy], h->c.qscale);
            }
        }
    }
    /* left copy */
    for (i = 1; i < 8; i++)
        ac_val1[i] = block[h->c.idsp.idct_permutation[i << 3]];

    /* top copy */
    for (i = 1; i < 8; i++)
        ac_val1[8 + i] = block[h->c.idsp.idct_permutation[i]];
}

/**
 * check if the next stuff is a resync marker or the end.
 * @return 0 if not
 */
static inline int mpeg4_is_resync(Mpeg4DecContext *ctx)
{
    H263DecContext *const h = &ctx->h;
    int bits_count = get_bits_count(&h->gb);
    int v          = show_bits(&h->gb, 16);

    if (h->c.workaround_bugs & FF_BUG_NO_PADDING && !ctx->resync_marker)
        return 0;

    while (v <= 0xFF) {
        if (h->c.pict_type == AV_PICTURE_TYPE_B ||
            (v >> (8 - h->c.pict_type) != 1) || h->partitioned_frame)
            break;
        skip_bits(&h->gb, 8 + h->c.pict_type);
        bits_count += 8 + h->c.pict_type;
        v = show_bits(&h->gb, 16);
    }

    if (bits_count + 8 >= h->gb.size_in_bits) {
        v >>= 8;
        v  |= 0x7F >> (7 - (bits_count & 7));

        if (v == 0x7F)
            return h->c.mb_num;
    } else {
        static const uint16_t mpeg4_resync_prefix[8] = {
            0x7F00, 0x7E00, 0x7C00, 0x7800, 0x7000, 0x6000, 0x4000, 0x0000
        };

        if (v == mpeg4_resync_prefix[bits_count & 7]) {
            int len, mb_num;
            int mb_num_bits = av_log2(h->c.mb_num - 1) + 1;
            GetBitContext gb = h->gb;

            skip_bits(&h->gb, 1);
            align_get_bits(&h->gb);

            for (len = 0; len < 32; len++)
                if (get_bits1(&h->gb))
                    break;

            mb_num = get_bits(&h->gb, mb_num_bits);
            if (!mb_num || mb_num > h->c.mb_num || get_bits_count(&h->gb) + 6 > h->gb.size_in_bits)
                mb_num= -1;

            h->gb = gb;

            if (len >= ff_mpeg4_get_video_packet_prefix_length(h->c.pict_type, ctx->f_code, ctx->b_code))
                return mb_num;
        }
    }
    return 0;
}

static int mpeg4_decode_sprite_trajectory(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    MpegEncContext *s = &ctx->h.c;
    int a     = 2 << ctx->sprite_warping_accuracy;
    int rho   = 3  - ctx->sprite_warping_accuracy;
    int r     = 16 / a;
    int alpha = 1;
    int beta  = 0;
    int w     = s->width;
    int h     = s->height;
    int min_ab, i, w2, h2, w3, h3;
    int sprite_ref[4][2];
    int virtual_ref[2][2];
    int64_t sprite_offset[2][2];
    int64_t sprite_delta[2][2];

    // only true for rectangle shapes
    const int vop_ref[4][2] = { { 0, 0 },         { s->width, 0 },
                                { 0, s->height }, { s->width, s->height } };
    int d[4][2]             = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };

    if (w <= 0 || h <= 0)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < ctx->num_sprite_warping_points; i++) {
        int length;
        int x = 0, y = 0;

        length = get_vlc2(gb, sprite_trajectory, SPRITE_TRAJ_VLC_BITS, 2);
        if (length > 0)
            x = get_xbits(gb, length);

        if (!(ctx->divx_version == 500 && ctx->divx_build == 413))
            check_marker(s->avctx, gb, "before sprite_trajectory");

        length = get_vlc2(gb, sprite_trajectory, SPRITE_TRAJ_VLC_BITS, 2);
        if (length > 0)
            y = get_xbits(gb, length);

        check_marker(s->avctx, gb, "after sprite_trajectory");
        ctx->sprite_traj[i][0] = d[i][0] = x;
        ctx->sprite_traj[i][1] = d[i][1] = y;
    }
    for (; i < 4; i++)
        ctx->sprite_traj[i][0] = ctx->sprite_traj[i][1] = 0;

    while ((1 << alpha) < w)
        alpha++;
    while ((1 << beta) < h)
        beta++;  /* typo in the MPEG-4 std for the definition of w' and h' */
    w2 = 1 << alpha;
    h2 = 1 << beta;

    // Note, the 4th point isn't used for GMC
    if (ctx->divx_version == 500 && ctx->divx_build == 413) {
        sprite_ref[0][0] = a * vop_ref[0][0] + d[0][0];
        sprite_ref[0][1] = a * vop_ref[0][1] + d[0][1];
        sprite_ref[1][0] = a * vop_ref[1][0] + d[0][0] + d[1][0];
        sprite_ref[1][1] = a * vop_ref[1][1] + d[0][1] + d[1][1];
        sprite_ref[2][0] = a * vop_ref[2][0] + d[0][0] + d[2][0];
        sprite_ref[2][1] = a * vop_ref[2][1] + d[0][1] + d[2][1];
    } else {
        sprite_ref[0][0] = (a >> 1) * (2 * vop_ref[0][0] + d[0][0]);
        sprite_ref[0][1] = (a >> 1) * (2 * vop_ref[0][1] + d[0][1]);
        sprite_ref[1][0] = (a >> 1) * (2 * vop_ref[1][0] + d[0][0] + d[1][0]);
        sprite_ref[1][1] = (a >> 1) * (2 * vop_ref[1][1] + d[0][1] + d[1][1]);
        sprite_ref[2][0] = (a >> 1) * (2 * vop_ref[2][0] + d[0][0] + d[2][0]);
        sprite_ref[2][1] = (a >> 1) * (2 * vop_ref[2][1] + d[0][1] + d[2][1]);
    }
    /* sprite_ref[3][0] = (a >> 1) * (2 * vop_ref[3][0] + d[0][0] + d[1][0] + d[2][0] + d[3][0]);
     * sprite_ref[3][1] = (a >> 1) * (2 * vop_ref[3][1] + d[0][1] + d[1][1] + d[2][1] + d[3][1]); */

    /* This is mostly identical to the MPEG-4 std (and is totally unreadable
     * because of that...). Perhaps it should be reordered to be more readable.
     * The idea behind this virtual_ref mess is to be able to use shifts later
     * per pixel instead of divides so the distance between points is converted
     * from w&h based to w2&h2 based which are of the 2^x form. */
    virtual_ref[0][0] = 16 * (vop_ref[0][0] + w2) +
                         ROUNDED_DIV(((w - w2) *
                                           (r * sprite_ref[0][0] - 16LL * vop_ref[0][0]) +
                                      w2 * (r * sprite_ref[1][0] - 16LL * vop_ref[1][0])), w);
    virtual_ref[0][1] = 16 * vop_ref[0][1] +
                        ROUNDED_DIV(((w - w2) *
                                          (r * sprite_ref[0][1] - 16LL * vop_ref[0][1]) +
                                     w2 * (r * sprite_ref[1][1] - 16LL * vop_ref[1][1])), w);
    virtual_ref[1][0] = 16 * vop_ref[0][0] +
                        ROUNDED_DIV(((h - h2) * (r * sprite_ref[0][0] - 16LL * vop_ref[0][0]) +
                                           h2 * (r * sprite_ref[2][0] - 16LL * vop_ref[2][0])), h);
    virtual_ref[1][1] = 16 * (vop_ref[0][1] + h2) +
                        ROUNDED_DIV(((h - h2) * (r * sprite_ref[0][1] - 16LL * vop_ref[0][1]) +
                                           h2 * (r * sprite_ref[2][1] - 16LL * vop_ref[2][1])), h);

    switch (ctx->num_sprite_warping_points) {
    case 0:
        sprite_offset[0][0]    =
        sprite_offset[0][1]    =
        sprite_offset[1][0]    =
        sprite_offset[1][1]    = 0;
        sprite_delta[0][0]     = a;
        sprite_delta[0][1]     =
        sprite_delta[1][0]     = 0;
        sprite_delta[1][1]     = a;
        ctx->sprite_shift[0]   =
        ctx->sprite_shift[1]   = 0;
        break;
    case 1:     // GMC only
        sprite_offset[0][0]    = sprite_ref[0][0] - a * vop_ref[0][0];
        sprite_offset[0][1]    = sprite_ref[0][1] - a * vop_ref[0][1];
        sprite_offset[1][0]    = ((sprite_ref[0][0] >> 1) | (sprite_ref[0][0] & 1)) -
                                 a * (vop_ref[0][0] / 2);
        sprite_offset[1][1]    = ((sprite_ref[0][1] >> 1) | (sprite_ref[0][1] & 1)) -
                                 a * (vop_ref[0][1] / 2);
        sprite_delta[0][0]     = a;
        sprite_delta[0][1]     =
        sprite_delta[1][0]     = 0;
        sprite_delta[1][1]     = a;
        ctx->sprite_shift[0]   =
        ctx->sprite_shift[1]   = 0;
        break;
    case 2:
        sprite_offset[0][0]    = ((int64_t)      sprite_ref[0][0] * (1 << alpha + rho)) +
                                 ((int64_t) -r * sprite_ref[0][0] + virtual_ref[0][0]) *
                                 ((int64_t)        -vop_ref[0][0]) +
                                 ((int64_t)  r * sprite_ref[0][1] - virtual_ref[0][1]) *
                                 ((int64_t)        -vop_ref[0][1]) + (1 << (alpha + rho - 1));
        sprite_offset[0][1]    = ((int64_t)      sprite_ref[0][1] * (1 << alpha + rho)) +
                                 ((int64_t) -r * sprite_ref[0][1] + virtual_ref[0][1]) *
                                 ((int64_t)        -vop_ref[0][0]) +
                                 ((int64_t) -r * sprite_ref[0][0] + virtual_ref[0][0]) *
                                 ((int64_t)        -vop_ref[0][1]) + (1 << (alpha + rho - 1));
        sprite_offset[1][0]    = (((int64_t)-r * sprite_ref[0][0] + virtual_ref[0][0]) *
                                  ((int64_t)-2 *    vop_ref[0][0] + 1) +
                                  ((int64_t) r * sprite_ref[0][1] - virtual_ref[0][1]) *
                                  ((int64_t)-2 *    vop_ref[0][1] + 1) + 2 * w2 * r *
                                   (int64_t)     sprite_ref[0][0] - 16 * w2 + (1 << (alpha + rho + 1)));
        sprite_offset[1][1]    = (((int64_t)-r * sprite_ref[0][1] + virtual_ref[0][1]) *
                                  ((int64_t)-2 *    vop_ref[0][0] + 1) +
                                  ((int64_t)-r * sprite_ref[0][0] + virtual_ref[0][0]) *
                                  ((int64_t)-2 *    vop_ref[0][1] + 1) + 2 * w2 * r *
                                  (int64_t)      sprite_ref[0][1] - 16 * w2 + (1 << (alpha + rho + 1)));
        sprite_delta[0][0] = (-r * sprite_ref[0][0] + virtual_ref[0][0]);
        sprite_delta[0][1] = (+r * sprite_ref[0][1] - virtual_ref[0][1]);
        sprite_delta[1][0] = (-r * sprite_ref[0][1] + virtual_ref[0][1]);
        sprite_delta[1][1] = (-r * sprite_ref[0][0] + virtual_ref[0][0]);

        ctx->sprite_shift[0]  = alpha + rho;
        ctx->sprite_shift[1]  = alpha + rho + 2;
        break;
    case 3:
        min_ab = FFMIN(alpha, beta);
        w3     = w2 >> min_ab;
        h3     = h2 >> min_ab;
        sprite_offset[0][0]    = ((int64_t)sprite_ref[0][0] * (1 << (alpha + beta + rho - min_ab))) +
                                 ((int64_t)-r * sprite_ref[0][0] + virtual_ref[0][0]) * h3 * (-vop_ref[0][0]) +
                                 ((int64_t)-r * sprite_ref[0][0] + virtual_ref[1][0]) * w3 * (-vop_ref[0][1]) +
                                 ((int64_t)1 << (alpha + beta + rho - min_ab - 1));
        sprite_offset[0][1]    = ((int64_t)sprite_ref[0][1] * (1 << (alpha + beta + rho - min_ab))) +
                                 ((int64_t)-r * sprite_ref[0][1] + virtual_ref[0][1]) * h3 * (-vop_ref[0][0]) +
                                 ((int64_t)-r * sprite_ref[0][1] + virtual_ref[1][1]) * w3 * (-vop_ref[0][1]) +
                                 ((int64_t)1 << (alpha + beta + rho - min_ab - 1));
        sprite_offset[1][0]    = ((int64_t)-r * sprite_ref[0][0] + virtual_ref[0][0]) * h3 * (-2 * vop_ref[0][0] + 1) +
                                 ((int64_t)-r * sprite_ref[0][0] + virtual_ref[1][0]) * w3 * (-2 * vop_ref[0][1] + 1) +
                                  (int64_t)2 * w2 * h3 * r * sprite_ref[0][0] - 16 * w2 * h3 +
                                 ((int64_t)1 << (alpha + beta + rho - min_ab + 1));
        sprite_offset[1][1]    = ((int64_t)-r * sprite_ref[0][1] + virtual_ref[0][1]) * h3 * (-2 * vop_ref[0][0] + 1) +
                                 ((int64_t)-r * sprite_ref[0][1] + virtual_ref[1][1]) * w3 * (-2 * vop_ref[0][1] + 1) +
                                  (int64_t)2 * w2 * h3 * r * sprite_ref[0][1] - 16 * w2 * h3 +
                                 ((int64_t)1 << (alpha + beta + rho - min_ab + 1));
        sprite_delta[0][0] = (-r * (int64_t)sprite_ref[0][0] + virtual_ref[0][0]) * h3;
        sprite_delta[0][1] = (-r * (int64_t)sprite_ref[0][0] + virtual_ref[1][0]) * w3;
        sprite_delta[1][0] = (-r * (int64_t)sprite_ref[0][1] + virtual_ref[0][1]) * h3;
        sprite_delta[1][1] = (-r * (int64_t)sprite_ref[0][1] + virtual_ref[1][1]) * w3;

        ctx->sprite_shift[0]  = alpha + beta + rho - min_ab;
        ctx->sprite_shift[1]  = alpha + beta + rho - min_ab + 2;
        break;
    default:
        av_unreachable("num_sprite_warping_points outside of 0..3 results in an error"
                       "in which num_sprite_warping_points is reset to zero");
    }
    /* try to simplify the situation */
    if (sprite_delta[0][0] == a << ctx->sprite_shift[0] &&
        sprite_delta[0][1] == 0 &&
        sprite_delta[1][0] == 0 &&
        sprite_delta[1][1] == a << ctx->sprite_shift[0]) {
        sprite_offset[0][0] >>= ctx->sprite_shift[0];
        sprite_offset[0][1] >>= ctx->sprite_shift[0];
        sprite_offset[1][0] >>= ctx->sprite_shift[1];
        sprite_offset[1][1] >>= ctx->sprite_shift[1];
        sprite_delta[0][0] = a;
        sprite_delta[0][1] = 0;
        sprite_delta[1][0] = 0;
        sprite_delta[1][1] = a;
        ctx->sprite_shift[0] = 0;
        ctx->sprite_shift[1] = 0;
        ctx->real_sprite_warping_points = 1;
    } else {
        int shift_y = 16 - ctx->sprite_shift[0];
        int shift_c = 16 - ctx->sprite_shift[1];

        for (i = 0; i < 2; i++) {
            if (shift_c < 0 || shift_y < 0 ||
                FFABS(  sprite_offset[0][i]) >= INT_MAX >> shift_y  ||
                FFABS(  sprite_offset[1][i]) >= INT_MAX >> shift_c  ||
                FFABS(   sprite_delta[0][i]) >= INT_MAX >> shift_y  ||
                FFABS(   sprite_delta[1][i]) >= INT_MAX >> shift_y
            ) {
                avpriv_request_sample(s->avctx, "Too large sprite shift, delta or offset");
                goto overflow;
            }
        }

        for (i = 0; i < 2; i++) {
            sprite_offset[0][i]    *= 1 << shift_y;
            sprite_offset[1][i]    *= 1 << shift_c;
            sprite_delta[0][i]     *= 1 << shift_y;
            sprite_delta[1][i]     *= 1 << shift_y;
            ctx->sprite_shift[i]     = 16;

        }
        for (i = 0; i < 2; i++) {
            int64_t sd[2] = {
                sprite_delta[i][0] - a * (1LL<<16),
                sprite_delta[i][1] - a * (1LL<<16)
            };

            if (llabs(sprite_offset[0][i] + sprite_delta[i][0] * (w+16LL)) >= INT_MAX ||
                llabs(sprite_offset[0][i] + sprite_delta[i][1] * (h+16LL)) >= INT_MAX ||
                llabs(sprite_offset[0][i] + sprite_delta[i][0] * (w+16LL) + sprite_delta[i][1] * (h+16LL)) >= INT_MAX ||
                llabs(sprite_delta[i][0] * (w+16LL)) >= INT_MAX ||
                llabs(sprite_delta[i][1] * (h+16LL)) >= INT_MAX ||
                llabs(sd[0]) >= INT_MAX ||
                llabs(sd[1]) >= INT_MAX ||
                llabs(sprite_offset[0][i] + sd[0] * (w+16LL)) >= INT_MAX ||
                llabs(sprite_offset[0][i] + sd[1] * (h+16LL)) >= INT_MAX ||
                llabs(sprite_offset[0][i] + sd[0] * (w+16LL) + sd[1] * (h+16LL)) >= INT_MAX
            ) {
                avpriv_request_sample(s->avctx, "Overflow on sprite points");
                goto overflow;
            }
        }
        ctx->real_sprite_warping_points = ctx->num_sprite_warping_points;
    }

    for (i = 0; i < 4; i++) {
        ctx->sprite_offset[i&1][i>>1] = sprite_offset[i&1][i>>1];
        ctx->sprite_delta [i&1][i>>1] = sprite_delta [i&1][i>>1];
    }

    return 0;
overflow:
    memset(ctx->sprite_offset, 0, sizeof(ctx->sprite_offset));
    memset(ctx->sprite_delta,  0, sizeof(ctx->sprite_delta));
    return AVERROR_PATCHWELCOME;
}

static int decode_new_pred(Mpeg4DecContext *ctx, GetBitContext *gb) {
    int len = FFMIN(ctx->time_increment_bits + 3, 15);

    get_bits(gb, len);
    if (get_bits1(gb))
        get_bits(gb, len);
    check_marker(ctx->h.c.avctx, gb, "after new_pred");

    return 0;
}

/**
 * Decode the next video packet.
 * @return <0 if something went wrong
 */
int ff_mpeg4_decode_video_packet_header(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);

    int mb_num_bits      = av_log2(h->c.mb_num - 1) + 1;
    int header_extension = 0, mb_num, len;

    /* is there enough space left for a video packet + header */
    if (get_bits_count(&h->gb) > h->gb.size_in_bits - 20)
        return AVERROR_INVALIDDATA;

    for (len = 0; len < 32; len++)
        if (get_bits1(&h->gb))
            break;

    if (len != ff_mpeg4_get_video_packet_prefix_length(h->c.pict_type, ctx->f_code, ctx->b_code)) {
        av_log(h->c.avctx, AV_LOG_ERROR, "marker does not match f_code\n");
        return AVERROR_INVALIDDATA;
    }

    if (ctx->shape != RECT_SHAPE) {
        header_extension = get_bits1(&h->gb);
        // FIXME more stuff here
    }

    mb_num = get_bits(&h->gb, mb_num_bits);
    if (mb_num >= h->c.mb_num || !mb_num) {
        av_log(h->c.avctx, AV_LOG_ERROR,
               "illegal mb_num in video packet (%d %d) \n", mb_num, h->c.mb_num);
        return AVERROR_INVALIDDATA;
    }

    h->c.mb_x = mb_num % h->c.mb_width;
    h->c.mb_y = mb_num / h->c.mb_width;

    if (ctx->shape != BIN_ONLY_SHAPE) {
        int qscale = get_bits(&h->gb, ctx->quant_precision);
        if (qscale)
            h->c.chroma_qscale = h->c.qscale = qscale;
    }

    if (ctx->shape == RECT_SHAPE)
        header_extension = get_bits1(&h->gb);

    if (header_extension) {
        while (get_bits1(&h->gb) != 0)
            ;

        check_marker(h->c.avctx, &h->gb, "before time_increment in video packed header");
        skip_bits(&h->gb, ctx->time_increment_bits);      /* time_increment */
        check_marker(h->c.avctx, &h->gb, "before vop_coding_type in video packed header");

        skip_bits(&h->gb, 2); /* vop coding type */
        // FIXME not rect stuff here

        if (ctx->shape != BIN_ONLY_SHAPE) {
            skip_bits(&h->gb, 3); /* intra dc vlc threshold */
            // FIXME don't just ignore everything
            if (h->c.pict_type == AV_PICTURE_TYPE_S &&
                ctx->vol_sprite_usage == GMC_SPRITE) {
                if (mpeg4_decode_sprite_trajectory(ctx, &h->gb) < 0)
                    return AVERROR_INVALIDDATA;
                av_log(h->c.avctx, AV_LOG_ERROR, "untested\n");
            }

            // FIXME reduced res stuff here

            if (h->c.pict_type != AV_PICTURE_TYPE_I) {
                int f_code = get_bits(&h->gb, 3);       /* fcode_for */
                if (f_code == 0)
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "Error, video packet header damaged (f_code=0)\n");
            }
            if (h->c.pict_type == AV_PICTURE_TYPE_B) {
                int b_code = get_bits(&h->gb, 3);
                if (b_code == 0)
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "Error, video packet header damaged (b_code=0)\n");
            }
        }
    }
    if (ctx->new_pred)
        decode_new_pred(ctx, &h->gb);

    return 0;
}

static void reset_studio_dc_predictors(Mpeg4DecContext *const ctx)
{
    MPVContext *const s = &ctx->h.c;
    /* Reset DC Predictors */
    s->last_dc[0] =
    s->last_dc[1] =
    s->last_dc[2] = 1 << (s->avctx->bits_per_raw_sample + ctx->dct_precision + s->intra_dc_precision - 1);
}

/**
 * Decode the next video packet.
 * @return <0 if something went wrong
 */
int ff_mpeg4_decode_studio_slice_header(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);
    GetBitContext *gb = &h->gb;
    unsigned vlc_len;
    uint16_t mb_num;

    if (get_bits_left(gb) >= 32 && get_bits_long(gb, 32) == SLICE_STARTCODE) {
        vlc_len = av_log2(h->c.mb_width * h->c.mb_height) + 1;
        mb_num = get_bits(gb, vlc_len);

        if (mb_num >= h->c.mb_num)
            return AVERROR_INVALIDDATA;

        h->c.mb_x = mb_num % h->c.mb_width;
        h->c.mb_y = mb_num / h->c.mb_width;

        if (ctx->shape != BIN_ONLY_SHAPE)
            h->c.qscale = mpeg_get_qscale(&h->gb, h->c.q_scale_type);

        if (get_bits1(gb)) {  /* slice_extension_flag */
            skip_bits1(gb);   /* intra_slice */
            skip_bits1(gb);   /* slice_VOP_id_enable */
            skip_bits(gb, 6); /* slice_VOP_id */
            while (get_bits1(gb)) /* extra_bit_slice */
                skip_bits(gb, 8); /* extra_information_slice */
        }

        reset_studio_dc_predictors(ctx);
    }
    else {
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 * Get the average motion vector for a GMC MB.
 * @param n either 0 for the x component or 1 for y
 * @return the average MV for a GMC MB
 */
static inline int get_amv(Mpeg4DecContext *ctx, int n)
{
    MPVContext *const s = &ctx->h.c;
    int x, y, mb_v, sum, dx, dy, shift;
    int len     = 1 << (ctx->f_code + 4);
    const int a = ctx->sprite_warping_accuracy;

    if (s->workaround_bugs & FF_BUG_AMV)
        len >>= s->quarter_sample;

    if (ctx->real_sprite_warping_points == 1) {
        if (ctx->divx_version == 500 && ctx->divx_build == 413 && a >= s->quarter_sample)
            sum = ctx->sprite_offset[0][n] / (1 << (a - s->quarter_sample));
        else
            sum = RSHIFT(ctx->sprite_offset[0][n] * (1 << s->quarter_sample), a);
    } else {
        dx    = ctx->sprite_delta[n][0];
        dy    = ctx->sprite_delta[n][1];
        shift = ctx->sprite_shift[0];
        if (n)
            dy -= 1 << (shift + a + 1);
        else
            dx -= 1 << (shift + a + 1);
        mb_v = ctx->sprite_offset[0][n] + dx * s->mb_x * 16U + dy * s->mb_y * 16U;

        sum = 0;
        for (y = 0; y < 16; y++) {
            int v;

            v = mb_v + (unsigned)dy * y;
            // FIXME optimize
            for (x = 0; x < 16; x++) {
                sum += v >> shift;
                v   += dx;
            }
        }
        sum = RSHIFT(sum, a + 8 - s->quarter_sample);
    }

    if (sum < -len)
        sum = -len;
    else if (sum >= len)
        sum = len - 1;

    return sum;
}

/**
 * Predict the dc.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir_ptr pointer to an integer where the prediction direction will be stored
 */
static inline int mpeg4_pred_dc(MpegEncContext *s, int n, int *dir_ptr)
{
    const int16_t *const dc_val = s->dc_val + s->block_index[n];
    const int wrap = s->block_wrap[n];
    int pred;

    /* find prediction */

    /* B C
     * A X
     */
    int a = dc_val[-1];
    int b = dc_val[-1 - wrap];
    int c = dc_val[-wrap];

    /* outside slice handling (we can't do that by memset as we need the
     * dc for error resilience) */
    if (s->first_slice_line && n != 3) {
        if (n != 2)
            b = c = 1024;
        if (n != 1 && s->mb_x == s->resync_mb_x)
            b = a = 1024;
    }
    if (s->mb_x == s->resync_mb_x && s->mb_y == s->resync_mb_y + 1) {
        if (n == 0 || n == 4 || n == 5)
            b = 1024;
    }

    if (abs(a - b) < abs(b - c)) {
        pred     = c;
        *dir_ptr = 1; /* top */
    } else {
        pred     = a;
        *dir_ptr = 0; /* left */
    }
    return pred;
}

static inline int mpeg4_get_level_dc(MpegEncContext *s, int n, int pred, int level)
{
    int scale = n < 4 ? s->y_dc_scale : s->c_dc_scale;
    int ret;

    if (IS_3IV1)
        scale = 8;

    /* we assume pred is positive */
    pred = FASTDIV((pred + (scale >> 1)), scale);

    level += pred;
    ret    = level;
    level *= scale;
    if (level & (~2047)) {
        if (s->avctx->err_recognition & (AV_EF_BITSTREAM | AV_EF_AGGRESSIVE)) {
            if (level < 0) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "dc<0 at %dx%d\n", s->mb_x, s->mb_y);
                return AVERROR_INVALIDDATA;
            }
            if (level > 2048 + scale) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "dc overflow at %dx%d\n", s->mb_x, s->mb_y);
                return AVERROR_INVALIDDATA;
            }
        }
        if (level < 0)
            level = 0;
        else if (!(s->workaround_bugs & FF_BUG_DC_CLIP))
            level = 2047;
    }
    s->dc_val[s->block_index[n]] = level;

    return ret;
}

/**
 * Decode the dc value.
 * @param n block index (0-3 are luma, 4-5 are chroma)
 * @param dir_ptr the prediction direction will be stored here
 * @return the quantized dc
 */
static inline int mpeg4_decode_dc(H263DecContext *const h, int n, int *dir_ptr)
{
    int level, code, pred;

    if (n < 4)
        code = get_vlc2(&h->gb, dc_lum, DC_VLC_BITS, 1);
    else
        code = get_vlc2(&h->gb, dc_chrom, DC_VLC_BITS, 1);

    if (code < 0) {
        av_log(h->c.avctx, AV_LOG_ERROR, "illegal dc vlc\n");
        return AVERROR_INVALIDDATA;
    }

    if (code == 0) {
        level = 0;
    } else {
        if (IS_3IV1) {
            if (code == 1)
                level = 2 * get_bits1(&h->gb) - 1;
            else {
                if (get_bits1(&h->gb))
                    level = get_bits(&h->gb, code - 1) + (1 << (code - 1));
                else
                    level = -get_bits(&h->gb, code - 1) - (1 << (code - 1));
            }
        } else {
            level = get_xbits(&h->gb, code);
        }

        if (code > 8) {
            if (get_bits1(&h->gb) == 0) { /* marker */
                if (h->c.avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_COMPLIANT)) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "dc marker bit missing\n");
                    return AVERROR_INVALIDDATA;
                }
            }
        }
    }

    pred = mpeg4_pred_dc(&h->c, n, dir_ptr);
    return mpeg4_get_level_dc(&h->c, n, pred, level);
}

/**
 * Decode first partition.
 * @return number of MBs decoded or <0 if an error occurred
 */
static int mpeg4_decode_partition_a(Mpeg4DecContext *ctx)
{
    H263DecContext *const h = &ctx->h;
    int mb_num = 0;
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    /* decode first partition */
    h->c.first_slice_line = 1;
    for (; h->c.mb_y < h->c.mb_height; h->c.mb_y++) {
        ff_init_block_index(&h->c);
        for (; h->c.mb_x < h->c.mb_width; h->c.mb_x++) {
            const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;
            int cbpc;
            int dir = 0;

            mb_num++;
            ff_update_block_index(&h->c, 8, h->c.avctx->lowres, 1);
            if (h->c.mb_x == h->c.resync_mb_x && h->c.mb_y == h->c.resync_mb_y + 1)
                h->c.first_slice_line = 0;

            if (h->c.pict_type == AV_PICTURE_TYPE_I) {
                int i;

                do {
                    if (show_bits(&h->gb, 19) == DC_MARKER)
                        return mb_num - 1;

                    cbpc = get_vlc2(&h->gb, ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 2);
                    if (cbpc < 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "mcbpc corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                        return AVERROR_INVALIDDATA;
                    }
                } while (cbpc == 8);

                h->c.cbp_table[xy]       = cbpc & 3;
                h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA;
                h->c.mb_intra            = 1;

                if (cbpc & 4)
                    ff_set_qscale(&h->c, h->c.qscale + quant_tab[get_bits(&h->gb, 2)]);

                h->c.cur_pic.qscale_table[xy] = h->c.qscale;

                h->c.mbintra_table[xy] = 1;
                for (i = 0; i < 6; i++) {
                    int dc_pred_dir;
                    int dc = mpeg4_decode_dc(h, i, &dc_pred_dir);
                    if (dc < 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "DC corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                        return dc;
                    }
                    dir <<= 1;
                    if (dc_pred_dir)
                        dir |= 1;
                }
                h->c.pred_dir_table[xy] = dir;
            } else { /* P/S_TYPE */
                int mx, my, pred_x, pred_y, bits;
                int16_t *const mot_val = h->c.cur_pic.motion_val[0][h->c.block_index[0]];
                const int stride       = h->c.b8_stride * 2;

try_again:
                bits = show_bits(&h->gb, 17);
                if (bits == MOTION_MARKER)
                    return mb_num - 1;

                skip_bits1(&h->gb);
                if (bits & 0x10000) {
                    /* skip mb */
                    if (h->c.pict_type == AV_PICTURE_TYPE_S &&
                        ctx->vol_sprite_usage == GMC_SPRITE) {
                        h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP  |
                                                         MB_TYPE_16x16 |
                                                         MB_TYPE_GMC   |
                                                         MB_TYPE_FORWARD_MV;
                        mx = get_amv(ctx, 0);
                        my = get_amv(ctx, 1);
                    } else {
                        h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP  |
                                                         MB_TYPE_16x16 |
                                                         MB_TYPE_FORWARD_MV;
                        mx = my = 0;
                    }
                    mot_val[0]          =
                    mot_val[2]          =
                    mot_val[0 + stride] =
                    mot_val[2 + stride] = mx;
                    mot_val[1]          =
                    mot_val[3]          =
                    mot_val[1 + stride] =
                    mot_val[3 + stride] = my;

                    ff_h263_clean_intra_table_entries(&h->c, xy);
                    continue;
                }

                cbpc = get_vlc2(&h->gb, ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 2);
                if (cbpc < 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "mcbpc corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                    return AVERROR_INVALIDDATA;
                }
                if (cbpc == 20)
                    goto try_again;

                h->c.cbp_table[xy] = cbpc & (8 + 3);  // 8 is dquant

                h->c.mb_intra = ((cbpc & 4) != 0);

                if (h->c.mb_intra) {
                    h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA;
                    h->c.mbintra_table[xy] = 1;
                    mot_val[0]          =
                    mot_val[2]          =
                    mot_val[0 + stride] =
                    mot_val[2 + stride] = 0;
                    mot_val[1]          =
                    mot_val[3]          =
                    mot_val[1 + stride] =
                    mot_val[3 + stride] = 0;
                } else {
                    ff_h263_clean_intra_table_entries(&h->c, xy);

                    if (h->c.pict_type == AV_PICTURE_TYPE_S &&
                        ctx->vol_sprite_usage == GMC_SPRITE &&
                        (cbpc & 16) == 0)
                        h->c.mcsel = get_bits1(&h->gb);
                    else
                        h->c.mcsel = 0;

                    if ((cbpc & 16) == 0) {
                        /* 16x16 motion prediction */

                        ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);
                        if (!h->c.mcsel) {
                            mx = ff_h263_decode_motion(h, pred_x, ctx->f_code);
                            if (mx >= 0xffff)
                                return AVERROR_INVALIDDATA;

                            my = ff_h263_decode_motion(h, pred_y, ctx->f_code);
                            if (my >= 0xffff)
                                return AVERROR_INVALIDDATA;
                            h->c.cur_pic.mb_type[xy] = MB_TYPE_16x16 |
                                                             MB_TYPE_FORWARD_MV;
                        } else {
                            mx = get_amv(ctx, 0);
                            my = get_amv(ctx, 1);
                            h->c.cur_pic.mb_type[xy] = MB_TYPE_16x16 |
                                                             MB_TYPE_GMC   |
                                                             MB_TYPE_FORWARD_MV;
                        }

                        mot_val[0]          =
                        mot_val[2]          =
                        mot_val[0 + stride] =
                        mot_val[2 + stride] = mx;
                        mot_val[1]          =
                        mot_val[3]          =
                        mot_val[1 + stride] =
                        mot_val[3 + stride] = my;
                    } else {
                        int i;
                        h->c.cur_pic.mb_type[xy] = MB_TYPE_8x8 |
                                                         MB_TYPE_FORWARD_MV;
                        for (i = 0; i < 4; i++) {
                            int16_t *mot_val = ff_h263_pred_motion(&h->c, i, 0, &pred_x, &pred_y);
                            mx = ff_h263_decode_motion(h, pred_x, ctx->f_code);
                            if (mx >= 0xffff)
                                return AVERROR_INVALIDDATA;

                            my = ff_h263_decode_motion(h, pred_y, ctx->f_code);
                            if (my >= 0xffff)
                                return AVERROR_INVALIDDATA;
                            mot_val[0] = mx;
                            mot_val[1] = my;
                        }
                    }
                }
            }
        }
        h->c.mb_x = 0;
    }

    return mb_num;
}

/**
 * decode second partition.
 * @return <0 if an error occurred
 */
static int mpeg4_decode_partition_b(H263DecContext *const h, int mb_count)
{
    int mb_num = 0;
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };

    h->c.mb_x = h->c.resync_mb_x;
    h->c.first_slice_line = 1;
    for (h->c.mb_y = h->c.resync_mb_y; mb_num < mb_count; h->c.mb_y++) {
        ff_init_block_index(&h->c);
        for (; mb_num < mb_count && h->c.mb_x < h->c.mb_width; h->c.mb_x++) {
            const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;

            mb_num++;
            ff_update_block_index(&h->c, 8, h->c.avctx->lowres, 1);
            if (h->c.mb_x == h->c.resync_mb_x && h->c.mb_y == h->c.resync_mb_y + 1)
                h->c.first_slice_line = 0;

            if (h->c.pict_type == AV_PICTURE_TYPE_I) {
                int ac_pred = get_bits1(&h->gb);
                int cbpy    = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
                if (cbpy < 0) {
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "cbpy corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                    return AVERROR_INVALIDDATA;
                }

                h->c.cbp_table[xy]       |= cbpy << 2;
                h->c.cur_pic.mb_type[xy] |= ac_pred * MB_TYPE_ACPRED;
            } else { /* P || S_TYPE */
                if (IS_INTRA(h->c.cur_pic.mb_type[xy])) {
                    int i;
                    int dir     = 0;
                    int ac_pred = get_bits1(&h->gb);
                    int cbpy    = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);

                    if (cbpy < 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "I cbpy corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                        return AVERROR_INVALIDDATA;
                    }

                    if (h->c.cbp_table[xy] & 8)
                        ff_set_qscale(&h->c, h->c.qscale + quant_tab[get_bits(&h->gb, 2)]);
                    h->c.cur_pic.qscale_table[xy] = h->c.qscale;

                    for (i = 0; i < 6; i++) {
                        int dc_pred_dir;
                        int dc = mpeg4_decode_dc(h, i, &dc_pred_dir);
                        if (dc < 0) {
                            av_log(h->c.avctx, AV_LOG_ERROR,
                                   "DC corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                            return dc;
                        }
                        dir <<= 1;
                        if (dc_pred_dir)
                            dir |= 1;
                    }
                    h->c.cbp_table[xy]       &= 3;  // remove dquant
                    h->c.cbp_table[xy]       |= cbpy << 2;
                    h->c.cur_pic.mb_type[xy] |= ac_pred * MB_TYPE_ACPRED;
                    h->c.pred_dir_table[xy]   = dir;
                } else if (IS_SKIP(h->c.cur_pic.mb_type[xy])) {
                    h->c.cur_pic.qscale_table[xy] = h->c.qscale;
                    h->c.cbp_table[xy]            = 0;
                } else {
                    int cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);

                    if (cbpy < 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "P cbpy corrupted at %d %d\n", h->c.mb_x, h->c.mb_y);
                        return AVERROR_INVALIDDATA;
                    }

                    if (h->c.cbp_table[xy] & 8)
                        ff_set_qscale(&h->c, h->c.qscale + quant_tab[get_bits(&h->gb, 2)]);
                    h->c.cur_pic.qscale_table[xy] = h->c.qscale;

                    h->c.cbp_table[xy] &= 3;  // remove dquant
                    h->c.cbp_table[xy] |= (cbpy ^ 0xf) << 2;
                }
            }
        }
        if (mb_num >= mb_count)
            return 0;
        h->c.mb_x = 0;
    }
    return 0;
}

/**
 * Decode the first and second partition.
 * @return <0 if error (and sets error type in the error_status_table)
 */
int ff_mpeg4_decode_partitions(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);
    int mb_num;
    int ret;
    const int part_a_error = h->c.pict_type == AV_PICTURE_TYPE_I ? (ER_DC_ERROR | ER_MV_ERROR) : ER_MV_ERROR;
    const int part_a_end   = h->c.pict_type == AV_PICTURE_TYPE_I ? (ER_DC_END   | ER_MV_END)   : ER_MV_END;

    mb_num = mpeg4_decode_partition_a(ctx);
    if (mb_num <= 0) {
        ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                        h->c.mb_x, h->c.mb_y, part_a_error);
        return mb_num ? mb_num : AVERROR_INVALIDDATA;
    }

    if (h->c.resync_mb_x + h->c.resync_mb_y * h->c.mb_width + mb_num > h->c.mb_num) {
        av_log(h->c.avctx, AV_LOG_ERROR, "slice below monitor ...\n");
        ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                        h->c.mb_x, h->c.mb_y, part_a_error);
        return AVERROR_INVALIDDATA;
    }

    h->mb_num_left = mb_num;

    if (h->c.pict_type == AV_PICTURE_TYPE_I) {
        while (show_bits(&h->gb, 9) == 1)
            skip_bits(&h->gb, 9);
        if (get_bits(&h->gb, 19) != DC_MARKER) {
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "marker missing after first I partition at %d %d\n",
                   h->c.mb_x, h->c.mb_y);
            return AVERROR_INVALIDDATA;
        }
    } else {
        while (show_bits(&h->gb, 10) == 1)
            skip_bits(&h->gb, 10);
        if (get_bits(&h->gb, 17) != MOTION_MARKER) {
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "marker missing after first P partition at %d %d\n",
                   h->c.mb_x, h->c.mb_y);
            return AVERROR_INVALIDDATA;
        }
    }
    ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                    h->c.mb_x - 1, h->c.mb_y, part_a_end);

    ret = mpeg4_decode_partition_b(h, mb_num);
    if (ret < 0) {
        if (h->c.pict_type == AV_PICTURE_TYPE_P)
            ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                            h->c.mb_x, h->c.mb_y, ER_DC_ERROR);
        return ret;
    } else {
        if (h->c.pict_type == AV_PICTURE_TYPE_P)
            ff_er_add_slice(&h->c.er, h->c.resync_mb_x, h->c.resync_mb_y,
                            h->c.mb_x - 1, h->c.mb_y, ER_DC_END);
    }

    return 0;
}

/**
 * Decode a block.
 * @return <0 if an error occurred
 */
static inline int mpeg4_decode_block(Mpeg4DecContext *ctx, int16_t *block,
                                     int n, int coded, int intra,
                                     int use_intra_dc_vlc, int rvlc)
{
    H263DecContext *const h = &ctx->h;
    int level, i, last, run, qmul, qadd, pred;
    int av_uninit(dc_pred_dir);
    const RLTable *rl;
    const RL_VLC_ELEM *rl_vlc;
    const uint8_t *scan_table;

    // Note intra & rvlc should be optimized away if this is inlined

    if (intra) {
        // FIXME add short header support
        if (use_intra_dc_vlc) {
            /* DC coef */
            if (h->partitioned_frame) {
                level = h->c.dc_val[h->c.block_index[n]];
                if (n < 4)
                    level = FASTDIV((level + (h->c.y_dc_scale >> 1)), h->c.y_dc_scale);
                else
                    level = FASTDIV((level + (h->c.c_dc_scale >> 1)), h->c.c_dc_scale);
                dc_pred_dir = (h->c.pred_dir_table[h->c.mb_x + h->c.mb_y * h->c.mb_stride] << n) & 32;
            } else {
                level = mpeg4_decode_dc(h, n, &dc_pred_dir);
                if (level < 0)
                    return level;
            }
            block[0] = level;
            i        = 0;
        } else {
            i = -1;
            pred = mpeg4_pred_dc(&h->c, n, &dc_pred_dir);
        }
        if (!coded)
            goto not_coded;

        if (rvlc) {
            rl     = &ff_rvlc_rl_intra;
            rl_vlc = ff_rvlc_rl_intra.rl_vlc[0];
        } else {
            rl     = &ff_mpeg4_rl_intra;
            rl_vlc = ff_mpeg4_rl_intra.rl_vlc[0];
        }
        if (h->c.ac_pred) {
            if (dc_pred_dir == 0)
                scan_table = h->c.permutated_intra_v_scantable;  /* left */
            else
                scan_table = h->c.permutated_intra_h_scantable;  /* top */
        } else {
            scan_table = h->c.intra_scantable.permutated;
        }
        qmul = 1;
        qadd = 0;
    } else {
        i = -1;
        if (!coded) {
            h->c.block_last_index[n] = i;
            return 0;
        }
        if (rvlc)
            rl = &ff_rvlc_rl_inter;
        else
            rl = &ff_h263_rl_inter;

        scan_table = h->c.intra_scantable.permutated;

        if (ctx->mpeg_quant) {
            qmul = 1;
            qadd = 0;
            if (rvlc)
                rl_vlc = ff_rvlc_rl_inter.rl_vlc[0];
            else
                rl_vlc = ff_h263_rl_inter.rl_vlc[0];
        } else {
            qmul = h->c.qscale << 1;
            qadd = (h->c.qscale - 1) | 1;
            if (rvlc)
                rl_vlc = ff_rvlc_rl_inter.rl_vlc[h->c.qscale];
            else
                rl_vlc = ff_h263_rl_inter.rl_vlc[h->c.qscale];
        }
    }
    {
        OPEN_READER(re, &h->gb);
        for (;;) {
            UPDATE_CACHE(re, &h->gb);
            GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 0);
            if (level == 0) {
                /* escape */
                if (rvlc) {
                    if (SHOW_UBITS(re, &h->gb, 1) == 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "1. marker bit missing in rvlc esc\n");
                        return AVERROR_INVALIDDATA;
                    }
                    SKIP_CACHE(re, &h->gb, 1);

                    last = SHOW_UBITS(re, &h->gb, 1);
                    SKIP_CACHE(re, &h->gb, 1);
                    run = SHOW_UBITS(re, &h->gb, 6);
                    SKIP_COUNTER(re, &h->gb, 1 + 1 + 6);
                    UPDATE_CACHE(re, &h->gb);

                    if (SHOW_UBITS(re, &h->gb, 1) == 0) {
                        av_log(h->c.avctx, AV_LOG_ERROR,
                               "2. marker bit missing in rvlc esc\n");
                        return AVERROR_INVALIDDATA;
                    }
                    SKIP_CACHE(re, &h->gb, 1);

                    level = SHOW_UBITS(re, &h->gb, 11);
                    SKIP_CACHE(re, &h->gb, 11);

                    if (SHOW_UBITS(re, &h->gb, 5) != 0x10) {
                        av_log(h->c.avctx, AV_LOG_ERROR, "reverse esc missing\n");
                        return AVERROR_INVALIDDATA;
                    }
                    SKIP_CACHE(re, &h->gb, 5);

                    level = level * qmul + qadd;
                    level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                    SKIP_COUNTER(re, &h->gb, 1 + 11 + 5 + 1);

                    i += run + 1;
                    if (last)
                        i += 192;
                } else {
                    int cache;
                    cache = GET_CACHE(re, &h->gb);

                    if (IS_3IV1)
                        cache ^= 0xC0000000;

                    if (cache & 0x80000000) {
                        if (cache & 0x40000000) {
                            /* third escape */
                            SKIP_CACHE(re, &h->gb, 2);
                            last = SHOW_UBITS(re, &h->gb, 1);
                            SKIP_CACHE(re, &h->gb, 1);
                            run = SHOW_UBITS(re, &h->gb, 6);
                            SKIP_COUNTER(re, &h->gb, 2 + 1 + 6);
                            UPDATE_CACHE(re, &h->gb);

                            if (IS_3IV1) {
                                level = SHOW_SBITS(re, &h->gb, 12);
                                LAST_SKIP_BITS(re, &h->gb, 12);
                            } else {
                                if (SHOW_UBITS(re, &h->gb, 1) == 0) {
                                    av_log(h->c.avctx, AV_LOG_ERROR,
                                           "1. marker bit missing in 3. esc\n");
                                    if (!(h->c.avctx->err_recognition & AV_EF_IGNORE_ERR) || get_bits_left(&h->gb) <= 0)
                                        return AVERROR_INVALIDDATA;
                                }
                                SKIP_CACHE(re, &h->gb, 1);

                                level = SHOW_SBITS(re, &h->gb, 12);
                                SKIP_CACHE(re, &h->gb, 12);

                                if (SHOW_UBITS(re, &h->gb, 1) == 0) {
                                    av_log(h->c.avctx, AV_LOG_ERROR,
                                           "2. marker bit missing in 3. esc\n");
                                    if (!(h->c.avctx->err_recognition & AV_EF_IGNORE_ERR) || get_bits_left(&h->gb) <= 0)
                                        return AVERROR_INVALIDDATA;
                                }

                                SKIP_COUNTER(re, &h->gb, 1 + 12 + 1);
                            }

#if 0
                            if (h->c.error_recognition >= FF_ER_COMPLIANT) {
                                const int abs_level= FFABS(level);
                                if (abs_level<=MAX_LEVEL && run<=MAX_RUN) {
                                    const int run1= run - rl->max_run[last][abs_level] - 1;
                                    if (abs_level <= rl->max_level[last][run]) {
                                        av_log(h->c.avctx, AV_LOG_ERROR, "illegal 3. esc, vlc encoding possible\n");
                                        return AVERROR_INVALIDDATA;
                                    }
                                    if (h->c.error_recognition > FF_ER_COMPLIANT) {
                                        if (abs_level <= rl->max_level[last][run]*2) {
                                            av_log(h->c.avctx, AV_LOG_ERROR, "illegal 3. esc, esc 1 encoding possible\n");
                                            return AVERROR_INVALIDDATA;
                                        }
                                        if (run1 >= 0 && abs_level <= rl->max_level[last][run1]) {
                                            av_log(h->c.avctx, AV_LOG_ERROR, "illegal 3. esc, esc 2 encoding possible\n");
                                            return AVERROR_INVALIDDATA;
                                        }
                                    }
                                }
                            }
#endif
                            if (level > 0)
                                level = level * qmul + qadd;
                            else
                                level = level * qmul - qadd;

                            if ((unsigned)(level + 2048) > 4095) {
                                if (h->c.avctx->err_recognition & (AV_EF_BITSTREAM|AV_EF_AGGRESSIVE)) {
                                    if (level > 2560 || level < -2560) {
                                        av_log(h->c.avctx, AV_LOG_ERROR,
                                               "|level| overflow in 3. esc, qp=%d\n",
                                               h->c.qscale);
                                        return AVERROR_INVALIDDATA;
                                    }
                                }
                                level = level < 0 ? -2048 : 2047;
                            }

                            i += run + 1;
                            if (last)
                                i += 192;
                        } else {
                            /* second escape */
                            SKIP_BITS(re, &h->gb, 2);
                            GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                            i    += run + rl->max_run[run >> 7][level / qmul] + 1;  // FIXME opt indexing
                            level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                            LAST_SKIP_BITS(re, &h->gb, 1);
                        }
                    } else {
                        /* first escape */
                        SKIP_BITS(re, &h->gb, 1);
                        GET_RL_VLC(level, run, re, &h->gb, rl_vlc, TEX_VLC_BITS, 2, 1);
                        i    += run;
                        level = level + rl->max_level[run >> 7][(run - 1) & 63] * qmul;  // FIXME opt indexing
                        level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                        LAST_SKIP_BITS(re, &h->gb, 1);
                    }
                }
            } else {
                i    += run;
                level = (level ^ SHOW_SBITS(re, &h->gb, 1)) - SHOW_SBITS(re, &h->gb, 1);
                LAST_SKIP_BITS(re, &h->gb, 1);
            }
            ff_tlog(h->c.avctx, "dct[%d][%d] = %- 4d end?:%d\n", scan_table[i&63]&7, scan_table[i&63] >> 3, level, i>62);
            if (i > 62) {
                i -= 192;
                if (i & (~63)) {
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "ac-tex damaged at %d %d\n", h->c.mb_x, h->c.mb_y);
                    return AVERROR_INVALIDDATA;
                }

                block[scan_table[i]] = level;
                break;
            }

            block[scan_table[i]] = level;
        }
        CLOSE_READER(re, &h->gb);
    }

not_coded:
    if (intra) {
        if (!use_intra_dc_vlc) {
            block[0] = mpeg4_get_level_dc(&h->c, n, pred, block[0]);

            i -= i >> 31;  // if (i == -1) i = 0;
        }

        ff_mpeg4_pred_ac(h, block, n, dc_pred_dir);
        if (h->c.ac_pred)
            i = 63;  // FIXME not optimal
    }
    h->c.block_last_index[n] = i;
    return 0;
}

/**
 * decode partition C of one MB.
 * @return <0 if an error occurred
 */
static int mpeg4_decode_partitioned_mb(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);
    const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;

    const int mb_type = h->c.cur_pic.mb_type[xy];
    int           cbp = h->c.cbp_table[xy];

    const int use_intra_dc_vlc = h->c.qscale < ctx->intra_dc_threshold;

    if (h->c.cur_pic.qscale_table[xy] != h->c.qscale)
        ff_set_qscale(&h->c, h->c.cur_pic.qscale_table[xy]);

    if (h->c.pict_type == AV_PICTURE_TYPE_P ||
        h->c.pict_type == AV_PICTURE_TYPE_S) {
        int i;
        for (i = 0; i < 4; i++) {
            h->c.mv[0][i][0] = h->c.cur_pic.motion_val[0][h->c.block_index[i]][0];
            h->c.mv[0][i][1] = h->c.cur_pic.motion_val[0][h->c.block_index[i]][1];
        }
        h->c.mb_intra = IS_INTRA(mb_type);

        if (IS_SKIP(mb_type)) {
            /* skip mb */
            for (i = 0; i < 6; i++)
                h->c.block_last_index[i] = -1;
            h->c.mv_dir  = MV_DIR_FORWARD;
            h->c.mv_type = MV_TYPE_16X16;
            if (h->c.pict_type == AV_PICTURE_TYPE_S
                && ctx->vol_sprite_usage == GMC_SPRITE) {
                h->c.mcsel      = 1;
                h->c.mb_skipped = 0;
                h->c.cur_pic.mbskip_table[xy] = 0;
            } else {
                h->c.mcsel      = 0;
                h->c.mb_skipped = 1;
                h->c.cur_pic.mbskip_table[xy] = 1;
            }
        } else if (h->c.mb_intra) {
            h->c.ac_pred = IS_ACPRED(h->c.cur_pic.mb_type[xy]);
        } else if (!h->c.mb_intra) {
            // h->c.mcsel = 0;  // FIXME do we need to init that?

            h->c.mv_dir = MV_DIR_FORWARD;
            if (IS_8X8(mb_type)) {
                h->c.mv_type = MV_TYPE_8X8;
            } else {
                h->c.mv_type = MV_TYPE_16X16;
            }
        }
    } else { /* I-Frame */
        h->c.mb_intra = 1;
        h->c.ac_pred  = IS_ACPRED(h->c.cur_pic.mb_type[xy]);
    }

    if (!IS_SKIP(mb_type)) {
        int i;
        h->c.bdsp.clear_blocks(h->block[0]);
        /* decode each block */
        for (i = 0; i < 6; i++) {
            if (mpeg4_decode_block(ctx, h->block[i], i, cbp & 32, h->c.mb_intra,
                                   use_intra_dc_vlc, ctx->rvlc) < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "texture corrupted at %d %d %d\n",
                       h->c.mb_x, h->c.mb_y, h->c.mb_intra);
                return AVERROR_INVALIDDATA;
            }
            cbp += cbp;
        }
    }

    /* per-MB end of slice check */
    if (--h->mb_num_left <= 0) {
        if (mpeg4_is_resync(ctx))
            return SLICE_END;
        else
            return SLICE_NOEND;
    } else {
        if (mpeg4_is_resync(ctx)) {
            const int delta = h->c.mb_x + 1 == h->c.mb_width ? 2 : 1;
            if (h->c.cbp_table[xy + delta])
                return SLICE_END;
        }
        return SLICE_OK;
    }
}

static int mpeg4_decode_mb(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);
    int cbpc, cbpy, i, cbp, pred_x, pred_y, mx, my, dquant;
    static const int8_t quant_tab[4] = { -1, -2, 1, 2 };
    const int xy = h->c.mb_x + h->c.mb_y * h->c.mb_stride;
    int next;

    av_assert2(h->c.h263_pred);

    if (h->c.pict_type == AV_PICTURE_TYPE_P ||
        h->c.pict_type == AV_PICTURE_TYPE_S) {
        do {
            if (get_bits1(&h->gb)) {
                /* skip mb */
                h->c.mb_intra = 0;
                for (i = 0; i < 6; i++)
                    h->c.block_last_index[i] = -1;
                h->c.mv_dir  = MV_DIR_FORWARD;
                h->c.mv_type = MV_TYPE_16X16;
                if (h->c.pict_type == AV_PICTURE_TYPE_S &&
                    ctx->vol_sprite_usage == GMC_SPRITE) {
                    h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP  |
                                                     MB_TYPE_GMC   |
                                                     MB_TYPE_16x16 |
                                             MB_TYPE_FORWARD_MV;
                    h->c.mcsel       = 1;
                    h->c.mv[0][0][0] = get_amv(ctx, 0);
                    h->c.mv[0][0][1] = get_amv(ctx, 1);
                    h->c.cur_pic.mbskip_table[xy] = 0;
                    h->c.mb_skipped  = 0;
                } else {
                    h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP | MB_TYPE_16x16 |
                                             MB_TYPE_FORWARD_MV;
                    h->c.mcsel       = 0;
                    h->c.mv[0][0][0] = 0;
                    h->c.mv[0][0][1] = 0;
                    h->c.cur_pic.mbskip_table[xy] = 1;
                    h->c.mb_skipped  = 1;
                }
                goto end;
            }
            cbpc = get_vlc2(&h->gb, ff_h263_inter_MCBPC_vlc, INTER_MCBPC_VLC_BITS, 2);
            if (cbpc < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "mcbpc damaged at %d %d\n", h->c.mb_x, h->c.mb_y);
                return AVERROR_INVALIDDATA;
            }
        } while (cbpc == 20);

        dquant      = cbpc & 8;
        h->c.mb_intra = ((cbpc & 4) != 0);
        if (h->c.mb_intra)
            goto intra;
        h->c.bdsp.clear_blocks(h->block[0]);

        if (h->c.pict_type == AV_PICTURE_TYPE_S &&
            ctx->vol_sprite_usage == GMC_SPRITE && (cbpc & 16) == 0)
            h->c.mcsel = get_bits1(&h->gb);
        else
            h->c.mcsel = 0;
        cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1) ^ 0x0F;
        if (cbpy < 0) {
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "P cbpy damaged at %d %d\n", h->c.mb_x, h->c.mb_y);
            return AVERROR_INVALIDDATA;
        }

        cbp = (cbpc & 3) | (cbpy << 2);
        if (dquant)
            ff_set_qscale(&h->c, h->c.qscale + quant_tab[get_bits(&h->gb, 2)]);
        if ((!h->c.progressive_sequence) &&
            (cbp || (h->c.workaround_bugs & FF_BUG_XVID_ILACE)))
            h->c.interlaced_dct = get_bits1(&h->gb);

        h->c.mv_dir = MV_DIR_FORWARD;
        if ((cbpc & 16) == 0) {
            if (h->c.mcsel) {
                h->c.cur_pic.mb_type[xy] = MB_TYPE_GMC | MB_TYPE_16x16 |
                                         MB_TYPE_FORWARD_MV;
                /* 16x16 global motion prediction */
                h->c.mv_type   = MV_TYPE_16X16;
                mx             = get_amv(ctx, 0);
                my             = get_amv(ctx, 1);
                h->c.mv[0][0][0] = mx;
                h->c.mv[0][0][1] = my;
            } else if ((!h->c.progressive_sequence) && get_bits1(&h->gb)) {
                h->c.cur_pic.mb_type[xy] = MB_TYPE_16x8 | MB_TYPE_FORWARD_MV |
                                                 MB_TYPE_INTERLACED;
                /* 16x8 field motion prediction */
                h->c.mv_type = MV_TYPE_FIELD;

                h->c.field_select[0][0] = get_bits1(&h->gb);
                h->c.field_select[0][1] = get_bits1(&h->gb);

                ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);

                for (i = 0; i < 2; i++) {
                    mx = ff_h263_decode_motion(h, pred_x, ctx->f_code);
                    if (mx >= 0xffff)
                        return AVERROR_INVALIDDATA;

                    my = ff_h263_decode_motion(h, pred_y / 2, ctx->f_code);
                    if (my >= 0xffff)
                        return AVERROR_INVALIDDATA;

                    h->c.mv[0][i][0] = mx;
                    h->c.mv[0][i][1] = my;
                }
            } else {
                h->c.cur_pic.mb_type[xy] = MB_TYPE_16x16 | MB_TYPE_FORWARD_MV;
                /* 16x16 motion prediction */
                h->c.mv_type             = MV_TYPE_16X16;
                ff_h263_pred_motion(&h->c, 0, 0, &pred_x, &pred_y);
                mx = ff_h263_decode_motion(h, pred_x, ctx->f_code);

                if (mx >= 0xffff)
                    return AVERROR_INVALIDDATA;

                my = ff_h263_decode_motion(h, pred_y, ctx->f_code);

                if (my >= 0xffff)
                    return AVERROR_INVALIDDATA;
                h->c.mv[0][0][0] = mx;
                h->c.mv[0][0][1] = my;
            }
        } else {
            h->c.cur_pic.mb_type[xy] = MB_TYPE_8x8 | MB_TYPE_FORWARD_MV;
            h->c.mv_type             = MV_TYPE_8X8;
            for (i = 0; i < 4; i++) {
                int16_t *mot_val = ff_h263_pred_motion(&h->c, i, 0, &pred_x, &pred_y);
                mx      = ff_h263_decode_motion(h, pred_x, ctx->f_code);
                if (mx >= 0xffff)
                    return AVERROR_INVALIDDATA;

                my = ff_h263_decode_motion(h, pred_y, ctx->f_code);
                if (my >= 0xffff)
                    return AVERROR_INVALIDDATA;
                h->c.mv[0][i][0] = mx;
                h->c.mv[0][i][1] = my;
                mot_val[0]     = mx;
                mot_val[1]     = my;
            }
        }
    } else if (h->c.pict_type == AV_PICTURE_TYPE_B) {
        int modb1;   // first bit of modb
        int modb2;   // second bit of modb
        int mb_type;

        h->c.mb_intra = 0;  // B-frames never contain intra blocks
        h->c.mcsel    = 0;  //      ...               true gmc blocks

        if (h->c.mb_x == 0) {
            for (i = 0; i < 2; i++) {
                h->c.last_mv[i][0][0] =
                h->c.last_mv[i][0][1] =
                h->c.last_mv[i][1][0] =
                h->c.last_mv[i][1][1] = 0;
            }

            ff_thread_progress_await(&h->c.next_pic.ptr->progress, h->c.mb_y);
        }

        /* if we skipped it in the future P-frame than skip it now too */
        h->c.mb_skipped = h->c.next_pic.mbskip_table[h->c.mb_y * h->c.mb_stride + h->c.mb_x];  // Note, skiptab=0 if last was GMC

        if (h->c.mb_skipped) {
            /* skip mb */
            for (i = 0; i < 6; i++)
                h->c.block_last_index[i] = -1;

            h->c.mv_dir      = MV_DIR_FORWARD;
            h->c.mv_type     = MV_TYPE_16X16;
            h->c.mv[0][0][0] =
            h->c.mv[0][0][1] =
            h->c.mv[1][0][0] =
            h->c.mv[1][0][1] = 0;
            h->c.cur_pic.mb_type[xy] = MB_TYPE_SKIP  |
                                             MB_TYPE_16x16 |
                                             MB_TYPE_FORWARD_MV;
            goto end;
        }

        modb1 = get_bits1(&h->gb);
        if (modb1) {
            // like MB_TYPE_B_DIRECT but no vectors coded
            mb_type = MB_TYPE_DIRECT2 | MB_TYPE_SKIP | MB_TYPE_BIDIR_MV;
            cbp     = 0;
        } else {
            modb2   = get_bits1(&h->gb);
            mb_type = get_vlc2(&h->gb, mb_type_b_vlc, MB_TYPE_B_VLC_BITS, 1);
            if (mb_type < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR, "illegal MB_type\n");
                return AVERROR_INVALIDDATA;
            }
            if (modb2) {
                cbp = 0;
            } else {
                h->c.bdsp.clear_blocks(h->block[0]);
                cbp = get_bits(&h->gb, 6);
            }

            if ((!IS_DIRECT(mb_type)) && cbp) {
                if (get_bits1(&h->gb))
                    ff_set_qscale(&h->c, h->c.qscale + get_bits1(&h->gb) * 4 - 2);
            }

            if (!h->c.progressive_sequence) {
                if (cbp)
                    h->c.interlaced_dct = get_bits1(&h->gb);

                if (!IS_DIRECT(mb_type) && get_bits1(&h->gb)) {
                    mb_type |= MB_TYPE_16x8 | MB_TYPE_INTERLACED;
                    mb_type &= ~MB_TYPE_16x16;

                    if (HAS_FORWARD_MV(mb_type)) {
                        h->c.field_select[0][0] = get_bits1(&h->gb);
                        h->c.field_select[0][1] = get_bits1(&h->gb);
                    }
                    if (HAS_BACKWARD_MV(mb_type)) {
                        h->c.field_select[1][0] = get_bits1(&h->gb);
                        h->c.field_select[1][1] = get_bits1(&h->gb);
                    }
                }
            }

            h->c.mv_dir = 0;
            if ((mb_type & (MB_TYPE_DIRECT2 | MB_TYPE_INTERLACED)) == 0) {
                h->c.mv_type = MV_TYPE_16X16;

                if (HAS_FORWARD_MV(mb_type)) {
                    h->c.mv_dir = MV_DIR_FORWARD;

                    mx = ff_h263_decode_motion(h, h->c.last_mv[0][0][0], ctx->f_code);
                    my = ff_h263_decode_motion(h, h->c.last_mv[0][0][1], ctx->f_code);
                    h->c.last_mv[0][1][0] =
                    h->c.last_mv[0][0][0] =
                    h->c.mv[0][0][0]      = mx;
                    h->c.last_mv[0][1][1] =
                    h->c.last_mv[0][0][1] =
                    h->c.mv[0][0][1]      = my;
                }

                if (HAS_BACKWARD_MV(mb_type)) {
                    h->c.mv_dir |= MV_DIR_BACKWARD;

                    mx = ff_h263_decode_motion(h, h->c.last_mv[1][0][0], ctx->b_code);
                    my = ff_h263_decode_motion(h, h->c.last_mv[1][0][1], ctx->b_code);
                    h->c.last_mv[1][1][0] =
                    h->c.last_mv[1][0][0] =
                    h->c.mv[1][0][0]      = mx;
                    h->c.last_mv[1][1][1] =
                    h->c.last_mv[1][0][1] =
                    h->c.mv[1][0][1]      = my;
                }
            } else if (!IS_DIRECT(mb_type)) {
                h->c.mv_type = MV_TYPE_FIELD;

                if (HAS_FORWARD_MV(mb_type)) {
                    h->c.mv_dir = MV_DIR_FORWARD;

                    for (i = 0; i < 2; i++) {
                        mx = ff_h263_decode_motion(h, h->c.last_mv[0][i][0], ctx->f_code);
                        my = ff_h263_decode_motion(h, h->c.last_mv[0][i][1] / 2, ctx->f_code);
                        h->c.last_mv[0][i][0] =
                        h->c.mv[0][i][0]      = mx;
                        h->c.last_mv[0][i][1] = (h->c.mv[0][i][1] = my) * 2;
                    }
                }

                if (HAS_BACKWARD_MV(mb_type)) {
                    h->c.mv_dir |= MV_DIR_BACKWARD;

                    for (i = 0; i < 2; i++) {
                        mx = ff_h263_decode_motion(h, h->c.last_mv[1][i][0], ctx->b_code);
                        my = ff_h263_decode_motion(h, h->c.last_mv[1][i][1] / 2, ctx->b_code);
                        h->c.last_mv[1][i][0] =
                        h->c.mv[1][i][0]      = mx;
                        h->c.last_mv[1][i][1] = (h->c.mv[1][i][1] = my) * 2;
                    }
                }
            }
        }

        if (IS_DIRECT(mb_type)) {
            if (IS_SKIP(mb_type)) {
                mx =
                my = 0;
            } else {
                mx = ff_h263_decode_motion(h, 0, 1);
                my = ff_h263_decode_motion(h, 0, 1);
            }

            h->c.mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
            mb_type  |= ff_mpeg4_set_direct_mv(&h->c, mx, my);
        }
        h->c.cur_pic.mb_type[xy] = mb_type;
    } else { /* I-Frame */
        int use_intra_dc_vlc;

        do {
            cbpc = get_vlc2(&h->gb, ff_h263_intra_MCBPC_vlc, INTRA_MCBPC_VLC_BITS, 2);
            if (cbpc < 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "I cbpc damaged at %d %d\n", h->c.mb_x, h->c.mb_y);
                return AVERROR_INVALIDDATA;
            }
        } while (cbpc == 8);

        dquant = cbpc & 4;
        h->c.mb_intra = 1;

intra:
        h->c.ac_pred = get_bits1(&h->gb);
        if (h->c.ac_pred)
            h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA | MB_TYPE_ACPRED;
        else
            h->c.cur_pic.mb_type[xy] = MB_TYPE_INTRA;

        cbpy = get_vlc2(&h->gb, ff_h263_cbpy_vlc, CBPY_VLC_BITS, 1);
        if (cbpy < 0) {
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "I cbpy damaged at %d %d\n", h->c.mb_x, h->c.mb_y);
            return AVERROR_INVALIDDATA;
        }
        cbp = (cbpc & 3) | (cbpy << 2);

        use_intra_dc_vlc = h->c.qscale < ctx->intra_dc_threshold;

        if (dquant)
            ff_set_qscale(&h->c, h->c.qscale + quant_tab[get_bits(&h->gb, 2)]);

        if (!h->c.progressive_sequence)
            h->c.interlaced_dct = get_bits1(&h->gb);

        h->c.bdsp.clear_blocks(h->block[0]);
        /* decode each block */
        for (i = 0; i < 6; i++) {
            if (mpeg4_decode_block(ctx, h->block[i], i, cbp & 32,
                                   1, use_intra_dc_vlc, 0) < 0)
                return AVERROR_INVALIDDATA;
            cbp += cbp;
        }
        goto end;
    }

    /* decode each block */
    for (i = 0; i < 6; i++) {
        if (mpeg4_decode_block(ctx, h->block[i], i, cbp & 32, 0, 0, 0) < 0)
            return AVERROR_INVALIDDATA;
        cbp += cbp;
    }

end:
    /* per-MB end of slice check */
    next = mpeg4_is_resync(ctx);
    if (next) {
        if (h->c.mb_x + h->c.mb_y*h->c.mb_width + 1 >  next && (h->c.avctx->err_recognition & AV_EF_AGGRESSIVE)) {
            return AVERROR_INVALIDDATA;
        } else if (h->c.mb_x + h->c.mb_y*h->c.mb_width + 1 >= next)
            return SLICE_END;

        if (h->c.pict_type == AV_PICTURE_TYPE_B) {
            const int delta = h->c.mb_x + 1 == h->c.mb_width ? 2 : 1;
            ff_thread_progress_await(&h->c.next_pic.ptr->progress,
                                        (h->c.mb_x + delta >= h->c.mb_width)
                                        ? FFMIN(h->c.mb_y + 1, h->c.mb_height - 1)
                                        : h->c.mb_y);
            if (h->c.next_pic.mbskip_table[xy + delta])
                return SLICE_OK;
        }

        return SLICE_END;
    }

    return SLICE_OK;
}

/* As per spec, studio start code search isn't the same as the old type of start code */
static void next_start_code_studio(GetBitContext *gb)
{
    align_get_bits(gb);

    while (get_bits_left(gb) >= 24 && show_bits(gb, 24) != 0x1) {
        get_bits(gb, 8);
    }
}

/* additional_code, vlc index */
static const uint8_t ac_state_tab[22][2] =
{
    {0, 0},
    {0, 1},
    {1, 1},
    {2, 1},
    {3, 1},
    {4, 1},
    {5, 1},
    {1, 2},
    {2, 2},
    {3, 2},
    {4, 2},
    {5, 2},
    {6, 2},
    {1, 3},
    {2, 4},
    {3, 5},
    {4, 6},
    {5, 7},
    {6, 8},
    {7, 9},
    {8, 10},
    {0, 11}
};

static int mpeg4_decode_studio_block(Mpeg4DecContext *const ctx, int32_t block[64], int n)
{
    H263DecContext *const h = &ctx->h;

    int cc, dct_dc_size, dct_diff, code, j, idx = 1, group = 0, run = 0,
        additional_code_len, sign, mismatch;
    const VLCElem *cur_vlc = studio_intra_tab[0];
    const uint8_t *const scantable = h->c.intra_scantable.permutated;
    const uint16_t *quant_matrix;
    uint32_t flc;
    const int min = -1 *  (1 << (h->c.avctx->bits_per_raw_sample + 6));
    const int max =      ((1 << (h->c.avctx->bits_per_raw_sample + 6)) - 1);
    int shift = 3 - ctx->dct_precision;

    mismatch = 1;

    memset(block, 0, 64 * sizeof(int32_t));

    if (n < 4) {
        cc = 0;
        dct_dc_size = get_vlc2(&h->gb, studio_luma_dc, STUDIO_INTRA_BITS, 2);
        quant_matrix = h->c.intra_matrix;
    } else {
        cc = (n & 1) + 1;
        if (ctx->rgb)
            dct_dc_size = get_vlc2(&h->gb, studio_luma_dc, STUDIO_INTRA_BITS, 2);
        else
            dct_dc_size = get_vlc2(&h->gb, studio_chroma_dc, STUDIO_INTRA_BITS, 2);
        quant_matrix = h->c.chroma_intra_matrix;
    }

    if (dct_dc_size == 0) {
        dct_diff = 0;
    } else {
        dct_diff = get_xbits(&h->gb, dct_dc_size);

        if (dct_dc_size > 8) {
            if(!check_marker(h->c.avctx, &h->gb, "dct_dc_size > 8"))
                return AVERROR_INVALIDDATA;
        }

    }

    h->c.last_dc[cc] += dct_diff;

    if (ctx->mpeg_quant)
        block[0] = h->c.last_dc[cc] * (8 >> h->c.intra_dc_precision);
    else
        block[0] = h->c.last_dc[cc] * (8 >> h->c.intra_dc_precision) * (8 >> ctx->dct_precision);
    /* TODO: support mpeg_quant for AC coefficients */

    block[0] = av_clip(block[0], min, max);
    mismatch ^= block[0];

    /* AC Coefficients */
    while (1) {
        group = get_vlc2(&h->gb, cur_vlc, STUDIO_INTRA_BITS, 2);

        if (group < 0) {
            av_log(h->c.avctx, AV_LOG_ERROR, "illegal ac coefficient group vlc\n");
            return AVERROR_INVALIDDATA;
        }

        additional_code_len = ac_state_tab[group][0];
        cur_vlc = studio_intra_tab[ac_state_tab[group][1]];

        if (group == 0) {
            /* End of Block */
            break;
        } else if (group >= 1 && group <= 6) {
            /* Zero run length (Table B.47) */
            run = 1 << additional_code_len;
            if (additional_code_len)
                run += get_bits(&h->gb, additional_code_len);
            idx += run;
            continue;
        } else if (group >= 7 && group <= 12) {
            /* Zero run length and +/-1 level (Table B.48) */
            code = get_bits(&h->gb, additional_code_len);
            sign = code & 1;
            code >>= 1;
            run = (1 << (additional_code_len - 1)) + code;
            idx += run;
            if (idx > 63)
                return AVERROR_INVALIDDATA;
            j = scantable[idx++];
            block[j] = sign ? 1 : -1;
        } else if (group >= 13 && group <= 20) {
            /* Level value (Table B.49) */
            if (idx > 63)
                return AVERROR_INVALIDDATA;
            j = scantable[idx++];
            block[j] = get_xbits(&h->gb, additional_code_len);
        } else if (group == 21) {
            /* Escape */
            if (idx > 63)
                return AVERROR_INVALIDDATA;
            j = scantable[idx++];
            additional_code_len = h->c.avctx->bits_per_raw_sample + ctx->dct_precision + 4;
            flc = get_bits(&h->gb, additional_code_len);
            if (flc >> (additional_code_len-1))
                block[j] = -1 * (( flc ^ ((1 << additional_code_len) -1)) + 1);
            else
                block[j] = flc;
        }
        block[j] = ((block[j] * quant_matrix[j] * h->c.qscale) * (1 << shift)) / 16;
        block[j] = av_clip(block[j], min, max);
        mismatch ^= block[j];
    }

    block[63] ^= mismatch & 1;

    return 0;
}

static int mpeg4_decode_dpcm_macroblock(Mpeg4DecContext *const ctx,
                                        int16_t macroblock[256], int n)
{
    H263DecContext *const h = &ctx->h;
    int j, w, height, idx = 0;
    int block_mean, rice_parameter, rice_prefix_code, rice_suffix_code,
        dpcm_residual, left, top, topleft, min_left_top, max_left_top, p, p2, output;
    height = 16 >> (n ? h->c.chroma_y_shift : 0);
    w = 16 >> (n ? h->c.chroma_x_shift : 0);

    block_mean = get_bits(&h->gb, h->c.avctx->bits_per_raw_sample);
    if (block_mean == 0){
        av_log(h->c.avctx, AV_LOG_ERROR, "Forbidden block_mean\n");
        return AVERROR_INVALIDDATA;
    }
    h->c.last_dc[n] = block_mean * (1 << (ctx->dct_precision + h->c.intra_dc_precision));

    rice_parameter = get_bits(&h->gb, 4);
    if (rice_parameter == 0) {
        av_log(h->c.avctx, AV_LOG_ERROR, "Forbidden rice_parameter\n");
        return AVERROR_INVALIDDATA;
    }

    if (rice_parameter == 15)
        rice_parameter = 0;

    if (rice_parameter > 11) {
        av_log(h->c.avctx, AV_LOG_ERROR, "Forbidden rice_parameter\n");
        return AVERROR_INVALIDDATA;
    }

    for (int i = 0; i < height; i++) {
        output = 1 << (h->c.avctx->bits_per_raw_sample - 1);
        top = 1 << (h->c.avctx->bits_per_raw_sample - 1);

        for (j = 0; j < w; j++) {
            left = output;
            topleft = top;

            rice_prefix_code = get_unary(&h->gb, 1, 12);

            /* Escape */
            if (rice_prefix_code == 11)
                dpcm_residual = get_bits(&h->gb, h->c.avctx->bits_per_raw_sample);
            else {
                if (rice_prefix_code == 12) {
                    av_log(h->c.avctx, AV_LOG_ERROR, "Forbidden rice_prefix_code\n");
                    return AVERROR_INVALIDDATA;
                }
                rice_suffix_code = get_bitsz(&h->gb, rice_parameter);
                dpcm_residual = (rice_prefix_code << rice_parameter) + rice_suffix_code;
            }

            /* Map to a signed residual */
            if (dpcm_residual & 1)
                dpcm_residual = (-1 * dpcm_residual) >> 1;
            else
                dpcm_residual = (dpcm_residual >> 1);

            if (i != 0)
                top = macroblock[idx-w];

            p = left + top - topleft;
            min_left_top = FFMIN(left, top);
            if (p < min_left_top)
                p = min_left_top;

            max_left_top = FFMAX(left, top);
            if (p > max_left_top)
                p = max_left_top;

            p2 = (FFMIN(min_left_top, topleft) + FFMAX(max_left_top, topleft)) >> 1;
            if (p2 == p)
                p2 = block_mean;

            if (p2 > p)
                dpcm_residual *= -1;

            macroblock[idx++] = output = (dpcm_residual + p) & ((1 << h->c.avctx->bits_per_raw_sample) - 1);
        }
    }

    return 0;
}

static int mpeg4_decode_studio_mb(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);
    int i;

    ctx->dpcm_direction = 0;

    /* StudioMacroblock */
    /* Assumes I-VOP */
    h->c.mb_intra = 1;
    if (get_bits1(&h->gb)) { /* compression_mode */
        /* DCT */
        /* macroblock_type, 1 or 2-bit VLC */
        if (!get_bits1(&h->gb)) {
            skip_bits1(&h->gb);
            h->c.qscale = mpeg_get_qscale(&h->gb, h->c.q_scale_type);
        }

        for (i = 0; i < mpeg4_block_count[h->c.chroma_format]; i++) {
            if (mpeg4_decode_studio_block(ctx, ctx->block32[i], i) < 0)
                return AVERROR_INVALIDDATA;
        }
    } else {
        /* DPCM */
        check_marker(h->c.avctx, &h->gb, "DPCM block start");
        ctx->dpcm_direction = get_bits1(&h->gb) ? -1 : 1;
        for (i = 0; i < 3; i++) {
            if (mpeg4_decode_dpcm_macroblock(ctx, ctx->dpcm_macroblock[i], i) < 0)
                return AVERROR_INVALIDDATA;
        }
    }

    if (get_bits_left(&h->gb) >= 24 && show_bits(&h->gb, 23) == 0) {
        next_start_code_studio(&h->gb);
        return SLICE_END;
    }

    //vcon-stp9L1.bits (first frame)
    if (get_bits_left(&h->gb) == 0)
        return SLICE_END;

    //vcon-stp2L1.bits, vcon-stp3L1.bits, vcon-stp6L1.bits, vcon-stp7L1.bits, vcon-stp8L1.bits, vcon-stp10L1.bits (first frame)
    if (get_bits_left(&h->gb) < 8U && show_bits(&h->gb, get_bits_left(&h->gb)) == 0)
        return SLICE_END;

    return SLICE_OK;
}

static int mpeg4_decode_gop_header(MpegEncContext *s, GetBitContext *gb)
{
    int hours, minutes, seconds;

    if (!show_bits(gb, 23)) {
        av_log(s->avctx, AV_LOG_WARNING, "GOP header invalid\n");
        return AVERROR_INVALIDDATA;
    }

    hours   = get_bits(gb, 5);
    minutes = get_bits(gb, 6);
    check_marker(s->avctx, gb, "in gop_header");
    seconds = get_bits(gb, 6);

    s->time_base = seconds + 60*(minutes + 60*hours);

    skip_bits1(gb);
    skip_bits1(gb);

    return 0;
}

static int mpeg4_decode_profile_level(MpegEncContext *s, GetBitContext *gb, int *profile, int *level)
{

    *profile = get_bits(gb, 4);
    *level   = get_bits(gb, 4);

    // for Simple profile, level 0
    if (*profile == 0 && *level == 8) {
        *level = 0;
    }

    return 0;
}

static int mpeg4_decode_visual_object(MpegEncContext *s, GetBitContext *gb)
{
    int visual_object_type;
    int is_visual_object_identifier = get_bits1(gb);

    if (is_visual_object_identifier) {
        skip_bits(gb, 4+3);
    }
    visual_object_type = get_bits(gb, 4);

    if (visual_object_type == VOT_VIDEO_ID ||
        visual_object_type == VOT_STILL_TEXTURE_ID) {
        int video_signal_type = get_bits1(gb);
        if (video_signal_type) {
            int video_range, color_description;
            skip_bits(gb, 3); // video_format
            video_range = get_bits1(gb);
            color_description = get_bits1(gb);

            s->avctx->color_range = video_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

            if (color_description) {
                s->avctx->color_primaries = get_bits(gb, 8);
                s->avctx->color_trc       = get_bits(gb, 8);
                s->avctx->colorspace      = get_bits(gb, 8);
            }
        }
    }

    return 0;
}

static void mpeg4_load_default_matrices(MpegEncContext *s)
{
    int i, v;

    /* load default matrices */
    for (i = 0; i < 64; i++) {
        int j = s->idsp.idct_permutation[i];
        v = ff_mpeg4_default_intra_matrix[i];
        s->intra_matrix[j]        = v;
        s->chroma_intra_matrix[j] = v;

        v = ff_mpeg4_default_non_intra_matrix[i];
        s->inter_matrix[j]        = v;
        s->chroma_inter_matrix[j] = v;
    }
}

static int read_quant_matrix_ext(MpegEncContext *s, GetBitContext *gb)
{
    int i, j, v;

    if (get_bits1(gb)) {
        if (get_bits_left(gb) < 64*8)
            return AVERROR_INVALIDDATA;
        /* intra_quantiser_matrix */
        for (i = 0; i < 64; i++) {
            v = get_bits(gb, 8);
            j = s->idsp.idct_permutation[ff_zigzag_direct[i]];
            s->intra_matrix[j]        = v;
            s->chroma_intra_matrix[j] = v;
        }
    }

    if (get_bits1(gb)) {
        if (get_bits_left(gb) < 64*8)
            return AVERROR_INVALIDDATA;
        /* non_intra_quantiser_matrix */
        for (i = 0; i < 64; i++) {
            get_bits(gb, 8);
        }
    }

    if (get_bits1(gb)) {
        if (get_bits_left(gb) < 64*8)
            return AVERROR_INVALIDDATA;
        /* chroma_intra_quantiser_matrix */
        for (i = 0; i < 64; i++) {
            v = get_bits(gb, 8);
            j = s->idsp.idct_permutation[ff_zigzag_direct[i]];
            s->chroma_intra_matrix[j] = v;
        }
    }

    if (get_bits1(gb)) {
        if (get_bits_left(gb) < 64*8)
            return AVERROR_INVALIDDATA;
        /* chroma_non_intra_quantiser_matrix */
        for (i = 0; i < 64; i++) {
            get_bits(gb, 8);
        }
    }

    next_start_code_studio(gb);
    return 0;
}

static void extension_and_user_data(MpegEncContext *s, GetBitContext *gb, int id)
{
    uint32_t startcode;
    uint8_t extension_type;

    startcode = show_bits_long(gb, 32);
    if (startcode == USER_DATA_STARTCODE || startcode == EXT_STARTCODE) {

        if ((id == 2 || id == 4) && startcode == EXT_STARTCODE) {
            skip_bits_long(gb, 32);
            extension_type = get_bits(gb, 4);
            if (extension_type == QUANT_MATRIX_EXT_ID)
                read_quant_matrix_ext(s, gb);
        }
    }
}

static int decode_studio_vol_header(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    MPVContext *const s = &ctx->h.c;
    int width, height, aspect_ratio_info;
    int bits_per_raw_sample;
    int rgb, chroma_format;

    // random_accessible_vol and video_object_type_indication have already
    // been read by the caller decode_vol_header()
    skip_bits(gb, 4); /* video_object_layer_verid */
    ctx->shape = get_bits(gb, 2); /* video_object_layer_shape */
    skip_bits(gb, 4); /* video_object_layer_shape_extension */
    skip_bits1(gb); /* progressive_sequence */
    if (ctx->shape != RECT_SHAPE) {
        avpriv_request_sample(s->avctx, "MPEG-4 Studio profile non rectangular shape");
        return AVERROR_PATCHWELCOME;
    }
    if (ctx->shape != BIN_ONLY_SHAPE) {
        rgb = get_bits1(gb); /* rgb_components */
        chroma_format = get_bits(gb, 2); /* chroma_format */
        if (!chroma_format || chroma_format == CHROMA_420 || (rgb && chroma_format == CHROMA_422)) {
            av_log(s->avctx, AV_LOG_ERROR, "illegal chroma format\n");
            return AVERROR_INVALIDDATA;
        }

        bits_per_raw_sample = get_bits(gb, 4); /* bit_depth */
        if (bits_per_raw_sample == 10) {
            if (rgb) {
                s->avctx->pix_fmt = AV_PIX_FMT_GBRP10;
            } else {
                s->avctx->pix_fmt = chroma_format == CHROMA_422 ? AV_PIX_FMT_YUV422P10 : AV_PIX_FMT_YUV444P10;
            }
        } else {
            avpriv_request_sample(s->avctx, "MPEG-4 Studio profile bit-depth %u", bits_per_raw_sample);
            return AVERROR_PATCHWELCOME;
        }
        if (rgb != ctx->rgb || s->chroma_format != chroma_format)
            s->context_reinit = 1;
        s->avctx->bits_per_raw_sample = bits_per_raw_sample;
        ctx->rgb = rgb;
        s->chroma_format = chroma_format;
    }
    if (ctx->shape == RECT_SHAPE) {
        check_marker(s->avctx, gb, "before video_object_layer_width");
        width  = get_bits(gb, 14); /* video_object_layer_width */
        check_marker(s->avctx, gb, "before video_object_layer_height");
        height = get_bits(gb, 14); /* video_object_layer_height */
        check_marker(s->avctx, gb, "after video_object_layer_height");

        /* Do the same check as non-studio profile */
        if (width && height) {
            if (s->width && s->height &&
                (s->width != width || s->height != height))
                s->context_reinit = 1;
            s->width  = width;
            s->height = height;
        }
    }
    aspect_ratio_info = get_bits(gb, 4);
    if (aspect_ratio_info == FF_ASPECT_EXTENDED) {
        s->avctx->sample_aspect_ratio.num = get_bits(gb, 8);  // par_width
        s->avctx->sample_aspect_ratio.den = get_bits(gb, 8);  // par_height
    } else {
        s->avctx->sample_aspect_ratio = ff_h263_pixel_aspect[aspect_ratio_info];
    }
    skip_bits(gb, 4); /* frame_rate_code */
    skip_bits(gb, 15); /* first_half_bit_rate */
    check_marker(s->avctx, gb, "after first_half_bit_rate");
    skip_bits(gb, 15); /* latter_half_bit_rate */
    check_marker(s->avctx, gb, "after latter_half_bit_rate");
    skip_bits(gb, 15); /* first_half_vbv_buffer_size */
    check_marker(s->avctx, gb, "after first_half_vbv_buffer_size");
    skip_bits(gb, 3); /* latter_half_vbv_buffer_size */
    skip_bits(gb, 11); /* first_half_vbv_buffer_size */
    check_marker(s->avctx, gb, "after first_half_vbv_buffer_size");
    skip_bits(gb, 15); /* latter_half_vbv_occupancy */
    check_marker(s->avctx, gb, "after latter_half_vbv_occupancy");
    s->low_delay  = get_bits1(gb);
    ctx->mpeg_quant = get_bits1(gb); /* mpeg2_stream */

    next_start_code_studio(gb);
    extension_and_user_data(s, gb, 2);

    return 0;
}

static int decode_vol_header(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    H263DecContext *const h = &ctx->h;
    int width, height, vo_ver_id, aspect_ratio_info;

    /* vol header */
    skip_bits(gb, 1);                   /* random access */
    ctx->vo_type = get_bits(gb, 8);

    /* If we are in studio profile (per vo_type), check if its all consistent
     * and if so continue pass control to decode_studio_vol_header().
     * elIf something is inconsistent, error out
     * else continue with (non studio) vol header decpoding.
     */
    if (ctx->vo_type == CORE_STUDIO_VO_TYPE ||
        ctx->vo_type == SIMPLE_STUDIO_VO_TYPE) {
        if (h->c.avctx->profile != AV_PROFILE_UNKNOWN && h->c.avctx->profile != AV_PROFILE_MPEG4_SIMPLE_STUDIO)
            return AVERROR_INVALIDDATA;
        h->c.studio_profile = 1;
        h->c.avctx->profile = AV_PROFILE_MPEG4_SIMPLE_STUDIO;
        return decode_studio_vol_header(ctx, gb);
    } else if (h->c.studio_profile) {
        return AVERROR_PATCHWELCOME;
    }

    if (get_bits1(gb) != 0) {           /* is_ol_id */
        vo_ver_id = get_bits(gb, 4);    /* vo_ver_id */
        skip_bits(gb, 3);               /* vo_priority */
    } else {
        vo_ver_id = 1;
    }
    aspect_ratio_info = get_bits(gb, 4);
    if (aspect_ratio_info == FF_ASPECT_EXTENDED) {
        h->c.avctx->sample_aspect_ratio.num = get_bits(gb, 8);  // par_width
        h->c.avctx->sample_aspect_ratio.den = get_bits(gb, 8);  // par_height
    } else {
        h->c.avctx->sample_aspect_ratio = ff_h263_pixel_aspect[aspect_ratio_info];
    }

    if ((ctx->vol_control_parameters = get_bits1(gb))) { /* vol control parameter */
        int chroma_format = get_bits(gb, 2);
        if (chroma_format != CHROMA_420)
            av_log(h->c.avctx, AV_LOG_ERROR, "illegal chroma format\n");

        h->c.low_delay = get_bits1(gb);
        if (get_bits1(gb)) {    /* vbv parameters */
            get_bits(gb, 15);   /* first_half_bitrate */
            check_marker(h->c.avctx, gb, "after first_half_bitrate");
            get_bits(gb, 15);   /* latter_half_bitrate */
            check_marker(h->c.avctx, gb, "after latter_half_bitrate");
            get_bits(gb, 15);   /* first_half_vbv_buffer_size */
            check_marker(h->c.avctx, gb, "after first_half_vbv_buffer_size");
            get_bits(gb, 3);    /* latter_half_vbv_buffer_size */
            get_bits(gb, 11);   /* first_half_vbv_occupancy */
            check_marker(h->c.avctx, gb, "after first_half_vbv_occupancy");
            get_bits(gb, 15);   /* latter_half_vbv_occupancy */
            check_marker(h->c.avctx, gb, "after latter_half_vbv_occupancy");
        }
    } else {
        /* is setting low delay flag only once the smartest thing to do?
         * low delay detection will not be overridden. */
        if (h->picture_number == 0) {
            switch (ctx->vo_type) {
            case SIMPLE_VO_TYPE:
            case ADV_SIMPLE_VO_TYPE:
                h->c.low_delay = 1;
                break;
            default:
                h->c.low_delay = 0;
            }
        }
    }

    ctx->shape = get_bits(gb, 2); /* vol shape */
    if (ctx->shape != RECT_SHAPE)
        av_log(h->c.avctx, AV_LOG_ERROR, "only rectangular vol supported\n");
    if (ctx->shape == GRAY_SHAPE && vo_ver_id != 1) {
        av_log(h->c.avctx, AV_LOG_ERROR, "Gray shape not supported\n");
        skip_bits(gb, 4);  /* video_object_layer_shape_extension */
    }

    check_marker(h->c.avctx, gb, "before time_increment_resolution");

    h->c.avctx->framerate.num = get_bits(gb, 16);
    if (!h->c.avctx->framerate.num) {
        av_log(h->c.avctx, AV_LOG_ERROR, "framerate==0\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->time_increment_bits = av_log2(h->c.avctx->framerate.num - 1) + 1;
    if (ctx->time_increment_bits < 1)
        ctx->time_increment_bits = 1;

    check_marker(h->c.avctx, gb, "before fixed_vop_rate");

    if (get_bits1(gb) != 0)     /* fixed_vop_rate  */
        h->c.avctx->framerate.den = get_bits(gb, ctx->time_increment_bits);
    else
        h->c.avctx->framerate.den = 1;

    ctx->t_frame = 0;

    if (ctx->shape != BIN_ONLY_SHAPE) {
        if (ctx->shape == RECT_SHAPE) {
            check_marker(h->c.avctx, gb, "before width");
            width = get_bits(gb, 13);
            check_marker(h->c.avctx, gb, "before height");
            height = get_bits(gb, 13);
            check_marker(h->c.avctx, gb, "after height");
            if (width && height &&  /* they should be non zero but who knows */
                !(h->c.width && h->c.codec_tag == AV_RL32("MP4S"))) {
                if (h->c.width && h->c.height &&
                    (h->c.width != width || h->c.height != height))
                    h->c.context_reinit = 1;
                h->c.width  = width;
                h->c.height = height;
            }
        }

        h->c.progressive_sequence  =
        h->c.progressive_frame     = get_bits1(gb) ^ 1;
        h->c.interlaced_dct        = 0;
        if (!get_bits1(gb) && (h->c.avctx->debug & FF_DEBUG_PICT_INFO))
            av_log(h->c.avctx, AV_LOG_INFO,           /* OBMC Disable */
                   "MPEG-4 OBMC not supported (very likely buggy encoder)\n");
        if (vo_ver_id == 1)
            ctx->vol_sprite_usage = get_bits1(gb);    /* vol_sprite_usage */
        else
            ctx->vol_sprite_usage = get_bits(gb, 2);  /* vol_sprite_usage */

        if (ctx->vol_sprite_usage == STATIC_SPRITE)
            av_log(h->c.avctx, AV_LOG_ERROR, "Static Sprites not supported\n");
        if (ctx->vol_sprite_usage == STATIC_SPRITE ||
            ctx->vol_sprite_usage == GMC_SPRITE) {
            if (ctx->vol_sprite_usage == STATIC_SPRITE) {
                skip_bits(gb, 13); // sprite_width
                check_marker(h->c.avctx, gb, "after sprite_width");
                skip_bits(gb, 13); // sprite_height
                check_marker(h->c.avctx, gb, "after sprite_height");
                skip_bits(gb, 13); // sprite_left
                check_marker(h->c.avctx, gb, "after sprite_left");
                skip_bits(gb, 13); // sprite_top
                check_marker(h->c.avctx, gb, "after sprite_top");
            }
            ctx->num_sprite_warping_points = get_bits(gb, 6);
            if (ctx->num_sprite_warping_points > 3) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "%d sprite_warping_points\n",
                       ctx->num_sprite_warping_points);
                ctx->num_sprite_warping_points = 0;
                return AVERROR_INVALIDDATA;
            }
            ctx->sprite_warping_accuracy  = get_bits(gb, 2);
            ctx->sprite_brightness_change = get_bits1(gb);
            if (ctx->vol_sprite_usage == STATIC_SPRITE)
                skip_bits1(gb); // low_latency_sprite
        }
        // FIXME sadct disable bit if verid!=1 && shape not rect

        if (get_bits1(gb) == 1) {                   /* not_8_bit */
            ctx->quant_precision = get_bits(gb, 4); /* quant_precision */
            if (get_bits(gb, 4) != 8)               /* bits_per_pixel */
                av_log(h->c.avctx, AV_LOG_ERROR, "N-bit not supported\n");
            if (ctx->quant_precision != 5)
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "quant precision %d\n", ctx->quant_precision);
            if (ctx->quant_precision < 3 || ctx->quant_precision > 9)
                ctx->quant_precision = 5;
        } else {
            ctx->quant_precision = 5;
        }

        // FIXME a bunch of grayscale shape things

        if ((ctx->mpeg_quant = get_bits1(gb))) { /* vol_quant_type */
            int i, v;

            mpeg4_load_default_matrices(&h->c);

            /* load custom intra matrix */
            if (get_bits1(gb)) {
                int last = 0;
                for (i = 0; i < 64; i++) {
                    int j;
                    if (get_bits_left(gb) < 8) {
                        av_log(h->c.avctx, AV_LOG_ERROR, "insufficient data for custom matrix\n");
                        return AVERROR_INVALIDDATA;
                    }
                    v = get_bits(gb, 8);
                    if (v == 0)
                        break;

                    last = v;
                    j = h->c.idsp.idct_permutation[ff_zigzag_direct[i]];
                    h->c.intra_matrix[j]        = last;
                }

                /* replicate last value */
                for (; i < 64; i++) {
                    int j = h->c.idsp.idct_permutation[ff_zigzag_direct[i]];
                    h->c.intra_matrix[j]        = last;
                }
            }

            /* load custom non intra matrix */
            if (get_bits1(gb)) {
                int last = 0;
                for (i = 0; i < 64; i++) {
                    int j;
                    if (get_bits_left(gb) < 8) {
                        av_log(h->c.avctx, AV_LOG_ERROR, "insufficient data for custom matrix\n");
                        return AVERROR_INVALIDDATA;
                    }
                    v = get_bits(gb, 8);
                    if (v == 0)
                        break;

                    last = v;
                    j = h->c.idsp.idct_permutation[ff_zigzag_direct[i]];
                    h->c.inter_matrix[j]        = v;
                }

                /* replicate last value */
                for (; i < 64; i++) {
                    int j = h->c.idsp.idct_permutation[ff_zigzag_direct[i]];
                    h->c.inter_matrix[j]        = last;
                }
            }

            // FIXME a bunch of grayscale shape things
        }

        if (vo_ver_id != 1)
            h->c.quarter_sample = get_bits1(gb);
        else
            h->c.quarter_sample = 0;

        if (get_bits_left(gb) < 4) {
            av_log(h->c.avctx, AV_LOG_ERROR, "VOL Header truncated\n");
            return AVERROR_INVALIDDATA;
        }

        if (!get_bits1(gb)) {
            int pos               = get_bits_count(gb);
            int estimation_method = get_bits(gb, 2);
            if (estimation_method < 2) {
                if (!get_bits1(gb)) {
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* opaque */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* transparent */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* intra_cae */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* inter_cae */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* no_update */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* upsampling */
                }
                if (!get_bits1(gb)) {
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* intra_blocks */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* inter_blocks */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* inter4v_blocks */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* not coded blocks */
                }
                if (!check_marker(h->c.avctx, gb, "in complexity estimation part 1")) {
                    skip_bits_long(gb, pos - get_bits_count(gb));
                    goto no_cplx_est;
                }
                if (!get_bits1(gb)) {
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* dct_coeffs */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* dct_lines */
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* vlc_syms */
                    ctx->cplx_estimation_trash_i += 4 * get_bits1(gb);  /* vlc_bits */
                }
                if (!get_bits1(gb)) {
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* apm */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* npm */
                    ctx->cplx_estimation_trash_b += 8 * get_bits1(gb);  /* interpolate_mc_q */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* forwback_mc_q */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* halfpel2 */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* halfpel4 */
                }
                if (!check_marker(h->c.avctx, gb, "in complexity estimation part 2")) {
                    skip_bits_long(gb, pos - get_bits_count(gb));
                    goto no_cplx_est;
                }
                if (estimation_method == 1) {
                    ctx->cplx_estimation_trash_i += 8 * get_bits1(gb);  /* sadct */
                    ctx->cplx_estimation_trash_p += 8 * get_bits1(gb);  /* qpel */
                }
            } else
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "Invalid Complexity estimation method %d\n",
                       estimation_method);
        } else {

no_cplx_est:
            ctx->cplx_estimation_trash_i =
            ctx->cplx_estimation_trash_p =
            ctx->cplx_estimation_trash_b = 0;
        }

        ctx->resync_marker = !get_bits1(gb); /* resync_marker_disabled */

        h->data_partitioning = get_bits1(gb);
        if (h->data_partitioning)
            ctx->rvlc = get_bits1(gb);

        if (vo_ver_id != 1) {
            ctx->new_pred = get_bits1(gb);
            if (ctx->new_pred) {
                av_log(h->c.avctx, AV_LOG_ERROR, "new pred not supported\n");
                skip_bits(gb, 2); /* requested upstream message type */
                skip_bits1(gb);   /* newpred segment type */
            }
            if (get_bits1(gb)) // reduced_res_vop
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "reduced resolution VOP not supported\n");
        } else {
            ctx->new_pred = 0;
        }

        ctx->scalability = get_bits1(gb);

        if (ctx->scalability) {
            GetBitContext bak = *gb;
            int h_sampling_factor_n;
            int h_sampling_factor_m;
            int v_sampling_factor_n;
            int v_sampling_factor_m;

            skip_bits1(gb);    // hierarchy_type
            skip_bits(gb, 4);  /* ref_layer_id */
            skip_bits1(gb);    /* ref_layer_sampling_dir */
            h_sampling_factor_n = get_bits(gb, 5);
            h_sampling_factor_m = get_bits(gb, 5);
            v_sampling_factor_n = get_bits(gb, 5);
            v_sampling_factor_m = get_bits(gb, 5);
            ctx->enhancement_type = get_bits1(gb);

            if (h_sampling_factor_n == 0 || h_sampling_factor_m == 0 ||
                v_sampling_factor_n == 0 || v_sampling_factor_m == 0) {
                /* illegal scalability header (VERY broken encoder),
                 * trying to workaround */
                ctx->scalability = 0;
                *gb            = bak;
            } else
                av_log(h->c.avctx, AV_LOG_ERROR, "scalability not supported\n");

            // bin shape stuff FIXME
        }
    }

    if (h->c.avctx->debug&FF_DEBUG_PICT_INFO) {
        av_log(h->c.avctx, AV_LOG_DEBUG, "tb %d/%d, tincrbits:%d, qp_prec:%d, ps:%d, low_delay:%d  %s%s%s%s\n",
               h->c.avctx->framerate.den, h->c.avctx->framerate.num,
               ctx->time_increment_bits,
               ctx->quant_precision,
               h->c.progressive_sequence,
               h->c.low_delay,
               ctx->scalability ? "scalability " :"" ,
               h->c.quarter_sample ? "qpel " : "",
               h->data_partitioning ? "partition " : "",
               ctx->rvlc ? "rvlc " : ""
        );
    }

    return 0;
}

/**
 * Decode the user data stuff in the header.
 * Also initializes divx/xvid/lavc_version/build.
 */
static int decode_user_data(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    H263DecContext *const h = &ctx->h;
    char buf[256];
    int i;
    int e;
    int ver = 0, build = 0, ver2 = 0, ver3 = 0;
    char last;

    for (i = 0; i < 255 && get_bits_count(gb) < gb->size_in_bits; i++) {
        if (show_bits(gb, 23) == 0)
            break;
        buf[i] = get_bits(gb, 8);
    }
    buf[i] = 0;

    /* divx detection */
    e = sscanf(buf, "DivX%dBuild%d%c", &ver, &build, &last);
    if (e < 2)
        e = sscanf(buf, "DivX%db%d%c", &ver, &build, &last);
    if (e >= 2) {
        ctx->divx_version = ver;
        ctx->divx_build   = build;
        h->divx_packed  = e == 3 && last == 'p';
    }

    /* libavcodec detection */
    e = sscanf(buf, "FFmpe%*[^b]b%d", &build) + 3;
    if (e != 4)
        e = sscanf(buf, "FFmpeg v%d.%d.%d / libavcodec build: %d", &ver, &ver2, &ver3, &build);
    if (e != 4) {
        e = sscanf(buf, "Lavc%d.%d.%d", &ver, &ver2, &ver3) + 1;
        if (e > 1) {
            if (ver > 0xFFU || ver2 > 0xFFU || ver3 > 0xFFU) {
                av_log(h->c.avctx, AV_LOG_WARNING,
                     "Unknown Lavc version string encountered, %d.%d.%d; "
                     "clamping sub-version values to 8-bits.\n",
                     ver, ver2, ver3);
            }
            build = ((ver & 0xFF) << 16) + ((ver2 & 0xFF) << 8) + (ver3 & 0xFF);
        }
    }
    if (e != 4) {
        if (strcmp(buf, "ffmpeg") == 0)
            ctx->lavc_build = 4600;
    }
    if (e == 4)
        ctx->lavc_build = build;

    /* Xvid detection */
    e = sscanf(buf, "XviD%d", &build);
    if (e == 1)
        ctx->xvid_build = build;

    return 0;
}

static av_cold void permute_quant_matrix(uint16_t matrix[64],
                                         const uint8_t new_perm[64],
                                         const uint8_t old_perm[64])
{
    uint16_t tmp[64];

    memcpy(tmp, matrix, sizeof(tmp));
    for (int i = 0; i < 64; ++i)
        matrix[new_perm[i]] = tmp[old_perm[i]];
}

static av_cold void switch_to_xvid_idct(AVCodecContext *const avctx,
                                        MpegEncContext *const s)
{
    uint8_t old_permutation[64];

    memcpy(old_permutation, s->idsp.idct_permutation, sizeof(old_permutation));

    avctx->idct_algo = FF_IDCT_XVID;
    ff_mpv_idct_init(s);
    ff_permute_scantable(s->permutated_intra_h_scantable,
                         s->alternate_scan ? ff_alternate_vertical_scan : ff_alternate_horizontal_scan,
                         s->idsp.idct_permutation);

    // Normal (i.e. non-studio) MPEG-4 does not use the chroma matrices.
    permute_quant_matrix(s->inter_matrix, s->idsp.idct_permutation, old_permutation);
    permute_quant_matrix(s->intra_matrix, s->idsp.idct_permutation, old_permutation);
}

void ff_mpeg4_workaround_bugs(AVCodecContext *avctx)
{
    Mpeg4DecContext *ctx = avctx->priv_data;
    H263DecContext *const h = &ctx->h;

    if (ctx->xvid_build == -1 && ctx->divx_version == -1 && ctx->lavc_build == -1) {
        if (h->c.codec_tag == AV_RL32("XVID") ||
            h->c.codec_tag == AV_RL32("XVIX") ||
            h->c.codec_tag == AV_RL32("RMP4") ||
            h->c.codec_tag == AV_RL32("ZMP4") ||
            h->c.codec_tag == AV_RL32("SIPP"))
            ctx->xvid_build = 0;
    }

    if (ctx->xvid_build == -1 && ctx->divx_version == -1 && ctx->lavc_build == -1)
        if (h->c.codec_tag == AV_RL32("DIVX") && ctx->vo_type == 0 &&
            ctx->vol_control_parameters == 0)
            ctx->divx_version = 400;  // divx 4

    if (ctx->xvid_build >= 0 && ctx->divx_version >= 0) {
        ctx->divx_version =
        ctx->divx_build   = -1;
    }

    if (h->c.workaround_bugs & FF_BUG_AUTODETECT) {
        if (h->c.codec_tag == AV_RL32("XVIX"))
            h->c.workaround_bugs |= FF_BUG_XVID_ILACE;

        if (h->c.codec_tag == AV_RL32("UMP4"))
            h->c.workaround_bugs |= FF_BUG_UMP4;

        if (ctx->divx_version >= 500 && ctx->divx_build < 1814)
            h->c.workaround_bugs |= FF_BUG_QPEL_CHROMA;

        if (ctx->divx_version > 502 && ctx->divx_build < 1814)
            h->c.workaround_bugs |= FF_BUG_QPEL_CHROMA2;

        if (ctx->xvid_build <= 3U)
            h->padding_bug_score = 256 * 256 * 256 * 64;

        if (ctx->xvid_build <= 1U)
            h->c.workaround_bugs |= FF_BUG_QPEL_CHROMA;

        if (ctx->xvid_build <= 12U)
            h->c.workaround_bugs |= FF_BUG_EDGE;

        if (ctx->xvid_build <= 32U)
            h->c.workaround_bugs |= FF_BUG_DC_CLIP;

#define SET_QPEL_FUNC(postfix1, postfix2)                           \
    h->c.qdsp.put_        ## postfix1 = ff_put_        ## postfix2; \
    h->c.qdsp.put_no_rnd_ ## postfix1 = ff_put_no_rnd_ ## postfix2; \
    h->c.qdsp.avg_        ## postfix1 = ff_avg_        ## postfix2;

        if (ctx->lavc_build < 4653U)
            h->c.workaround_bugs |= FF_BUG_STD_QPEL;

        if (ctx->lavc_build < 4655U)
            h->c.workaround_bugs |= FF_BUG_DIRECT_BLOCKSIZE;

        if (ctx->lavc_build < 4670U)
            h->c.workaround_bugs |= FF_BUG_EDGE;

        if (ctx->lavc_build <= 4712U)
            h->c.workaround_bugs |= FF_BUG_DC_CLIP;

        if ((ctx->lavc_build&0xFF) >= 100) {
            if (ctx->lavc_build > 3621476 && ctx->lavc_build < 3752552 &&
               (ctx->lavc_build < 3752037 || ctx->lavc_build > 3752191) // 3.2.1+
            )
                h->c.workaround_bugs |= FF_BUG_IEDGE;
        }

        if (ctx->divx_version >= 0)
            h->c.workaround_bugs |= FF_BUG_DIRECT_BLOCKSIZE;
        if (ctx->divx_version == 501 && ctx->divx_build == 20020416)
            h->padding_bug_score = 256 * 256 * 256 * 64;

        if (ctx->divx_version < 500U)
            h->c.workaround_bugs |= FF_BUG_EDGE;

        if (ctx->divx_version >= 0)
            h->c.workaround_bugs |= FF_BUG_HPEL_CHROMA;
    }

    if (h->c.workaround_bugs & FF_BUG_STD_QPEL) {
        SET_QPEL_FUNC(qpel_pixels_tab[0][5], qpel16_mc11_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][7], qpel16_mc31_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][9], qpel16_mc12_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][11], qpel16_mc32_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][13], qpel16_mc13_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[0][15], qpel16_mc33_old_c)

        SET_QPEL_FUNC(qpel_pixels_tab[1][5], qpel8_mc11_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][7], qpel8_mc31_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][9], qpel8_mc12_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][11], qpel8_mc32_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][13], qpel8_mc13_old_c)
        SET_QPEL_FUNC(qpel_pixels_tab[1][15], qpel8_mc33_old_c)
    }

    if (avctx->debug & FF_DEBUG_BUGS)
        av_log(h->c.avctx, AV_LOG_DEBUG,
               "bugs: %X lavc_build:%d xvid_build:%d divx_version:%d divx_build:%d %s\n",
               h->c.workaround_bugs, ctx->lavc_build, ctx->xvid_build,
               ctx->divx_version, ctx->divx_build, h->divx_packed ? "p" : "");

    if (CONFIG_MPEG4_DECODER && ctx->xvid_build >= 0 &&
        avctx->idct_algo == FF_IDCT_AUTO && !h->c.studio_profile) {
        switch_to_xvid_idct(avctx, &h->c);
    }
}

static int decode_vop_header(Mpeg4DecContext *ctx, GetBitContext *gb,
                             int parse_only)
{
    H263DecContext *const h = &ctx->h;
    int time_incr, time_increment;
    int64_t pts;

    h->c.mcsel     = 0;
    h->c.pict_type = get_bits(gb, 2) + AV_PICTURE_TYPE_I;        /* pict type: I = 0 , P = 1 */
    if (h->c.pict_type == AV_PICTURE_TYPE_B && h->c.low_delay &&
        ctx->vol_control_parameters == 0 && !(h->c.avctx->flags & AV_CODEC_FLAG_LOW_DELAY)) {
        av_log(h->c.avctx, AV_LOG_ERROR, "low_delay flag set incorrectly, clearing it\n");
        h->c.low_delay = 0;
    }

    h->partitioned_frame = h->data_partitioning && h->c.pict_type != AV_PICTURE_TYPE_B;
    if (h->partitioned_frame)
        h->decode_mb = mpeg4_decode_partitioned_mb;
    else
        h->decode_mb = mpeg4_decode_mb;

    time_incr = 0;
    while (get_bits1(gb) != 0)
        time_incr++;

    check_marker(h->c.avctx, gb, "before time_increment");

    if (ctx->time_increment_bits == 0 ||
        !(show_bits(gb, ctx->time_increment_bits + 1) & 1)) {
        av_log(h->c.avctx, AV_LOG_WARNING,
               "time_increment_bits %d is invalid in relation to the current bitstream, this is likely caused by a missing VOL header\n", ctx->time_increment_bits);

        for (ctx->time_increment_bits = 1;
             ctx->time_increment_bits < 16;
             ctx->time_increment_bits++) {
            if (h->c.pict_type == AV_PICTURE_TYPE_P ||
                (h->c.pict_type == AV_PICTURE_TYPE_S &&
                 ctx->vol_sprite_usage == GMC_SPRITE)) {
                if ((show_bits(gb, ctx->time_increment_bits + 6) & 0x37) == 0x30)
                    break;
            } else if ((show_bits(gb, ctx->time_increment_bits + 5) & 0x1F) == 0x18)
                break;
        }

        av_log(h->c.avctx, AV_LOG_WARNING,
               "time_increment_bits set to %d bits, based on bitstream analysis\n", ctx->time_increment_bits);
    }

    if (IS_3IV1)
        time_increment = get_bits1(gb);        // FIXME investigate further
    else
        time_increment = get_bits(gb, ctx->time_increment_bits);

    if (h->c.pict_type != AV_PICTURE_TYPE_B) {
        h->c.last_time_base = h->c.time_base;
        h->c.time_base     += time_incr;
        h->c.time = h->c.time_base * (int64_t)h->c.avctx->framerate.num + time_increment;
        if (h->c.workaround_bugs & FF_BUG_UMP4) {
            if (h->c.time < h->c.last_non_b_time) {
                /* header is not mpeg-4-compatible, broken encoder,
                 * trying to workaround */
                h->c.time_base++;
                h->c.time += h->c.avctx->framerate.num;
            }
        }
        h->c.pp_time         = h->c.time - h->c.last_non_b_time;
        h->c.last_non_b_time = h->c.time;
    } else {
        h->c.time    = (h->c.last_time_base + time_incr) * (int64_t)h->c.avctx->framerate.num + time_increment;
        h->c.pb_time = h->c.pp_time - (h->c.last_non_b_time - h->c.time);
        if (h->c.pp_time <= h->c.pb_time ||
            h->c.pp_time <= h->c.pp_time - h->c.pb_time ||
            h->c.pp_time <= 0) {
            /* messed up order, maybe after seeking? skipping current B-frame */
            return FRAME_SKIPPED;
        }
        ff_mpeg4_init_direct_mv(&h->c);

        if (ctx->t_frame == 0)
            ctx->t_frame = h->c.pb_time;
        if (ctx->t_frame == 0)
            ctx->t_frame = 1;  // 1/0 protection
        h->c.pp_field_time = (ROUNDED_DIV(h->c.last_non_b_time, ctx->t_frame) -
                              ROUNDED_DIV(h->c.last_non_b_time - h->c.pp_time, ctx->t_frame)) * 2;
        h->c.pb_field_time = (ROUNDED_DIV(h->c.time, ctx->t_frame) -
                              ROUNDED_DIV(h->c.last_non_b_time - h->c.pp_time, ctx->t_frame)) * 2;
        if (h->c.pp_field_time <= h->c.pb_field_time || h->c.pb_field_time <= 1) {
            h->c.pb_field_time = 2;
            h->c.pp_field_time = 4;
            if (!h->c.progressive_sequence)
                return FRAME_SKIPPED;
        }
    }

    if (h->c.avctx->framerate.den)
        pts = ROUNDED_DIV(h->c.time, h->c.avctx->framerate.den);
    else
        pts = AV_NOPTS_VALUE;
    ff_dlog(h->c.avctx, "MPEG4 PTS: %"PRId64"\n", pts);

    check_marker(h->c.avctx, gb, "before vop_coded");

    /* vop coded */
    if (get_bits1(gb) != 1) {
        if (h->c.avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(h->c.avctx, AV_LOG_ERROR, "vop not coded\n");
        h->skipped_last_frame = 1;
        return FRAME_SKIPPED;
    }
    if (ctx->new_pred)
        decode_new_pred(ctx, gb);

    if (ctx->shape != BIN_ONLY_SHAPE &&
                    (h->c.pict_type == AV_PICTURE_TYPE_P ||
                     (h->c.pict_type == AV_PICTURE_TYPE_S &&
                      ctx->vol_sprite_usage == GMC_SPRITE))) {
        /* rounding type for motion estimation */
        h->c.no_rounding = get_bits1(gb);
    } else {
        h->c.no_rounding = 0;
    }
    // FIXME reduced res stuff

    if (ctx->shape != RECT_SHAPE) {
        if (ctx->vol_sprite_usage != 1 || h->c.pict_type != AV_PICTURE_TYPE_I) {
            skip_bits(gb, 13);  /* width */
            check_marker(h->c.avctx, gb, "after width");
            skip_bits(gb, 13);  /* height */
            check_marker(h->c.avctx, gb, "after height");
            skip_bits(gb, 13);  /* hor_spat_ref */
            check_marker(h->c.avctx, gb, "after hor_spat_ref");
            skip_bits(gb, 13);  /* ver_spat_ref */
        }
        skip_bits1(gb);         /* change_CR_disable */

        if (get_bits1(gb) != 0)
            skip_bits(gb, 8);   /* constant_alpha_value */
    }

    // FIXME complexity estimation stuff

    if (ctx->shape != BIN_ONLY_SHAPE) {
        skip_bits_long(gb, ctx->cplx_estimation_trash_i);
        if (h->c.pict_type != AV_PICTURE_TYPE_I)
            skip_bits_long(gb, ctx->cplx_estimation_trash_p);
        if (h->c.pict_type == AV_PICTURE_TYPE_B)
            skip_bits_long(gb, ctx->cplx_estimation_trash_b);

        if (get_bits_left(gb) < 3) {
            av_log(h->c.avctx, AV_LOG_ERROR, "Header truncated\n");
            return AVERROR_INVALIDDATA;
        }
        ctx->intra_dc_threshold = ff_mpeg4_dc_threshold[get_bits(gb, 3)];
        if (!h->c.progressive_sequence) {
            h->c.top_field_first = get_bits1(gb);
            h->c.alternate_scan  = get_bits1(gb);
        } else
            h->c.alternate_scan = 0;
    }
    /* Skip at this point when only parsing since the remaining
     * data is not useful for a parser and requires the
     * sprite_trajectory VLC to be initialized. */
    if (parse_only)
        goto end;

    if (h->c.alternate_scan) {
        ff_init_scantable(h->c.idsp.idct_permutation, &h->c.intra_scantable,   ff_alternate_vertical_scan);
        ff_permute_scantable(h->c.permutated_intra_h_scantable, ff_alternate_vertical_scan,
                             h->c.idsp.idct_permutation);
    } else {
        ff_init_scantable(h->c.idsp.idct_permutation, &h->c.intra_scantable,   ff_zigzag_direct);
        ff_permute_scantable(h->c.permutated_intra_h_scantable, ff_alternate_horizontal_scan,
                             h->c.idsp.idct_permutation);
    }
    ff_permute_scantable(h->c.permutated_intra_v_scantable, ff_alternate_vertical_scan,
                         h->c.idsp.idct_permutation);

    if (h->c.pict_type == AV_PICTURE_TYPE_S) {
        if((ctx->vol_sprite_usage == STATIC_SPRITE ||
            ctx->vol_sprite_usage == GMC_SPRITE)) {
            if (mpeg4_decode_sprite_trajectory(ctx, gb) < 0)
                return AVERROR_INVALIDDATA;
            if (ctx->sprite_brightness_change)
                av_log(h->c.avctx, AV_LOG_ERROR,
                    "sprite_brightness_change not supported\n");
            if (ctx->vol_sprite_usage == STATIC_SPRITE)
                av_log(h->c.avctx, AV_LOG_ERROR, "static sprite not supported\n");
        } else {
            memset(ctx->sprite_offset, 0, sizeof(ctx->sprite_offset));
            memset(ctx->sprite_delta,  0, sizeof(ctx->sprite_delta));
        }
    }

    ctx->f_code = 1;
    ctx->b_code = 1;
    if (ctx->shape != BIN_ONLY_SHAPE) {
        h->c.chroma_qscale = h->c.qscale = get_bits(gb, ctx->quant_precision);
        if (h->c.qscale == 0) {
            av_log(h->c.avctx, AV_LOG_ERROR,
                   "Error, header damaged or not MPEG-4 header (qscale=0)\n");
            return AVERROR_INVALIDDATA;  // makes no sense to continue, as there is nothing left from the image then
        }

        if (h->c.pict_type != AV_PICTURE_TYPE_I) {
            ctx->f_code = get_bits(gb, 3);        /* fcode_for */
            if (ctx->f_code == 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "Error, header damaged or not MPEG-4 header (f_code=0)\n");
                ctx->f_code = 1;
                return AVERROR_INVALIDDATA;  // makes no sense to continue, as there is nothing left from the image then
            }
        }

        if (h->c.pict_type == AV_PICTURE_TYPE_B) {
            ctx->b_code = get_bits(gb, 3);
            if (ctx->b_code == 0) {
                av_log(h->c.avctx, AV_LOG_ERROR,
                       "Error, header damaged or not MPEG4 header (b_code=0)\n");
                ctx->b_code=1;
                return AVERROR_INVALIDDATA; // makes no sense to continue, as the MV decoding will break very quickly
            }
        }

        if (h->c.avctx->debug & FF_DEBUG_PICT_INFO) {
            av_log(h->c.avctx, AV_LOG_DEBUG,
                   "qp:%d fc:%d,%d %c size:%d pro:%d alt:%d top:%d %cpel part:%d resync:%d w:%d a:%d rnd:%d vot:%d%s dc:%d ce:%d/%d/%d time:%"PRId64" tincr:%d\n",
                   h->c.qscale, ctx->f_code, ctx->b_code,
                   h->c.pict_type == AV_PICTURE_TYPE_I ? 'I' : (h->c.pict_type == AV_PICTURE_TYPE_P ? 'P' : (h->c.pict_type == AV_PICTURE_TYPE_B ? 'B' : 'S')),
                   gb->size_in_bits,h->c.progressive_sequence, h->c.alternate_scan,
                   h->c.top_field_first, h->c.quarter_sample ? 'q' : 'h',
                   h->data_partitioning, ctx->resync_marker,
                   ctx->num_sprite_warping_points, ctx->sprite_warping_accuracy,
                   1 - h->c.no_rounding, ctx->vo_type,
                   ctx->vol_control_parameters ? " VOLC" : " ", ctx->intra_dc_threshold,
                   ctx->cplx_estimation_trash_i, ctx->cplx_estimation_trash_p,
                   ctx->cplx_estimation_trash_b,
                   h->c.time,
                   time_increment
                  );
        }

        if (!ctx->scalability) {
            if (ctx->shape != RECT_SHAPE && h->c.pict_type != AV_PICTURE_TYPE_I)
                skip_bits1(gb);  // vop shape coding type
        } else {
            if (ctx->enhancement_type) {
                int load_backward_shape = get_bits1(gb);
                if (load_backward_shape)
                    av_log(h->c.avctx, AV_LOG_ERROR,
                           "load backward shape isn't supported\n");
            }
            skip_bits(gb, 2);  // ref_select_code
        }
    }

    h->c.dct_unquantize_intra = ctx->mpeg_quant ? ctx->dct_unquantize_mpeg2_intra
                                                : ctx->dct_unquantize_h263_intra;
    // The following tells ff_mpv_reconstruct_mb() to unquantize iff mpeg_quant
    h->c.dct_unquantize_inter = ctx->mpeg_quant ? ctx->dct_unquantize_mpeg2_inter : NULL;

end:
    /* detect buggy encoders which don't set the low_delay flag
     * (divx4/xvid/opendivx). Note we cannot detect divx5 without B-frames
     * easily (although it's buggy too) */
    if (ctx->vo_type == 0 && ctx->vol_control_parameters == 0 &&
        ctx->divx_version == -1 && h->picture_number == 0) {
        av_log(h->c.avctx, AV_LOG_WARNING,
               "looks like this file was encoded with (divx4/(old)xvid/opendivx) -> forcing low_delay flag\n");
        h->c.low_delay = 1;
    }

    h->picture_number++;  // better than pic number==0 always ;)

    if (h->c.workaround_bugs & FF_BUG_EDGE) {
        h->c.h_edge_pos = h->c.width;
        h->c.v_edge_pos = h->c.height;
    }
    return 0;
}

static void decode_smpte_tc(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    AVCodecContext *const avctx = ctx->h.c.avctx;

    skip_bits(gb, 16); /* Time_code[63..48] */
    check_marker(avctx, gb, "after Time_code[63..48]");
    skip_bits(gb, 16); /* Time_code[47..32] */
    check_marker(avctx, gb, "after Time_code[47..32]");
    skip_bits(gb, 16); /* Time_code[31..16] */
    check_marker(avctx, gb, "after Time_code[31..16]");
    skip_bits(gb, 16); /* Time_code[15..0] */
    check_marker(avctx, gb, "after Time_code[15..0]");
    skip_bits(gb, 4); /* reserved_bits */
}

/**
 * Decode the next studio vop header.
 * @return <0 if something went wrong
 */
static int decode_studio_vop_header(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    H263DecContext *const h = &ctx->h;

    if (get_bits_left(gb) <= 32)
        return 0;

    h->partitioned_frame = 0;
    h->c.interlaced_dct = 0;
    h->decode_mb = mpeg4_decode_studio_mb;

    decode_smpte_tc(ctx, gb);

    skip_bits(gb, 10); /* temporal_reference */
    skip_bits(gb, 2); /* vop_structure */
    h->c.pict_type = get_bits(gb, 2) + AV_PICTURE_TYPE_I; /* vop_coding_type */
    if (get_bits1(gb)) { /* vop_coded */
        skip_bits1(gb); /* top_field_first */
        skip_bits1(gb); /* repeat_first_field */
        h->c.progressive_frame = get_bits1(gb) ^ 1; /* progressive_frame */
    }

    if (h->c.pict_type == AV_PICTURE_TYPE_I) {
        if (get_bits1(gb))
            reset_studio_dc_predictors(ctx);
    }

    if (ctx->shape != BIN_ONLY_SHAPE) {
        h->c.alternate_scan       = get_bits1(gb);
        h->c.frame_pred_frame_dct = get_bits1(gb);
        ctx->dct_precision        = get_bits(gb, 2);
        h->c.intra_dc_precision   = get_bits(gb, 2);
        h->c.q_scale_type         = get_bits1(gb);
    }

    ff_init_scantable(h->c.idsp.idct_permutation, &h->c.intra_scantable,
                      h->c.alternate_scan ? ff_alternate_vertical_scan : ff_zigzag_direct);

    mpeg4_load_default_matrices(&h->c);

    next_start_code_studio(gb);
    extension_and_user_data(&h->c, gb, 4);

    return 0;
}

static int decode_studiovisualobject(Mpeg4DecContext *ctx, GetBitContext *gb)
{
    int visual_object_type;

    skip_bits(gb, 4); /* visual_object_verid */
    visual_object_type = get_bits(gb, 4);
    if (visual_object_type != VOT_VIDEO_ID) {
        avpriv_request_sample(ctx->h.c.avctx, "VO type %u", visual_object_type);
        return AVERROR_PATCHWELCOME;
    }

    next_start_code_studio(gb);
    extension_and_user_data(&ctx->h.c, gb, 1);

    return 0;
}

/**
 * Decode MPEG-4 headers.
 *
 * @param  header If set the absence of a VOP is not treated as error; otherwise, it is treated as such.
 * @param  parse_only If set, things only relevant to a decoder may be skipped;
 *                    furthermore, the VLC tables may be uninitialized.
 * @return <0 if an error occurred
 *         FRAME_SKIPPED if a not coded VOP is found
 *         0 else
 */
int ff_mpeg4_parse_picture_header(Mpeg4DecContext *ctx, GetBitContext *gb,
                                  int header, int parse_only)
{
    MPVContext *const s = &ctx->h.c;
    unsigned startcode, v;
    int ret;
    int vol = 0;

    /* search next start code */
    align_get_bits(gb);

    // If we have not switched to studio profile than we also did not switch bps
    // that means something else (like a previous instance) outside set bps which
    // would be inconsistent with the correct state, thus reset it
    if (!s->studio_profile && s->avctx->bits_per_raw_sample != 8)
        s->avctx->bits_per_raw_sample = 0;

    if (s->codec_tag == AV_RL32("WV1F") && show_bits(gb, 24) == 0x575630) {
        skip_bits(gb, 24);
        if (get_bits(gb, 8) == 0xF0)
            goto end;
    }

    startcode = 0xff;
    for (;;) {
        if (get_bits_count(gb) >= gb->size_in_bits) {
            if (gb->size_in_bits == 8 &&
                (ctx->divx_version >= 0 || ctx->xvid_build >= 0) || s->codec_tag == AV_RL32("QMP4")) {
                av_log(s->avctx, AV_LOG_VERBOSE, "frame skip %d\n", gb->size_in_bits);
                return FRAME_SKIPPED;  // divx bug
            } else if (header && get_bits_count(gb) == gb->size_in_bits) {
                return 0; // ordinary return value for parsing of extradata
            } else
                return AVERROR_INVALIDDATA;  // end of stream
        }

        /* use the bits after the test */
        v = get_bits(gb, 8);
        startcode = ((startcode << 8) | v) & 0xffffffff;

        if ((startcode & 0xFFFFFF00) != 0x100)
            continue;  // no startcode

        if (s->avctx->debug & FF_DEBUG_STARTCODE) {
            const char *name;
            if (startcode <= 0x11F)
                name = "Video Object Start";
            else if (startcode <= 0x12F)
                name = "Video Object Layer Start";
            else if (startcode <= 0x13F)
                name = "Reserved";
            else if (startcode <= 0x15F)
                name = "FGS bp start";
            else if (startcode <= 0x1AF)
                name = "Reserved";
            else if (startcode == 0x1B0)
                name = "Visual Object Seq Start";
            else if (startcode == 0x1B1)
                name = "Visual Object Seq End";
            else if (startcode == 0x1B2)
                name = "User Data";
            else if (startcode == 0x1B3)
                name = "Group of VOP start";
            else if (startcode == 0x1B4)
                name = "Video Session Error";
            else if (startcode == 0x1B5)
                name = "Visual Object Start";
            else if (startcode == 0x1B6)
                name = "Video Object Plane start";
            else if (startcode == 0x1B7)
                name = "slice start";
            else if (startcode == 0x1B8)
                name = "extension start";
            else if (startcode == 0x1B9)
                name = "fgs start";
            else if (startcode == 0x1BA)
                name = "FBA Object start";
            else if (startcode == 0x1BB)
                name = "FBA Object Plane start";
            else if (startcode == 0x1BC)
                name = "Mesh Object start";
            else if (startcode == 0x1BD)
                name = "Mesh Object Plane start";
            else if (startcode == 0x1BE)
                name = "Still Texture Object start";
            else if (startcode == 0x1BF)
                name = "Texture Spatial Layer start";
            else if (startcode == 0x1C0)
                name = "Texture SNR Layer start";
            else if (startcode == 0x1C1)
                name = "Texture Tile start";
            else if (startcode == 0x1C2)
                name = "Texture Shape Layer start";
            else if (startcode == 0x1C3)
                name = "stuffing start";
            else if (startcode <= 0x1C5)
                name = "Reserved";
            else if (startcode <= 0x1FF)
                name = "System start";
            av_log(s->avctx, AV_LOG_DEBUG, "startcode: %3X %s at %d\n",
                   startcode, name, get_bits_count(gb));
        }

        if (startcode >= 0x120 && startcode <= 0x12F) {
            if (vol) {
                av_log(s->avctx, AV_LOG_WARNING, "Ignoring multiple VOL headers\n");
                continue;
            }
            vol++;
            if ((ret = decode_vol_header(ctx, gb)) < 0)
                return ret;
        } else if (startcode == USER_DATA_STARTCODE) {
            decode_user_data(ctx, gb);
        } else if (startcode == GOP_STARTCODE) {
            mpeg4_decode_gop_header(s, gb);
        } else if (startcode == VOS_STARTCODE) {
            int profile, level;
            mpeg4_decode_profile_level(s, gb, &profile, &level);
            if (profile == AV_PROFILE_MPEG4_SIMPLE_STUDIO &&
                (level > 0 && level < 9)) {
                s->studio_profile = 1;
                next_start_code_studio(gb);
                extension_and_user_data(s, gb, 0);
            } else if (s->studio_profile) {
                avpriv_request_sample(s->avctx, "Mix of studio and non studio profile");
                return AVERROR_PATCHWELCOME;
            }
            s->avctx->profile = profile;
            s->avctx->level   = level;
        } else if (startcode == VISUAL_OBJ_STARTCODE) {
            if (s->studio_profile) {
                if ((ret = decode_studiovisualobject(ctx, gb)) < 0)
                    return ret;
            } else
                mpeg4_decode_visual_object(s, gb);
        } else if (startcode == VOP_STARTCODE) {
            break;
        }

        align_get_bits(gb);
        startcode = 0xff;
    }

end:
    if (s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY)
        s->low_delay = 1;

    if (s->studio_profile) {
        if (!s->avctx->bits_per_raw_sample) {
            av_log(s->avctx, AV_LOG_ERROR, "Missing VOL header\n");
            return AVERROR_INVALIDDATA;
        }
        return decode_studio_vop_header(ctx, gb);
    } else
        return decode_vop_header(ctx, gb, parse_only);
}

static int mpeg4_decode_picture_header(H263DecContext *const h)
{
    Mpeg4DecContext *const ctx = h263_to_mpeg4(h);

    h->skipped_last_frame = 0;

    if (ctx->bitstream_buffer) {
        int buf_size = get_bits_left(&h->gb) / 8U;
        int bitstream_buffer_size = ctx->bitstream_buffer->size;
        const uint8_t *buf = h->gb.buffer;

        if (h->divx_packed) {
            for (int i = 0; i < buf_size - 3; i++) {
                if (buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) {
                    if (buf[i+3] == 0xB0) {
                        av_log(h->c.avctx, AV_LOG_WARNING, "Discarding excessive bitstream in packed xvid\n");
                        bitstream_buffer_size = 0;
                    }
                    break;
                }
            }
        }
        ctx->bitstream_buffer->size = 0;
        if (bitstream_buffer_size && (h->divx_packed || buf_size <= MAX_NVOP_SIZE)) {// divx 5.01+/xvid frame reorder
            int ret = init_get_bits8(&h->gb, ctx->bitstream_buffer->data,
                                     bitstream_buffer_size);
            if (ret < 0)
                return ret;
        } else
            av_buffer_unref(&ctx->bitstream_buffer);
    }

    return ff_mpeg4_parse_picture_header(ctx, &h->gb, 0, 0);
}

int ff_mpeg4_frame_end(AVCodecContext *avctx, const AVPacket *pkt)
{
    Mpeg4DecContext *ctx = avctx->priv_data;
    H263DecContext *const h = &ctx->h;
    int ret;

    av_assert1(!ctx->bitstream_buffer || !ctx->bitstream_buffer->size);

    /* divx 5.01+ bitstream reorder stuff */
    if (h->divx_packed) {
        int current_pos     = ctx->bitstream_buffer && h->gb.buffer == ctx->bitstream_buffer->data ? 0 : (get_bits_count(&h->gb) >> 3);
        int startcode_found = 0;
        uint8_t *buf = pkt->data;
        int buf_size = pkt->size;

        if (buf_size - current_pos > 7) {

            int i;
            for (i = current_pos; i < buf_size - 4; i++)

                if (buf[i]     == 0 &&
                    buf[i + 1] == 0 &&
                    buf[i + 2] == 1 &&
                    buf[i + 3] == 0xB6) {
                    startcode_found = !(buf[i + 4] & 0x40);
                    break;
                }
        }

        if (startcode_found) {
            if (!ctx->showed_packed_warning) {
                av_log(h->c.avctx, AV_LOG_INFO, "Video uses a non-standard and "
                       "wasteful way to store B-frames ('packed B-frames'). "
                       "Consider using the mpeg4_unpack_bframes bitstream filter without encoding but stream copy to fix it.\n");
                ctx->showed_packed_warning = 1;
            }
            ret = av_buffer_replace(&ctx->bitstream_buffer, pkt->buf);
            if (ret < 0)
                return ret;

            ctx->bitstream_buffer->data = buf + current_pos;
            ctx->bitstream_buffer->size = buf_size - current_pos;
        }
    }

    return 0;
}

#if CONFIG_MPEG4_DECODER
#if HAVE_THREADS
static av_cold void clear_context(MpegEncContext *s)
{
    memset(&s->buffer_pools, 0, sizeof(s->buffer_pools));
    memset(&s->next_pic, 0, sizeof(s->next_pic));
    memset(&s->last_pic, 0, sizeof(s->last_pic));
    memset(&s->cur_pic,  0, sizeof(s->cur_pic));

    memset(s->thread_context, 0, sizeof(s->thread_context));

    s->ac_val_base = NULL;
    s->ac_val = NULL;
    memset(&s->sc, 0, sizeof(s->sc));

    s->p_field_mv_table_base = NULL;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            s->p_field_mv_table[i][j] = NULL;

    s->dc_val_base = NULL;
    s->coded_block_base = NULL;
    s->mbintra_table = NULL;
    s->cbp_table = NULL;
    s->pred_dir_table = NULL;

    s->mbskip_table = NULL;

    s->er.error_status_table = NULL;
    s->er.er_temp_buffer = NULL;
    s->mb_index2xy = NULL;

    s->context_initialized   = 0;
    s->context_reinit        = 0;
}

static av_cold int update_mpvctx(MpegEncContext *s, const MpegEncContext *s1)
{
    AVCodecContext *avctx = s->avctx;
    // FIXME the following leads to a data race; instead copy only
    // the necessary fields.
    memcpy(s, s1, sizeof(*s));
    clear_context(s);

    s->avctx = avctx;

    if (s1->context_initialized) {
        int err = ff_mpv_common_init(s);
        if (err < 0)
            return err;
    }
    return 0;
}

static int mpeg4_update_thread_context(AVCodecContext *dst,
                                       const AVCodecContext *src)
{
    Mpeg4DecContext *s = dst->priv_data;
    const Mpeg4DecContext *s1 = src->priv_data;
    int init = s->h.c.context_initialized;
    int ret;

    if (!init) {
        ret = update_mpvctx(&s->h.c, &s1->h.c);
        if (ret < 0)
            return ret;
    }

    ret = ff_mpeg_update_thread_context(dst, src);
    if (ret < 0)
        return ret;

    // copy all the necessary fields explicitly
    s->time_increment_bits       = s1->time_increment_bits;
    s->shape                     = s1->shape;
    s->vol_sprite_usage          = s1->vol_sprite_usage;
    s->sprite_brightness_change  = s1->sprite_brightness_change;
    s->sprite_warping_accuracy   = s1->sprite_warping_accuracy;
    s->num_sprite_warping_points = s1->num_sprite_warping_points;
    s->h.data_partitioning       = s1->h.data_partitioning;
    s->mpeg_quant                = s1->mpeg_quant;
    s->rvlc                      = s1->rvlc;
    s->resync_marker             = s1->resync_marker;
    s->t_frame                   = s1->t_frame;
    s->new_pred                  = s1->new_pred;
    s->enhancement_type          = s1->enhancement_type;
    s->scalability               = s1->scalability;
    s->intra_dc_threshold        = s1->intra_dc_threshold;
    s->h.divx_packed             = s1->h.divx_packed;
    s->divx_version              = s1->divx_version;
    s->divx_build                = s1->divx_build;
    s->xvid_build                = s1->xvid_build;
    s->lavc_build                = s1->lavc_build;
    s->vo_type                   = s1->vo_type;
    s->showed_packed_warning     = s1->showed_packed_warning;
    s->vol_control_parameters    = s1->vol_control_parameters;
    s->cplx_estimation_trash_i   = s1->cplx_estimation_trash_i;
    s->cplx_estimation_trash_p   = s1->cplx_estimation_trash_p;
    s->cplx_estimation_trash_b   = s1->cplx_estimation_trash_b;
    s->rgb                       = s1->rgb;

    s->h.skipped_last_frame      = s1->h.skipped_last_frame;
    s->h.padding_bug_score       = s1->h.padding_bug_score; // FIXME: racy

    s->h.picture_number          = s1->h.picture_number;

    memcpy(s->sprite_shift, s1->sprite_shift, sizeof(s1->sprite_shift));
    memcpy(s->sprite_traj,  s1->sprite_traj,  sizeof(s1->sprite_traj));

    return av_buffer_replace(&s->bitstream_buffer, s1->bitstream_buffer);
}

static int mpeg4_update_thread_context_for_user(AVCodecContext *dst,
                                                const AVCodecContext *src)
{
    H263DecContext *const h = dst->priv_data;
    const H263DecContext *const h1 = src->priv_data;

    h->c.quarter_sample = h1->c.quarter_sample;
    h->divx_packed      = h1->divx_packed;

    return 0;
}
#endif

static av_cold void mpeg4_init_static(void)
{
    static VLCElem vlc_buf[6498];
    VLCInitState state = VLC_INIT_STATE(vlc_buf);

    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(studio_luma_dc, STUDIO_INTRA_BITS, 19,
                                       &ff_mpeg4_studio_dc_luma[0][1], 2,
                                       &ff_mpeg4_studio_dc_luma[0][0], 2, 1,
                                       0, 0);

    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(studio_chroma_dc, STUDIO_INTRA_BITS, 19,
                                       &ff_mpeg4_studio_dc_chroma[0][1], 2,
                                       &ff_mpeg4_studio_dc_chroma[0][0], 2, 1,
                                       0, 0);

    for (unsigned i = 0; i < 12; i++) {
        studio_intra_tab[i] =
            ff_vlc_init_tables_from_lengths(&state, STUDIO_INTRA_BITS, 24,
                                            &ff_mpeg4_studio_intra[i][0][1], 2,
                                            &ff_mpeg4_studio_intra[i][0][0], 2, 1,
                                            0, 0);
    }

    static uint8_t mpeg4_rl_intra_table[2][2 * MAX_RUN + MAX_LEVEL + 3];
    ff_rl_init(&ff_mpeg4_rl_intra, mpeg4_rl_intra_table);
    ff_h263_init_rl_inter();

    INIT_FIRST_VLC_RL(ff_mpeg4_rl_intra, 554);
    VLC_INIT_RL(ff_rvlc_rl_inter, 1072);
    INIT_FIRST_VLC_RL(ff_rvlc_rl_intra, 1072);
    VLC_INIT_STATIC_TABLE(dc_lum, DC_VLC_BITS, 10 /* 13 */,
                          &ff_mpeg4_DCtab_lum[0][1], 2, 1,
                          &ff_mpeg4_DCtab_lum[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE(dc_chrom, DC_VLC_BITS, 10 /* 13 */,
                          &ff_mpeg4_DCtab_chrom[0][1], 2, 1,
                          &ff_mpeg4_DCtab_chrom[0][0], 2, 1, 0);
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(sprite_trajectory, SPRITE_TRAJ_VLC_BITS, 15,
                                       ff_sprite_trajectory_lens, 1,
                                       NULL, 0, 0, 0, 0);
    VLC_INIT_STATIC_SPARSE_TABLE(mb_type_b_vlc, MB_TYPE_B_VLC_BITS, 4,
                                 &ff_mb_type_b_tab[0][1], 2, 1,
                                 &ff_mb_type_b_tab[0][0], 2, 1,
                                 mb_type_b_map, 2, 2, 0);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    Mpeg4DecContext *ctx = avctx->priv_data;
    H263DecContext *const h = &ctx->h;
    MPVUnquantDSPContext unquant_dsp_ctx;
    int ret;

    ctx->divx_version =
    ctx->divx_build   =
    ctx->xvid_build   =
    ctx->lavc_build   = -1;

    if ((ret = ff_h263_decode_init(avctx)) < 0)
        return ret;

    ff_mpv_unquantize_init(&unquant_dsp_ctx,
                           avctx->flags & AV_CODEC_FLAG_BITEXACT, 0);

    ctx->dct_unquantize_h263_intra  = unquant_dsp_ctx.dct_unquantize_h263_intra;
    ctx->dct_unquantize_mpeg2_intra = unquant_dsp_ctx.dct_unquantize_mpeg2_intra;
    // dct_unquantize_inter is only used with MPEG-2 quantizers,
    // so that is all we keep.
    ctx->dct_unquantize_mpeg2_inter = unquant_dsp_ctx.dct_unquantize_mpeg2_inter;

    h->c.y_dc_scale_table = ff_mpeg4_y_dc_scale_table;
    h->c.c_dc_scale_table = ff_mpeg4_c_dc_scale_table;

    h->c.h263_pred = 1;
    h->c.low_delay = 0; /* default, might be overridden in the vol header during header parsing */
    h->decode_header = mpeg4_decode_picture_header;
    h->decode_mb   = mpeg4_decode_mb;
    ctx->time_increment_bits = 4; /* default value for broken headers */
    ctx->quant_precision     = 5;

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    ff_qpeldsp_init(&h->c.qdsp);
    ff_mpeg4videodsp_init(&ctx->mdsp);

    ff_thread_once(&init_static_once, mpeg4_init_static);

    /* Must be after initializing the MPEG-4 static tables */
    if (avctx->extradata_size && !avctx->internal->is_copy) {
        GetBitContext gb;

        if (init_get_bits8(&gb, avctx->extradata, avctx->extradata_size) >= 0)
            ff_mpeg4_parse_picture_header(ctx, &gb, 1, 0);
    }

    return 0;
}

static av_cold void mpeg4_flush(AVCodecContext *avctx)
{
    Mpeg4DecContext *const ctx = avctx->priv_data;

    av_buffer_unref(&ctx->bitstream_buffer);
    ff_mpeg_flush(avctx);
}

static av_cold int mpeg4_close(AVCodecContext *avctx)
{
    Mpeg4DecContext *const ctx = avctx->priv_data;

    av_buffer_unref(&ctx->bitstream_buffer);

    return ff_mpv_decode_close(avctx);
}

#define OFFSET(x) offsetof(H263DecContext, x)
#define FLAGS AV_OPT_FLAG_EXPORT | AV_OPT_FLAG_READONLY
static const AVOption mpeg4_options[] = {
    {"quarter_sample", "1/4 subpel MC", OFFSET(c.quarter_sample), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS},
    {"divx_packed", "divx style packed b frames", OFFSET(divx_packed), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS},
    {NULL}
};

static const AVClass mpeg4_class = {
    .class_name = "MPEG4 Video Decoder",
    .item_name  = av_default_item_name,
    .option     = mpeg4_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_mpeg4_decoder = {
    .p.name                = "mpeg4",
    CODEC_LONG_NAME("MPEG-4 part 2"),
    .p.type                = AVMEDIA_TYPE_VIDEO,
    .p.id                  = AV_CODEC_ID_MPEG4,
    .priv_data_size        = sizeof(Mpeg4DecContext),
    .init                  = decode_init,
    FF_CODEC_DECODE_CB(ff_h263_decode_frame),
    .close                 = mpeg4_close,
    .p.capabilities        = AV_CODEC_CAP_DRAW_HORIZ_BAND | AV_CODEC_CAP_DR1 |
                             AV_CODEC_CAP_DELAY | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal         = FF_CODEC_CAP_INIT_CLEANUP |
                             FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    .flush                 = mpeg4_flush,
    .p.max_lowres          = 3,
    .p.profiles            = NULL_IF_CONFIG_SMALL(ff_mpeg4_video_profiles),
    UPDATE_THREAD_CONTEXT(mpeg4_update_thread_context),
    UPDATE_THREAD_CONTEXT_FOR_USER(mpeg4_update_thread_context_for_user),
    .p.priv_class = &mpeg4_class,
    .hw_configs            = (const AVCodecHWConfigInternal *const []) {
#if CONFIG_MPEG4_NVDEC_HWACCEL
                               HWACCEL_NVDEC(mpeg4),
#endif
#if CONFIG_MPEG4_VAAPI_HWACCEL
                               HWACCEL_VAAPI(mpeg4),
#endif
#if CONFIG_MPEG4_VDPAU_HWACCEL
                               HWACCEL_VDPAU(mpeg4),
#endif
#if CONFIG_MPEG4_VIDEOTOOLBOX_HWACCEL
                               HWACCEL_VIDEOTOOLBOX(mpeg4),
#endif
                               NULL
                           },
};
#endif /* CONFIG_MPEG4_DECODER */
