/*
 * QuickTime RPZA Video Encoder
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

/**
 * @file rpzaenc.c
 * QT RPZA Video Encoder by Todd Kirby <doubleshot@pacbell.net> and David Adler
 */

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"

typedef struct RpzaContext {
    AVClass *avclass;

    int skip_frame_thresh;
    int start_one_color_thresh;
    int continue_one_color_thresh;
    int sixteen_color_thresh;

    AVFrame *prev_frame;    // buffer for previous source frame
    PutBitContext pb;       // buffer for encoded frame data.

    int frame_width;        // width in pixels of source frame
    int frame_height;       // height in pixesl of source frame

    int first_frame;        // flag set to one when the first frame is being processed
                            // so that comparisons with previous frame data in not attempted
} RpzaContext;

typedef enum channel_offset {
    RED = 2,
    GREEN = 1,
    BLUE = 0,
} channel_offset;

typedef struct rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb;

#define SQR(x) ((x) * (x))

/* 15 bit components */
#define GET_CHAN(color, chan) (((color) >> ((chan) * 5) & 0x1F) * 8)
#define R(color) GET_CHAN(color, RED)
#define G(color) GET_CHAN(color, GREEN)
#define B(color) GET_CHAN(color, BLUE)

typedef struct BlockInfo {
    int row;
    int col;
    int block_width;
    int block_height;
    int image_width;
    int image_height;
    int block_index;
    uint16_t start;
    int rowstride;
    int blocks_per_row;
    int total_blocks;
} BlockInfo;

static void get_colors(uint8_t *min, uint8_t *max, uint8_t color4[4][3])
{
    uint8_t step;

    color4[0][0] = min[0];
    color4[0][1] = min[1];
    color4[0][2] = min[2];

    color4[3][0] = max[0];
    color4[3][1] = max[1];
    color4[3][2] = max[2];

    // red components
    step = (color4[3][0] - color4[0][0] + 1) / 3;
    color4[1][0] = color4[0][0] + step;
    color4[2][0] = color4[3][0] - step;

    // green components
    step = (color4[3][1] - color4[0][1] + 1) / 3;
    color4[1][1] = color4[0][1] + step;
    color4[2][1] = color4[3][1] - step;

    // blue components
    step = (color4[3][2] - color4[0][2] + 1) / 3;
    color4[1][2] = color4[0][2] + step;
    color4[2][2] = color4[3][2] - step;
}

/* Fill BlockInfo struct with information about a 4x4 block of the image */
static int get_block_info(BlockInfo *bi, int block)
{
    bi->row = block / bi->blocks_per_row;
    bi->col = block % bi->blocks_per_row;

    // test for right edge block
    if (bi->col == bi->blocks_per_row - 1 && (bi->image_width % 4) != 0) {
        bi->block_width = bi->image_width % 4;
    } else {
        bi->block_width = 4;
    }

    // test for bottom edge block
    if (bi->row == (bi->image_height / 4) && (bi->image_height % 4) != 0) {
        bi->block_height = bi->image_height % 4;
    } else {
        bi->block_height = 4;
    }

    return block ? (bi->col * 4) + (bi->row * bi->rowstride * 4) : 0;
}

static uint16_t rgb24_to_rgb555(uint8_t *rgb24)
{
    uint16_t rgb555 = 0;
    uint32_t r, g, b;

    r = rgb24[0] >> 3;
    g = rgb24[1] >> 3;
    b = rgb24[2] >> 3;

    rgb555 |= (r << 10);
    rgb555 |= (g << 5);
    rgb555 |= (b << 0);

    return rgb555;
}

/*
 * Returns the total difference between two 24 bit color values
 */
static int diff_colors(uint8_t *colorA, uint8_t *colorB)
{
    int tot;

    tot  = SQR(colorA[0] - colorB[0]);
    tot += SQR(colorA[1] - colorB[1]);
    tot += SQR(colorA[2] - colorB[2]);

    return tot;
}

/*
 * Returns the maximum channel difference
 */
static int max_component_diff(uint16_t *colorA, uint16_t *colorB)
{
    int diff, max = 0;

    diff = FFABS(R(colorA[0]) - R(colorB[0]));
    if (diff > max) {
        max = diff;
    }
    diff = FFABS(G(colorA[0]) - G(colorB[0]));
    if (diff > max) {
        max = diff;
    }
    diff = FFABS(B(colorA[0]) - B(colorB[0]));
    if (diff > max) {
        max = diff;
    }
    return max * 8;
}

