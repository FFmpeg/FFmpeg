/*
 * HEVC/VVC SAO template
 *
 * Copyright (C) 2024 Nuo Mi
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

static void FUNC(sao_band_filter)(uint8_t *_dst, const uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  const int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height)
{
    pixel *dst = (pixel *)_dst;
    const pixel *src = (const pixel *)_src;
    int offset_table[32] = { 0 };
    int k, y, x;
    int shift  = BIT_DEPTH - 5;

    stride_dst /= sizeof(pixel);
    stride_src /= sizeof(pixel);

    for (k = 0; k < 4; k++)
        offset_table[(k + sao_left_class) & 31] = sao_offset_val[k + 1];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(src[x] + offset_table[(src[x] >> shift) & 31]);
        dst += stride_dst;
        src += stride_src;
    }
}

#define CMP(a, b) (((a) > (b)) - ((a) < (b)))

static void FUNC(sao_edge_filter)(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst, const int16_t *sao_offset_val,
                                  int eo, int width, int height) {

    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };
    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    pixel *dst = (pixel *)_dst;
    const pixel *src = (const pixel *)_src;
    int a_stride, b_stride;
    int x, y;
    ptrdiff_t stride_src = (2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE) / sizeof(pixel);
    stride_dst /= sizeof(pixel);

    a_stride = pos[eo][0][0] + pos[eo][0][1] * stride_src;
    b_stride = pos[eo][1][0] + pos[eo][1][1] * stride_src;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int diff0 = CMP(src[x], src[x + a_stride]);
            int diff1 = CMP(src[x], src[x + b_stride]);
            int offset_val        = edge_idx[2 + diff0 + diff1];
            dst[x] = av_clip_pixel(src[x] + sao_offset_val[offset_val]);
        }
        src += stride_src;
        dst += stride_dst;
    }
}

static void FUNC(sao_edge_restore_0)(uint8_t *_dst, const uint8_t *_src,
                                    ptrdiff_t stride_dst, ptrdiff_t stride_src, const SAOParams *sao,
                                    const int *borders, int _width, int _height,
                                    int c_idx, const uint8_t *vert_edge,
                                    const uint8_t *horiz_edge, const uint8_t *diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    const pixel *src = (const pixel *)_src;
    const int16_t *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, width = _width, height = _height;

    stride_dst /= sizeof(pixel);
    stride_src /= sizeof(pixel);

    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            for (y = 0; y < height; y++) {
                dst[y * stride_dst] = av_clip_pixel(src[y * stride_src] + offset_val);
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int offset     = width - 1;
            for (x = 0; x < height; x++) {
                dst[x * stride_dst + offset] = av_clip_pixel(src[x * stride_src + offset] + offset_val);
            }
            width--;
        }
    }
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
        }
        if (borders[3]) {
            int offset_val   = sao_offset_val[0];
            ptrdiff_t y_stride_dst = stride_dst * (height - 1);
            ptrdiff_t y_stride_src = stride_src * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_stride_dst] = av_clip_pixel(src[x + y_stride_src] + offset_val);
            height--;
        }
    }
}

static void FUNC(sao_edge_restore_1)(uint8_t *_dst, const uint8_t *_src,
                                    ptrdiff_t stride_dst, ptrdiff_t stride_src, const SAOParams *sao,
                                    const int *borders, int _width, int _height,
                                    int c_idx, const uint8_t *vert_edge,
                                    const uint8_t *horiz_edge, const uint8_t *diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    const pixel *src = (const pixel *)_src;
    const int16_t *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    stride_dst /= sizeof(pixel);
    stride_src /= sizeof(pixel);

    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            for (y = 0; y < height; y++) {
                dst[y * stride_dst] = av_clip_pixel(src[y * stride_src] + offset_val);
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int offset     = width - 1;
            for (x = 0; x < height; x++) {
                dst[x * stride_dst + offset] = av_clip_pixel(src[x * stride_src + offset] + offset_val);
            }
            width--;
        }
    }
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
            init_y = 1;
        }
        if (borders[3]) {
            int offset_val   = sao_offset_val[0];
            ptrdiff_t y_stride_dst = stride_dst * (height - 1);
            ptrdiff_t y_stride_src = stride_src * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_stride_dst] = av_clip_pixel(src[x + y_stride_src] + offset_val);
            height--;
        }
    }

    {
        int save_upper_left  = !diag_edge[0] && sao_eo_class == SAO_EO_135D && !borders[0] && !borders[1];
        int save_upper_right = !diag_edge[1] && sao_eo_class == SAO_EO_45D  && !borders[1] && !borders[2];
        int save_lower_right = !diag_edge[2] && sao_eo_class == SAO_EO_135D && !borders[2] && !borders[3];
        int save_lower_left  = !diag_edge[3] && sao_eo_class == SAO_EO_45D  && !borders[0] && !borders[3];

        // Restore pixels that can't be modified
        if(vert_edge[0] && sao_eo_class != SAO_EO_VERT) {
            for(y = init_y+save_upper_left; y< height-save_lower_left; y++)
                dst[y*stride_dst] = src[y*stride_src];
        }
        if(vert_edge[1] && sao_eo_class != SAO_EO_VERT) {
            for(y = init_y+save_upper_right; y< height-save_lower_right; y++)
                dst[y*stride_dst+width-1] = src[y*stride_src+width-1];
        }

        if(horiz_edge[0] && sao_eo_class != SAO_EO_HORIZ) {
            for(x = init_x+save_upper_left; x < width-save_upper_right; x++)
                dst[x] = src[x];
        }
        if(horiz_edge[1] && sao_eo_class != SAO_EO_HORIZ) {
            for(x = init_x+save_lower_left; x < width-save_lower_right; x++)
                dst[(height-1)*stride_dst+x] = src[(height-1)*stride_src+x];
        }
        if(diag_edge[0] && sao_eo_class == SAO_EO_135D)
            dst[0] = src[0];
        if(diag_edge[1] && sao_eo_class == SAO_EO_45D)
            dst[width-1] = src[width-1];
        if(diag_edge[2] && sao_eo_class == SAO_EO_135D)
            dst[stride_dst*(height-1)+width-1] = src[stride_src*(height-1)+width-1];
        if(diag_edge[3] && sao_eo_class == SAO_EO_45D)
            dst[stride_dst*(height-1)] = src[stride_src*(height-1)];

    }
}

#undef CMP
