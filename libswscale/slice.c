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

#include "libavutil/mem.h"
#include "swscale_internal.h"

static void free_lines(SwsSlice *s)
{
    int i;
    for (i = 0; i < 2; ++i) {
        int n = s->plane[i].available_lines;
        int j;
        for (j = 0; j < n; ++j) {
            av_freep(&s->plane[i].line[j]);
            if (s->is_ring)
               s->plane[i].line[j+n] = NULL;
        }
    }

    for (i = 0; i < 4; ++i)
        memset(s->plane[i].line, 0, sizeof(uint8_t*) * s->plane[i].available_lines * (s->is_ring ? 3 : 1));
    s->should_free_lines = 0;
}

/*
 slice lines contains extra bytes for vectorial code thus @size
 is the allocated memory size and @width is the number of pixels
*/
static int alloc_lines(SwsSlice *s, int size, int width)
{
    int i;
    int idx[2] = {3, 2};

    s->should_free_lines = 1;
    s->width = width;

    for (i = 0; i < 2; ++i) {
        int n = s->plane[i].available_lines;
        int j;
        int ii = idx[i];

        av_assert0(n == s->plane[ii].available_lines);
        for (j = 0; j < n; ++j) {
            // chroma plane line U and V are expected to be contiguous in memory
            // by mmx vertical scaler code
            s->plane[i].line[j] = av_malloc(size * 2 + 32);
            if (!s->plane[i].line[j]) {
                free_lines(s);
                return AVERROR(ENOMEM);
            }
            s->plane[ii].line[j] = s->plane[i].line[j] + size + 16;
            if (s->is_ring) {
               s->plane[i].line[j+n] = s->plane[i].line[j];
               s->plane[ii].line[j+n] = s->plane[ii].line[j];
            }
        }
    }

    return 0;
}

static int alloc_slice(SwsSlice *s, enum AVPixelFormat fmt, int lumLines, int chrLines, int h_sub_sample, int v_sub_sample, int ring)
{
    int i;
    int size[4] = { lumLines,
                    chrLines,
                    chrLines,
                    lumLines };

    s->h_chr_sub_sample = h_sub_sample;
    s->v_chr_sub_sample = v_sub_sample;
    s->fmt = fmt;
    s->is_ring = ring;
    s->should_free_lines = 0;

    for (i = 0; i < 4; ++i) {
        int n = size[i] * ( ring == 0 ? 1 : 3);
        s->plane[i].line = av_calloc(n, sizeof(*s->plane[i].line));
        if (!s->plane[i].line)
            return AVERROR(ENOMEM);

        s->plane[i].tmp = ring ? s->plane[i].line + size[i] * 2 : NULL;
        s->plane[i].available_lines = size[i];
        s->plane[i].sliceY = 0;
        s->plane[i].sliceH = 0;
    }
    return 0;
}

static void free_slice(SwsSlice *s)
{
    int i;
    if (s) {
        if (s->should_free_lines)
            free_lines(s);
        for (i = 0; i < 4; ++i) {
            av_freep(&s->plane[i].line);
            s->plane[i].tmp = NULL;
        }
    }
}

int ff_rotate_slice(SwsSlice *s, int lum, int chr)
{
    int i;
    if (lum) {
        for (i = 0; i < 4; i+=3) {
            int n = s->plane[i].available_lines;
            int l = lum - s->plane[i].sliceY;

            if (l >= n * 2) {
                s->plane[i].sliceY += n;
                s->plane[i].sliceH -= n;
            }
        }
    }
    if (chr) {
        for (i = 1; i < 3; ++i) {
            int n = s->plane[i].available_lines;
            int l = chr - s->plane[i].sliceY;

            if (l >= n * 2) {
                s->plane[i].sliceY += n;
                s->plane[i].sliceH -= n;
            }
        }
    }
    return 0;
}

int ff_init_slice_from_src(SwsSlice * s, uint8_t *const src[4], const int stride[4],
                           int srcW, int lumY, int lumH, int chrY, int chrH, int relative)
{
    int i = 0;

    const int start[4] = {lumY,
                          chrY,
                          chrY,
                          lumY};

    const int end[4] = {lumY +lumH,
                        chrY + chrH,
                        chrY + chrH,
                        lumY + lumH};

    s->width = srcW;

    for (i = 0; i < 4 && src[i] != NULL; ++i) {
        uint8_t *const src_i = src[i] + (relative ? 0 : start[i]) * stride[i];
        int j;
        int first = s->plane[i].sliceY;
        int n = s->plane[i].available_lines;
        int lines = end[i] - start[i];
        int tot_lines = end[i] - first;

        if (start[i] >= first && n >= tot_lines) {
            s->plane[i].sliceH = FFMAX(tot_lines, s->plane[i].sliceH);
            for (j = 0; j < lines; j+= 1)
                s->plane[i].line[start[i] - first + j] = src_i +  j * stride[i];
        } else {
            s->plane[i].sliceY = start[i];
            lines = lines > n ? n : lines;
            s->plane[i].sliceH = lines;
            for (j = 0; j < lines; j+= 1)
                s->plane[i].line[j] = src_i +  j * stride[i];
        }

    }

    return 0;
}