/*
 * Find the channel that has the largest difference between minimum and maximum
 * color values. Put the minimum value in min, maximum in max and the channel
 * in chan.
 */
static void get_max_component_diff(BlockInfo *bi, uint16_t *block_ptr,
                                   uint8_t *min, uint8_t *max, channel_offset *chan)
{
    int x, y;
    uint8_t min_r, max_r, min_g, max_g, min_b, max_b;
    uint8_t r, g, b;

    // fix warning about uninitialized vars
    min_r = min_g = min_b = UINT8_MAX;
    max_r = max_g = max_b = 0;

    // loop thru and compare pixels
    for (y = 0; y < bi->block_height; y++) {
        for (x = 0; x < bi->block_width; x++){
            // TODO:  optimize
            min_r = FFMIN(R(block_ptr[x]), min_r);
            min_g = FFMIN(G(block_ptr[x]), min_g);
            min_b = FFMIN(B(block_ptr[x]), min_b);

            max_r = FFMAX(R(block_ptr[x]), max_r);
            max_g = FFMAX(G(block_ptr[x]), max_g);
            max_b = FFMAX(B(block_ptr[x]), max_b);
        }
        block_ptr += bi->rowstride;
    }

    r = max_r - min_r;
    g = max_g - min_g;
    b = max_b - min_b;

    if (r > g && r > b) {
        *max = max_r;
        *min = min_r;
        *chan = RED;
    } else if (g > b && g >= r) {
        *max = max_g;
        *min = min_g;
        *chan = GREEN;
    } else {
        *max = max_b;
        *min = min_b;
        *chan = BLUE;
    }
}

/*
 * Compare two 4x4 blocks to determine if the total difference between the
 * blocks is greater than the thresh parameter. Returns -1 if difference
 * exceeds threshold or zero otherwise.
 */
static int compare_blocks(uint16_t *block1, uint16_t *block2, BlockInfo *bi, int thresh)
{
    int x, y, diff = 0;
    for (y = 0; y < bi->block_height; y++) {
        for (x = 0; x < bi->block_width; x++) {
            diff = max_component_diff(&block1[x], &block2[x]);
            if (diff >= thresh) {
                return -1;
            }
        }
        block1 += bi->rowstride;
        block2 += bi->rowstride;
    }
    return 0;
}

/*
 * Determine the fit of one channel to another within a 4x4 block. This
 * is used to determine the best palette choices for 4-color encoding.
 */
static int leastsquares(uint16_t *block_ptr, BlockInfo *bi,
                        channel_offset xchannel, channel_offset ychannel,
                        double *slope, double *y_intercept, double *correlation_coef)
{
    double sumx = 0, sumy = 0, sumx2 = 0, sumy2 = 0, sumxy = 0,
           sumx_sq = 0, sumy_sq = 0, tmp, tmp2;
    int i, j, count;
    uint8_t x, y;

    count = bi->block_height * bi->block_width;

    if (count < 2)
        return -1;

    for (i = 0; i < bi->block_height; i++) {
        for (j = 0; j < bi->block_width; j++){
            x = GET_CHAN(block_ptr[j], xchannel);
            y = GET_CHAN(block_ptr[j], ychannel);
            sumx += x;
            sumy += y;
            sumx2 += x * x;
            sumy2 += y * y;
            sumxy += x * y;
        }
        block_ptr += bi->rowstride;
    }

    sumx_sq = sumx * sumx;
    tmp = (count * sumx2 - sumx_sq);

    // guard against div/0
    if (tmp == 0)
        return -2;

    sumy_sq = sumy * sumy;

    *slope = (sumx * sumy - sumxy) / tmp;
    *y_intercept = (sumy - (*slope) * sumx) / count;

    tmp2 = count * sumy2 - sumy_sq;
    if (tmp2 == 0) {
        *correlation_coef = 0.0;
    } else {
        *correlation_coef = (count * sumxy - sumx * sumy) /
            sqrt(tmp * tmp2);
    }

    return 0; // success
}

/*
 * Determine the amount of error in the leastsquares fit.
 */
