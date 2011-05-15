/*
 * XVideo Motion Compensation
 * Copyright (c) 2003 Ivan Kalvachev
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

#include <limits.h>
#include <X11/extensions/XvMC.h>

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

#include "xvmc.h"
#include "xvmc_internal.h"

/**
 * Initialize the block field of the MpegEncContext pointer passed as
 * parameter after making sure that the data is not corrupted.
 * In order to implement something like direct rendering instead of decoding
 * coefficients in s->blocks and then copying them, copy them directly
 * into the data_blocks array provided by xvmc.
 */
void ff_xvmc_init_block(MpegEncContext *s)
{
    struct xvmc_pix_fmt *render = (struct xvmc_pix_fmt*)s->current_picture.data[2];
    assert(render && render->xvmc_id == AV_XVMC_ID);

    s->block = (DCTELEM (*)[64])(render->data_blocks + render->next_free_data_block_num * 64);
}

/**
 * Fill individual block pointers, so there are no gaps in the data_block array
 * in case not all blocks in the macroblock are coded.
 */
void ff_xvmc_pack_pblocks(MpegEncContext *s, int cbp)
{
    int i, j = 0;
    const int mb_block_count = 4 + (1 << s->chroma_format);

    cbp <<= 12-mb_block_count;
    for (i = 0; i < mb_block_count; i++) {
        if (cbp & (1 << 11))
            s->pblocks[i] = &s->block[j++];
        else
            s->pblocks[i] = NULL;
        cbp += cbp;
    }
}

/**
 * Find and store the surfaces that are used as reference frames.
 * This function should be called for every new field and/or frame.
 * It should be safe to call the function a few times for the same field.
 */
int ff_xvmc_field_start(MpegEncContext *s, AVCodecContext *avctx)
{
    struct xvmc_pix_fmt *last, *next, *render = (struct xvmc_pix_fmt*)s->current_picture.data[2];
    const int mb_block_count = 4 + (1 << s->chroma_format);

    assert(avctx);
    if (!render || render->xvmc_id != AV_XVMC_ID ||
        !render->data_blocks || !render->mv_blocks ||
        (unsigned int)render->allocated_mv_blocks   > INT_MAX/(64*6) ||
        (unsigned int)render->allocated_data_blocks > INT_MAX/64     ||
        !render->p_surface) {
        av_log(avctx, AV_LOG_ERROR,
               "Render token doesn't look as expected.\n");
        return -1; // make sure that this is a render packet
    }

    if (render->filled_mv_blocks_num) {
        av_log(avctx, AV_LOG_ERROR,
               "Rendering surface contains %i unprocessed blocks.\n",
               render->filled_mv_blocks_num);
        return -1;
    }
    if (render->allocated_mv_blocks   < 1 ||
        render->allocated_data_blocks <  render->allocated_mv_blocks*mb_block_count ||
        render->start_mv_blocks_num   >= render->allocated_mv_blocks                ||
        render->next_free_data_block_num >
                        render->allocated_data_blocks -
                        mb_block_count*(render->allocated_mv_blocks-render->start_mv_blocks_num)) {
        av_log(avctx, AV_LOG_ERROR,
               "Rendering surface doesn't provide enough block structures to work with.\n");
        return -1;
    }

    render->picture_structure = s->picture_structure;
    render->flags             = s->first_field ? 0 : XVMC_SECOND_FIELD;
    render->p_future_surface  = NULL;
    render->p_past_surface    = NULL;

    switch(s->pict_type) {
        case  AV_PICTURE_TYPE_I:
            return 0; // no prediction from other frames
        case  AV_PICTURE_TYPE_B:
            next = (struct xvmc_pix_fmt*)s->next_picture.data[2];
            if (!next)
                return -1;
            if (next->xvmc_id != AV_XVMC_ID)
                return -1;
            render->p_future_surface = next->p_surface;
            // no return here, going to set forward prediction
        case  AV_PICTURE_TYPE_P:
            last = (struct xvmc_pix_fmt*)s->last_picture.data[2];
            if (!last)
                last = render; // predict second field from the first
            if (last->xvmc_id != AV_XVMC_ID)
                return -1;
            render->p_past_surface = last->p_surface;
            return 0;
    }

return -1;
}

