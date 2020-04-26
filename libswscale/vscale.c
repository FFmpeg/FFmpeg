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

typedef struct VScalerContext
{
    uint16_t *filter[2];
    int32_t  *filter_pos;
    int filter_size;
    int isMMX;
    union {
        yuv2planar1_fn      yuv2planar1;
        yuv2planarX_fn      yuv2planarX;
        yuv2interleavedX_fn yuv2interleavedX;
        yuv2packed1_fn      yuv2packed1;
        yuv2packed2_fn      yuv2packed2;
        yuv2anyX_fn         yuv2anyX;
    } pfn;
    yuv2packedX_fn yuv2packedX;
} VScalerContext;


static int lum_planar_vscale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    VScalerContext *inst = desc->instance;
    int dstW = desc->dst->width;

    int first = FFMAX(1-inst->filter_size, inst->filter_pos[sliceY]);
    int sp = first - desc->src->plane[0].sliceY;
    int dp = sliceY - desc->dst->plane[0].sliceY;
    uint8_t **src = desc->src->plane[0].line + sp;
    uint8_t **dst = desc->dst->plane[0].line + dp;
    uint16_t *filter = inst->filter[0] + (inst->isMMX ? 0 : sliceY * inst->filter_size);

    if (inst->filter_size == 1)
        inst->pfn.yuv2planar1((const int16_t*)src[0], dst[0], dstW, c->lumDither8, 0);
    else
        inst->pfn.yuv2planarX(filter, inst->filter_size, (const int16_t**)src, dst[0], dstW, c->lumDither8, 0);

    if (desc->alpha) {
        int sp = first - desc->src->plane[3].sliceY;
        int dp = sliceY - desc->dst->plane[3].sliceY;
        uint8_t **src = desc->src->plane[3].line + sp;
        uint8_t **dst = desc->dst->plane[3].line + dp;
        uint16_t *filter = inst->filter[1] + (inst->isMMX ? 0 : sliceY * inst->filter_size);

        if (inst->filter_size == 1)
            inst->pfn.yuv2planar1((const int16_t*)src[0], dst[0], dstW, c->lumDither8, 0);
        else
            inst->pfn.yuv2planarX(filter, inst->filter_size, (const int16_t**)src, dst[0], dstW, c->lumDither8, 0);
    }

    return 1;
}

static int chr_planar_vscale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    const int chrSkipMask = (1 << desc->dst->v_chr_sub_sample) - 1;
    if (sliceY & chrSkipMask)
        return 0;
    else {
        VScalerContext *inst = desc->instance;
        int dstW = AV_CEIL_RSHIFT(desc->dst->width, desc->dst->h_chr_sub_sample);
        int chrSliceY = sliceY >> desc->dst->v_chr_sub_sample;

        int first = FFMAX(1-inst->filter_size, inst->filter_pos[chrSliceY]);
        int sp1 = first - desc->src->plane[1].sliceY;
        int sp2 = first - desc->src->plane[2].sliceY;
        int dp1 = chrSliceY - desc->dst->plane[1].sliceY;
        int dp2 = chrSliceY - desc->dst->plane[2].sliceY;
        uint8_t **src1 = desc->src->plane[1].line + sp1;
        uint8_t **src2 = desc->src->plane[2].line + sp2;
        uint8_t **dst1 = desc->dst->plane[1].line + dp1;
        uint8_t **dst2 = desc->dst->plane[2].line + dp2;
        uint16_t *filter = inst->filter[0] + (inst->isMMX ? 0 : chrSliceY * inst->filter_size);

        if (c->yuv2nv12cX) {
            inst->pfn.yuv2interleavedX(c, filter, inst->filter_size, (const int16_t**)src1, (const int16_t**)src2, dst1[0], dstW);
        } else if (inst->filter_size == 1) {
            inst->pfn.yuv2planar1((const int16_t*)src1[0], dst1[0], dstW, c->chrDither8, 0);
            inst->pfn.yuv2planar1((const int16_t*)src2[0], dst2[0], dstW, c->chrDither8, 3);
        } else {
            inst->pfn.yuv2planarX(filter, inst->filter_size, (const int16_t**)src1, dst1[0], dstW, c->chrDither8, 0);
            inst->pfn.yuv2planarX(filter, inst->filter_size, (const int16_t**)src2, dst2[0], dstW, c->chrDither8, inst->isMMX ? (c->uv_offx2 >> 1) : 3);
        }
    }

    return 1;
}

