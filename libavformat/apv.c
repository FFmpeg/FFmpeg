/*
 * APV helper functions for muxers
 * Copyright (c) 2025 Dawid Kozinski <d.kozinski@samsung.com>
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "apv.h"
#include "cbs.h"
#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "libavcodec/cbs_apv.h"
#include "libavcodec/packet.h"

typedef struct APVDecoderFrameInfo {
    uint8_t color_description_present_flag;         // 1 bit

    // The variable indicates whether the capture_time_distance value in the APV bitstream's frame header should be ignored during playback.
    // If capture_time_distance_ignored is set to true, the capture_time_distance information will not be utilized,
    // and timing information for playback should be calculated using an alternative method.
    // If set to false, the capture_time_distance value will be used as is from the frame header.
    // It is recommended to set this variable to true, allowing the use of MP4 timestamps for playback and recording,
    // which enables the conventional compression and playback methods based on the timestamp table defined by the ISO-based file format.
    uint8_t capture_time_distance_ignored;          // 1-bit

    uint8_t profile_idc;                            // 8 bits
    uint8_t level_idc;                              // 8 bits
    uint8_t band_idc;                               // 8 bits
    uint32_t frame_width;                           // 32 bits
    uint32_t frame_height;                          // 32 bits
    uint8_t chroma_format_idc;                      // 4 bits
    uint8_t bit_depth_minus8;                       // 4 bits
    uint8_t capture_time_distance;                  // 8 bits

    // if (color_description_present_flag)
    uint8_t color_primaries;                        // 8 bits
    uint8_t transfer_characteristics;               // 8 bits
    uint8_t matrix_coefficients;                    // 8 bits
    uint8_t full_range_flag;                        // 1 bit
} APVDecoderFrameInfo;

typedef struct APVDecoderConfigurationEntry {
    uint8_t pbu_type;                   // 8 bits
    uint8_t number_of_frame_info;       // 8 bits

    APVDecoderFrameInfo *frame_info;   // An array of size number_of_frame_info storing elements of type APVDecoderFrameInfo*
} APVDecoderConfigurationEntry;

// ISOBMFF binding for APV
// @see https://github.com/openapv/openapv/blob/main/readme/apv_isobmff.md
typedef struct APVDecoderConfigurationRecord  {
    uint8_t configurationVersion;           // 8 bits
    uint8_t number_of_configuration_entry;  // 8 bits

    APVDecoderConfigurationEntry *configuration_entry; // table of size number_of_configuration_entry

    CodedBitstreamContext *cbc;
    CodedBitstreamFragment frag;
} APVDecoderConfigurationRecord;

void ff_isom_write_apvc(AVIOContext *pb, const APVDecoderConfigurationRecord *apvc, void *logctx)
{
    av_log(logctx, AV_LOG_TRACE, "configurationVersion:                           %"PRIu8"\n",
           apvc->configurationVersion);

    av_log(logctx, AV_LOG_TRACE, "number_of_configuration_entry:                  %"PRIu8"\n",
           apvc->number_of_configuration_entry);

    for (int i = 0; i < apvc->number_of_configuration_entry; i++) {
        const APVDecoderConfigurationEntry *configuration_entry = &apvc->configuration_entry[i];

        av_log(logctx, AV_LOG_TRACE, "pbu_type:                                       %"PRIu8"\n",
               configuration_entry->pbu_type);

        av_log(logctx, AV_LOG_TRACE, "number_of_frame_info:                           %"PRIu8"\n",
               configuration_entry->number_of_frame_info);

        for (int j = 0; j < configuration_entry->number_of_frame_info; j++) {
            const APVDecoderFrameInfo *frame_info = &configuration_entry->frame_info[j];

            av_log(logctx, AV_LOG_TRACE, "color_description_present_flag:                 %"PRIu8"\n",
                   frame_info->color_description_present_flag);

            av_log(logctx, AV_LOG_TRACE, "capture_time_distance_ignored:                  %"PRIu8"\n",
                   frame_info->capture_time_distance_ignored);

            av_log(logctx, AV_LOG_TRACE, "profile_idc:                                    %"PRIu8"\n",
                   frame_info->profile_idc);

            av_log(logctx, AV_LOG_TRACE, "level_idc:                                      %"PRIu8"\n",
                   frame_info->level_idc);

            av_log(logctx, AV_LOG_TRACE, "band_idc:                                       %"PRIu8"\n",
                   frame_info->band_idc);

            av_log(logctx, AV_LOG_TRACE, "frame_width:                                    %"PRIu32"\n",
                   frame_info->frame_width);

            av_log(logctx, AV_LOG_TRACE, "frame_height:                                   %"PRIu32"\n",
                   frame_info->frame_height);

            av_log(logctx, AV_LOG_TRACE, "chroma_format_idc:                              %"PRIu8"\n",
                   frame_info->chroma_format_idc);

            av_log(logctx, AV_LOG_TRACE, "bit_depth_minus8:                               %"PRIu8"\n",
                   frame_info->bit_depth_minus8);

            av_log(logctx, AV_LOG_TRACE, "capture_time_distance:                          %"PRIu8"\n",
                   frame_info->capture_time_distance);

            if (frame_info->color_description_present_flag) {
                av_log(logctx, AV_LOG_TRACE, "color_primaries:                                %"PRIu8"\n",
                       frame_info->color_primaries);

                av_log(logctx, AV_LOG_TRACE, "transfer_characteristics:                       %"PRIu8"\n",
                       frame_info->transfer_characteristics);

                av_log(logctx, AV_LOG_TRACE, "matrix_coefficients:                            %"PRIu8"\n",
                       frame_info->matrix_coefficients);

                av_log(logctx, AV_LOG_TRACE, "full_range_flag:                                %"PRIu8"\n",
                       frame_info->full_range_flag);
            }
        }
    }

    /* unsigned int(8) configurationVersion = 1; */
    avio_w8(pb, apvc->configurationVersion);

    avio_w8(pb, apvc->number_of_configuration_entry);

    for (int i = 0; i < apvc->number_of_configuration_entry; i++) {
        const APVDecoderConfigurationEntry *configuration_entry = &apvc->configuration_entry[i];

        avio_w8(pb, configuration_entry->pbu_type);
        avio_w8(pb, configuration_entry->number_of_frame_info);

        for (int j = 0; j < configuration_entry->number_of_frame_info; j++) {
            const APVDecoderFrameInfo *frame_info = &configuration_entry->frame_info[j];

            /* reserved_zero_6bits
             * unsigned int(1) color_description_present_flag
             * unsigned int(1) capture_time_distance_ignored */
            avio_w8(pb, frame_info->color_description_present_flag << 1 |
                        frame_info->capture_time_distance_ignored);

            /* unsigned int(8) profile_idc */
            avio_w8(pb, frame_info->profile_idc);

            /* unsigned int(8) level_idc */
            avio_w8(pb, frame_info->level_idc);

            /* unsigned int(8) band_idc */
            avio_w8(pb, frame_info->band_idc);

            /* unsigned int(32) frame_width_minus1 */
            avio_wb32(pb, frame_info->frame_width);

            /* unsigned int(32) frame_height_minus1 */
            avio_wb32(pb, frame_info->frame_height);

            /* unsigned int(4) chroma_format_idc */
            /* unsigned int(4) bit_depth_minus8 */
            avio_w8(pb, (frame_info->chroma_format_idc << 4) |
                        frame_info->bit_depth_minus8);

            /* unsigned int(8) capture_time_distance */
            avio_w8(pb, frame_info->capture_time_distance);

            if (frame_info->color_description_present_flag) {
                /* unsigned int(8) color_primaries */
                avio_w8(pb, frame_info->color_primaries);

                /* unsigned int(8) transfer_characteristics */
                avio_w8(pb, frame_info->transfer_characteristics);

                /* unsigned int(8) matrix_coefficients */
                avio_w8(pb, frame_info->matrix_coefficients);

                /* unsigned int(1) full_range_flag
                 * reserved_zero_7bits */
                avio_w8(pb, frame_info->full_range_flag << 7);
            }
        }
    }
}

