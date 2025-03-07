/*
 * Copyright (c) 2023 Intel Corporation
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

#include <va/va.h>
#include <va/va_enc_av1.h>

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/mastering_display_metadata.h"

#include "cbs_av1.h"
#include "put_bits.h"
#include "codec_internal.h"
#include "av1_levels.h"
#include "vaapi_encode.h"

#define AV1_MAX_QUANT 255

typedef struct VAAPIEncodeAV1Picture {
    int64_t last_idr_frame;
    int slot;
} VAAPIEncodeAV1Picture;

typedef struct VAAPIEncodeAV1Context {
    VAAPIEncodeContext common;
    AV1RawOBU sh; /**< sequence header.*/
    AV1RawOBU fh; /**< frame header.*/
    AV1RawOBU mh[4]; /**< metadata header.*/
    int nb_mh;
    CodedBitstreamContext *cbc;
    CodedBitstreamFragment current_obu;
    VAConfigAttribValEncAV1 attr;
    VAConfigAttribValEncAV1Ext1 attr_ext1;
    VAConfigAttribValEncAV1Ext2 attr_ext2;

    char sh_data[MAX_PARAM_BUFFER_SIZE]; /**< coded sequence header data. */
    size_t sh_data_len; /**< bit length of sh_data. */
    char fh_data[MAX_PARAM_BUFFER_SIZE]; /**< coded frame header data. */
    size_t fh_data_len; /**< bit length of fh_data. */

    uint8_t uniform_tile;
    uint8_t use_128x128_superblock;
    int sb_cols;
    int sb_rows;
    int tile_cols_log2;
    int tile_rows_log2;
    int max_tile_width_sb;
    int max_tile_height_sb;
    uint8_t width_in_sbs_minus_1[AV1_MAX_TILE_COLS];
    uint8_t height_in_sbs_minus_1[AV1_MAX_TILE_ROWS];

    int min_log2_tile_cols;
    int max_log2_tile_cols;
    int min_log2_tile_rows;
    int max_log2_tile_rows;

    int q_idx_idr;
    int q_idx_p;
    int q_idx_b;

    /** bit positions in current frame header */
    int qindex_offset;
    int loopfilter_offset;
    int cdef_start_offset;
    int cdef_param_size;

    /** user options */
    int profile;
    int level;
    int tier;
    int tile_cols, tile_rows;
    int tile_groups;
} VAAPIEncodeAV1Context;

static void vaapi_encode_av1_trace_write_log(void *ctx,
                                             PutBitContext *pbc, int length,
                                             const char *str, const int *subscripts,
                                             int64_t value)
{
    VAAPIEncodeAV1Context *priv = ctx;
    int position;

    position = put_bits_count(pbc);
    av_assert0(position >= length);

    if (!strcmp(str, "base_q_idx"))
        priv->qindex_offset = position - length;
    else if (!strcmp(str, "loop_filter_level[0]"))
        priv->loopfilter_offset = position - length;
    else if (!strcmp(str, "cdef_damping_minus_3"))
        priv->cdef_start_offset = position - length;
    else if (!strcmp(str, "cdef_uv_sec_strength[i]"))
        priv->cdef_param_size = position - priv->cdef_start_offset;
}

static av_cold int vaapi_encode_av1_get_encoder_caps(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    VAAPIEncodeAV1Context     *priv = avctx->priv_data;

    // Surfaces must be aligned to superblock boundaries.
    base_ctx->surface_width  = FFALIGN(avctx->width,  priv->use_128x128_superblock ? 128 : 64);
    base_ctx->surface_height = FFALIGN(avctx->height, priv->use_128x128_superblock ? 128 : 64);

    return 0;
}

static av_cold int vaapi_encode_av1_configure(AVCodecContext *avctx)
{
    VAAPIEncodeContext     *ctx = avctx->priv_data;
    VAAPIEncodeAV1Context *priv = avctx->priv_data;
    int ret;

    ret = ff_cbs_init(&priv->cbc, AV_CODEC_ID_AV1, avctx);
    if (ret < 0)
        return ret;
    priv->cbc->trace_enable  = 1;
    priv->cbc->trace_level   = AV_LOG_DEBUG;
    priv->cbc->trace_context = ctx;
    priv->cbc->trace_write_callback = vaapi_encode_av1_trace_write_log;

    if (ctx->rc_mode->quality) {
        priv->q_idx_p = av_clip(ctx->rc_quality, 0, AV1_MAX_QUANT);
        if (fabs(avctx->i_quant_factor) > 0.0)
            priv->q_idx_idr =
                av_clip((fabs(avctx->i_quant_factor) * priv->q_idx_p  +
                         avctx->i_quant_offset) + 0.5,
                        0, AV1_MAX_QUANT);
        else
            priv->q_idx_idr = priv->q_idx_p;

        if (fabs(avctx->b_quant_factor) > 0.0)
            priv->q_idx_b =
                av_clip((fabs(avctx->b_quant_factor) * priv->q_idx_p  +
                         avctx->b_quant_offset) + 0.5,
                        0, AV1_MAX_QUANT);
        else
            priv->q_idx_b = priv->q_idx_p;
    } else {
        /** Arbitrary value */
        priv->q_idx_idr = priv->q_idx_p = priv->q_idx_b = 128;
    }

    ctx->roi_quant_range = AV1_MAX_QUANT;

    return 0;
}

static int vaapi_encode_av1_add_obu(AVCodecContext *avctx,
                                    CodedBitstreamFragment *au,
                                    uint8_t type,
                                    void *obu_unit)
{
    int ret;

    ret = ff_cbs_insert_unit_content(au, -1,
                                     type, obu_unit, NULL);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to add OBU unit: "
               "type = %d.\n", type);
        return ret;
    }

    return 0;
}

