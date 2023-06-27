/*
 * EVC helper functions for muxers
 * Copyright (c) 2022 Dawid Kozinski <d.kozinski@samsung.com>
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

#include "libavutil/intreadwrite.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/golomb.h"
#include "libavcodec/evc.h"
#include "avformat.h"
#include "avio.h"
#include "evc.h"
#include "avio_internal.h"

// @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.1
enum {
    SPS_INDEX,
    PPS_INDEX,
    APS_INDEX,
    SEI_INDEX,
    NB_ARRAYS
};

// @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
typedef struct EVCNALUnitArray {
    uint8_t  array_completeness; // when equal to 1 indicates that all NAL units of the given type are in the following array
    uint8_t  NAL_unit_type;      // indicates the type of the NAL units in the following array
    uint16_t numNalus;           // indicates the number of NAL units of the indicated type
    uint16_t *nalUnitLength;     // indicates the length in bytes of the NAL unit
    uint8_t  **nalUnit;          // contains an SPS, PPS, APS or a SEI NAL unit, as specified in ISO/IEC 23094-1
} EVCNALUnitArray;

/**
 * @brief Specifies the decoder configuration information for ISO/IEC 23094-1 video content.
 * @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.2
 *      Carriage of network abstraction layer (NAL) unit structured video in the ISO base media file format
 */
typedef struct EVCDecoderConfigurationRecord {
    uint8_t  configurationVersion;          // 8 bits
    uint8_t  profile_idc;                   // 8 bits
    uint8_t  level_idc;                     // 8 bits
    uint32_t toolset_idc_h;                 // 32 bits
    uint32_t toolset_idc_l;                 // 32 bits
    uint8_t  chroma_format_idc;             // 2 bits
    uint8_t  bit_depth_luma_minus8;         // 3 bits
    uint8_t  bit_depth_chroma_minus8;       // 3 bits
    uint16_t pic_width_in_luma_samples;     // 16 bits
    uint16_t pic_height_in_luma_samples;    // 16 bits
    uint8_t  reserved;                      // 6 bits '000000'b
    uint8_t  lengthSizeMinusOne;            // 2 bits
    uint8_t  num_of_arrays;                 // 8 bits
    EVCNALUnitArray arrays[NB_ARRAYS];
} EVCDecoderConfigurationRecord;

typedef struct NALU {
    int offset;
    uint32_t size;
} NALU;

typedef struct NALUList {
    NALU *nalus;
    unsigned nalus_array_size;
    unsigned nb_nalus;          ///< valid entries in nalus
} NALUList;

// @see ISO_IEC_23094-1 (7.3.2.1 SPS RBSP syntax)
static int evcc_parse_sps(const uint8_t *bs, int bs_size, EVCDecoderConfigurationRecord *evcc)
{
    GetBitContext gb;
    unsigned sps_seq_parameter_set_id;
    int ret;

    bs += EVC_NALU_HEADER_SIZE;
    bs_size -= EVC_NALU_HEADER_SIZE;

    ret = init_get_bits8(&gb, bs, bs_size);
    if (ret < 0)
        return ret;

    sps_seq_parameter_set_id = get_ue_golomb_long(&gb);

    if (sps_seq_parameter_set_id >= EVC_MAX_SPS_COUNT)
        return AVERROR_INVALIDDATA;

    // the Baseline profile is indicated by profile_idc eqal to 0
    // the Main profile is indicated by profile_idc eqal to 1
    evcc->profile_idc = get_bits(&gb, 8);

    evcc->level_idc = get_bits(&gb, 8);

    evcc->toolset_idc_h = get_bits_long(&gb, 32);
    evcc->toolset_idc_l = get_bits_long(&gb, 32);

    // 0 - monochrome
    // 1 - 4:2:0
    // 2 - 4:2:2
    // 3 - 4:4:4
    evcc->chroma_format_idc = get_ue_golomb_long(&gb);
    if (evcc->chroma_format_idc > 3)
        return AVERROR_INVALIDDATA;

    evcc->pic_width_in_luma_samples = get_ue_golomb_long(&gb);
    evcc->pic_height_in_luma_samples = get_ue_golomb_long(&gb);

    evcc->bit_depth_luma_minus8 = get_ue_golomb_long(&gb);
    evcc->bit_depth_chroma_minus8 = get_ue_golomb_long(&gb);
    // EVCDecoderConfigurationRecord can't store values > 7. Limit it to bit depth 14.
    if (evcc->bit_depth_luma_minus8 > 6 || evcc->bit_depth_chroma_minus8 > 6)
        return AVERROR_INVALIDDATA;

    return 0;
}

