/*
 * Copyright (C) 2010 Georg Martius <georg.martius@web.de>
 * Copyright (C) 2010 Daniel G. Taylor <dan@programmer-art.org>
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
 * @file
 * fast deshake / depan video filter
 *
 * SAD block-matching motion compensation to fix small changes in
 * horizontal and/or vertical shift. This filter helps remove camera shake
 * from hand-holding a camera, bumping a tripod, moving on a vehicle, etc.
 *
 * Algorithm:
 *   - For each frame with one previous reference frame
 *       - For each block in the frame
 *           - If contrast > threshold then find likely motion vector
 *       - For all found motion vectors
 *           - Find most common, store as global motion vector
 *       - Find most likely rotation angle
 *       - Transform image along global motion
 *
 * TODO:
 *   - Fill frame edges based on previous/next reference frames
 *   - Fill frame edges by stretching image near the edges?
 *       - Can this be done quickly and look decent?
 *
 * Dark Shikari links to http://wiki.videolan.org/SoC_x264_2010#GPU_Motion_Estimation_2
 * for an algorithm similar to what could be used here to get the gmv
 * It requires only a couple diamond searches + fast downscaling
 *
 * Special thanks to Jason Kotenko for his help with the algorithm and my
 * inability to see simple errors in C code.
 */

#include "avfilter.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/dsputil.h"

#include "transform.h"

#define CHROMA_WIDTH(link)  -((-link->w) >> av_pix_fmt_descriptors[link->format].log2_chroma_w)
#define CHROMA_HEIGHT(link) -((-link->h) >> av_pix_fmt_descriptors[link->format].log2_chroma_h)

enum SearchMethod {
    EXHAUSTIVE,        ///< Search all possible positions
    SMART_EXHAUSTIVE,  ///< Search most possible positions (faster)
    SEARCH_COUNT
};

typedef struct {
    double x;             ///< Horizontal shift
    double y;             ///< Vertical shift
} MotionVector;

typedef struct {
    MotionVector vector;  ///< Motion vector
    double angle;         ///< Angle of rotation
    double zoom;          ///< Zoom percentage
} Transform;

typedef struct {
    AVClass av_class;
    AVFilterBufferRef *ref;    ///< Previous frame
    int rx;                    ///< Maximum horizontal shift
    int ry;                    ///< Maximum vertical shift
    enum FillMethod edge;      ///< Edge fill method
    int blocksize;             ///< Size of blocks to compare
    int contrast;              ///< Contrast threshold
    enum SearchMethod search;  ///< Motion search method
    AVCodecContext *avctx;
    DSPContext c;              ///< Context providing optimized SAD methods
    Transform last;            ///< Transform from last frame
    int refcount;              ///< Number of reference frames (defines averaging window)
    FILE *fp;
    Transform avg;
    int cw;                    ///< Crop motion search to this box
    int ch;
    int cx;
    int cy;
} DeshakeContext;

static int cmp(const double *a, const double *b)
{
    return *a < *b ? -1 : ( *a > *b ? 1 : 0 );
}

/**
 * Cleaned mean (cuts off 20% of values to remove outliers and then averages)
 */
static double clean_mean(double *values, int count)
{
    double mean = 0;
    int cut = count / 5;
    int x;

    qsort(values, count, sizeof(double), (void*)cmp);

    for (x = cut; x < count - cut; x++) {
        mean += values[x];
    }

    return mean / (count - cut * 2);
}

/**
 * Find the most likely shift in motion between two frames for a given
 * macroblock. Test each block against several shifts given by the rx
 * and ry attributes. Searches using a simple matrix of those shifts and
 * chooses the most likely shift by the smallest difference in blocks.
 */
