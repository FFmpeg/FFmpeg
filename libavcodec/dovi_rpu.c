/*
 * Dolby Vision RPU decoder
 *
 * Copyright (C) 2021 Jan Ekström
 * Copyright (C) 2021 Niklas Haas
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

#include "libavutil/buffer.h"

#include "dovi_rpu.h"
#include "golomb.h"
#include "get_bits.h"

enum {
    RPU_COEFF_FIXED = 0,
    RPU_COEFF_FLOAT = 1,
};

/**
 * Private contents of vdr_ref.
 */
typedef struct DOVIVdrRef {
    AVDOVIDataMapping mapping;
    AVDOVIColorMetadata color;
} DOVIVdrRef;

void ff_dovi_ctx_unref(DOVIContext *s)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(s->vdr_ref); i++)
        av_buffer_unref(&s->vdr_ref[i]);

    *s = (DOVIContext) {
        .logctx = s->logctx,
    };
}

void ff_dovi_ctx_flush(DOVIContext *s)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(s->vdr_ref); i++)
        av_buffer_unref(&s->vdr_ref[i]);

    *s = (DOVIContext) {
        .logctx = s->logctx,
        .dv_profile = s->dv_profile,
    };
}

int ff_dovi_ctx_replace(DOVIContext *s, const DOVIContext *s0)
{
    int ret;
    s->logctx = s0->logctx;
    s->mapping = s0->mapping;
    s->color = s0->color;
    s->dv_profile = s0->dv_profile;
    for (int i = 0; i < DOVI_MAX_DM_ID; i++) {
        if ((ret = av_buffer_replace(&s->vdr_ref[i], s0->vdr_ref[i])) < 0)
            goto fail;
    }

    return 0;

fail:
    ff_dovi_ctx_unref(s);
    return ret;
}

void ff_dovi_update_cfg(DOVIContext *s, const AVDOVIDecoderConfigurationRecord *cfg)
{
    if (!cfg)
        return;

    s->dv_profile = cfg->dv_profile;
}

int ff_dovi_attach_side_data(DOVIContext *s, AVFrame *frame)
{
    AVFrameSideData *sd;
    AVBufferRef *buf;
    AVDOVIMetadata *dovi;
    size_t dovi_size;

    if (!s->mapping || !s->color)
        return 0; /* incomplete dovi metadata */

    dovi = av_dovi_metadata_alloc(&dovi_size);
    if (!dovi)
        return AVERROR(ENOMEM);

    buf = av_buffer_create((uint8_t *) dovi, dovi_size, NULL, NULL, 0);
    if (!buf) {
        av_free(dovi);
        return AVERROR(ENOMEM);
    }

    sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_METADATA, buf);
    if (!sd) {
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    /* Copy only the parts of these structs known to us at compiler-time. */
#define COPY(t, a, b, last) memcpy(a, b, offsetof(t, last) + sizeof((b)->last))
    COPY(AVDOVIRpuDataHeader, av_dovi_get_header(dovi), &s->header, disable_residual_flag);
    COPY(AVDOVIDataMapping, av_dovi_get_mapping(dovi), s->mapping, nlq[2].linear_deadzone_threshold);
    COPY(AVDOVIColorMetadata, av_dovi_get_color(dovi), s->color, source_diagonal);
    return 0;
}

static int guess_profile(const AVDOVIRpuDataHeader *hdr)
{
    switch (hdr->vdr_rpu_profile) {
    case 0:
        if (hdr->bl_video_full_range_flag)
            return 5;
        break;
    case 1:
        if (hdr->el_spatial_resampling_filter_flag && !hdr->disable_residual_flag) {
            if (hdr->vdr_bit_depth == 12) {
                return 7;
            } else {
                return 4;
            }
        } else {
            return 8;
        }
    }

    return 0; /* unknown */
}

static inline uint64_t get_ue_coef(GetBitContext *gb, const AVDOVIRpuDataHeader *hdr)
{
    uint64_t ipart;
    union { uint32_t u32; float f32; } fpart;

    switch (hdr->coef_data_type) {
    case RPU_COEFF_FIXED:
        ipart = get_ue_golomb_long(gb);
        fpart.u32 = get_bits_long(gb, hdr->coef_log2_denom);
        return (ipart << hdr->coef_log2_denom) + fpart.u32;

    case RPU_COEFF_FLOAT:
        fpart.u32 = get_bits_long(gb, 32);
        return fpart.f32 * (1LL << hdr->coef_log2_denom);
    }

    return 0; /* unreachable */
}

