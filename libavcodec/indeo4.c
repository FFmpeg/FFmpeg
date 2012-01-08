/*
 * Indeo Video Interactive v4 compatible decoder
 * Copyright (c) 2009-2011 Maxim Poliakovski
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

/**
 * @file
 * Indeo Video Interactive version 4 decoder
 *
 * Indeo 4 data is usually transported within .avi or .mov files.
 * Known FOURCCs: 'IV41'
 */

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "ivi_dsp.h"
#include "ivi_common.h"
#include "indeo4data.h"

#define IVI4_STREAM_ANALYSER    0
#define IVI4_DEBUG_CHECKSUM     0

/**
 *  Indeo 4 frame types.
 */
enum {
    FRAMETYPE_INTRA       = 0,
    FRAMETYPE_BIDIR1      = 1,  ///< bidirectional frame
    FRAMETYPE_INTER       = 2,  ///< non-droppable P-frame
    FRAMETYPE_BIDIR       = 3,  ///< bidirectional frame
    FRAMETYPE_INTER_NOREF = 4,  ///< droppable P-frame
    FRAMETYPE_NULL_FIRST  = 5,  ///< empty frame with no data
    FRAMETYPE_NULL_LAST   = 6   ///< empty frame with no data
};

#define IVI4_PIC_SIZE_ESC   7


typedef struct {
    GetBitContext   gb;
    AVFrame         frame;
    RVMapDesc       rvmap_tabs[9];   ///< local corrected copy of the static rvmap tables

    uint32_t        frame_num;
    int             frame_type;
    int             prev_frame_type; ///< frame type of the previous frame
    uint32_t        data_size;       ///< size of the frame data in bytes from picture header
    int             is_scalable;
    int             transp_status;   ///< transparency mode status: 1 - enabled

    IVIPicConfig    pic_conf;
    IVIPlaneDesc    planes[3];       ///< color planes

    int             buf_switch;      ///< used to switch between three buffers
    int             dst_buf;         ///< buffer index for the currently decoded frame
    int             ref_buf;         ///< inter frame reference buffer index

    IVIHuffTab      mb_vlc;          ///< current macroblock table descriptor
    IVIHuffTab      blk_vlc;         ///< current block table descriptor

    uint16_t        checksum;        ///< frame checksum

    uint8_t         rvmap_sel;
    uint8_t         in_imf;
    uint8_t         in_q;            ///< flag for explicitly stored quantiser delta
    uint8_t         pic_glob_quant;
    uint8_t         unknown1;

#if IVI4_STREAM_ANALYSER
    uint8_t         has_b_frames;
    uint8_t         has_transp;
    uint8_t         uses_tiling;
    uint8_t         uses_haar;
    uint8_t         uses_fullpel;
#endif
} IVI4DecContext;


static const struct {
    InvTransformPtr *inv_trans;
    DCTransformPtr  *dc_trans;
    int             is_2d_trans;
} transforms[18] = {
    { ff_ivi_inverse_haar_8x8,  ff_ivi_dc_haar_2d,       1 },
    { NULL, NULL, 0 }, /* inverse Haar 8x1 */
    { NULL, NULL, 0 }, /* inverse Haar 1x8 */
    { ff_ivi_put_pixels_8x8,    ff_ivi_put_dc_pixel_8x8, 1 },
    { ff_ivi_inverse_slant_8x8, ff_ivi_dc_slant_2d,      1 },
    { ff_ivi_row_slant8,        ff_ivi_dc_row_slant,     1 },
    { ff_ivi_col_slant8,        ff_ivi_dc_col_slant,     1 },
    { NULL, NULL, 0 }, /* inverse DCT 8x8 */
    { NULL, NULL, 0 }, /* inverse DCT 8x1 */
    { NULL, NULL, 0 }, /* inverse DCT 1x8 */
    { NULL, NULL, 0 }, /* inverse Haar 4x4 */
    { ff_ivi_inverse_slant_4x4, ff_ivi_dc_slant_2d,      1 },
    { NULL, NULL, 0 }, /* no transform 4x4 */
    { NULL, NULL, 0 }, /* inverse Haar 1x4 */
    { NULL, NULL, 0 }, /* inverse Haar 4x1 */
    { NULL, NULL, 0 }, /* inverse slant 1x4 */
    { NULL, NULL, 0 }, /* inverse slant 4x1 */
    { NULL, NULL, 0 }, /* inverse DCT 4x4 */
};

