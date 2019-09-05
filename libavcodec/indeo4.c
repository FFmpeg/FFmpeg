/*
 * Indeo Video Interactive v4 compatible decoder
 * Copyright (c) 2009-2011 Maxim Poliakovski
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
 * Indeo Video Interactive version 4 decoder
 *
 * Indeo 4 data is usually transported within .avi or .mov files.
 * Known FOURCCs: 'IV41'
 */

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "libavutil/imgutils.h"
#include "indeo4data.h"
#include "internal.h"
#include "ivi.h"
#include "ivi_dsp.h"

#define IVI4_PIC_SIZE_ESC   7


static const struct {
    InvTransformPtr *inv_trans;
    DCTransformPtr  *dc_trans;
    int             is_2d_trans;
} transforms[18] = {
    { ff_ivi_inverse_haar_8x8,  ff_ivi_dc_haar_2d,       1 },
    { ff_ivi_row_haar8,         ff_ivi_dc_haar_2d,       0 },
    { ff_ivi_col_haar8,         ff_ivi_dc_haar_2d,       0 },
    { ff_ivi_put_pixels_8x8,    ff_ivi_put_dc_pixel_8x8, 1 },
    { ff_ivi_inverse_slant_8x8, ff_ivi_dc_slant_2d,      1 },
    { ff_ivi_row_slant8,        ff_ivi_dc_row_slant,     1 },
    { ff_ivi_col_slant8,        ff_ivi_dc_col_slant,     1 },
    { NULL, NULL, 0 }, /* inverse DCT 8x8 */
    { NULL, NULL, 0 }, /* inverse DCT 8x1 */
    { NULL, NULL, 0 }, /* inverse DCT 1x8 */
    { ff_ivi_inverse_haar_4x4,  ff_ivi_dc_haar_2d,       1 },
    { ff_ivi_inverse_slant_4x4, ff_ivi_dc_slant_2d,      1 },
    { NULL, NULL, 0 }, /* no transform 4x4 */
    { ff_ivi_row_haar4,         ff_ivi_dc_haar_2d,       0 },
    { ff_ivi_col_haar4,         ff_ivi_dc_haar_2d,       0 },
    { ff_ivi_row_slant4,        ff_ivi_dc_row_slant,     0 },
    { ff_ivi_col_slant4,        ff_ivi_dc_col_slant,     0 },
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
static int decode_pic_hdr(IVI45DecContext *ctx, AVCodecContext *avctx)
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

    if (ctx->frame_type == IVI4_FRAMETYPE_BIDIR)
        ctx->has_b_frames = 1;

    ctx->has_transp = get_bits1(&ctx->gb);

    /* unknown bit: Mac decoder ignores this bit, XANIM returns error */
    if (get_bits1(&ctx->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Sync bit is set!\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->data_size = get_bits1(&ctx->gb) ? get_bits(&ctx->gb, 24) : 0;

    /* null frames don't contain anything else so we just return */
    if (ctx->frame_type >= IVI4_FRAMETYPE_NULL_FIRST) {
        ff_dlog(avctx, "Null frame encountered!\n");
        return 0;
    }

    /* Check key lock status. If enabled - ignore lock word.         */
    /* Usually we have to prompt the user for the password, but      */
    /* we don't do that because Indeo 4 videos can be decoded anyway */
    if (get_bits1(&ctx->gb)) {
        skip_bits_long(&ctx->gb, 32);
        ff_dlog(avctx, "Password-protected clip!\n");
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
    ctx->uses_tiling = get_bits1(&ctx->gb);
    if (ctx->uses_tiling) {
        pic_conf.tile_height = scale_tile_size(pic_conf.pic_height, get_bits(&ctx->gb, 4));
        pic_conf.tile_width  = scale_tile_size(pic_conf.pic_width,  get_bits(&ctx->gb, 4));
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
    pic_conf.chroma_bands = 0;
    if (pic_conf.luma_bands)
        pic_conf.chroma_bands = decode_plane_subdivision(&ctx->gb);

    if (av_image_check_size2(pic_conf.pic_width, pic_conf.pic_height, avctx->max_pixels, AV_PIX_FMT_YUV410P, 0, avctx) < 0) {
        av_log(avctx, AV_LOG_ERROR, "picture dimensions %d %d cannot be decoded\n",
               pic_conf.pic_width, pic_conf.pic_height);
        return AVERROR_INVALIDDATA;
    }

    ctx->is_scalable = pic_conf.luma_bands != 1 || pic_conf.chroma_bands != 1;
    if (ctx->is_scalable && (pic_conf.luma_bands != 4 || pic_conf.chroma_bands != 1)) {
        av_log(avctx, AV_LOG_ERROR, "Scalability: unsupported subdivision! Luma bands: %d, chroma bands: %d\n",
               pic_conf.luma_bands, pic_conf.chroma_bands);
        return AVERROR_INVALIDDATA;
    }

    /* check if picture layout was changed and reallocate buffers */
    if (ivi_pic_config_cmp(&pic_conf, &ctx->pic_conf)) {
        if (ff_ivi_init_planes(avctx, ctx->planes, &pic_conf, 1)) {
            av_log(avctx, AV_LOG_ERROR, "Couldn't reallocate color planes!\n");
            ctx->pic_conf.luma_bands = 0;
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
        ff_dlog(avctx, "Pic hdr extension encountered!\n");
        if (get_bits_left(&ctx->gb) < 10)
            return AVERROR_INVALIDDATA;
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
static int decode_band_hdr(IVI45DecContext *ctx, IVIBandDesc *arg_band,
                           AVCodecContext *avctx)
{
    int plane, band_num, indx, transform_id, scan_indx;
    int i;
    int quant_mat;
    IVIBandDesc temp_band, *band = &temp_band;
    memcpy(&temp_band, arg_band, sizeof(temp_band));

    plane    = get_bits(&ctx->gb, 2);
    band_num = get_bits(&ctx->gb, 4);
    if (band->plane != plane || band->band_num != band_num) {
        av_log(avctx, AV_LOG_ERROR, "Invalid band header sequence!\n");
        return AVERROR_INVALIDDATA;
    }

    band->is_empty = get_bits1(&ctx->gb);
    if (!band->is_empty) {
        int old_blk_size = band->blk_size;
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
        if (!band->is_halfpel)
            ctx->uses_fullpel = 1;

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

        if (!get_bits1(&ctx->gb) || ctx->frame_type == IVI4_FRAMETYPE_INTRA) {
            transform_id = get_bits(&ctx->gb, 5);
            if (transform_id >= FF_ARRAY_ELEMS(transforms) ||
                !transforms[transform_id].inv_trans) {
                avpriv_request_sample(avctx, "Transform %d", transform_id);
                return AVERROR_PATCHWELCOME;
            }
            if ((transform_id >= 7 && transform_id <= 9) ||
                 transform_id == 17) {
                avpriv_request_sample(avctx, "DCT transform");
                return AVERROR_PATCHWELCOME;
            }

            if (transform_id < 10 && band->blk_size < 8) {
                av_log(avctx, AV_LOG_ERROR, "wrong transform size!\n");
                return AVERROR_INVALIDDATA;
            }
            if ((transform_id >= 0 && transform_id <= 2) || transform_id == 10)
                ctx->uses_haar = 1;

            band->inv_transform = transforms[transform_id].inv_trans;
            band->dc_transform  = transforms[transform_id].dc_trans;
            band->is_2d_trans   = transforms[transform_id].is_2d_trans;

            if (transform_id < 10)
                band->transform_size = 8;
            else
                band->transform_size = 4;

            if (band->blk_size != band->transform_size) {
                av_log(avctx, AV_LOG_ERROR, "transform and block size mismatch (%d != %d)\n", band->transform_size, band->blk_size);
                return AVERROR_INVALIDDATA;
            }

            scan_indx = get_bits(&ctx->gb, 4);
            if (scan_indx == 15) {
                av_log(avctx, AV_LOG_ERROR, "Custom scan pattern encountered!\n");
                return AVERROR_INVALIDDATA;
            }
            if (scan_indx > 4 && scan_indx < 10) {
                if (band->blk_size != 4) {
                    av_log(avctx, AV_LOG_ERROR, "mismatching scan table!\n");
                    return AVERROR_INVALIDDATA;
                }
            } else if (band->blk_size != 8) {
                av_log(avctx, AV_LOG_ERROR, "mismatching scan table!\n");
                return AVERROR_INVALIDDATA;
            }

            band->scan = scan_index_to_tab[scan_indx];
            band->scan_size = band->blk_size;

            quant_mat = get_bits(&ctx->gb, 5);
            if (quant_mat == 31) {
                av_log(avctx, AV_LOG_ERROR, "Custom quant matrix encountered!\n");
                return AVERROR_INVALIDDATA;
            }
            if (quant_mat >= FF_ARRAY_ELEMS(quant_index_to_tab)) {
                avpriv_request_sample(avctx, "Quantization matrix %d",
                                      quant_mat);
                return AVERROR_INVALIDDATA;
            }
            band->quant_mat = quant_mat;
        } else {
            if (old_blk_size != band->blk_size) {
                av_log(avctx, AV_LOG_ERROR,
                       "The band block size does not match the configuration "
                       "inherited\n");
                return AVERROR_INVALIDDATA;
            }
        }
        if (quant_index_to_tab[band->quant_mat] > 4 && band->blk_size == 4) {
            av_log(avctx, AV_LOG_ERROR, "Invalid quant matrix for 4x4 block encountered!\n");
            band->quant_mat = 0;
            return AVERROR_INVALIDDATA;
        }
        if (band->scan_size != band->blk_size) {
            av_log(avctx, AV_LOG_ERROR, "mismatching scan table!\n");
            return AVERROR_INVALIDDATA;
        }
        if (band->transform_size == 8 && band->blk_size < 8) {
            av_log(avctx, AV_LOG_ERROR, "mismatching transform_size!\n");
            return AVERROR_INVALIDDATA;
        }

        /* decode block huffman codebook */
        if (!get_bits1(&ctx->gb))
            arg_band->blk_vlc.tab = ctx->blk_vlc.tab;
        else
            if (ff_ivi_dec_huff_desc(&ctx->gb, 1, IVI_BLK_HUFF,
                                     &arg_band->blk_vlc, avctx))
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

    if (!band->scan) {
        av_log(avctx, AV_LOG_ERROR, "band->scan not set\n");
        return AVERROR_INVALIDDATA;
    }

    band->blk_vlc = arg_band->blk_vlc;
    memcpy(arg_band, band, sizeof(*arg_band));

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
static int decode_mb_info(IVI45DecContext *ctx, IVIBandDesc *band,
                          IVITile *tile, AVCodecContext *avctx)
{
    int         x, y, mv_x, mv_y, mv_delta, offs, mb_offset, blks_per_mb,
                mv_scale, mb_type_bits, s;
    IVIMbInfo   *mb, *ref_mb;
    int         row_offset = band->mb_size * band->pitch;

    mb     = tile->mbs;
    ref_mb = tile->ref_mbs;
    offs   = tile->ypos * band->pitch + tile->xpos;

    blks_per_mb  = band->mb_size   != band->blk_size  ? 4 : 1;
    mb_type_bits = ctx->frame_type == IVI4_FRAMETYPE_BIDIR ? 2 : 1;

    /* scale factor for motion vectors */
    mv_scale = (ctx->planes[0].bands[0].mb_size >> 3) - (band->mb_size >> 3);
    mv_x = mv_y = 0;

    if (((tile->width + band->mb_size-1)/band->mb_size) * ((tile->height + band->mb_size-1)/band->mb_size) != tile->num_MBs) {
        av_log(avctx, AV_LOG_ERROR, "num_MBs mismatch %d %d %d %d\n", tile->width, tile->height, band->mb_size, tile->num_MBs);
        return -1;
    }

    for (y = tile->ypos; y < tile->ypos + tile->height; y += band->mb_size) {
        mb_offset = offs;

        for (x = tile->xpos; x < tile->xpos + tile->width; x += band->mb_size) {
            mb->xpos     = x;
            mb->ypos     = y;
            mb->buf_offs = mb_offset;
            mb->b_mv_x   =
            mb->b_mv_y   = 0;

            if (get_bits_left(&ctx->gb) < 1) {
                av_log(avctx, AV_LOG_ERROR, "Insufficient input for mb info\n");
                return AVERROR_INVALIDDATA;
            }

            if (get_bits1(&ctx->gb)) {
                if (ctx->frame_type == IVI4_FRAMETYPE_INTRA) {
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
                if (band->inherit_mv && ref_mb) {
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
                    /* copy mb_type from corresponding reference mb */
                    if (!ref_mb) {
                        av_log(avctx, AV_LOG_ERROR, "ref_mb unavailable\n");
                        return AVERROR_INVALIDDATA;
                    }
                    mb->type = ref_mb->type;
                } else if (ctx->frame_type == IVI4_FRAMETYPE_INTRA ||
                           ctx->frame_type == IVI4_FRAMETYPE_INTRA1) {
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
                        if (ref_mb)
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
                        if (mb->type == 3) {
                            mv_delta = get_vlc2(&ctx->gb,
                                                ctx->mb_vlc.tab->table,
                                                IVI_VLC_BITS, 1);
                            mv_y += IVI_TOSIGNED(mv_delta);
                            mv_delta = get_vlc2(&ctx->gb,
                                                ctx->mb_vlc.tab->table,
                                                IVI_VLC_BITS, 1);
                            mv_x += IVI_TOSIGNED(mv_delta);
                            mb->b_mv_x = -mv_x;
                            mb->b_mv_y = -mv_y;
                        }
                    }
                    if (mb->type == 2) {
                        mb->b_mv_x = -mb->mv_x;
                        mb->b_mv_y = -mb->mv_y;
                        mb->mv_x = 0;
                        mb->mv_y = 0;
                    }
                }
            }

            s= band->is_halfpel;
            if (mb->type)
            if ( x +  (mb->mv_x   >>s) +                 (y+               (mb->mv_y   >>s))*band->pitch < 0 ||
                 x + ((mb->mv_x+s)>>s) + band->mb_size - 1
                   + (y+band->mb_size - 1 +((mb->mv_y+s)>>s))*band->pitch > band->bufsize -1) {
                av_log(avctx, AV_LOG_ERROR, "motion vector %d %d outside reference\n", x*s + mb->mv_x, y*s + mb->mv_y);
                return AVERROR_INVALIDDATA;
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
 *  Rearrange decoding and reference buffers.
 *
 *  @param[in,out] ctx       pointer to the decoder context
 */
static void switch_buffers(IVI45DecContext *ctx)
{
    int is_prev_ref = 0, is_ref = 0;

    switch (ctx->prev_frame_type) {
    case IVI4_FRAMETYPE_INTRA:
    case IVI4_FRAMETYPE_INTRA1:
    case IVI4_FRAMETYPE_INTER:
        is_prev_ref = 1;
        break;
    }

    switch (ctx->frame_type) {
    case IVI4_FRAMETYPE_INTRA:
    case IVI4_FRAMETYPE_INTRA1:
    case IVI4_FRAMETYPE_INTER:
        is_ref = 1;
        break;
    }

    if (is_prev_ref && is_ref) {
        FFSWAP(int, ctx->dst_buf, ctx->ref_buf);
    } else if (is_prev_ref) {
        FFSWAP(int, ctx->ref_buf, ctx->b_ref_buf);
        FFSWAP(int, ctx->dst_buf, ctx->ref_buf);
    }
}


static int is_nonnull_frame(IVI45DecContext *ctx)
{
    return ctx->frame_type < IVI4_FRAMETYPE_NULL_FIRST;
}


static av_cold int decode_init(AVCodecContext *avctx)
{
    IVI45DecContext *ctx = avctx->priv_data;

    ff_ivi_init_static_vlc();

    /* copy rvmap tables in our context so we can apply changes to them */
    memcpy(ctx->rvmap_tabs, ff_ivi_rvmap_tabs, sizeof(ff_ivi_rvmap_tabs));

    /* Force allocation of the internal buffers */
    /* during picture header decoding.          */
    ctx->pic_conf.pic_width  = 0;
    ctx->pic_conf.pic_height = 0;

    avctx->pix_fmt = AV_PIX_FMT_YUV410P;

    ctx->decode_pic_hdr   = decode_pic_hdr;
    ctx->decode_band_hdr  = decode_band_hdr;
    ctx->decode_mb_info   = decode_mb_info;
    ctx->switch_buffers   = switch_buffers;
    ctx->is_nonnull_frame = is_nonnull_frame;

    ctx->is_indeo4 = 1;
    ctx->show_indeo4_info = 1;

    ctx->dst_buf   = 0;
    ctx->ref_buf   = 1;
    ctx->b_ref_buf = 3; /* buffer 2 is used for scalability mode */
    ctx->p_frame = av_frame_alloc();
    if (!ctx->p_frame)
        return AVERROR(ENOMEM);

    return 0;
}


AVCodec ff_indeo4_decoder = {
    .name           = "indeo4",
    .long_name      = NULL_IF_CONFIG_SMALL("Intel Indeo Video Interactive 4"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_INDEO4,
    .priv_data_size = sizeof(IVI45DecContext),
    .init           = decode_init,
    .close          = ff_ivi_decode_close,
    .decode         = ff_ivi_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
