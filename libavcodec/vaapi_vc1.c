/*
 * VC-1 HW decode acceleration through VA API
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
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

#include "vaapi_internal.h"
#include "internal.h"
#include "vc1.h"
#include "vc1data.h"

/** Translate FFmpeg MV modes to VA API */
static int get_VAMvModeVC1(enum MVModes mv_mode)
{
    switch (mv_mode) {
    case MV_PMODE_1MV_HPEL_BILIN: return VAMvMode1MvHalfPelBilinear;
    case MV_PMODE_1MV:            return VAMvMode1Mv;
    case MV_PMODE_1MV_HPEL:       return VAMvMode1MvHalfPel;
    case MV_PMODE_MIXED_MV:       return VAMvModeMixedMv;
    case MV_PMODE_INTENSITY_COMP: return VAMvModeIntensityCompensation;
    }
    return 0;
}

/** Check whether the MVTYPEMB bitplane is present */
static inline int vc1_has_MVTYPEMB_bitplane(VC1Context *v)
{
    if (v->mv_type_is_raw)
        return 0;
    return v->s.pict_type == AV_PICTURE_TYPE_P &&
           (v->mv_mode == MV_PMODE_MIXED_MV ||
            (v->mv_mode == MV_PMODE_INTENSITY_COMP &&
             v->mv_mode2 == MV_PMODE_MIXED_MV));
}

/** Check whether the SKIPMB bitplane is present */
static inline int vc1_has_SKIPMB_bitplane(VC1Context *v)
{
    if (v->skip_is_raw)
        return 0;
    return v->s.pict_type == AV_PICTURE_TYPE_P ||
           (v->s.pict_type == AV_PICTURE_TYPE_B && !v->bi_type);
}

/** Check whether the DIRECTMB bitplane is present */
static inline int vc1_has_DIRECTMB_bitplane(VC1Context *v)
{
    if (v->dmb_is_raw)
        return 0;
    return v->s.pict_type == AV_PICTURE_TYPE_B && !v->bi_type;
}

/** Check whether the ACPRED bitplane is present */
static inline int vc1_has_ACPRED_bitplane(VC1Context *v)
{
    if (v->acpred_is_raw)
        return 0;
    return v->profile == PROFILE_ADVANCED &&
           (v->s.pict_type == AV_PICTURE_TYPE_I ||
            (v->s.pict_type == AV_PICTURE_TYPE_B && v->bi_type));
}

/** Check whether the OVERFLAGS bitplane is present */
static inline int vc1_has_OVERFLAGS_bitplane(VC1Context *v)
{
    if (v->overflg_is_raw)
        return 0;
    return v->profile == PROFILE_ADVANCED &&
           (v->s.pict_type == AV_PICTURE_TYPE_I ||
            (v->s.pict_type == AV_PICTURE_TYPE_B && v->bi_type)) &&
           (v->overlap && v->pq <= 8) &&
           v->condover == CONDOVER_SELECT;
}

/** Reconstruct bitstream PTYPE (7.1.1.4, index into Table-35) */
static int vc1_get_PTYPE(VC1Context *v)
{
    MpegEncContext * const s = &v->s;
    switch (s->pict_type) {
    case AV_PICTURE_TYPE_I: return 0;
    case AV_PICTURE_TYPE_P: return v->p_frame_skipped ? 4 : 1;
    case AV_PICTURE_TYPE_B: return v->bi_type         ? 3 : 2;
    }
    return 0;
}

/** Reconstruct bitstream MVMODE (7.1.1.32) */
static inline VAMvModeVC1 vc1_get_MVMODE(VC1Context *v)
{
    if (v->s.pict_type == AV_PICTURE_TYPE_P ||
        (v->s.pict_type == AV_PICTURE_TYPE_B && !v->bi_type))
        return get_VAMvModeVC1(v->mv_mode);
    return 0;
}

/** Reconstruct bitstream MVMODE2 (7.1.1.33) */
static inline VAMvModeVC1 vc1_get_MVMODE2(VC1Context *v)
{
    if (v->s.pict_type == AV_PICTURE_TYPE_P && v->mv_mode == MV_PMODE_INTENSITY_COMP)
        return get_VAMvModeVC1(v->mv_mode2);
    return 0;
}

/** Reconstruct bitstream TTFRM (7.1.1.41, Table-53) */
static inline int vc1_get_TTFRM(VC1Context *v)
{
    switch (v->ttfrm) {
    case TT_8X8: return 0;
    case TT_8X4: return 1;
    case TT_4X8: return 2;
    case TT_4X4: return 3;
    }
    return 0;
}

