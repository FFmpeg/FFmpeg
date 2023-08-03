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

#include "libavutil/avassert.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"

enum {
    X, Y, W, H,
    NB_PARAMS,
};
static const char addroi_param_names[] = {
    'x', 'y', 'w', 'h',
};

enum {
    VAR_IW,
    VAR_IH,
    NB_VARS,
};
static const char *const addroi_var_names[] = {
    "iw",
    "ih",
};

typedef struct AddROIContext {
    const AVClass *class;

    char   *region_str[NB_PARAMS];
    AVExpr *region_expr[NB_PARAMS];

    int region[NB_PARAMS];
    AVRational qoffset;

    int clear;
} AddROIContext;

static int addroi_config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    AddROIContext     *ctx = avctx->priv;
    int i;
    double vars[NB_VARS];
    double val;

    vars[VAR_IW] = inlink->w;
    vars[VAR_IH] = inlink->h;

    for (i = 0; i < NB_PARAMS; i++) {
        int max_value;
        switch (i) {
        case X: max_value = inlink->w;                  break;
        case Y: max_value = inlink->h;                  break;
        case W: max_value = inlink->w - ctx->region[X]; break;
        case H: max_value = inlink->h - ctx->region[Y]; break;
        }

        val = av_expr_eval(ctx->region_expr[i], vars, NULL);
        if (val < 0.0) {
            av_log(avctx, AV_LOG_WARNING, "Calculated value %g for %c is "
                   "less than zero - using zero instead.\n", val,
                   addroi_param_names[i]);
            val = 0.0;
        } else if (val > max_value) {
            av_log(avctx, AV_LOG_WARNING, "Calculated value %g for %c is "
                   "greater than maximum allowed value %d - "
                   "using %d instead.\n", val, addroi_param_names[i],
                   max_value, max_value);
            val = max_value;
        }
        ctx->region[i] = val;
    }

    return 0;
}

static int addroi_filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *avctx = inlink->dst;
    AVFilterLink  *outlink = avctx->outputs[0];
    AddROIContext     *ctx = avctx->priv;
    AVRegionOfInterest *roi;
    AVFrameSideData *sd;
    int err;

    if (ctx->clear) {
        av_frame_remove_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
        sd = NULL;
    } else {
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
    }
    if (sd) {
        const AVRegionOfInterest *old_roi;
        uint32_t old_roi_size;
        AVBufferRef *roi_ref;
        int nb_roi, i;

        old_roi = (const AVRegionOfInterest*)sd->data;
        old_roi_size = old_roi->self_size;
        av_assert0(old_roi_size && sd->size % old_roi_size == 0);
        nb_roi = sd->size / old_roi_size + 1;

        roi_ref = av_buffer_alloc(sizeof(*roi) * nb_roi);
        if (!roi_ref) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        roi = (AVRegionOfInterest*)roi_ref->data;

        for (i = 0; i < nb_roi - 1; i++) {
            old_roi = (const AVRegionOfInterest*)
                (sd->data + old_roi_size * i);

            roi[i] = (AVRegionOfInterest) {
                .self_size = sizeof(*roi),
                .top       = old_roi->top,
                .bottom    = old_roi->bottom,
                .left      = old_roi->left,
                .right     = old_roi->right,
                .qoffset   = old_roi->qoffset,
            };
        }

        roi[nb_roi - 1] = (AVRegionOfInterest) {
            .self_size = sizeof(*roi),
            .top       = ctx->region[Y],
            .bottom    = ctx->region[Y] + ctx->region[H],
            .left      = ctx->region[X],
            .right     = ctx->region[X] + ctx->region[W],
            .qoffset   = ctx->qoffset,
        };

        av_frame_remove_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);

        sd = av_frame_new_side_data_from_buf(frame,
                                             AV_FRAME_DATA_REGIONS_OF_INTEREST,
                                             roi_ref);
        if (!sd) {
            av_buffer_unref(&roi_ref);
            err = AVERROR(ENOMEM);
            goto fail;
        }

    } else {
        sd = av_frame_new_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST,
                                    sizeof(AVRegionOfInterest));
        if (!sd) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        roi = (AVRegionOfInterest*)sd->data;
        *roi = (AVRegionOfInterest) {
            .self_size = sizeof(*roi),
            .top       = ctx->region[Y],
            .bottom    = ctx->region[Y] + ctx->region[H],
            .left      = ctx->region[X],
            .right     = ctx->region[X] + ctx->region[W],
            .qoffset   = ctx->qoffset,
        };
    }

    return ff_filter_frame(outlink, frame);

fail:
    av_frame_free(&frame);
    return err;
}

static av_cold int addroi_init(AVFilterContext *avctx)
{
    AddROIContext *ctx = avctx->priv;
    int i, err;

    for (i = 0; i < NB_PARAMS; i++) {
        err = av_expr_parse(&ctx->region_expr[i], ctx->region_str[i],
                            addroi_var_names, NULL, NULL, NULL, NULL,
                            0, avctx);
        if (err < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error parsing %c expression '%s'.\n",
                   addroi_param_names[i], ctx->region_str[i]);
            return err;
        }
    }

    return 0;
}

static av_cold void addroi_uninit(AVFilterContext *avctx)
{
    AddROIContext *ctx = avctx->priv;
    int i;

    for (i = 0; i < NB_PARAMS; i++) {
        av_expr_free(ctx->region_expr[i]);
        ctx->region_expr[i] = NULL;
    }
}

#define OFFSET(x) offsetof(AddROIContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption addroi_options[] = {
    { "x", "Region distance from left edge of frame.",
      OFFSET(region_str[X]), AV_OPT_TYPE_STRING, { .str = "0" }, .flags = FLAGS },
    { "y", "Region distance from top edge of frame.",
      OFFSET(region_str[Y]), AV_OPT_TYPE_STRING, { .str = "0" }, .flags = FLAGS },
    { "w", "Region width.",
      OFFSET(region_str[W]), AV_OPT_TYPE_STRING, { .str = "0" }, .flags = FLAGS },
    { "h", "Region height.",
      OFFSET(region_str[H]), AV_OPT_TYPE_STRING, { .str = "0" }, .flags = FLAGS },

    { "qoffset", "Quantisation offset to apply in the region.",
      OFFSET(qoffset), AV_OPT_TYPE_RATIONAL, { .dbl = -0.1 }, -1, +1, FLAGS },

    { "clear", "Remove any existing regions of interest before adding the new one.",
      OFFSET(clear), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

    { NULL }
};

AVFILTER_DEFINE_CLASS(addroi);

static const AVFilterPad addroi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = addroi_config_input,
        .filter_frame = addroi_filter_frame,
    },
};

const AVFilter ff_vf_addroi = {
    .name        = "addroi",
    .description = NULL_IF_CONFIG_SMALL("Add region of interest to frame."),
    .init        = addroi_init,
    .uninit      = addroi_uninit,

    .priv_size   = sizeof(AddROIContext),
    .priv_class  = &addroi_class,

    .flags       = AVFILTER_FLAG_METADATA_ONLY,

    FILTER_INPUTS(addroi_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
};