static int calc_lsq_max_fit_error(uint16_t *block_ptr, BlockInfo *bi,
                                  int min, int max, int tmp_min, int tmp_max,
                                  channel_offset xchannel, channel_offset ychannel)
{
    int i, j, x, y;
    int err;
    int max_err = 0;

    for (i = 0; i < bi->block_height; i++) {
        for (j = 0; j < bi->block_width; j++){
            int x_inc, lin_y, lin_x;
            x = GET_CHAN(block_ptr[j], xchannel);
            y = GET_CHAN(block_ptr[j], ychannel);

            /* calculate x_inc as the 4-color index (0..3) */
            x_inc = floor( (x - min) * 3.0 / (max - min) + 0.5);
            x_inc = FFMAX(FFMIN(3, x_inc), 0);

            /* calculate lin_y corresponding to x_inc */
            lin_y = (int)(tmp_min + (tmp_max - tmp_min) * x_inc / 3.0 + 0.5);

            err = FFABS(lin_y - y);
            if (err > max_err)
                max_err = err;

            /* calculate lin_x corresponding to x_inc */
            lin_x = (int)(min + (max - min) * x_inc / 3.0 + 0.5);

            err = FFABS(lin_x - x);
            if (err > max_err)
                max_err += err;
        }
        block_ptr += bi->rowstride;
    }

    return max_err;
}

/*
 * Find the closest match to a color within the 4-color palette
 */
static int match_color(uint16_t *color, uint8_t colors[4][3])
{
    int ret = 0;
    int smallest_variance = INT_MAX;
    uint8_t dithered_color[3];

    for (int channel = 0; channel < 3; channel++) {
        dithered_color[channel] = GET_CHAN(color[0], channel);
    }

    for (int palette_entry = 0; palette_entry < 4; palette_entry++) {
        int variance = diff_colors(dithered_color, colors[palette_entry]);

        if (variance < smallest_variance) {
            smallest_variance = variance;
            ret = palette_entry;
        }
    }

    return ret;
}

/*
 * Encode a block using the 4-color opcode and palette. return number of
 * blocks encoded (until we implement multi-block 4 color runs this will
 * always be 1)
 */
static int encode_four_color_block(uint8_t *min_color, uint8_t *max_color,
                                   PutBitContext *pb, uint16_t *block_ptr, BlockInfo *bi)
{
    int x, y, idx;
    uint8_t color4[4][3];
    uint16_t rounded_max, rounded_min;

    // round min and max wider
    rounded_min = rgb24_to_rgb555(min_color);
    rounded_max = rgb24_to_rgb555(max_color);

    // put a and b colors
    // encode 4 colors = first 16 bit color with MSB zeroed and...
    put_bits(pb, 16, rounded_max & ~0x8000);
    // ...second 16 bit color with MSB on.
    put_bits(pb, 16, rounded_min | 0x8000);

    get_colors(min_color, max_color, color4);

    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            idx = match_color(&block_ptr[x], color4);
            put_bits(pb, 2, idx);
        }
        block_ptr += bi->rowstride;
    }
    return 1; // num blocks encoded
}

/*
 * Copy a 4x4 block from the current frame buffer to the previous frame buffer.
 */
static void update_block_in_prev_frame(const uint16_t *src_pixels,
                                       uint16_t *dest_pixels,
                                       const BlockInfo *bi, int block_counter)
{
    for (int y = 0; y < 4; y++) {
        memcpy(dest_pixels, src_pixels, 8);
        dest_pixels += bi->rowstride;
        src_pixels += bi->rowstride;
    }
}

/*
 * update statistics for the specified block. If first_block,
 * it initializes the statistics.  Otherwise it updates the statistics IF THIS
 * BLOCK IS SUITABLE TO CONTINUE A 1-COLOR RUN. That is, it checks whether
 * the range of colors (since the routine was called first_block != 0) are
 * all close enough intensities to be represented by a single color.

 * The routine returns 0 if this block is too different to be part of
 * the same run of 1-color blocks. The routine returns 1 if this
 * block can be part of the same 1-color block run.

 * If the routine returns 1, it also updates its arguments to include
 * the statistics of this block. Otherwise, the stats are unchanged
 * and don't include the current block.
 */