/** Pack FFmpeg bitplanes into a VABitPlaneBuffer element */
static inline void vc1_pack_bitplanes(uint8_t *bitplane, int n, const uint8_t *ff_bp[3], int x, int y, int stride)
{
    const int bitplane_index = n / 2;
    const int ff_bp_index = y * stride + x;
    uint8_t v = 0;
    if (ff_bp[0])
        v = ff_bp[0][ff_bp_index];
    if (ff_bp[1])
        v |= ff_bp[1][ff_bp_index] << 1;
    if (ff_bp[2])
        v |= ff_bp[2][ff_bp_index] << 2;
    bitplane[bitplane_index] = (bitplane[bitplane_index] << 4) | v;
}

static int vaapi_vc1_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    VC1Context * const v = avctx->priv_data;
    MpegEncContext * const s = &v->s;
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    VAPictureParameterBufferVC1 *pic_param;

    ff_dlog(avctx, "vaapi_vc1_start_frame()\n");

    vactx->slice_param_size = sizeof(VASliceParameterBufferVC1);

    /* Fill in VAPictureParameterBufferVC1 */
    pic_param = ff_vaapi_alloc_pic_param(vactx, sizeof(VAPictureParameterBufferVC1));
    if (!pic_param)
        return -1;
    pic_param->forward_reference_picture                            = VA_INVALID_ID;
    pic_param->backward_reference_picture                           = VA_INVALID_ID;
    pic_param->inloop_decoded_picture                               = VA_INVALID_ID;
    pic_param->sequence_fields.value                                = 0; /* reset all bits */
    pic_param->sequence_fields.bits.pulldown                        = v->broadcast;
    pic_param->sequence_fields.bits.interlace                       = v->interlace;
    pic_param->sequence_fields.bits.tfcntrflag                      = v->tfcntrflag;
    pic_param->sequence_fields.bits.finterpflag                     = v->finterpflag;
    pic_param->sequence_fields.bits.psf                             = v->psf;
    pic_param->sequence_fields.bits.multires                        = v->multires;
    pic_param->sequence_fields.bits.overlap                         = v->overlap;
    pic_param->sequence_fields.bits.syncmarker                      = v->resync_marker;
    pic_param->sequence_fields.bits.rangered                        = v->rangered;
    pic_param->sequence_fields.bits.max_b_frames                    = s->avctx->max_b_frames;
#if VA_CHECK_VERSION(0,32,0)
    pic_param->sequence_fields.bits.profile                         = v->profile;
