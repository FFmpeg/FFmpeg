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
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "deshake.h"
#include "deshake_opencl.h"

#define CHROMA_WIDTH(link)  (-((-(link)->w) >> av_pix_fmt_desc_get((link)->format)->log2_chroma_w))
#define CHROMA_HEIGHT(link) (-((-(link)->h) >> av_pix_fmt_desc_get((link)->format)->log2_chroma_h))

#define OFFSET(x) offsetof(DeshakeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption deshake_options[] = {
    { "x", "set x for the rectangular search area",      OFFSET(cx), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "y", "set y for the rectangular search area",      OFFSET(cy), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "w", "set width for the rectangular search area",  OFFSET(cw), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "h", "set height for the rectangular search area", OFFSET(ch), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, .flags = FLAGS },
    { "rx", "set x for the rectangular search area",     OFFSET(rx), AV_OPT_TYPE_INT, {.i64=16}, 0, MAX_R, .flags = FLAGS },
    { "ry", "set y for the rectangular search area",     OFFSET(ry), AV_OPT_TYPE_INT, {.i64=16}, 0, MAX_R, .flags = FLAGS },
    { "edge", "set edge mode", OFFSET(edge), AV_OPT_TYPE_INT, {.i64=FILL_MIRROR}, FILL_BLANK, FILL_COUNT-1, FLAGS, "edge"},
        { "blank",    "fill zeroes at blank locations",         0, AV_OPT_TYPE_CONST, {.i64=FILL_BLANK},    INT_MIN, INT_MAX, FLAGS, "edge" },
        { "original", "original image at blank locations",      0, AV_OPT_TYPE_CONST, {.i64=FILL_ORIGINAL}, INT_MIN, INT_MAX, FLAGS, "edge" },
        { "clamp",    "extruded edge value at blank locations", 0, AV_OPT_TYPE_CONST, {.i64=FILL_CLAMP},    INT_MIN, INT_MAX, FLAGS, "edge" },
        { "mirror",   "mirrored edge at blank locations",       0, AV_OPT_TYPE_CONST, {.i64=FILL_MIRROR},   INT_MIN, INT_MAX, FLAGS, "edge" },
    { "blocksize", "set motion search blocksize",       OFFSET(blocksize), AV_OPT_TYPE_INT, {.i64=8},   4, 128, .flags = FLAGS },
    { "contrast",  "set contrast threshold for blocks", OFFSET(contrast),  AV_OPT_TYPE_INT, {.i64=125}, 1, 255, .flags = FLAGS },
    { "search",  "set search strategy", OFFSET(search), AV_OPT_TYPE_INT, {.i64=EXHAUSTIVE}, EXHAUSTIVE, SEARCH_COUNT-1, FLAGS, "smode" },
        { "exhaustive", "exhaustive search",      0, AV_OPT_TYPE_CONST, {.i64=EXHAUSTIVE},       INT_MIN, INT_MAX, FLAGS, "smode" },
        { "less",       "less exhaustive search", 0, AV_OPT_TYPE_CONST, {.i64=SMART_EXHAUSTIVE}, INT_MIN, INT_MAX, FLAGS, "smode" },
    { "filename", "set motion search detailed log file name", OFFSET(filename), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "opencl", "use OpenCL filtering capabilities", OFFSET(opencl), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(deshake);

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
                              IntMotionVector *mv)
{
    int x, y;
    int diff;
    int smallest = INT_MAX;
    int tmp, tmp2;

    #define CMP(i, j) deshake->sad(src1 + cy  * stride + cx,  stride,\
                                   src2 + (j) * stride + (i), stride)

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
        for (y = -deshake->ry + 1; y < deshake->ry; y += 2) {
            for (x = -deshake->rx + 1; x < deshake->rx; x += 2) {
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
    int lowest = 255;
    int i, j, pos;

    for (i = 0; i <= blocksize * 2; i++) {
        // We use a width of 16 here to match the sad function
        for (j = 0; j <= 15; j++) {
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
static double block_angle(int x, int y, int cx, int cy, IntMotionVector *shift)
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
    IntMotionVector mv = {0, 0};
    int count_max_value = 0;
    int contrast;

    int pos;
    int center_x = 0, center_y = 0;
    double p_x, p_y;

    av_fast_malloc(&deshake->angles, &deshake->angles_size, width * height / (16 * deshake->blocksize) * sizeof(*deshake->angles));

    // Reset counts to zero
    for (x = 0; x < deshake->rx * 2 + 1; x++) {
        for (y = 0; y < deshake->ry * 2 + 1; y++) {
            deshake->counts[x][y] = 0;
        }
    }

    pos = 0;
    // Find motion for every block and store the motion vector in the counts
    for (y = deshake->ry; y < height - deshake->ry - (deshake->blocksize * 2); y += deshake->blocksize * 2) {
        // We use a width of 16 here to match the sad function
        for (x = deshake->rx; x < width - deshake->rx - 16; x += 16) {
            // If the contrast is too low, just skip this block as it probably
            // won't be very useful to us.
            contrast = block_contrast(src2, x, y, stride, deshake->blocksize);
            if (contrast > deshake->contrast) {
                //av_log(NULL, AV_LOG_ERROR, "%d\n", contrast);
                find_block_motion(deshake, src1, src2, x, y, stride, &mv);
                if (mv.x != -1 && mv.y != -1) {
                    deshake->counts[mv.x + deshake->rx][mv.y + deshake->ry] += 1;
                    if (x > deshake->rx && y > deshake->ry)
                        deshake->angles[pos++] = block_angle(x, y, 0, 0, &mv);

                    center_x += mv.x;
                    center_y += mv.y;
                }
            }
        }
    }

    if (pos) {
         center_x /= pos;
         center_y /= pos;
         t->angle = clean_mean(deshake->angles, pos);
         if (t->angle < 0.001)
              t->angle = 0;
    } else {
         t->angle = 0;
    }

    // Find the most common motion vector in the frame and use it as the gmv
    for (y = deshake->ry * 2; y >= 0; y--) {
        for (x = 0; x < deshake->rx * 2 + 1; x++) {
            //av_log(NULL, AV_LOG_ERROR, "%5d ", deshake->counts[x][y]);
            if (deshake->counts[x][y] > count_max_value) {
                t->vec.x = x - deshake->rx;
                t->vec.y = y - deshake->ry;
                count_max_value = deshake->counts[x][y];
            }
        }
        //av_log(NULL, AV_LOG_ERROR, "\n");
    }

    p_x = (center_x - width / 2.0);
    p_y = (center_y - height / 2.0);
    t->vec.x += (cos(t->angle)-1)*p_x  - sin(t->angle)*p_y;
    t->vec.y += sin(t->angle)*p_x  + (cos(t->angle)-1)*p_y;

    // Clamp max shift & rotation?
    t->vec.x = av_clipf(t->vec.x, -deshake->rx * 2, deshake->rx * 2);
    t->vec.y = av_clipf(t->vec.y, -deshake->ry * 2, deshake->ry * 2);
    t->angle = av_clipf(t->angle, -0.1, 0.1);

    //av_log(NULL, AV_LOG_ERROR, "%d x %d\n", avg->x, avg->y);
}

static int deshake_transform_c(AVFilterContext *ctx,
                                    int width, int height, int cw, int ch,
                                    const float *matrix_y, const float *matrix_uv,
                                    enum InterpolateMethod interpolate,
                                    enum FillMethod fill, AVFrame *in, AVFrame *out)
{
    int i = 0, ret = 0;
    const float *matrixs[3];
    int plane_w[3], plane_h[3];
    matrixs[0] = matrix_y;
    matrixs[1] =  matrixs[2] = matrix_uv;
    plane_w[0] = width;
    plane_w[1] = plane_w[2] = cw;
    plane_h[0] = height;
    plane_h[1] = plane_h[2] = ch;

    for (i = 0; i < 3; i++) {
        // Transform the luma and chroma planes
        ret = avfilter_transform(in->data[i], out->data[i], in->linesize[i], out->linesize[i],
                                 plane_w[i], plane_h[i], matrixs[i], interpolate, fill);
        if (ret < 0)
            return ret;
    }
    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    int ret;
    DeshakeContext *deshake = ctx->priv;

    deshake->sad = av_pixelutils_get_sad_fn(4, 4, 1, deshake); // 16x16, 2nd source unaligned
    if (!deshake->sad)
        return AVERROR(EINVAL);

    deshake->refcount = 20; // XXX: add to options?
    deshake->blocksize /= 2;
    deshake->blocksize = av_clip(deshake->blocksize, 4, 128);

    if (deshake->rx % 16) {
        av_log(ctx, AV_LOG_ERROR, "rx must be a multiple of 16\n");
        return AVERROR_PATCHWELCOME;
    }

    if (deshake->filename)
        deshake->fp = fopen(deshake->filename, "w");
    if (deshake->fp)
        fwrite("Ori x, Avg x, Fin x, Ori y, Avg y, Fin y, Ori angle, Avg angle, Fin angle, Ori zoom, Avg zoom, Fin zoom\n", sizeof(char), 104, deshake->fp);

    // Quadword align left edge of box for MMX code, adjust width if necessary
    // to keep right margin
    if (deshake->cx > 0) {
        deshake->cw += deshake->cx - (deshake->cx & ~15);
        deshake->cx &= ~15;
    }
    deshake->transform = deshake_transform_c;
    if (!CONFIG_OPENCL && deshake->opencl) {
        av_log(ctx, AV_LOG_ERROR, "OpenCL support was not enabled in this build, cannot be selected\n");
        return AVERROR(EINVAL);
    }

    if (CONFIG_OPENCL && deshake->opencl) {
        deshake->transform = ff_opencl_transform;
        ret = ff_opencl_deshake_init(ctx);
        if (ret < 0)
            return ret;
    }
    av_log(ctx, AV_LOG_VERBOSE, "cx: %d, cy: %d, cw: %d, ch: %d, rx: %d, ry: %d, edge: %d blocksize: %d contrast: %d search: %d\n",
           deshake->cx, deshake->cy, deshake->cw, deshake->ch,
           deshake->rx, deshake->ry, deshake->edge, deshake->blocksize * 2, deshake->contrast, deshake->search);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));

    return 0;
}

static int config_props(AVFilterLink *link)
{
    DeshakeContext *deshake = link->dst->priv;

    deshake->ref = NULL;
    deshake->last.vec.x = 0;
    deshake->last.vec.y = 0;
    deshake->last.angle = 0;
    deshake->last.zoom = 0;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DeshakeContext *deshake = ctx->priv;
    if (CONFIG_OPENCL && deshake->opencl) {
        ff_opencl_deshake_uninit(ctx);
    }
    av_frame_free(&deshake->ref);
    av_freep(&deshake->angles);
    deshake->angles_size = 0;
    if (deshake->fp)
        fclose(deshake->fp);
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    DeshakeContext *deshake = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFrame *out;
    Transform t = {{0},0}, orig = {{0},0};
    float matrix_y[9], matrix_uv[9];
    float alpha = 2.0 / deshake->refcount;
    char tmp[256];
    int ret = 0;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (CONFIG_OPENCL && deshake->opencl) {
        ret = ff_opencl_deshake_process_inout_buf(link->dst,in, out);
        if (ret < 0)
            return ret;
    }

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
    orig.vec.x = t.vec.x;
    orig.vec.y = t.vec.y;
    orig.angle = t.angle;
    orig.zoom = t.zoom;

    // Generate a one-sided moving exponential average
    deshake->avg.vec.x = alpha * t.vec.x + (1.0 - alpha) * deshake->avg.vec.x;
    deshake->avg.vec.y = alpha * t.vec.y + (1.0 - alpha) * deshake->avg.vec.y;
    deshake->avg.angle = alpha * t.angle + (1.0 - alpha) * deshake->avg.angle;
    deshake->avg.zoom = alpha * t.zoom + (1.0 - alpha) * deshake->avg.zoom;

    // Remove the average from the current motion to detect the motion that
    // is not on purpose, just as jitter from bumping the camera
    t.vec.x -= deshake->avg.vec.x;
    t.vec.y -= deshake->avg.vec.y;
    t.angle -= deshake->avg.angle;
    t.zoom -= deshake->avg.zoom;

    // Invert the motion to undo it
    t.vec.x *= -1;
    t.vec.y *= -1;
    t.angle *= -1;

    // Write statistics to file
    if (deshake->fp) {
        snprintf(tmp, 256, "%f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f\n", orig.vec.x, deshake->avg.vec.x, t.vec.x, orig.vec.y, deshake->avg.vec.y, t.vec.y, orig.angle, deshake->avg.angle, t.angle, orig.zoom, deshake->avg.zoom, t.zoom);
        fwrite(tmp, sizeof(char), strlen(tmp), deshake->fp);
    }

    // Turn relative current frame motion into absolute by adding it to the
    // last absolute motion
    t.vec.x += deshake->last.vec.x;
    t.vec.y += deshake->last.vec.y;
    t.angle += deshake->last.angle;
    t.zoom += deshake->last.zoom;

    // Shrink motion by 10% to keep things centered in the camera frame
    t.vec.x *= 0.9;
    t.vec.y *= 0.9;
    t.angle *= 0.9;

    // Store the last absolute motion information
    deshake->last.vec.x = t.vec.x;
    deshake->last.vec.y = t.vec.y;
    deshake->last.angle = t.angle;
    deshake->last.zoom = t.zoom;

    // Generate a luma transformation matrix
    avfilter_get_matrix(t.vec.x, t.vec.y, t.angle, 1.0 + t.zoom / 100.0, matrix_y);
    // Generate a chroma transformation matrix
    avfilter_get_matrix(t.vec.x / (link->w / CHROMA_WIDTH(link)), t.vec.y / (link->h / CHROMA_HEIGHT(link)), t.angle, 1.0 + t.zoom / 100.0, matrix_uv);
    // Transform the luma and chroma planes
    ret = deshake->transform(link->dst, link->w, link->h, CHROMA_WIDTH(link), CHROMA_HEIGHT(link),
                             matrix_y, matrix_uv, INTERPOLATE_BILINEAR, deshake->edge, in, out);

    // Cleanup the old reference frame
    av_frame_free(&deshake->ref);

    if (ret < 0)
        return ret;

    // Store the current frame as the reference frame for calculating the
    // motion of the next frame
    deshake->ref = in;

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad deshake_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad deshake_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_deshake = {
    .name          = "deshake",
    .description   = NULL_IF_CONFIG_SMALL("Stabilize shaky video."),
    .priv_size     = sizeof(DeshakeContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = deshake_inputs,
    .outputs       = deshake_outputs,
    .priv_class    = &deshake_class,
};
