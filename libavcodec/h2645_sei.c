/*
 * Common H.264 and HEVC Supplementary Enhancement Information messages
 *
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2013 Vittorio Giovara
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

#include "config_components.h"

#include "libavutil/display.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/pixdesc.h"

#include "atsc_a53.h"
#include "avcodec.h"
#include "dynamic_hdr10_plus.h"
#include "dynamic_hdr_vivid.h"
#include "get_bits.h"
#include "golomb.h"
#include "h2645_sei.h"

#define IS_H264(codec_id) (CONFIG_H264_SEI && CONFIG_HEVC_SEI ? codec_id == AV_CODEC_ID_H264 : CONFIG_H264_SEI)
#define IS_HEVC(codec_id) (CONFIG_H264_SEI && CONFIG_HEVC_SEI ? codec_id == AV_CODEC_ID_HEVC : CONFIG_HEVC_SEI)

#if CONFIG_HEVC_SEI
static int decode_registered_user_data_dynamic_hdr_plus(HEVCSEIDynamicHDRPlus *s,
                                                        GetByteContext *gb)
{
    size_t meta_size;
    int err;
    AVDynamicHDRPlus *metadata = av_dynamic_hdr_plus_alloc(&meta_size);
    if (!metadata)
        return AVERROR(ENOMEM);

    err = ff_parse_itu_t_t35_to_dynamic_hdr10_plus(metadata, gb->buffer,
                                                   bytestream2_get_bytes_left(gb));
    if (err < 0) {
        av_free(metadata);
        return err;
    }

    av_buffer_unref(&s->info);
    s->info = av_buffer_create((uint8_t *)metadata, meta_size, NULL, NULL, 0);
    if (!s->info) {
        av_free(metadata);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int decode_registered_user_data_dynamic_hdr_vivid(HEVCSEIDynamicHDRVivid *s,
                                                         GetByteContext *gb)
{
    size_t meta_size;
    int err;
    AVDynamicHDRVivid *metadata = av_dynamic_hdr_vivid_alloc(&meta_size);
    if (!metadata)
        return AVERROR(ENOMEM);

    err = ff_parse_itu_t_t35_to_dynamic_hdr_vivid(metadata,
                                                  gb->buffer, bytestream2_get_bytes_left(gb));
    if (err < 0) {
        av_free(metadata);
        return err;
    }

    av_buffer_unref(&s->info);
    s->info = av_buffer_create((uint8_t *)metadata, meta_size, NULL, NULL, 0);
    if (!s->info) {
        av_free(metadata);
        return AVERROR(ENOMEM);
    }

    return 0;
}
#endif

static int decode_registered_user_data_afd(H264SEIAFD *h, GetByteContext *gb)
{
    int flag;

    if (bytestream2_get_bytes_left(gb) <= 0)
        return AVERROR_INVALIDDATA;

    flag = !!(bytestream2_get_byteu(gb) & 0x40); // active_format_flag

    if (flag) {
        if (bytestream2_get_bytes_left(gb) <= 0)
            return AVERROR_INVALIDDATA;
        h->active_format_description = bytestream2_get_byteu(gb) & 0xF;
        h->present                   = 1;
    }

    return 0;
}

static int decode_registered_user_data_closed_caption(H2645SEIA53Caption *h,
                                                      GetByteContext *gb)
{
    return ff_parse_a53_cc(&h->buf_ref, gb->buffer,
                           bytestream2_get_bytes_left(gb));
}

static int decode_registered_user_data(H2645SEI *h, GetByteContext *gb,
                                       enum AVCodecID codec_id, void *logctx)
{
    int country_code, provider_code;

    if (bytestream2_get_bytes_left(gb) < 3)
        return AVERROR_INVALIDDATA;

    country_code = bytestream2_get_byteu(gb); // itu_t_t35_country_code
    if (country_code == 0xFF) {
        if (bytestream2_get_bytes_left(gb) < 3)
            return AVERROR_INVALIDDATA;

        bytestream2_skipu(gb, 1);  // itu_t_t35_country_code_extension_byte
    }

    if (country_code != 0xB5 && country_code != 0x26) { // usa_country_code and cn_country_code
        av_log(logctx, AV_LOG_VERBOSE,
               "Unsupported User Data Registered ITU-T T35 SEI message (country_code = %d)\n",
               country_code);
        return 0;
    }

    /* itu_t_t35_payload_byte follows */
    provider_code = bytestream2_get_be16u(gb);

    switch (provider_code) {
    case 0x31: { // atsc_provider_code
        uint32_t user_identifier;

        if (bytestream2_get_bytes_left(gb) < 4)
            return AVERROR_INVALIDDATA;

        user_identifier = bytestream2_get_be32u(gb);
        switch (user_identifier) {
        case MKBETAG('D', 'T', 'G', '1'):       // afd_data
            if (!IS_H264(codec_id))
                goto unsupported;
            return decode_registered_user_data_afd(&h->afd, gb);
        case MKBETAG('G', 'A', '9', '4'):       // closed captions
            return decode_registered_user_data_closed_caption(&h->a53_caption, gb);
        default:
        unsupported:
            av_log(logctx, AV_LOG_VERBOSE,
                   "Unsupported User Data Registered ITU-T T35 SEI message (atsc user_identifier = 0x%04x)\n",
                   user_identifier);
            break;
        }
        break;
    }
#if CONFIG_HEVC_SEI
    case 0x04: { // cuva_provider_code
        const uint16_t cuva_provider_oriented_code = 0x0005;
        uint16_t provider_oriented_code;

        if (!IS_HEVC(codec_id))
            goto unsupported_provider_code;

        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;

        provider_oriented_code = bytestream2_get_be16u(gb);
        if (provider_oriented_code == cuva_provider_oriented_code) {
            return decode_registered_user_data_dynamic_hdr_vivid(&h->dynamic_hdr_vivid, gb);
        }
        break;
    }
    case 0x3C: { // smpte_provider_code
        // A/341 Amendment - 2094-40
        const uint16_t smpte2094_40_provider_oriented_code = 0x0001;
        const uint8_t smpte2094_40_application_identifier = 0x04;
        uint16_t provider_oriented_code;
        uint8_t application_identifier;

        if (!IS_HEVC(codec_id))
            goto unsupported_provider_code;

        if (bytestream2_get_bytes_left(gb) < 3)
            return AVERROR_INVALIDDATA;

        provider_oriented_code = bytestream2_get_be16u(gb);
        application_identifier = bytestream2_get_byteu(gb);
        if (provider_oriented_code == smpte2094_40_provider_oriented_code &&
            application_identifier == smpte2094_40_application_identifier) {
            return decode_registered_user_data_dynamic_hdr_plus(&h->dynamic_hdr_plus, gb);
        }
        break;
    }
    unsupported_provider_code:
#endif
    default:
        av_log(logctx, AV_LOG_VERBOSE,
               "Unsupported User Data Registered ITU-T T35 SEI message (provider_code = %d)\n",
               provider_code);
        break;
    }

    return 0;
}

