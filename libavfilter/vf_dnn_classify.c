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
 * @file
 * implementing an classification filter using deep learning networks.
 */

#include "libavutil/file_open.h"
#include "libavutil/opt.h"
#include "filters.h"
#include "dnn_filter_common.h"
#include "internal.h"
#include "video.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/detection_bbox.h"

typedef struct DnnClassifyContext {
    const AVClass *class;
    DnnContext dnnctx;
    float confidence;
    char *labels_filename;
    char *target;
    char **labels;
    int label_count;
} DnnClassifyContext;

#define OFFSET(x) offsetof(DnnClassifyContext, dnnctx.x)
#define OFFSET2(x) offsetof(DnnClassifyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption dnn_classify_options[] = {
    { "dnn_backend", "DNN backend",                OFFSET(backend_type),     AV_OPT_TYPE_INT,       { .i64 = DNN_OV },    INT_MIN, INT_MAX, FLAGS, .unit = "backend" },
#if (CONFIG_LIBOPENVINO == 1)
    { "openvino",    "openvino backend flag",      0,                        AV_OPT_TYPE_CONST,     { .i64 = DNN_OV },    0, 0, FLAGS, .unit = "backend" },
#endif
    DNN_COMMON_OPTIONS
    { "confidence",  "threshold of confidence",    OFFSET2(confidence),      AV_OPT_TYPE_FLOAT,     { .dbl = 0.5 },  0, 1, FLAGS},
    { "labels",      "path to labels file",        OFFSET2(labels_filename), AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { "target",      "which one to be classified", OFFSET2(target),          AV_OPT_TYPE_STRING,    { .str = NULL }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dnn_classify);

static int dnn_classify_post_proc(AVFrame *frame, DNNData *output, uint32_t bbox_index, AVFilterContext *filter_ctx)
{
    DnnClassifyContext *ctx = filter_ctx->priv;
    float conf_threshold = ctx->confidence;
    AVDetectionBBoxHeader *header;
    AVDetectionBBox *bbox;
    float *classifications;
    uint32_t label_id;
    float confidence;
    AVFrameSideData *sd;
    int output_size = output->dims[3] * output->dims[2] * output->dims[1];
    if (output_size <= 0) {
        return -1;
    }

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
    if (!sd) {
        av_log(filter_ctx, AV_LOG_ERROR, "Cannot get side data in dnn_classify_post_proc\n");
        return -1;
    }
    header = (AVDetectionBBoxHeader *)sd->data;

    if (bbox_index == 0) {
        av_strlcat(header->source, ", ", sizeof(header->source));
        av_strlcat(header->source, ctx->dnnctx.model_filename, sizeof(header->source));
    }

    classifications = output->data;
    label_id = 0;
    confidence= classifications[0];
    for (int i = 1; i < output_size; i++) {
        if (classifications[i] > confidence) {
            label_id = i;
            confidence= classifications[i];
        }
    }

    if (confidence < conf_threshold) {
        return 0;
    }

    bbox = av_get_detection_bbox(header, bbox_index);
    bbox->classify_confidences[bbox->classify_count] = av_make_q((int)(confidence * 10000), 10000);

    if (ctx->labels && label_id < ctx->label_count) {
        av_strlcpy(bbox->classify_labels[bbox->classify_count], ctx->labels[label_id], sizeof(bbox->classify_labels[bbox->classify_count]));
    } else {
        snprintf(bbox->classify_labels[bbox->classify_count], sizeof(bbox->classify_labels[bbox->classify_count]), "%d", label_id);
    }

    bbox->classify_count++;

    return 0;
}

static void free_classify_labels(DnnClassifyContext *ctx)
{
    for (int i = 0; i < ctx->label_count; i++) {
        av_freep(&ctx->labels[i]);
    }
    ctx->label_count = 0;
    av_freep(&ctx->labels);
}

static int read_classify_label_file(AVFilterContext *context)
{
    int line_len;
    FILE *file;
    DnnClassifyContext *ctx = context->priv;

    file = avpriv_fopen_utf8(ctx->labels_filename, "r");
    if (!file){
        av_log(context, AV_LOG_ERROR, "failed to open file %s\n", ctx->labels_filename);
        return AVERROR(EINVAL);
    }

    while (!feof(file)) {
        char *label;
        char buf[256];
        if (!fgets(buf, 256, file)) {
            break;
        }

        line_len = strlen(buf);
        while (line_len) {
            int i = line_len - 1;
            if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ') {
                buf[i] = '\0';
                line_len--;
            } else {
                break;
            }
        }

        if (line_len == 0)  // empty line
            continue;

        if (line_len >= AV_DETECTION_BBOX_LABEL_NAME_MAX_SIZE) {
            av_log(context, AV_LOG_ERROR, "label %s too long\n", buf);
            fclose(file);
            return AVERROR(EINVAL);
        }

        label = av_strdup(buf);
        if (!label) {
            av_log(context, AV_LOG_ERROR, "failed to allocate memory for label %s\n", buf);
            fclose(file);
            return AVERROR(ENOMEM);
        }

        if (av_dynarray_add_nofree(&ctx->labels, &ctx->label_count, label) < 0) {
            av_log(context, AV_LOG_ERROR, "failed to do av_dynarray_add\n");
            fclose(file);
            av_freep(&label);
            return AVERROR(ENOMEM);
        }
    }

    fclose(file);
    return 0;
}

