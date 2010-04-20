/*
 * Indeo Video Interactive v5 compatible decoder
 * Copyright (c) 2009 Maxim Poliakovski
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

/**
 * @file
 * Indeo Video Interactive version 5 decoder
 *
 * Indeo5 data is usually transported within .avi or .mov files.
 * Known FOURCCs: 'IV50'
 */

#define ALT_BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "ivi_dsp.h"
#include "ivi_common.h"
#include "indeo5data.h"

/**
 *  Indeo5 frame types.
 */
enum {
    FRAMETYPE_INTRA       = 0,
    FRAMETYPE_INTER       = 1,  ///< non-droppable P-frame
    FRAMETYPE_INTER_SCAL  = 2,  ///< droppable P-frame used in the scalability mode
    FRAMETYPE_INTER_NOREF = 3,  ///< droppable P-frame
    FRAMETYPE_NULL        = 4   ///< empty frame with no data
};

#define IVI5_PIC_SIZE_ESC       15

#define IVI5_IS_PROTECTED       0x20

typedef struct {
    GetBitContext   gb;
    AVFrame         frame;
    RVMapDesc       rvmap_tabs[9];   ///< local corrected copy of the static rvmap tables
    IVIPlaneDesc    planes[3];       ///< color planes
    const uint8_t   *frame_data;     ///< input frame data pointer
    int             buf_switch;      ///< used to switch between three buffers
    int             inter_scal;      ///< signals a sequence of scalable inter frames
    int             dst_buf;         ///< buffer index for the currently decoded frame
    int             ref_buf;         ///< inter frame reference buffer index
    int             ref2_buf;        ///< temporal storage for switching buffers
    uint32_t        frame_size;      ///< frame size in bytes
    int             frame_type;
    int             prev_frame_type; ///< frame type of the previous frame
    int             frame_num;
    uint32_t        pic_hdr_size;    ///< picture header size in bytes
    uint8_t         frame_flags;
    uint16_t        checksum;        ///< frame checksum

    IVIHuffTab      mb_vlc;          ///< vlc table for decoding macroblock data

    uint16_t        gop_hdr_size;
    uint8_t         gop_flags;
    int             is_scalable;
    uint32_t        lock_word;
    IVIPicConfig    pic_conf;
} IVI5DecContext;


/**
 *  Decodes Indeo5 GOP (Group of pictures) header.
 *  This header is present in key frames only.
 *  It defines parameters for all frames in a GOP.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param avctx    [in] ptr to the AVCodecContext
 *  @return         result code: 0 = OK, -1 = error
 */