static int packed_vscale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    VScalerContext *inst = desc->instance;
    int dstW = desc->dst->width;
    int chrSliceY = sliceY >> desc->dst->v_chr_sub_sample;

    int lum_fsize = inst[0].filter_size;
    int chr_fsize = inst[1].filter_size;
    uint16_t *lum_filter = inst[0].filter[0];
    uint16_t *chr_filter = inst[1].filter[0];

    int firstLum = FFMAX(1-lum_fsize, inst[0].filter_pos[   sliceY]);
    int firstChr = FFMAX(1-chr_fsize, inst[1].filter_pos[chrSliceY]);

    int sp0 = firstLum - desc->src->plane[0].sliceY;
    int sp1 = firstChr - desc->src->plane[1].sliceY;
    int sp2 = firstChr - desc->src->plane[2].sliceY;
    int sp3 = firstLum - desc->src->plane[3].sliceY;
    int dp = sliceY - desc->dst->plane[0].sliceY;
    uint8_t **src0 = desc->src->plane[0].line + sp0;
    uint8_t **src1 = desc->src->plane[1].line + sp1;
    uint8_t **src2 = desc->src->plane[2].line + sp2;
    uint8_t **src3 = desc->alpha ? desc->src->plane[3].line + sp3 : NULL;
    uint8_t **dst = desc->dst->plane[0].line + dp;


    if (c->yuv2packed1 && lum_fsize == 1 && chr_fsize == 1) { // unscaled RGB
        inst->pfn.yuv2packed1(c, (const int16_t*)*src0, (const int16_t**)src1, (const int16_t**)src2,
                                    (const int16_t*)(desc->alpha ? *src3 : NULL),  *dst, dstW, 0, sliceY);
    } else if (c->yuv2packed1 && lum_fsize == 1 && chr_fsize == 2 &&
               chr_filter[2 * chrSliceY + 1] + chr_filter[2 * chrSliceY] == 4096 &&
               chr_filter[2 * chrSliceY + 1] <= 4096U) { // unscaled RGB
        int chrAlpha = chr_filter[2 * chrSliceY + 1];
        inst->pfn.yuv2packed1(c, (const int16_t*)*src0, (const int16_t**)src1, (const int16_t**)src2,
                                    (const int16_t*)(desc->alpha ? *src3 : NULL),  *dst, dstW, chrAlpha, sliceY);
    } else if (c->yuv2packed2 && lum_fsize == 2 && chr_fsize == 2 &&
               lum_filter[2 * sliceY + 1] + lum_filter[2 * sliceY] == 4096 &&
               lum_filter[2 * sliceY + 1] <= 4096U &&
               chr_filter[2 * chrSliceY + 1] + chr_filter[2 * chrSliceY] == 4096 &&
               chr_filter[2 * chrSliceY + 1] <= 4096U
    ) { // bilinear upscale RGB
        int lumAlpha = lum_filter[2 * sliceY + 1];
        int chrAlpha = chr_filter[2 * chrSliceY + 1];
        c->lumMmxFilter[2] =
        c->lumMmxFilter[3] = lum_filter[2 * sliceY]    * 0x10001;
        c->chrMmxFilter[2] =
        c->chrMmxFilter[3] = chr_filter[2 * chrSliceY] * 0x10001;
        inst->pfn.yuv2packed2(c, (const int16_t**)src0, (const int16_t**)src1, (const int16_t**)src2, (const int16_t**)src3,
                    *dst, dstW, lumAlpha, chrAlpha, sliceY);
    } else { // general RGB
        if ((c->yuv2packed1 && lum_fsize == 1 && chr_fsize == 2) ||
            (c->yuv2packed2 && lum_fsize == 2 && chr_fsize == 2)) {
            if (!c->warned_unuseable_bilinear)
                av_log(c, AV_LOG_INFO, "Optimized 2 tap filter code cannot be used\n");
            c->warned_unuseable_bilinear = 1;
        }

        inst->yuv2packedX(c, lum_filter + sliceY * lum_fsize,
                    (const int16_t**)src0, lum_fsize, chr_filter + chrSliceY * chr_fsize,
                    (const int16_t**)src1, (const int16_t**)src2, chr_fsize, (const int16_t**)src3, *dst, dstW, sliceY);
    }
    return 1;
}

