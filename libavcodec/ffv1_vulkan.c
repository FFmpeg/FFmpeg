/*
 * Copyright (c) 2025 Lynne <dev@lynne.ee>
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

#include "ffv1_vulkan.h"
#include "libavutil/crc.h"

int ff_ffv1_vk_update_state_transition_data(FFVulkanContext *s,
                                            FFVkBuffer *vkb, FFV1Context *f)
{
    int err;
    uint8_t *buf_mapped;

    RET(ff_vk_map_buffer(s, vkb, &buf_mapped, 0));

    for (int i = 1; i < 256; i++) {
        buf_mapped[256 + i] = f->state_transition[i];
        buf_mapped[256 - i] = 256 - (int)f->state_transition[i];
    }

    RET(ff_vk_unmap_buffer(s, vkb, 1));

fail:
    return err;
}

static int init_state_transition_data(FFVulkanContext *s,
                                      FFVkBuffer *vkb, FFV1Context *f,
                                      int (*write_data)(FFVulkanContext *s,
                                                        FFVkBuffer *vkb, FFV1Context *f))
{
    int err;
    size_t buf_len = 512*sizeof(uint8_t);

    RET(ff_vk_create_buf(s, vkb,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

    write_data(s, vkb, f);

fail:
    return err;
}

int ff_ffv1_vk_init_state_transition_data(FFVulkanContext *s,
                                          FFVkBuffer *vkb, FFV1Context *f)
{
    return init_state_transition_data(s, vkb, f,
                                      ff_ffv1_vk_update_state_transition_data);
}

int ff_ffv1_vk_init_quant_table_data(FFVulkanContext *s,
                                     FFVkBuffer *vkb, FFV1Context *f)
{
    int err;

    int16_t *buf_mapped;
    size_t buf_len = MAX_QUANT_TABLES*
                     MAX_CONTEXT_INPUTS*
                     MAX_QUANT_TABLE_SIZE*sizeof(int16_t);

    RET(ff_vk_create_buf(s, vkb,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(s, vkb, (void *)&buf_mapped, 0));

    memcpy(buf_mapped, f->quant_tables,
           sizeof(f->quant_tables));

    RET(ff_vk_unmap_buffer(s, vkb, 1));

fail:
    return err;
}

int ff_ffv1_vk_init_crc_table_data(FFVulkanContext *s,
                                   FFVkBuffer *vkb, FFV1Context *f)
{
    int err;

    uint32_t *buf_mapped;
    size_t buf_len = 256*sizeof(int32_t);

    RET(ff_vk_create_buf(s, vkb,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(s, vkb, (void *)&buf_mapped, 0));

    memcpy(buf_mapped, av_crc_get_table(AV_CRC_32_IEEE), buf_len);

    RET(ff_vk_unmap_buffer(s, vkb, 1));

fail:
    return err;
}