static int decode_gop_header(IVI5DecContext *ctx, AVCodecContext *avctx)
{
    int             result, i, p, tile_size, pic_size_indx, mb_size, blk_size, blk_size_changed = 0;
    IVIBandDesc     *band, *band1, *band2;
    IVIPicConfig    pic_conf;

    ctx->gop_flags = get_bits(&ctx->gb, 8);

    ctx->gop_hdr_size = (ctx->gop_flags & 1) ? get_bits(&ctx->gb, 16) : 0;

    if (ctx->gop_flags & IVI5_IS_PROTECTED)
        ctx->lock_word = get_bits_long(&ctx->gb, 32);

    tile_size = (ctx->gop_flags & 0x40) ? 64 << get_bits(&ctx->gb, 2) : 0;
    if (tile_size > 256) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile size: %d\n", tile_size);
        return -1;
    }

    /* decode number of wavelet bands */
    /* num_levels * 3 + 1 */
    pic_conf.luma_bands   = get_bits(&ctx->gb, 2) * 3 + 1;
    pic_conf.chroma_bands = get_bits1(&ctx->gb)   * 3 + 1;
    ctx->is_scalable = pic_conf.luma_bands != 1 || pic_conf.chroma_bands != 1;
    if (ctx->is_scalable && (pic_conf.luma_bands != 4 || pic_conf.chroma_bands != 1)) {
        av_log(avctx, AV_LOG_ERROR, "Scalability: unsupported subdivision! Luma bands: %d, chroma bands: %d\n",
               pic_conf.luma_bands, pic_conf.chroma_bands);
        return -1;
    }

    pic_size_indx = get_bits(&ctx->gb, 4);
    if (pic_size_indx == IVI5_PIC_SIZE_ESC) {
        pic_conf.pic_height = get_bits(&ctx->gb, 13);
        pic_conf.pic_width  = get_bits(&ctx->gb, 13);
    } else {
        pic_conf.pic_height = ivi5_common_pic_sizes[pic_size_indx * 2 + 1] << 2;
        pic_conf.pic_width  = ivi5_common_pic_sizes[pic_size_indx * 2    ] << 2;
    }

    if (ctx->gop_flags & 2) {
        av_log(avctx, AV_LOG_ERROR, "YV12 picture format not supported!\n");
        return -1;
    }

    pic_conf.chroma_height = (pic_conf.pic_height + 3) >> 2;
    pic_conf.chroma_width  = (pic_conf.pic_width  + 3) >> 2;

    if (!tile_size) {
        pic_conf.tile_height = pic_conf.pic_height;
        pic_conf.tile_width  = pic_conf.pic_width;
    } else {
        pic_conf.tile_height = pic_conf.tile_width = tile_size;
    }

    /* check if picture layout was changed and reallocate buffers */
    if (ivi_pic_config_cmp(&pic_conf, &ctx->pic_conf)) {
        result = ff_ivi_init_planes(ctx->planes, &pic_conf);
        if (result) {
            av_log(avctx, AV_LOG_ERROR, "Couldn't reallocate color planes!\n");
            return -1;
        }
        ctx->pic_conf = pic_conf;
        blk_size_changed = 1; /* force reallocation of the internal structures */
    }

    for (p = 0; p <= 1; p++) {
        for (i = 0; i < (!p ? pic_conf.luma_bands : pic_conf.chroma_bands); i++) {
            band = &ctx->planes[p].bands[i];

            band->is_halfpel = get_bits1(&ctx->gb);

            mb_size  = get_bits1(&ctx->gb);
            blk_size = 8 >> get_bits1(&ctx->gb);
            mb_size  = blk_size << !mb_size;

            blk_size_changed = mb_size != band->mb_size || blk_size != band->blk_size;
            if (blk_size_changed) {
                band->mb_size  = mb_size;
                band->blk_size = blk_size;
            }

            if (get_bits1(&ctx->gb)) {
                av_log(avctx, AV_LOG_ERROR, "Extended transform info encountered!\n");
                return -1;
            }

            /* select transform function and scan pattern according to plane and band number */
            switch ((p << 2) + i) {
            case 0:
                band->inv_transform = ff_ivi_inverse_slant_8x8;
                band->dc_transform  = ff_ivi_dc_slant_2d;
                band->scan          = ff_zigzag_direct;
                break;

            case 1:
                band->inv_transform = ff_ivi_row_slant8;
                band->dc_transform  = ff_ivi_dc_row_slant;
                band->scan          = ivi5_scans8x8[0];
                break;

            case 2:
                band->inv_transform = ff_ivi_col_slant8;
                band->dc_transform  = ff_ivi_dc_col_slant;
                band->scan          = ivi5_scans8x8[1];
                break;

            case 3:
                band->inv_transform = ff_ivi_put_pixels_8x8;
                band->dc_transform  = ff_ivi_put_dc_pixel_8x8;
                band->scan          = ivi5_scans8x8[1];
                break;

            case 4:
                band->inv_transform = ff_ivi_inverse_slant_4x4;
                band->dc_transform  = ff_ivi_dc_slant_2d;
                band->scan          = ivi5_scan4x4;
                break;
            }

            band->is_2d_trans = band->inv_transform == ff_ivi_inverse_slant_8x8 ||
                                band->inv_transform == ff_ivi_inverse_slant_4x4;

            /* select dequant matrix according to plane and band number */
            if (!p) {
                band->quant_mat = (pic_conf.luma_bands > 1) ? i+1 : 0;
            } else {
                band->quant_mat = 5;
            }

            if (get_bits(&ctx->gb, 2)) {
                av_log(avctx, AV_LOG_ERROR, "End marker missing!\n");
                return -1;
            }
        }
    }

    /* copy chroma parameters into the 2nd chroma plane */
    for (i = 0; i < pic_conf.chroma_bands; i++) {
        band1 = &ctx->planes[1].bands[i];
        band2 = &ctx->planes[2].bands[i];

        band2->width         = band1->width;
        band2->height        = band1->height;
        band2->mb_size       = band1->mb_size;
        band2->blk_size      = band1->blk_size;
        band2->is_halfpel    = band1->is_halfpel;
        band2->quant_mat     = band1->quant_mat;
        band2->scan          = band1->scan;
        band2->inv_transform = band1->inv_transform;
        band2->dc_transform  = band1->dc_transform;
        band2->is_2d_trans   = band1->is_2d_trans;
    }

    /* reallocate internal structures if needed */
    if (blk_size_changed) {
        result = ff_ivi_init_tiles(ctx->planes, pic_conf.tile_width,
                                   pic_conf.tile_height);
        if (result) {
            av_log(avctx, AV_LOG_ERROR,
                   "Couldn't reallocate internal structures!\n");
            return -1;
        }
    }

    if (ctx->gop_flags & 8) {
        if (get_bits(&ctx->gb, 3)) {
            av_log(avctx, AV_LOG_ERROR, "Alignment bits are not zero!\n");
            return -1;
        }

        if (get_bits1(&ctx->gb))
            skip_bits_long(&ctx->gb, 24); /* skip transparency fill color */
    }

    align_get_bits(&ctx->gb);

    skip_bits(&ctx->gb, 23); /* FIXME: unknown meaning */

    /* skip GOP extension if any */
    if (get_bits1(&ctx->gb)) {
        do {
            i = get_bits(&ctx->gb, 16);
        } while (i & 0x8000);
    }

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Skips a header extension.
 *
 *  @param gb   [in,out] the GetBit context
 */