static int any_vscale(SwsContext *c, SwsFilterDescriptor *desc, int sliceY, int sliceH)
{
    VScalerContext *inst = desc->instance;
    int dstW = desc->dst->width;
    int chrSliceY = sliceY >> desc->dst->v_chr_sub_sample;

    int lum_fsize = inst[0].filter_size;
    int chr_fsize = inst[1].filter_size;
    uint16_t *lum_filter = inst[0].filter[0];
    uint16_t *chr_filter = inst[1].filter[0];

    int firstLum = FFMAX(1-lum_fsize, inst[0].filter_pos[   sliceY]);
    int firstChr = FFMAX(1-chr_fsize, inst[1].filter_pos[chrSliceY]);

    int sp0 = firstLum - desc->src->plane[0].sliceY;
    int sp1 = firstChr - desc->src->plane[1].sliceY;
    int sp2 = firstChr - desc->src->plane[2].sliceY;
    int sp3 = firstLum - desc->src->plane[3].sliceY;
    int dp0 = sliceY - desc->dst->plane[0].sliceY;
    int dp1 = chrSliceY - desc->dst->plane[1].sliceY;
    int dp2 = chrSliceY - desc->dst->plane[2].sliceY;
    int dp3 = sliceY - desc->dst->plane[3].sliceY;

    uint8_t **src0 = desc->src->plane[0].line + sp0;
    uint8_t **src1 = desc->src->plane[1].line + sp1;
    uint8_t **src2 = desc->src->plane[2].line + sp2;
    uint8_t **src3 = desc->alpha ? desc->src->plane[3].line + sp3 : NULL;
    uint8_t *dst[4] = { desc->dst->plane[0].line[dp0],
                        desc->dst->plane[1].line[dp1],
                        desc->dst->plane[2].line[dp2],
                        desc->alpha ? desc->dst->plane[3].line[dp3] : NULL };

    av_assert1(!c->yuv2packed1 && !c->yuv2packed2);
    inst->pfn.yuv2anyX(c, lum_filter + sliceY * lum_fsize,
             (const int16_t**)src0, lum_fsize, chr_filter + sliceY * chr_fsize,
             (const int16_t**)src1, (const int16_t**)src2, chr_fsize, (const int16_t**)src3, dst, dstW, sliceY);

    return 1;

}

int ff_init_vscale(SwsContext *c, SwsFilterDescriptor *desc, SwsSlice *src, SwsSlice *dst)
{
    VScalerContext *lumCtx = NULL;
    VScalerContext *chrCtx = NULL;

    if (isPlanarYUV(c->dstFormat) || (isGray(c->dstFormat) && !isALPHA(c->dstFormat))) {
        lumCtx = av_mallocz(sizeof(VScalerContext));
        if (!lumCtx)
            return AVERROR(ENOMEM);


        desc[0].process = lum_planar_vscale;
        desc[0].instance = lumCtx;
        desc[0].src = src;
        desc[0].dst = dst;
        desc[0].alpha = c->needAlpha;

        if (!isGray(c->dstFormat)) {
            chrCtx = av_mallocz(sizeof(VScalerContext));
            if (!chrCtx)
                return AVERROR(ENOMEM);
            desc[1].process = chr_planar_vscale;
            desc[1].instance = chrCtx;
            desc[1].src = src;
            desc[1].dst = dst;
        }
    } else {
        lumCtx = av_mallocz_array(sizeof(VScalerContext), 2);
        if (!lumCtx)
            return AVERROR(ENOMEM);
        chrCtx = &lumCtx[1];

        desc[0].process = c->yuv2packedX ? packed_vscale : any_vscale;
        desc[0].instance = lumCtx;
        desc[0].src = src;
        desc[0].dst = dst;
        desc[0].alpha = c->needAlpha;
    }

    ff_init_vscale_pfn(c, c->yuv2plane1, c->yuv2planeX, c->yuv2nv12cX,
        c->yuv2packed1, c->yuv2packed2, c->yuv2packedX, c->yuv2anyX, c->use_mmx_vfilter);
    return 0;
}