static int vaapi_encode_av1_write_obu(AVCodecContext *avctx,
                                      char *data, size_t *data_len,
                                      CodedBitstreamFragment *bs)
{
    VAAPIEncodeAV1Context *priv = avctx->priv_data;
    int ret;

    ret = ff_cbs_write_fragment_data(priv->cbc, bs);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to write packed header.\n");
        return ret;
    }

    if ((size_t)8 * MAX_PARAM_BUFFER_SIZE < 8 * bs->data_size - bs->data_bit_padding) {
        av_log(avctx, AV_LOG_ERROR, "Access unit too large: "
               "%zu < %zu.\n", (size_t)8 * MAX_PARAM_BUFFER_SIZE,
               8 * bs->data_size - bs->data_bit_padding);
        return AVERROR(ENOSPC);
    }

    memcpy(data, bs->data, bs->data_size);
    *data_len = 8 * bs->data_size - bs->data_bit_padding;

    return 0;
}

static int tile_log2(int blkSize, int target) {
    int k;
    for (k = 0; (blkSize << k) < target; k++);
    return k;
}

static int vaapi_encode_av1_set_tile(AVCodecContext *avctx)
{
    VAAPIEncodeAV1Context *priv = avctx->priv_data;
    int mi_cols, mi_rows, sb_shift, sb_size;
    int max_tile_area_sb, max_tile_area_sb_varied;
    int tile_width_sb, tile_height_sb, widest_tile_sb;
    int tile_cols, tile_rows;
    int min_log2_tiles;
    int i;

    if (priv->tile_cols > AV1_MAX_TILE_COLS ||
        priv->tile_rows > AV1_MAX_TILE_ROWS) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile number %dx%d, should less than %dx%d.\n",
               priv->tile_cols, priv->tile_rows, AV1_MAX_TILE_COLS, AV1_MAX_TILE_ROWS);
        return AVERROR(EINVAL);
    }

    mi_cols = 2 * ((avctx->width + 7) >> 3);
    mi_rows = 2 * ((avctx->height + 7) >> 3);
    priv->sb_cols = priv->use_128x128_superblock ?
                    ((mi_cols + 31) >> 5) : ((mi_cols + 15) >> 4);
    priv->sb_rows = priv->use_128x128_superblock ?
                    ((mi_rows + 31) >> 5) : ((mi_rows + 15) >> 4);
    sb_shift = priv->use_128x128_superblock ? 5 : 4;
    sb_size  = sb_shift + 2;
    priv->max_tile_width_sb = AV1_MAX_TILE_WIDTH >> sb_size;
    max_tile_area_sb = AV1_MAX_TILE_AREA  >> (2 * sb_size);

    priv->min_log2_tile_cols = tile_log2(priv->max_tile_width_sb, priv->sb_cols);
    priv->max_log2_tile_cols = tile_log2(1, FFMIN(priv->sb_cols, AV1_MAX_TILE_COLS));
    priv->max_log2_tile_rows = tile_log2(1, FFMIN(priv->sb_rows, AV1_MAX_TILE_ROWS));
    min_log2_tiles = FFMAX(priv->min_log2_tile_cols,
                           tile_log2(max_tile_area_sb, priv->sb_rows * priv->sb_cols));

    tile_cols = av_clip(priv->tile_cols, (priv->sb_cols + priv->max_tile_width_sb - 1) / priv->max_tile_width_sb, priv->sb_cols);

    if (!priv->tile_cols)
        priv->tile_cols = tile_cols;
    else if (priv->tile_cols != tile_cols){
        av_log(avctx, AV_LOG_ERROR, "Invalid tile cols %d, should be in range of %d~%d\n",
               priv->tile_cols,
               (priv->sb_cols + priv->max_tile_width_sb - 1) / priv->max_tile_width_sb,
               priv->sb_cols);
        return AVERROR(EINVAL);
    }

    priv->tile_cols_log2 = tile_log2(1, priv->tile_cols);
    tile_width_sb = (priv->sb_cols + (1 << priv->tile_cols_log2) - 1) >>
                    priv->tile_cols_log2;

    if (priv->tile_rows > priv->sb_rows) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tile rows %d, should be less than %d.\n",
               priv->tile_rows, priv->sb_rows);
        return AVERROR(EINVAL);
    }

    /** Try user setting tile rows number first. */
    tile_rows = priv->tile_rows ? priv->tile_rows : 1;
    for (; tile_rows <= priv->sb_rows && tile_rows <= AV1_MAX_TILE_ROWS; tile_rows++) {
        /** try uniformed tile. */
        priv->tile_rows_log2 = tile_log2(1, tile_rows);
        if ((priv->sb_cols + tile_width_sb - 1) / tile_width_sb == priv->tile_cols) {
            for (i = 0; i < priv->tile_cols - 1; i++)
                priv->width_in_sbs_minus_1[i] = tile_width_sb - 1;
            priv->width_in_sbs_minus_1[i] = priv->sb_cols - (priv->tile_cols - 1) * tile_width_sb - 1;

            tile_height_sb = (priv->sb_rows + (1 << priv->tile_rows_log2) - 1) >>
                             priv->tile_rows_log2;

            if ((priv->sb_rows + tile_height_sb - 1) / tile_height_sb == tile_rows &&
                tile_height_sb <= max_tile_area_sb / tile_width_sb) {
                for (i = 0; i < tile_rows - 1; i++)
                    priv->height_in_sbs_minus_1[i] = tile_height_sb - 1;
                priv->height_in_sbs_minus_1[i] = priv->sb_rows - (tile_rows - 1) * tile_height_sb - 1;

                priv->uniform_tile = 1;
                priv->min_log2_tile_rows = FFMAX(min_log2_tiles - priv->tile_cols_log2, 0);

                break;
            }
        }

        /** try non-uniformed tile. */
        widest_tile_sb = 0;
        for (i = 0; i < priv->tile_cols; i++) {
            priv->width_in_sbs_minus_1[i] = (i + 1) * priv->sb_cols / priv->tile_cols - i * priv->sb_cols / priv->tile_cols - 1;
            widest_tile_sb = FFMAX(widest_tile_sb, priv->width_in_sbs_minus_1[i] + 1);
        }

        if (min_log2_tiles)
            max_tile_area_sb_varied = (priv->sb_rows * priv->sb_cols) >> (min_log2_tiles + 1);
        else
            max_tile_area_sb_varied = priv->sb_rows * priv->sb_cols;
        priv->max_tile_height_sb = FFMAX(1, max_tile_area_sb_varied / widest_tile_sb);

        if (tile_rows == av_clip(tile_rows, (priv->sb_rows + priv->max_tile_height_sb - 1) / priv->max_tile_height_sb, priv->sb_rows)) {
            for (i = 0; i < tile_rows; i++)
                priv->height_in_sbs_minus_1[i] = (i + 1) * priv->sb_rows / tile_rows - i * priv->sb_rows / tile_rows - 1;

            break;
        }

        /** Return invalid parameter if explicit tile rows is set. */
        if (priv->tile_rows) {
            av_log(avctx, AV_LOG_ERROR, "Invalid tile rows %d.\n", priv->tile_rows);
            return AVERROR(EINVAL);
        }
    }

    priv->tile_rows = tile_rows;
    av_log(avctx, AV_LOG_DEBUG, "Setting tile cols/rows to %d/%d.\n",
           priv->tile_cols, priv->tile_rows);

    /** check if tile cols/rows is supported by driver. */
    if (priv->attr_ext2.bits.max_tile_num_minus1) {
        if ((priv->tile_cols * priv->tile_rows - 1) > priv->attr_ext2.bits.max_tile_num_minus1) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported tile num %d * %d = %d by driver, "
                   "should be at most %d.\n", priv->tile_cols, priv->tile_rows,
                   priv->tile_cols * priv->tile_rows,
                   priv->attr_ext2.bits.max_tile_num_minus1 + 1);
            return AVERROR(EINVAL);
        }
    }

    /** check if tile group numbers is valid. */
    if (priv->tile_groups > priv->tile_cols * priv->tile_rows) {
        av_log(avctx, AV_LOG_WARNING, "Invalid tile groups number %d, "
        "correct to %d.\n", priv->tile_groups, priv->tile_cols * priv->tile_rows);
        priv->tile_groups = priv->tile_cols * priv->tile_rows;
    }

    return 0;
}