static inline void skip_hdr_extension(GetBitContext *gb)
{
    int i, len;

    do {
        len = get_bits(gb, 8);
        for (i = 0; i < len; i++) skip_bits(gb, 8);
    } while(len);
}


/**
 *  Decodes Indeo5 picture header.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param avctx    [in] ptr to the AVCodecContext
 *  @return         result code: 0 = OK, -1 = error
 */
static int decode_pic_hdr(IVI5DecContext *ctx, AVCodecContext *avctx)
{
    if (get_bits(&ctx->gb, 5) != 0x1F) {
        av_log(avctx, AV_LOG_ERROR, "Invalid picture start code!\n");
        return -1;
    }

    ctx->prev_frame_type = ctx->frame_type;
    ctx->frame_type      = get_bits(&ctx->gb, 3);
    if (ctx->frame_type >= 5) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame type: %d \n", ctx->frame_type);
        return -1;
    }

    ctx->frame_num = get_bits(&ctx->gb, 8);

    if (ctx->frame_type == FRAMETYPE_INTRA) {
        if (decode_gop_header(ctx, avctx))
            return -1;
    }

    if (ctx->frame_type != FRAMETYPE_NULL) {
        ctx->frame_flags = get_bits(&ctx->gb, 8);

        ctx->pic_hdr_size = (ctx->frame_flags & 1) ? get_bits_long(&ctx->gb, 24) : 0;

        ctx->checksum = (ctx->frame_flags & 0x10) ? get_bits(&ctx->gb, 16) : 0;

        /* skip unknown extension if any */
        if (ctx->frame_flags & 0x20)
            skip_hdr_extension(&ctx->gb); /* XXX: untested */

        /* decode macroblock huffman codebook */
        if (ff_ivi_dec_huff_desc(&ctx->gb, ctx->frame_flags & 0x40, IVI_MB_HUFF, &ctx->mb_vlc, avctx))
            return -1;

        skip_bits(&ctx->gb, 3); /* FIXME: unknown meaning! */
    }

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Decodes Indeo5 band header.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param band     [in,out] ptr to the band descriptor
 *  @param avctx    [in] ptr to the AVCodecContext
 *  @return         result code: 0 = OK, -1 = error
 */