static void find_block_motion(DeshakeContext *deshake, uint8_t *src1,
                              uint8_t *src2, int cx, int cy, int stride,
                              MotionVector *mv)
{
    int x, y;
    int diff;
    int smallest = INT_MAX;
    int tmp, tmp2;

    #define CMP(i, j) deshake->c.sad[0](deshake, src1 + cy * stride + cx, \
                                        src2 + (j) * stride + (i), stride, \
                                        deshake->blocksize)

    if (deshake->search == EXHAUSTIVE) {
        // Compare every possible position - this is sloooow!
        for (y = -deshake->ry; y <= deshake->ry; y++) {
            for (x = -deshake->rx; x <= deshake->rx; x++) {
                diff = CMP(cx - x, cy - y);
                if (diff < smallest) {
                    smallest = diff;
                    mv->x = x;
                    mv->y = y;
                }
            }
        }
    } else if (deshake->search == SMART_EXHAUSTIVE) {
        // Compare every other possible position and find the best match
        for (y = -deshake->ry + 1; y < deshake->ry - 2; y += 2) {
            for (x = -deshake->rx + 1; x < deshake->rx - 2; x += 2) {
                diff = CMP(cx - x, cy - y);
                if (diff < smallest) {
                    smallest = diff;
                    mv->x = x;
                    mv->y = y;
                }
            }
        }

        // Hone in on the specific best match around the match we found above
        tmp = mv->x;
        tmp2 = mv->y;

        for (y = tmp2 - 1; y <= tmp2 + 1; y++) {
            for (x = tmp - 1; x <= tmp + 1; x++) {
                if (x == tmp && y == tmp2)
                    continue;

                diff = CMP(cx - x, cy - y);
                if (diff < smallest) {
                    smallest = diff;
                    mv->x = x;
                    mv->y = y;
                }
            }
        }
    }

    if (smallest > 512) {
        mv->x = -1;
        mv->y = -1;
    }
    emms_c();
    //av_log(NULL, AV_LOG_ERROR, "%d\n", smallest);
    //av_log(NULL, AV_LOG_ERROR, "Final: (%d, %d) = %d x %d\n", cx, cy, mv->x, mv->y);
}

/**
 * Find the contrast of a given block. When searching for global motion we
 * really only care about the high contrast blocks, so using this method we
 * can actually skip blocks we don't care much about.
 */
static int block_contrast(uint8_t *src, int x, int y, int stride, int blocksize)
{
    int highest = 0;
    int lowest = 0;
    int i, j, pos;

    for (i = 0; i <= blocksize * 2; i++) {
        // We use a width of 16 here to match the libavcodec sad functions
        for (j = 0; i <= 15; i++) {
            pos = (y - i) * stride + (x - j);
            if (src[pos] < lowest)
                lowest = src[pos];
            else if (src[pos] > highest) {
                highest = src[pos];
            }
        }
    }

    return highest - lowest;
}

/**
 * Find the rotation for a given block.
 */
static double block_angle(int x, int y, int cx, int cy, MotionVector *shift)
{
    double a1, a2, diff;

    a1 = atan2(y - cy, x - cx);
    a2 = atan2(y - cy + shift->y, x - cx + shift->x);

    diff = a2 - a1;

    return (diff > M_PI)  ? diff - 2 * M_PI :
           (diff < -M_PI) ? diff + 2 * M_PI :
           diff;
}

/**
 * Find the estimated global motion for a scene given the most likely shift
 * for each block in the frame. The global motion is estimated to be the
 * same as the motion from most blocks in the frame, so if most blocks
 * move one pixel to the right and two pixels down, this would yield a
 * motion vector (1, -2).
 */