static inline int64_t get_se_coef(GetBitContext *gb, const AVDOVIRpuDataHeader *hdr)
{
    int64_t ipart;
    union { uint32_t u32; float f32; } fpart;

    switch (hdr->coef_data_type) {
    case RPU_COEFF_FIXED:
        ipart = get_se_golomb_long(gb);
        fpart.u32 = get_bits_long(gb, hdr->coef_log2_denom);
        return ipart * (1LL << hdr->coef_log2_denom) + fpart.u32;

    case RPU_COEFF_FLOAT:
        fpart.u32 = get_bits_long(gb, 32);
        return fpart.f32 * (1LL << hdr->coef_log2_denom);
    }

    return 0; /* unreachable */
}

#define VALIDATE(VAR, MIN, MAX)                                                 \
    do {                                                                        \
        if (VAR < MIN || VAR > MAX) {                                           \
            av_log(s->logctx, AV_LOG_ERROR, "RPU validation failed: "           \
                   #MIN" <= "#VAR" = %d <= "#MAX"\n", (int) VAR);               \
            goto fail;                                                          \
        }                                                                       \
    } while (0)

int ff_dovi_rpu_parse(DOVIContext *s, const uint8_t *rpu, size_t rpu_size)
{
    AVDOVIRpuDataHeader *hdr = &s->header;
    GetBitContext *gb = &(GetBitContext){0};
    DOVIVdrRef *vdr;
    int ret;

    uint8_t nal_prefix;
    uint8_t rpu_type;
    uint8_t vdr_seq_info_present;
    uint8_t vdr_dm_metadata_present;
    uint8_t use_prev_vdr_rpu;
    uint8_t use_nlq;
    uint8_t profile;
    if ((ret = init_get_bits8(gb, rpu, rpu_size)) < 0)
        return ret;

    /* RPU header, common values */
    nal_prefix = get_bits(gb, 8);
    VALIDATE(nal_prefix, 25, 25);
    rpu_type = get_bits(gb, 6);
    if (rpu_type != 2) {
        av_log(s->logctx, AV_LOG_WARNING, "Unrecognized RPU type "
               "%"PRIu8", ignoring\n", rpu_type);
        return 0;
    }

    hdr->rpu_type = rpu_type;
    hdr->rpu_format = get_bits(gb, 11);

    /* Values specific to RPU type 2 */
    hdr->vdr_rpu_profile = get_bits(gb, 4);
    hdr->vdr_rpu_level = get_bits(gb, 4);

    vdr_seq_info_present = get_bits1(gb);
    if (vdr_seq_info_present) {
        hdr->chroma_resampling_explicit_filter_flag = get_bits1(gb);
        hdr->coef_data_type = get_bits(gb, 2);
        VALIDATE(hdr->coef_data_type, RPU_COEFF_FIXED, RPU_COEFF_FLOAT);
        switch (hdr->coef_data_type) {
        case RPU_COEFF_FIXED:
            hdr->coef_log2_denom = get_ue_golomb(gb);
            VALIDATE(hdr->coef_log2_denom, 13, 32);
            break;
        case RPU_COEFF_FLOAT:
            hdr->coef_log2_denom = 32; /* arbitrary, choose maximum precision */
            break;
        }

        hdr->vdr_rpu_normalized_idc = get_bits(gb, 2);
        hdr->bl_video_full_range_flag = get_bits1(gb);

        if ((hdr->rpu_format & 0x700) == 0) {
            int bl_bit_depth_minus8 = get_ue_golomb_31(gb);
            int el_bit_depth_minus8 = get_ue_golomb_31(gb);
            int vdr_bit_depth_minus8 = get_ue_golomb_31(gb);
            VALIDATE(bl_bit_depth_minus8, 0, 8);
            VALIDATE(el_bit_depth_minus8, 0, 8);
            VALIDATE(vdr_bit_depth_minus8, 0, 8);
            hdr->bl_bit_depth = bl_bit_depth_minus8 + 8;
            hdr->el_bit_depth = el_bit_depth_minus8 + 8;
            hdr->vdr_bit_depth = vdr_bit_depth_minus8 + 8;
            hdr->spatial_resampling_filter_flag = get_bits1(gb);
            skip_bits(gb, 3); /* reserved_zero_3bits */
            hdr->el_spatial_resampling_filter_flag = get_bits1(gb);
            hdr->disable_residual_flag = get_bits1(gb);
        }
    }

    if (!hdr->bl_bit_depth) {
        av_log(s->logctx, AV_LOG_ERROR, "Missing RPU VDR sequence info?\n");
        goto fail;
    }

    vdr_dm_metadata_present = get_bits1(gb);
    use_prev_vdr_rpu = get_bits1(gb);
    use_nlq = (hdr->rpu_format & 0x700) == 0 && !hdr->disable_residual_flag;

    profile = s->dv_profile ? s->dv_profile : guess_profile(hdr);
    if (profile == 5 && use_nlq) {
        av_log(s->logctx, AV_LOG_ERROR, "Profile 5 RPUs should not use NLQ\n");
        goto fail;
    }

    if (use_prev_vdr_rpu) {
        int prev_vdr_rpu_id = get_ue_golomb_31(gb);
        VALIDATE(prev_vdr_rpu_id, 0, DOVI_MAX_DM_ID);
        if (!s->vdr_ref[prev_vdr_rpu_id]) {
            av_log(s->logctx, AV_LOG_ERROR, "Unknown previous RPU ID: %u\n",
                   prev_vdr_rpu_id);
            goto fail;
        }
        vdr = (DOVIVdrRef *) s->vdr_ref[prev_vdr_rpu_id]->data;
        s->mapping = &vdr->mapping;
    } else {
        int vdr_rpu_id = get_ue_golomb_31(gb);
        VALIDATE(vdr_rpu_id, 0, DOVI_MAX_DM_ID);
        if (!s->vdr_ref[vdr_rpu_id]) {
            s->vdr_ref[vdr_rpu_id] = av_buffer_allocz(sizeof(DOVIVdrRef));
            if (!s->vdr_ref[vdr_rpu_id])
                return AVERROR(ENOMEM);
        }

        vdr = (DOVIVdrRef *) s->vdr_ref[vdr_rpu_id]->data;
        s->mapping = &vdr->mapping;

        vdr->mapping.vdr_rpu_id = vdr_rpu_id;
        vdr->mapping.mapping_color_space = get_ue_golomb_31(gb);
        vdr->mapping.mapping_chroma_format_idc = get_ue_golomb_31(gb);

        for (int c = 0; c < 3; c++) {
            AVDOVIReshapingCurve *curve = &vdr->mapping.curves[c];
            int num_pivots_minus_2 = get_ue_golomb_31(gb);
            int pivot = 0;

            VALIDATE(num_pivots_minus_2, 0, AV_DOVI_MAX_PIECES - 1);
            curve->num_pivots = num_pivots_minus_2 + 2;
            for (int i = 0; i < curve->num_pivots; i++) {
                pivot += get_bits(gb, hdr->bl_bit_depth);
                curve->pivots[i] = av_clip_uint16(pivot);
            }
        }

        if (use_nlq) {
            vdr->mapping.nlq_method_idc = get_bits(gb, 3);
            /**
             * The patent mentions another legal value, NLQ_MU_LAW, but it's
             * not documented anywhere how to parse or apply that type of NLQ.
             */
            VALIDATE(vdr->mapping.nlq_method_idc, 0, AV_DOVI_NLQ_LINEAR_DZ);
        } else {
            vdr->mapping.nlq_method_idc = AV_DOVI_NLQ_NONE;
        }

        vdr->mapping.num_x_partitions = get_ue_golomb_long(gb) + 1;
        vdr->mapping.num_y_partitions = get_ue_golomb_long(gb) + 1;
        /* End of rpu_data_header(), start of vdr_rpu_data_payload() */

        for (int c = 0; c < 3; c++) {
            AVDOVIReshapingCurve *curve = &vdr->mapping.curves[c];
            for (int i = 0; i < curve->num_pivots - 1; i++) {
                int mapping_idc = get_ue_golomb_31(gb);
                VALIDATE(mapping_idc, 0, 1);
                curve->mapping_idc[i] = mapping_idc;
                switch (mapping_idc) {
                case AV_DOVI_MAPPING_POLYNOMIAL: {
                    int poly_order_minus1 = get_ue_golomb_31(gb);
                    VALIDATE(poly_order_minus1, 0, 1);
                    curve->poly_order[i] = poly_order_minus1 + 1;
                    if (poly_order_minus1 == 0) {
                        int linear_interp_flag = get_bits1(gb);
                        if (linear_interp_flag) {
                            /* lack of documentation/samples */
                            avpriv_request_sample(s->logctx, "Dolby Vision "
                                                  "linear interpolation");
                            ff_dovi_ctx_unref(s);
                            return AVERROR_PATCHWELCOME;
                        }
                    }
                    for (int k = 0; k <= curve->poly_order[i]; k++)
                        curve->poly_coef[i][k] = get_se_coef(gb, hdr);
                    break;
                }
                case AV_DOVI_MAPPING_MMR: {
                    int mmr_order_minus1 = get_bits(gb, 2);
                    VALIDATE(mmr_order_minus1, 0, 2);
                    curve->mmr_order[i] = mmr_order_minus1 + 1;
                    curve->mmr_constant[i] = get_se_coef(gb, hdr);
                    for (int j = 0; j < curve->mmr_order[i]; j++) {
                        for (int k = 0; k < 7; k++)
                            curve->mmr_coef[i][j][k] = get_se_coef(gb, hdr);
                    }
                    break;
                }
                }
            }
        }

        if (use_nlq) {
            for (int c = 0; c < 3; c++) {
                AVDOVINLQParams *nlq = &vdr->mapping.nlq[c];
                nlq->nlq_offset = get_bits(gb, hdr->el_bit_depth);
                nlq->vdr_in_max = get_ue_coef(gb, hdr);
                switch (vdr->mapping.nlq_method_idc) {
                case AV_DOVI_NLQ_LINEAR_DZ:
                    nlq->linear_deadzone_slope = get_ue_coef(gb, hdr);
                    nlq->linear_deadzone_threshold = get_ue_coef(gb, hdr);
                    break;
                }
            }
        }
    }

    if (vdr_dm_metadata_present) {
        AVDOVIColorMetadata *color;
        int affected_dm_id = get_ue_golomb_31(gb);
        int current_dm_id = get_ue_golomb_31(gb);
        VALIDATE(affected_dm_id, 0, DOVI_MAX_DM_ID);
        VALIDATE(current_dm_id, 0, DOVI_MAX_DM_ID);
        if (!s->vdr_ref[affected_dm_id]) {
            s->vdr_ref[affected_dm_id] = av_buffer_allocz(sizeof(DOVIVdrRef));
            if (!s->vdr_ref[affected_dm_id])
                return AVERROR(ENOMEM);
        }

        if (!s->vdr_ref[current_dm_id]) {
            av_log(s->logctx, AV_LOG_ERROR, "Unknown previous RPU DM ID: %u\n",
                   current_dm_id);
            goto fail;
        }

        /* Update current pointer based on current_dm_id */
        vdr = (DOVIVdrRef *) s->vdr_ref[current_dm_id]->data;
        s->color = &vdr->color;

        /* Update values of affected_dm_id */
        vdr = (DOVIVdrRef *) s->vdr_ref[affected_dm_id]->data;
        color = &vdr->color;
        color->dm_metadata_id = affected_dm_id;
        color->scene_refresh_flag = get_ue_golomb_31(gb);
        for (int i = 0; i < 9; i++)
            color->ycc_to_rgb_matrix[i] = av_make_q(get_sbits(gb, 16), 1 << 13);
        for (int i = 0; i < 3; i++) {
            int denom = profile == 4 ? (1 << 30) : (1 << 28);
            unsigned offset = get_bits_long(gb, 32);
            if (offset > INT_MAX) {
                /* Ensure the result fits inside AVRational */
                offset >>= 1;
                denom >>= 1;
            }
            color->ycc_to_rgb_offset[i] = av_make_q(offset, denom);
        }
        for (int i = 0; i < 9; i++)
            color->rgb_to_lms_matrix[i] = av_make_q(get_sbits(gb, 16), 1 << 14);

        color->signal_eotf = get_bits(gb, 16);
        color->signal_eotf_param0 = get_bits(gb, 16);
        color->signal_eotf_param1 = get_bits(gb, 16);
        color->signal_eotf_param2 = get_bits_long(gb, 32);
        color->signal_bit_depth = get_bits(gb, 5);
        VALIDATE(color->signal_bit_depth, 8, 16);
        color->signal_color_space = get_bits(gb, 2);
        color->signal_chroma_format = get_bits(gb, 2);
        color->signal_full_range_flag = get_bits(gb, 2);
        color->source_min_pq = get_bits(gb, 12);
        color->source_max_pq = get_bits(gb, 12);
        color->source_diagonal = get_bits(gb, 10);
    }

    /* FIXME: verify CRC32, requires implementation of AV_CRC_32_MPEG_2 */
    return 0;

fail:
    ff_dovi_ctx_unref(s); /* don't leak potentially invalid state */
    return AVERROR(EINVAL);
}
