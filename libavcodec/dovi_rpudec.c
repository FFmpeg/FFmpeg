/*
 * Dolby Vision RPU decoder
 *
 * Copyright (C) 2021 Jan Ekström
 * Copyright (C) 2021-2024 Niklas Haas
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

#include "libavutil/mem.h"
#include "libavutil/crc.h"

#include "avcodec.h"
#include "dovi_rpu.h"
#include "golomb.h"
#include "get_bits.h"
#include "libavutil/refstruct.h"

int ff_dovi_get_metadata(DOVIContext *s, AVDOVIMetadata **out_metadata)
{
    AVDOVIMetadata *dovi;
    size_t dovi_size;

    if (!s->mapping || !s->color)
        return 0; /* incomplete dovi metadata */

    dovi = av_dovi_metadata_alloc(&dovi_size);
    if (!dovi)
        return AVERROR(ENOMEM);

    /* Copy only the parts of these structs known to us at compiler-time. */
#define COPY(t, a, b, last) memcpy(a, b, offsetof(t, last) + sizeof((b)->last))
    COPY(AVDOVIRpuDataHeader, av_dovi_get_header(dovi), &s->header, ext_mapping_idc_5_7);
    COPY(AVDOVIDataMapping, av_dovi_get_mapping(dovi), s->mapping, nlq_pivots);
    COPY(AVDOVIColorMetadata, av_dovi_get_color(dovi), s->color, source_diagonal);

    if (s->ext_blocks) {
        const DOVIExt *ext = s->ext_blocks;
        size_t ext_sz = FFMIN(sizeof(AVDOVIDmData), dovi->ext_block_size);
        for (int i = 0; i < ext->num_static; i++)
            memcpy(av_dovi_get_ext(dovi, dovi->num_ext_blocks++), &ext->dm_static[i], ext_sz);
        for (int i = 0; i < ext->num_dynamic; i++)
            memcpy(av_dovi_get_ext(dovi, dovi->num_ext_blocks++), &ext->dm_dynamic[i], ext_sz);
    }

    *out_metadata = dovi;
    return dovi_size;
}

