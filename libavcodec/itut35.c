/*
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
#include "libavutil/frame.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "atsc_a53.h"
#include "bytestream.h"
#include "decode.h"
#include "dynamic_hdr_vivid.h"
#include "dovi_rpu.h"
#include "itut35.h"
#include "version.h"

int ff_itut_t35_parse_buffer(FFITUTT35 *const itut_t35, const uint8_t *buf,
                             size_t buf_size, int flags) {
    GetByteContext gb;
    int provider_code, country_code;
    unsigned int provider_oriented_code = 0;

    bytestream2_init(&gb, buf, buf_size);

    if (flags & FF_ITUT_T35_FLAG_COUNTRY_CODE)
        country_code = itut_t35->country_code;
    else {
        country_code = bytestream2_get_byte(&gb);
        if (country_code == 0xFF) {
            if (bytestream2_get_bytes_left(&gb) < 1)
                return AVERROR_INVALIDDATA;

            bytestream2_skipu(&gb, 1); // itu_t_t35_country_code_extension_byte
        }
    }

    switch (country_code) {
    case ITU_T_T35_COUNTRY_CODE_US:
        if (bytestream2_get_bytes_left(&gb) < 2)
            return AVERROR_INVALIDDATA;
        provider_code = bytestream2_get_be16u(&gb);

        switch (provider_code) {
        case ITU_T_T35_PROVIDER_CODE_ATSC:
            if (bytestream2_get_bytes_left(&gb) < 4)
                return AVERROR_INVALIDDATA;
            provider_oriented_code = bytestream2_get_be32u(&gb);
            switch (provider_oriented_code) {
            case MKBETAG('D', 'T', 'G', '1'): // afd_data
                if (bytestream2_get_bytes_left(&gb) < 2)
                    return AVERROR_INVALIDDATA;
                if (!(bytestream2_get_byteu(&gb) & 0x40)) // active_format_flag
                    return 0; //ignore
                break;
            case MKBETAG('G', 'A', '9', '4'): // closed captions
                break;
            default: // ignore unsupported identifiers
                return 0;
            }
            break;
        case ITU_T_T35_PROVIDER_CODE_AOM:
            if (bytestream2_get_bytes_left(&gb) < 1)
                return AVERROR_INVALIDDATA;

            provider_oriented_code = bytestream2_get_byteu(&gb);
            if (provider_oriented_code != 0x0001)
                return 0; // ignore
            break;
        case ITU_T_T35_PROVIDER_CODE_SAMSUNG:
            if (bytestream2_get_bytes_left(&gb) < 3)
                return AVERROR_INVALIDDATA;
            provider_oriented_code = bytestream2_get_be16u(&gb);
            int application_identifier = bytestream2_get_byteu(&gb);

            if (provider_oriented_code != 1 || application_identifier != 4)
                return 0; // ignore
            break;
        case ITU_T_T35_PROVIDER_CODE_DOLBY:
            if (bytestream2_get_bytes_left(&gb) < 4)
                return AVERROR_INVALIDDATA;
            provider_oriented_code = bytestream2_get_be32u(&gb);
            if (provider_oriented_code != 0x800)
                return 0; // ignore
            break;
        case ITU_T_T35_PROVIDER_CODE_SMPTE:
            if (bytestream2_get_bytes_left(&gb) < 2)
                return AVERROR_INVALIDDATA;
            provider_oriented_code = bytestream2_get_be16u(&gb);
            if (provider_oriented_code != 1)
                return 0; // ignore
            break;
        default: // ignore unsupported provider codes
            return 0;
        }
        break;
    case ITU_T_T35_COUNTRY_CODE_UK:
        if (bytestream2_get_bytes_left(&gb) < 3)
            return AVERROR_INVALIDDATA;

        bytestream2_skipu(&gb, 1); // t35_uk_country_code_second_octet
        provider_code = bytestream2_get_be16u(&gb);

        switch (provider_code) {
        case ITU_T_T35_PROVIDER_CODE_VNOVA:
            break;
        default: // ignore unsupported provider codes
            return 0;
        }
        break;
    case ITU_T_T35_COUNTRY_CODE_CN:
        if (bytestream2_get_bytes_left(&gb) < 2)
            return AVERROR_INVALIDDATA;
        provider_code = bytestream2_get_be16u(&gb);

        switch (provider_code) {
        case ITU_T_T35_PROVIDER_CODE_HDR_VIVID:
            if (bytestream2_get_bytes_left(&gb) < 2)
                return AVERROR_INVALIDDATA;

            provider_oriented_code = bytestream2_get_be16u(&gb);
            if (provider_oriented_code != 0x0005)
                return 0; //ignore
            break;
        default: // ignore unsupported provider codes
            return 0;
        }
        break;

    default: // ignore unsupported country codes
        return 0;
    }

    if (!bytestream2_get_bytes_left(&gb))
        return AVERROR_INVALIDDATA;

    itut_t35->payload = gb.buffer;
    itut_t35->payload_size = bytestream2_get_bytes_left(&gb);

    itut_t35->country_code  = country_code;
    itut_t35->provider_code = provider_code;
    itut_t35->provider_oriented_code = provider_oriented_code;

    return 1;
}

int ff_itut_t35_parse_payload_to_struct(FFITUTT35 *const itut_t35, FFITUTT35Aux *const aux,
                                        FFITUTT35Meta *metadata, int err_recognition)
{
    int ret;

    switch (itut_t35->country_code) {
    case ITU_T_T35_COUNTRY_CODE_US:
        switch (itut_t35->provider_code) {
        case ITU_T_T35_PROVIDER_CODE_AOM:
            ff_aom_uninit_film_grain_params(&metadata->aom_film_grain);
            ret = ff_aom_parse_film_grain_sets(&metadata->aom_film_grain,
                                               itut_t35->payload, itut_t35->payload_size);
            if (ret < 0)
                return ret;
            break;
        case ITU_T_T35_PROVIDER_CODE_ATSC:
            switch (itut_t35->provider_oriented_code) {
            case MKBETAG('D', 'T', 'G', '1'): // afd_data
                av_buffer_unref(&metadata->afd);
                metadata->afd = av_buffer_alloc(1);
                if (!metadata->afd)
                    return AVERROR(ENOMEM);

                *metadata->afd->data = *itut_t35->payload & 0xf;

                break;
            case MKBETAG('G', 'A', '9', '4'): // closed captions
                ret = ff_parse_a53_cc(&metadata->a53_cc, itut_t35->payload, itut_t35->payload_size);
                if (ret < 0)
                    return ret;

                break;
            default: // ignore unsupported identifiers
                break;
            }
            break;
        case ITU_T_T35_PROVIDER_CODE_SAMSUNG: {
            size_t size;
            AVDynamicHDRPlus *hdr_plus = av_dynamic_hdr_plus_alloc(&size);
            if (!hdr_plus)
                return AVERROR(ENOMEM);

            ret = av_dynamic_hdr_plus_from_t35(hdr_plus, itut_t35->payload,
                                               itut_t35->payload_size);
            if (ret < 0) {
                av_free(hdr_plus);
                return ret;
            }

            av_buffer_unref(&metadata->hdr_plus);
            metadata->hdr_plus = av_buffer_create((uint8_t *)hdr_plus, size, NULL, NULL, 0);
            if (!metadata->hdr_plus) {
                av_free(hdr_plus);
                return AVERROR(ENOMEM);
            }

            break;
        }
        case ITU_T_T35_PROVIDER_CODE_DOLBY: {
            AVDOVIMetadata *dovi;

            if (!aux || !aux->dovi)
                return 0; // ignore

            ret = ff_dovi_rpu_parse(aux->dovi, itut_t35->payload, itut_t35->payload_size,
                                    err_recognition);
            if (ret < 0)
                return 0; // ignore

            ret = ff_dovi_get_metadata(aux->dovi, &dovi);
            if (ret <= 0)
                return ret;

            av_buffer_unref(&metadata->dovi);
            metadata->dovi = av_buffer_create((uint8_t *)dovi, ret, NULL, NULL, 0);
            if (!metadata->dovi) {
                av_free(dovi);
                return AVERROR(ENOMEM);
            }

            break;
        }
        case ITU_T_T35_PROVIDER_CODE_SMPTE: {
            size_t size;
            AVDynamicHDRSmpte2094App5 *hdr_smpte2094_app5 = av_dynamic_hdr_smpte2094_app5_alloc(&size);
            if (!hdr_smpte2094_app5)
                return AVERROR(ENOMEM);

            ret = av_dynamic_hdr_smpte2094_app5_from_t35(hdr_smpte2094_app5, itut_t35->payload,
                                                         itut_t35->payload_size);
            if (ret < 0) {
                av_free(hdr_smpte2094_app5);
                return ret;
            }

            av_buffer_unref(&metadata->hdr_smpte2094_app5);
            metadata->hdr_smpte2094_app5 = av_buffer_create((uint8_t *)hdr_smpte2094_app5, size, NULL, NULL, 0);
            if (!metadata->hdr_smpte2094_app5) {
                av_free(hdr_smpte2094_app5);
                return AVERROR(ENOMEM);
            }

            break;
        }
        default:
            break;
        }
        break;
    case ITU_T_T35_COUNTRY_CODE_UK:
        switch (itut_t35->provider_code) {
        case ITU_T_T35_PROVIDER_CODE_VNOVA:
            av_buffer_unref(&metadata->lcevc);
            metadata->lcevc = av_buffer_alloc(itut_t35->payload_size);
            if (!metadata->lcevc)
                return AVERROR(ENOMEM);

            memcpy(metadata->lcevc->data, itut_t35->payload, itut_t35->payload_size);

            break;
        default:
            break;
        }
        break;
    case ITU_T_T35_COUNTRY_CODE_CN:
        switch (itut_t35->provider_code) {
        case ITU_T_T35_PROVIDER_CODE_HDR_VIVID: {
            size_t size;
            AVDynamicHDRVivid *hdr_vivid = av_dynamic_hdr_vivid_alloc(&size);
            if (!hdr_vivid)
                return AVERROR(ENOMEM);

            ret = ff_parse_itu_t_t35_to_dynamic_hdr_vivid(hdr_vivid, itut_t35->payload,
                                                          itut_t35->payload_size);
            if (ret < 0) {
                av_free(hdr_vivid);
                return ret;
            }

            av_buffer_unref(&metadata->hdr_vivid);
            metadata->hdr_vivid = av_buffer_create((uint8_t *)hdr_vivid, size, NULL, NULL, 0);
            if (!metadata->hdr_vivid) {
                av_free(hdr_vivid);
                return AVERROR(ENOMEM);
            }
            break;
        }
        default:
            break;
        }
        break;

    default:
        // ignore unsupported provider codes
        break;
    }

    return 0;
}

int ff_itut_t35_parse_payload_to_frame(FFITUTT35 *const itut_t35, FFITUTT35Aux *const aux,
                                       AVCodecContext *const avctx, AVFrame *const frame)
{
    FFITUTT35Meta metadata = { 0 };
    int ret;

    ret = ff_itut_t35_parse_payload_to_struct(itut_t35, aux, &metadata, avctx->err_recognition);
    if (ret < 0)
        return ret;

    if (metadata.afd) {
        if (!av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_AFD, metadata.afd))
            av_buffer_unref(&metadata.afd);
        metadata.afd = NULL;
    }

    if (metadata.a53_cc) {
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_A53_CC, &metadata.a53_cc);
        if (ret < 0)
            return ret;

#if FF_API_CODEC_PROPS
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->properties |= FF_CODEC_PROPERTY_CLOSED_CAPTIONS;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    if (metadata.aom_film_grain.enable) {
        ret = ff_aom_attach_film_grain_sets(&metadata.aom_film_grain, frame);
        if (ret < 0)
            return ret;
    }

    if (metadata.hdr_plus) {
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS,
                                              &metadata.hdr_plus);
        if (ret < 0)
            return ret;
    }

    if (metadata.dovi) {
        AVFrameSideData *sd = av_frame_new_side_data_from_buf(frame, AV_FRAME_DATA_DOVI_METADATA,
                                                              metadata.dovi);
        if (!sd) {
            av_buffer_unref(&metadata.dovi);
            return AVERROR(ENOMEM);
        }
        metadata.dovi = NULL;
    }

    if (metadata.hdr_smpte2094_app5) {
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_DYNAMIC_HDR_SMPTE_2094_APP5,
                                              &metadata.hdr_smpte2094_app5);
        if (ret < 0)
            return ret;
    }

    if (metadata.lcevc) {
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_LCEVC,
                                             &metadata.lcevc);
        if (ret < 0)
            return ret;
    }

    if (metadata.hdr_vivid) {
        ret = ff_frame_new_side_data_from_buf(avctx, frame, AV_FRAME_DATA_DYNAMIC_HDR_VIVID,
                                              &metadata.hdr_vivid);
        if (ret < 0)
            return ret;
    }

    ff_itut_t35_unref(&metadata);

    return 0;
}

void ff_itut_t35_unref(FFITUTT35Meta *metadata)
{
    ff_aom_uninit_film_grain_params(&metadata->aom_film_grain);
    av_buffer_unref(&metadata->afd);
    av_buffer_unref(&metadata->a53_cc);
    av_buffer_unref(&metadata->hdr_plus);
    av_buffer_unref(&metadata->hdr_smpte2094_app5);
    av_buffer_unref(&metadata->lcevc);
    av_buffer_unref(&metadata->dovi);
    av_buffer_unref(&metadata->hdr_vivid);
}
