/*
 * Copyright (c) 2005 Robert Edele <yartrebo@earthlink.net>
 * Copyright (c) 2012 Stefano Sabatini
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
 * Advanced blur-based logo removing filter
 *
 * This filter loads an image mask file showing where a logo is and
 * uses a blur transform to remove the logo.
 *
 * Based on the libmpcodecs remove-logo filter by Robert Edele.
 */

/**
 * This code implements a filter to remove annoying TV logos and other annoying
 * images placed onto a video stream. It works by filling in the pixels that
 * comprise the logo with neighboring pixels. The transform is very loosely
 * based on a gaussian blur, but it is different enough to merit its own
 * paragraph later on. It is a major improvement on the old delogo filter as it
 * both uses a better blurring algorithm and uses a bitmap to use an arbitrary
 * and generally much tighter fitting shape than a rectangle.
 *
 * The logo removal algorithm has two key points. The first is that it
 * distinguishes between pixels in the logo and those not in the logo by using
 * the passed-in bitmap. Pixels not in the logo are copied over directly without
 * being modified and they also serve as source pixels for the logo
 * fill-in. Pixels inside the logo have the mask applied.
 *
 * At init-time the bitmap is reprocessed internally, and the distance to the
 * nearest edge of the logo (Manhattan distance), along with a little extra to
 * remove rough edges, is stored in each pixel. This is done using an in-place
 * erosion algorithm, and incrementing each pixel that survives any given
 * erosion.  Once every pixel is eroded, the maximum value is recorded, and a
 * set of masks from size 0 to this size are generaged. The masks are circular
 * binary masks, where each pixel within a radius N (where N is the size of the
 * mask) is a 1, and all other pixels are a 0. Although a gaussian mask would be
 * more mathematically accurate, a binary mask works better in practice because
 * we generally do not use the central pixels in the mask (because they are in
 * the logo region), and thus a gaussian mask will cause too little blur and
 * thus a very unstable image.
 *
 * The mask is applied in a special way. Namely, only pixels in the mask that
 * line up to pixels outside the logo are used. The dynamic mask size means that
 * the mask is just big enough so that the edges touch pixels outside the logo,
 * so the blurring is kept to a minimum and at least the first boundary
 * condition is met (that the image function itself is continuous), even if the
 * second boundary condition (that the derivative of the image function is
 * continuous) is not met. A masking algorithm that does preserve the second
 * boundary coundition (perhaps something based on a highly-modified bi-cubic
 * algorithm) should offer even better results on paper, but the noise in a
 * typical TV signal should make anything based on derivatives hopelessly noisy.
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "bbox.h"
#include "lavfutils.h"
#include "lswsutils.h"

typedef struct {
    const AVClass *class;
    char *filename;
    /* Stores our collection of masks. The first is for an array of
       the second for the y axis, and the third for the x axis. */
    int ***mask;
    int max_mask_size;
    int mask_w, mask_h;

    uint8_t      *full_mask_data;
    FFBoundingBox full_mask_bbox;
    uint8_t      *half_mask_data;
    FFBoundingBox half_mask_bbox;
} RemovelogoContext;

