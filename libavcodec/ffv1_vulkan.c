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

void ff_ffv1_vk_set_common_sl(AVCodecContext *avctx, FFV1Context *f,
                              VkSpecializationInfo *sl,
                              enum AVPixelFormat sw_format)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(sw_format);
    int color_planes = av_pix_fmt_desc_get(sw_format)->nb_components;
    int is_rgb = !(f->colorspace == 0 && sw_format != AV_PIX_FMT_YA8) &&
                 !(sw_format == AV_PIX_FMT_YA8);

    SPEC_LIST_ADD(sl,  2, 32, f->version);
    SPEC_LIST_ADD(sl,  3, 32, f->quant_table_count);

    for (int i = 0; i < f->quant_table_count; i++) {
        if (f->quant_tables[i][3][127] || f->quant_tables[i][4][127]) {
            SPEC_LIST_ADD(sl, 4, 32, 1);
            break;
        }
    }

    int bits = desc->comp[0].depth;
    /* Bayer pixfmts report misleading per-component depth in comp[0].depth
     * (it counts the fraction of bits each component contributes per output
     * pixel, not the per-sample bit width). Use bits_per_raw_sample. The
     * encoder fills f->bits_per_raw_sample directly; the decoder only
     * fills f->avctx->bits_per_raw_sample. Prefer the FFV1Context field
     * with the avctx field as a fallback so this works from both sides. */
    if (f->bayer)
        bits = f->bits_per_raw_sample ? f->bits_per_raw_sample
                                      : f->avctx->bits_per_raw_sample;
    SPEC_LIST_ADD(sl,  5, 32, (uint32_t)(1ULL << bits));
    SPEC_LIST_ADD(sl,  6, 32, f->colorspace);
    SPEC_LIST_ADD(sl,  7, 32, f->transparency);
    SPEC_LIST_ADD(sl,  8, 32, ff_vk_mt_is_np_rgb(sw_format) &&
                              (desc->flags & AV_PIX_FMT_FLAG_PLANAR));
    SPEC_LIST_ADD(sl,  9, 32, f->plane_count);
    SPEC_LIST_ADD(sl, 10, 32, color_planes);
    SPEC_LIST_ADD(sl, 11, 32, av_pix_fmt_count_planes(sw_format));
    SPEC_LIST_ADD(sl, 12, 32, bits + is_rgb);

    SPEC_LIST_ADD(sl, 13, 32, f->chroma_h_shift);
    SPEC_LIST_ADD(sl, 14, 32, f->chroma_v_shift);
}

static void set_crc_tab(uint32_t *buf)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int j = 0; j < 8; j++)
            c = (c << 1) ^ (0x04C11DB7 & (((int32_t) c) >> 31));
        buf[i] = av_bswap32(c);
    }
}

static void set_rc_state_tab(FFV1Context *f, uint8_t *buf)
{
    for (int i = 1; i < 256; i++) {
        buf[256 + i] = f->state_transition[i];
        buf[256 - i] = 256 - (int)f->state_transition[i];
    }
}

uint32_t ff_ffv1_vk_rc_lanes(const FFVulkanContext *s, uint32_t *required)
{
    const VkPhysicalDeviceSubgroupSizeControlProperties *p = &s->subgroup_props;
    uint32_t min = p->minSubgroupSize ? p->minSubgroupSize : s->props_11.subgroupSize;
    uint32_t max = p->maxSubgroupSize ? p->maxSubgroupSize : s->props_11.subgroupSize;

    /* A workgroup of the required size is a single, full subgroup */
    if ((p->requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
        min <= CONTEXT_SIZE) {
        *required = FFMIN(max, CONTEXT_SIZE);
        return *required;
    }

    /* Otherwise, a workgroup no larger than the smallest subgroup is part of
     * a single one */
    *required = 0;
    return FFMIN(min, CONTEXT_SIZE);
}

int ff_ffv1_vk_quant_ballot(const FFV1Context *f, FFv1QuantBallot *qb)
{
    int ok = 1;

    for (int i = 0; i < MAX_QUANT_TABLES; i++)
        for (int k = 0; k < 32; k++)
            qb->thresh[i][k][0] = qb->thresh[i][k][1] = 128;
    memset(qb->scale_off, 0, sizeof(qb->scale_off));

    for (int i = 0; i < f->quant_table_count; i++) {
        for (int j = 0; j < 2; j++) {
            const int16_t *qt = f->quant_tables[i][3*j];
            int n = 0, scale = 0;

            for (int d = -127; d < 128; d++) {
                int step = qt[d & 255] - qt[(d - 1) & 255];
                if (!step)
                    continue;
                if (!scale)
                    scale = step;
                if (step != scale || n == 32 || (!j && step != 1)) {
                    ok = 0;
                    break;
                }
                qb->thresh[i][n++][j] = d;
            }

            if (j)
                qb->scale_off[i][0] = scale;
            qb->scale_off[i][1] += qt[128];
        }
    }

    return ok;
}

int ff_ffv1_vk_init_consts(FFVulkanContext *s, FFVkBuffer *vkb, FFV1Context *f)
{
    int err;

    uint8_t *buf_mapped;
    int32_t (*quant_tables)[MAX_CONTEXT_INPUTS][MAX_QUANT_TABLE_SIZE];
    size_t buf_len = 256*sizeof(uint32_t) + /* CRC */
                     512*sizeof(uint8_t) + /* Rangecoder */
                     MAX_QUANT_TABLES*
                     MAX_CONTEXT_INPUTS*
                     MAX_QUANT_TABLE_SIZE*sizeof(int32_t) +
                     sizeof(FFv1QuantBallot);

    RET(ff_vk_create_buf(s, vkb,
                         buf_len,
                         NULL, NULL,
                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));
    RET(ff_vk_map_buffer(s, vkb, (void *)&buf_mapped, 0));

    set_crc_tab((uint32_t *)buf_mapped);

    set_rc_state_tab(f, buf_mapped + 256*sizeof(uint32_t));

    quant_tables = (void *)(buf_mapped + 256*sizeof(uint32_t) + 512*sizeof(uint8_t));
    for (int i = 0; i < MAX_QUANT_TABLES; i++)
        for (int j = 0; j < MAX_CONTEXT_INPUTS; j++)
            for (int k = 0; k < MAX_QUANT_TABLE_SIZE; k++)
                quant_tables[i][j][k] = f->quant_tables[i][j][k];

    ff_ffv1_vk_quant_ballot(f, (FFv1QuantBallot *)(quant_tables + MAX_QUANT_TABLES));

    RET(ff_vk_unmap_buffer(s, vkb, 1));

fail:
    return err;
}