// @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
static int evcc_array_add_nal_unit(const uint8_t *nal_buf, uint32_t nal_size,
                                   uint8_t nal_type, int ps_array_completeness,
                                   EVCNALUnitArray *array)
{
    int ret;
    uint16_t numNalus = array->numNalus;

    ret = av_reallocp_array(&array->nalUnit, numNalus + 1, sizeof(uint8_t *));
    if (ret < 0)
        return ret;

    ret = av_reallocp_array(&array->nalUnitLength, numNalus + 1, sizeof(uint16_t));
    if (ret < 0)
        return ret;

    array->nalUnit      [numNalus] = (uint8_t *)nal_buf;
    array->nalUnitLength[numNalus] = nal_size;
    array->NAL_unit_type           = nal_type;
    array->numNalus++;

    /*
     * When the sample entry name is 'evc1', the default and mandatory value of
     * array_completeness is 1 for arrays of all types of parameter sets, and 0
     * for all other arrays.
     */
    if (nal_type == EVC_SPS_NUT || nal_type == EVC_PPS_NUT || nal_type == EVC_APS_NUT)
        array->array_completeness = ps_array_completeness;

    return 0;
}

static void evcc_init(EVCDecoderConfigurationRecord *evcc)
{
    memset(evcc, 0, sizeof(EVCDecoderConfigurationRecord));
    evcc->configurationVersion = 1;
    evcc->lengthSizeMinusOne   = 3; // 4 bytes
}

static void evcc_close(EVCDecoderConfigurationRecord *evcc)
{
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(evcc->arrays); i++) {
        EVCNALUnitArray *const array = &evcc->arrays[i];
        array->numNalus = 0;
        av_freep(&array->nalUnit);
        av_freep(&array->nalUnitLength);
    }
}

static int evcc_write(AVIOContext *pb, EVCDecoderConfigurationRecord *evcc)
{
    uint16_t sps_count;

    av_log(NULL, AV_LOG_TRACE,  "configurationVersion:                %"PRIu8"\n",
           evcc->configurationVersion);
    av_log(NULL, AV_LOG_TRACE,  "profile_idc:                         %"PRIu8"\n",
           evcc->profile_idc);
    av_log(NULL, AV_LOG_TRACE,  "level_idc:                           %"PRIu8"\n",
           evcc->level_idc);
    av_log(NULL, AV_LOG_TRACE,  "toolset_idc_h:                       %"PRIu32"\n",
           evcc->toolset_idc_h);
    av_log(NULL, AV_LOG_TRACE, "toolset_idc_l:                        %"PRIu32"\n",
           evcc->toolset_idc_l);
    av_log(NULL, AV_LOG_TRACE, "chroma_format_idc:                    %"PRIu8"\n",
           evcc->chroma_format_idc);
    av_log(NULL, AV_LOG_TRACE,  "bit_depth_luma_minus8:               %"PRIu8"\n",
           evcc->bit_depth_luma_minus8);
    av_log(NULL, AV_LOG_TRACE,  "bit_depth_chroma_minus8:             %"PRIu8"\n",
           evcc->bit_depth_chroma_minus8);
    av_log(NULL, AV_LOG_TRACE,  "pic_width_in_luma_samples:           %"PRIu16"\n",
           evcc->pic_width_in_luma_samples);
    av_log(NULL, AV_LOG_TRACE,  "pic_height_in_luma_samples:          %"PRIu16"\n",
           evcc->pic_height_in_luma_samples);
    av_log(NULL, AV_LOG_TRACE,  "lengthSizeMinusOne:                  %"PRIu8"\n",
           evcc->lengthSizeMinusOne);
    av_log(NULL, AV_LOG_TRACE,  "num_of_arrays:                       %"PRIu8"\n",
           evcc->num_of_arrays);
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(evcc->arrays); i++) {
        const EVCNALUnitArray *const array = &evcc->arrays[i];

        if(array->numNalus == 0)
            continue;

        av_log(NULL, AV_LOG_TRACE, "array_completeness[%"PRIu8"]:               %"PRIu8"\n",
               i, array->array_completeness);
        av_log(NULL, AV_LOG_TRACE, "NAL_unit_type[%"PRIu8"]:                    %"PRIu8"\n",
               i, array->NAL_unit_type);
        av_log(NULL, AV_LOG_TRACE, "numNalus[%"PRIu8"]:                         %"PRIu16"\n",
               i, array->numNalus);
        for ( unsigned j = 0; j < array->numNalus; j++)
            av_log(NULL, AV_LOG_TRACE,
                   "nalUnitLength[%"PRIu8"][%"PRIu16"]:                 %"PRIu16"\n",
                   i, j, array->nalUnitLength[j]);
    }

    /*
     * We need at least one SPS.
     */
    sps_count = evcc->arrays[SPS_INDEX].numNalus;
    if (!sps_count || sps_count > EVC_MAX_SPS_COUNT)
        return AVERROR_INVALIDDATA;

    /* unsigned int(8) configurationVersion = 1; */
    avio_w8(pb, evcc->configurationVersion);

    /* unsigned int(8) profile_idc */
    avio_w8(pb, evcc->profile_idc);

    /* unsigned int(8) profile_idc */
    avio_w8(pb, evcc->level_idc);

    /* unsigned int(32) toolset_idc_h */
    avio_wb32(pb, evcc->toolset_idc_h);

    /* unsigned int(32) toolset_idc_l */
    avio_wb32(pb, evcc->toolset_idc_l);

    /*
     * unsigned int(2) chroma_format_idc;
     * unsigned int(3) bit_depth_luma_minus8;
     * unsigned int(3) bit_depth_chroma_minus8;
     */
    avio_w8(pb, evcc->chroma_format_idc << 6 |
            evcc->bit_depth_luma_minus8  << 3 |
            evcc->bit_depth_chroma_minus8);

    /* unsigned int(16) pic_width_in_luma_samples; */
    avio_wb16(pb, evcc->pic_width_in_luma_samples);

    /* unsigned int(16) pic_width_in_luma_samples; */
    avio_wb16(pb, evcc->pic_height_in_luma_samples);

    /*
     * bit(6) reserved = '111111'b;
     * unsigned int(2) chromaFormat;
     */
    avio_w8(pb, evcc->lengthSizeMinusOne | 0xfc);

    /* unsigned int(8) numOfArrays; */
    avio_w8(pb, evcc->num_of_arrays);

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(evcc->arrays); i++) {
        const EVCNALUnitArray *const array = &evcc->arrays[i];

        if (!array->numNalus)
            continue;

        /*
         * bit(1) array_completeness;
         * unsigned int(1) reserved = 0;
         * unsigned int(6) NAL_unit_type;
         */
        avio_w8(pb, array->array_completeness << 7 |
                array->NAL_unit_type & 0x3f);

        /* unsigned int(16) numNalus; */
        avio_wb16(pb, array->numNalus);

        for (unsigned j = 0; j < array->numNalus; j++) {
            /* unsigned int(16) nalUnitLength; */
            avio_wb16(pb, array->nalUnitLength[j]);

            /* bit(8*nalUnitLength) nalUnit; */
            avio_write(pb, array->nalUnit[j],
                       array->nalUnitLength[j]);
        }
    }

    return 0;
}