#define OFFSET(x) offsetof(RemovelogoContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption removelogo_options[] = {
    { "filename", "set bitmap filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { "f",        "set bitmap filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(removelogo);

/**
 * Choose a slightly larger mask size to improve performance.
 *
 * This function maps the absolute minimum mask size needed to the
 * mask size we'll actually use. f(x) = x (the smallest that will
 * work) will produce the sharpest results, but will be quite
 * jittery. f(x) = 1.25x (what I'm using) is a good tradeoff in my
 * opinion. This will calculate only at init-time, so you can put a
 * long expression here without effecting performance.
 */
#define apply_mask_fudge_factor(x) (((x) >> 2) + x)

/**
 * Pre-process an image to give distance information.
 *
 * This function takes a bitmap image and converts it in place into a
 * distance image. A distance image is zero for pixels outside of the
 * logo and is the Manhattan distance (|dx| + |dy|) from the logo edge
 * for pixels inside of the logo. This will overestimate the distance,
 * but that is safe, and is far easier to implement than a proper
 * pythagorean distance since I'm using a modified erosion algorithm
 * to compute the distances.
 *
 * @param mask image which will be converted from a greyscale image
 * into a distance image.
 */
static void convert_mask_to_strength_mask(uint8_t *data, int linesize,
                                          int w, int h, int min_val,
                                          int *max_mask_size)
{
    int x, y;

    /* How many times we've gone through the loop. Used in the
       in-place erosion algorithm and to get us max_mask_size later on. */
    int current_pass = 0;

    /* set all non-zero values to 1 */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            data[y*linesize + x] = data[y*linesize + x] > min_val;

    /* For each pass, if a pixel is itself the same value as the
       current pass, and its four neighbors are too, then it is
       incremented. If no pixels are incremented by the end of the
       pass, then we go again. Edge pixels are counted as always
       excluded (this should be true anyway for any sane mask, but if
       it isn't this will ensure that we eventually exit). */
    while (1) {
        /* If this doesn't get set by the end of this pass, then we're done. */
        int has_anything_changed = 0;
        uint8_t *current_pixel0 = data, *current_pixel;
        current_pass++;

        for (y = 1; y < h-1; y++) {
            current_pixel = current_pixel0;
            for (x = 1; x < w-1; x++) {
                /* Apply the in-place erosion transform. It is based
                   on the following two premises:
                   1 - Any pixel that fails 1 erosion will fail all
                       future erosions.

                   2 - Only pixels having survived all erosions up to
                       the present will be >= to current_pass.
                   It doesn't matter if it survived the current pass,
                   failed it, or hasn't been tested yet.  By using >=
                   instead of ==, we allow the algorithm to work in
                   place. */
                if ( *current_pixel      >= current_pass &&
                    *(current_pixel + 1) >= current_pass &&
                    *(current_pixel - 1) >= current_pass &&
                    *(current_pixel + w) >= current_pass &&
                    *(current_pixel - w) >= current_pass) {
                    /* Increment the value since it still has not been
                     * eroded, as evidenced by the if statement that
                     * just evaluated to true. */
                    (*current_pixel)++;
                    has_anything_changed = 1;
                }
                current_pixel++;
            }
            current_pixel0 += linesize;
        }
        if (!has_anything_changed)
            break;
    }

    /* Apply the fudge factor, which will increase the size of the
     * mask a little to reduce jitter at the cost of more blur. */
    for (y = 1; y < h - 1; y++)
        for (x = 1; x < w - 1; x++)
            data[(y * linesize) + x] = apply_mask_fudge_factor(data[(y * linesize) + x]);

    /* As a side-effect, we now know the maximum mask size, which
     * we'll use to generate our masks. */
    /* Apply the fudge factor to this number too, since we must ensure
     * that enough masks are generated. */
    *max_mask_size = apply_mask_fudge_factor(current_pass + 1);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int load_mask(uint8_t **mask, int *w, int *h,
                     const char *filename, void *log_ctx)
{
    int ret;
    enum AVPixelFormat pix_fmt;
    uint8_t *src_data[4], *gray_data[4];
    int src_linesize[4], gray_linesize[4];

    /* load image from file */
    if ((ret = ff_load_image(src_data, src_linesize, w, h, &pix_fmt, filename, log_ctx)) < 0)
        return ret;

    /* convert the image to GRAY8 */
    if ((ret = ff_scale_image(gray_data, gray_linesize, *w, *h, AV_PIX_FMT_GRAY8,
                              src_data, src_linesize, *w, *h, pix_fmt,
                              log_ctx)) < 0)
        goto end;

    /* copy mask to a newly allocated array */
    *mask = av_malloc(*w * *h);
    if (!*mask)
        ret = AVERROR(ENOMEM);
    av_image_copy_plane(*mask, *w, gray_data[0], gray_linesize[0], *w, *h);

end:
    av_free(src_data[0]);
    av_free(gray_data[0]);
    return ret;
}

/**
 * Generate a scaled down image with half width, height, and intensity.
 *
 * This function not only scales down an image, but halves the value
 * in each pixel too. The purpose of this is to produce a chroma
 * filter image out of a luma filter image. The pixel values store the
 * distance to the edge of the logo and halving the dimensions halves
 * the distance. This function rounds up, because a downwards rounding
 * error could cause the filter to fail, but an upwards rounding error
 * will only cause a minor amount of excess blur in the chroma planes.
 */
static void generate_half_size_image(const uint8_t *src_data, int src_linesize,
                                     uint8_t *dst_data, int dst_linesize,
                                     int src_w, int src_h,
                                     int *max_mask_size)
{
    int x, y;

    /* Copy over the image data, using the average of 4 pixels for to
     * calculate each downsampled pixel. */
    for (y = 0; y < src_h/2; y++) {
        for (x = 0; x < src_w/2; x++) {
            /* Set the pixel if there exists a non-zero value in the
             * source pixels, else clear it. */
            dst_data[(y * dst_linesize) + x] =
                src_data[((y << 1) * src_linesize) + (x << 1)] ||
                src_data[((y << 1) * src_linesize) + (x << 1) + 1] ||
                src_data[(((y << 1) + 1) * src_linesize) + (x << 1)] ||
                src_data[(((y << 1) + 1) * src_linesize) + (x << 1) + 1];
            dst_data[(y * dst_linesize) + x] = FFMIN(1, dst_data[(y * dst_linesize) + x]);
        }
    }

    convert_mask_to_strength_mask(dst_data, dst_linesize,
                                  src_w/2, src_h/2, 0, max_mask_size);
}

static av_cold int init(AVFilterContext *ctx)
{
    RemovelogoContext *removelogo = ctx->priv;
    int ***mask;
    int ret = 0;
    int a, b, c, w, h;
    int full_max_mask_size, half_max_mask_size;

    if (!removelogo->filename) {
        av_log(ctx, AV_LOG_ERROR, "The bitmap file name is mandatory\n");
        return AVERROR(EINVAL);
    }

    /* Load our mask image. */
    if ((ret = load_mask(&removelogo->full_mask_data, &w, &h, removelogo->filename, ctx)) < 0)
        return ret;
    removelogo->mask_w = w;
    removelogo->mask_h = h;

    convert_mask_to_strength_mask(removelogo->full_mask_data, w, w, h,
                                  16, &full_max_mask_size);

    /* Create the scaled down mask image for the chroma planes. */
    if (!(removelogo->half_mask_data = av_mallocz(w/2 * h/2)))
        return AVERROR(ENOMEM);
    generate_half_size_image(removelogo->full_mask_data, w,
                             removelogo->half_mask_data, w/2,
                             w, h, &half_max_mask_size);

    removelogo->max_mask_size = FFMAX(full_max_mask_size, half_max_mask_size);

    /* Create a circular mask for each size up to max_mask_size. When
       the filter is applied, the mask size is determined on a pixel
       by pixel basis, with pixels nearer the edge of the logo getting
       smaller mask sizes. */
    mask = (int ***)av_malloc(sizeof(int **) * (removelogo->max_mask_size + 1));
    if (!mask)
        return AVERROR(ENOMEM);

    for (a = 0; a <= removelogo->max_mask_size; a++) {
        mask[a] = (int **)av_malloc(sizeof(int *) * ((a * 2) + 1));
        if (!mask[a])
            return AVERROR(ENOMEM);
        for (b = -a; b <= a; b++) {
            mask[a][b + a] = (int *)av_malloc(sizeof(int) * ((a * 2) + 1));
            if (!mask[a][b + a])
                return AVERROR(ENOMEM);
            for (c = -a; c <= a; c++) {
                if ((b * b) + (c * c) <= (a * a)) /* Circular 0/1 mask. */
                    mask[a][b + a][c + a] = 1;
                else
                    mask[a][b + a][c + a] = 0;
            }
        }
    }
    removelogo->mask = mask;

    /* Calculate our bounding rectangles, which determine in what
     * region the logo resides for faster processing. */
    ff_calculate_bounding_box(&removelogo->full_mask_bbox, removelogo->full_mask_data, w, w, h, 0);
    ff_calculate_bounding_box(&removelogo->half_mask_bbox, removelogo->half_mask_data, w/2, w/2, h/2, 0);

#define SHOW_LOGO_INFO(mask_type)                                       \
    av_log(ctx, AV_LOG_VERBOSE, #mask_type " x1:%d x2:%d y1:%d y2:%d max_mask_size:%d\n", \
           removelogo->mask_type##_mask_bbox.x1, removelogo->mask_type##_mask_bbox.x2, \
           removelogo->mask_type##_mask_bbox.y1, removelogo->mask_type##_mask_bbox.y2, \
           mask_type##_max_mask_size);
    SHOW_LOGO_INFO(full);
    SHOW_LOGO_INFO(half);

    return 0;
}

static int config_props_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    RemovelogoContext *removelogo = ctx->priv;

    if (inlink->w != removelogo->mask_w || inlink->h != removelogo->mask_h) {
        av_log(ctx, AV_LOG_INFO,
               "Mask image size %dx%d does not match with the input video size %dx%d\n",
               removelogo->mask_w, removelogo->mask_h, inlink->w, inlink->h);
        return AVERROR(EINVAL);
    }

    return 0;
}

/**
 * Blur image.
 *
 * It takes a pixel that is inside the mask and blurs it. It does so
 * by finding the average of all the pixels within the mask and
 * outside of the mask.
 *
 * @param mask_data  the mask plane to use for averaging
 * @param image_data the image plane to blur
 * @param w width of the image
 * @param h height of the image
 * @param x x-coordinate of the pixel to blur
 * @param y y-coordinate of the pixel to blur
 */
static unsigned int blur_pixel(int ***mask,
                               const uint8_t *mask_data, int mask_linesize,
                               uint8_t       *image_data, int image_linesize,
                               int w, int h, int x, int y)
{
    /* Mask size tells how large a circle to use. The radius is about
     * (slightly larger than) mask size. */
    int mask_size;
    int start_posx, start_posy, end_posx, end_posy;
    int i, j;
    unsigned int accumulator = 0, divisor = 0;
    /* What pixel we are reading out of the circular blur mask. */
    const uint8_t *image_read_position;
    /* What pixel we are reading out of the filter image. */
    const uint8_t *mask_read_position;

    /* Prepare our bounding rectangle and clip it if need be. */
    mask_size  = mask_data[y * mask_linesize + x];
    start_posx = FFMAX(0, x - mask_size);
    start_posy = FFMAX(0, y - mask_size);
    end_posx   = FFMIN(w - 1, x + mask_size);
    end_posy   = FFMIN(h - 1, y + mask_size);

    image_read_position = image_data + image_linesize * start_posy + start_posx;
    mask_read_position  = mask_data  + mask_linesize  * start_posy + start_posx;

    for (j = start_posy; j <= end_posy; j++) {
        for (i = start_posx; i <= end_posx; i++) {
            /* Check if this pixel is in the mask or not. Only use the
             * pixel if it is not. */
            if (!(*mask_read_position) && mask[mask_size][i - start_posx][j - start_posy]) {
                accumulator += *image_read_position;
                divisor++;
            }

            image_read_position++;
            mask_read_position++;
        }

        image_read_position += (image_linesize - ((end_posx + 1) - start_posx));
        mask_read_position  += (mask_linesize - ((end_posx + 1) - start_posx));
    }

    /* If divisor is 0, it means that not a single pixel is outside of
       the logo, so we have no data.  Else we need to normalise the
       data using the divisor. */
    return divisor == 0 ? 255:
        (accumulator + (divisor / 2)) / divisor;  /* divide, taking into account average rounding error */
}

/**
 * Blur image plane using a mask.
 *
 * @param source The image to have it's logo removed.
 * @param destination Where the output image will be stored.
 * @param source_stride How far apart (in memory) two consecutive lines are.
 * @param destination Same as source_stride, but for the destination image.
 * @param width Width of the image. This is the same for source and destination.
 * @param height Height of the image. This is the same for source and destination.
 * @param is_image_direct If the image is direct, then source and destination are
 *        the same and we can save a lot of time by not copying pixels that
 *        haven't changed.
 * @param filter The image that stores the distance to the edge of the logo for
 *        each pixel.
 * @param logo_start_x smallest x-coordinate that contains at least 1 logo pixel.
 * @param logo_start_y smallest y-coordinate that contains at least 1 logo pixel.
 * @param logo_end_x   largest x-coordinate that contains at least 1 logo pixel.
 * @param logo_end_y   largest y-coordinate that contains at least 1 logo pixel.
 *
 * This function processes an entire plane. Pixels outside of the logo are copied
 * to the output without change, and pixels inside the logo have the de-blurring
 * function applied.
 */
static void blur_image(int ***mask,
                       const uint8_t *src_data,  int src_linesize,
                             uint8_t *dst_data,  int dst_linesize,
                       const uint8_t *mask_data, int mask_linesize,
                       int w, int h, int direct,
                       FFBoundingBox *bbox)
{
    int x, y;
    uint8_t *dst_line;
    const uint8_t *src_line;

    if (!direct)
        av_image_copy_plane(dst_data, dst_linesize, src_data, src_linesize, w, h);

    for (y = bbox->y1; y <= bbox->y2; y++) {
        src_line = src_data + src_linesize * y;
        dst_line = dst_data + dst_linesize * y;

        for (x = bbox->x1; x <= bbox->x2; x++) {
             if (mask_data[y * mask_linesize + x]) {
                /* Only process if we are in the mask. */
                 dst_line[x] = blur_pixel(mask,
                                          mask_data, mask_linesize,
                                          dst_data, dst_linesize,
                                          w, h, x, y);
            } else {
                /* Else just copy the data. */
                if (!direct)
                    dst_line[x] = src_line[x];
            }
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    RemovelogoContext *removelogo = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outpicref;
    int direct = 0;

    if (av_frame_is_writable(inpicref)) {
        direct = 1;
        outpicref = inpicref;
    } else {
        outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!outpicref) {
            av_frame_free(&inpicref);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(outpicref, inpicref);
    }

    blur_image(removelogo->mask,
               inpicref ->data[0], inpicref ->linesize[0],
               outpicref->data[0], outpicref->linesize[0],
               removelogo->full_mask_data, inlink->w,
               inlink->w, inlink->h, direct, &removelogo->full_mask_bbox);
    blur_image(removelogo->mask,
               inpicref ->data[1], inpicref ->linesize[1],
               outpicref->data[1], outpicref->linesize[1],
               removelogo->half_mask_data, inlink->w/2,
               inlink->w/2, inlink->h/2, direct, &removelogo->half_mask_bbox);
    blur_image(removelogo->mask,
               inpicref ->data[2], inpicref ->linesize[2],
               outpicref->data[2], outpicref->linesize[2],
               removelogo->half_mask_data, inlink->w/2,
               inlink->w/2, inlink->h/2, direct, &removelogo->half_mask_bbox);

    if (!direct)
        av_frame_free(&inpicref);

    return ff_filter_frame(outlink, outpicref);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    RemovelogoContext *removelogo = ctx->priv;
    int a, b;

    av_freep(&removelogo->full_mask_data);
    av_freep(&removelogo->half_mask_data);

    if (removelogo->mask) {
        /* Loop through each mask. */
        for (a = 0; a <= removelogo->max_mask_size; a++) {
            /* Loop through each scanline in a mask. */
            for (b = -a; b <= a; b++) {
                av_free(removelogo->mask[a][b + a]); /* Free a scanline. */
            }
            av_free(removelogo->mask[a]);
        }
        /* Free the array of pointers pointing to the masks. */
        av_freep(&removelogo->mask);
    }
}

static const AVFilterPad removelogo_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .config_props     = config_props_input,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

static const AVFilterPad removelogo_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_removelogo = {
    .name          = "removelogo",
    .description   = NULL_IF_CONFIG_SMALL("Remove a TV logo based on a mask image."),
    .priv_size     = sizeof(RemovelogoContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = removelogo_inputs,
    .outputs       = removelogo_outputs,
    .priv_class    = &removelogo_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE,
};