static void fill_ones(SwsSlice *s, int n, int bpc)
{
    int i, j, k, size, end;

    for (i = 0; i < 4; ++i) {
        size = s->plane[i].available_lines;
        for (j = 0; j < size; ++j) {
            if (bpc == 16) {
                end = (n>>1) + 1;
                for (k = 0; k < end; ++k)
                    ((int32_t*)(s->plane[i].line[j]))[k] = 1<<18;
            } else if (bpc == 32) {
                end = (n>>2) + 1;
                for (k = 0; k < end; ++k)
                    ((int64_t*)(s->plane[i].line[j]))[k] = 1LL<<34;
            } else {
                end = n + 1;
                for (k = 0; k < end; ++k)
                    ((int16_t*)(s->plane[i].line[j]))[k] = 1<<14;
            }
        }
    }
}

/*
 Calculates the minimum ring buffer size, it should be able to store vFilterSize
 more n lines where n is the max difference between each adjacent slice which
 outputs a line.
 The n lines are needed only when there is not enough src lines to output a single
 dst line, then we should buffer these lines to process them on the next call to scale.
*/
static void get_min_buffer_size(SwsInternal *c, int *out_lum_size, int *out_chr_size)
{
    int lumY;
    int dstH = c->opts.dst_h;
    int chrDstH = c->chrDstH;
    int *lumFilterPos = c->vLumFilterPos;
    int *chrFilterPos = c->vChrFilterPos;
    int lumFilterSize = c->vLumFilterSize;
    int chrFilterSize = c->vChrFilterSize;
    int chrSubSample = c->chrSrcVSubSample;

    *out_lum_size = lumFilterSize;
    *out_chr_size = chrFilterSize;

    for (lumY = 0; lumY < dstH; lumY++) {
        int chrY      = (int64_t)lumY * chrDstH / dstH;
        int nextSlice = FFMAX(lumFilterPos[lumY] + lumFilterSize - 1,
                              ((chrFilterPos[chrY] + chrFilterSize - 1)
                               << chrSubSample));

        nextSlice >>= chrSubSample;
        nextSlice <<= chrSubSample;
        (*out_lum_size) = FFMAX((*out_lum_size), nextSlice - lumFilterPos[lumY]);
        (*out_chr_size) = FFMAX((*out_chr_size), (nextSlice >> chrSubSample) - chrFilterPos[chrY]);
    }
}