/**
 *  Decode subdivision of a plane.
 *  This is a simplified version that checks for two supported subdivisions:
 *  - 1 wavelet band  per plane, size factor 1:1, code pattern: 3
 *  - 4 wavelet bands per plane, size factor 1:4, code pattern: 2,3,3,3,3
 *  Anything else is either unsupported or corrupt.
 *
 *  @param[in,out] gb    the GetBit context
 *  @return        number of wavelet bands or 0 on error
 */
static int decode_plane_subdivision(GetBitContext *gb)
{
    int i;

    switch (get_bits(gb, 2)) {
    case 3:
        return 1;
    case 2:
        for (i = 0; i < 4; i++)
            if (get_bits(gb, 2) != 3)
                return 0;
        return 4;
    default:
        return 0;
    }
}

static inline int scale_tile_size(int def_size, int size_factor)
{
    return size_factor == 15 ? def_size : (size_factor + 1) << 5;
}

/**
 *  Decode Indeo 4 picture header.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 *  @param[in]     avctx     pointer to the AVCodecContext
 *  @return        result code: 0 = OK, negative number = error
 */
static int decode_pic_hdr(IVI4DecContext *ctx, AVCodecContext *avctx)
{
    int             pic_size_indx, i, p;
    IVIPicConfig    pic_conf;

    if (get_bits(&ctx->gb, 18) != 0x3FFF8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid picture start code!\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->prev_frame_type = ctx->frame_type;
    ctx->frame_type      = get_bits(&ctx->gb, 3);
    if (ctx->frame_type == 7) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame type: %d\n", ctx->frame_type);
        return AVERROR_INVALIDDATA;
    }

#if IVI4_STREAM_ANALYSER
    if (   ctx->frame_type == FRAMETYPE_BIDIR1
        || ctx->frame_type == FRAMETYPE_BIDIR)
        ctx->has_b_frames = 1;
#endif

    ctx->transp_status = get_bits1(&ctx->gb);
#if IVI4_STREAM_ANALYSER
    if (ctx->transp_status) {
        ctx->has_transp = 1;
    }
#endif

    /* unknown bit: Mac decoder ignores this bit, XANIM returns error */
    if (get_bits1(&ctx->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Sync bit is set!\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->data_size = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 24) : 0;

    /* null frames don't contain anything else so we just return */
    if (ctx->frame_type >= FRAMETYPE_NULL_FIRST) {
        av_dlog(avctx, "Null frame encountered!\n");
        return 0;
    }

    /* Check key lock status. If enabled - ignore lock word.         */
    /* Usually we have to prompt the user for the password, but      */
    /* we don't do that because Indeo 4 videos can be decoded anyway */
    if (get_bits1(&ctx->gb)) {
        skip_bits_long(&ctx->gb, 32);
        av_dlog(avctx, "Password-protected clip!\n");
    }

    pic_size_indx = get_bits(&ctx->gb, 3);
    if (pic_size_indx == IVI4_PIC_SIZE_ESC) {
        pic_conf.pic_height = get_bits(&ctx->gb, 16);
        pic_conf.pic_width  = get_bits(&ctx->gb, 16);
    } else {
        pic_conf.pic_height = ivi4_common_pic_sizes[pic_size_indx * 2 + 1];
        pic_conf.pic_width  = ivi4_common_pic_sizes[pic_size_indx * 2    ];
    }

    /* Decode tile dimensions. */
    if (get_bits1(&ctx->gb)) {
        pic_conf.tile_height = scale_tile_size(pic_conf.pic_height, get_bits(&ctx->gb, 4));
        pic_conf.tile_width  = scale_tile_size(pic_conf.pic_width,  get_bits(&ctx->gb, 4));
#if IVI4_STREAM_ANALYSER
        ctx->uses_tiling = 1;
#endif
    } else {
        pic_conf.tile_height = pic_conf.pic_height;
        pic_conf.tile_width  = pic_conf.pic_width;
    }

    /* Decode chroma subsampling. We support only 4:4 aka YVU9. */
    if (get_bits(&ctx->gb, 2)) {
        av_log(avctx, AV_LOG_ERROR, "Only YVU9 picture format is supported!\n");
        return AVERROR_INVALIDDATA;
    }
    pic_conf.chroma_height = (pic_conf.pic_height + 3) >> 2;
    pic_conf.chroma_width  = (pic_conf.pic_width  + 3) >> 2;

    /* decode subdivision of the planes */
    pic_conf.luma_bands = decode_plane_subdivision(&ctx->gb);
    if (pic_conf.luma_bands)
        pic_conf.chroma_bands = decode_plane_subdivision(&ctx->gb);
    ctx->is_scalable = pic_conf.luma_bands != 1 || pic_conf.chroma_bands != 1;
    if (ctx->is_scalable && (pic_conf.luma_bands != 4 || pic_conf.chroma_bands != 1)) {
        av_log(avctx, AV_LOG_ERROR, "Scalability: unsupported subdivision! Luma bands: %d, chroma bands: %d\n",
               pic_conf.luma_bands, pic_conf.chroma_bands);
        return AVERROR_INVALIDDATA;
    }

    /* check if picture layout was changed and reallocate buffers */
    if (ivi_pic_config_cmp(&pic_conf, &ctx->pic_conf)) {
        if (ff_ivi_init_planes(ctx->planes, &pic_conf)) {
            av_log(avctx, AV_LOG_ERROR, "Couldn't reallocate color planes!\n");
            return AVERROR(ENOMEM);
        }

        ctx->pic_conf = pic_conf;

        /* set default macroblock/block dimensions */
        for (p = 0; p <= 2; p++) {
            for (i = 0; i < (!p ? pic_conf.luma_bands : pic_conf.chroma_bands); i++) {
                ctx->planes[p].bands[i].mb_size  = !p ? (!ctx->is_scalable ? 16 : 8) : 4;
                ctx->planes[p].bands[i].blk_size = !p ? 8 : 4;
            }
        }

        if (ff_ivi_init_tiles(ctx->planes, ctx->pic_conf.tile_width,
                              ctx->pic_conf.tile_height)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Couldn't reallocate internal structures!\n");
            return AVERROR(ENOMEM);
        }
    }

    ctx->frame_num = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 20) : 0;

    /* skip decTimeEst field if present */
    if (get_bits1(&ctx->gb))
        skip_bits(&ctx->gb, 8);

    /* decode macroblock and block huffman codebooks */
    if (ff_ivi_dec_huff_desc(&ctx->gb, get_bits1(&ctx->gb), IVI_MB_HUFF,  &ctx->mb_vlc,  avctx) ||
        ff_ivi_dec_huff_desc(&ctx->gb, get_bits1(&ctx->gb), IVI_BLK_HUFF, &ctx->blk_vlc, avctx))
        return AVERROR_INVALIDDATA;

    ctx->rvmap_sel = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 3) : 8;

    ctx->in_imf = get_bits1(&ctx->gb);
    ctx->in_q   = get_bits1(&ctx->gb);

    ctx->pic_glob_quant = get_bits(&ctx->gb, 5);

    /* TODO: ignore this parameter if unused */
    ctx->unknown1 = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 3) : 0;

    ctx->checksum = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 16) : 0;

    /* skip picture header extension if any */
    while (get_bits1(&ctx->gb)) {
        av_dlog(avctx, "Pic hdr extension encountered!\n");
        skip_bits(&ctx->gb, 8);
    }

    if (get_bits1(&ctx->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Bad blocks bits encountered!\n");
    }

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Decode Indeo 4 band header.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 *  @param[in,out] band      pointer to the band descriptor
 *  @param[in]     avctx     pointer to the AVCodecContext
 *  @return        result code: 0 = OK, negative number = error
 */
static int decode_band_hdr(IVI4DecContext *ctx, IVIBandDesc *band,
                           AVCodecContext *avctx)
{
    int plane, band_num, indx, transform_id, scan_indx;
    int i;

    plane    = get_bits(&ctx->gb, 2);
    band_num = get_bits(&ctx->gb, 4);
    if (band->plane != plane || band->band_num != band_num) {
        av_log(avctx, AV_LOG_ERROR, "Invalid band header sequence!\n");
        return AVERROR_INVALIDDATA;
    }

    band->is_empty = get_bits1(&ctx->gb);
    if (!band->is_empty) {
        /* skip header size
         * If header size is not given, header size is 4 bytes. */
        if (get_bits1(&ctx->gb))
            skip_bits(&ctx->gb, 16);

        band->is_halfpel = get_bits(&ctx->gb, 2);
        if (band->is_halfpel >= 2) {
            av_log(avctx, AV_LOG_ERROR, "Invalid/unsupported mv resolution: %d!\n",
                   band->is_halfpel);
            return AVERROR_INVALIDDATA;
        }
#if IVI4_STREAM_ANALYSER
        if (!band->is_halfpel)
            ctx->uses_fullpel = 1;
#endif

        band->checksum_present = get_bits1(&ctx->gb);
        if (band->checksum_present)
            band->checksum = get_bits(&ctx->gb, 16);

        indx = get_bits(&ctx->gb, 2);
        if (indx == 3) {
            av_log(avctx, AV_LOG_ERROR, "Invalid block size!\n");
            return AVERROR_INVALIDDATA;
        }
        band->mb_size  = 16 >> indx;
        band->blk_size = 8 >> (indx >> 1);

        band->inherit_mv     = get_bits1(&ctx->gb);
        band->inherit_qdelta = get_bits1(&ctx->gb);

        band->glob_quant = get_bits(&ctx->gb, 5);

        if (!get_bits1(&ctx->gb) || ctx->frame_type == FRAMETYPE_INTRA) {
            transform_id = get_bits(&ctx->gb, 5);
            if (!transforms[transform_id].inv_trans) {
                av_log_ask_for_sample(avctx, "Unimplemented transform: %d!\n", transform_id);
                return AVERROR_PATCHWELCOME;
            }
            if ((transform_id >= 7 && transform_id <= 9) ||
                 transform_id == 17) {
                av_log_ask_for_sample(avctx, "DCT transform not supported yet!\n");
                return AVERROR_PATCHWELCOME;
            }

#if IVI4_STREAM_ANALYSER
            if ((transform_id >= 0 && transform_id <= 2) || transform_id == 10)
                ctx->uses_haar = 1;
#endif

            band->inv_transform = transforms[transform_id].inv_trans;
            band->dc_transform  = transforms[transform_id].dc_trans;
            band->is_2d_trans   = transforms[transform_id].is_2d_trans;

            scan_indx = get_bits(&ctx->gb, 4);
            if (scan_indx == 15) {
                av_log(avctx, AV_LOG_ERROR, "Custom scan pattern encountered!\n");
                return AVERROR_INVALIDDATA;
            }
            band->scan = scan_index_to_tab[scan_indx];

            band->quant_mat = get_bits(&ctx->gb, 5);
            if (band->quant_mat == 31) {
                av_log(avctx, AV_LOG_ERROR, "Custom quant matrix encountered!\n");
                return AVERROR_INVALIDDATA;
            }
        }

        /* decode block huffman codebook */
        if (ff_ivi_dec_huff_desc(&ctx->gb, get_bits1(&ctx->gb), IVI_BLK_HUFF,
                                 &band->blk_vlc, avctx))
            return AVERROR_INVALIDDATA;

        /* select appropriate rvmap table for this band */
        band->rvmap_sel = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 3) : 8;

        /* decode rvmap probability corrections if any */
        band->num_corr = 0; /* there is no corrections */
        if (get_bits1(&ctx->gb)) {
            band->num_corr = get_bits(&ctx->gb, 8); /* get number of correction pairs */
            if (band->num_corr > 61) {
                av_log(avctx, AV_LOG_ERROR, "Too many corrections: %d\n",
                       band->num_corr);
                return AVERROR_INVALIDDATA;
            }

            /* read correction pairs */
            for (i = 0; i < band->num_corr * 2; i++)
                band->corr[i] = get_bits(&ctx->gb, 8);
        }
    }

    if (band->blk_size == 8) {
        band->intra_base = &ivi4_quant_8x8_intra[quant_index_to_tab[band->quant_mat]][0];
        band->inter_base = &ivi4_quant_8x8_inter[quant_index_to_tab[band->quant_mat]][0];
    } else {
        band->intra_base = &ivi4_quant_4x4_intra[quant_index_to_tab[band->quant_mat]][0];
        band->inter_base = &ivi4_quant_4x4_inter[quant_index_to_tab[band->quant_mat]][0];
    }

    /* Indeo 4 doesn't use scale tables */
    band->intra_scale = NULL;
    band->inter_scale = NULL;

    align_get_bits(&ctx->gb);

    return 0;
}