static int decode_band_hdr(IVI5DecContext *ctx, IVIBandDesc *band,
                           AVCodecContext *avctx)
{
    int         i;
    uint8_t     band_flags;

    band_flags = get_bits(&ctx->gb, 8);

    if (band_flags & 1) {
        band->is_empty = 1;
        return 0;
    }

    band->data_size = (ctx->frame_flags & 0x80) ? get_bits_long(&ctx->gb, 24) : 0;

    band->inherit_mv     = band_flags & 2;
    band->inherit_qdelta = band_flags & 8;
    band->qdelta_present = band_flags & 4;
    if (!band->qdelta_present) band->inherit_qdelta = 1;

    /* decode rvmap probability corrections if any */
    band->num_corr = 0; /* there are no corrections */
    if (band_flags & 0x10) {
        band->num_corr = get_bits(&ctx->gb, 8); /* get number of correction pairs */
        if (band->num_corr > 61) {
            av_log(avctx, AV_LOG_ERROR, "Too many corrections: %d\n",
                   band->num_corr);
            return -1;
        }

        /* read correction pairs */
        for (i = 0; i < band->num_corr * 2; i++)
            band->corr[i] = get_bits(&ctx->gb, 8);
    }

    /* select appropriate rvmap table for this band */
    band->rvmap_sel = (band_flags & 0x40) ? get_bits(&ctx->gb, 3) : 8;

    /* decode block huffman codebook */
    if (ff_ivi_dec_huff_desc(&ctx->gb, band_flags & 0x80, IVI_BLK_HUFF, &band->blk_vlc, avctx))
        return -1;

    band->checksum_present = get_bits1(&ctx->gb);
    if (band->checksum_present)
        band->checksum = get_bits(&ctx->gb, 16);

    band->glob_quant = get_bits(&ctx->gb, 5);

    /* skip unknown extension if any */
    if (band_flags & 0x20) { /* XXX: untested */
        align_get_bits(&ctx->gb);
        skip_hdr_extension(&ctx->gb);
    }

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Decodes info (block type, cbp, quant delta, motion vector)
 *  for all macroblocks in the current tile.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param band     [in,out] ptr to the band descriptor
 *  @param tile     [in,out] ptr to the tile descriptor
 *  @param avctx    [in] ptr to the AVCodecContext
 *  @return         result code: 0 = OK, -1 = error
 */
static int decode_mb_info(IVI5DecContext *ctx, IVIBandDesc *band,
                          IVITile *tile, AVCodecContext *avctx)
{
    int         x, y, mv_x, mv_y, mv_delta, offs, mb_offset,
                mv_scale, blks_per_mb;
    IVIMbInfo   *mb, *ref_mb;
    int         row_offset = band->mb_size * band->pitch;

    mb     = tile->mbs;
    ref_mb = tile->ref_mbs;
    offs   = tile->ypos * band->pitch + tile->xpos;

    /* scale factor for motion vectors */
    mv_scale = (ctx->planes[0].bands[0].mb_size >> 3) - (band->mb_size >> 3);
    mv_x = mv_y = 0;

    for (y = tile->ypos; y < (tile->ypos + tile->height); y += band->mb_size) {
        mb_offset = offs;

        for (x = tile->xpos; x < (tile->xpos + tile->width); x += band->mb_size) {
            mb->xpos     = x;
            mb->ypos     = y;
            mb->buf_offs = mb_offset;

            if (get_bits1(&ctx->gb)) {
                if (ctx->frame_type == FRAMETYPE_INTRA) {
                    av_log(avctx, AV_LOG_ERROR, "Empty macroblock in an INTRA picture!\n");
                    return -1;
                }
                mb->type = 1; /* empty macroblocks are always INTER */
                mb->cbp  = 0; /* all blocks are empty */

                mb->q_delta = 0;
                if (!band->plane && !band->band_num && (ctx->frame_flags & 8)) {
                    mb->q_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                           IVI_VLC_BITS, 1);
                    mb->q_delta = IVI_TOSIGNED(mb->q_delta);
                }

                mb->mv_x = mb->mv_y = 0; /* no motion vector coded */
                if (band->inherit_mv){
                    /* motion vector inheritance */
                    if (mv_scale) {
                        mb->mv_x = ivi_scale_mv(ref_mb->mv_x, mv_scale);
                        mb->mv_y = ivi_scale_mv(ref_mb->mv_y, mv_scale);
                    } else {
                        mb->mv_x = ref_mb->mv_x;
                        mb->mv_y = ref_mb->mv_y;
                    }
                }
            } else {
                if (band->inherit_mv) {
                    mb->type = ref_mb->type; /* copy mb_type from corresponding reference mb */
                } else if (ctx->frame_type == FRAMETYPE_INTRA) {
                    mb->type = 0; /* mb_type is always INTRA for intra-frames */
                } else {
                    mb->type = get_bits1(&ctx->gb);
                }

                blks_per_mb = band->mb_size != band->blk_size ? 4 : 1;
                mb->cbp = get_bits(&ctx->gb, blks_per_mb);

                mb->q_delta = 0;
                if (band->qdelta_present) {
                    if (band->inherit_qdelta) {
                        if (ref_mb) mb->q_delta = ref_mb->q_delta;
                    } else if (mb->cbp || (!band->plane && !band->band_num &&
                                           (ctx->frame_flags & 8))) {
                        mb->q_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                               IVI_VLC_BITS, 1);
                        mb->q_delta = IVI_TOSIGNED(mb->q_delta);
                    }
                }

                if (!mb->type) {
                    mb->mv_x = mb->mv_y = 0; /* there is no motion vector in intra-macroblocks */
                } else {
                    if (band->inherit_mv){
                        /* motion vector inheritance */
                        if (mv_scale) {
                            mb->mv_x = ivi_scale_mv(ref_mb->mv_x, mv_scale);
                            mb->mv_y = ivi_scale_mv(ref_mb->mv_y, mv_scale);
                        } else {
                            mb->mv_x = ref_mb->mv_x;
                            mb->mv_y = ref_mb->mv_y;
                        }
                    } else {
                        /* decode motion vector deltas */
                        mv_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                            IVI_VLC_BITS, 1);
                        mv_y += IVI_TOSIGNED(mv_delta);
                        mv_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                            IVI_VLC_BITS, 1);
                        mv_x += IVI_TOSIGNED(mv_delta);
                        mb->mv_x = mv_x;
                        mb->mv_y = mv_y;
                    }
                }
            }

            mb++;
            if (ref_mb)
                ref_mb++;
            mb_offset += band->mb_size;
        }

        offs += row_offset;
    }

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Decodes an Indeo5 band.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param band     [in,out] ptr to the band descriptor
 *  @param avctx    [in] ptr to the AVCodecContext
 *  @return         result code: 0 = OK, -1 = error
 */