int ff_isom_write_evcc(AVIOContext *pb, const uint8_t *data,
                       int size, int ps_array_completeness)
{
    EVCDecoderConfigurationRecord evcc;
    int nalu_type;
    size_t nalu_size;
    int bytes_to_read = size;
    unsigned array_index;

    int ret = 0;

    if (size < 8) {
        /* We can't write a valid evcC from the provided data */
        return AVERROR_INVALIDDATA;
    } else if (*data == 1) {
        /* Data is already evcC-formatted */
        avio_write(pb, data, size);
        return 0;
    }

    evcc_init(&evcc);

    while (bytes_to_read > EVC_NALU_LENGTH_PREFIX_SIZE) {
        nalu_size = evc_read_nal_unit_length(data, EVC_NALU_LENGTH_PREFIX_SIZE);
        if (nalu_size == 0) break;

        data += EVC_NALU_LENGTH_PREFIX_SIZE;
        bytes_to_read -= EVC_NALU_LENGTH_PREFIX_SIZE;

        if (bytes_to_read < nalu_size) break;

        nalu_type = evc_get_nalu_type(data, bytes_to_read);
        if (nalu_type < EVC_NOIDR_NUT || nalu_type > EVC_UNSPEC_NUT62) {
            ret = AVERROR_INVALIDDATA;
            goto end;
        }

        // @see ISO/IEC 14496-15:2021 Coding of audio-visual objects - Part 15: section 12.3.3.3
        // NAL_unit_type indicates the type of the NAL units in the following array (which shall be all of that type);
        // - it takes a value as defined in ISO/IEC 23094-1;
        // - it is restricted to take one of the values indicating a SPS, PPS, APS, or SEI NAL unit.
        switch (nalu_type) {
        case EVC_SPS_NUT:
            array_index = SPS_INDEX;
            break;
        case EVC_PPS_NUT:
            array_index = PPS_INDEX;
            break;
        case EVC_APS_NUT:
            array_index = APS_INDEX;
            break;
        case EVC_SEI_NUT:
            array_index = SEI_INDEX;
            break;
        default:
            array_index = -1;
            break;
        }

        if( (array_index == SPS_INDEX) ||
            (array_index == PPS_INDEX) ||
            (array_index == APS_INDEX) ||
            (array_index == SEI_INDEX) ) {

            ret = evcc_array_add_nal_unit(data, nalu_size, nalu_type, ps_array_completeness, &(evcc.arrays[array_index]));

            if (ret < 0)
                goto end;
            if (evcc.arrays[array_index].numNalus == 1)
                evcc.num_of_arrays++;

            if(nalu_type == EVC_SPS_NUT) {
                ret = evcc_parse_sps(data, nalu_size, &evcc);
                if (ret < 0)
                    goto end;
            }
        }

        data += nalu_size;
        bytes_to_read -= nalu_size;
    }

    ret = evcc_write(pb, &evcc);

end:
    evcc_close(&evcc);
    return ret;
}