static const CodedBitstreamUnitType decompose_unit_types[] = {
    APV_PBU_PRIMARY_FRAME, APV_PBU_NON_PRIMARY_FRAME,
    APV_PBU_PREVIEW_FRAME, APV_PBU_DEPTH_FRAME, APV_PBU_ALPHA_FRAME
};

static int apv_add_configuration_entry(APVDecoderConfigurationRecord *apvc, int pbu_type)
{
    APVDecoderConfigurationEntry *temp;

    av_assert0(apvc->number_of_configuration_entry < FF_ARRAY_ELEMS(decompose_unit_types));
    temp = av_realloc_array(apvc->configuration_entry,
                            apvc->number_of_configuration_entry + 1, sizeof(*apvc->configuration_entry));

    if (!temp)
        return AVERROR(ENOMEM);

    apvc->configuration_entry = temp;
    memset(&apvc->configuration_entry[apvc->number_of_configuration_entry], 0, sizeof(*apvc->configuration_entry));
    apvc->configuration_entry[apvc->number_of_configuration_entry].pbu_type = pbu_type;
    apvc->number_of_configuration_entry++;

    return 0;
}

static int apv_add_frameinfo(APVDecoderConfigurationEntry *configuration_entry,
                             const APVDecoderFrameInfo *frame_info)
{
    APVDecoderFrameInfo *temp;

    if (configuration_entry->number_of_frame_info >= UINT8_MAX)
        return AVERROR(EINVAL);

    temp = av_realloc_array(configuration_entry->frame_info,
                            configuration_entry->number_of_frame_info + 1, sizeof(*configuration_entry->frame_info));

    if (!temp)
        return AVERROR(ENOMEM);

    configuration_entry->frame_info = temp;
    memcpy(&configuration_entry->frame_info[configuration_entry->number_of_frame_info], frame_info, sizeof(*frame_info));
    configuration_entry->number_of_frame_info++;

    return 0;
}