static int update_block_stats(RpzaContext *s, BlockInfo *bi, uint16_t *block,
                              uint8_t min_color[3], uint8_t max_color[3],
                              int *total_rgb, int *total_pixels,
                              uint8_t avg_color[3], int first_block)
{
    int x, y;
    int is_in_range;
    int total_pixels_blk;
    int threshold;

    uint8_t min_color_blk[3], max_color_blk[3];
    int total_rgb_blk[3];
    uint8_t avg_color_blk[3];

    if (first_block) {
        min_color[0] = UINT8_MAX;
        min_color[1] = UINT8_MAX;
        min_color[2] = UINT8_MAX;
        max_color[0] = 0;
        max_color[1] = 0;
        max_color[2] = 0;
        total_rgb[0] = 0;
        total_rgb[1] = 0;
        total_rgb[2] = 0;
        *total_pixels = 0;
        threshold = s->start_one_color_thresh;
    } else {
        threshold = s->continue_one_color_thresh;
    }

    /*
       The *_blk variables will include the current block.
       Initialize them based on the blocks so far.
     */
    min_color_blk[0] = min_color[0];
    min_color_blk[1] = min_color[1];
    min_color_blk[2] = min_color[2];
    max_color_blk[0] = max_color[0];
    max_color_blk[1] = max_color[1];
    max_color_blk[2] = max_color[2];
    total_rgb_blk[0] = total_rgb[0];
    total_rgb_blk[1] = total_rgb[1];
    total_rgb_blk[2] = total_rgb[2];
    total_pixels_blk = *total_pixels + bi->block_height * bi->block_width;

    /*
       Update stats for this block's pixels
     */
    for (y = 0; y < bi->block_height; y++) {
        for (x = 0; x < bi->block_width; x++) {
            total_rgb_blk[0] += R(block[x]);
            total_rgb_blk[1] += G(block[x]);
            total_rgb_blk[2] += B(block[x]);

            min_color_blk[0] = FFMIN(R(block[x]), min_color_blk[0]);
            min_color_blk[1] = FFMIN(G(block[x]), min_color_blk[1]);
            min_color_blk[2] = FFMIN(B(block[x]), min_color_blk[2]);

            max_color_blk[0] = FFMAX(R(block[x]), max_color_blk[0]);
            max_color_blk[1] = FFMAX(G(block[x]), max_color_blk[1]);
            max_color_blk[2] = FFMAX(B(block[x]), max_color_blk[2]);
        }
        block += bi->rowstride;
    }

    /*
       Calculate average color including current block.
     */
    avg_color_blk[0] = total_rgb_blk[0] / total_pixels_blk;
    avg_color_blk[1] = total_rgb_blk[1] / total_pixels_blk;
    avg_color_blk[2] = total_rgb_blk[2] / total_pixels_blk;

    /*
       Are all the pixels within threshold of the average color?
     */
    is_in_range = (max_color_blk[0] - avg_color_blk[0] <= threshold &&
                   max_color_blk[1] - avg_color_blk[1] <= threshold &&
                   max_color_blk[2] - avg_color_blk[2] <= threshold &&
                   avg_color_blk[0] - min_color_blk[0] <= threshold &&
                   avg_color_blk[1] - min_color_blk[1] <= threshold &&
                   avg_color_blk[2] - min_color_blk[2] <= threshold);

    if (is_in_range) {
        /*
           Set the output variables to include this block.
         */
        min_color[0] = min_color_blk[0];
        min_color[1] = min_color_blk[1];
        min_color[2] = min_color_blk[2];
        max_color[0] = max_color_blk[0];
        max_color[1] = max_color_blk[1];
        max_color[2] = max_color_blk[2];
        total_rgb[0] = total_rgb_blk[0];
        total_rgb[1] = total_rgb_blk[1];
        total_rgb[2] = total_rgb_blk[2];
        *total_pixels = total_pixels_blk;
        avg_color[0] = avg_color_blk[0];
        avg_color[1] = avg_color_blk[1];
        avg_color[2] = avg_color_blk[2];
    }

    return is_in_range;
}