static int decode_band(IVI5DecContext *ctx, int plane_num,
                       IVIBandDesc *band, AVCodecContext *avctx)
{
    int         result, i, t, idx1, idx2, pos;
    IVITile     *tile;

    band->buf     = band->bufs[ctx->dst_buf];
    band->ref_buf = band->bufs[ctx->ref_buf];
    band->data_ptr = ctx->frame_data + (get_bits_count(&ctx->gb) >> 3);

    result = decode_band_hdr(ctx, band, avctx);
    if (result) {
        av_log(avctx, AV_LOG_ERROR, "Error while decoding band header: %d\n",
               result);
        return -1;
    }

    if (band->is_empty) {
        av_log(avctx, AV_LOG_ERROR, "Empty band encountered!\n");
        return -1;
    }

    if (band->blk_size == 8) {
        band->intra_base  = &ivi5_base_quant_8x8_intra[band->quant_mat][0];
        band->inter_base  = &ivi5_base_quant_8x8_inter[band->quant_mat][0];
        band->intra_scale = &ivi5_scale_quant_8x8_intra[band->quant_mat][0];
        band->inter_scale = &ivi5_scale_quant_8x8_inter[band->quant_mat][0];
    } else {
        band->intra_base  = ivi5_base_quant_4x4_intra;
        band->inter_base  = ivi5_base_quant_4x4_inter;
        band->intra_scale = ivi5_scale_quant_4x4_intra;
        band->inter_scale = ivi5_scale_quant_4x4_inter;
    }

    band->rv_map = &ctx->rvmap_tabs[band->rvmap_sel];

    /* apply corrections to the selected rvmap table if present */
    for (i = 0; i < band->num_corr; i++) {
        idx1 = band->corr[i*2];
        idx2 = band->corr[i*2+1];
        FFSWAP(uint8_t, band->rv_map->runtab[idx1], band->rv_map->runtab[idx2]);
        FFSWAP(int16_t, band->rv_map->valtab[idx1], band->rv_map->valtab[idx2]);
    }

    pos = get_bits_count(&ctx->gb);

    for (t = 0; t < band->num_tiles; t++) {
        tile = &band->tiles[t];

        tile->is_empty = get_bits1(&ctx->gb);
        if (tile->is_empty) {
            ff_ivi_process_empty_tile(avctx, band, tile,
                                      (ctx->planes[0].bands[0].mb_size >> 3) - (band->mb_size >> 3));
        } else {
            tile->data_size = ff_ivi_dec_tile_data_size(&ctx->gb);

            result = decode_mb_info(ctx, band, tile, avctx);
            if (result < 0)
                break;

            result = ff_ivi_decode_blocks(&ctx->gb, band, tile);
            if (result < 0 || (get_bits_count(&ctx->gb) - pos) >> 3 != tile->data_size) {
                av_log(avctx, AV_LOG_ERROR, "Corrupted tile data encountered!\n");
                break;
            }
            pos += tile->data_size << 3; // skip to next tile
        }
    }

    /* restore the selected rvmap table by applying its corrections in reverse order */
    for (i = band->num_corr-1; i >= 0; i--) {
        idx1 = band->corr[i*2];
        idx2 = band->corr[i*2+1];
        FFSWAP(uint8_t, band->rv_map->runtab[idx1], band->rv_map->runtab[idx2]);
        FFSWAP(int16_t, band->rv_map->valtab[idx1], band->rv_map->valtab[idx2]);
    }

#if IVI_DEBUG
    if (band->checksum_present) {
        uint16_t chksum = ivi_calc_band_checksum(band);
        if (chksum != band->checksum) {
            av_log(avctx, AV_LOG_ERROR,
                   "Band checksum mismatch! Plane %d, band %d, received: %x, calculated: %x\n",
                   band->plane, band->band_num, band->checksum, chksum);
        }
    }
#endif

    align_get_bits(&ctx->gb);

    return result;
}


