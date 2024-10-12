/*
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
 * @file QR decoder video filter
 *
 * Use libquirc library to decode the content of QR codes, and put the decoded
 * content to metadata. See:
 * https://github.com/dlbeer/quirc
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"
#include <quirc.h>

typedef struct QuircContext {
    const AVClass *class;

    struct quirc *quirc;
} QuircContext;

static av_cold int init(AVFilterContext *ctx)
{
    QuircContext *quirc = ctx->priv;

    quirc->quirc = quirc_new();
    if (!quirc->quirc) {
        return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    QuircContext *quirc = ctx->priv;

    quirc_destroy(quirc->quirc);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    QuircContext *quirc = ctx->priv;
    int err;

    err = quirc_resize(quirc->quirc, inlink->w, inlink->h);
    if (err == -1) {
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    QuircContext *quirc = ctx->priv;
    int codes_count;
    uint8_t *image;

    /* copy input image to quirc buffer */
    image = quirc_begin(quirc->quirc, NULL, NULL);
    av_image_copy_plane(image, inlink->w,
                        frame->data[0], frame->linesize[0], inlink->w, inlink->h);

    quirc_end(quirc->quirc);

    codes_count = quirc_count(quirc->quirc);
    av_log(ctx, AV_LOG_VERBOSE,
           "Found count %d codes in image #%ld\n", codes_count, inl->frame_count_out);

    if (codes_count) {
        int i, j;
        AVDictionary **metadata = &frame->metadata;

        av_dict_set_int(metadata, "lavfi.quirc.count", codes_count, 0);

        for (i = 0; i < codes_count; i++) {
            struct quirc_code code;
            struct quirc_data data;
            quirc_decode_error_t err;
            char metadata_key[64];

            quirc_extract(quirc->quirc, i, &code);

            err = quirc_decode(&code, &data);
            if (err) {
                av_log(ctx, AV_LOG_WARNING,
                       "Failed to decode image: %s\n", quirc_strerror(err));
                continue;
            }

            for (j = 0; j < 4; j++) {
                struct quirc_point corner = code.corners[j];

#define SET_CORNER_METADATA(key_, value_)                               \
                snprintf(metadata_key, sizeof(metadata_key)-1,          \
                         "lavfi.quirc.%d.corner.%d." #key_, i, j);      \
                av_dict_set_int(metadata, metadata_key, value_, 0)

                SET_CORNER_METADATA(x, corner.x);
                SET_CORNER_METADATA(y, corner.y);
            }

            snprintf(metadata_key, sizeof(metadata_key)-1, "lavfi.quirc.%d.payload", i); \
            av_dict_set(metadata, metadata_key, data.payload, 0);

            av_log(ctx, AV_LOG_INFO,
                   "Found QR code at position %d,%d - %d,%d with payload: %s\n",
                   code.corners[0].x, code.corners[0].y,
                   code.corners[3].x, code.corners[3].y, data.payload);
        }
    }

    return ff_filter_frame(outlink, frame);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NV12, AV_PIX_FMT_NV21,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_NONE
};

static const AVClass quirc_class = {
    .class_name = "quirc",
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER
};

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input
    },
};

const AVFilter ff_vf_quirc = {
    .name        = "quirc",
    .description = NULL_IF_CONFIG_SMALL("Decode and show QR codes content."),
    .priv_size   = sizeof(QuircContext),
    .priv_class  = &quirc_class,
    .init        = init,
    .uninit      = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                   AVFILTER_FLAG_METADATA_ONLY,
};