#endif
    pic_param->coded_width                                          = s->avctx->coded_width;
    pic_param->coded_height                                         = s->avctx->coded_height;
    pic_param->entrypoint_fields.value                              = 0; /* reset all bits */
    pic_param->entrypoint_fields.bits.broken_link                   = v->broken_link;
    pic_param->entrypoint_fields.bits.closed_entry                  = v->closed_entry;
    pic_param->entrypoint_fields.bits.panscan_flag                  = v->panscanflag;
    pic_param->entrypoint_fields.bits.loopfilter                    = s->loop_filter;
    pic_param->conditional_overlap_flag                             = v->condover;
    pic_param->fast_uvmc_flag                                       = v->fastuvmc;
    pic_param->range_mapping_fields.value                           = 0; /* reset all bits */
    pic_param->range_mapping_fields.bits.luma_flag                  = v->range_mapy_flag;
    pic_param->range_mapping_fields.bits.luma                       = v->range_mapy;
    pic_param->range_mapping_fields.bits.chroma_flag                = v->range_mapuv_flag;
    pic_param->range_mapping_fields.bits.chroma                     = v->range_mapuv;
    pic_param->b_picture_fraction                                   = v->bfraction_lut_index;
    pic_param->cbp_table                                            = v->cbpcy_vlc ? v->cbpcy_vlc - ff_vc1_cbpcy_p_vlc : 0;
    pic_param->mb_mode_table                                        = 0; /* XXX: interlaced frame */
    pic_param->range_reduction_frame                                = v->rangeredfrm;
    pic_param->rounding_control                                     = v->rnd;
    pic_param->post_processing                                      = v->postproc;
    pic_param->picture_resolution_index                             = v->respic;
    pic_param->luma_scale                                           = v->lumscale;
    pic_param->luma_shift                                           = v->lumshift;
    pic_param->picture_fields.value                                 = 0; /* reset all bits */
    pic_param->picture_fields.bits.picture_type                     = vc1_get_PTYPE(v);
    pic_param->picture_fields.bits.frame_coding_mode                = v->fcm;
    pic_param->picture_fields.bits.top_field_first                  = v->tff;
    pic_param->picture_fields.bits.is_first_field                   = v->fcm == 0; /* XXX: interlaced frame */
    pic_param->picture_fields.bits.intensity_compensation           = v->mv_mode == MV_PMODE_INTENSITY_COMP;
    pic_param->raw_coding.value                                     = 0; /* reset all bits */
    pic_param->raw_coding.flags.mv_type_mb                          = v->mv_type_is_raw;
    pic_param->raw_coding.flags.direct_mb                           = v->dmb_is_raw;
    pic_param->raw_coding.flags.skip_mb                             = v->skip_is_raw;
    pic_param->raw_coding.flags.field_tx                            = 0; /* XXX: interlaced frame */
    pic_param->raw_coding.flags.forward_mb                          = 0; /* XXX: interlaced frame */
    pic_param->raw_coding.flags.ac_pred                             = v->acpred_is_raw;
    pic_param->raw_coding.flags.overflags                           = v->overflg_is_raw;
    pic_param->bitplane_present.value                               = 0; /* reset all bits */
    pic_param->bitplane_present.flags.bp_mv_type_mb                 = vc1_has_MVTYPEMB_bitplane(v);
    pic_param->bitplane_present.flags.bp_direct_mb                  = vc1_has_DIRECTMB_bitplane(v);
    pic_param->bitplane_present.flags.bp_skip_mb                    = vc1_has_SKIPMB_bitplane(v);
    pic_param->bitplane_present.flags.bp_field_tx                   = 0; /* XXX: interlaced frame */
    pic_param->bitplane_present.flags.bp_forward_mb                 = 0; /* XXX: interlaced frame */
    pic_param->bitplane_present.flags.bp_ac_pred                    = vc1_has_ACPRED_bitplane(v);
    pic_param->bitplane_present.flags.bp_overflags                  = vc1_has_OVERFLAGS_bitplane(v);
    pic_param->reference_fields.value                               = 0; /* reset all bits */
    pic_param->reference_fields.bits.reference_distance_flag        = v->refdist_flag;
    pic_param->reference_fields.bits.reference_distance             = 0; /* XXX: interlaced frame */
    pic_param->reference_fields.bits.num_reference_pictures         = 0; /* XXX: interlaced frame */
    pic_param->reference_fields.bits.reference_field_pic_indicator  = 0; /* XXX: interlaced frame */
    pic_param->mv_fields.value                                      = 0; /* reset all bits */
    pic_param->mv_fields.bits.mv_mode                               = vc1_get_MVMODE(v);
    pic_param->mv_fields.bits.mv_mode2                              = vc1_get_MVMODE2(v);
    pic_param->mv_fields.bits.mv_table                              = s->mv_table_index;
    pic_param->mv_fields.bits.two_mv_block_pattern_table            = 0; /* XXX: interlaced frame */
    pic_param->mv_fields.bits.four_mv_switch                        = 0; /* XXX: interlaced frame */
    pic_param->mv_fields.bits.four_mv_block_pattern_table           = 0; /* XXX: interlaced frame */
    pic_param->mv_fields.bits.extended_mv_flag                      = v->extended_mv;
    pic_param->mv_fields.bits.extended_mv_range                     = v->mvrange;
    pic_param->mv_fields.bits.extended_dmv_flag                     = v->extended_dmv;
    pic_param->mv_fields.bits.extended_dmv_range                    = 0; /* XXX: interlaced frame */
    pic_param->pic_quantizer_fields.value                           = 0; /* reset all bits */
    pic_param->pic_quantizer_fields.bits.dquant                     = v->dquant;
    pic_param->pic_quantizer_fields.bits.quantizer                  = v->quantizer_mode;
    pic_param->pic_quantizer_fields.bits.half_qp                    = v->halfpq;
    pic_param->pic_quantizer_fields.bits.pic_quantizer_scale        = v->pq;
    pic_param->pic_quantizer_fields.bits.pic_quantizer_type         = v->pquantizer;
    pic_param->pic_quantizer_fields.bits.dq_frame                   = v->dquantfrm;
    pic_param->pic_quantizer_fields.bits.dq_profile                 = v->dqprofile;
    pic_param->pic_quantizer_fields.bits.dq_sb_edge                 = v->dqprofile == DQPROFILE_SINGLE_EDGE  ? v->dqsbedge : 0;
    pic_param->pic_quantizer_fields.bits.dq_db_edge                 = v->dqprofile == DQPROFILE_DOUBLE_EDGES ? v->dqsbedge : 0;
    pic_param->pic_quantizer_fields.bits.dq_binary_level            = v->dqbilevel;
    pic_param->pic_quantizer_fields.bits.alt_pic_quantizer          = v->altpq;
    pic_param->transform_fields.value                               = 0; /* reset all bits */
    pic_param->transform_fields.bits.variable_sized_transform_flag  = v->vstransform;
    pic_param->transform_fields.bits.mb_level_transform_type_flag   = v->ttmbf;
    pic_param->transform_fields.bits.frame_level_transform_type     = vc1_get_TTFRM(v);
    pic_param->transform_fields.bits.transform_ac_codingset_idx1    = v->c_ac_table_index;
    pic_param->transform_fields.bits.transform_ac_codingset_idx2    = v->y_ac_table_index;
    pic_param->transform_fields.bits.intra_transform_dc_table       = v->s.dc_table_index;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        pic_param->backward_reference_picture = ff_vaapi_get_surface_id(s->next_picture.f);
        // fall-through
    case AV_PICTURE_TYPE_P:
        pic_param->forward_reference_picture = ff_vaapi_get_surface_id(s->last_picture.f);
        break;
    }

    if (pic_param->bitplane_present.value) {
        uint8_t *bitplane;
        const uint8_t *ff_bp[3];
        int x, y, n;

        switch (s->pict_type) {
        case AV_PICTURE_TYPE_P:
            ff_bp[0] = pic_param->bitplane_present.flags.bp_direct_mb  ? v->direct_mb_plane    : NULL;
            ff_bp[1] = pic_param->bitplane_present.flags.bp_skip_mb    ? s->mbskip_table       : NULL;
            ff_bp[2] = pic_param->bitplane_present.flags.bp_mv_type_mb ? v->mv_type_mb_plane   : NULL;
            break;
        case AV_PICTURE_TYPE_B:
            if (!v->bi_type) {
                ff_bp[0] = pic_param->bitplane_present.flags.bp_direct_mb ? v->direct_mb_plane : NULL;
                ff_bp[1] = pic_param->bitplane_present.flags.bp_skip_mb   ? s->mbskip_table    : NULL;
                ff_bp[2] = NULL; /* XXX: interlaced frame (FORWARD plane) */
                break;
            }
            /* fall-through (BI-type) */
        case AV_PICTURE_TYPE_I:
            ff_bp[0] = NULL; /* XXX: interlaced frame (FIELDTX plane) */
            ff_bp[1] = pic_param->bitplane_present.flags.bp_ac_pred    ? v->acpred_plane       : NULL;
            ff_bp[2] = pic_param->bitplane_present.flags.bp_overflags  ? v->over_flags_plane   : NULL;
            break;
        default:
            ff_bp[0] = NULL;
            ff_bp[1] = NULL;
            ff_bp[2] = NULL;
            break;
        }

        bitplane = ff_vaapi_alloc_bitplane(vactx, (s->mb_width * s->mb_height + 1) / 2);
        if (!bitplane)
            return -1;

        n = 0;
        for (y = 0; y < s->mb_height; y++)
            for (x = 0; x < s->mb_width; x++, n++)
                vc1_pack_bitplanes(bitplane, n, ff_bp, x, y, s->mb_stride);
        if (n & 1) /* move last nibble to the high order */
            bitplane[n/2] <<= 4;
    }
    return 0;
}