static void find_motion(DeshakeContext *deshake, uint8_t *src1, uint8_t *src2,
                        int width, int height, int stride, Transform *t)
{
    int x, y;
    MotionVector mv = {0, 0};
    int counts[128][128];
    int count_max_value = 0;
    int contrast;

    int pos;
    double *angles = av_malloc(sizeof(*angles) * width * height / (16 * deshake->blocksize));
    double totalangles = 0;

    int center_x = 0, center_y = 0;
    double p_x, p_y;

    // Reset counts to zero
    for (x = 0; x < deshake->rx * 2 + 1; x++) {
        for (y = 0; y < deshake->ry * 2 + 1; y++) {
            counts[x][y] = 0;
        }
    }

    pos = 0;
    // Find motion for every block and store the motion vector in the counts
    for (y = deshake->ry; y < height - deshake->ry - (deshake->blocksize * 2); y += deshake->blocksize * 2) {
        // We use a width of 16 here to match the libavcodec sad functions
        for (x = deshake->rx; x < width - deshake->rx - 16; x += 16) {
            // If the contrast is too low, just skip this block as it probably
            // won't be very useful to us.
            contrast = block_contrast(src2, x, y, stride, deshake->blocksize);
            if (contrast > deshake->contrast) {
                //av_log(NULL, AV_LOG_ERROR, "%d\n", contrast);
                find_block_motion(deshake, src1, src2, x, y, stride, &mv);
                if (mv.x != -1 && mv.y != -1) {
                    counts[(int)(mv.x + deshake->rx)][(int)(mv.y + deshake->ry)] += 1;
                    if (x > deshake->rx && y > deshake->ry)
                        angles[pos++] = block_angle(x, y, 0, 0, &mv);

                    center_x += mv.x;
                    center_y += mv.y;
                }
            }
        }
    }

    pos = FFMAX(1, pos);

    center_x /= pos;
    center_y /= pos;

    for (x = 0; x < pos; x++) {
        totalangles += angles[x];
    }

    //av_log(NULL, AV_LOG_ERROR, "Angle: %lf\n", totalangles / (pos - 1));
    t->angle = totalangles / (pos - 1);

    t->angle = clean_mean(angles, pos);
    if (t->angle < 0.001)
        t->angle = 0;

    // Find the most common motion vector in the frame and use it as the gmv
    for (y = deshake->ry * 2; y >= 0; y--) {
        for (x = 0; x < deshake->rx * 2 + 1; x++) {
            //av_log(NULL, AV_LOG_ERROR, "%5d ", counts[x][y]);
            if (counts[x][y] > count_max_value) {
                t->vector.x = x - deshake->rx;
                t->vector.y = y - deshake->ry;
                count_max_value = counts[x][y];
            }
        }
        //av_log(NULL, AV_LOG_ERROR, "\n");
    }

    p_x = (center_x - width / 2);
    p_y = (center_y - height / 2);
    t->vector.x += (cos(t->angle)-1)*p_x  - sin(t->angle)*p_y;
    t->vector.y += sin(t->angle)*p_x  + (cos(t->angle)-1)*p_y;

    // Clamp max shift & rotation?
    t->vector.x = av_clipf(t->vector.x, -deshake->rx * 2, deshake->rx * 2);
    t->vector.y = av_clipf(t->vector.y, -deshake->ry * 2, deshake->ry * 2);
    t->angle = av_clipf(t->angle, -0.1, 0.1);

    //av_log(NULL, AV_LOG_ERROR, "%d x %d\n", avg->x, avg->y);
    av_free(angles);
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    DeshakeContext *deshake = ctx->priv;
    char filename[256] = {0};

    deshake->rx = 16;
    deshake->ry = 16;
    deshake->edge = FILL_MIRROR;
    deshake->blocksize = 8;
    deshake->contrast = 125;
    deshake->search = EXHAUSTIVE;
    deshake->refcount = 20;

    deshake->cw = -1;
    deshake->ch = -1;
    deshake->cx = -1;
    deshake->cy = -1;

    if (args) {
        sscanf(args, "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%255s",
               &deshake->cx, &deshake->cy, &deshake->cw, &deshake->ch,
               &deshake->rx, &deshake->ry, (int *)&deshake->edge,
               &deshake->blocksize, &deshake->contrast, (int *)&deshake->search, filename);

        deshake->blocksize /= 2;

        deshake->rx = av_clip(deshake->rx, 0, 64);
        deshake->ry = av_clip(deshake->ry, 0, 64);
        deshake->edge = av_clip(deshake->edge, FILL_BLANK, FILL_COUNT - 1);
        deshake->blocksize = av_clip(deshake->blocksize, 4, 128);
        deshake->contrast = av_clip(deshake->contrast, 1, 255);
        deshake->search = av_clip(deshake->search, EXHAUSTIVE, SEARCH_COUNT - 1);

    }
    if (*filename)
        deshake->fp = fopen(filename, "w");
    if (deshake->fp)
        fwrite("Ori x, Avg x, Fin x, Ori y, Avg y, Fin y, Ori angle, Avg angle, Fin angle, Ori zoom, Avg zoom, Fin zoom\n", sizeof(char), 104, deshake->fp);

    // Quadword align left edge of box for MMX code, adjust width if necessary
    // to keep right margin
    if (deshake->cx > 0) {
        deshake->cw += deshake->cx - (deshake->cx & ~15);
        deshake->cx &= ~15;
    }

    av_log(ctx, AV_LOG_INFO, "cx: %d, cy: %d, cw: %d, ch: %d, rx: %d, ry: %d, edge: %d blocksize: %d contrast: %d search: %d\n",
           deshake->cx, deshake->cy, deshake->cw, deshake->ch,
           deshake->rx, deshake->ry, deshake->edge, deshake->blocksize * 2, deshake->contrast, deshake->search);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV420P,  PIX_FMT_YUV422P,  PIX_FMT_YUV444P,  PIX_FMT_YUV410P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV440P,  PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P,
        PIX_FMT_YUVJ444P, PIX_FMT_YUVJ440P, PIX_FMT_NONE
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static int config_props(AVFilterLink *link)
{
    DeshakeContext *deshake = link->dst->priv;

    deshake->ref = NULL;
    deshake->last.vector.x = 0;
    deshake->last.vector.y = 0;
    deshake->last.angle = 0;
    deshake->last.zoom = 0;

    deshake->avctx = avcodec_alloc_context3(NULL);
    dsputil_init(&deshake->c, deshake->avctx);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DeshakeContext *deshake = ctx->priv;

    avfilter_unref_buffer(deshake->ref);
    if (deshake->fp)
        fclose(deshake->fp);
}

static void end_frame(AVFilterLink *link)
{
    DeshakeContext *deshake = link->dst->priv;
    AVFilterBufferRef *in  = link->cur_buf;
    AVFilterBufferRef *out = link->dst->outputs[0]->out_buf;
    Transform t;
    float matrix[9];
    float alpha = 2.0 / deshake->refcount;
    char tmp[256];
    Transform orig;

    if (deshake->cx < 0 || deshake->cy < 0 || deshake->cw < 0 || deshake->ch < 0) {
        // Find the most likely global motion for the current frame
        find_motion(deshake, (deshake->ref == NULL) ? in->data[0] : deshake->ref->data[0], in->data[0], link->w, link->h, in->linesize[0], &t);
    } else {
        uint8_t *src1 = (deshake->ref == NULL) ? in->data[0] : deshake->ref->data[0];
        uint8_t *src2 = in->data[0];

        deshake->cx = FFMIN(deshake->cx, link->w);
        deshake->cy = FFMIN(deshake->cy, link->h);

        if ((unsigned)deshake->cx + (unsigned)deshake->cw > link->w) deshake->cw = link->w - deshake->cx;
        if ((unsigned)deshake->cy + (unsigned)deshake->ch > link->h) deshake->ch = link->h - deshake->cy;

        // Quadword align right margin
        deshake->cw &= ~15;

        src1 += deshake->cy * in->linesize[0] + deshake->cx;
        src2 += deshake->cy * in->linesize[0] + deshake->cx;

        find_motion(deshake, src1, src2, deshake->cw, deshake->ch, in->linesize[0], &t);
    }


    // Copy transform so we can output it later to compare to the smoothed value
    orig.vector.x = t.vector.x;
    orig.vector.y = t.vector.y;
    orig.angle = t.angle;
    orig.zoom = t.zoom;

    // Generate a one-sided moving exponential average
    deshake->avg.vector.x = alpha * t.vector.x + (1.0 - alpha) * deshake->avg.vector.x;
    deshake->avg.vector.y = alpha * t.vector.y + (1.0 - alpha) * deshake->avg.vector.y;
    deshake->avg.angle = alpha * t.angle + (1.0 - alpha) * deshake->avg.angle;
    deshake->avg.zoom = alpha * t.zoom + (1.0 - alpha) * deshake->avg.zoom;

    // Remove the average from the current motion to detect the motion that
    // is not on purpose, just as jitter from bumping the camera
    t.vector.x -= deshake->avg.vector.x;
    t.vector.y -= deshake->avg.vector.y;
    t.angle -= deshake->avg.angle;
    t.zoom -= deshake->avg.zoom;

    // Invert the motion to undo it
    t.vector.x *= -1;
    t.vector.y *= -1;
    t.angle *= -1;

    // Write statistics to file
    if (deshake->fp) {
        snprintf(tmp, 256, "%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n", orig.vector.x, deshake->avg.vector.x, t.vector.x, orig.vector.y, deshake->avg.vector.y, t.vector.y, orig.angle, deshake->avg.angle, t.angle, orig.zoom, deshake->avg.zoom, t.zoom);
        fwrite(tmp, sizeof(char), strlen(tmp), deshake->fp);
    }

    // Turn relative current frame motion into absolute by adding it to the
    // last absolute motion
    t.vector.x += deshake->last.vector.x;
    t.vector.y += deshake->last.vector.y;
    t.angle += deshake->last.angle;
    t.zoom += deshake->last.zoom;

    // Shrink motion by 10% to keep things centered in the camera frame
    t.vector.x *= 0.9;
    t.vector.y *= 0.9;
    t.angle *= 0.9;

    // Store the last absolute motion information
    deshake->last.vector.x = t.vector.x;
    deshake->last.vector.y = t.vector.y;
    deshake->last.angle = t.angle;
    deshake->last.zoom = t.zoom;

    // Generate a luma transformation matrix
    avfilter_get_matrix(t.vector.x, t.vector.y, t.angle, 1.0 + t.zoom / 100.0, matrix);

    // Transform the luma plane
    avfilter_transform(in->data[0], out->data[0], in->linesize[0], out->linesize[0], link->w, link->h, matrix, INTERPOLATE_BILINEAR, deshake->edge);

    // Generate a chroma transformation matrix
    avfilter_get_matrix(t.vector.x / (link->w / CHROMA_WIDTH(link)), t.vector.y / (link->h / CHROMA_HEIGHT(link)), t.angle, 1.0 + t.zoom / 100.0, matrix);

    // Transform the chroma planes
    avfilter_transform(in->data[1], out->data[1], in->linesize[1], out->linesize[1], CHROMA_WIDTH(link), CHROMA_HEIGHT(link), matrix, INTERPOLATE_BILINEAR, deshake->edge);
    avfilter_transform(in->data[2], out->data[2], in->linesize[2], out->linesize[2], CHROMA_WIDTH(link), CHROMA_HEIGHT(link), matrix, INTERPOLATE_BILINEAR, deshake->edge);

    // Store the current frame as the reference frame for calculating the
    // motion of the next frame
    if (deshake->ref != NULL)
        avfilter_unref_buffer(deshake->ref);

    // Cleanup the old reference frame
    deshake->ref = in;

    // Draw the transformed frame information
    avfilter_draw_slice(link->dst->outputs[0], 0, link->h, 1);
    avfilter_end_frame(link->dst->outputs[0]);
    avfilter_unref_buffer(out);
}

static void draw_slice(AVFilterLink *link, int y, int h, int slice_dir)
{
}

AVFilter avfilter_vf_deshake = {
    .name      = "deshake",
    .description = NULL_IF_CONFIG_SMALL("Stabilize shaky video."),

    .priv_size = sizeof(DeshakeContext),

    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .draw_slice       = draw_slice,
                                    .end_frame        = end_frame,
                                    .config_props     = config_props,
                                    .min_perms        = AV_PERM_READ, },
                                  { .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name       = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