int ff_init_filters(SwsInternal * c)
{
    int i;
    int index;
    int num_ydesc;
    int num_cdesc;
    int num_vdesc = isPlanarYUV(c->opts.dst_format) && !isGray(c->opts.dst_format) ? 2 : 1;
    int need_lum_conv = c->lumToYV12 || c->readLumPlanar || c->alpToYV12 || c->readAlpPlanar;
    int need_chr_conv = c->chrToYV12 || c->readChrPlanar;
    int need_gamma = c->is_internal_gamma;
    int srcIdx, dstIdx;
    int dst_stride = FFALIGN(c->opts.dst_w * sizeof(int16_t) + 66, 16);

    uint32_t * pal = usePal(c->opts.src_format) ? c->pal_yuv : (uint32_t*)c->input_rgb2yuv_table;
    int res = 0;

    int lumBufSize;
    int chrBufSize;

    get_min_buffer_size(c, &lumBufSize, &chrBufSize);
    lumBufSize = FFMAX(lumBufSize, c->vLumFilterSize + MAX_LINES_AHEAD);
    chrBufSize = FFMAX(chrBufSize, c->vChrFilterSize + MAX_LINES_AHEAD);

    if (c->dstBpc == 16)
        dst_stride <<= 1;

    if (c->dstBpc == 32)
        dst_stride <<= 2;

    num_ydesc = need_lum_conv ? 2 : 1;
    num_cdesc = need_chr_conv ? 2 : 1;

    c->numSlice = FFMAX(num_ydesc, num_cdesc) + 2;
    c->numDesc = num_ydesc + num_cdesc + num_vdesc + (need_gamma ? 2 : 0);
    c->descIndex[0] = num_ydesc + (need_gamma ? 1 : 0);
    c->descIndex[1] = num_ydesc + num_cdesc + (need_gamma ? 1 : 0);

    if (isFloat16(c->opts.src_format)) {
        c->h2f_tables = av_malloc(sizeof(*c->h2f_tables));
        if (!c->h2f_tables)
            return AVERROR(ENOMEM);
        ff_init_half2float_tables(c->h2f_tables);
        c->input_opaque = c->h2f_tables;
    }

    c->desc  = av_calloc(c->numDesc,  sizeof(*c->desc));
    if (!c->desc)
        return AVERROR(ENOMEM);
    c->slice = av_calloc(c->numSlice, sizeof(*c->slice));
    if (!c->slice) {
        res = AVERROR(ENOMEM);
        goto cleanup;
    }

    res = alloc_slice(&c->slice[0], c->opts.src_format, c->opts.src_h, c->chrSrcH, c->chrSrcHSubSample, c->chrSrcVSubSample, 0);
    if (res < 0) goto cleanup;
    for (i = 1; i < c->numSlice-2; ++i) {
        res = alloc_slice(&c->slice[i], c->opts.src_format, lumBufSize, chrBufSize, c->chrSrcHSubSample, c->chrSrcVSubSample, 0);
        if (res < 0) goto cleanup;
        res = alloc_lines(&c->slice[i], FFALIGN(c->opts.src_w*2+78, 16), c->opts.src_w);
        if (res < 0) goto cleanup;
    }
    // horizontal scaler output
    res = alloc_slice(&c->slice[i], c->opts.src_format, lumBufSize, chrBufSize, c->chrDstHSubSample, c->chrDstVSubSample, 1);
    if (res < 0) goto cleanup;
    res = alloc_lines(&c->slice[i], dst_stride, c->opts.dst_w);
    if (res < 0) goto cleanup;

    fill_ones(&c->slice[i], dst_stride>>1, c->dstBpc);

    // vertical scaler output
    ++i;
    res = alloc_slice(&c->slice[i], c->opts.dst_format, c->opts.dst_h, c->chrDstH, c->chrDstHSubSample, c->chrDstVSubSample, 0);
    if (res < 0) goto cleanup;

    index = 0;
    srcIdx = 0;
    dstIdx = 1;

    if (need_gamma) {
        res = ff_init_gamma_convert(c->desc + index, c->slice + srcIdx, c->inv_gamma);
        if (res < 0) goto cleanup;
        ++index;
    }

    if (need_lum_conv) {
        res = ff_init_desc_fmt_convert(&c->desc[index], &c->slice[srcIdx], &c->slice[dstIdx], pal);
        if (res < 0) goto cleanup;
        c->desc[index].alpha = c->needAlpha;
        ++index;
        srcIdx = dstIdx;
    }


    dstIdx = FFMAX(num_ydesc, num_cdesc);
    res = ff_init_desc_hscale(&c->desc[index], &c->slice[srcIdx], &c->slice[dstIdx], c->hLumFilter, c->hLumFilterPos, c->hLumFilterSize, c->lumXInc);
    if (res < 0) goto cleanup;
    c->desc[index].alpha = c->needAlpha;


    ++index;
    {
        srcIdx = 0;
        dstIdx = 1;
        if (need_chr_conv) {
            res = ff_init_desc_cfmt_convert(&c->desc[index], &c->slice[srcIdx], &c->slice[dstIdx], pal);
            if (res < 0) goto cleanup;
            ++index;
            srcIdx = dstIdx;
        }

        dstIdx = FFMAX(num_ydesc, num_cdesc);
        if (c->needs_hcscale)
            res = ff_init_desc_chscale(&c->desc[index], &c->slice[srcIdx], &c->slice[dstIdx], c->hChrFilter, c->hChrFilterPos, c->hChrFilterSize, c->chrXInc);
        else
            res = ff_init_desc_no_chr(&c->desc[index], &c->slice[srcIdx], &c->slice[dstIdx]);
        if (res < 0) goto cleanup;
    }

    ++index;
    {
        srcIdx = c->numSlice - 2;
        dstIdx = c->numSlice - 1;
        res = ff_init_vscale(c, c->desc + index, c->slice + srcIdx, c->slice + dstIdx);
        if (res < 0) goto cleanup;
    }

    ++index;
    if (need_gamma) {
        res = ff_init_gamma_convert(c->desc + index, c->slice + dstIdx, c->gamma);
        if (res < 0) goto cleanup;
    }

    return 0;

cleanup:
    ff_free_filters(c);
    return res;
}

int ff_free_filters(SwsInternal *c)
{
    int i;
    if (c->desc) {
        for (i = 0; i < c->numDesc; ++i)
            av_freep(&c->desc[i].instance);
        av_freep(&c->desc);
    }

    if (c->slice) {
        for (i = 0; i < c->numSlice; ++i)
            free_slice(&c->slice[i]);
        av_freep(&c->slice);
    }
    av_freep(&c->h2f_tables);
    return 0;
}
