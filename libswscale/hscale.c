/*
 * Copyright (C) 2015 Pedro Arthur <bygrandao@gmail.com>
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

#include "swscale_internal.h"

/// Scaler instance data
typedef struct FilterContext
{
    uint16_t *filter;
    int *filter_pos;
    int filter_size;
    int xInc;
} FilterContext;

/// Color conversion instance data
typedef struct ColorContext
{
    uint32_t *pal;
} ColorContext;

static int lum_h_scale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    FilterContext *instance = desc->instance;
    int srcW = desc->src->width;
    int dstW = desc->dst->width;
    int xInc = instance->xInc;

    int i;
    for (i = 0; i < sliceH; ++i) {
        uint8_t ** src = desc->src->plane[0].line;
        uint8_t ** dst = desc->dst->plane[0].line;
        int src_pos = sliceY+i - desc->src->plane[0].sliceY;
        int dst_pos = sliceY+i - desc->dst->plane[0].sliceY;


        if (c->hyscale_fast) {
            c->hyscale_fast(c, (int16_t*)dst[dst_pos], dstW, src[src_pos], srcW, xInc);
        } else {
            c->hyScale(c, (int16_t*)dst[dst_pos], dstW, (const uint8_t *)src[src_pos], instance->filter,
                       instance->filter_pos, instance->filter_size);
        }

        if (c->lumConvertRange)
            c->lumConvertRange((int16_t*)dst[dst_pos], dstW);

        desc->dst->plane[0].sliceH += 1;

        if (desc->alpha) {
            src = desc->src->plane[3].line;
            dst = desc->dst->plane[3].line;

            src_pos = sliceY+i - desc->src->plane[3].sliceY;
            dst_pos = sliceY+i - desc->dst->plane[3].sliceY;

            desc->dst->plane[3].sliceH += 1;

            if (c->hyscale_fast) {
                c->hyscale_fast(c, (int16_t*)dst[dst_pos], dstW, src[src_pos], srcW, xInc);
            } else {
                c->hyScale(c, (int16_t*)dst[dst_pos], dstW, (const uint8_t *)src[src_pos], instance->filter,
                            instance->filter_pos, instance->filter_size);
            }
        }
    }

    return sliceH;
}

static int lum_convert(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    int srcW = desc->src->width;
    ColorContext * instance = desc->instance;
    uint32_t * pal = instance->pal;
    int i;

    desc->dst->plane[0].sliceY = sliceY;
    desc->dst->plane[0].sliceH = sliceH;
    desc->dst->plane[3].sliceY = sliceY;
    desc->dst->plane[3].sliceH = sliceH;

    for (i = 0; i < sliceH; ++i) {
        int sp0 = sliceY+i - desc->src->plane[0].sliceY;
        int sp1 = ((sliceY+i) >> desc->src->v_chr_sub_sample) - desc->src->plane[1].sliceY;
        const uint8_t * src[4] = { desc->src->plane[0].line[sp0],
                        desc->src->plane[1].line[sp1],
                        desc->src->plane[2].line[sp1],
                        desc->src->plane[3].line[sp0]};
        uint8_t * dst = desc->dst->plane[0].line[i];

        if (c->lumToYV12) {
            c->lumToYV12(dst, src[0], src[1], src[2], srcW, pal, c->input_opaque);
        } else if (c->readLumPlanar) {
            c->readLumPlanar(dst, src, srcW, c->input_rgb2yuv_table, c->input_opaque);
        }


        if (desc->alpha) {
            dst = desc->dst->plane[3].line[i];
            if (c->alpToYV12) {
                c->alpToYV12(dst, src[3], src[1], src[2], srcW, pal, c->input_opaque);
            } else if (c->readAlpPlanar) {
                c->readAlpPlanar(dst, src, srcW, NULL, c->input_opaque);
            }
        }
    }

    return sliceH;
}

int ff_init_desc_fmt_convert(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst, uint32_t *pal)
{
    ColorContext * li = av_malloc(sizeof(ColorContext));
    if (!li)
        return AVERROR(ENOMEM);
    li->pal = pal;
    desc->instance = li;

    desc->alpha = isALPHA(src->fmt) && isALPHA(dst->fmt);
    desc->src =src;
    desc->dst = dst;
    desc->process = &lum_convert;

    return 0;
}


int ff_init_desc_hscale(SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst, uint16_t *filter, int * filter_pos, int filter_size, int xInc)
{
    FilterContext *li = av_malloc(sizeof(FilterContext));
    if (!li)
        return AVERROR(ENOMEM);

    li->filter = filter;
    li->filter_pos = filter_pos;
    li->filter_size = filter_size;
    li->xInc = xInc;

    desc->instance = li;

    desc->alpha = isALPHA(src->fmt) && isALPHA(dst->fmt);
    desc->src = src;
    desc->dst = dst;

    desc->process = &lum_h_scale;

    return 0;
}

static int chr_h_scale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    FilterContext *instance = desc->instance;
    int srcW = AV_CEIL_RSHIFT(desc->src->width, desc->src->h_chr_sub_sample);
    int dstW = AV_CEIL_RSHIFT(desc->dst->width, desc->dst->h_chr_sub_sample);
    int xInc = instance->xInc;

    uint8_t ** src1 = desc->src->plane[1].line;
    uint8_t ** dst1 = desc->dst->plane[1].line;
    uint8_t ** src2 = desc->src->plane[2].line;
    uint8_t ** dst2 = desc->dst->plane[2].line;

    int src_pos1 = sliceY - desc->src->plane[1].sliceY;
    int dst_pos1 = sliceY - desc->dst->plane[1].sliceY;

    int src_pos2 = sliceY - desc->src->plane[2].sliceY;
    int dst_pos2 = sliceY - desc->dst->plane[2].sliceY;

    int i;
    for (i = 0; i < sliceH; ++i) {
        if (c->hcscale_fast) {
            c->hcscale_fast(c, (uint16_t*)dst1[dst_pos1+i], (uint16_t*)dst2[dst_pos2+i], dstW, src1[src_pos1+i], src2[src_pos2+i], srcW, xInc);
        } else {
            c->hcScale(c, (uint16_t*)dst1[dst_pos1+i], dstW, src1[src_pos1+i], instance->filter, instance->filter_pos, instance->filter_size);
            c->hcScale(c, (uint16_t*)dst2[dst_pos2+i], dstW, src2[src_pos2+i], instance->filter, instance->filter_pos, instance->filter_size);
        }

        if (c->chrConvertRange)
            c->chrConvertRange((uint16_t*)dst1[dst_pos1+i], (uint16_t*)dst2[dst_pos2+i], dstW);

        desc->dst->plane[1].sliceH += 1;
        desc->dst->plane[2].sliceH += 1;
    }
    return sliceH;
}

static int chr_convert(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    int srcW = AV_CEIL_RSHIFT(desc->src->width, desc->src->h_chr_sub_sample);
    ColorContext * instance = desc->instance;
    uint32_t * pal = instance->pal;

    int sp0 = (sliceY - (desc->src->plane[0].sliceY >> desc->src->v_chr_sub_sample)) << desc->src->v_chr_sub_sample;
    int sp1 = sliceY - desc->src->plane[1].sliceY;

    int i;

    desc->dst->plane[1].sliceY = sliceY;
    desc->dst->plane[1].sliceH = sliceH;
    desc->dst->plane[2].sliceY = sliceY;
    desc->dst->plane[2].sliceH = sliceH;

    for (i = 0; i < sliceH; ++i) {
        const uint8_t * src[4] = { desc->src->plane[0].line[sp0+i],
                        desc->src->plane[1].line[sp1+i],
                        desc->src->plane[2].line[sp1+i],
                        desc->src->plane[3].line[sp0+i]};

        uint8_t * dst1 = desc->dst->plane[1].line[i];
        uint8_t * dst2 = desc->dst->plane[2].line[i];
        if (c->chrToYV12) {
            c->chrToYV12(dst1, dst2, src[0], src[1], src[2], srcW, pal, c->input_opaque);
        } else if (c->readChrPlanar) {
            c->readChrPlanar(dst1, dst2, src, srcW, c->input_rgb2yuv_table, c->input_opaque);
        }
    }
    return sliceH;
}

int ff_init_desc_cfmt_convert(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst, uint32_t *pal)
{
    ColorContext * li = av_malloc(sizeof(ColorContext));
    if (!li)
        return AVERROR(ENOMEM);
    li->pal = pal;
    desc->instance = li;

    desc->src =src;
    desc->dst = dst;
    desc->process = &chr_convert;

    return 0;
}

int ff_init_desc_chscale(SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst, uint16_t *filter, int * filter_pos, int filter_size, int xInc)
{
    FilterContext *li = av_malloc(sizeof(FilterContext));
    if (!li)
        return AVERROR(ENOMEM);

    li->filter = filter;
    li->filter_pos = filter_pos;
    li->filter_size = filter_size;
    li->xInc = xInc;

    desc->instance = li;

    desc->alpha = isALPHA(src->fmt) && isALPHA(dst->fmt);
    desc->src = src;
    desc->dst = dst;

    desc->process = &chr_h_scale;

    return 0;
}

static int no_chr_scale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    desc->dst->plane[1].sliceY = sliceY + sliceH - desc->dst->plane[1].available_lines;
    desc->dst->plane[1].sliceH = desc->dst->plane[1].available_lines;
    desc->dst->plane[2].sliceY = sliceY + sliceH - desc->dst->plane[2].available_lines;
    desc->dst->plane[2].sliceH = desc->dst->plane[2].available_lines;
    return 0;
}

int ff_init_desc_no_chr(SwsFilterDescriptor *desc, SwsSlice * src, SwsSlice *dst)
{
    desc->src = src;
    desc->dst = dst;
    desc->alpha = 0;
    desc->instance = NULL;
    desc->process = &no_chr_scale;
    return 0;
}