/**
 *  Switches buffers.
 *
 *  @param ctx      [in,out] ptr to the decoder context
 *  @param avctx    [in] ptr to the AVCodecContext
 */
static void switch_buffers(IVI5DecContext *ctx, AVCodecContext *avctx)
{
    switch (ctx->prev_frame_type) {
    case FRAMETYPE_INTRA:
    case FRAMETYPE_INTER:
        ctx->buf_switch ^= 1;
        ctx->dst_buf = ctx->buf_switch;
        ctx->ref_buf = ctx->buf_switch ^ 1;
        break;
    case FRAMETYPE_INTER_SCAL:
        if (!ctx->inter_scal) {
            ctx->ref2_buf   = 2;
            ctx->inter_scal = 1;
        }
        FFSWAP(int, ctx->dst_buf, ctx->ref2_buf);
        ctx->ref_buf = ctx->ref2_buf;
        break;
    case FRAMETYPE_INTER_NOREF:
        break;
    }

    switch (ctx->frame_type) {
    case FRAMETYPE_INTRA:
        ctx->buf_switch = 0;
        /* FALLTHROUGH */
    case FRAMETYPE_INTER:
        ctx->inter_scal = 0;
        ctx->dst_buf = ctx->buf_switch;
        ctx->ref_buf = ctx->buf_switch ^ 1;
        break;
    case FRAMETYPE_INTER_SCAL:
    case FRAMETYPE_INTER_NOREF:
    case FRAMETYPE_NULL:
        break;
    }
}