static int decode_unregistered_user_data(H2645SEIUnregistered *h,
                                         GetByteContext *gb,
                                         enum AVCodecID codec_id)
{
    uint8_t *user_data;
    int size = bytestream2_get_bytes_left(gb);
    AVBufferRef *buf_ref, **tmp;

    if (size < 16 || size >= INT_MAX - 1)
        return AVERROR_INVALIDDATA;

    tmp = av_realloc_array(h->buf_ref, h->nb_buf_ref + 1, sizeof(*h->buf_ref));
    if (!tmp)
        return AVERROR(ENOMEM);
    h->buf_ref = tmp;

    buf_ref = av_buffer_alloc(size + 1);
    if (!buf_ref)
        return AVERROR(ENOMEM);
    user_data = buf_ref->data;

    bytestream2_get_bufferu(gb, user_data, size);
    user_data[size] = 0;
    buf_ref->size = size;
    h->buf_ref[h->nb_buf_ref++] = buf_ref;

    if (IS_H264(codec_id)) {
        int e, build;
        e = sscanf(user_data + 16, "x264 - core %d", &build);
        if (e == 1 && build > 0)
            h->x264_build = build;
        if (e == 1 && build == 1 && !strncmp(user_data+16, "x264 - core 0000", 16))
            h->x264_build = 67;
    }