int ff_dovi_attach_side_data(DOVIContext *s, AVFrame *frame)
{
    AVFrameSideData *sd;
    AVDOVIMetadata *dovi;
    AVBufferRef *buf;
    int size;

    size = ff_dovi_get_metadata(s, &dovi);
    if (size <= 0)
        return size;

    buf = av_buffer_create((uint8_t *) dovi, size, NULL, NULL, 0);
    if (!buf) {
        av_free(dovi);
        return AVERROR(ENOMEM);
    }

    sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_METADATA, buf);
    if (!sd) {
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static inline uint64_t get_ue_coef(GetBitContext *gb, const AVDOVIRpuDataHeader *hdr)
{
    uint64_t ipart;
    union { uint32_t u32; float f32; } fpart;

    switch (hdr->coef_data_type) {
    case RPU_COEFF_FIXED:
        ipart = get_ue_golomb_long(gb);
        fpart.u32 = get_bits_long(gb, hdr->coef_log2_denom);
        return (ipart << hdr->coef_log2_denom) | fpart.u32;

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
        return ipart * (1LL << hdr->coef_log2_denom) | fpart.u32;

    case RPU_COEFF_FLOAT:
        fpart.u32 = get_bits_long(gb, 32);
        return fpart.f32 * (1LL << hdr->coef_log2_denom);
    }

    return 0; /* unreachable */
}

static inline unsigned get_variable_bits(GetBitContext *gb, int n)
{
    unsigned int value = get_bits(gb, n);
    int read_more = get_bits1(gb);
    while (read_more) {
        value = (value + 1) << n;
        value |= get_bits(gb, n);
        read_more = get_bits1(gb);
    }
    return value;
}

#define VALIDATE(VAR, MIN, MAX)                                                 \
    do {                                                                        \
        if (VAR < MIN || VAR > MAX) {                                           \
            av_log(s->logctx, AV_LOG_ERROR, "RPU validation failed: "           \
                   #MIN" <= "#VAR" = %d <= "#MAX"\n", (int) VAR);               \
            ff_dovi_ctx_unref(s);                                               \
            return AVERROR_INVALIDDATA;                                         \
        }                                                                       \
    } while (0)

static int parse_ext_v1(DOVIContext *s, GetBitContext *gb, AVDOVIDmData *dm)
{
    switch (dm->level) {
    case 1:
        dm->l1.min_pq = get_bits(gb, 12);
        dm->l1.max_pq = get_bits(gb, 12);
        dm->l1.avg_pq = get_bits(gb, 12);
        break;
    case 2:
        dm->l2.target_max_pq = get_bits(gb, 12);
        dm->l2.trim_slope = get_bits(gb, 12);
        dm->l2.trim_offset = get_bits(gb, 12);
        dm->l2.trim_power = get_bits(gb, 12);
        dm->l2.trim_chroma_weight = get_bits(gb, 12);
        dm->l2.trim_saturation_gain = get_bits(gb, 12);
        dm->l2.ms_weight = get_sbits(gb, 13);
        VALIDATE(dm->l2.ms_weight, -1, 4095);
        break;
    case 4:
        dm->l4.anchor_pq = get_bits(gb, 12);
        dm->l4.anchor_power = get_bits(gb, 12);
        break;
    case 5:
        dm->l5.left_offset = get_bits(gb, 13);
        dm->l5.right_offset = get_bits(gb, 13);
        dm->l5.top_offset = get_bits(gb, 13);
        dm->l5.bottom_offset = get_bits(gb, 13);
        break;
    case 6:
        dm->l6.max_luminance = get_bits(gb, 16);
        dm->l6.min_luminance = get_bits(gb, 16);
        dm->l6.max_cll = get_bits(gb, 16);
        dm->l6.max_fall = get_bits(gb, 16);
        break;
    case 255:
        dm->l255.dm_run_mode = get_bits(gb, 8);
        dm->l255.dm_run_version = get_bits(gb, 8);
        for (int i = 0; i < 4; i++)
            dm->l255.dm_debug[i] = get_bits(gb, 8);
        break;
    default:
        av_log(s->logctx, AV_LOG_WARNING,
               "Unknown Dolby Vision DM v1 level: %u\n", dm->level);
    }

    return 0;
}

static AVCIExy get_cie_xy(GetBitContext *gb)
{
    AVCIExy xy;
    const int denom = 32767;
    xy.x = av_make_q(get_sbits(gb, 16), denom);
    xy.y = av_make_q(get_sbits(gb, 16), denom);
    return xy;
}

static int parse_ext_v2(DOVIContext *s, GetBitContext *gb, AVDOVIDmData *dm,
                        int ext_block_length)
{
    switch (dm->level) {
    case 3:
        dm->l3.min_pq_offset = get_bits(gb, 12);
        dm->l3.max_pq_offset = get_bits(gb, 12);
        dm->l3.avg_pq_offset = get_bits(gb, 12);
        break;
    case 8:
        dm->l8.target_display_index = get_bits(gb, 8);
        dm->l8.trim_slope = get_bits(gb, 12);
        dm->l8.trim_offset = get_bits(gb, 12);
        dm->l8.trim_power = get_bits(gb, 12);
        dm->l8.trim_chroma_weight = get_bits(gb, 12);
        dm->l8.trim_saturation_gain = get_bits(gb, 12);
        dm->l8.ms_weight = get_bits(gb, 12);
        if (ext_block_length < 12)
            break;
        dm->l8.target_mid_contrast = get_bits(gb, 12);
        if (ext_block_length < 13)
            break;
        dm->l8.clip_trim = get_bits(gb, 12);
        if (ext_block_length < 19)
            break;
        for (int i = 0; i < 6; i++)
            dm->l8.saturation_vector_field[i] = get_bits(gb, 8);
        if (ext_block_length < 25)
            break;
        for (int i = 0; i < 6; i++)
            dm->l8.hue_vector_field[i] = get_bits(gb, 8);
        break;
    case 9:
        dm->l9.source_primary_index = get_bits(gb, 8);
        if (ext_block_length < 17)
            break;
        dm->l9.source_display_primaries.prim.r = get_cie_xy(gb);
        dm->l9.source_display_primaries.prim.g = get_cie_xy(gb);
        dm->l9.source_display_primaries.prim.b = get_cie_xy(gb);
        dm->l9.source_display_primaries.wp = get_cie_xy(gb);
        break;
    case 10:
        dm->l10.target_display_index = get_bits(gb, 8);
        dm->l10.target_max_pq = get_bits(gb, 12);
        dm->l10.target_min_pq = get_bits(gb, 12);
        dm->l10.target_primary_index = get_bits(gb, 8);
        if (ext_block_length < 21)
            break;
        dm->l10.target_display_primaries.prim.r = get_cie_xy(gb);
        dm->l10.target_display_primaries.prim.g = get_cie_xy(gb);
        dm->l10.target_display_primaries.prim.b = get_cie_xy(gb);
        dm->l10.target_display_primaries.wp = get_cie_xy(gb);
        break;
    case 11:
        dm->l11.content_type = get_bits(gb, 8);
        dm->l11.whitepoint = get_bits(gb, 4);
        dm->l11.reference_mode_flag = get_bits1(gb);
        skip_bits(gb, 3); /* reserved */
        dm->l11.sharpness = get_bits(gb, 2);
        dm->l11.noise_reduction = get_bits(gb, 2);
        dm->l11.mpeg_noise_reduction = get_bits(gb, 2);
        dm->l11.frame_rate_conversion = get_bits(gb, 2);
        dm->l11.brightness = get_bits(gb, 2);
        dm->l11.color = get_bits(gb, 2);
        break;
    case 254:
        dm->l254.dm_mode = get_bits(gb, 8);
        dm->l254.dm_version_index = get_bits(gb, 8);
        break;
    default:
        av_log(s->logctx, AV_LOG_WARNING,
               "Unknown Dolby Vision DM v2 level: %u\n", dm->level);
    }

    return 0;
}

static int parse_ext_blocks(DOVIContext *s, GetBitContext *gb, int ver,
                            int compression, int err_recognition)
{
    int num_ext_blocks, ext_block_length, start_pos, parsed_bits, ret;
    DOVIExt *ext = s->ext_blocks;

    num_ext_blocks = get_ue_golomb_31(gb);
    align_get_bits(gb);

    if (num_ext_blocks && !ext) {
        ext = s->ext_blocks = av_refstruct_allocz(sizeof(*s->ext_blocks));
        if (!ext)
            return AVERROR(ENOMEM);
    }

    while (num_ext_blocks--) {
        AVDOVIDmData dummy;
        AVDOVIDmData *dm;
        uint8_t level;

        ext_block_length = get_ue_golomb_31(gb);
        level = get_bits(gb, 8);
        start_pos = get_bits_count(gb);

        if (ff_dovi_rpu_extension_is_static(level)) {
            if (compression) {
                av_log(s->logctx, AV_LOG_WARNING, "Compressed DM RPU contains "
                       "static extension block level %d\n", level);
                if (err_recognition & (AV_EF_AGGRESSIVE | AV_EF_EXPLODE))
                    return AVERROR_INVALIDDATA;
                dm = &dummy;
            } else {
                if (ext->num_static >= FF_ARRAY_ELEMS(ext->dm_static))
                    return AVERROR_INVALIDDATA;
                dm = &ext->dm_static[ext->num_static++];
            }
        } else {
            if (ext->num_dynamic >= FF_ARRAY_ELEMS(ext->dm_dynamic))
                return AVERROR_INVALIDDATA;
            dm = &ext->dm_dynamic[ext->num_dynamic++];
        }

        memset(dm, 0, sizeof(*dm));
        dm->level = level;
        switch (ver) {
        case 1: ret = parse_ext_v1(s, gb, dm); break;
        case 2: ret = parse_ext_v2(s, gb, dm, ext_block_length); break;
        default: return AVERROR_BUG;
        }

        if (ret < 0)
            return ret;

        parsed_bits = get_bits_count(gb) - start_pos;
        if (parsed_bits > ext_block_length * 8)
            return AVERROR_INVALIDDATA;
        skip_bits(gb, ext_block_length * 8 - parsed_bits);
    }

    return 0;
}

int ff_dovi_rpu_parse(DOVIContext *s, const uint8_t *rpu, size_t rpu_size,
                      int err_recognition)
{
    AVDOVIRpuDataHeader *hdr = &s->header;
    GetBitContext *gb = &(GetBitContext){0};
    int ret;

    uint8_t rpu_type;
    uint8_t vdr_seq_info_present;
    uint8_t vdr_dm_metadata_present;
    uint8_t dm_compression = 0;
    uint8_t use_prev_vdr_rpu;
    uint8_t use_nlq;
    uint8_t profile;
    uint8_t compression = s->cfg.dv_profile ? s->cfg.dv_md_compression : 0;

    if (rpu_size < 5)
        return AVERROR_INVALIDDATA;

    /* Container */
    if (s->cfg.dv_profile == 10 /* dav1.10 */) {
        /* DV inside AV1 re-uses an EMDF container skeleton, but with fixed
         * values - so we can effectively treat this as a magic byte sequence.
         *
         * The exact fields are, as follows:
         *   emdf_version            : f(2) = 0
         *   key_id                  : f(3) = 6
         *   emdf_payload_id         : f(5) = 31
         *   emdf_payload_id_ext     : var(5) = 225
         *   smploffste              : f(1) = 0
         *   duratione               : f(1) = 0
         *   groupide                : f(1) = 0
         *   codecdatae              : f(1) = 0
         *   discard_unknown_payload : f(1) = 1
         */
        const unsigned header_magic = 0x01be6841u;
        unsigned emdf_header, emdf_payload_size, emdf_protection;
        if ((ret = init_get_bits8(gb, rpu, rpu_size)) < 0)
            return ret;
        emdf_header = get_bits_long(gb, 27);
        VALIDATE(emdf_header, header_magic, header_magic);
        emdf_payload_size = get_variable_bits(gb, 8);
        VALIDATE(emdf_payload_size, 6, 512);
        if (emdf_payload_size * 8 > get_bits_left(gb))
            return AVERROR_INVALIDDATA;

        /* The payload is not byte-aligned (off by *one* bit, curse Dolby),
         * so copy into a fresh buffer to preserve byte alignment of the
         * RPU struct */
        av_fast_padded_malloc(&s->rpu_buf, &s->rpu_buf_sz, emdf_payload_size);
        if (!s->rpu_buf)
            return AVERROR(ENOMEM);
        for (int i = 0; i < emdf_payload_size; i++)
            s->rpu_buf[i] = get_bits(gb, 8);
        rpu = s->rpu_buf;
        rpu_size = emdf_payload_size;

        /* Validate EMDF footer */
        emdf_protection = get_bits(gb, 5 + 12);
        VALIDATE(emdf_protection, 0x400, 0x400);
    } else {
        /* NAL unit with prefix and trailing zeroes */
        VALIDATE(rpu[0], 25, 25); /* NAL prefix */
        rpu++;
        rpu_size--;
        /* Strip trailing padding bytes */
        while (rpu_size && rpu[rpu_size - 1] == 0)
            rpu_size--;
    }

    if (!rpu_size || rpu[rpu_size - 1] != 0x80)
        return AVERROR_INVALIDDATA;

    if (err_recognition & AV_EF_CRCCHECK) {
        uint32_t crc = av_bswap32(av_crc(av_crc_get_table(AV_CRC_32_IEEE),
                                  -1, rpu, rpu_size - 1)); /* exclude 0x80 */
        if (crc) {
            av_log(s->logctx, AV_LOG_ERROR, "RPU CRC mismatch: %X\n", crc);
            if (err_recognition & AV_EF_EXPLODE)
                return AVERROR_INVALIDDATA;
        }
    }

    if ((ret = init_get_bits8(gb, rpu, rpu_size)) < 0)
        return ret;

    /* RPU header */
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
            int el_bit_depth_minus8 = get_ue_golomb_long(gb);
            int vdr_bit_depth_minus8 = get_ue_golomb_31(gb);
            /* ext_mapping_idc is in the upper 8 bits of el_bit_depth_minus8 */
            int ext_mapping_idc = el_bit_depth_minus8 >> 8;
            el_bit_depth_minus8 = el_bit_depth_minus8 & 0xFF;
            VALIDATE(bl_bit_depth_minus8, 0, 8);
            VALIDATE(el_bit_depth_minus8, 0, 8);
            VALIDATE(ext_mapping_idc, 0, 0xFF);
            VALIDATE(vdr_bit_depth_minus8, 0, 8);
            hdr->bl_bit_depth = bl_bit_depth_minus8 + 8;
            hdr->el_bit_depth = el_bit_depth_minus8 + 8;
            hdr->ext_mapping_idc_0_4 = ext_mapping_idc & 0x1f; /* 5 bits */
            hdr->ext_mapping_idc_5_7 = ext_mapping_idc >> 5;
            hdr->vdr_bit_depth = vdr_bit_depth_minus8 + 8;
            hdr->spatial_resampling_filter_flag = get_bits1(gb);
            dm_compression = get_bits(gb, 3);
            hdr->el_spatial_resampling_filter_flag = get_bits1(gb);
            hdr->disable_residual_flag = get_bits1(gb);
        } else {
            avpriv_request_sample(s->logctx, "Unsupported RPU format 0x%x\n", hdr->rpu_format);
            ff_dovi_ctx_unref(s);
            return AVERROR_PATCHWELCOME;
        }
    } else {
        /* lack of documentation/samples */
        avpriv_request_sample(s->logctx, "Missing RPU VDR sequence info\n");
        ff_dovi_ctx_unref(s);
        return AVERROR_PATCHWELCOME;
    }

    vdr_dm_metadata_present = get_bits1(gb);
    if (dm_compression > 1) {
        /* It seems no device supports this */
        av_log(s->logctx, AV_LOG_ERROR, "Dynamic metadata compression is not "
               "yet implemented");
        return AVERROR_PATCHWELCOME;
    } else if (dm_compression && !vdr_dm_metadata_present) {
        av_log(s->logctx, AV_LOG_ERROR, "Nonzero DM metadata compression method "
               "but no DM metadata present");
        return AVERROR_INVALIDDATA;
    }

    use_prev_vdr_rpu = get_bits1(gb);
    use_nlq = (hdr->rpu_format & 0x700) == 0 && !hdr->disable_residual_flag;

    profile = s->cfg.dv_profile ? s->cfg.dv_profile : ff_dovi_guess_profile_hevc(hdr);
    if (profile == 5 && use_nlq) {
        av_log(s->logctx, AV_LOG_ERROR, "Profile 5 RPUs should not use NLQ\n");
        ff_dovi_ctx_unref(s);
        return AVERROR_INVALIDDATA;
    }

    if (err_recognition & (AV_EF_COMPLIANT | AV_EF_CAREFUL)) {
        if (profile < 8 && compression) {
            av_log(s->logctx, AV_LOG_ERROR, "Profile %d RPUs should not use "
                   "metadata compression.", profile);
            return AVERROR_INVALIDDATA;
        }

        if (use_prev_vdr_rpu && !compression) {
            av_log(s->logctx, AV_LOG_ERROR, "Uncompressed RPUs should not have "
                   "use_prev_vdr_rpu=1\n");
            return AVERROR_INVALIDDATA;
        }

        if (dm_compression && !compression) {
            av_log(s->logctx, AV_LOG_ERROR, "Uncompressed RPUs should not use "
                   "dm_compression=%d\n", dm_compression);
            return AVERROR_INVALIDDATA;
        }
    }

    if (use_prev_vdr_rpu) {
        int prev_vdr_rpu_id = get_ue_golomb_31(gb);
        VALIDATE(prev_vdr_rpu_id, 0, DOVI_MAX_DM_ID);
        if (!s->vdr[prev_vdr_rpu_id])
            prev_vdr_rpu_id = 0;
        if (!s->vdr[prev_vdr_rpu_id]) {
            /* FIXME: Technically, the spec says that in this case we should
             * synthesize "neutral" vdr metadata, but easier to just error
             * out as this corner case is not hit in practice */
            av_log(s->logctx, AV_LOG_ERROR, "Unknown previous RPU ID: %u\n",
                   prev_vdr_rpu_id);
            ff_dovi_ctx_unref(s);
            return AVERROR_INVALIDDATA;
        }
        s->mapping = s->vdr[prev_vdr_rpu_id];
    } else {
        AVDOVIDataMapping *mapping;
        int vdr_rpu_id = get_ue_golomb_31(gb);
        VALIDATE(vdr_rpu_id, 0, DOVI_MAX_DM_ID);
        if (!s->vdr[vdr_rpu_id]) {
            s->vdr[vdr_rpu_id] = av_refstruct_allocz(sizeof(AVDOVIDataMapping));
            if (!s->vdr[vdr_rpu_id]) {
                ff_dovi_ctx_unref(s);
                return AVERROR(ENOMEM);
            }
        }

        s->mapping = mapping = s->vdr[vdr_rpu_id];
        mapping->vdr_rpu_id = vdr_rpu_id;
        mapping->mapping_color_space = get_ue_golomb_31(gb);
        mapping->mapping_chroma_format_idc = get_ue_golomb_31(gb);

        for (int c = 0; c < 3; c++) {
            AVDOVIReshapingCurve *curve = &mapping->curves[c];
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
            int nlq_pivot = 0;
            mapping->nlq_method_idc = get_bits(gb, 3);

            for (int i = 0; i < 2; i++) {
                nlq_pivot += get_bits(gb, hdr->bl_bit_depth);
                mapping->nlq_pivots[i] = av_clip_uint16(nlq_pivot);
            }

            /**
             * The patent mentions another legal value, NLQ_MU_LAW, but it's
             * not documented anywhere how to parse or apply that type of NLQ.
             */
            VALIDATE(mapping->nlq_method_idc, 0, AV_DOVI_NLQ_LINEAR_DZ);
        } else {
            mapping->nlq_method_idc = AV_DOVI_NLQ_NONE;
        }

        mapping->num_x_partitions = get_ue_golomb_long(gb) + 1;
        mapping->num_y_partitions = get_ue_golomb_long(gb) + 1;
        /* End of rpu_data_header(), start of vdr_rpu_data_payload() */

        for (int c = 0; c < 3; c++) {
            AVDOVIReshapingCurve *curve = &mapping->curves[c];
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
                AVDOVINLQParams *nlq = &mapping->nlq[c];
                nlq->nlq_offset = get_bits(gb, hdr->el_bit_depth);
                nlq->vdr_in_max = get_ue_coef(gb, hdr);
                switch (mapping->nlq_method_idc) {
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
        if (affected_dm_id != current_dm_id) {
            /* The spec does not explain these fields at all, and there is
             * a lack of samples to understand how they're supposed to work,
             * so just assert them being equal for now */
            avpriv_request_sample(s->logctx, "affected/current_dm_metadata_id "
                "mismatch? %u != %u\n", affected_dm_id, current_dm_id);
            ff_dovi_ctx_unref(s);
            return AVERROR_PATCHWELCOME;
        }

        if (!s->dm) {
            s->dm = av_refstruct_allocz(sizeof(AVDOVIColorMetadata));
            if (!s->dm) {
                ff_dovi_ctx_unref(s);
                return AVERROR(ENOMEM);
            }
        }

        s->color = color = s->dm;
        color->dm_metadata_id = affected_dm_id;
        color->scene_refresh_flag = get_ue_golomb_31(gb);
        if (!dm_compression) {
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

        /* Parse extension blocks */
        if (s->ext_blocks) {
            DOVIExt *ext = s->ext_blocks;
            if (!dm_compression)
                ext->num_static = 0;
            ext->num_dynamic = 0;
        }
        if ((ret = parse_ext_blocks(s, gb, 1, dm_compression, err_recognition)) < 0) {
            ff_dovi_ctx_unref(s);
            return ret;
        }

        if (get_bits_left(gb) > 48 /* padding + CRC32 + terminator */) {
            if ((ret = parse_ext_blocks(s, gb, 2, dm_compression, err_recognition)) < 0) {
                ff_dovi_ctx_unref(s);
                return ret;
            }
        }
    } else {
        s->color = &ff_dovi_color_default;
        av_refstruct_unref(&s->ext_blocks);
    }

    return 0;
}