/**
 * Complete frame/field rendering by passing any remaining blocks.
 * Normally ff_draw_horiz_band() is called for each slice, however,
 * some leftover blocks, for example from error_resilience(), may remain.
 * It should be safe to call the function a few times for the same field.
 */
void ff_xvmc_field_end(MpegEncContext *s)
{
    struct xvmc_pix_fmt *render = (struct xvmc_pix_fmt*)s->current_picture.data[2];
    assert(render);

    if (render->filled_mv_blocks_num > 0)
        ff_draw_horiz_band(s, 0, 0);
}

/**
 * Synthesize the data needed by XvMC to render one macroblock of data.
 * Fill all relevant fields, if necessary do IDCT.
 */
void ff_xvmc_decode_mb(MpegEncContext *s)
{
    XvMCMacroBlock *mv_block;
    struct xvmc_pix_fmt *render;
    int i, cbp, blocks_per_mb;

    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;


    if (s->encoding) {
        av_log(s->avctx, AV_LOG_ERROR, "XVMC doesn't support encoding!!!\n");
        return;
    }

    // from MPV_decode_mb(), update DC predictors for P macroblocks
    if (!s->mb_intra) {
        s->last_dc[0] =
        s->last_dc[1] =
        s->last_dc[2] =  128 << s->intra_dc_precision;
    }

    // MC doesn't skip blocks
    s->mb_skipped = 0;


    // Do I need to export quant when I could not perform postprocessing?
    // Anyway, it doesn't hurt.
    s->current_picture.qscale_table[mb_xy] = s->qscale;

    // start of XVMC-specific code
    render = (struct xvmc_pix_fmt*)s->current_picture.data[2];
    assert(render);
    assert(render->xvmc_id == AV_XVMC_ID);
    assert(render->mv_blocks);

    // take the next free macroblock
    mv_block = &render->mv_blocks[render->start_mv_blocks_num +
                                  render->filled_mv_blocks_num];

    mv_block->x        = s->mb_x;
    mv_block->y        = s->mb_y;
    mv_block->dct_type = s->interlaced_dct; // XVMC_DCT_TYPE_FRAME/FIELD;
    if (s->mb_intra) {
        mv_block->macroblock_type = XVMC_MB_TYPE_INTRA; // no MC, all done
    } else {
        mv_block->macroblock_type = XVMC_MB_TYPE_PATTERN;

        if (s->mv_dir & MV_DIR_FORWARD) {
            mv_block->macroblock_type |= XVMC_MB_TYPE_MOTION_FORWARD;
            // PMV[n][dir][xy] = mv[dir][n][xy]
            mv_block->PMV[0][0][0] = s->mv[0][0][0];
            mv_block->PMV[0][0][1] = s->mv[0][0][1];
            mv_block->PMV[1][0][0] = s->mv[0][1][0];
            mv_block->PMV[1][0][1] = s->mv[0][1][1];
        }
        if (s->mv_dir & MV_DIR_BACKWARD) {
            mv_block->macroblock_type |= XVMC_MB_TYPE_MOTION_BACKWARD;
            mv_block->PMV[0][1][0] = s->mv[1][0][0];
            mv_block->PMV[0][1][1] = s->mv[1][0][1];
            mv_block->PMV[1][1][0] = s->mv[1][1][0];
            mv_block->PMV[1][1][1] = s->mv[1][1][1];
        }

        switch(s->mv_type) {
            case  MV_TYPE_16X16:
                mv_block->motion_type = XVMC_PREDICTION_FRAME;
                break;
            case  MV_TYPE_16X8:
                mv_block->motion_type = XVMC_PREDICTION_16x8;
                break;
            case  MV_TYPE_FIELD:
                mv_block->motion_type = XVMC_PREDICTION_FIELD;
                if (s->picture_structure == PICT_FRAME) {
                    mv_block->PMV[0][0][1] <<= 1;
                    mv_block->PMV[1][0][1] <<= 1;
                    mv_block->PMV[0][1][1] <<= 1;
                    mv_block->PMV[1][1][1] <<= 1;
                }
                break;
            case  MV_TYPE_DMV:
                mv_block->motion_type = XVMC_PREDICTION_DUAL_PRIME;
                if (s->picture_structure == PICT_FRAME) {

                    mv_block->PMV[0][0][0] = s->mv[0][0][0];      // top from top
                    mv_block->PMV[0][0][1] = s->mv[0][0][1] << 1;

                    mv_block->PMV[0][1][0] = s->mv[0][0][0];      // bottom from bottom
                    mv_block->PMV[0][1][1] = s->mv[0][0][1] << 1;

                    mv_block->PMV[1][0][0] = s->mv[0][2][0];      // dmv00, top from bottom
                    mv_block->PMV[1][0][1] = s->mv[0][2][1] << 1; // dmv01

                    mv_block->PMV[1][1][0] = s->mv[0][3][0];      // dmv10, bottom from top
                    mv_block->PMV[1][1][1] = s->mv[0][3][1] << 1; // dmv11

                } else {
                    mv_block->PMV[0][1][0] = s->mv[0][2][0];      // dmv00
                    mv_block->PMV[0][1][1] = s->mv[0][2][1];      // dmv01
                }
                break;
            default:
                assert(0);
        }

        mv_block->motion_vertical_field_select = 0;

        // set correct field references
        if (s->mv_type == MV_TYPE_FIELD || s->mv_type == MV_TYPE_16X8) {
            mv_block->motion_vertical_field_select |= s->field_select[0][0];
            mv_block->motion_vertical_field_select |= s->field_select[1][0] << 1;
            mv_block->motion_vertical_field_select |= s->field_select[0][1] << 2;
            mv_block->motion_vertical_field_select |= s->field_select[1][1] << 3;
        }
    } // !intra
    // time to handle data blocks
    mv_block->index = render->next_free_data_block_num;

    blocks_per_mb = 6;
    if (s->chroma_format >= 2) {
        blocks_per_mb = 4 + (1 << s->chroma_format);
    }

    // calculate cbp
    cbp = 0;
    for (i = 0; i < blocks_per_mb; i++) {
        cbp += cbp;
        if (s->block_last_index[i] >= 0)
            cbp++;
    }

    if (s->flags & CODEC_FLAG_GRAY) {
        if (s->mb_intra) {                                   // intra frames are always full chroma blocks
            for (i = 4; i < blocks_per_mb; i++) {
                memset(s->pblocks[i], 0, sizeof(*s->pblocks[i]));  // so we need to clear them
                if (!render->unsigned_intra)
                    *s->pblocks[i][0] = 1 << 10;
            }
        } else {
            cbp &= 0xf << (blocks_per_mb - 4);
            blocks_per_mb = 4;                               // luminance blocks only
        }
    }
    mv_block->coded_block_pattern = cbp;
    if (cbp == 0)
        mv_block->macroblock_type &= ~XVMC_MB_TYPE_PATTERN;

    for (i = 0; i < blocks_per_mb; i++) {
        if (s->block_last_index[i] >= 0) {
            // I do not have unsigned_intra MOCO to test, hope it is OK.
            if (s->mb_intra && (render->idct || !render->unsigned_intra))
                *s->pblocks[i][0] -= 1 << 10;
            if (!render->idct) {
                s->dsp.idct(*s->pblocks[i]);
                /* It is unclear if MC hardware requires pixel diff values to be
                 * in the range [-255;255]. TODO: Clipping if such hardware is
                 * ever found. As of now it would only be an unnecessary
                 * slowdown. */
            }
            // copy blocks only if the codec doesn't support pblocks reordering
            if (s->avctx->xvmc_acceleration == 1) {
                memcpy(&render->data_blocks[render->next_free_data_block_num*64],
                       s->pblocks[i], sizeof(*s->pblocks[i]));
            }
            render->next_free_data_block_num++;
        }
    }
    render->filled_mv_blocks_num++;

    assert(render->filled_mv_blocks_num     <= render->allocated_mv_blocks);
    assert(render->next_free_data_block_num <= render->allocated_data_blocks);
    /* The above conditions should not be able to fail as long as this function
     * is used and the following 'if ()' automatically calls a callback to free
     * blocks. */


    if (render->filled_mv_blocks_num == render->allocated_mv_blocks)
        ff_draw_horiz_band(s, 0, 0);
}