    return 0;
}

static int decode_display_orientation(H2645SEIDisplayOrientation *h,
                                      GetBitContext *gb)
{
    h->present = !get_bits1(gb);  // display_orientation_cancel_flag

    if (h->present) {
        h->hflip = get_bits1(gb);     // hor_flip
        h->vflip = get_bits1(gb);     // ver_flip

        h->anticlockwise_rotation = get_bits(gb, 16);
        // This is followed by display_orientation_repetition_period
        // and display_orientation_extension_flag for H.264
        // and by display_orientation_persistence_flag for HEVC.
    }

    return 0;
}

static int decode_frame_packing_arrangement(H2645SEIFramePacking *h,
                                            GetBitContext *gb,
                                            enum AVCodecID codec_id)
{
    h->arrangement_id          = get_ue_golomb_long(gb);
    h->arrangement_cancel_flag = get_bits1(gb);
    h->present = !h->arrangement_cancel_flag;

    if (h->present) {
        h->arrangement_type              = get_bits(gb, 7);
        h->quincunx_sampling_flag        = get_bits1(gb);
        h->content_interpretation_type   = get_bits(gb, 6);

        // spatial_flipping_flag, frame0_flipped_flag, field_views_flag
        skip_bits(gb, 3);
        h->current_frame_is_frame0_flag  = get_bits1(gb);
        // frame0_self_contained_flag, frame1_self_contained_flag
        skip_bits(gb, 2);

        if (!h->quincunx_sampling_flag && h->arrangement_type != 5)
            skip_bits(gb, 16);      // frame[01]_grid_position_[xy]
        skip_bits(gb, 8);           // frame_packing_arrangement_reserved_byte
        if (IS_H264(codec_id))
            h->arrangement_repetition_period = get_ue_golomb_long(gb);
        else
            skip_bits1(gb); // frame_packing_arrangement_persistence_flag
    }
    // H.264: frame_packing_arrangement_extension_flag,
    // HEVC:  upsampled_aspect_ratio_flag
    skip_bits1(gb);

    return 0;
}

static int decode_alternative_transfer(H2645SEIAlternativeTransfer *s,
                                       GetByteContext *gb)
{
    if (bytestream2_get_bytes_left(gb) < 1)
        return AVERROR_INVALIDDATA;

    s->present = 1;
    s->preferred_transfer_characteristics = bytestream2_get_byteu(gb);

    return 0;
}

static int decode_film_grain_characteristics(H2645SEIFilmGrainCharacteristics *h,
                                             enum AVCodecID codec_id, GetBitContext *gb)
{
    h->present = !get_bits1(gb); // film_grain_characteristics_cancel_flag