static av_cold int dnn_classify_init(AVFilterContext *context)
{
    DnnClassifyContext *ctx = context->priv;
    int ret = ff_dnn_init(&ctx->dnnctx, DFT_ANALYTICS_CLASSIFY, context);
    if (ret < 0)
        return ret;
    ff_dnn_set_classify_post_proc(&ctx->dnnctx, dnn_classify_post_proc);

    if (ctx->labels_filename) {
        return read_classify_label_file(context);
    }
    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAYF32,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NONE
};

static int dnn_classify_flush_frame(AVFilterLink *outlink, int64_t pts, int64_t *out_pts)
{
    DnnClassifyContext *ctx = outlink->src->priv;
    int ret;
    DNNAsyncStatusType async_state;

    ret = ff_dnn_flush(&ctx->dnnctx);
    if (ret != 0) {
        return -1;
    }

    do {
        AVFrame *in_frame = NULL;
        AVFrame *out_frame = NULL;
        async_state = ff_dnn_get_result(&ctx->dnnctx, &in_frame, &out_frame);
        if (async_state == DAST_SUCCESS) {
            ret = ff_filter_frame(outlink, in_frame);
            if (ret < 0)
                return ret;
            if (out_pts)
                *out_pts = in_frame->pts + pts;
        }
        av_usleep(5000);
    } while (async_state >= DAST_NOT_READY);

    return 0;
}

static int dnn_classify_activate(AVFilterContext *filter_ctx)
{
    AVFilterLink *inlink = filter_ctx->inputs[0];
    AVFilterLink *outlink = filter_ctx->outputs[0];
    DnnClassifyContext *ctx = filter_ctx->priv;
    AVFrame *in = NULL;
    int64_t pts;
    int ret, status;
    int got_frame = 0;
    int async_state;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    do {
        // drain all input frames
        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            if (ff_dnn_execute_model_classification(&ctx->dnnctx, in, NULL, ctx->target) != 0) {
                return AVERROR(EIO);
            }
        }
    } while (ret > 0);

    // drain all processed frames
    do {
        AVFrame *in_frame = NULL;
        AVFrame *out_frame = NULL;
        async_state = ff_dnn_get_result(&ctx->dnnctx, &in_frame, &out_frame);
        if (async_state == DAST_SUCCESS) {
            ret = ff_filter_frame(outlink, in_frame);
            if (ret < 0)
                return ret;
            got_frame = 1;
        }
    } while (async_state == DAST_SUCCESS);

    // if frame got, schedule to next filter
    if (got_frame)
        return 0;

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF) {
            int64_t out_pts = pts;
            ret = dnn_classify_flush_frame(outlink, pts, &out_pts);
            ff_outlink_set_status(outlink, status, out_pts);
            return ret;
        }
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return 0;
}

static av_cold void dnn_classify_uninit(AVFilterContext *context)
{
    DnnClassifyContext *ctx = context->priv;
    ff_dnn_uninit(&ctx->dnnctx);
    free_classify_labels(ctx);
}

const AVFilter ff_vf_dnn_classify = {
    .name          = "dnn_classify",
    .description   = NULL_IF_CONFIG_SMALL("Apply DNN classify filter to the input."),
    .priv_size     = sizeof(DnnClassifyContext),
    .init          = dnn_classify_init,
    .uninit        = dnn_classify_uninit,
    FILTER_INPUTS(ff_video_default_filterpad),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &dnn_classify_class,
    .activate      = dnn_classify_activate,
};