void ff_init_vscale_pfn(SwsContext *c,
    yuv2planar1_fn yuv2plane1,
    yuv2planarX_fn yuv2planeX,
    yuv2interleavedX_fn yuv2nv12cX,
    yuv2packed1_fn yuv2packed1,
    yuv2packed2_fn yuv2packed2,
    yuv2packedX_fn yuv2packedX,
    yuv2anyX_fn yuv2anyX, int use_mmx)
{
    VScalerContext *lumCtx = NULL;
    VScalerContext *chrCtx = NULL;
    int idx = c->numDesc - (c->is_internal_gamma ? 2 : 1); //FIXME avoid hardcoding indexes

    if (isPlanarYUV(c->dstFormat) || (isGray(c->dstFormat) && !isALPHA(c->dstFormat))) {
        if (!isGray(c->dstFormat)) {
            chrCtx = c->desc[idx].instance;

            chrCtx->filter[0] = use_mmx ? (int16_t*)c->chrMmxFilter : c->vChrFilter;
            chrCtx->filter_size = c->vChrFilterSize;
            chrCtx->filter_pos = c->vChrFilterPos;
            chrCtx->isMMX = use_mmx;

            --idx;
            if (yuv2nv12cX)             chrCtx->pfn.yuv2interleavedX = yuv2nv12cX;
            else if (c->vChrFilterSize == 1) chrCtx->pfn.yuv2planar1 = yuv2plane1;
            else                             chrCtx->pfn.yuv2planarX = yuv2planeX;
        }

        lumCtx = c->desc[idx].instance;

        lumCtx->filter[0] = use_mmx ? (int16_t*)c->lumMmxFilter : c->vLumFilter;
        lumCtx->filter[1] = use_mmx ? (int16_t*)c->alpMmxFilter : c->vLumFilter;
        lumCtx->filter_size = c->vLumFilterSize;
        lumCtx->filter_pos = c->vLumFilterPos;
        lumCtx->isMMX = use_mmx;

        if (c->vLumFilterSize == 1) lumCtx->pfn.yuv2planar1 = yuv2plane1;
        else                        lumCtx->pfn.yuv2planarX = yuv2planeX;

    } else {
        lumCtx = c->desc[idx].instance;
        chrCtx = &lumCtx[1];

        lumCtx->filter[0] = c->vLumFilter;
        lumCtx->filter_size = c->vLumFilterSize;
        lumCtx->filter_pos = c->vLumFilterPos;

        chrCtx->filter[0] = c->vChrFilter;
        chrCtx->filter_size = c->vChrFilterSize;
        chrCtx->filter_pos = c->vChrFilterPos;

        lumCtx->isMMX = use_mmx;
        chrCtx->isMMX = use_mmx;

        if (yuv2packedX) {
            if (c->yuv2packed1 && c->vLumFilterSize == 1 && c->vChrFilterSize <= 2)
                lumCtx->pfn.yuv2packed1 = yuv2packed1;
            else if (c->yuv2packed2 && c->vLumFilterSize == 2 && c->vChrFilterSize == 2)
                lumCtx->pfn.yuv2packed2 = yuv2packed2;
            lumCtx->yuv2packedX = yuv2packedX;
        } else
            lumCtx->pfn.yuv2anyX = yuv2anyX;
    }
}