int ff_isom_parse_apvc(APVDecoderConfigurationRecord *apvc,
                       const AVPacket *pkt, void *logctx)
{
    APVDecoderFrameInfo frame_info;
    int ret;

    if (pkt->size < 8 || AV_RB32(pkt->data) != APV_SIGNATURE)
        /* We can't write a valid apvC from the provided data */
        return AVERROR_INVALIDDATA;

    ret = ff_lavf_cbs_read(apvc->cbc, &apvc->frag, pkt->buf, pkt->data, pkt->size);
    if (ret < 0) {
        av_log(logctx, AV_LOG_ERROR, "Failed to parse access unit.\n");
        return ret;
    }

    memset(&frame_info, 0, sizeof(frame_info));
    frame_info.capture_time_distance_ignored = 1;

    for (int i = 0; i < apvc->frag.nb_units; i++) {
        const CodedBitstreamUnit *pbu = &apvc->frag.units[i];
        int j;

        switch (pbu->type) {
        case APV_PBU_PRIMARY_FRAME:
        case APV_PBU_NON_PRIMARY_FRAME:
        case APV_PBU_PREVIEW_FRAME:
        case APV_PBU_DEPTH_FRAME:
        case APV_PBU_ALPHA_FRAME:
            break;
        default:
            continue;
        };

        const APVRawFrame *frame        = pbu->content;
        const APVRawFrameHeader *header = &frame->frame_header;
        const APVRawFrameInfo *info     = &header->frame_info;
        int bit_depth = info->bit_depth_minus8 + 8;

        if (bit_depth < 8 || bit_depth > 16 || bit_depth % 2)
            break;

        frame_info.profile_idc = info->profile_idc;
        frame_info.level_idc = info->level_idc;
        frame_info.band_idc = info->band_idc;

        frame_info.frame_width = info->frame_width;
        frame_info.frame_height =info->frame_height;
        frame_info.chroma_format_idc = info->chroma_format_idc;
        frame_info.bit_depth_minus8 = info->bit_depth_minus8;
        frame_info.capture_time_distance = info->capture_time_distance;

        frame_info.color_description_present_flag = header->color_description_present_flag;
        if (frame_info.color_description_present_flag) {
            frame_info.color_primaries = header->color_primaries;
            frame_info.transfer_characteristics = header->transfer_characteristics;
            frame_info.matrix_coefficients = header->matrix_coefficients;
            frame_info.full_range_flag = header->full_range_flag;
        } else {
            frame_info.color_primaries =
            frame_info.transfer_characteristics =
            frame_info.matrix_coefficients =
            frame_info.full_range_flag = 0;
        }

        for (j = 0; j < apvc->number_of_configuration_entry; j++) {
            int k;

            if (apvc->configuration_entry[j].pbu_type != pbu->type)
                continue;

            for (k = 0; k < apvc->configuration_entry[j].number_of_frame_info; k++) {
                if (!memcmp(&apvc->configuration_entry[j].frame_info[k], &frame_info, sizeof(frame_info)))
                    break;
            }
            if (k == apvc->configuration_entry[j].number_of_frame_info) {
                ret = apv_add_frameinfo(&apvc->configuration_entry[j], &frame_info);
                if (ret < 0)
                    goto end;
            }
            break;
        }

        if (j == apvc->number_of_configuration_entry) {
            ret = apv_add_configuration_entry(apvc, pbu->type);
            if (ret < 0)
                goto end;
            ret = apv_add_frameinfo(&apvc->configuration_entry[j], &frame_info);
            if (ret < 0)
                goto end;
        }
    }

    ret = 0;
end:
    ff_lavf_cbs_fragment_reset(&apvc->frag);

    return ret;
}

int ff_isom_init_apvc(APVDecoderConfigurationRecord **papvc, void *logctx)
{
    APVDecoderConfigurationRecord *apvc = av_mallocz(sizeof(*apvc));

    if (!apvc)
        return AVERROR(ENOMEM);

    int ret = ff_lavf_cbs_init(&apvc->cbc, AV_CODEC_ID_APV, logctx);
    if (ret < 0) {
        av_freep(&apvc);
        return ret;
    }

    apvc->cbc->decompose_unit_types    = decompose_unit_types;
    apvc->cbc->nb_decompose_unit_types = FF_ARRAY_ELEMS(decompose_unit_types);

    apvc->configurationVersion = 1;

    *papvc = apvc;

    return 0;
}

void ff_isom_close_apvc(APVDecoderConfigurationRecord **papvc)
{
    APVDecoderConfigurationRecord *apvc = *papvc;

    if (!apvc)
        return;

    for (int i = 0; i < apvc->number_of_configuration_entry; i++)
        av_freep(&apvc->configuration_entry[i].frame_info);
    av_freep(&apvc->configuration_entry);

    ff_lavf_cbs_fragment_free(&apvc->frag);
    ff_lavf_cbs_close(&apvc->cbc);

    av_freep(papvc);
}