static int vaapi_encode_av1_write_sequence_header(AVCodecContext *avctx,
                                                  char *data, size_t *data_len)
{
    VAAPIEncodeAV1Context *priv = avctx->priv_data;

    memcpy(data, &priv->sh_data, MAX_PARAM_BUFFER_SIZE * sizeof(char));
    *data_len = priv->sh_data_len;

    return 0;
}

static int vaapi_encode_av1_init_sequence_params(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext       *base_ctx = avctx->priv_data;
    VAAPIEncodeContext               *ctx = avctx->priv_data;
    VAAPIEncodeAV1Context           *priv = avctx->priv_data;
    AV1RawOBU                     *sh_obu = &priv->sh;
    AV1RawSequenceHeader              *sh = &sh_obu->obu.sequence_header;
    VAEncSequenceParameterBufferAV1 *vseq = ctx->codec_sequence_params;
    CodedBitstreamFragment           *obu = &priv->current_obu;
    const AVPixFmtDescriptor *desc;
    int ret;

    memset(sh_obu, 0, sizeof(*sh_obu));
    sh_obu->header.obu_type = AV1_OBU_SEQUENCE_HEADER;

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    av_assert0(desc);

    sh->seq_profile  = avctx->profile;
    if (!sh->seq_force_screen_content_tools)
        sh->seq_force_integer_mv = AV1_SELECT_INTEGER_MV;
    sh->frame_width_bits_minus_1  = av_log2(avctx->width);
    sh->frame_height_bits_minus_1 = av_log2(avctx->height);
    sh->max_frame_width_minus_1   = avctx->width - 1;
    sh->max_frame_height_minus_1  = avctx->height - 1;
    sh->seq_tier[0]               = priv->tier;
    /** enable order hint and reserve maximum 8 bits for it by default. */
    sh->enable_order_hint         = 1;
    sh->order_hint_bits_minus_1   = 7;

    sh->color_config = (AV1RawColorConfig) {
        .high_bitdepth                  = desc->comp[0].depth == 8 ? 0 : 1,
        .color_primaries                = avctx->color_primaries,
        .transfer_characteristics       = avctx->color_trc,
        .matrix_coefficients            = avctx->colorspace,
        .color_description_present_flag = (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                           avctx->color_trc       != AVCOL_TRC_UNSPECIFIED ||
                                           avctx->colorspace      != AVCOL_SPC_UNSPECIFIED),
        .color_range                    = avctx->color_range == AVCOL_RANGE_JPEG,
        .subsampling_x                  = desc->log2_chroma_w,
        .subsampling_y                  = desc->log2_chroma_h,
    };

    switch (avctx->chroma_sample_location) {
        case AVCHROMA_LOC_LEFT:
            sh->color_config.chroma_sample_position = AV1_CSP_VERTICAL;
            break;
        case AVCHROMA_LOC_TOPLEFT:
            sh->color_config.chroma_sample_position = AV1_CSP_COLOCATED;
            break;
        default:
            sh->color_config.chroma_sample_position = AV1_CSP_UNKNOWN;
            break;
    }

    if (avctx->level != AV_LEVEL_UNKNOWN) {
        sh->seq_level_idx[0] = avctx->level;
    } else {
        const AV1LevelDescriptor *level;
        float framerate;

        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            framerate = avctx->framerate.num / avctx->framerate.den;
        else
            framerate = 0;

        level = ff_av1_guess_level(avctx->bit_rate, priv->tier,
                                   base_ctx->surface_width, base_ctx->surface_height,
                                   priv->tile_rows * priv->tile_cols,
                                   priv->tile_cols, framerate);
        if (level) {
            av_log(avctx, AV_LOG_VERBOSE, "Using level %s.\n", level->name);
            sh->seq_level_idx[0] = level->level_idx;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "Stream will not conform to "
                   "any normal level, using maximum parameters level by default.\n");
            sh->seq_level_idx[0] = 31;
            sh->seq_tier[0] = 1;
        }
    }
    vseq->seq_profile             = sh->seq_profile;
    vseq->seq_level_idx           = sh->seq_level_idx[0];
    vseq->seq_tier                = sh->seq_tier[0];
    vseq->order_hint_bits_minus_1 = sh->order_hint_bits_minus_1;
    vseq->intra_period            = base_ctx->gop_size;
    vseq->ip_period               = base_ctx->b_per_p + 1;

    vseq->seq_fields.bits.enable_order_hint = sh->enable_order_hint;

    if (!(ctx->va_rc_mode & VA_RC_CQP)) {
        vseq->bits_per_second = ctx->va_bit_rate;
        vseq->seq_fields.bits.enable_cdef = sh->enable_cdef = 1;
    }

    ret = vaapi_encode_av1_add_obu(avctx, obu, AV1_OBU_SEQUENCE_HEADER, &priv->sh);
    if (ret < 0)
        goto end;

    ret = vaapi_encode_av1_write_obu(avctx, priv->sh_data, &priv->sh_data_len, obu);
    if (ret < 0)
        goto end;