static void rpza_encode_stream(RpzaContext *s, const AVFrame *pict)
{
    BlockInfo bi;
    int block_counter = 0;
    int n_blocks;
    int total_blocks;
    int prev_block_offset;
    int block_offset = 0;
    uint8_t min = 0, max = 0;
    channel_offset chan;
    int i;
    int tmp_min, tmp_max;
    int total_rgb[3];
    uint8_t avg_color[3];
    int pixel_count;
    uint8_t min_color[3], max_color[3];
    double slope, y_intercept, correlation_coef;
    uint16_t *src_pixels = (uint16_t *)pict->data[0];
    uint16_t *prev_pixels = (uint16_t *)s->prev_frame->data[0];

    /* Number of 4x4 blocks in frame. */
    total_blocks = ((s->frame_width + 3) / 4) * ((s->frame_height + 3) / 4);

    bi.image_width = s->frame_width;
    bi.image_height = s->frame_height;
    bi.rowstride = pict->linesize[0] / 2;

    bi.blocks_per_row = (s->frame_width + 3) / 4;

    while (block_counter < total_blocks) {
        // SKIP CHECK
        // make sure we have a valid previous frame and we're not writing
        // a key frame
        if (!s->first_frame) {
            n_blocks = 0;
            prev_block_offset = 0;

            while (n_blocks < 32 && block_counter + n_blocks < total_blocks) {

                block_offset = get_block_info(&bi, block_counter + n_blocks);

                // multi-block opcodes cannot span multiple rows.
                // If we're starting a new row, break out and write the opcode
                /* TODO: Should eventually use bi.row here to determine when a
                   row break occurs, but that is currently breaking the
                   quicktime player. This is probably due to a bug in the
                   way I'm calculating the current row.
                 */
                if (prev_block_offset && block_offset - prev_block_offset > 12) {
                    break;
                }

                prev_block_offset = block_offset;

                if (compare_blocks(&prev_pixels[block_offset],
                                   &src_pixels[block_offset], &bi, s->skip_frame_thresh) != 0) {
                    // write out skipable blocks
                    if (n_blocks) {

                        // write skip opcode
                        put_bits(&s->pb, 8, 0x80 | (n_blocks - 1));
                        block_counter += n_blocks;

                        goto post_skip;
                    }
                    break;
                }

                /*
                 * NOTE: we don't update skipped blocks in the previous frame buffer
                 * since skipped needs always to be compared against the first skipped
                 * block to avoid artifacts during gradual fade in/outs.
                 */

                // update_block_in_prev_frame(&src_pixels[block_offset],
                //   &prev_pixels[block_offset], &bi, block_counter + n_blocks);

                n_blocks++;
            }

            // we're either at the end of the frame or we've reached the maximum
            // of 32 blocks in a run. Write out the run.
            if (n_blocks) {
                // write skip opcode
                put_bits(&s->pb, 8, 0x80 | (n_blocks - 1));
                block_counter += n_blocks;

                continue;
            }

        } else {
            block_offset = get_block_info(&bi, block_counter);
        }
post_skip :

        // ONE COLOR CHECK
        if (update_block_stats(s, &bi, &src_pixels[block_offset],
                               min_color, max_color,
                               total_rgb, &pixel_count, avg_color, 1)) {
            prev_block_offset = block_offset;

            n_blocks = 1;

            /* update this block in the previous frame buffer */
            update_block_in_prev_frame(&src_pixels[block_offset],
                                       &prev_pixels[block_offset], &bi, block_counter + n_blocks);

            // check for subsequent blocks with the same color
            while (n_blocks < 32 && block_counter + n_blocks < total_blocks) {
                block_offset = get_block_info(&bi, block_counter + n_blocks);

                // multi-block opcodes cannot span multiple rows.
                // If we've hit end of a row, break out and write the opcode
                if (block_offset - prev_block_offset > 12) {
                    break;
                }

                if (!update_block_stats(s, &bi, &src_pixels[block_offset],
                                        min_color, max_color,
                                        total_rgb, &pixel_count, avg_color, 0)) {
                    break;
                }

                prev_block_offset = block_offset;

                /* update this block in the previous frame buffer */
                update_block_in_prev_frame(&src_pixels[block_offset],
                                           &prev_pixels[block_offset], &bi, block_counter + n_blocks);

                n_blocks++;
            }

            // write one color opcode.
            put_bits(&s->pb, 8, 0xa0 | (n_blocks - 1));
            // write color to encode.
            put_bits(&s->pb, 16, rgb24_to_rgb555(avg_color));
            // skip past the blocks we've just encoded.
            block_counter += n_blocks;
        } else { // FOUR COLOR CHECK
            int err = 0;

            // get max component diff for block
            get_max_component_diff(&bi, &src_pixels[block_offset], &min, &max, &chan);

            min_color[0] = 0;
            max_color[0] = 0;
            min_color[1] = 0;
            max_color[1] = 0;
            min_color[2] = 0;
            max_color[2] = 0;

            // run least squares against other two components
            for (i = 0; i < 3; i++) {
                if (i == chan) {
                    min_color[i] = min;
                    max_color[i] = max;
                    continue;
                }

                slope = y_intercept = correlation_coef = 0;

                if (leastsquares(&src_pixels[block_offset], &bi, chan, i,
                                 &slope, &y_intercept, &correlation_coef)) {
                    min_color[i] = GET_CHAN(src_pixels[block_offset], i);
                    max_color[i] = GET_CHAN(src_pixels[block_offset], i);
                } else {
                    tmp_min = (int)(0.5 + min * slope + y_intercept);
                    tmp_max = (int)(0.5 + max * slope + y_intercept);

                    av_assert0(tmp_min <= tmp_max);
                    // clamp min and max color values
                    tmp_min = av_clip_uint8(tmp_min);
                    tmp_max = av_clip_uint8(tmp_max);

                    err = FFMAX(calc_lsq_max_fit_error(&src_pixels[block_offset], &bi,
                                                       min, max, tmp_min, tmp_max, chan, i), err);

                    min_color[i] = tmp_min;
                    max_color[i] = tmp_max;
                }
            }

            if (err > s->sixteen_color_thresh) { // DO SIXTEEN COLOR BLOCK
                uint16_t *row_ptr;
                int rgb555;

                block_offset = get_block_info(&bi, block_counter);

                row_ptr = &src_pixels[block_offset];

                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++){
                        rgb555 = row_ptr[x] & ~0x8000;

                        put_bits(&s->pb, 16, rgb555);
                    }
                    row_ptr += bi.rowstride;
                }

                block_counter++;
            } else { // FOUR COLOR BLOCK
                block_counter += encode_four_color_block(min_color, max_color,
                                                         &s->pb, &src_pixels[block_offset], &bi);
            }

            /* update this block in the previous frame buffer */
            update_block_in_prev_frame(&src_pixels[block_offset],
                                       &prev_pixels[block_offset], &bi, block_counter);
        }
    }
}