static int vaapi_vc1_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    VC1Context * const v = avctx->priv_data;
    MpegEncContext * const s = &v->s;
    FFVAContext * const vactx = ff_vaapi_get_context(avctx);
    VASliceParameterBufferVC1 *slice_param;

    ff_dlog(avctx, "vaapi_vc1_decode_slice(): buffer %p, size %d\n", buffer, size);

    /* Current bit buffer is beyond any marker for VC-1, so skip it */
    if (avctx->codec_id == AV_CODEC_ID_VC1 && IS_MARKER(AV_RB32(buffer))) {
        buffer += 4;
        size -= 4;
    }

    /* Fill in VASliceParameterBufferVC1 */
    slice_param = (VASliceParameterBufferVC1 *)ff_vaapi_alloc_slice(vactx, buffer, size);
    if (!slice_param)
        return -1;
    slice_param->macroblock_offset       = get_bits_count(&s->gb);
    slice_param->slice_vertical_position = s->mb_y;
    return 0;
}

#if CONFIG_WMV3_VAAPI_HWACCEL
AVHWAccel ff_wmv3_vaapi_hwaccel = {
    .name           = "wmv3_vaapi",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .pix_fmt        = AV_PIX_FMT_VAAPI,
    .start_frame    = vaapi_vc1_start_frame,
    .end_frame      = ff_vaapi_mpeg_end_frame,
    .decode_slice   = vaapi_vc1_decode_slice,
    .init           = ff_vaapi_context_init,
    .uninit         = ff_vaapi_context_fini,
    .priv_data_size = sizeof(FFVAContext),
};
#endif

AVHWAccel ff_vc1_vaapi_hwaccel = {
    .name           = "vc1_vaapi",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_VAAPI,
    .start_frame    = vaapi_vc1_start_frame,
    .end_frame      = ff_vaapi_mpeg_end_frame,
    .decode_slice   = vaapi_vc1_decode_slice,
    .init           = ff_vaapi_context_init,
    .uninit         = ff_vaapi_context_fini,
    .priv_data_size = sizeof(FFVAContext),
};