end:
    ff_cbs_fragment_reset(obu);
    return ret;
}

static int vaapi_encode_av1_init_picture_params(AVCodecContext *avctx,
                                                FFHWBaseEncodePicture *pic)
{
    VAAPIEncodeContext              *ctx = avctx->priv_data;
    VAAPIEncodeAV1Context          *priv = avctx->priv_data;
    VAAPIEncodePicture        *vaapi_pic = pic->priv;
    VAAPIEncodeAV1Picture          *hpic = pic->codec_priv;
    AV1RawOBU                    *fh_obu = &priv->fh;
    AV1RawFrameHeader                *fh = &fh_obu->obu.frame.header;
    VAEncPictureParameterBufferAV1 *vpic = vaapi_pic->codec_picture_params;
    CodedBitstreamFragment          *obu = &priv->current_obu;
    FFHWBaseEncodePicture *ref;
    VAAPIEncodeAV1Picture *href;
    int slot, i;
    int ret;
    static const int8_t default_loop_filter_ref_deltas[AV1_TOTAL_REFS_PER_FRAME] =
        { 1, 0, 0, 0, -1, 0, -1, -1 };

    memset(fh_obu, 0, sizeof(*fh_obu));
    vaapi_pic->nb_slices = priv->tile_groups;
    vaapi_pic->non_independent_frame = pic->encode_order < pic->display_order;
    fh_obu->header.obu_type = AV1_OBU_FRAME_HEADER;
    fh_obu->header.obu_has_size_field = 1;

    switch (pic->type) {
    case FF_HW_PICTURE_TYPE_IDR:
        av_assert0(pic->nb_refs[0] == 0 || pic->nb_refs[1]);
        fh->frame_type = AV1_FRAME_KEY;
        fh->refresh_frame_flags = 0xFF;
        fh->base_q_idx = priv->q_idx_idr;
        hpic->slot = 0;
        hpic->last_idr_frame = pic->display_order;
        break;
    case FF_HW_PICTURE_TYPE_P:
        av_assert0(pic->nb_refs[0]);
        fh->frame_type = AV1_FRAME_INTER;
        fh->base_q_idx = priv->q_idx_p;
        ref = pic->refs[0][pic->nb_refs[0] - 1];
        href = ref->codec_priv;
        hpic->slot = !href->slot;
        hpic->last_idr_frame = href->last_idr_frame;
        fh->refresh_frame_flags = 1 << hpic->slot;

        /** set the nearest frame in L0 as all reference frame. */
        for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
            fh->ref_frame_idx[i] = href->slot;
        }
        fh->primary_ref_frame = href->slot;
        fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;
        vpic->ref_frame_ctrl_l0.fields.search_idx0 = AV1_REF_FRAME_LAST;

        /** set the 2nd nearest frame in L0 as Golden frame. */
        if (pic->nb_refs[0] > 1) {
            ref = pic->refs[0][pic->nb_refs[0] - 2];
            href = ref->codec_priv;
            fh->ref_frame_idx[3] = href->slot;
            fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;
            vpic->ref_frame_ctrl_l0.fields.search_idx1 = AV1_REF_FRAME_GOLDEN;
        }
        break;
    case FF_HW_PICTURE_TYPE_B:
        av_assert0(pic->nb_refs[0] && pic->nb_refs[1]);
        fh->frame_type = AV1_FRAME_INTER;
        fh->base_q_idx = priv->q_idx_b;
        fh->refresh_frame_flags = 0x0;
        fh->reference_select = 1;

        /** B frame will not be referenced, disable its recon frame. */
        vpic->picture_flags.bits.disable_frame_recon = 1;

        /** Use LAST_FRAME and BWDREF_FRAME for reference. */
        vpic->ref_frame_ctrl_l0.fields.search_idx0 = AV1_REF_FRAME_LAST;
        vpic->ref_frame_ctrl_l1.fields.search_idx0 = AV1_REF_FRAME_BWDREF;

        ref                            = pic->refs[0][pic->nb_refs[0] - 1];
        href                           = ref->codec_priv;
        hpic->last_idr_frame           = href->last_idr_frame;
        fh->primary_ref_frame          = href->slot;
        fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;
        for (i = 0; i < AV1_REF_FRAME_GOLDEN; i++) {
            fh->ref_frame_idx[i] = href->slot;
        }

        ref                            = pic->refs[1][pic->nb_refs[1] - 1];
        href                           = ref->codec_priv;
        fh->ref_order_hint[href->slot] = ref->display_order - href->last_idr_frame;
        for (i = AV1_REF_FRAME_GOLDEN; i < AV1_REFS_PER_FRAME; i++) {
            fh->ref_frame_idx[i] = href->slot;
        }
        break;
    default:
        av_assert0(0 && "invalid picture type");
    }

    fh->show_frame                = pic->display_order <= pic->encode_order;
    fh->showable_frame            = fh->frame_type != AV1_FRAME_KEY;
    fh->frame_width_minus_1       = avctx->width - 1;
    fh->frame_height_minus_1      = avctx->height - 1;
    fh->render_width_minus_1      = fh->frame_width_minus_1;
    fh->render_height_minus_1     = fh->frame_height_minus_1;
    fh->order_hint                = pic->display_order - hpic->last_idr_frame;
    fh->tile_cols                 = priv->tile_cols;
    fh->tile_rows                 = priv->tile_rows;
    fh->tile_cols_log2            = priv->tile_cols_log2;
    fh->tile_rows_log2            = priv->tile_rows_log2;
    fh->uniform_tile_spacing_flag = priv->uniform_tile;
    fh->tile_size_bytes_minus1    = priv->attr_ext2.bits.tile_size_bytes_minus1;

    /** ignore ONLY_4x4 mode for codedlossless is not fully implemented. */
    if (priv->attr_ext2.bits.tx_mode_support & 0x04)
        fh->tx_mode = AV1_TX_MODE_SELECT;
    else if (priv->attr_ext2.bits.tx_mode_support & 0x02)
        fh->tx_mode = AV1_TX_MODE_LARGEST;
    else {
        av_log(avctx, AV_LOG_ERROR, "No available tx mode found.\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < fh->tile_cols; i++)
        fh->width_in_sbs_minus_1[i] = vpic->width_in_sbs_minus_1[i] = priv->width_in_sbs_minus_1[i];

    for (i = 0; i < fh->tile_rows; i++)
        fh->height_in_sbs_minus_1[i] = vpic->height_in_sbs_minus_1[i] = priv->height_in_sbs_minus_1[i];

    memcpy(fh->loop_filter_ref_deltas, default_loop_filter_ref_deltas,
           AV1_TOTAL_REFS_PER_FRAME * sizeof(int8_t));

    if (fh->frame_type == AV1_FRAME_KEY && fh->show_frame) {
        fh->error_resilient_mode = 1;
    }

    if (fh->frame_type == AV1_FRAME_KEY || fh->error_resilient_mode)
        fh->primary_ref_frame = AV1_PRIMARY_REF_NONE;

    vpic->base_qindex          = fh->base_q_idx;
    vpic->frame_width_minus_1  = fh->frame_width_minus_1;
    vpic->frame_height_minus_1 = fh->frame_height_minus_1;
    vpic->primary_ref_frame    = fh->primary_ref_frame;
    vpic->reconstructed_frame  = vaapi_pic->recon_surface;
    vpic->coded_buf            = vaapi_pic->output_buffer;
    vpic->tile_cols            = fh->tile_cols;
    vpic->tile_rows            = fh->tile_rows;
    vpic->order_hint           = fh->order_hint;
#if VA_CHECK_VERSION(1, 15, 0)
    vpic->refresh_frame_flags  = fh->refresh_frame_flags;
#endif

    vpic->picture_flags.bits.enable_frame_obu     = 0;
    vpic->picture_flags.bits.frame_type           = fh->frame_type;
    vpic->picture_flags.bits.reduced_tx_set       = fh->reduced_tx_set;
    vpic->picture_flags.bits.error_resilient_mode = fh->error_resilient_mode;

    /** let driver decide to use single or compound reference prediction mode. */
    vpic->mode_control_flags.bits.reference_mode = fh->reference_select ? 2 : 0;
    vpic->mode_control_flags.bits.tx_mode = fh->tx_mode;

    vpic->tile_group_obu_hdr_info.bits.obu_has_size_field = 1;

    /** set reference. */
    for (i = 0; i < AV1_REFS_PER_FRAME; i++)
        vpic->ref_frame_idx[i] = fh->ref_frame_idx[i];

    for (i = 0; i < FF_ARRAY_ELEMS(vpic->reference_frames); i++)
        vpic->reference_frames[i] = VA_INVALID_SURFACE;

    for (i = 0; i < MAX_REFERENCE_LIST_NUM; i++) {
        for (int j = 0; j < pic->nb_refs[i]; j++) {
            FFHWBaseEncodePicture *ref_pic = pic->refs[i][j];

            slot = ((VAAPIEncodeAV1Picture*)ref_pic->codec_priv)->slot;
            av_assert0(vpic->reference_frames[slot] == VA_INVALID_SURFACE);

            vpic->reference_frames[slot] = ((VAAPIEncodePicture *)ref_pic->priv)->recon_surface;
        }
    }

    ret = vaapi_encode_av1_add_obu(avctx, obu, AV1_OBU_FRAME_HEADER, &priv->fh);
    if (ret < 0)
        goto end;

    ret = vaapi_encode_av1_write_obu(avctx, priv->fh_data, &priv->fh_data_len, obu);
    if (ret < 0)
        goto end;

    if (!(ctx->va_rc_mode & VA_RC_CQP)) {
        vpic->min_base_qindex = av_clip(avctx->qmin, 1, AV1_MAX_QUANT);
        vpic->max_base_qindex = av_clip(avctx->qmax, 1, AV1_MAX_QUANT);

        vpic->bit_offset_qindex              = priv->qindex_offset;
        vpic->bit_offset_loopfilter_params   = priv->loopfilter_offset;
        vpic->bit_offset_cdef_params         = priv->cdef_start_offset;
        vpic->size_in_bits_cdef_params       = priv->cdef_param_size;
        vpic->size_in_bits_frame_hdr_obu     = priv->fh_data_len;
        vpic->byte_offset_frame_hdr_obu_size = (((pic->type == FF_HW_PICTURE_TYPE_IDR) ?
                                               priv->sh_data_len / 8 : 0) +
                                               (fh_obu->header.obu_extension_flag ?
                                               2 : 1));
    }

    priv->nb_mh = 0;

    if (pic->type == FF_HW_PICTURE_TYPE_IDR) {
        AVFrameSideData *sd =
            av_frame_get_side_data(pic->input_image,
                                   AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        if (sd) {
            AVMasteringDisplayMetadata *mdm =
                (AVMasteringDisplayMetadata *)sd->data;
            if (mdm->has_primaries && mdm->has_luminance) {
                AV1RawOBU              *obu = &priv->mh[priv->nb_mh++];
                AV1RawMetadata          *md = &obu->obu.metadata;
                AV1RawMetadataHDRMDCV *mdcv = &md->metadata.hdr_mdcv;
                const int        chroma_den = 1 << 16;
                const int      max_luma_den = 1 << 8;
                const int      min_luma_den = 1 << 14;

                memset(obu, 0, sizeof(*obu));
                obu->header.obu_type = AV1_OBU_METADATA;
                md->metadata_type = AV1_METADATA_TYPE_HDR_MDCV;

                for (i = 0; i < 3; i++) {
                    mdcv->primary_chromaticity_x[i] =
                        av_rescale(mdm->display_primaries[i][0].num, chroma_den,
                                   mdm->display_primaries[i][0].den);
                    mdcv->primary_chromaticity_y[i] =
                        av_rescale(mdm->display_primaries[i][1].num, chroma_den,
                                   mdm->display_primaries[i][1].den);
                }

                mdcv->white_point_chromaticity_x =
                    av_rescale(mdm->white_point[0].num, chroma_den,
                               mdm->white_point[0].den);
                mdcv->white_point_chromaticity_y =
                    av_rescale(mdm->white_point[1].num, chroma_den,
                               mdm->white_point[1].den);

                mdcv->luminance_max =
                    av_rescale(mdm->max_luminance.num, max_luma_den,
                               mdm->max_luminance.den);
                mdcv->luminance_min =
                    av_rescale(mdm->min_luminance.num, min_luma_den,
                               mdm->min_luminance.den);
            }
        }

        sd = av_frame_get_side_data(pic->input_image,
                                    AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        if (sd) {
            AVContentLightMetadata *cllm = (AVContentLightMetadata *)sd->data;
            AV1RawOBU               *obu = &priv->mh[priv->nb_mh++];
            AV1RawMetadata           *md = &obu->obu.metadata;
            AV1RawMetadataHDRCLL    *cll = &md->metadata.hdr_cll;

            memset(obu, 0, sizeof(*obu));
            obu->header.obu_type = AV1_OBU_METADATA;
            md->metadata_type    = AV1_METADATA_TYPE_HDR_CLL;
            cll->max_cll         = cllm->MaxCLL;
            cll->max_fall        = cllm->MaxFALL;
        }
    }

end:
    ff_cbs_fragment_reset(obu);
    return ret;
}

static int vaapi_encode_av1_init_slice_params(AVCodecContext *avctx,
                                              FFHWBaseEncodePicture *base,
                                              VAAPIEncodeSlice *slice)
{
    VAAPIEncodeAV1Context      *priv = avctx->priv_data;
    VAEncTileGroupBufferAV1  *vslice = slice->codec_slice_params;
    CodedBitstreamAV1Context  *cbctx = priv->cbc->priv_data;
    int div;

    /** Set tile group info. */
    div = priv->tile_cols * priv->tile_rows / priv->tile_groups;
    vslice->tg_start = slice->index * div;
    if (slice->index == (priv->tile_groups - 1)) {
        vslice->tg_end = priv->tile_cols * priv->tile_rows - 1;
        cbctx->seen_frame_header = 0;
    } else {
        vslice->tg_end = (slice->index + 1) * div - 1;
    }

    return 0;
}

static int vaapi_encode_av1_write_picture_header(AVCodecContext *avctx,
                                                 FFHWBaseEncodePicture *pic,
                                                 char *data, size_t *data_len)
{
    VAAPIEncodeAV1Context     *priv = avctx->priv_data;
    CodedBitstreamFragment     *obu = &priv->current_obu;
    CodedBitstreamAV1Context *cbctx = priv->cbc->priv_data;
    AV1RawOBU               *fh_obu = &priv->fh;
    AV1RawFrameHeader       *rep_fh = &fh_obu->obu.frame_header;
    VAAPIEncodePicture   *vaapi_pic = pic->priv;
    VAAPIEncodeAV1Picture *href;
    int ret = 0;

    vaapi_pic->tail_size = 0;
    /** Pack repeat frame header. */
    if (pic->display_order > pic->encode_order) {
        memset(fh_obu, 0, sizeof(*fh_obu));
        href = pic->refs[0][pic->nb_refs[0] - 1]->codec_priv;
        fh_obu->header.obu_type = AV1_OBU_FRAME_HEADER;
        fh_obu->header.obu_has_size_field = 1;

        rep_fh->show_existing_frame   = 1;
        rep_fh->frame_to_show_map_idx = href->slot == 0;
        rep_fh->frame_type            = AV1_FRAME_INTER;
        rep_fh->frame_width_minus_1   = avctx->width - 1;
        rep_fh->frame_height_minus_1  = avctx->height - 1;
        rep_fh->render_width_minus_1  = rep_fh->frame_width_minus_1;
        rep_fh->render_height_minus_1 = rep_fh->frame_height_minus_1;

        cbctx->seen_frame_header = 0;

        ret = vaapi_encode_av1_add_obu(avctx, obu, AV1_OBU_FRAME_HEADER, &priv->fh);
        if (ret < 0)
            goto end;

        ret = vaapi_encode_av1_write_obu(avctx, vaapi_pic->tail_data, &vaapi_pic->tail_size, obu);
        if (ret < 0)
            goto end;

        vaapi_pic->tail_size /= 8;
    }

    memcpy(data, &priv->fh_data, MAX_PARAM_BUFFER_SIZE * sizeof(char));
    *data_len = priv->fh_data_len;

end:
    ff_cbs_fragment_reset(obu);
    return ret;
}

static int vaapi_encode_av1_write_extra_header(AVCodecContext *avctx,
                                               FFHWBaseEncodePicture *base_pic,
                                               int index, int *type,
                                               char *data, size_t *data_len)
{
    VAAPIEncodeAV1Context  *priv = avctx->priv_data;
    CodedBitstreamFragment *obu  = &priv->current_obu;
    AV1RawOBU *mh_obu;
    char mh_data[MAX_PARAM_BUFFER_SIZE];
    size_t mh_data_len;
    int ret = 0;

    if (index >= priv->nb_mh)
        return AVERROR_EOF;

    mh_obu = &priv->mh[index];
    ret = vaapi_encode_av1_add_obu(avctx, obu, AV1_OBU_METADATA, mh_obu);
    if (ret < 0)
        goto end;

    ret = vaapi_encode_av1_write_obu(avctx, mh_data, &mh_data_len, obu);
    if (ret < 0)
        goto end;

    memcpy(data, mh_data, MAX_PARAM_BUFFER_SIZE * sizeof(char));
    *data_len = mh_data_len;
    *type = VAEncPackedHeaderRawData;

end:
    ff_cbs_fragment_reset(obu);
    return ret;
}

static const VAAPIEncodeProfile vaapi_encode_av1_profiles[] = {
    { AV_PROFILE_AV1_MAIN,  8, 3, 1, 1, VAProfileAV1Profile0 },
    { AV_PROFILE_AV1_MAIN, 10, 3, 1, 1, VAProfileAV1Profile0 },
    { AV_PROFILE_UNKNOWN }
};

static const VAAPIEncodeType vaapi_encode_type_av1 = {
    .profiles        = vaapi_encode_av1_profiles,
    .flags           = FF_HW_FLAG_B_PICTURES | FLAG_TIMESTAMP_NO_DELAY,
    .default_quality = 25,

    .get_encoder_caps = &vaapi_encode_av1_get_encoder_caps,
    .configure        = &vaapi_encode_av1_configure,

    .sequence_header_type  = VAEncPackedHeaderSequence,
    .sequence_params_size  = sizeof(VAEncSequenceParameterBufferAV1),
    .init_sequence_params  = &vaapi_encode_av1_init_sequence_params,
    .write_sequence_header = &vaapi_encode_av1_write_sequence_header,

    .picture_priv_data_size = sizeof(VAAPIEncodeAV1Picture),
    .picture_header_type    = VAEncPackedHeaderPicture,
    .picture_params_size    = sizeof(VAEncPictureParameterBufferAV1),
    .init_picture_params    = &vaapi_encode_av1_init_picture_params,
    .write_picture_header   = &vaapi_encode_av1_write_picture_header,

    .slice_params_size = sizeof(VAEncTileGroupBufferAV1),
    .init_slice_params = &vaapi_encode_av1_init_slice_params,

    .write_extra_header     = &vaapi_encode_av1_write_extra_header,
};

static av_cold int vaapi_encode_av1_init(AVCodecContext *avctx)
{
    VAAPIEncodeContext      *ctx = avctx->priv_data;
    VAAPIEncodeAV1Context  *priv = avctx->priv_data;
    VAConfigAttrib attr;
    VAStatus vas;
    int ret;

    ctx->codec = &vaapi_encode_type_av1;

    ctx->desired_packed_headers =
        VA_ENC_PACKED_HEADER_SEQUENCE |
        VA_ENC_PACKED_HEADER_PICTURE |
        VA_ENC_PACKED_HEADER_MISC;      // Metadata

    if (avctx->profile == AV_PROFILE_UNKNOWN)
        avctx->profile = priv->profile;
    if (avctx->level == AV_LEVEL_UNKNOWN)
        avctx->level = priv->level;

    if (avctx->level != AV_LEVEL_UNKNOWN && avctx->level & ~0x1f) {
        av_log(avctx, AV_LOG_ERROR, "Invalid level %d\n", avctx->level);
        return AVERROR(EINVAL);
    }

    ret = ff_vaapi_encode_init(avctx);
    if (ret < 0)
        return ret;

    attr.type = VAConfigAttribEncAV1;
    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    } else if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        priv->attr.value = 0;
        av_log(avctx, AV_LOG_WARNING, "Attribute type:%d is not "
               "supported.\n", attr.type);
    } else {
        priv->attr.value = attr.value;
    }

    attr.type = VAConfigAttribEncAV1Ext1;
    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    } else if (attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        priv->attr_ext1.value = 0;
        av_log(avctx, AV_LOG_WARNING, "Attribute type:%d is not "
               "supported.\n", attr.type);
    } else {
        priv->attr_ext1.value = attr.value;
    }

    /** This attr provides essential indicators, return error if not support. */
    attr.type = VAConfigAttribEncAV1Ext2;
    vas = vaGetConfigAttributes(ctx->hwctx->display,
                                ctx->va_profile,
                                ctx->va_entrypoint,
                                &attr, 1);
    if (vas != VA_STATUS_SUCCESS || attr.value == VA_ATTRIB_NOT_SUPPORTED) {
        av_log(avctx, AV_LOG_ERROR, "Failed to query "
               "config attribute: %d (%s).\n", vas, vaErrorStr(vas));
        return AVERROR_EXTERNAL;
    } else {
        priv->attr_ext2.value = attr.value;
    }

    av_opt_set_int(priv->cbc->priv_data, "fixed_obu_size_length",
                   priv->attr_ext2.bits.obu_size_bytes_minus1 + 1, 0);

    ret = vaapi_encode_av1_set_tile(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

static av_cold int vaapi_encode_av1_close(AVCodecContext *avctx)
{
    VAAPIEncodeAV1Context *priv = avctx->priv_data;

    ff_cbs_fragment_free(&priv->current_obu);
    ff_cbs_close(&priv->cbc);

    return ff_vaapi_encode_close(avctx);
}

#define OFFSET(x) offsetof(VAAPIEncodeAV1Context, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM)

static const AVOption vaapi_encode_av1_options[] = {
    HW_BASE_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_COMMON_OPTIONS,
    VAAPI_ENCODE_RC_OPTIONS,
    { "profile", "Set profile (seq_profile)",
      OFFSET(profile), AV_OPT_TYPE_INT,
      { .i64 = AV_PROFILE_UNKNOWN }, AV_PROFILE_UNKNOWN, 0xff, FLAGS, .unit = "profile" },

#define PROFILE(name, value)  name, NULL, 0, AV_OPT_TYPE_CONST, \
    { .i64 = value }, 0, 0, FLAGS, .unit = "profile"
    { PROFILE("main",               AV_PROFILE_AV1_MAIN) },
    { PROFILE("high",               AV_PROFILE_AV1_HIGH) },
    { PROFILE("professional",       AV_PROFILE_AV1_PROFESSIONAL) },
#undef PROFILE

    { "tier", "Set tier (seq_tier)",
      OFFSET(tier), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, FLAGS, .unit = "tier" },
    { "main", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 0 }, 0, 0, FLAGS, .unit = "tier" },
    { "high", NULL, 0, AV_OPT_TYPE_CONST,
      { .i64 = 1 }, 0, 0, FLAGS, .unit = "tier" },
    { "level", "Set level (seq_level_idx)",
      OFFSET(level), AV_OPT_TYPE_INT,
      { .i64 = AV_LEVEL_UNKNOWN }, AV_LEVEL_UNKNOWN, 0x1f, FLAGS, .unit = "level" },

#define LEVEL(name, value) name, NULL, 0, AV_OPT_TYPE_CONST, \
      { .i64 = value }, 0, 0, FLAGS, .unit = "level"
    { LEVEL("2.0",  0) },
    { LEVEL("2.1",  1) },
    { LEVEL("3.0",  4) },
    { LEVEL("3.1",  5) },
    { LEVEL("4.0",  8) },
    { LEVEL("4.1",  9) },
    { LEVEL("5.0", 12) },
    { LEVEL("5.1", 13) },
    { LEVEL("5.2", 14) },
    { LEVEL("5.3", 15) },
    { LEVEL("6.0", 16) },
    { LEVEL("6.1", 17) },
    { LEVEL("6.2", 18) },
    { LEVEL("6.3", 19) },
#undef LEVEL

    { "tiles", "Tile columns x rows (Use minimal tile column/row number automatically by default)",
      OFFSET(tile_cols), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, 0, FLAGS },
    { "tile_groups", "Number of tile groups for encoding",
      OFFSET(tile_groups), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, AV1_MAX_TILE_ROWS * AV1_MAX_TILE_COLS, FLAGS },

    { NULL },
};

static const FFCodecDefault vaapi_encode_av1_defaults[] = {
    { "b",              "0"   },
    { "bf",             "2"   },
    { "g",              "120" },
    { "qmin",           "1"   },
    { "qmax",           "255" },
    { NULL },
};

static const AVClass vaapi_encode_av1_class = {
    .class_name = "av1_vaapi",
    .item_name  = av_default_item_name,
    .option     = vaapi_encode_av1_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_av1_vaapi_encoder = {
    .p.name         = "av1_vaapi",
    CODEC_LONG_NAME("AV1 (VAAPI)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .priv_data_size = sizeof(VAAPIEncodeAV1Context),
    .init           = &vaapi_encode_av1_init,
    FF_CODEC_RECEIVE_PACKET_CB(&ff_vaapi_encode_receive_packet),
    .close          = &vaapi_encode_av1_close,
    .p.priv_class   = &vaapi_encode_av1_class,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .defaults       = vaapi_encode_av1_defaults,
    CODEC_PIXFMTS(AV_PIX_FMT_VAAPI),
    .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .hw_configs     = ff_vaapi_encode_hw_configs,
    .p.wrapper_name = "vaapi",
};
