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

#include "libavutil/ambient_viewing_environment.h"
#include "libavutil/display.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/film_grain_params.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/stereo3d.h"

#include "atsc_a53.h"
#include "avcodec.h"
#include "decode.h"
#include "dynamic_hdr_vivid.h"
#include "get_bits.h"
#include "golomb.h"
#include "h2645_sei.h"
#include "itut35.h"

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

    err = av_dynamic_hdr_plus_from_t35(metadata, gb->buffer,
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

static int decode_registered_user_data_lcevc(HEVCSEILCEVC *s,
                                             GetByteContext *gb)
{
    int size = bytestream2_get_bytes_left(gb);

    av_buffer_unref(&s->info);
    s->info = av_buffer_alloc(size);
    if (!s->info)
        return AVERROR(ENOMEM);

    bytestream2_get_bufferu(gb, s->info->data, size);
    return 0;
}

static int decode_registered_user_data_afd(H2645SEIAFD *h, GetByteContext *gb)
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

    if (country_code != ITU_T_T35_COUNTRY_CODE_US &&
        country_code != ITU_T_T35_COUNTRY_CODE_UK &&
        country_code != ITU_T_T35_COUNTRY_CODE_CN) {
        av_log(logctx, AV_LOG_VERBOSE,
               "Unsupported User Data Registered ITU-T T35 SEI message (country_code = %d)\n",
               country_code);
        return 0;
    }

    /* itu_t_t35_payload_byte follows */
    provider_code = bytestream2_get_be16u(gb);

    switch (provider_code) {
    case ITU_T_T35_PROVIDER_CODE_ATSC: {
        uint32_t user_identifier;

        if (bytestream2_get_bytes_left(gb) < 4)
            return AVERROR_INVALIDDATA;

        user_identifier = bytestream2_get_be32u(gb);
        switch (user_identifier) {
        case MKBETAG('D', 'T', 'G', '1'):       // afd_data
            return decode_registered_user_data_afd(&h->afd, gb);
        case MKBETAG('G', 'A', '9', '4'):       // closed captions
            return decode_registered_user_data_closed_caption(&h->a53_caption, gb);
        default:
            av_log(logctx, AV_LOG_VERBOSE,
                   "Unsupported User Data Registered ITU-T T35 SEI message (atsc user_identifier = 0x%04x)\n",
                   user_identifier);
            break;
        }
        break;
    }
    case ITU_T_T35_PROVIDER_CODE_LCEVC: {
        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;

        bytestream2_skipu(gb, 1); // user_data_type_code
        return decode_registered_user_data_lcevc(&h->lcevc, gb);
    }
#if CONFIG_HEVC_SEI
    case ITU_T_T35_PROVIDER_CODE_CUVA: {
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
    case ITU_T_T35_PROVIDER_CODE_SMTPE: {
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
    case 0x5890: { // aom_provider_code
        const uint16_t aom_grain_provider_oriented_code = 0x0001;
        uint16_t provider_oriented_code;

        if (!IS_HEVC(codec_id))
            goto unsupported_provider_code;

        if (bytestream2_get_bytes_left(gb) < 2)
            return AVERROR_INVALIDDATA;

        provider_oriented_code = bytestream2_get_byteu(gb);
        if (provider_oriented_code == aom_grain_provider_oriented_code) {
            return ff_aom_parse_film_grain_sets(&h->aom_film_grain,
                                                gb->buffer,
                                                bytestream2_get_bytes_left(gb));
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

static int decode_ambient_viewing_environment(H2645SEIAmbientViewingEnvironment *s,
                                              GetByteContext *gb)
{
    static const uint16_t max_ambient_light_value = 50000;

    if (bytestream2_get_bytes_left(gb) < 8)
        return AVERROR_INVALIDDATA;

    s->ambient_illuminance = bytestream2_get_be32u(gb);
    if (!s->ambient_illuminance)
        return AVERROR_INVALIDDATA;

    s->ambient_light_x = bytestream2_get_be16u(gb);
    if (s->ambient_light_x > max_ambient_light_value)
        return AVERROR_INVALIDDATA;

    s->ambient_light_y = bytestream2_get_be16u(gb);
    if (s->ambient_light_y > max_ambient_light_value)
        return AVERROR_INVALIDDATA;

    s->present = 1;

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

static int decode_nal_sei_mastering_display_info(H2645SEIMasteringDisplay *s,
                                                 GetByteContext *gb)
{
    int i;

    if (bytestream2_get_bytes_left(gb) < 24)
        return AVERROR_INVALIDDATA;

    // Mastering primaries
    for (i = 0; i < 3; i++) {
        s->display_primaries[i][0] = bytestream2_get_be16u(gb);
        s->display_primaries[i][1] = bytestream2_get_be16u(gb);
    }
    // White point (x, y)
    s->white_point[0] = bytestream2_get_be16u(gb);
    s->white_point[1] = bytestream2_get_be16u(gb);

    // Max and min luminance of mastering display
    s->max_luminance = bytestream2_get_be32u(gb);
    s->min_luminance = bytestream2_get_be32u(gb);

    // As this SEI message comes before the first frame that references it,
    // initialize the flag to 2 and decrement on IRAP access unit so it
    // persists for the coded video sequence (e.g., between two IRAPs)
    s->present = 2;

    return 0;
}

static int decode_nal_sei_content_light_info(H2645SEIContentLight *s,
                                             GetByteContext *gb)
{
    if (bytestream2_get_bytes_left(gb) < 4)
        return AVERROR_INVALIDDATA;

    // Max and average light levels
    s->max_content_light_level     = bytestream2_get_be16u(gb);
    s->max_pic_average_light_level = bytestream2_get_be16u(gb);
    // As this SEI message comes before the first frame that references it,
    // initialize the flag to 2 and decrement on IRAP access unit so it
    // persists for the coded video sequence (e.g., between two IRAPs)
    s->present = 2;

    return  0;
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
    case SEI_TYPE_AMBIENT_VIEWING_ENVIRONMENT:
        return decode_ambient_viewing_environment(&h->ambient_viewing_environment,
                                                  gbyte);
    case SEI_TYPE_MASTERING_DISPLAY_COLOUR_VOLUME:
        return decode_nal_sei_mastering_display_info(&h->mastering_display,
                                                     gbyte);
    case SEI_TYPE_CONTENT_LIGHT_LEVEL_INFO:
        return decode_nal_sei_content_light_info(&h->content_light, gbyte);
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

    ret = av_buffer_replace(&dst->lcevc.info, src->lcevc.info);
    if (ret < 0)
        return ret;

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

static int is_frame_packing_type_valid(SEIFpaType type, enum AVCodecID codec_id)
{
    if (IS_H264(codec_id))
        return type <= SEI_FPA_H264_TYPE_2D &&
               type >= SEI_FPA_H264_TYPE_CHECKERBOARD;
    else
        return type <= SEI_FPA_TYPE_INTERLEAVE_TEMPORAL &&
               type >= SEI_FPA_TYPE_SIDE_BY_SIDE;
}

static int h2645_sei_to_side_data(AVCodecContext *avctx, H2645SEI *sei,
                                  AVFrameSideData ***sd, int *nb_sd)
{
    int ret;

    for (unsigned i = 0; i < sei->unregistered.nb_buf_ref; i++) {
        H2645SEIUnregistered *unreg = &sei->unregistered;

        if (unreg->buf_ref[i]) {
            AVFrameSideData *entry =
                av_frame_side_data_add(sd, nb_sd, AV_FRAME_DATA_SEI_UNREGISTERED,
                                       &unreg->buf_ref[i], 0);
            if (!entry)
                av_buffer_unref(&unreg->buf_ref[i]);
        }
    }
    sei->unregistered.nb_buf_ref = 0;

    if (sei->ambient_viewing_environment.present) {
        H2645SEIAmbientViewingEnvironment *env = &sei->ambient_viewing_environment;
        AVBufferRef *buf;
        size_t size;

        AVAmbientViewingEnvironment *dst_env =
            av_ambient_viewing_environment_alloc(&size);
        if (!dst_env)
            return AVERROR(ENOMEM);

        buf = av_buffer_create((uint8_t *)dst_env, size, NULL, NULL, 0);
        if (!buf) {
            av_free(dst_env);
            return AVERROR(ENOMEM);
        }

        ret = ff_frame_new_side_data_from_buf_ext(avctx, sd, nb_sd,
                                                  AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT, &buf);

        if (ret < 0)
            return ret;

        dst_env->ambient_illuminance = av_make_q(env->ambient_illuminance, 10000);
        dst_env->ambient_light_x     = av_make_q(env->ambient_light_x,     50000);
        dst_env->ambient_light_y     = av_make_q(env->ambient_light_y,     50000);
    }

    if (sei->mastering_display.present) {
        // HEVC uses a g,b,r ordering, which we convert to a more natural r,g,b
        const int mapping[3] = {2, 0, 1};
        const int chroma_den = 50000;
        const int luma_den = 10000;
        int i;
        AVMasteringDisplayMetadata *metadata;

        ret = ff_decode_mastering_display_new_ext(avctx, sd, nb_sd, &metadata);
        if (ret < 0)
            return ret;

        if (metadata) {
            metadata->has_luminance = 1;
            metadata->has_primaries = 1;

            for (i = 0; i < 3; i++) {
                const int j = mapping[i];
                metadata->display_primaries[i][0].num = sei->mastering_display.display_primaries[j][0];
                metadata->display_primaries[i][0].den = chroma_den;
                metadata->has_primaries &= sei->mastering_display.display_primaries[j][0] >= 5 &&
                                           sei->mastering_display.display_primaries[j][0] <= 37000;

                metadata->display_primaries[i][1].num = sei->mastering_display.display_primaries[j][1];
                metadata->display_primaries[i][1].den = chroma_den;
                metadata->has_primaries &= sei->mastering_display.display_primaries[j][1] >= 5 &&
                                           sei->mastering_display.display_primaries[j][1] <= 42000;
            }
            metadata->white_point[0].num = sei->mastering_display.white_point[0];
            metadata->white_point[0].den = chroma_den;
            metadata->has_primaries &= sei->mastering_display.white_point[0] >= 5 &&
                                       sei->mastering_display.white_point[0] <= 37000;

            metadata->white_point[1].num = sei->mastering_display.white_point[1];
            metadata->white_point[1].den = chroma_den;
            metadata->has_primaries &= sei->mastering_display.white_point[1] >= 5 &&
                                       sei->mastering_display.white_point[1] <= 42000;

            metadata->max_luminance.num = sei->mastering_display.max_luminance;
            metadata->max_luminance.den = luma_den;
            metadata->has_luminance &= sei->mastering_display.max_luminance >= 50000 &&
                                       sei->mastering_display.max_luminance <= 100000000;

            metadata->min_luminance.num = sei->mastering_display.min_luminance;
            metadata->min_luminance.den = luma_den;
            metadata->has_luminance &= sei->mastering_display.min_luminance <= 50000 &&
                                       sei->mastering_display.min_luminance <
                                       sei->mastering_display.max_luminance;

            /* Real (blu-ray) releases in the wild come with minimum luminance
             * values of 0.000 cd/m2, so permit this edge case */
            if (avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT)
                metadata->has_luminance &= sei->mastering_display.min_luminance >= 1;

            if (metadata->has_luminance || metadata->has_primaries)
                av_log(avctx, AV_LOG_DEBUG, "Mastering Display Metadata:\n");
            if (metadata->has_primaries) {
                av_log(avctx, AV_LOG_DEBUG,
                       "r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f)\n",
                       av_q2d(metadata->display_primaries[0][0]),
                       av_q2d(metadata->display_primaries[0][1]),
                       av_q2d(metadata->display_primaries[1][0]),
                       av_q2d(metadata->display_primaries[1][1]),
                       av_q2d(metadata->display_primaries[2][0]),
                       av_q2d(metadata->display_primaries[2][1]),
                       av_q2d(metadata->white_point[0]), av_q2d(metadata->white_point[1]));
            }
            if (metadata->has_luminance) {
                av_log(avctx, AV_LOG_DEBUG,
                       "min_luminance=%f, max_luminance=%f\n",
                       av_q2d(metadata->min_luminance), av_q2d(metadata->max_luminance));
            }
        }
    }

    if (sei->content_light.present) {
        AVContentLightMetadata *metadata;

        ret = ff_decode_content_light_new_ext(avctx, sd, nb_sd, &metadata);
        if (ret < 0)
            return ret;

        if (metadata) {
            metadata->MaxCLL  = sei->content_light.max_content_light_level;
            metadata->MaxFALL = sei->content_light.max_pic_average_light_level;

            av_log(avctx, AV_LOG_DEBUG, "Content Light Level Metadata:\n");
            av_log(avctx, AV_LOG_DEBUG, "MaxCLL=%d, MaxFALL=%d\n",
                   metadata->MaxCLL, metadata->MaxFALL);
        }
    }

    return 0;
}

int ff_h2645_sei_to_frame(AVFrame *frame, H2645SEI *sei,
                          enum AVCodecID codec_id,
                          AVCodecContext *avctx, const H2645VUI *vui,
                          unsigned bit_depth_luma, unsigned bit_depth_chroma,
                          int seed)
{
    H2645SEIFramePacking *fp = &sei->frame_packing;
    int ret;

    if (fp->present &&
        is_frame_packing_type_valid(fp->arrangement_type, codec_id) &&
        fp->content_interpretation_type > 0 &&
        fp->content_interpretation_type < 3) {
        AVStereo3D *stereo = av_stereo3d_create_side_data(frame);

        if (!stereo)
            return AVERROR(ENOMEM);

        switch (fp->arrangement_type) {
#if CONFIG_H264_SEI
        case SEI_FPA_H264_TYPE_CHECKERBOARD:
            stereo->type = AV_STEREO3D_CHECKERBOARD;
            break;
        case SEI_FPA_H264_TYPE_INTERLEAVE_COLUMN:
            stereo->type = AV_STEREO3D_COLUMNS;
            break;
        case SEI_FPA_H264_TYPE_INTERLEAVE_ROW:
            stereo->type = AV_STEREO3D_LINES;
            break;
#endif
        case SEI_FPA_TYPE_SIDE_BY_SIDE:
            if (fp->quincunx_sampling_flag)
                stereo->type = AV_STEREO3D_SIDEBYSIDE_QUINCUNX;
            else
                stereo->type = AV_STEREO3D_SIDEBYSIDE;
            break;
        case SEI_FPA_TYPE_TOP_BOTTOM:
            stereo->type = AV_STEREO3D_TOPBOTTOM;
            break;
        case SEI_FPA_TYPE_INTERLEAVE_TEMPORAL:
            stereo->type = AV_STEREO3D_FRAMESEQUENCE;
            break;
#if CONFIG_H264_SEI
        case SEI_FPA_H264_TYPE_2D:
            stereo->type = AV_STEREO3D_2D;
            break;
#endif
        }

        if (fp->content_interpretation_type == 2)
            stereo->flags = AV_STEREO3D_FLAG_INVERT;

        if (fp->arrangement_type == SEI_FPA_TYPE_INTERLEAVE_TEMPORAL) {
            if (fp->current_frame_is_frame0_flag)
                stereo->view = AV_STEREO3D_VIEW_LEFT;
            else
                stereo->view = AV_STEREO3D_VIEW_RIGHT;
        }
    }

    if (sei->display_orientation.present &&
        (sei->display_orientation.anticlockwise_rotation ||
         sei->display_orientation.hflip ||
         sei->display_orientation.vflip)) {
        H2645SEIDisplayOrientation *o = &sei->display_orientation;
        double angle = o->anticlockwise_rotation * 360 / (double) (1 << 16);
        AVFrameSideData *rotation = av_frame_new_side_data(frame,
                                                           AV_FRAME_DATA_DISPLAYMATRIX,
                                                           sizeof(int32_t) * 9);
        if (!rotation)
            return AVERROR(ENOMEM);

        /* av_display_rotation_set() expects the angle in the clockwise
         * direction, hence the first minus.
         * The below code applies the flips after the rotation, yet
         * the H.2645 specs require flipping to be applied first.
         * Because of R O(phi) = O(-phi) R (where R is flipping around
         * an arbitatry axis and O(phi) is the proper rotation by phi)
         * we can create display matrices as desired by negating
         * the degree once for every flip applied. */
        angle = -angle * (1 - 2 * !!o->hflip) * (1 - 2 * !!o->vflip);
        av_display_rotation_set((int32_t *)rotation->data, angle);
        av_display_matrix_flip((int32_t *)rotation->data,
                                o->hflip, o->vflip);
    }

    if (sei->a53_caption.buf_ref) {
        H2645SEIA53Caption *a53 = &sei->a53_caption;
        AVFrameSideData *sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_A53_CC, a53->buf_ref);
        if (!sd)
            av_buffer_unref(&a53->buf_ref);
        a53->buf_ref = NULL;
        avctx->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
    }

    ret = h2645_sei_to_side_data(avctx, sei, &frame->side_data, &frame->nb_side_data);
    if (ret < 0)
        return ret;

    if (sei->afd.present) {
        AVFrameSideData *sd = av_frame_new_side_data(frame, AV_FRAME_DATA_AFD,
                                                     sizeof(uint8_t));

        if (sd) {
            *sd->data = sei->afd.active_format_description;
            sei->afd.present = 0;
        }
    }

    if (sei->lcevc.info) {
        HEVCSEILCEVC *lcevc = &sei->lcevc;
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_LCEVC, &lcevc->info);
        if (ret < 0)
            return ret;
    }

    if (sei->film_grain_characteristics.present) {
        H2645SEIFilmGrainCharacteristics *fgc = &sei->film_grain_characteristics;
        AVFilmGrainParams *fgp = av_film_grain_params_create_side_data(frame);
        AVFilmGrainH274Params *h274;

        if (!fgp)
            return AVERROR(ENOMEM);

        fgp->type = AV_FILM_GRAIN_PARAMS_H274;
        h274      = &fgp->codec.h274;

        fgp->seed = seed;
        fgp->width = frame->width;
        fgp->height = frame->height;

        /* H.274 mandates film grain be applied to 4:4:4 frames */
        fgp->subsampling_x = fgp->subsampling_y = 0;

        h274->model_id = fgc->model_id;
        if (fgc->separate_colour_description_present_flag) {
            fgp->bit_depth_luma   = fgc->bit_depth_luma;
            fgp->bit_depth_chroma = fgc->bit_depth_chroma;
            fgp->color_range      = fgc->full_range + 1;
            fgp->color_primaries  = fgc->color_primaries;
            fgp->color_trc        = fgc->transfer_characteristics;
            fgp->color_space      = fgc->matrix_coeffs;
        } else {
            fgp->bit_depth_luma   = bit_depth_luma;
            fgp->bit_depth_chroma = bit_depth_chroma;
            if (vui->video_signal_type_present_flag)
                fgp->color_range = vui->video_full_range_flag + 1;
            if (vui->colour_description_present_flag) {
                fgp->color_primaries = vui->colour_primaries;
                fgp->color_trc       = vui->transfer_characteristics;
                fgp->color_space     = vui->matrix_coeffs;
            }
        }
        h274->blending_mode_id  = fgc->blending_mode_id;
        h274->log2_scale_factor = fgc->log2_scale_factor;

#if FF_API_H274_FILM_GRAIN_VCS
FF_DISABLE_DEPRECATION_WARNINGS
        h274->bit_depth_luma   = fgp->bit_depth_luma;
        h274->bit_depth_chroma = fgp->bit_depth_chroma;
        h274->color_range      = fgp->color_range;
        h274->color_primaries  = fgp->color_primaries;
        h274->color_trc        = fgp->color_trc;
        h274->color_space      = fgp->color_space;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        memcpy(&h274->component_model_present, &fgc->comp_model_present_flag,
               sizeof(h274->component_model_present));
        memcpy(&h274->num_intensity_intervals, &fgc->num_intensity_intervals,
               sizeof(h274->num_intensity_intervals));
        memcpy(&h274->num_model_values, &fgc->num_model_values,
               sizeof(h274->num_model_values));
        memcpy(&h274->intensity_interval_lower_bound, &fgc->intensity_interval_lower_bound,
               sizeof(h274->intensity_interval_lower_bound));
        memcpy(&h274->intensity_interval_upper_bound, &fgc->intensity_interval_upper_bound,
               sizeof(h274->intensity_interval_upper_bound));
        memcpy(&h274->comp_model_value, &fgc->comp_model_value,
               sizeof(h274->comp_model_value));

        if (IS_H264(codec_id))
            fgc->present = !!fgc->repetition_period;
        else
            fgc->present = fgc->persistence_flag;

        avctx->properties |= FF_CODEC_PROPERTY_FILM_GRAIN;
    }

#if CONFIG_HEVC_SEI
    ret = ff_aom_attach_film_grain_sets(&sei->aom_film_grain, frame);
    if (ret < 0)
        return ret;
#endif

    return 0;
}

int ff_h2645_sei_to_context(AVCodecContext *avctx, H2645SEI *sei)
{
    return h2645_sei_to_side_data(avctx, sei, &avctx->decoded_side_data,
                                  &avctx->nb_decoded_side_data);
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
    av_buffer_unref(&s->lcevc.info);

    s->ambient_viewing_environment.present = 0;
    s->mastering_display.present = 0;
    s->content_light.present = 0;
    s->aom_film_grain.enable = 0;
}
