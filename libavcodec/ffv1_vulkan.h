/*
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#ifndef AVCODEC_FFV1_VULKAN_H
#define AVCODEC_FFV1_VULKAN_H

#include "libavutil/vulkan.h"
#include "ffv1.h"

void ff_ffv1_vk_set_common_sl(AVCodecContext *avctx, FFV1Context *f,
                              VkSpecializationInfo *sl,
                              enum AVPixelFormat sw_format);

int ff_ffv1_vk_update_state_transition_data(FFVulkanContext *s,
                                            FFVkBuffer *vkb, FFV1Context *f);

int ff_ffv1_vk_init_state_transition_data(FFVulkanContext *s,
                                          FFVkBuffer *vkb, FFV1Context *f);

int ff_ffv1_vk_init_quant_table_data(FFVulkanContext *s,
                                     FFVkBuffer *vkb, FFV1Context *f);

int ff_ffv1_vk_init_crc_table_data(FFVulkanContext *s,
                                   FFVkBuffer *vkb, FFV1Context *f);

typedef struct FFv1ShaderParams {
    VkDeviceAddress slice_data;

    uint32_t extend_lookup[8];
    uint16_t context_count[8];

    int fmt_lut[4];
    uint16_t img_size[2];

    uint32_t plane_state_size;
    uint32_t key_frame;
    uint32_t crcref;
    int micro_version;

    /* Encoder-only */
    int sar[2];
    int pic_mode;
    uint32_t slice_size_max;
} FFv1ShaderParams;

#endif /* AVCODEC_FFV1_VULKAN_H */