/**
 *  Initializes Indeo5 decoder.
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    IVI5DecContext  *ctx = avctx->priv_data;
    int             result;

    ff_ivi_init_static_vlc();

    /* copy rvmap tables in our context so we can apply changes to them */
    memcpy(ctx->rvmap_tabs, ff_ivi_rvmap_tabs, sizeof(ff_ivi_rvmap_tabs));

    /* set the initial picture layout according to the basic profile:
       there is only one band per plane (no scalability), only one tile (no local decoding)
       and picture format = YVU9 */
    ctx->pic_conf.pic_width     = avctx->width;
    ctx->pic_conf.pic_height    = avctx->height;
    ctx->pic_conf.chroma_width  = (avctx->width  + 3) >> 2;
    ctx->pic_conf.chroma_height = (avctx->height + 3) >> 2;
    ctx->pic_conf.tile_width    = avctx->width;
    ctx->pic_conf.tile_height   = avctx->height;
    ctx->pic_conf.luma_bands    = ctx->pic_conf.chroma_bands = 1;

    result = ff_ivi_init_planes(ctx->planes, &ctx->pic_conf);
    if (result) {
        av_log(avctx, AV_LOG_ERROR, "Couldn't allocate color planes!\n");
        return -1;
    }

    ctx->buf_switch = 0;
    ctx->inter_scal = 0;

    avctx->pix_fmt = PIX_FMT_YUV410P;

    return 0;
}


/**
 *  main decoder function
 */
static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    IVI5DecContext  *ctx = avctx->priv_data;
    const uint8_t   *buf = avpkt->data;
    int             buf_size = avpkt->size;
    int             result, p, b;

    init_get_bits(&ctx->gb, buf, buf_size * 8);
    ctx->frame_data = buf;
    ctx->frame_size = buf_size;

    result = decode_pic_hdr(ctx, avctx);
    if (result) {
        av_log(avctx, AV_LOG_ERROR,
               "Error while decoding picture header: %d\n", result);
        return -1;
    }

    if (ctx->gop_flags & IVI5_IS_PROTECTED) {
        av_log(avctx, AV_LOG_ERROR, "Password-protected clip!\n");
        return -1;
    }

    switch_buffers(ctx, avctx);

    //START_TIMER;

    if (ctx->frame_type != FRAMETYPE_NULL) {
        for (p = 0; p < 3; p++) {
            for (b = 0; b < ctx->planes[p].num_bands; b++) {
                result = decode_band(ctx, p, &ctx->planes[p].bands[b], avctx);
                if (result) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Error while decoding band: %d, plane: %d\n", b, p);
                    return -1;
                }
            }
        }
    }

    //STOP_TIMER("decode_planes");

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

    ctx->frame.reference = 0;
    if (avctx->get_buffer(avctx, &ctx->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if (ctx->is_scalable) {
        ff_ivi_recompose53 (&ctx->planes[0], ctx->frame.data[0], ctx->frame.linesize[0], 4);
    } else {
        ff_ivi_output_plane(&ctx->planes[0], ctx->frame.data[0], ctx->frame.linesize[0]);
    }

    ff_ivi_output_plane(&ctx->planes[2], ctx->frame.data[1], ctx->frame.linesize[1]);
    ff_ivi_output_plane(&ctx->planes[1], ctx->frame.data[2], ctx->frame.linesize[2]);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = ctx->frame;

    return buf_size;
}


/**
 *  Closes Indeo5 decoder and cleans up its context.
 */
static av_cold int decode_close(AVCodecContext *avctx)
{
    IVI5DecContext *ctx = avctx->priv_data;

    ff_ivi_free_buffers(&ctx->planes[0]);

    if (ctx->mb_vlc.cust_tab.table)
        free_vlc(&ctx->mb_vlc.cust_tab);

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

    return 0;
}


AVCodec indeo5_decoder = {
    .name           = "indeo5",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_INDEO5,
    .priv_data_size = sizeof(IVI5DecContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("Intel Indeo Video Interactive 5"),
};