static int rpza_encode_init(AVCodecContext *avctx)
{
    RpzaContext *s = avctx->priv_data;

    s->frame_width = avctx->width;
    s->frame_height = avctx->height;

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int rpza_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *frame, int *got_packet)
{
    RpzaContext *s = avctx->priv_data;
    const AVFrame *pict = frame;
    uint8_t *buf;
    int ret = ff_alloc_packet(avctx, pkt, 6LL * avctx->height * avctx->width);

    if (ret < 0)
        return ret;

    init_put_bits(&s->pb, pkt->data, pkt->size);

    // skip 4 byte header, write it later once the size of the chunk is known
    put_bits32(&s->pb, 0x00);

    if (!s->prev_frame->data[0]) {
        s->first_frame = 1;
        s->prev_frame->format = pict->format;
        s->prev_frame->width = pict->width;
        s->prev_frame->height = pict->height;
        ret = av_frame_get_buffer(s->prev_frame, 0);
        if (ret < 0)
            return ret;
    } else {
        s->first_frame = 0;
    }

    rpza_encode_stream(s, pict);

    flush_put_bits(&s->pb);

    av_shrink_packet(pkt, put_bytes_output(&s->pb));
    buf = pkt->data;

    // write header opcode
    buf[0] = 0xe1; // chunk opcode

    // write chunk length
    AV_WB24(buf + 1, pkt->size);

    *got_packet = 1;

    return 0;
}

static int rpza_encode_end(AVCodecContext *avctx)
{
    RpzaContext *s = (RpzaContext *)avctx->priv_data;

    av_frame_free(&s->prev_frame);

    return 0;
}

#define OFFSET(x) offsetof(RpzaContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "skip_frame_thresh", NULL, OFFSET(skip_frame_thresh), AV_OPT_TYPE_INT, {.i64=1}, 0, 24, VE},
    { "start_one_color_thresh", NULL, OFFSET(start_one_color_thresh), AV_OPT_TYPE_INT, {.i64=1}, 0, 24, VE},
    { "continue_one_color_thresh", NULL, OFFSET(continue_one_color_thresh), AV_OPT_TYPE_INT, {.i64=0}, 0, 24, VE},
    { "sixteen_color_thresh", NULL, OFFSET(sixteen_color_thresh), AV_OPT_TYPE_INT, {.i64=1}, 0, 24, VE},
    { NULL },
};

static const AVClass rpza_class = {
    .class_name = "rpza",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_rpza_encoder = {
    .p.name         = "rpza",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QuickTime video (RPZA)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RPZA,
    .priv_data_size = sizeof(RpzaContext),
    .p.priv_class   = &rpza_class,
    .init           = rpza_encode_init,
    FF_CODEC_ENCODE_CB(rpza_encode_frame),
    .close          = rpza_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_RGB555,
                                                     AV_PIX_FMT_NONE},
};