/**
 *  Decode information (block type, cbp, quant delta, motion vector)
 *  for all macroblocks in the current tile.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 *  @param[in,out] band      pointer to the band descriptor
 *  @param[in,out] tile      pointer to the tile descriptor
 *  @param[in]     avctx     pointer to the AVCodecContext
 *  @return        result code: 0 = OK, negative number = error
 */
static int decode_mb_info(IVI4DecContext *ctx, IVIBandDesc *band,
                          IVITile *tile, AVCodecContext *avctx)
{
    int         x, y, mv_x, mv_y, mv_delta, offs, mb_offset, blks_per_mb,
                mv_scale, mb_type_bits;
    IVIMbInfo   *mb, *ref_mb;
    int         row_offset = band->mb_size * band->pitch;

    mb     = tile->mbs;
    ref_mb = tile->ref_mbs;
    offs   = tile->ypos * band->pitch + tile->xpos;

    blks_per_mb  = band->mb_size   != band->blk_size  ? 4 : 1;
    mb_type_bits = ctx->frame_type == FRAMETYPE_BIDIR ? 2 : 1;

    /* scale factor for motion vectors */
    mv_scale = (ctx->planes[0].bands[0].mb_size >> 3) - (band->mb_size >> 3);
    mv_x = mv_y = 0;

    for (y = tile->ypos; y < tile->ypos + tile->height; y += band->mb_size) {
        mb_offset = offs;

        for (x = tile->xpos; x < tile->xpos + tile->width; x += band->mb_size) {
            mb->xpos     = x;
            mb->ypos     = y;
            mb->buf_offs = mb_offset;

            if (get_bits1(&ctx->gb)) {
                if (ctx->frame_type == FRAMETYPE_INTRA) {
                    av_log(avctx, AV_LOG_ERROR, "Empty macroblock in an INTRA picture!\n");
                    return AVERROR_INVALIDDATA;
                }
                mb->type = 1; /* empty macroblocks are always INTER */
                mb->cbp  = 0; /* all blocks are empty */

                mb->q_delta = 0;
                if (!band->plane && !band->band_num && ctx->in_q) {
                    mb->q_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                           IVI_VLC_BITS, 1);
                    mb->q_delta = IVI_TOSIGNED(mb->q_delta);
                }

                mb->mv_x = mb->mv_y = 0; /* no motion vector coded */
                if (band->inherit_mv) {
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
                    mb->type = get_bits(&ctx->gb, mb_type_bits);
                }

                mb->cbp = get_bits(&ctx->gb, blks_per_mb);

                mb->q_delta = 0;
                if (band->inherit_qdelta) {
                    if (ref_mb) mb->q_delta = ref_mb->q_delta;
                } else if (mb->cbp || (!band->plane && !band->band_num &&
                           ctx->in_q)) {
                    mb->q_delta = get_vlc2(&ctx->gb, ctx->mb_vlc.tab->table,
                                           IVI_VLC_BITS, 1);
                    mb->q_delta = IVI_TOSIGNED(mb->q_delta);
                }

                if (!mb->type) {
                    mb->mv_x = mb->mv_y = 0; /* there is no motion vector in intra-macroblocks */
                } else {
                    if (band->inherit_mv) {
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
 *  Decode an Indeo 4 band.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 *  @param[in,out] band      pointer to the band descriptor
 *  @param[in]     avctx     pointer to the AVCodecContext
 *  @return        result code: 0 = OK, negative number = error
 */
static int decode_band(IVI4DecContext *ctx, int plane_num,
                       IVIBandDesc *band, AVCodecContext *avctx)
{
    int         result, i, t, pos, idx1, idx2;
    IVITile     *tile;

    band->buf     = band->bufs[ctx->dst_buf];
    band->ref_buf = band->bufs[ctx->ref_buf];

    result = decode_band_hdr(ctx, band, avctx);
    if (result) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding band header\n");
        return result;
    }

    if (band->is_empty) {
        av_log(avctx, AV_LOG_ERROR, "Empty band encountered!\n");
        return AVERROR_INVALIDDATA;
    }

    band->rv_map = &ctx->rvmap_tabs[band->rvmap_sel];

    /* apply corrections to the selected rvmap table if present */
    for (i = 0; i < band->num_corr; i++) {
        idx1 = band->corr[i * 2];
        idx2 = band->corr[i * 2 + 1];
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
            av_dlog(avctx, "Empty tile encountered!\n");
        } else {
            tile->data_size = ff_ivi_dec_tile_data_size(&ctx->gb);
            if (!tile->data_size) {
                av_log(avctx, AV_LOG_ERROR, "Tile data size is zero!\n");
                return AVERROR_INVALIDDATA;
            }

            result = decode_mb_info(ctx, band, tile, avctx);
            if (result < 0)
                break;

            result = ff_ivi_decode_blocks(&ctx->gb, band, tile);
            if (result < 0 || ((get_bits_count(&ctx->gb) - pos) >> 3) != tile->data_size) {
                av_log(avctx, AV_LOG_ERROR, "Corrupted tile data encountered!\n");
                break;
            }

            pos += tile->data_size << 3; // skip to next tile
        }
    }

    /* restore the selected rvmap table by applying its corrections in reverse order */
    for (i = band->num_corr - 1; i >= 0; i--) {
        idx1 = band->corr[i * 2];
        idx2 = band->corr[i * 2 + 1];
        FFSWAP(uint8_t, band->rv_map->runtab[idx1], band->rv_map->runtab[idx2]);
        FFSWAP(int16_t, band->rv_map->valtab[idx1], band->rv_map->valtab[idx2]);
    }

#if defined(DEBUG) && IVI4_DEBUG_CHECKSUM
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

    return 0;
}


static av_cold int decode_init(AVCodecContext *avctx)
{
    IVI4DecContext *ctx = avctx->priv_data;

    ff_ivi_init_static_vlc();

    /* copy rvmap tables in our context so we can apply changes to them */
    memcpy(ctx->rvmap_tabs, ff_ivi_rvmap_tabs, sizeof(ff_ivi_rvmap_tabs));

    /* Force allocation of the internal buffers */
    /* during picture header decoding.          */
    ctx->pic_conf.pic_width  = 0;
    ctx->pic_conf.pic_height = 0;

    avctx->pix_fmt = PIX_FMT_YUV410P;

    return 0;
}


/**
 *  Rearrange decoding and reference buffers.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 */
static void switch_buffers(IVI4DecContext *ctx)
{
    switch (ctx->prev_frame_type) {
    case FRAMETYPE_INTRA:
    case FRAMETYPE_INTER:
        ctx->buf_switch ^= 1;
        ctx->dst_buf     = ctx->buf_switch;
        ctx->ref_buf     = ctx->buf_switch ^ 1;
        break;
    case FRAMETYPE_INTER_NOREF:
        break;
    }

    switch (ctx->frame_type) {
    case FRAMETYPE_INTRA:
        ctx->buf_switch = 0;
        /* FALLTHROUGH */
    case FRAMETYPE_INTER:
        ctx->dst_buf = ctx->buf_switch;
        ctx->ref_buf = ctx->buf_switch ^ 1;
        break;
    case FRAMETYPE_INTER_NOREF:
    case FRAMETYPE_NULL_FIRST:
    case FRAMETYPE_NULL_LAST:
        break;
    }
}


static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt)
{
    IVI4DecContext  *ctx = avctx->priv_data;
    const uint8_t   *buf = avpkt->data;
    int             buf_size = avpkt->size;
    int             result, p, b;

    init_get_bits(&ctx->gb, buf, buf_size * 8);

    result = decode_pic_hdr(ctx, avctx);
    if (result) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding picture header\n");
        return result;
    }

    switch_buffers(ctx);

    if (ctx->frame_type < FRAMETYPE_NULL_FIRST) {
        for (p = 0; p < 3; p++) {
            for (b = 0; b < ctx->planes[p].num_bands; b++) {
                result = decode_band(ctx, p, &ctx->planes[p].bands[b], avctx);
                if (result) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Error decoding band: %d, plane: %d\n", b, p);
                    return result;
                }
            }
        }
    }

    /* If the bidirectional mode is enabled, next I and the following P frame will */
    /* be sent together. Unfortunately the approach below seems to be the only way */
    /* to handle the B-frames mode. That's exactly the same Intel decoders do.     */
    if (ctx->frame_type == FRAMETYPE_INTRA) {
        while (get_bits(&ctx->gb, 8)); // skip version string
        skip_bits_long(&ctx->gb, 64);  // skip padding, TODO: implement correct 8-bytes alignment
        if (get_bits_left(&ctx->gb) > 18 && show_bits(&ctx->gb, 18) == 0x3FFF8)
            av_log(avctx, AV_LOG_ERROR, "Buffer contains IP frames!\n");
    }

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

    ctx->frame.reference = 0;
    if ((result = avctx->get_buffer(avctx, &ctx->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return result;
    }

    if (ctx->is_scalable) {
        ff_ivi_recompose_haar(&ctx->planes[0], ctx->frame.data[0], ctx->frame.linesize[0], 4);
    } else {
        ff_ivi_output_plane(&ctx->planes[0], ctx->frame.data[0], ctx->frame.linesize[0]);
    }

    ff_ivi_output_plane(&ctx->planes[2], ctx->frame.data[1], ctx->frame.linesize[1]);
    ff_ivi_output_plane(&ctx->planes[1], ctx->frame.data[2], ctx->frame.linesize[2]);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = ctx->frame;

    return buf_size;
}


static av_cold int decode_close(AVCodecContext *avctx)
{
    IVI4DecContext *ctx = avctx->priv_data;

    ff_ivi_free_buffers(&ctx->planes[0]);

    if (ctx->frame.data[0])
        avctx->release_buffer(avctx, &ctx->frame);

#if IVI4_STREAM_ANALYSER
    if (ctx->is_scalable)
        av_log(avctx, AV_LOG_ERROR, "This video uses scalability mode!\n");
    if (ctx->uses_tiling)
        av_log(avctx, AV_LOG_ERROR, "This video uses local decoding!\n");
    if (ctx->has_b_frames)
        av_log(avctx, AV_LOG_ERROR, "This video contains B-frames!\n");
    if (ctx->has_transp)
        av_log(avctx, AV_LOG_ERROR, "Transparency mode is enabled!\n");
    if (ctx->uses_haar)
        av_log(avctx, AV_LOG_ERROR, "This video uses Haar transform!\n");
    if (ctx->uses_fullpel)
        av_log(avctx, AV_LOG_ERROR, "This video uses fullpel motion vectors!\n");
#endif

    return 0;
}


AVCodec ff_indeo4_decoder = {
    .name           = "indeo4",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_INDEO4,
    .priv_data_size = sizeof(IVI4DecContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("Intel Indeo Video Interactive 4"),
};
