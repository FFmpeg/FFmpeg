/*
 * Copyright (c) 2013 Georg Martius <georg dot martius at web dot de>
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

#define DEFAULT_RESULT_NAME     "transforms.trf"

#include <vid.stab/libvidstab.h>

#include "libavutil/common.h"
#include "libavutil/file_open.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

#include "vidstabutils.h"

typedef struct StabData {
    const AVClass *class;

    VSMotionDetect md;
    VSMotionDetectConfig conf;

    char *result;
    FILE *f;
} StabData;


#define OFFSET(x) offsetof(StabData, x)
#define OFFSETC(x) (offsetof(StabData, conf)+offsetof(VSMotionDetectConfig, x))
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption vidstabdetect_options[] = {
    {"result",      "path to the file used to write the transforms",                 OFFSET(result),             AV_OPT_TYPE_STRING, {.str = DEFAULT_RESULT_NAME}, .flags = FLAGS},
    {"shakiness",   "how shaky is the video and how quick is the camera?"
                    " 1: little (fast) 10: very strong/quick (slow)",                OFFSETC(shakiness),         AV_OPT_TYPE_INT,    {.i64 = 5},      1,  10, FLAGS},
    {"accuracy",    "(>=shakiness) 1: low 15: high (slow)",                          OFFSETC(accuracy),          AV_OPT_TYPE_INT,    {.i64 = 15},     1,  15, FLAGS},
    {"stepsize",    "region around minimum is scanned with 1 pixel resolution",      OFFSETC(stepSize),          AV_OPT_TYPE_INT,    {.i64 = 6},      1,  32, FLAGS},
    {"mincontrast", "below this contrast a field is discarded (0-1)",                OFFSETC(contrastThreshold), AV_OPT_TYPE_DOUBLE, {.dbl = 0.25}, 0.0, 1.0, FLAGS},
    {"show",        "0: draw nothing; 1,2: show fields and transforms",              OFFSETC(show),              AV_OPT_TYPE_INT,    {.i64 = 0},      0,   2, FLAGS},
    {"tripod",      "virtual tripod mode (if >0): motion is compared to a reference"
                    " reference frame (frame # is the value)",                       OFFSETC(virtualTripod),     AV_OPT_TYPE_INT,    {.i64 = 0}, 0, INT_MAX, FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(vidstabdetect);

static av_cold int init(AVFilterContext *ctx)
{
    StabData *s = ctx->priv;
    ff_vs_init();
    s->class = &vidstabdetect_class;
    av_log(ctx, AV_LOG_VERBOSE, "vidstabdetect filter: init %s\n", LIBVIDSTAB_VERSION);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    StabData *s = ctx->priv;
    VSMotionDetect *md = &(s->md);

    if (s->f) {
        fclose(s->f);
        s->f = NULL;
    }

    vsMotionDetectionCleanup(md);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    StabData *s = ctx->priv;

    VSMotionDetect* md = &(s->md);
    VSFrameInfo fi;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int is_planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;

    vsFrameInfoInit(&fi, inlink->w, inlink->h,
                    ff_av2vs_pixfmt(ctx, inlink->format));
    if (!is_planar && fi.bytesPerPixel != av_get_bits_per_pixel(desc)/8) {
        av_log(ctx, AV_LOG_ERROR, "pixel-format error: wrong bits/per/pixel, please report a BUG");
        return AVERROR(EINVAL);
    }
    if (fi.log2ChromaW != desc->log2_chroma_w) {
        av_log(ctx, AV_LOG_ERROR, "pixel-format error: log2_chroma_w, please report a BUG");
        return AVERROR(EINVAL);
    }

    if (fi.log2ChromaH != desc->log2_chroma_h) {
        av_log(ctx, AV_LOG_ERROR, "pixel-format error: log2_chroma_h, please report a BUG");
        return AVERROR(EINVAL);
    }

    // set values that are not initialized by the options
    s->conf.algo     = 1;
    s->conf.modName  = "vidstabdetect";
    if (vsMotionDetectInit(md, &s->conf, &fi) != VS_OK) {
        av_log(ctx, AV_LOG_ERROR, "initialization of Motion Detection failed, please report a BUG");
        return AVERROR(EINVAL);
    }

    vsMotionDetectGetConfig(&s->conf, md);
    av_log(ctx, AV_LOG_INFO, "Video stabilization settings (pass 1/2):\n");
    av_log(ctx, AV_LOG_INFO, "     shakiness = %d\n", s->conf.shakiness);
    av_log(ctx, AV_LOG_INFO, "      accuracy = %d\n", s->conf.accuracy);
    av_log(ctx, AV_LOG_INFO, "      stepsize = %d\n", s->conf.stepSize);
    av_log(ctx, AV_LOG_INFO, "   mincontrast = %f\n", s->conf.contrastThreshold);
    av_log(ctx, AV_LOG_INFO, "        tripod = %d\n", s->conf.virtualTripod);
    av_log(ctx, AV_LOG_INFO, "          show = %d\n", s->conf.show);
    av_log(ctx, AV_LOG_INFO, "        result = %s\n", s->result);

    s->f = avpriv_fopen_utf8(s->result, "w");
    if (s->f == NULL) {
        av_log(ctx, AV_LOG_ERROR, "cannot open transform file %s\n", s->result);
        return AVERROR(EINVAL);
    } else {
        if (vsPrepareFile(md, s->f) != VS_OK) {
            av_log(ctx, AV_LOG_ERROR, "cannot write to transform file %s\n", s->result);
            return AVERROR(EINVAL);
        }
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    StabData *s = ctx->priv;
    VSMotionDetect *md = &(s->md);
    LocalMotions localmotions;

    AVFilterLink *outlink = inlink->dst->outputs[0];
    VSFrame frame;
    int plane, ret;

    if (s->conf.show > 0 && !av_frame_is_writable(in)) {
        ret = ff_inlink_make_frame_writable(inlink, &in);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }
    }

    for (plane = 0; plane < md->fi.planes; plane++) {
        frame.data[plane] = in->data[plane];
        frame.linesize[plane] = in->linesize[plane];
    }
    if (vsMotionDetection(md, &localmotions, &frame) != VS_OK) {
        av_log(ctx, AV_LOG_ERROR, "motion detection failed");
        return AVERROR(AVERROR_EXTERNAL);
    } else {
        if (vsWriteToFile(md, s->f, &localmotions) != VS_OK) {
            int ret = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "cannot write to transform file");
            return ret;
        }
        vs_vector_del(&localmotions);
    }

    return ff_filter_frame(outlink, in);
}

static const AVFilterPad avfilter_vf_vidstabdetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad avfilter_vf_vidstabdetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_vidstabdetect = {
    .name          = "vidstabdetect",
    .description   = NULL_IF_CONFIG_SMALL("Extract relative transformations, "
                                          "pass 1 of 2 for stabilization "
                                          "(see vidstabtransform for pass 2)."),
    .priv_size     = sizeof(StabData),
    .init          = init,
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(avfilter_vf_vidstabdetect_inputs),
    FILTER_OUTPUTS(avfilter_vf_vidstabdetect_outputs),
    FILTER_PIXFMTS_ARRAY(ff_vidstab_pix_fmts),
    .priv_class    = &vidstabdetect_class,
};