    if (h->present) {
        memset(h, 0, sizeof(*h));
        h->model_id = get_bits(gb, 2);
        h->separate_colour_description_present_flag = get_bits1(gb);
        if (h->separate_colour_description_present_flag) {
            h->bit_depth_luma   = get_bits(gb, 3) + 8;
            h->bit_depth_chroma = get_bits(gb, 3) + 8;
            h->full_range       = get_bits1(gb);
            h->color_primaries  = get_bits(gb, 8);
            h->transfer_characteristics = get_bits(gb, 8);
            h->matrix_coeffs    = get_bits(gb, 8);
        }
        h->blending_mode_id  = get_bits(gb, 2);
        h->log2_scale_factor = get_bits(gb, 4);
        for (int c = 0; c < 3; c++)
            h->comp_model_present_flag[c] = get_bits1(gb);
        for (int c = 0; c < 3; c++) {
            if (h->comp_model_present_flag[c]) {
                h->num_intensity_intervals[c] = get_bits(gb, 8) + 1;
                h->num_model_values[c] = get_bits(gb, 3) + 1;
                if (h->num_model_values[c] > 6)
                    return AVERROR_INVALIDDATA;
                for (int i = 0; i < h->num_intensity_intervals[c]; i++) {
                    h->intensity_interval_lower_bound[c][i] = get_bits(gb, 8);
                    h->intensity_interval_upper_bound[c][i] = get_bits(gb, 8);
                    for (int j = 0; j < h->num_model_values[c]; j++)
                        h->comp_model_value[c][i][j] = get_se_golomb_long(gb);
                }
            }
        }
        if (IS_HEVC(codec_id))
            h->persistence_flag = get_bits1(gb);
        else
            h->repetition_period = get_ue_golomb_long(gb);

        h->present = 1;
    }

    return 0;
}

int ff_h2645_sei_message_decode(H2645SEI *h, enum SEIType type,
                                enum AVCodecID codec_id, GetBitContext *gb,
                                GetByteContext *gbyte, void *logctx)
{
    switch (type) {
    case SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35:
        return decode_registered_user_data(h, gbyte, codec_id, logctx);
    case SEI_TYPE_USER_DATA_UNREGISTERED:
        return decode_unregistered_user_data(&h->unregistered, gbyte, codec_id);
    case SEI_TYPE_DISPLAY_ORIENTATION:
        return decode_display_orientation(&h->display_orientation, gb);
    case SEI_TYPE_FILM_GRAIN_CHARACTERISTICS:
        return decode_film_grain_characteristics(&h->film_grain_characteristics, codec_id, gb);
    case SEI_TYPE_FRAME_PACKING_ARRANGEMENT:
        return decode_frame_packing_arrangement(&h->frame_packing, gb, codec_id);
    case SEI_TYPE_ALTERNATIVE_TRANSFER_CHARACTERISTICS:
        return decode_alternative_transfer(&h->alternative_transfer, gbyte);
    default:
        return FF_H2645_SEI_MESSAGE_UNHANDLED;
    }
}

int ff_h2645_sei_ctx_replace(H2645SEI *dst, const H2645SEI *src)
{
    int ret = av_buffer_replace(&dst->a53_caption.buf_ref,
                                 src->a53_caption.buf_ref);
    if (ret < 0)
        return ret;

    for (unsigned i = 0; i < dst->unregistered.nb_buf_ref; i++)
        av_buffer_unref(&dst->unregistered.buf_ref[i]);
    dst->unregistered.nb_buf_ref = 0;

    if (src->unregistered.nb_buf_ref) {
        ret = av_reallocp_array(&dst->unregistered.buf_ref,
                                src->unregistered.nb_buf_ref,
                                sizeof(*dst->unregistered.buf_ref));
        if (ret < 0)
            return ret;

        for (unsigned i = 0; i < src->unregistered.nb_buf_ref; i++) {
            dst->unregistered.buf_ref[i] = av_buffer_ref(src->unregistered.buf_ref[i]);
            if (!dst->unregistered.buf_ref[i])
                return AVERROR(ENOMEM);
            dst->unregistered.nb_buf_ref++;
        }
    }

    return 0;
}

void ff_h2645_sei_reset(H2645SEI *s)
{
    av_buffer_unref(&s->a53_caption.buf_ref);

    for (unsigned i = 0; i < s->unregistered.nb_buf_ref; i++)
        av_buffer_unref(&s->unregistered.buf_ref[i]);
    s->unregistered.nb_buf_ref = 0;
    av_freep(&s->unregistered.buf_ref);
    av_buffer_unref(&s->dynamic_hdr_plus.info);
    av_buffer_unref(&s->dynamic_hdr_vivid.info);
}
